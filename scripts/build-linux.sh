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

echo ""
echo "Done. Output:"
ls -lh "${ROOT}/dist/linux/x86_64/baresdk.a" 2>/dev/null || \
  find "${ROOT}/dist/linux" -name "baresdk.a" -exec ls -lh {} \;
