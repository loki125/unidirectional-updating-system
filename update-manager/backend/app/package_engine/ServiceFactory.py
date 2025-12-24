import logging
from .PackageService import *

class SearchFactory:

    def __init__(self, db_instance: db):
        self._engine_classes = {
            DebianPackageService.__name__: DebianPackageService,
            # "pip": PipSearchEngine
        }

        self._global_db_instance = db_instance # db(uri="mongodb://mongo:27017", db_name="um-db") need to add db configurations

    def get_engine(self, pack_type: str):
        """
        Return an engine instance for the given package type,
        already initialized with the global DB instance.

        :param pack_type: The package type to use.
        """
        class_name = f"{pack_type}PackageService"
        engine_cls = self._engine_classes.get(class_name)

        if not engine_cls:
            return None

        return engine_cls(self._global_db_instance)

    async def global_refresh_metadata(self):
        """
        updates database for latest metadata of ALL packages from every service
        """
        engine = None
        try:
            for engine_class in self._engine_classes.values():
                engine = engine_class(self._global_db_instance)
                await engine.refresh_metadata()
        except Exception as e:
            logging.error(f"Tried refreshing metadata for engine {engine}\n{e}")




