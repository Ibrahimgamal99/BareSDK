#!/usr/bin/env bash
# Build baresdk for Linux x86_64.
# Output: dist/linux/x86_64/baresdk.a  +  dist/linux/x86_64/include/
#
# Prerequisites: cmake ninja gcc openssl-devel (or libssl-dev)
#                webrtc-audio-processing-devel (build-time only; optional at runtime)
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_TYPE="${BUILD_TYPE:-Release}"
BUILD_DIR="${ROOT}/build/linux-x86_64"

cmake -S "${ROOT}" -B "${BUILD_DIR}" -GNinja \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DBARESDK_TLS=openssl \
  -DBARESDK_MODULES_PROFILE=desktop \
  -DBARESDK_WITH_WEBRTC_AEC=ON \
  -DHAVE_THREADS=OFF \
  -DCMAKE_C_FLAGS="-std=gnu11 -D_GNU_SOURCE -D_DEFAULT_SOURCE -Wno-error=deprecated-declarations"

cmake --build "${BUILD_DIR}" --target baresdk -j"$(nproc)"
cmake --install "${BUILD_DIR}"

# ── Link shared library ───────────────────────────────────────────────────────
DIST_DIR="${ROOT}/dist/linux/x86_64"
SO="${DIST_DIR}/baresdk.so"
echo ""
echo "=== Linking ${SO} ==="
# glibc_symver.c is already compiled into baresdk.a via the SRC_MODE build.
gcc -shared \
  -Wl,--wrap=websock_connect \
  -Wl,--whole-archive "${DIST_DIR}/baresdk.a" -Wl,--no-whole-archive \
  -lssl -lcrypto -lz -lpthread -lm -lresolv -ldl -lstdc++ \
  -lpulse \
  -Wl,--default-symver \
  -Wl,--version-script="${SCRIPT_DIR}/glibc-compat.ver" \
  -Wl,-z,defs \
  -o "${SO}"

echo ""
echo "Done. Output:"
ls -lh "${DIST_DIR}/baresdk.a" "${SO}"
