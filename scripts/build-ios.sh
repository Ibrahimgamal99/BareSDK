#!/usr/bin/env bash
# Build baresdk for iOS: device (arm64) + simulator (arm64 + x86_64)
# Output: dist/ios/baresdk.xcframework  +  dist/ios/include/
#
# Prerequisites (macOS only):
#   - Xcode with iphoneos + iphonesimulator SDKs
#   - cmake  (brew install cmake)
#   - submodules initialised:
#       git submodule update --init third_party/mbedtls third_party/opus
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_TYPE="${BUILD_TYPE:-Release}"
MIN_IOS="${IOS_DEPLOYMENT_TARGET:-13.0}"
DIST="${ROOT}/dist/ios"

# ---------------------------------------------------------------------------
# Helper: configure and build one slice
# ---------------------------------------------------------------------------
build_slice() {
  local NAME="$1"; local SYSROOT="$2"; local ARCHS="$3"
  local BUILD_DIR="${ROOT}/build/ios-${NAME}"

  cmake -S "${ROOT}" -B "${BUILD_DIR}" -GXcode \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT="${SYSROOT}" \
    -DCMAKE_OSX_ARCHITECTURES="${ARCHS}" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="${MIN_IOS}" \
    -DBARESDK_TLS=mbedtls \
    -DBARESDK_MODULES_PROFILE=mobile

  cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}" --target baresdk \
    -- CODE_SIGNING_ALLOWED=NO

  # Locate and copy the merged archive
  local LIB
  LIB="$(find "${BUILD_DIR}" -name "baresdk.a" | head -1)"
  mkdir -p "${DIST}/${NAME}"
  cp "${LIB}" "${DIST}/${NAME}/baresdk.a"

  # Headers (same for both slices — copy only once)
  if [ ! -d "${DIST}/include" ]; then
    local SYSROOT_DIR
    SYSROOT_DIR="$(find "${BUILD_DIR}" -name "baresip.h" | head -1 | xargs dirname)"
    cp -r "${SYSROOT_DIR}" "${DIST}/include" 2>/dev/null || true
    # Fallback: copy from the re sysroot
    local RE_SYSROOT="${BUILD_DIR}/sysroot"
    [ -d "${RE_SYSROOT}/include" ] && cp -r "${RE_SYSROOT}/include/." "${DIST}/include/"
  fi
}

# ---------------------------------------------------------------------------
# Device slice: arm64 / iphoneos
# ---------------------------------------------------------------------------
echo "=== iOS device (arm64, iphoneos) ==="
build_slice "device" "iphoneos" "arm64"

# ---------------------------------------------------------------------------
# Simulator slice: arm64 + x86_64 (fat) / iphonesimulator
# Apple keeps device-arm64 and simulator-arm64 as separate slices
# by their Mach-O LC_BUILD_VERSION platform tag — lipo alone is NOT enough.
# ---------------------------------------------------------------------------
echo "=== iOS simulator (arm64 + x86_64, iphonesimulator) ==="
build_slice "simulator" "iphonesimulator" "arm64;x86_64"

# ---------------------------------------------------------------------------
# Package as xcframework
# ---------------------------------------------------------------------------
echo "=== Creating xcframework ==="
rm -rf "${DIST}/baresdk.xcframework"

xcodebuild -create-xcframework \
  -library "${DIST}/device/baresdk.a"    -headers "${DIST}/include" \
  -library "${DIST}/simulator/baresdk.a" -headers "${DIST}/include" \
  -output  "${DIST}/baresdk.xcframework"

echo ""
echo "Done. Output: ${DIST}/baresdk.xcframework"
echo ""
plutil -p "${DIST}/baresdk.xcframework/Info.plist" | grep -E "(Identifier|library)"
