from fastapi import FastAPI
import os
import logging

app = FastAPI()
STORE = os.getenv("STORE_PATH", "/data/store_volume")


@app.on_event("startup")
async def startup_event():
    print(">>> STARTUP RAN!")
    