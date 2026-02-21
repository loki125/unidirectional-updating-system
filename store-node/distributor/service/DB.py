from motor.motor_asyncio import AsyncIOMotorClient
from typing import Dict, List, Optional

class PackageDB:
    def __init__(self, uri, name, collection):
        _client = AsyncIOMotorClient(uri)
        _db = _client[name]
        self._collection = _db[collection]

    async def _get_pkgs(self, query: Dict, amount: Optional[int] = None) -> List | None:
        cursor = self._collection.find(query)
        if amount is not None:
            cursor = cursor.limit(amount)

        packages = []
        async for doc in cursor:
            doc.pop("_id", None)
            packages.append(doc)

        return packages
    
    async def _get_pkg(self, query: Dict) -> Dict | None:
        package = await self._collection.find_one(query)
        if not package:
            return None
        
        package.pop("_id", None)
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