from motor.motor_asyncio import AsyncIOMotorClient
from typing import Dict, List, Optional

class Database:
    def __init__(self, uri, name, collection):
        self._client = AsyncIOMotorClient(uri)
        _db = self._client[name]
        self._collection = _db[collection]

    def close(self):
        self._client.close()

class PackageDB(Database):
    def __init__(self, uri, name, collection):
        super().__init__(uri, name, collection)

    async def _get_pkgs(self, query: Dict, amount: Optional[int] = None) -> List | None:
        cursor = self._collection.find(query, {"_id": 0})
        if amount is not None:
            cursor = cursor.limit(amount)

        return await cursor.to_list(length=amount)
    
    async def _get_pkg(self, query: Dict) -> Dict | None:
        package = await self._collection.find_one(query, {"_id": 0})
        if not package:
            return None
        
        return package
    
    async def get_pkg_by_name(self, name : str) -> List | None:
        return await self._get_pkgs({"Package": name})
    
    async def get_pkg_by_name_version(self, name : str, version: str) -> Dict | None:
        return await self._get_pkg({"Package": name, "Version": version})

    async def get_pkg_by_hash(self, hash : str) -> Dict | None:
        return await self._get_pkg({"SHA256": hash})
    
    async def pkg_exists(self, package_data: Dict) -> bool:
        package = await self._collection.find_one({"Store_path": package_data["Store_path"]})
        return package is not None
