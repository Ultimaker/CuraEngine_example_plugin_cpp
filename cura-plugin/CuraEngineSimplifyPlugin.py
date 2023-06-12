from typing import Optional

from PyQt6.QtCore import QObject

from UM.Logger import Logger
from UM.Extension import Extension


class CuraEngineSimplifyPluginExtension(Extension, QObject):
    def __init__(self, parent: Optional[QObject] = None) -> None:
        Logger.info("Starting the CuraEnginePlugin Simplify")
        QObject.__init__(self, parent)
        Extension.__init__(self)
