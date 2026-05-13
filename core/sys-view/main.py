import asyncio
import os
import logging
import json
import socket
import threading
import graphviz
from contextlib import asynccontextmanager
from typing import List, Optional, Dict, Any
from enum import Enum

from fastapi import FastAPI, HTTPException, Response, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field, ValidationError
from pymongo import MongoClient

logging.basicConfig(
    level=logging.INFO, 
    format="%(asctime)s [%(levelname)s] %(message)s"
)
logger = logging.getLogger("UdpReceiver")

# DATABASE SETUP
MONGO_URI = os.getenv("MONGO_URI", "mongodb://localhost:37017")
DB_NAME = "deployment_db"
client = MongoClient(MONGO_URI)
db = client[DB_NAME]

networks_col = db["networks"]
packages_col = db["packages"]

# PYDANTIC MODELS
class ReportStatus(str, Enum):
    SUCCESS = "SUCCESS"
    SKIPPED_DUPLICATE = "SKIPPED_DUPLICATE"
    FAILED = "FAILED"
    PARTIAL = "PARTIAL"
    CRIT_PHASE_1 = "CRITICAL_FAILURE_PHASE_1"
    CRITICAL = "CRITICAL_FAILURE"

class PackageMetadata(BaseModel):
    Package: str
    Version: str
    Type: Optional[str] = ""
    Architecture: str
    Store_Path: Optional[str] = ""
    Dependencies: List[List[str]] = []
    SHA256: str
    Installed_Size: int = Field(0, alias="Installed-Size")
    Size: int = 0
    Filename: str
    Latest: bool = False

class NetworkInfo(BaseModel):
    net_id: str
    network_name: str
    subnet: Optional[str] = "Unknown"

class PackageReport(BaseModel):
    sha256: Optional[str] = None
    filename: Optional[str] = None
    status: str
    error_message: Optional[str] = None
    metadata: Optional[PackageMetadata] = None

class UpdateReport(BaseModel):
    bundle_name: Optional[str] = None
    timestamp: Optional[int] = None
    overall_status: Optional[str] = None
    error_message: Optional[str] = None
    packages: List[PackageReport] = []

class ReportPayload(BaseModel):
    bundle_name: Optional[str] = None
    timestamp: Optional[int] = None
    overall_status: Optional[str] = None
    error_message: Optional[str] = None
    network: Optional[NetworkInfo] = None
    packages: List[PackageReport] = []
    updates: List[UpdateReport] = []


# REQUEST MODELS
class RemovePackageReq(BaseModel):
    sha256: str

class RemoveNetworkReq(BaseModel):
    net_id: str


# WEBSOCKET CONNECTION MANAGER
class ConnectionManager:
    def __init__(self):
        self.active_connections: list[WebSocket] = []

    async def connect(self, websocket: WebSocket):
        await websocket.accept()
        self.active_connections.append(websocket)

    def disconnect(self, websocket: WebSocket):
        self.active_connections.remove(websocket)

    async def broadcast(self, message: dict):
        """Sends a JSON message to all connected web browsers."""
        for connection in self.active_connections:
            try:
                await connection.send_json(message)
            except RuntimeError:
                pass

manager = ConnectionManager()


# THE PROCESSOR LOGIC
def process_report(report_json: dict, loop: asyncio.AbstractEventLoop):
    """Parses the incoming report using Pydantic, updates Mongo, and notifies the frontend."""

    try:
        report = ReportPayload.model_validate(report_json)
    except ValidationError as e:
        logger.error(f"Validation error for incoming report: {e}")
        return

    if not report.network or not report.network.net_id:
        logger.warning("Report missing valid network_name or net_id.")
        return
        
    net_id = report.network.net_id
    network_name = report.network.network_name
    subnet = report.network.subnet
    
    logging.info(f"Received report from network {net_id} ({network_name}) - Status: {report.overall_status}")

    network_package_updates = {}
    new_packages_ui = []  
    
    for pkg in report.packages:
        pkg_sha256 = pkg.sha256 or (pkg.metadata.SHA256 if pkg.metadata else "UNKNOWN_SHA")
        if pkg_sha256 == "UNKNOWN_SHA":
            continue

        health_obj = {"status": pkg.status}
        if pkg.error_message:
            health_obj["error_message"] = pkg.error_message

        network_package_updates[pkg_sha256] = health_obj
        
        display_name = pkg.filename or pkg_sha256
        if pkg.metadata and pkg.metadata.Filename:
            display_name = pkg.metadata.Filename

        new_packages_ui.append({
            "sha256": pkg_sha256, 
            "status": pkg.status, 
            "name": display_name
        })

        if pkg.metadata:
            packages_col.update_one(
                {"SHA256": pkg_sha256}, 
                {"$set": pkg.metadata.model_dump(by_alias=True)}, 
                upsert=True
            )

    update_record = {
        "bundle_name": report.bundle_name,
        "timestamp": report.timestamp,
        "overall_status": report.overall_status,
        "error_message": report.error_message,
        "packages": network_package_updates
    }

    networks_col.update_one(
        {"net_id": net_id}, 
        {
            "$set": {
                "network_name": network_name, 
                "subnet": subnet
            },
            "$push": {
                "updates": update_record
            }
        }, 
        upsert=True
    )
    
    logger.info(f"Updated network {net_id}. Broadcasting to frontend...")
    
    update_data = {
        "type": "NEW_DATA",
        "net_id": net_id,
        "network_name": network_name,
        "latest_update": {
            "bundle_name": report.bundle_name,
            "timestamp": report.timestamp,
            "overall_status": report.overall_status,
            "error_message": report.error_message,
            "packages": new_packages_ui
        }
    }
    asyncio.run_coroutine_threadsafe(manager.broadcast(update_data), loop)

