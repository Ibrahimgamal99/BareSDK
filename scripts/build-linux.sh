#!/usr/bin/env bash
# Build libbare for Linux x86_64.
# Output: dist/linux/x86_64/libbare.a  +  dist/linux/x86_64/include/
#
# Prerequisites: cmake ninja gcc openssl-devel (or libssl-dev)
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_TYPE="${BUILD_TYPE:-Release}"
BUILD_DIR="${ROOT}/build/linux-x86_64"

cmake -S "${ROOT}" -B "${BUILD_DIR}" -GNinja \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DLIBBARE_TLS=openssl \
  -DLIBBARE_MODULES_PROFILE=desktop

cmake --build "${BUILD_DIR}" --target libbare -j"$(nproc)"
cmake --install "${BUILD_DIR}"

echo ""
echo "Done. Output:"
ls -lh "${ROOT}/dist/linux/x86_64/libbare.a" 2>/dev/null || \
  find "${ROOT}/dist/linux" -name "libbare.a" -exec ls -lh {} \;
