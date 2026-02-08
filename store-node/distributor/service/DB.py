from distro import name
from motor.motor_asyncio import AsyncIOMotorClient
from typing import Dict, List

class PackageDB:
    def __init__(self, uri, name, collection):
        _client = AsyncIOMotorClient(uri)
        _db = _client[name]
        self._collection = _db[collection]

    async def get_pkg(self, name : str) -> List | None:
        cursor = self._collection.find({"Package": name})
        packages = []
        async for doc in cursor:
            doc.pop("_id", None)
            packages.append(doc)

        return packages

    
    async def get_pkg_by_hash(self, hash : str) -> Dict | None:
        package = await self._collection.find_one({"SHA256": hash})
        if not package:
            return None
        
        package.pop("_id", None)
        return package
    
    async def get_depend(self, depend : List) -> Dict | None:
        package = await self._collection.find_one({"Package": depend[0], 
                                                   "Version" : depend[1], 
                                                   "Architecture" : depend[2]})
        if not package:
            return None
        
        package.pop("_id", None)
        return package
    
    async def pkg_exists(self, package_data: Dict) -> bool:
        package = await self._collection.find_one({"Store_path": package_data["Store_path"]})
        return package is not None