# HELPER 
def enrich_network_data(net_doc: dict) -> dict:
    """Formats the network dictionary to send clean status/name data to the frontend UI"""
    if "updates" in net_doc:
        for update in net_doc["updates"]:
            if "packages" in update and isinstance(update["packages"], dict):
                enriched_packages = {}
                for p_hash, health in update["packages"].items():
                    pkg_meta = packages_col.find_one({"SHA256": p_hash}, {"Filename": 1, "Package": 1, "_id": 0})
                    
                    ui_health = {
                        "status": health.get("status", "UNKNOWN"),
                        "error_message": health.get("error_message", "")
                    }

                    if pkg_meta:
                        ui_health["display_name"] = pkg_meta.get("Filename") or pkg_meta.get("Package") or p_hash
                    else:
                        ui_health["display_name"] = p_hash
                        
                    enriched_packages[p_hash] = ui_health
                
                update["packages"] = enriched_packages
    return net_doc


def udp_listener_thread(host: str, port: int, loop: asyncio.AbstractEventLoop):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((host, port))
    logger.info(f"UDP Listener started on {host}:{port}")

    while True:
        try:
            data, addr = sock.recvfrom(65535)
            report_json = json.loads(data.decode("utf-8"))
            process_report(report_json, loop)
        except Exception as e:
            logger.error(f"Error processing UDP packet: {e}")

@asynccontextmanager
async def lifespan(app: FastAPI):
    loop = asyncio.get_running_loop()
    udp_thread = threading.Thread(
        target=udp_listener_thread, 
        args=("0.0.0.0", 5005, loop), 
        daemon=True
    )
    udp_thread.start()
    yield

app = FastAPI(lifespan=lifespan)

os.makedirs("static", exist_ok=True)
app.mount("/static", StaticFiles(directory="static"), name="static")

@app.get("/")
async def read_index():
    return FileResponse('static/index.html')


# IMPLEMENTED ENDPOINTS
@app.get("/packages")
def get_all_packages(q: str = None):
    query = {}
    if q:
        query = {
            "$or": [
                {"Package": {"$regex": q, "$options": "i"}},
                {"Filename": {"$regex": q, "$options": "i"}},
                {"SHA256": {"$regex": q, "$options": "i"}}
            ]
        }
    return list(packages_col.find(query, {"_id": 0}))

@app.get("/package")
def get_pkg(sha256: str):
    pkg = packages_col.find_one({"SHA256": sha256}, {"_id": 0})
    if not pkg:
        raise HTTPException(status_code=404, detail="Package not found")
    return pkg

@app.post("/rm_package")
def remove_pkg(req: RemovePackageReq):
    result = packages_col.delete_one({"SHA256": req.sha256})
    if result.deleted_count == 0:
        raise HTTPException(status_code=404, detail="Package not found")
    
    networks_col.update_many({}, {"$unset": {f"updates.$[].packages.{req.sha256}": ""}})
    return {"message": f"Package {req.sha256} removed successfully"}

@app.get("/networks")
def get_all_networks(q: str = None):
    query = {}
    if q:
        query = {
            "$or": [
                {"network_name": {"$regex": q, "$options": "i"}},
                {"net_id": {"$regex": q, "$options": "i"}},
                {"subnet": {"$regex": q, "$options": "i"}}
            ]
        }
    nets = list(networks_col.find(query, {"_id": 0}))
    return [enrich_network_data(n) for n in nets]

@app.get("/network")
def get_net(net_id: str):
    net = networks_col.find_one({"net_id": net_id}, {"_id": 0})
    if not net:
        raise HTTPException(status_code=404, detail="Network not found")
    return enrich_network_data(net)

@app.post("/rm_network")
def remove_net(req: RemoveNetworkReq):
    result = networks_col.delete_one({"net_id": req.net_id})
    if result.deleted_count == 0:
        raise HTTPException(status_code=404, detail="Network not found")
    return {"message": f"Network {req.net_id} removed successfully"}

