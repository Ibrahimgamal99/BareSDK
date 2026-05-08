#!/usr/bin/env bash
# Build baresdk for Linux x86_64.
# Output: dist/linux/x86_64/baresdk.a  +  dist/linux/x86_64/include/
#
# Prerequisites: cmake ninja gcc openssl-devel (or libssl-dev)
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_TYPE="${BUILD_TYPE:-Release}"
BUILD_DIR="${ROOT}/build/linux-x86_64"

cmake -S "${ROOT}" -B "${BUILD_DIR}" -GNinja \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DBARESDK_TLS=openssl \
  -DBARESDK_MODULES_PROFILE=desktop

cmake --build "${BUILD_DIR}" --target baresdk -j"$(nproc)"
cmake --install "${BUILD_DIR}"

# ── Link shared library ───────────────────────────────────────────────────────
DIST_DIR="${ROOT}/dist/linux/x86_64"
SO="${DIST_DIR}/baresdk.so"
echo ""
echo "=== Linking ${SO} ==="
# All deps linked dynamically — glibc's libm/libresolv.a are not PIC so cannot
# be embedded in a .so, and openssl-static no longer ships on modern distros.
# All of these are present on every Linux system.
gcc -shared \
  -Wl,--whole-archive "${DIST_DIR}/baresdk.a" -Wl,--no-whole-archive \
  -lssl -lcrypto -lz -lpthread -lm -lresolv -ldl \
  -lpulse -lopus \
  -o "${SO}"

echo ""
echo "Done. Output:"
ls -lh "${DIST_DIR}/baresdk.a" "${SO}"
