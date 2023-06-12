import sys
import platform

from pathlib import Path

arch = "x86_64" if platform.machine() == "AMD64" else platform.machine()
lib_root = Path(__file__).parent.joinpath(sys.platform).joinpath(arch)
if lib_root.exists():
    sys.path.append(str(lib_root))
else:
    raise RuntimeError("Required binaries not available for current platform.")

from . import CuraEngineSimplifyPlugin


def getMetaData():
    return {}


def register(app):
    return {"extension": CuraEngineSimplifyPlugin.CuraEngineSimplifyPluginExtension()}