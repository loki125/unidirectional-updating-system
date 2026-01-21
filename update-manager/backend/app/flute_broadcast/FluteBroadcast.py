import asyncio
import io
import aiohttp
import logging
import tarfile
import json
import os
import ipaddress
from datetime import datetime
from typing import Dict, List, Tuple, Union, Optional, Any
from package_engine.ServiceFactory import SearchFactory as sf
from utilitys.PackageMetadata import PackageMetadata
from utilitys.UpdateManifest import UpdateManifest


class FluteBroadcast:
    def __init__(self, uri: str):
        self.broadcaster_uri = uri
        self.session: Optional[aiohttp.ClientSession] = None
        self.version = "1.0"

    async def __aenter__(self):
        timeout = aiohttp.ClientTimeout(
            total=None,      
            connect=30,
            sock_connect=30,
            sock_read=120
        )
        self.session = aiohttp.ClientSession(timeout=timeout)
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        if self.session:
            await self.session.close()

    async def _send(self, data: Dict, path: str) -> Dict:
        """
        Sends a POST request and handles the JSON response.
        Supports 200, 207 (Multi-Status), and handles 4xx/5xx gracefully.
        """
        url = f"{self.broadcaster_uri}/{path}"
    
        async with self.session.post(url, json=data) as resp:
            # We attempt to read JSON regardless of status code because
            # the C++ server returns JSON details on errors (400, 409, 500, etc.)
            try:
                response_data = await resp.json()
            except aiohttp.ContentTypeError:
                response_data = {}
                text = await resp.text()
                logging.error(f"Server returned non-JSON response: {text}")

            match(resp.status):
                case 200:
                    logging.info("Broadcaster completed the request")
                case 207:
                    logging.warning("Broadcaster returned Multi-Status (Partial Success).")
                case 404:
                    raise Exception(f"Resource not found: {response_data.get('message', 'Unknown')}")
                case _:
                    raise Exception(f"Broadcaster returned error {resp.status}: {response_data.get('message', response_data)}")
            return response_data

    async def add_destinations(self, *dest_ips: str) -> List[Dict]:
        """
        Adds destinations and returns a list of response objects for each IP.
        """
        results = []
        for dist_ip in dest_ips:
            try:
                ipaddress.ip_address(dist_ip)
            except ValueError:
                msg = f"add_destinations: Destination {dist_ip} is not a valid IP. Skipping."
                logging.warning(msg)
                results.append({"status": "error", "message": msg, "ip": dist_ip})
                continue

            data = {
                "param": {"ip": dist_ip}
            }
            resp = await self._send(data, "add_destination")
            results.append(resp)
            
            if resp.get("status") == "success":
                logging.info(f"Successfully added destination: {dist_ip}")
            elif resp.get("status") == "error":
                logging.warning(f"Failed to add {dist_ip}: {resp.get('message')}")
        
        return results

    async def remove_destinations(self, *dest_ips: str) -> List[Dict]:
        """
        Removes destinations and returns a list of response objects for each IP.
        """
        results = []
        for dist_ip in dest_ips:
            try:
                ipaddress.ip_address(dist_ip)
            except ValueError:
                msg = f"remove_destinations: Destination {dist_ip} is not a valid IP. Skipping."
                logging.warning(msg)
                results.append({"status": "error", "message": msg, "ip": dist_ip})
                continue

            data = {
                "param": {"ip": dist_ip}
            }
            resp = await self._send(data, "remove_destination")
            results.append(resp)
            
            if resp.get("status") == "success":
                logging.info(f"Successfully removed destination: {dist_ip}")
            elif resp.get("status") == "error":
                logging.warning(f"Failed to remove {dist_ip}: {resp.get('message')}")
        
        return results

    async def send_object(self, file_paths: List[str], destinations: Union[List[str], str] = "all") -> Dict:
        """
        Sends files to specific IPs or 'all'.
        Returns the detailed JSON response from the broadcaster containing success/fail stats.
        """
        data = {
            "param": {
                "file_paths": file_paths,
                "ip_destinations": destinations
            }
        }

        logging.info(f"Requesting broadcast of {len(file_paths)} files to {destinations}...")
        resp = await self._send(data, "send_object")

        # Process the detailed response for logging
        successful_ips = resp.get("successful_ips", [])
        failed_ips = resp.get("failed_ips", [])
        file_errors = resp.get("file_errors", [])
        global_error = resp.get("global_error")

        if global_error:
            logging.error(f"Broadcast global error: {global_error}")
        else:
            if successful_ips:
                logging.info(f"Broadcast successful to: {successful_ips}")

            if failed_ips:
                for fail in failed_ips:
                    logging.error(f"Failed to send to IP {fail.get('ip')}: {fail.get('reason')}")

            if file_errors:
                for ferr in file_errors:
                    logging.error(f"File error on IP {ferr.get('ip')} for file {ferr.get('file')}: {ferr.get('error')}")

        return resp

    async def send_update(self, 
                          main_package_path: str, 
                          update_metadata: PackageMetadata, 
                          factory: sf, 
                          broadcaster_path: str,
                          destinations: Union[List[str], str] = "all") -> Dict:
        """
        Orchestrates package collection, tar creation, and broadcasting.
        Returns the result of the broadcast request, or an error dict if preparation failed.
        """
        object_path = None

        try:
            file_list = []
            packages_for_manifest = []
            total_size_byte = 0
            visited = set()
            engine = factory.get_engine(update_metadata.Type)

            # Helper to process and collect
            async def process_package(metadata: PackageMetadata, f_path):
                nonlocal visited
                nonlocal total_size_byte
                pkg_id = metadata.generate_id()

                if pkg_id in visited:
                    return
                visited.add(pkg_id)

                # Update metadata with actual filename and store
                file_list.append(f_path)
                packages_for_manifest.append(metadata)
                total_size_byte += metadata.Size

                # Process Dependencies recursively
                for dep_info in metadata.Dependencies:
                    pkg_name = dep_info[0]
                    version = dep_info[1]
                    arch = dep_info[2]

                    if PackageMetadata.build_id(pkg_name, version, arch) in visited:
                        continue

                    next_metadata, next_f_path = await self._config_file(engine, pkg_name, version, arch)
                    if next_metadata and next_f_path:
                        await process_package(next_metadata, next_f_path)
                    else:
                        logging.warning(f"Dependency {dep_info} missing.")

            async with engine:
                await process_package(update_metadata, main_package_path)

            # Construct the Manifest
            manifest = UpdateManifest(
                Update_id=update_metadata.generate_id(),
                Format_version=self.version,
                Timestamp=datetime.now(),
                Total_size_byte=total_size_byte,
                Packages=packages_for_manifest
            )

            # Finalize: Create the tarball
            object_path = await self._create_tar_object(manifest, file_list, broadcaster_path)
            
            # Send the broadcast request 
            return await self.send_object([object_path], destinations=destinations)

        except Exception as e:
            logging.error(
                f"Broadcasting update failed for package {update_metadata.generate_id()}",
                exc_info=True
            )
            return {"status": "error", "message": f"Broadcasting preparation failed: {str(e)}"}
        finally:
            # Cleanup temporary tar file
            if object_path and os.path.exists(object_path):
                try:
                    await asyncio.to_thread(os.remove, object_path)
                    logging.info(f"Cleaned up temporary object: {object_path}")
                except OSError as cleanup_e:
                    logging.warning(f"Failed to delete temporary file {object_path}: {cleanup_e}")

    async def _config_file(self, engine, pkg_name: str, version: str, arch: str) -> Tuple[Optional[PackageMetadata], Optional[str]]:
        # Download the file
        download_path = await engine.get_package_file(pkg_name, version, arch)
        if not download_path:
            logging.error(f"Failed to download package pkg_name: {pkg_name}, version: {version}, arch: {arch}")
            return None, None

        next_metadata = await engine.get_package_metadata(download_path)
        return next_metadata, download_path

    async def _create_tar_object(self, manifest: UpdateManifest, file_paths: List[str], tar_path: str) -> str:
        """
        Creates a tarball in a thread-safe manner to avoid blocking the event loop.
        """
        # Define the file path
        new_file_path = os.path.join(tar_path, f"{manifest.update_id}.tar")

        # This inner function contains all the blocking I/O logic
        def _build_tar():
            with tarfile.open(new_file_path, "w") as tar:

                # Add the manifest JSON directly from memory
                if manifest.packages:
                    # Convert manifest to bytes
                    manifest_data = manifest.to_json().encode("utf-8")
                    
                    # Prepare TarInfo and write to archive
                    tarinfo = tarfile.TarInfo(name="manifest.json")
                    tarinfo.size = len(manifest_data)
                    # Use BytesIO to stream the memory-stored JSON into the tar file
                    tar.addfile(tarinfo, io.BytesIO(manifest_data))

                # Add all files
                for path in file_paths:
                    if not os.path.isfile(path):
                        raise FileNotFoundError(f"Missing component file: {path}")
                    # arcname ensures we don't include the full local folder structure in the tar
                    tar.add(path, arcname=os.path.basename(path))



        # Offload the blocking function to a worker thread
        await asyncio.to_thread(_build_tar)

        return new_file_path