from fastapi import FastAPI, BackgroundTasks
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse
import os
import logging
import json
import core  #  C++ module

app = FastAPI()

# Static files (unchanged)
app.mount("/static", StaticFiles(directory="static"), name="static")

@app.get("/")
async def read_index():
    return FileResponse('static/index.html')

BROADCASTER_VOLUME = os.getenv("UPDATE_FILE_PATH")
if BROADCASTER_VOLUME is None:
    raise EnvironmentError("no volume path for broadcaster given")

core_service = core.CoreService()


@app.on_event("startup")
async def startup_event():
    print(">>> STARTUP RAN!")
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(message)s"
    )

@app.post("/test")
async def test_event(background_tasks: BackgroundTasks):
    print("trying to send update for libc6, 2.42-7, amd64")

    background_tasks.add_task(
        core_service.process_and_broadcast,
        "Debian",
        "libc6",
        "2.42-7",
        "amd64",
        BROADCASTER_VOLUME
    )

    return {"status": "broadcast started"}

@app.post("/update")
async def broadcast_event(
    package_name: str,
    version: str,
    architecture: str,
    background_tasks: BackgroundTasks
):
    print(f"trying to send update for {package_name}, {version}, {architecture}")

    background_tasks.add_task(
        core_service.process_and_broadcast,
        "Debian",
        package_name,
        version,
        architecture,
        BROADCASTER_VOLUME
    )

    return {"status": "broadcast started"}

@app.get("/instance")
async def get_package_metadata(package_name: str, type: str):
    raw_list = core_service.get_package_instances(package_name, type)

    if not raw_list:
        return {"error": "Package not found"}

    # each item is JSON string → convert
    return [json.loads(item) for item in raw_list]

@app.get("/info")
async def get_package_info(package_name: str, version: str, architecture: str):
    raw = core_service.get_package_info("Debian", package_name, version, architecture)

    data = json.loads(raw)
    return data