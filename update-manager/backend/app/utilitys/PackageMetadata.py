from pydantic import BaseModel, Field, ConfigDict, field_validator
from typing import List, Optional

class PackageMetadata(BaseModel):
    # Enforce strict types
    Package: str
    Version: str
    Type: str
    Architecture: str
    Store_Path: str = "/opt/store/"
    Dependencies: List[List[str]] | List
    SHA256: str
    Installed_Size: int = Field(alias="Installed-Size")
    Size: int = 0
    Filename: str
    Latest: bool = False
    Timestamp: Optional[str] = ""

    # This prevents extra fields from being added accidentally
    model_config = ConfigDict(populate_by_name=True, extra='ignore')

    def generate_id(self) -> str:
        """Constructs the unique _id: Package_Version_Type"""
        return f"{self.Package}_{self.Version}_{self.Architecture}"
    
    @staticmethod
    def build_id(package: str, version: str, arch: str) -> str:
        return f"{package}_{version}_{arch}"

    def get_final_store_path(self) -> str:
        """Generates the full store path including the hash"""
        return f"{self.Store_Path}{self.SHA256}-{self.Package}-{self.Version}"
    
        

