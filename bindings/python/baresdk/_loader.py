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

import ctypes
import os
import platform
import sys
from cffi import FFI


# Libraries that are NOT pre-installed on all desktop Linux distros.
# Key = .so name as the dynamic linker sees it.
# Value = install command per distro family.
_REQUIRED_LINUX_LIBS = {
    "libwebrtc-audio-processing-1.so.3": {
        "debian": "apt install libwebrtc-audio-processing-1",
        "fedora": "dnf install webrtc-audio-processing",
        "arch":   "pacman -S webrtc-audio-processing",
        "suse":   "zypper install libwebrtc-audio-processing-1-0",
    },
}

# baresdk.dll is built with x64-windows-static-md — OpenSSL and zlib are
# statically embedded, but the MSVC runtime is dynamically linked and may
# be absent on a clean Windows Server install.
_REQUIRED_WINDOWS_LIBS = {
    "vcruntime140.dll": (
        "Microsoft Visual C++ Redistributable is not installed.\n"
        "    → winget install Microsoft.VCRedist.2022.x64\n"
        "    or download from https://aka.ms/vs/17/release/vc_redist.x64.exe"
    ),
}


def _distro_family():
    try:
        with open("/etc/os-release") as f:
            vals = {}
            for line in f:
                k, _, v = line.partition("=")
                vals[k.strip()] = v.strip().strip('"').lower()
        combined = vals.get("ID", "") + " " + vals.get("ID_LIKE", "")
        for family in ("debian", "ubuntu", "fedora", "rhel", "centos", "arch", "suse"):
            if family in combined:
                return "debian" if family in ("ubuntu", "debian") else \
                       "fedora" if family in ("fedora", "rhel", "centos") else \
                       "arch"   if family == "arch" else \
                       "suse"
    except OSError:
        pass
    return None


def _check_windows_deps():
    missing = []
    for dll, message in _REQUIRED_WINDOWS_LIBS.items():
        try:
            ctypes.CDLL(dll)
        except OSError:
            missing.append(message)
    if missing:
        raise ImportError("baresdk: missing required runtime.\n\n" + "\n\n".join(missing))


def _check_linux_deps():
    missing = []
    for lib, _ in _REQUIRED_LINUX_LIBS.items():
        try:
            ctypes.CDLL(lib)
        except OSError:
            missing.append(lib)

    if not missing:
        return

    family = _distro_family()
    lines = ["baresdk: missing required system libraries.\n"]
    for lib in missing:
        cmds = _REQUIRED_LINUX_LIBS[lib]
        cmd = cmds.get(family) if family else None
        lines.append(f"  {lib}")
        if cmd:
            lines.append(f"    → {cmd}")
        else:
            lines.append("    → install via your distro's package manager")
    raise ImportError("\n".join(lines))

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


if sys.platform == "linux":
    _check_linux_deps()
elif sys.platform == "win32":
    _check_windows_deps()

lib = ffi.dlopen(_find_lib())

__all__ = ["ffi", "lib"]
