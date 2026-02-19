from fastapi import FastAPI, Request
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse
import os
import logging
from flute_broadcast.FluteBroadcast import FluteBroadcast as fb
from package_engine import ServiceFactory as sf
from utilitys.PackageMetadata import PackageMetadata

app = FastAPI()

# This serves everything in the /static folder
app.mount("/static", StaticFiles(directory="static"), name="static")

@app.get("/")
async def read_index():
    # This serves the GUI when you visit http://localhost:8000
    return FileResponse('static/index.html')

service_factory = sf.SearchFactory()

BROADCASTER_HOST = os.getenv("BROADCASTER_HOST", "broadcaster")

BROADCASTER_PORT = os.getenv("BROADCASTER_PORT")
if BROADCASTER_PORT is None:
    raise EnvironmentError("no port for broadcaster given")

BROADCASTER_VOLUME = os.getenv("BROADCASTER_VOLUME_PATH")
if BROADCASTER_VOLUME is None:
    raise EnvironmentError("no volume path for broadcaster given")

flute_broadcaster =  fb(uri=f"http://{BROADCASTER_HOST}:{BROADCASTER_PORT}")


@app.on_event("startup")
async def startup_event():
    print(">>> STARTUP RAN!")
    logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s"
)

@app.post("/test")
async def test_event():
    print("tying to send update for libc6, 2.42-7, amd64")
    engine = service_factory.get_engine("Debian")
    metadata : PackageMetadata
    async with engine:
        file_name = await engine.get_package_file("libc6", "2.42-7", "amd64")
        metadata = await engine.get_package_metadata(file_name)

    async with flute_broadcaster:
        await flute_broadcaster.send_update(file_name, metadata, service_factory, BROADCASTER_VOLUME)
        pass
        
    print("FINISHED SENDING UPDATE")

@app.post("/update")
async def broadcast_event(package_name: str, version: str, architecture: str):
    print(f"tying to send update for {package_name}, {version}, {architecture}")
    engine = service_factory.get_engine("Debian")
    metadata : PackageMetadata
    async with engine:
        file_name = await engine.get_package_file(package_name, version, architecture)
        metadata = await engine.get_package_metadata(file_name)

    async with flute_broadcaster:
        await flute_broadcaster.send_update(file_name, metadata, service_factory, BROADCASTER_VOLUME)
        
    print("FINISHED SENDING UPDATE")

@app.get("/instance")
async def get_package_metadata(package_name: str, type: str):
    engine = service_factory.get_engine(type)
    async with engine:
        instance = await engine.get_package_instances(package_name)
    
    if instance is None:
        return {"error": "Package not found"}
    
    return instance

@app.get("/info")
async def get_package_info(package_name: str, version: str, architecture: str):
    engine = service_factory.get_engine("Debian")  # Assuming Debian for now
    async with engine:
        info = await engine.get_package_info(package_name, version, architecture)
    
    if info is None:
        return {"error": "Package not found"}
    return info