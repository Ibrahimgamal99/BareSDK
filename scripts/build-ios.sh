#!/usr/bin/env bash
# Build baresdk for iOS as a DYNAMIC xcframework (macOS + Xcode required).
#
# Output:
#   dist/ios/baresdk.xcframework    device (arm64) + simulator (arm64, x86_64)
#   dist/ios/<slice>/baresdk.a      static archives (for non-Flutter consumers)
#   dist/ios/include/               public headers
#
# A dynamic framework (not a static lib) is what the Flutter plugin vendors:
# with a static archive, symbols reached only via dart:ffi dlsym() are
# dead-stripped from Release app binaries; a dylib's exported symbols are
# always visible to DynamicLibrary.process(). CocoaPods embeds vendored
# dynamic frameworks automatically.
#
# Env knobs:
#   IOS_DEPLOYMENT_TARGET  minimum iOS (default 13.0)
#   BUILD_TYPE             CMake config (default Release)
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_TYPE="${BUILD_TYPE:-Release}"
MIN_IOS="${IOS_DEPLOYMENT_TARGET:-13.0}"
DIST="${ROOT}/dist/ios"
FRAMEWORKS_FOR_LINK=(-framework AVFoundation -framework AudioToolbox
                     -framework CoreAudio -framework CoreFoundation)

if ! command -v xcodebuild >/dev/null; then
  echo "ERROR: xcodebuild not found — this script requires macOS + Xcode." >&2
  exit 1
fi

# Fetch third-party sources (idempotent; mbedtls incl. its framework submodule).
BARESDK_TLS=mbedtls "${SCRIPT_DIR}/fetch-third-party.sh"

# ---------------------------------------------------------------------------
# Helper: configure + build the static archive for one slice
# ---------------------------------------------------------------------------
build_slice() {
  local NAME="$1"; local SYSROOT="$2"; local ARCHS="$3"
  local BUILD_DIR="${ROOT}/build/ios-${NAME}"

  rm -rf "${BUILD_DIR}"
  cmake -S "${ROOT}" -B "${BUILD_DIR}" -GXcode \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT="${SYSROOT}" \
    -DCMAKE_OSX_ARCHITECTURES="${ARCHS}" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="${MIN_IOS}" \
    -DBARESDK_TLS=mbedtls \
    -DBARESDK_MODULES_PROFILE=mobile

  cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}" --target baresdk \
    -- CODE_SIGNING_ALLOWED=NO

  local LIB
  LIB="$(find "${BUILD_DIR}" -name "baresdk.a" | head -1)"
  mkdir -p "${DIST}/${NAME}"
  cp "${LIB}" "${DIST}/${NAME}/baresdk.a"

  # Headers (identical across slices — copy once)
  if [ ! -d "${DIST}/include" ]; then
    mkdir -p "${DIST}/include"
    cp "${ROOT}/include/baresdk.h" "${DIST}/include/"
    local RE_SYSROOT="${BUILD_DIR}/sysroot"
    [ -d "${RE_SYSROOT}/include" ] && cp -r "${RE_SYSROOT}/include/." "${DIST}/include/"
  fi
}

# ---------------------------------------------------------------------------
# Helper: link one dylib for one arch of one slice
# ---------------------------------------------------------------------------
link_dylib() {
  local SDK="$1"; local ARCH="$2"; local TARGET_SUFFIX="$3"
  local ARCHIVE="$4"; local OUT="$5"
  local SDK_PATH
  SDK_PATH="$(xcrun --sdk "${SDK}" --show-sdk-path)"

  xcrun --sdk "${SDK}" clang -dynamiclib \
    -arch "${ARCH}" \
    -isysroot "${SDK_PATH}" \
    -target "${ARCH}-apple-ios${MIN_IOS}${TARGET_SUFFIX}" \
    -Wl,-all_load "${ARCHIVE}" \
    "${FRAMEWORKS_FOR_LINK[@]}" \
    -lc++ \
    -install_name @rpath/baresdk.framework/baresdk \
    -o "${OUT}"
}

