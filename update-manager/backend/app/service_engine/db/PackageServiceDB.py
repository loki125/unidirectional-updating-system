import logging
import pprint
import re
from motor.motor_asyncio import AsyncIOMotorClient
from typing import Dict


class PackageServiceDB:
    """
    A service class for managing Debian package documents in a MongoDB database.

    Each package document must contain the keys:
    - "Package"
    - "Version"
    - "Type"
    - "Metadata"

    The unique document `_id` is constructed from Package, Version, and Type.
    """

    def __init__(self, uri: str, db_name: str):
        """
        Initialize the database connection and collection.

        :param uri: MongoDB connection string.
        :param db_name: Name of the MongoDB database to use.
        """
        self.client = AsyncIOMotorClient(uri)
        self.db = self.client[db_name]
        self.packages_collection = self.db["packages"]

        self.structure = {"Package": "", "Version": 0.0, "Type": "", "Metadata": ""}

    async def insert(self, package: Dict) -> int:
        """
        Insert a new package document into the database.

        :param package: A dictionary representing the package.
        :return:
            0 - inserted successfully
            1 - invalid structure
            2 - package already exists
        """
        if package.keys() != self.structure.keys():
            return 1

        if await self.package_exist(package):
            return 2

        package["_id"] = self._make_id(package)
        await self.packages_collection.insert_one(package)
        return 0

    async def package_exist(self, package: Dict) -> bool:
        """
        Check whether a package already exists in the database.

        :param package: The package dictionary.
        :return: True if the document exists, False otherwise.
        """
        count = await self.packages_collection.count_documents(
            {"_id": self._make_id(package)},
            limit=1
        )
        return count > 0

    @staticmethod
    def _make_id(package: Dict) -> str:
        """
        Create the unique `_id` string for a package document.

        :param package: The package dictionary.
        :return: A unique ID of format "Package_Version_Type".
        """
        return f"{package['Package']}_{package['Version']}_{package['Type']}"

    async def print_packages(self, num : int):
        """
        Print the `_id` of up to num package documents in the collection.
        Useful for debugging and verifying stored data.

        :param num: The number of package documents to print.
        """
        cursor = self.packages_collection.find({}).limit(num)

        async for doc in cursor:
            pprint.pprint(doc["_id"])

    async def reset_packages_collection(self):
        """
        Delete all documents from the 'packages' collection.
        Useful for resetting the system state.
        """
        result = await self.packages_collection.delete_many({})
        print(f"Deleted {result.deleted_count} documents.")

    async def find_one(self, _id: str) -> Dict | None:
        """
        Retrieve a single package document by its `_id`.

        :param _id: The unique ID of the package.
        :return: The document if found, otherwise None.
        """
        return await self.packages_collection.find_one({"_id": _id})

    @staticmethod
    def find_in_metadate(package_metadate: str, finder_str: str) -> str | None:
        """
        Search inside a metadata string for the value following a keyword.

        Example:
            metadata = "Size: 1200 Priority: optional"
            find_in_metadate(metadata, "Size:")  ->  "1200"

        :param package_metadate: Raw metadata text.
        :param finder_str: The keyword to search for.
        :return: The matched value, or None if not found.
        """
        match = re.search(rf"{finder_str}\s*(\S+)", package_metadate)
        if match:
            return match.group(1)

        logging.warning(f"could not find **{finder_str}** in package metadata")
        return None
