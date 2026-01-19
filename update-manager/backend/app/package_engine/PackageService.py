from abc import ABC, abstractmethod
import re
import os
from pprint import pprint
import hashlib
from typing import Dict, Optional, List, Any, Tuple
import aiohttp
import asyncio
import urllib.parse
import logging
from utilitys.PackageMetadata import PackageMetadata

class PackageService(ABC):
    """
    Abstract Base Class defining the required interface for all package service 
    implementations (e.g., Debian, PyPI, RPM).
    
    A concrete implementation MUST be used as an async context manager.
    """

    @abstractmethod
    async def __aenter__(self):
        """
        Initializes the package service, typically by creating an aiohttp.ClientSession 
        or other necessary resources.
        
        :return: The service instance.
        """
        pass

    @abstractmethod
    async def __aexit__(self, exc_type, exc_val, exc_tb):
        """
        Cleans up the package service resources, typically by closing the HTTP session.
        
        :param exc_type: Exception type (if any).
        :param exc_val: Exception value (if any).
        :param exc_tb: Traceback (if any).
        """
        pass

    @abstractmethod
    async def get_package_instances(self, pkg_name: str) -> List[Dict[str, Any]]:
        """
        Retrieves a list of all available versions and instances for a given package name 
        from the service's repository.
        
        :param pkg_name: The name of the package.
        :return: A list of dictionaries, where each dict represents a package instance.
        """
        raise NotImplementedError

    @abstractmethod
    async def get_package_info(self, pkg_name: str, version: str, architecture: str) -> Dict[str, Any]:
        """
        Fetches detailed metadata about a specific binary package instance 
        (version/architecture combination) from the service's repository.
        
        :param pkg_name: The name of the package.
        :param version: The version of the package.
        :param architecture: The target hardware architecture (e.g., 'amd64').
        :return: A dictionary containing the package metadata.
        """
        raise NotImplementedError

    @abstractmethod
    async def get_package_file(self, pkg_name: str, version: str, architecture: str) -> str:
        """
        Locates the package file, downloads it to a local path, and verifies its 
        integrity (e.g., hash check) using information from the service.
        
        :param pkg_name: The name of the package.
        :param version: The version of the package.
        :param architecture: The target hardware architecture.
        :return: The local file path to the downloaded package file.
        """
        raise NotImplementedError

    @abstractmethod
    async def get_package_metadata(self, file_path: str) -> 'PackageMetadata':
        """
        Parses the package control information from a locally downloaded file
        and returns a standardized PackageMetadata object. This typically involves
        using a system tool (like dpkg-deb for Debian) and resolving dependencies.
        
        :param file_path: The local path to the package file.
        :return: A PackageMetadata object containing parsed information.
        """
        raise NotImplementedError

