#!/usr/bin/env bash
# Copy the built Android shared libraries into the Flutter plugin's jniLibs,
# stripped of debug info (the dist/ copies keep full symbols for debugging).
#
# Run after scripts/build-android.sh. Requires ANDROID_NDK (or auto-detected
# like build-android.sh) for llvm-strip.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

NDK="${ANDROID_NDK:-${ANDROID_NDK_ROOT:-${ANDROID_NDK_LATEST_HOME:-}}}"
if [ -z "${NDK}" ]; then
  echo "ERROR: Set ANDROID_NDK to the NDK root directory." >&2
  exit 1
fi
NDK_HOST_TAG="$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"
STRIP="${NDK}/toolchains/llvm/prebuilt/${NDK_HOST_TAG}/bin/llvm-strip"

JNILIBS="${ROOT}/bindings/flutter/android/src/main/jniLibs"
ABIS="${ANDROID_ABIS:-arm64-v8a armeabi-v7a x86_64}"

for ABI in ${ABIS}; do
  SRC="${ROOT}/dist/android/${ABI}/libbaresdk.so"
  if [ ! -f "${SRC}" ]; then
    echo "ERROR: ${SRC} not built — run scripts/build-android.sh first." >&2
    exit 1
  fi
  mkdir -p "${JNILIBS}/${ABI}"
  "${STRIP}" --strip-unneeded -o "${JNILIBS}/${ABI}/libbaresdk.so" "${SRC}"
done

echo "jniLibs updated:"
find "${JNILIBS}" -name '*.so' -exec ls -lh {} \;
