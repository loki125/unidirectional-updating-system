from motor.motor_asyncio import AsyncIOMotorClient
from typing import Dict

class DB:
    def __init__(self, uri, name):
        _client = AsyncIOMotorClient(uri)
        _db = _client[name]
        self._collection = _db["packages"]

    async def insert_pkg(self, package_data):
        result = await self._collection.insert_one(package_data)
        return result.inserted_id

    async def get_pkg(self, name : str) -> Dict | None:
        package = await self._collection.find_one({"Package": name})
        if not package:
            return None
        return package