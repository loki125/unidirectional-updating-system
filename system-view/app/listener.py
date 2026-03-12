import asyncio

from utils import *

class TelemetryWorker:
    def __init__(self, db_session: Session):
        self.db_session = db_session

    async def run(self):
        while True:
            # Simulate telemetry data collection and processing
            print("Collecting telemetry data...")
            await asyncio.sleep(5)  # Simulate time taken to collect/process data
            
            # Example: Update package health status randomly (for demonstration)
            packages = self.db_session.query(Package).all()
            for pkg in packages:
                pkg.health = '{"status": "Healthy"}'  # In a real scenario, this would be dynamic
            self.db_session.commit()
            print("Telemetry data processed and database updated.")