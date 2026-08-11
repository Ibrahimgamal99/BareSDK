#!/usr/bin/env bash
# Copy the built iOS xcframework into the Flutter plugin's ios/Frameworks/,
# where the podspec vendors it. Run after scripts/build-ios.sh (macOS).
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

SRC="${ROOT}/dist/ios/baresdk.xcframework"
DST="${ROOT}/bindings/flutter/ios/Frameworks"

if [ ! -d "${SRC}" ]; then
  echo "ERROR: ${SRC} not built — run scripts/build-ios.sh first." >&2
  exit 1
fi

mkdir -p "${DST}"
rm -rf "${DST}/baresdk.xcframework"
cp -R "${SRC}" "${DST}/baresdk.xcframework"

echo "Flutter plugin xcframework updated:"
du -sh "${DST}/baresdk.xcframework"