class DebianPackageService(PackageService):

    def __init__(self):
        self.BASE_URL = "https://snapshot.debian.org"
        self.service_type = self.__class__.__name__.removesuffix("PackageService")
        self.session = None

        self.BUFFER = 64 * 1024
        self.timeout = aiohttp.ClientTimeout(total=1800, connect=30, sock_read=1800)

    async def __aenter__(self):
        self.session = aiohttp.ClientSession(timeout=self.timeout)
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        await self.session.close()

    def _check_session(self):
        """Raises an error if the aiohttp session has not been initialized."""
        if self.session is None:
            raise RuntimeError(
                "PackageService must be used as an async context manager: "
                "'async with DebianPackageService(...) as service:'"
            )
                
    async def _get_json(self, endpoint: str) -> Dict[str, Any]:
        """Internal helper to fetch JSON from the API."""
        self._check_session()
        url = f"{self.BASE_URL}{endpoint}"

        try:
            async with self.session.get(url) as response:
                if response.status != 200:
                    raise Exception(f"API Error: {response.status} ")
                return await response.json()
            
        except Exception as e:
            logging.error(f"could not find url: {url},\n cause: {e}")
            return None
        
    async def get_package_instances(self, pkg_name: str) -> List[Dict[str, Any]]:
        """
        Returns a list of all available versions and source instances of a package.
        """
        endpoint = f"/mr/binary/{pkg_name}/"
        data = await self._get_json(endpoint)
        if data is None:
            return None
        return data.get("result", [])
    
    async def get_package_info(self, pkg_name: str, version: str, architecture: str) -> Dict[str, Any]:
        """
        Returns metadata about a specific binary package instance.
        """
        # Note: Snapshot API lists binaries under the source version, 
        endpoint = f"/mr/binary/{pkg_name}/{urllib.parse.quote(version)}/binfiles?fileinfo=1"
        data = await self._get_json(endpoint)
        if data is None:
            logging.error(f"package: {pkg_name}_{version}_{architecture} not found")
            return None 
        
        arch_list = data.get("result", [])
        file_info_map = data.get("fileinfo", {})  # This is usually where the timestamps live
        target_hash = None

        for entry in arch_list:
            if entry.get("architecture") == architecture:
                target_hash = entry.get("hash")
                break

        if target_hash is None:
            logging.error(f"for package: {pkg_name}_{version} there is no arch {architecture} support")
            return None
        
        file_info = file_info_map[target_hash][0]
        file_info["SHA1"] = target_hash

        return file_info
    
    async def get_package_file(self, pkg_name: str, version: str, architecture: str) -> str:
        """
        Locates the binary package, downloads the .deb file, 
        verifies SHA1 integrity, and returns the local file path.
        """
        # 1. Get binary metadata to find the file hashes
        pkg_meta = await self.get_package_info(pkg_name, version, architecture)
        if pkg_meta is None:
            raise Exception(f"No binary found for {pkg_name} {version} ({architecture})")
        
        # Snapshot uses the SHA1 hash for the download URL, 
        expected_sha1 = pkg_meta['SHA1']
        
        file_name = pkg_meta["name"]
        file_url = f"{self.BASE_URL}/archive/debian/{pkg_meta["first_seen"]}/{pkg_meta["path"]}/{urllib.parse.quote(file_name)}"

        # 2. Download and Calculate Hash simultaneously
        sha1_hash = hashlib.sha1()
        
        async with self.session.get(file_url) as response:
            if response.status != 200:
                raise Exception(f"Failed to download deb: {response.status}")
            
            with open(file_name, 'wb') as f:
                # Read in chunks (8KB) to be memory efficient and hash on the fly
                async for chunk in response.content.iter_chunked(self.BUFFER):
                    f.write(chunk)
                    sha1_hash.update(chunk)

        # 3. Perform Integrity Check
        actual_sha1 = sha1_hash.hexdigest()
        
        if expected_sha1 and actual_sha1 != expected_sha1:
            # Delete the corrupted file before raising error
            if await asyncio.to_thread(os.path.exists, file_name):
                await asyncio.to_thread(os.remove, file_name)
            raise Exception(
                f"Integrity check failed for {file_name}!\n"
                f"Expected: {expected_sha1}\n"
                f"Actual:   {actual_sha1}"
            )

        print(f"[OK] Downloaded and verified: {file_name}")
        return file_name

    async def get_package_metadata(self, file_path: str) -> PackageMetadata:
        """
        Takes a local .deb file path and returns the info using 'dpkg-deb -I',
        parses the output to a packageMetadata Baseclass.
        """
        if not os.path.exists(file_path):
            raise FileNotFoundError(f"File not found: {file_path}")

        # Run dpkg-deb -I asynchronously
        process = await asyncio.create_subprocess_exec(
            'dpkg-deb', '-I', file_path,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE
        )
        stdout, stderr = await process.communicate()

        if process.returncode != 0:
            raise Exception(f"dpkg-deb error: {stderr.decode()}")

        deb_info = stdout.decode()
        lines = deb_info.splitlines()
        control_data = {}
        
        # Parse key-value pairs from dpkg-deb output
        for line in lines:
            if ':' in line:
                key, value = line.split(':', 1)
                control_data[key.strip()] = value.strip()

        # Calculate SHA256 of the local file
        sha256_hash = hashlib.sha256()
        with open(file_path, "rb") as f:
            for byte_block in iter(lambda: f.read(self.BUFFER), b""):
                sha256_hash.update(byte_block)

        arch = control_data.get("Architecture", "")
        depends = await self._resolve_dependencies(control_data.get("Depends"), arch)

        # Build the model using extracted and provided data
        metadata = PackageMetadata(
            Package=control_data.get("Package", ""),
            Version=control_data.get("Version", ""),
            Type=self.service_type,
            Architecture=arch,
            Dependencies=depends,
            SHA256=sha256_hash.hexdigest(),
            Installed_Size=int(control_data.get("Installed-Size", 0)),
            Size=os.path.getsize(file_path),
            Filename=os.path.basename(file_path),
        )
        return metadata

    async def _compare_versions(self, v1: str, operator: str, v2: str) -> bool:
        """Uses 'dpkg --compare-versions' to accurately compare Debian versions."""
        # Mapping standard ops to dpkg flags
        op_map = {
            "<<": "lt", "<=": "le", "=": "eq", 
            "!=": "ne", ">=": "ge", ">>": "gt"
        }   
        dpkg_op = op_map.get(operator, "eq")
        
        proc = await asyncio.create_subprocess_exec(
            'dpkg', '--compare-versions', v1, dpkg_op, v2,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE
        )
        await proc.wait()
        # dpkg returns 0 for True
        return proc.returncode == 0

    async def _find_best_version(self, pkg_name: str, constraints: List[Tuple[str, str]]) -> Optional[str]:
        """
        Queries Snapshot for all versions and picks the highest one satisfying ALL constraints.
        NOTE: The signature is updated to take a list of constraints to handle ranges.
        """
        instances = await self.get_package_instances(pkg_name)
        if not instances:
            return None

        all_versions = [i['version'] for i in instances]

        # If no constraints at all, return the newest version
        if not constraints:
            return all_versions[0]

        valid_versions = []
        
        # Check every available version against ALL gathered constraints for this package
        for v in all_versions:
            is_valid = True
            for op, target_ver in constraints:
                if not await self._compare_versions(v, op, target_ver):
                    is_valid = False
                    break # Failed one constraint, move to the next version

            if is_valid:
                valid_versions.append(v)
        
        if not valid_versions:
            # If nothing satisfies the constraints, we can't resolve it.
            return None 

        # Return the highest (first) version that satisfied all checks
        return valid_versions[0]


    async def _target_arch_or_all(self, pkg_name, version, target_arch):
        endpoint = f"/mr/binary/{pkg_name}/{urllib.parse.quote(version)}/binfiles"
        data = await self._get_json(endpoint)
        if data is None:
            return None
        
        # Grab all unique architectures from the result list
        available_architectures = {entry.get("architecture") for entry in data.get("result", [])}
        
        # Check for the specific arch first, then fall back to 'all'
        if target_arch in available_architectures:
            return target_arch
        elif "all" in available_architectures:
            return "all"
        else:
            return None


    async def _resolve_dependencies(self, depends_str: str, target_arch: str) -> List[List[str]]:
        """
        Parses dependencies, groups constraints by package, and resolves them concurrently.
        """
        if not depends_str:
            return []

        # Dictionary to group all constraints by package name:
        # { 'pkg_name': [ (op1, ver1), (op2, ver2), ... ] }
        package_constraints: Dict[str, List[Tuple[str, str]]] = {}

        # 1. Parse all constraints and consolidate by package name
        for part in depends_str.split(','):
            # Take the preferred dependency (ignoring alternatives for now)
            preferred_dep = part.split('|')[0].strip()

            # Regex groups: 1=name, 5=operator, 6=version
            match = re.search(r'^([a-z0-9\+\-\.]+)(:([a-z0-9]+))?(\s*\((<<|<=|=|>=|>>)\s*([^)]+)\))?', preferred_dep)
            if not match:
                continue

            pkg_name = match.group(1)
            constraint_op = match.group(5)
            constraint_ver = match.group(6)
            
            if pkg_name not in package_constraints:
                package_constraints[pkg_name] = []
            
            # Group constraints if they exist
            if constraint_op and constraint_ver:
                package_constraints[pkg_name].append((constraint_op, constraint_ver))
            # If no constraint is provided (just "pkg-name"), we rely on the default behavior
            # of _find_best_version (returning the newest version).

        
        # 2. Phase 1: Create version tasks for unique packages only
        # This prevents duplicate API calls for ranges like pkg (>= V1), pkg (<= V2)
        version_tasks = []
        unique_pkg_names = list(package_constraints.keys()) 

        for pkg_name in unique_pkg_names:
            constraints = package_constraints[pkg_name]
            # The find_best_version call now handles the list of constraints
            task = self._find_best_version(pkg_name, constraints) 
            version_tasks.append(task)
        
        if not version_tasks:
            return []

        # Run all version lookups concurrently
        chosen_versions = await asyncio.gather(*version_tasks)
        
        
        # 3. Phase 2: Create and execute arch tasks based on found versions
        arch_tasks = []
        successful_deps = [] 
        
        # Only proceed for packages where we successfully found a version
        for pkg_name, best_version in zip(unique_pkg_names, chosen_versions):
            if best_version:
                # We need the resolved version to check the available binary files
                arch_task = self._target_arch_or_all(pkg_name, best_version, target_arch)
                arch_tasks.append(arch_task)
                
                successful_deps.append((pkg_name, best_version))
        
        if not arch_tasks:
            return []
            
        # Run all architecture lookups concurrently
        effective_architectures = await asyncio.gather(*arch_tasks)
        
        
        # 4. Final assembly
        resolved_list = []
        
        for (pkg_name, chosen_version), effective_arch in zip(successful_deps, effective_architectures):
            # effective_arch is the final determined architecture (e.g., 'amd64' or 'all')
            if effective_arch:
                resolved_list.append([pkg_name, chosen_version, effective_arch])

        return resolved_list

