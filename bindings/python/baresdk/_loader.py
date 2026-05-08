"""
_loader.py — cffi FFI setup for baresdk.

Uses a hand-written cdef (see _cdef.py) for maximum portability across
platforms and Python versions, then opens the shared library.

The library is searched in this order:
  1. BARESDK_LIB environment variable (absolute path to .so/.dylib/.dll)
  2. The package directory (next to this file)
  3. The repo's dist/ directory (works without installing or copying the .so)
  4. System library paths (LD_LIBRARY_PATH / DYLD_LIBRARY_PATH)
"""

import os
import platform
import sys
from cffi import FFI
from ._cdef import CDEF

_here = os.path.dirname(os.path.abspath(__file__))
# repo root is 3 levels up: baresdk/bindings/python/baresdk/ -> baresdk/
_repo_root = os.path.normpath(os.path.join(_here, "..", "..", ".."))

ffi = FFI()
ffi.cdef(CDEF)

def _dist_candidates():
    """Yield candidate paths inside dist/ for the current platform/arch."""
    machine = platform.machine().lower()
    if sys.platform == "win32":
        yield os.path.join(_repo_root, "dist", "windows", "x64", "baresdk.dll")
    elif sys.platform == "darwin":
        yield os.path.join(_repo_root, "dist", "macos", "universal", "baresdk.dylib")
    else:
        arch = "arm64" if machine in ("aarch64", "arm64") else "x86_64"
        yield os.path.join(_repo_root, "dist", "linux", arch, "baresdk.so")

def _find_lib():
    env = os.environ.get("BARESDK_LIB")
    if env:
        return env

    if sys.platform == "win32":
        names = ["baresdk.dll"]
    elif sys.platform == "darwin":
        names = ["baresdk.dylib", "libbaresdk.dylib"]
    else:
        names = ["baresdk.so", "libbaresdk.so"]

    # 1. Next to the package (pip-installed wheel)
    for name in names:
        candidate = os.path.join(_here, name)
        if os.path.exists(candidate):
            return candidate

    # 2. Repo dist/ directory (works straight from source checkout)
    for candidate in _dist_candidates():
        if os.path.exists(candidate):
            return candidate

    # 3. System paths (LD_LIBRARY_PATH / DYLD_LIBRARY_PATH)
    return names[0]

lib = ffi.dlopen(_find_lib())

__all__ = ["ffi", "lib"]
