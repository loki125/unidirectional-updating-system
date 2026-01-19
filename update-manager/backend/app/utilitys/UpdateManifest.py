from pydantic import BaseModel, Field, ConfigDict
from datetime import datetime
from typing import List
from .PackageMetadata import PackageMetadata

class UpdateManifest(BaseModel):
    update_id: str = Field(alias="Update_id")
    format_version: str = Field(alias="Format_version")
    timestamp: datetime = Field(alias="Timestamp")
    total_size_byte: int = Field(alias="Total_size_byte")
    packages: List[PackageMetadata] = Field(alias="Packages")

    # This ensures the JSON output uses the TitleCase keys you want
    model_config = ConfigDict(populate_by_name=True, alias_generator=None)

    def to_json(self):
        return self.model_dump_json(by_alias=True)