#!/usr/bin/env bash
# Build libbare for macOS: arm64 + x86_64, then lipo into a universal archive.
# Output: dist/macos/universal/libbare.a  +  dist/macos/universal/include/
#
# Prerequisites (macOS only):
#   - Xcode or Command Line Tools (for lipo, libtool)
#   - cmake  (brew install cmake)
#   - openssl@3  (brew install openssl@3)
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_TYPE="${BUILD_TYPE:-Release}"
DIST="${ROOT}/dist/macos"

# Locate Homebrew OpenSSL
OPENSSL_ROOT="$(brew --prefix openssl@3 2>/dev/null || echo "")"
if [ -z "${OPENSSL_ROOT}" ]; then
  echo "WARNING: openssl@3 not found via brew. Set OPENSSL_ROOT_DIR manually." >&2
fi

build_arch() {
  local ARCH="$1"
  local BUILD_DIR="${ROOT}/build/macos-${ARCH}"

  cmake -S "${ROOT}" -B "${BUILD_DIR}" -GNinja \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_OSX_ARCHITECTURES="${ARCH}" \
    -DLIBBARE_TLS=openssl \
    -DLIBBARE_MODULES_PROFILE=desktop \
    ${OPENSSL_ROOT:+-DOPENSSL_ROOT_DIR="${OPENSSL_ROOT}"}

  cmake --build "${BUILD_DIR}" --target libbare -j"$(sysctl -n hw.logicalcpu)"
  cmake --install "${BUILD_DIR}"
}

echo "=== macOS arm64 ==="
build_arch arm64

echo "=== macOS x86_64 ==="
build_arch x86_64

echo "=== Creating universal binary with lipo ==="
mkdir -p "${DIST}/universal"
lipo -create \
  "${DIST}/arm64/libbare.a" \
  "${DIST}/x86_64/libbare.a" \
  -output "${DIST}/universal/libbare.a"

# Copy headers from one of the slices
cp -r "${DIST}/arm64/include" "${DIST}/universal/"

echo ""
echo "Done. Output:"
ls -lh "${DIST}/universal/libbare.a"
lipo -info "${DIST}/universal/libbare.a"