# ---------------------------------------------------------------------------
# Helper: assemble baresdk.framework for one slice from a (fat) dylib
# ---------------------------------------------------------------------------
make_framework() {
  local DYLIB="$1"; local FW_DIR="$2"
  rm -rf "${FW_DIR}"
  mkdir -p "${FW_DIR}"
  cp "${DYLIB}" "${FW_DIR}/baresdk"
  cat > "${FW_DIR}/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key><string>en</string>
	<key>CFBundleExecutable</key><string>baresdk</string>
	<key>CFBundleIdentifier</key><string>dev.baresdk.core</string>
	<key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
	<key>CFBundleName</key><string>baresdk</string>
	<key>CFBundlePackageType</key><string>FMWK</string>
	<key>CFBundleShortVersionString</key><string>1.0.0</string>
	<key>CFBundleVersion</key><string>1</string>
	<key>MinimumOSVersion</key><string>${MIN_IOS}</string>
</dict>
</plist>
PLIST
  codesign --force --sign - "${FW_DIR}" >/dev/null 2>&1 || true
}

# ---------------------------------------------------------------------------
# Device slice: arm64 / iphoneos
# ---------------------------------------------------------------------------
echo "=== iOS device (arm64, iphoneos) ==="
build_slice "device" "iphoneos" "arm64"
link_dylib iphoneos arm64 "" \
  "${DIST}/device/baresdk.a" "${DIST}/device/baresdk.dylib"
make_framework "${DIST}/device/baresdk.dylib" "${DIST}/device/baresdk.framework"

# ---------------------------------------------------------------------------
# Simulator slice: arm64 + x86_64 (fat) / iphonesimulator
# Device-arm64 and simulator-arm64 are distinct slices by their Mach-O
# LC_BUILD_VERSION platform tag — lipo alone cannot merge them; the
# xcframework keeps them apart.
# ---------------------------------------------------------------------------
echo "=== iOS simulator (arm64 + x86_64, iphonesimulator) ==="
build_slice "simulator" "iphonesimulator" "arm64;x86_64"
link_dylib iphonesimulator arm64 "-simulator" \
  "${DIST}/simulator/baresdk.a" "${DIST}/simulator/baresdk-arm64.dylib"
link_dylib iphonesimulator x86_64 "-simulator" \
  "${DIST}/simulator/baresdk.a" "${DIST}/simulator/baresdk-x86_64.dylib"
lipo -create \
  "${DIST}/simulator/baresdk-arm64.dylib" \
  "${DIST}/simulator/baresdk-x86_64.dylib" \
  -output "${DIST}/simulator/baresdk.dylib"
make_framework "${DIST}/simulator/baresdk.dylib" \
  "${DIST}/simulator/baresdk.framework"

# ---------------------------------------------------------------------------
# Verify before packaging: exported API + WSS wrapper present
# ---------------------------------------------------------------------------
for SLICE in device simulator; do
  BIN="${DIST}/${SLICE}/baresdk.framework/baresdk"
  EXPORTS=$(xcrun nm -gU "${BIN}" | grep -c ' _baresdk_' || true)
  if [ "${EXPORTS}" -lt 40 ]; then
    echo "ERROR: ${SLICE}: only ${EXPORTS} baresdk_* symbols exported" >&2
    exit 1
  fi
  # Apple builds use the compile-time websock override — the wrapper owns the
  # public name and the real function is __real_websock_connect.
  if ! xcrun nm -U "${BIN}" | grep -q '___real_websock_connect'; then
    echo "ERROR: ${SLICE}: websock override missing (RE_WEBSOCK_CONNECT_OVERRIDE not applied?)" >&2
    exit 1
  fi
done

# ---------------------------------------------------------------------------
# Package as xcframework
# ---------------------------------------------------------------------------
echo "=== Creating xcframework ==="
rm -rf "${DIST}/baresdk.xcframework"
xcodebuild -create-xcframework \
  -framework "${DIST}/device/baresdk.framework" \
  -framework "${DIST}/simulator/baresdk.framework" \
  -output "${DIST}/baresdk.xcframework"

echo ""
echo "Done. Output: ${DIST}/baresdk.xcframework"
plutil -p "${DIST}/baresdk.xcframework/Info.plist" | grep -E "(Identifier|Library)" || true

# Refresh the Flutter plugin's vendored copy.
bash "${SCRIPT_DIR}/sync-flutter-xcframework.sh"
