#!/usr/bin/env bash
# Build baresdk for Android (arm64-v8a, armeabi-v7a, x86_64).
# Output: dist/android/<abi>/baresdk.a  +  dist/android/<abi>/include/
#
# Prerequisites:
#   - Android NDK (set ANDROID_NDK or detected from ANDROID_NDK_ROOT /
#     ANDROID_NDK_LATEST_HOME)
#   - cmake ninja
#   - third_party/mbedtls submodule initialised
#       git submodule update --init third_party/mbedtls
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_TYPE="${BUILD_TYPE:-Release}"
ABIS="${ANDROID_ABIS:-arm64-v8a armeabi-v7a x86_64}"
API="${ANDROID_PLATFORM:-android-24}"  # re requires >= 24 for getifaddrs

# Locate NDK
NDK="${ANDROID_NDK:-${ANDROID_NDK_ROOT:-${ANDROID_NDK_LATEST_HOME:-}}}"
if [ -z "${NDK}" ]; then
  echo "ERROR: Set ANDROID_NDK to the NDK root directory." >&2
  exit 1
fi
TOOLCHAIN="${NDK}/build/cmake/android.toolchain.cmake"
if [ ! -f "${TOOLCHAIN}" ]; then
  echo "ERROR: NDK toolchain not found at ${TOOLCHAIN}" >&2
  exit 1
fi

for ABI in ${ABIS}; do
  echo "=== Building Android ${ABI} ==="
  BUILD_DIR="${ROOT}/build/android-${ABI}"

  cmake -S "${ROOT}" -B "${BUILD_DIR}" -GNinja \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" \
    -DANDROID_ABI="${ABI}" \
    -DANDROID_PLATFORM="${API}" \
    -DANDROID_STL=c++_static \
    -DBARESDK_TLS=mbedtls \
    -DBARESDK_MODULES_PROFILE=mobile

  cmake --build "${BUILD_DIR}" --target baresdk -j"$(nproc)"
  cmake --install "${BUILD_DIR}"

  echo "  -> dist/android/${ABI}/baresdk.a"
done

echo ""
echo "Done. Outputs:"
find "${ROOT}/dist/android" -name "baresdk.a" -exec ls -lh {} \;
