import asyncio
from contextlib import asynccontextmanager
import os
from fastapi import FastAPI, Query, Depends, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from typing import List, Optional

from fastapi.staticfiles import StaticFiles

from utils import *
from listener import TelemetryWorker


# Connects to the MySQL docker container on localhost:3306
SQLALCHEMY_DATABASE_URL = os.getenv("DB_URL", "mysql+pymysql://root:rootpassword@localhost:3306/infra_db")

engine = create_engine(SQLALCHEMY_DATABASE_URL)
SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=engine)

# Dependency to get the DB session per request
def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()


# 4. APP LIFESPAN & DB SEEDING
@asynccontextmanager
async def lifespan(app: FastAPI):
    # Create the tables in the MySQL database automatically
    Base.metadata.create_all(bind=engine)
    
    # TEST SEEDING - Only runs if the database is empty
    db = SessionLocal()
    if not db.query(Network).first():
        print("Seeding database...")
        pkg1 = Package(name="SecureShield Antivirus", version="2.4.1", arch="x64", health='{"status": "Healthy"}', metadata_json='{"vendor": "SecurityInc"}')
        pkg2 = Package(name="CodeEditor Pro", version="1.87.0", arch="x64", health='{"status": "Warning"}', metadata_json='{"category": "IDE"}')
        
        net1 = Network(name="Corporate Office LAN", packages=[pkg1, pkg2])
        comp1 = Computer(pc_username="jsmith-workstation", network=net1, packages=[pkg1, pkg2])
        comp2 = Computer(pc_username="emily-laptop", network=net1, packages=[pkg1])
        
        db.add_all([net1, comp1, comp2])
        db.commit()
        print("Database seeded successfully.")
    db.close()
    
    yield

app = FastAPI(title="Infrastructure & Packages API", lifespan=lifespan)
listener = TelemetryWorker(db_session=SessionLocal())

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

@app.on_event("startup")
async def startup():
    asyncio.create_task(listener.run())

# --- API ROUTES ---

@app.get("/api/networks")
def get_networks():
    db = SessionLocal()
    nets = db.query(Network).options(Network.computers).all()
    db.close()
    return nets

@app.get("/api/computers/{id}/packages")
def get_computer_packages(id: int):
    db = SessionLocal()
    pkgs = db.query(Package).filter(Package.computer_id == id).all()
    db.close()
    return pkgs

@app.get("/api/networks/{id}/packages")
def get_network_packages(id: int):
    db = SessionLocal()
    pkgs = db.query(Package).filter(Package.network_id == id).all()
    db.close()
    return pkgs

@app.get("/api/packages")
def get_all_packages(search: Optional[str] = Query(None)):
    db = SessionLocal()
    query = db.query(Package)
    if search:
        query = query.filter(Package.name.contains(search) | Package.arch.contains(search))
    pkgs = query.all()
    db.close()
    return pkgs

# Serve your HTML/JS/CSS files from the root or a 'static' folder
app.mount("/", StaticFiles(directory="../static", html=True), name="static")