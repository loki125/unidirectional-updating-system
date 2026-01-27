from pydantic import BaseModel, Field, ConfigDict, model_validator
from typing import List, Optional

class PackageMetadata(BaseModel):
    # Enforce strict types
    Package: str
    Version: str
    Type: str
    Architecture: str
    Store_Path: str | None = None
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

    @model_validator(mode='after') 
    def compute_store_path(cls, model) -> str:
        """Generates the full store path including the hash"""
        model.Store_Path = f"{model.SHA256}-{model.Package}-{model.Version}"
        return model
    
        

