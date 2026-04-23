import logging
from .PackageService import *

class SearchFactory:

    def __init__(self):
        self._engine_classes = {
            DebianPackageService.__name__: DebianPackageService,
            # "pip": PipSearchEngine
        }

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

        return engine_cls()





