import logging
import pprint
import re
from motor.motor_asyncio import AsyncIOMotorClient
from typing import Dict

class PackageServiceDB:
    def __init__(self, uri: str, db_name: str):
        self.client = AsyncIOMotorClient(uri)
        self.db = self.client[db_name]
        self.packages_collection = self.db["packages"]

        self.structure = {"Package" : "", "Version" : 0.0, "Type" : "", "Metadata" : ""}

    async def insert(self, package: Dict) -> int:
        if package.keys() != self.structure.keys():
            return 1

        if await self.package_exist(package):
            return 2

        package["_id"] = self._make_id(package)
        await self.packages_collection.insert_one(package)
        return 0

    async def package_exist(self, package: Dict) -> bool:
        count = await self.packages_collection.count_documents(
            {"_id": self._make_id(package)},
            limit=1
        )
        return count > 0

    @staticmethod
    def _make_id(package):
        return f"{package['Package']}_{package['Version']}_{package['Type']}"

    async def print_all_packages(self):
        """
        Print all documents in the packages collection.
        """
        cursor = self.packages_collection.find({}).limit(30)

        async for doc in cursor:
            pprint.pprint(doc["_id"])

    async def reset_packages_collection(self):
        """
        Drop the 'packages' collection, clearing all package data.
        """
        result = await self.packages_collection.delete_many({})
        print(f"Deleted {result.deleted_count} documents.")

    async def find_one(self, _id: str) -> Dict | None:
        return await self.packages_collection.find_one({"_id": _id})

    @staticmethod
    def find_in_metadate(package_metadate: str, finder_str : str) -> str | None:
        match = re.search(rf"{finder_str}\s*(\S+)", package_metadate)
        if match:
            return match.group(1)

        logging.warning(f"could not find **{finder_str}** in package metadata")
        return None


