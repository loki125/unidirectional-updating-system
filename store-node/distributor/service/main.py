from io import BytesIO
import pprint
from typing import Dict, List
import zipfile
from fastapi import FastAPI, HTTPException
import os

from fastapi.responses import StreamingResponse
from DB import PackageDB

service = FastAPI()

STORE = os.getenv("STORE_PATH", "/data/store_volume")
STORE_MANAGER_UPDATES : str = os.getenv("UPDATE_FILE_REQUEST")

db = PackageDB(
    uri=os.getenv("DB_HOST", "mongodb://mongo:27017"),
    name=os.getenv("MONGO_PACKAGES_DB"),
    collection=os.getenv("MONGO_PACKAGES_COLLECTION")
)

@service.on_event("startup")
async def startup_event():
    print(">>> STARTUP RAN!")
    
@service.post(STORE_MANAGER_UPDATES)
async def create_package(package: Dict):
    result = await db.get_pkg_by_hash(package["SHA256"])
    if result is None:
        raise HTTPException(status_code=500, detail="Failed to find document")
    
    #broadcast new package logic
    
    return {"status": "success", "hash": str(result["SHA256"])}
    
@service.get("/pkgs_by_name")
async def get_packages_by_name(name: str):
    result = await db.get_pkg(name)
    if not result:
        raise HTTPException(status_code=404, detail="Package not found")
    return result

@service.get("/pkgs_by_name_version")
async def get_packages_by_name_version(name: str, version: str):
    result = await db.get_pkg_by_name_version(name, version)
    if not result:
        raise HTTPException(status_code=404, detail="Package not found")
    return result

@service.get("/pkg_by_hash")
async def get_packages_by_hash(hash: str):
    result = await db.get_pkg_by_hash(hash)
    if not result:
        raise HTTPException(status_code=404, detail="Package not found")
    return result

@service.get("/download_pkg")
async def get_package_by_hash(store_path: str):
    folder_path = os.path.join(STORE, store_path)
    if not os.path.isdir(folder_path):
        raise HTTPException(status_code=404, detail="Folder not found")

    # Collect files to send
    files_to_send = []
    for fname in os.listdir(folder_path):
        files_to_send.append(os.path.join(folder_path, fname))

    if not files_to_send:
        raise HTTPException(status_code=404, detail="No files found")

    # Create a ZIP in memory
    zip_stream = BytesIO()
    with zipfile.ZipFile(zip_stream, mode="w") as zf:
        for fpath in files_to_send:
            zf.write(fpath, arcname=os.path.basename(fpath))
    zip_stream.seek(0)

    return StreamingResponse(
        zip_stream,
        media_type="application/zip",
        headers={"Content-Disposition": f"attachment; filename=pkg.zip"}
    )