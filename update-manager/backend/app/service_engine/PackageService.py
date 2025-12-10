from abc import ABC, abstractmethod
import re
import os
import hashlib
import aiohttp
import aiofiles
import asyncio
import gzip
import logging
from .db.PackageServiceDB import PackageServiceDB as db

class PackageService(ABC):

    @abstractmethod
    async def refresh_metadata(self) -> None:
        """
        updates database for latest metadata of packages that the service provides
        """
        pass

    @abstractmethod
    async def request_download_file(self, db_packet_id : str) -> str | None:
        """
        gets a package_id, and downloads its package from the service

        :param db_packet_id: The package id of the package to download
        :return file_name of the downloaded package
        """
        pass

    @abstractmethod
    async def _parse_packages_file(self, packages_file_path: str) -> int:
        """
        Internal function: gets an OPEN file, and parse Packages.gz into a Dict in the PackegeServiceDB format.

        :param packages_file_path: The path to the packages.gz file
        :return amount_of_changed_packets
        """
        pass

    @abstractmethod
    async def _insert_metadata_to_db(self, package : str) -> int:
        """
        Internal function: inserts a parsed package into the PackegeServiceDB

        :param package: The package to insert
        :return if a package was changed
        """
        pass

class DebianPackageService(PackageService):

    def __init__(self, db_instance : db, distribution: str = "stable", component: str = "main", arch : str = "amd64"):
        self.default_url = "https://deb.debian.org/debian"
        self._metadate_repo_url = f"{self.default_url}/dists/{distribution}/{component}/binary-{arch}/Packages.gz"
        self.service_type = self.__class__.__name__.removesuffix("PackageService")

        self.db_instance = db_instance
        self.download_buffer = 64 * 1024

    async def refresh_metadata(self) -> None:
        total_bytes = 0
        file_path = "Packages.gz"

        async with aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=20)) as session:
            async with session.get(self._metadate_repo_url) as response:
                response.raise_for_status()

                async with aiofiles.open(file_path, "wb") as f:
                    async for chunk in response.content.iter_chunked(self.download_buffer):
                        await f.write(chunk)
                        total_bytes += len(chunk)

        print("Total megabytes written:", total_bytes / (1024 * 1024))
        await self._parse_packages_file("Packages.gz")

        print(f"done phrasing {file_path}, deleting file")
        await asyncio.to_thread(os.remove, file_path)

    async def _parse_packages_file(self, packages_file_path: str) -> int:
        package = ""
        amount_of_changed_packets = 0

        with gzip.open(packages_file_path, "rt", encoding="utf-8") as f:
            for line in f:
                if line.strip() == "":
                    amount_of_changed_packets += int( await self._insert_metadata_to_db(package))
                    package = ""
                else:
                    package += line

            # Handle the last package after EOF
            if package:
                amount_of_changed_packets += int(await self._insert_metadata_to_db(package))

        return amount_of_changed_packets

    async def _insert_metadata_to_db(self, package: str) -> int:
        #make package to db format
        struct_copy = self.db_instance.structure.copy()
        struct_copy["Type"] = self.service_type
        struct_copy["Metadata"] = package

        match = re.search(r"Package:\s*(\S+)", package)
        if not match:
            logging.error(f"Could not find package name")
        struct_copy["Package"] = match.group(1)

        match = re.search(r"Version:\s*(\S+)", package)
        if not match:
            logging.error(f"Could not find package version {package}")
        struct_copy["Version"] = match.group(1)

        #if packet exists send false
        return await self.db_instance.insert(struct_copy)

    async def request_download_file(self, db_packet_id: str) -> str | None:
        package = await self.db_instance.find_one(db_packet_id)
        if package is None:
            logging.error(f"Could not find package with id {db_packet_id}")
            return None

        filename = self.db_instance.find_in_metadate(package["Metadata"], "Filename:")
        expected_hash = self.db_instance.find_in_metadate(package["Metadata"], "SHA256:")
        if expected_hash is None or filename is None:
            return None

        request_file_path = f"{self.default_url}/{filename}"
        download_file_path = f"{db_packet_id}.deb"

        sha256 = hashlib.sha256()
        # Download
        async with aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=20)) as session:
            async with session.get(request_file_path) as response:
                response.raise_for_status()
                async with aiofiles.open(download_file_path, "wb") as f:
                    async for chunk in response.content.iter_chunked(self.download_buffer):
                        if not chunk:
                            continue
                        await f.write(chunk)
                        sha256.update(chunk)

        if expected_hash != sha256.hexdigest():
            logging.error("SHA256 mismatch, File is corrupted.")
            await asyncio.to_thread(os.remove, download_file_path)
            logging.info(f"File {download_file_path} has been deleted")
            return None

        return download_file_path


