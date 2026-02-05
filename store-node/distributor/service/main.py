from io import BytesIO
from typing import Dict, List
import zipfile
from fastapi import FastAPI, HTTPException
import os

from fastapi.responses import StreamingResponse
from DB import DB

service = FastAPI()

STORE = os.getenv("STORE_PATH", "/data/store_volume")
URI = os.getenv("DB_HOST", "mongodb://mongo:27017")

db = DB(URI, "package_db")

@service.on_event("startup")
async def startup_event():
    print(">>> STARTUP RAN!")
    
@service.post("/package")
async def create_package(package: Dict):
    result = await db.pkg_exists(package)
    if not result:
        raise HTTPException(status_code=500, detail="Failed to find document")
    
    #broadcast new package logic
    
    return {"status": "success", "id": str(result.inserted_id)}
    
@service.get("/pkgs_by_name/{name}")
async def get_packages_by_name(name: str):
    result = await db.get_pkg(name)
    if not result:
        raise HTTPException(status_code=404, detail="Package not found")
    return result

@service.get("/pkgs_by_hash/{hash}")
async def get_packages_by_hash(hash: str):
    result = await db.get_pkg_by_hash(hash)
    if not result:
        raise HTTPException(status_code=404, detail="Package not found")
    return result

@service.get("/depend/{list}")
async def get_packages_by_depends(depnde_list: List[List[str]]):
    result : List[Dict]
    for depend in depnde_list:
        result.append(await db.get_depend(depend))
        if not result:
            raise HTTPException(status_code=404, detail="Package not found")
    return result


@service.get("/download_pkg/{store_path}")
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
        headers={"Content-Disposition": f"attachment; filename={store_path}.zip"}
    )