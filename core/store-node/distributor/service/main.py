from io import BytesIO
from typing import Dict
import zipfile
from fastapi import FastAPI, HTTPException
import os

from fastapi.responses import StreamingResponse
from DB import PackageDB

service = FastAPI()

STORE = os.getenv("STORE_PATH", "/data/store_volume")
pkg_db: PackageDB = None

@service.on_event("startup")
async def startup_event():
    global pkg_db
    print(">>> Initializing Database...")
    pkg_db = PackageDB(
        uri=os.getenv("MONGO_ISOLATED_URI"),
        name=os.getenv("MONGO_PACKAGES_DB"),
        collection=os.getenv("MONGO_PACKAGES_COLLECTION")
    )

@service.on_event("shutdown")
async def shutdown_event():
    if pkg_db:
        pkg_db.close()
    
@service.get("/pkgs_by_name")
async def get_packages_by_name(Package: str):
    result = await pkg_db.get_pkg_by_name(Package)
    if not result:
        raise HTTPException(status_code=404, detail="Package not found")
    return result

@service.get("/pkgs_by_name_version")
async def get_packages_by_name_version(Package: str, Version: str):
    result = await pkg_db.get_pkg_by_name_version(Package, Version)
    if not result:
        raise HTTPException(status_code=404, detail="Package not found")
    return result

@service.get("/pkg_by_hash")
async def get_packages_by_hash(SHA256: str):
    result = await pkg_db.get_pkg_by_hash(SHA256)
    if not result:
        raise HTTPException(status_code=404, detail="Package not found")
    return result

@service.get("/download_pkg")
async def get_download_package(Store_path: str):
    folder_path = os.path.join(STORE, Store_path)
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