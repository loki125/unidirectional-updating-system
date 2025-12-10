from fastapi import FastAPI
from service_engine.db.PackageServiceDB import PackageServiceDB as db
from service_engine import ServiceFactory as sf

app = FastAPI()

db_instance = db(uri="mongodb://mongo:27017", db_name="um-db")
service_factory = sf.SearchFactory(db_instance)

@app.on_event("startup")
async def startup_event():
    print(">>> STARTUP RAN!")
    #await db_instance.reset_packages_collection()
    #await service_factory.global_refresh_metadata()

@app.on_event("startup")
async def startup_test_event():
    print("printing all packages")
    await db_instance.print_packages(30)
    print("DONE")

    file_path = await service_factory.get_engine("Debian").request_download_file("python3-lib389_3.1.2+dfsg1-1_Debian")
    print(file_path)