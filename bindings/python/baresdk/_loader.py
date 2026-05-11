"""
_loader.py — cffi FFI setup for baresdk.

Reads _baresdk_clean.h (generated from include/baresdk.h by build.sh) so
the Python binding stays in sync with the C header automatically.

The library is searched in this order:
  1. BARESDK_LIB environment variable (absolute path to .so/.dylib/.dll)
  2. LD_LIBRARY_PATH / DYLD_LIBRARY_PATH directories
  3. The package directory (bundled .so copied by build.sh)
  4. The repo's dist/ directory (source checkout without install)
  5. System library paths
"""

import os
import platform
import sys
from cffi import FFI

_here = os.path.dirname(os.path.abspath(__file__))
_repo_root = os.path.normpath(os.path.join(_here, "..", "..", ".."))

ffi = FFI()
with open(os.path.join(_here, "_baresdk_clean.h")) as _f:
    ffi.cdef(_f.read())


def _lib_names():
    if sys.platform == "win32":
        return ["baresdk.dll"]
    if sys.platform == "darwin":
        return ["baresdk.dylib"]
    return ["baresdk.so"]


def _dist_candidates():
    machine = platform.machine().lower()
    if sys.platform == "win32":
        yield os.path.join(_repo_root, "dist", "windows", "x64", "baresdk.dll")
    elif sys.platform == "darwin":
        yield os.path.join(_repo_root, "dist", "macos", "universal", "baresdk.dylib")
    else:
        arch = "arm64" if machine in ("aarch64", "arm64") else "x86_64"
        yield os.path.join(_repo_root, "dist", "linux", arch, "baresdk.so")


def _find_lib():
    # 1. Explicit override
    env = os.environ.get("BARESDK_LIB")
    if env:
        return env

    names = _lib_names()

    # 2. LD_LIBRARY_PATH / DYLD_LIBRARY_PATH
    if sys.platform == "darwin":
        ld_path = os.environ.get("DYLD_LIBRARY_PATH", "")
    else:
        ld_path = os.environ.get("LD_LIBRARY_PATH", "")
    for directory in filter(None, ld_path.split(":")):
        for name in names:
            candidate = os.path.join(directory, name)
            if os.path.exists(candidate):
                return candidate

    # 3. Bundled next to the package (copied by build.sh)
    for name in names:
        candidate = os.path.join(_here, name)
        if os.path.exists(candidate):
            return candidate

    # 4. Repo dist/ (source checkout)
    for candidate in _dist_candidates():
        if os.path.exists(candidate):
            return candidate

    # 5. System paths
    return names[0]


lib = ffi.dlopen(_find_lib())

__all__ = ["ffi", "lib"]