@app.get("/pkggraph")
def generate_pkg_graph(pkg_sha256: str, file_format: str = 'png'):
    start_pkg = packages_col.find_one({"SHA256": pkg_sha256})
    if not start_pkg:
        raise HTTPException(status_code=404, detail="Package not found in DB")

    graph_data = {}
    visited_names = set()
    queue = [start_pkg]

    while queue:
        current_pkg = queue.pop(0)
        p_name = current_pkg.get("Package") or current_pkg.get("Filename", "Unknown")
        
        if p_name in visited_names:
            continue
        visited_names.add(p_name)
        
        raw_dependencies = current_pkg.get("Dependencies", [])
        formatted_deps = []
        
        for dep in raw_dependencies:
            if len(dep) >= 3:
                dep_name, dep_version, dep_arch = dep[0], dep[1], dep[2]
            elif len(dep) > 0:
                dep_name, dep_version, dep_arch = dep[0], "any", "any"
            else:
                continue
                
            formatted_deps.append([dep_name, dep_version, dep_arch])
            
            if dep_name not in visited_names:
                child_pkg = packages_col.find_one({
                    "$or": [
                        {"Package": dep_name},
                        {"Filename": {"$regex": dep_name, "$options": "i"}}
                    ]
                })
                if child_pkg:
                    queue.append(child_pkg)
        
        graph_data[p_name] = formatted_deps

    dot = graphviz.Digraph(comment='Package Dependencies')
    dot.attr(rankdir='LR') 
    dot.attr('node', shape='box', style='filled', fillcolor='white', fontname='Helvetica')
    dot.attr('edge', fontname='Helvetica', fontsize='10')

    for parent_name, dependencies in graph_data.items():
        dot.node(parent_name)
        for dep in dependencies:
            dep_name, version, arch = dep[0], dep[1], dep[2]
            dot.edge(parent_name, dep_name, label=f"{version}\n({arch})")

    try:
        image_bytes = dot.pipe(format=file_format)
    except graphviz.ExecutableNotFound:
        raise HTTPException(status_code=500, detail="Graphviz is not installed on the server.")

    return Response(content=image_bytes, media_type=f"image/{file_format}")

@app.get("/netgraph")
def generate_net_graph(net_id: str, file_format: str = 'png'):
    net = networks_col.find_one({"net_id": net_id})
    if not net:
        raise HTTPException(status_code=404, detail="Network not found in DB")

    dot = graphviz.Digraph(comment=f'Network {net_id} Dependencies')
    dot.attr(rankdir='LR') 
    dot.attr('node', shape='box', style='filled', fillcolor='white', fontname='Helvetica')
    dot.attr('edge', fontname='Helvetica', fontsize='10')

    net_node_name = f"Network:\n{net.get('network_name', net_id)}"
    dot.node(net_node_name, shape='ellipse', fillcolor='lightblue', style='filled')

    graph_data = {}
    visited_names = set()
    queue = []

    all_package_hashes = set()
    if "updates" in net:
        for update in net["updates"]:
            if "packages" in update:
                all_package_hashes.update(update["packages"].keys())

    for p_hash in all_package_hashes:
        pkg = packages_col.find_one({"SHA256": p_hash})
        if pkg:
            p_name = pkg.get("Package") or pkg.get("Filename", p_hash)
            dot.edge(net_node_name, p_name)
            if pkg not in queue:
                queue.append(pkg)

    while queue:
        current_pkg = queue.pop(0)
        p_name = current_pkg.get("Package") or current_pkg.get("Filename", "Unknown")
        
        if p_name in visited_names:
            continue
        visited_names.add(p_name)
        
        raw_dependencies = current_pkg.get("Dependencies", [])
        formatted_deps = []
        
        for dep in raw_dependencies:
            if len(dep) >= 3:
                dep_name, dep_version, dep_arch = dep[0], dep[1], dep[2]
            elif len(dep) > 0:
                dep_name, dep_version, dep_arch = dep[0], "any", "any"
            else:
                continue

            formatted_deps.append([dep_name, dep_version, dep_arch])
            
            if dep_name not in visited_names:
                child_pkg = packages_col.find_one({
                    "$or": [
                        {"Package": dep_name},
                        {"Filename": {"$regex": dep_name, "$options": "i"}}
                    ]
                })
                if child_pkg:
                    queue.append(child_pkg)
        
        graph_data[p_name] = formatted_deps

    for parent_name, dependencies in graph_data.items():
        dot.node(parent_name)
        for dep in dependencies:
            dep_name, version, arch = dep[0], dep[1], dep[2]
            dot.edge(parent_name, dep_name, label=f"{version}\n({arch})")

    try:
        image_bytes = dot.pipe(format=file_format)
    except graphviz.ExecutableNotFound:
        raise HTTPException(status_code=500, detail="Graphviz is not installed on the server.")

    return Response(content=image_bytes, media_type=f"image/{file_format}")

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await manager.connect(websocket)
    try:
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        manager.disconnect(websocket)