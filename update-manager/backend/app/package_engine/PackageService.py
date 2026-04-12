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
from utilitys.PackageMetadata import PackageMetadata, IGNORE_PACKAGES, ESSENTIAL_PACKAGES

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
    
    @abstractmethod
    async def get_recursive_dependencies(self, metadata: 'PackageMetadata', file_path: str) -> List['PackageMetadata']:
        """
        Dynamically resolves the full dependency tree for a given package. This method 
        should handle complex scenarios such as multi-constraint intersections, 
        re-evaluating packages if new constraints are discovered, and ensuring essential 
        packages are included if they appear in 'Breaks'.
        
        :param metadata: The PackageMetadata of the root package.
        :param file_path: The local path to the root package file.
        :return: A list of PackageMetadata objects representing all resolved dependencies.
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

    async def _get_raw_control_data(self, file_path: str) -> Dict[str, str]:
        """Helper to extract control fields from a .deb file."""
        if not os.path.exists(file_path):
            raise FileNotFoundError(f"File not found: {file_path}")

        process = await asyncio.create_subprocess_exec(
            'dpkg-deb', '-I', file_path,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE
        )
        stdout, stderr = await process.communicate()

        if process.returncode != 0:
            raise Exception(f"dpkg-deb error: {stderr.decode()}")

        control_data = {}
        for line in stdout.decode().splitlines():
            if ':' in line:
                key, value = line.split(':', 1)
                control_data[key.strip()] = value.strip()
        return control_data

    def _parse_constraints(self, depends_str: str) -> Dict[str, List[Tuple[str, str]]]:
        """Helper to parse a dependency string into a dictionary of constraints."""
        package_constraints = {}
        if not depends_str:
            return package_constraints

        for part in depends_str.split(','):
            preferred_dep = part.split('|')[0].strip()
            # Regex groups: 1=name, 3=arch, 5=operator, 6=version
            match = re.search(r'^([a-z0-9\+\-\.]+)(:([a-z0-9]+))?(\s*\((<<|<=|=|>=|>>)\s*([^)]+)\))?', preferred_dep)
            if not match:
                continue

            pkg_name = match.group(1)
            constraint_op = match.group(5)
            constraint_ver = match.group(6)
            
            if pkg_name not in package_constraints:
                package_constraints[pkg_name] =[]
            
            if constraint_op and constraint_ver:
                package_constraints[pkg_name].append((constraint_op, constraint_ver))
                
        return package_constraints

    async def _resolve_dependencies(self, depends_str: str, target_arch: str) -> List[List[str]]:
        """Updated to use the new helper function."""
        package_constraints = self._parse_constraints(depends_str)
        
        version_tasks =[]
        unique_pkg_names = list(package_constraints.keys()) 

        for pkg_name in unique_pkg_names:
            constraints = package_constraints[pkg_name]
            task = self._find_best_version(pkg_name, constraints) 
            version_tasks.append(task)
        
        if not version_tasks: return[]
        chosen_versions = await asyncio.gather(*version_tasks)
        
        arch_tasks = []
        successful_deps =[] 
        
        for pkg_name, best_version in zip(unique_pkg_names, chosen_versions):
            if best_version:
                arch_task = self._target_arch_or_all(pkg_name, best_version, target_arch)
                arch_tasks.append(arch_task)
                successful_deps.append((pkg_name, best_version))
        
        if not arch_tasks: return[]
        effective_architectures = await asyncio.gather(*arch_tasks)
        
        resolved_list =[]
        for (pkg_name, chosen_version), effective_arch in zip(successful_deps, effective_architectures):
            if effective_arch:
                resolved_list.append([pkg_name, chosen_version, effective_arch])

        return resolved_list

    async def get_package_metadata(self, file_path: str) -> PackageMetadata:
        """
        Modified to include both Depends and Pre-Depends in the required dependencies.
        """
        control_data = await self._get_raw_control_data(file_path)

        sha256_hash = hashlib.sha256()
        with open(file_path, "rb") as f:
            for byte_block in iter(lambda: f.read(self.BUFFER), b""):
                sha256_hash.update(byte_block)

        arch = control_data.get("Architecture", "")
        
        # Merge Depends and Pre-Depends
        depends_str = control_data.get("Depends", "")
        pre_depends_str = control_data.get("Pre-Depends", "")
        all_depends =[]
        if depends_str: all_depends.append(depends_str)
        if pre_depends_str: all_depends.append(pre_depends_str)
        combined_depends = ", ".join(all_depends)

        depends = await self._resolve_dependencies(combined_depends, arch)

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

    async def get_recursive_dependencies(self, metadata: 'PackageMetadata', file_path: str) -> List['PackageMetadata']:
        """
        Dynamically resolves the dependency tree. Handles:
        1. Multi-constraint intersections (the 'middle ground').
        2. Re-evaluating/Re-downloading packages if new strict constraints are discovered.
        3. Ensuring essential packages are queued if they are in 'Breaks'.
        """
        arch = metadata.Architecture
        
        # global_constraints maps a package to ALL of its version requirements
        global_constraints: Dict[str, List[Tuple[str, str]]] = {}
        
        resolved_packages: Dict[str, 'PackageMetadata'] = {} 
        replaces_provides = set()
        enqueued = set()
        queue = asyncio.Queue()
        
        # Track relationships to attach dependencies to their proper parent later
        # Format: List of Tuples (parent_package_name, required_package_name)
        dependency_edges: List[Tuple[str, str]] = []

        # NEW: Added parent_metadata argument
        async def check_and_queue(pkg: str, reqs: List[Tuple[str, str]], parent_metadata: 'PackageMetadata', invert: bool = False, is_essential_break: bool = False):
            """Evaluates constraints and decides if a package needs to be queued or RE-queued."""
            if pkg in IGNORE_PACKAGES:
                return

            if pkg not in global_constraints:
                global_constraints[pkg] = []
            
            new_constraints = []
            for op, ver in reqs:
                if invert:
                    # Invert logic for Breaks (e.g., Breaks < 13 -> requires >= 13)
                    op = {"<<": ">=", "<=": ">>", "=": "!=", ">=": "<<", ">>": "<="}.get(op, op)
                new_constraints.append((op, ver))
                global_constraints[pkg].append((op, ver))
            
            # If we ALREADY downloaded this package, check if it satisfies these NEW rules.
            if pkg in resolved_packages:
                resolved_ver = resolved_packages[pkg].Version
                for op, ver in new_constraints:
                    if not await self._compare_versions(resolved_ver, op, ver):
                        logging.warning(f"Version conflict! Currently have {pkg} v{resolved_ver}, but need {op} {ver}. Re-queueing to find middle ground.")
                        await queue.put(pkg)
                        break
            
            # Decide if this requirement means we need to download it
            should_queue = not invert or is_essential_break
            
            if should_queue:
                # NEW: Record that the parent requires this package
                dependency_edges.append((parent_metadata.Package, pkg))
                
                if pkg not in enqueued:
                    enqueued.add(pkg)
                    await queue.put(pkg)

        # NEW: Added parent_metadata argument
        async def process_control_data(c_data: Dict[str, str], parent_metadata: 'PackageMetadata'):
            """Extracts logic from the control file of a downloaded package."""
            
            # 1. Standard Dependencies (Depends + Pre-Depends)
            d_str = c_data.get("Depends", "")
            pd_str = c_data.get("Pre-Depends", "")
            parsed_d = self._parse_constraints(f"{d_str}, {pd_str}".strip(", "))
            
            for dep_pkg, reqs in parsed_d.items():
                await check_and_queue(dep_pkg, reqs, parent_metadata, invert=False)

            # 2. Replaces / Provides (Keeps us from downloading obsolete packages)
            rep_str = c_data.get("Replaces", "")
            for part in rep_str.split(','):
                cleaned = part.split('|')[0].strip()
                if cleaned:
                    r_pkg = cleaned.split()[0]
                    replaces_provides.add(r_pkg)

            # 3. Breaks / Conflicts
            brk_str = c_data.get("Breaks", "")
            parsed_brk = self._parse_constraints(brk_str)
            
            for brk_pkg, reqs in parsed_brk.items():
                is_essential = brk_pkg in ESSENTIAL_PACKAGES
                await check_and_queue(brk_pkg, reqs, parent_metadata, invert=True, is_essential_break=is_essential)

        # ----------------- Start the recursive loop -----------------
        root_control = await self._get_raw_control_data(file_path)
        enqueued.add(metadata.Package)
        
        # Add the root package to resolved so we don't try to download it again
        resolved_packages[metadata.Package] = metadata 
        
        # Pass the root metadata as the parent context
        await process_control_data(root_control, metadata)

        # Process the download queue
        while not queue.empty():
            current_pkg = await queue.get()

            # Skip if another package replaces it
            if current_pkg in replaces_provides:
                continue

            constraints = global_constraints.get(current_pkg, [])
            best_version = await self._find_best_version(current_pkg, constraints)

            if not best_version:
                logging.error(f"Unresolvable constraints! No version of {current_pkg} satisfies: {constraints}")
                continue

            if current_pkg in resolved_packages and resolved_packages[current_pkg].Version == best_version:
                continue 

            target_arch = await self._target_arch_or_all(current_pkg, best_version, arch)
            if not target_arch:
                logging.warning(f"Architecture {arch} not found for {current_pkg}_{best_version}")
                continue

            try:
                dl_path = await self.get_package_file(current_pkg, best_version, target_arch)
            except Exception as e:
                logging.error(f"Failed to download {current_pkg}: {e}")
                continue

            pkg_meta = await self.get_package_metadata(dl_path)
            resolved_packages[current_pkg] = pkg_meta

            curr_control = await self._get_raw_control_data(dl_path)
            
            # Pass the newly resolved package metadata as the parent context
            await process_control_data(curr_control, pkg_meta)

        # Link dependencies to their parents instead of just the root tree
        for parent_name, child_name in dependency_edges:
            if parent_name in resolved_packages and child_name in resolved_packages:
                parent_meta = resolved_packages[parent_name]
                child_meta = resolved_packages[child_name]
                
                # Check if it's already in parent's Dependencies to avoid duplicates
                exists = any(d[0] == child_name for d in parent_meta.Dependencies)
                if not exists:
                    parent_meta.Dependencies.append([child_meta.Package, child_meta.Version, child_meta.Architecture])

        # Return all dependencies (excluding the original root package we started with)
        final_metadata_list = [v for k, v in resolved_packages.items() if k != metadata.Package]

        return final_metadata_list
