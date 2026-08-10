#!/usr/bin/env bash
# Build baresdk for Android (arm64-v8a, armeabi-v7a, x86_64).
# Output per ABI:
#   dist/android/<abi>/baresdk.a       merged static archive
#   dist/android/<abi>/libbaresdk.so   shared library (16 KB page aligned)
#   dist/android/<abi>/include/
#
# Prerequisites:
#   - Android NDK (set ANDROID_NDK or detected from ANDROID_NDK_ROOT /
#     ANDROID_NDK_LATEST_HOME)
#   - cmake ninja
#
# Env knobs:
#   ANDROID_ABIS         ABIs to build (default: arm64-v8a armeabi-v7a x86_64)
#   ANDROID_PLATFORM     API level (default: android-24; re needs >= 24)
#   BARESDK_BUILD_ROOT   Build directory root (default: <repo>/build).
#                        Point at a native filesystem (e.g. ~/.cache) when the
#                        repo lives on NTFS — incremental builds there silently
#                        reuse stale objects.
#   BARESDK_INCREMENTAL  Set to 1 to keep existing build dirs (default: clean)
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_TYPE="${BUILD_TYPE:-Release}"
ABIS="${ANDROID_ABIS:-arm64-v8a armeabi-v7a x86_64}"
API="${ANDROID_PLATFORM:-android-24}"  # re requires >= 24 for getifaddrs
BUILD_ROOT="${BARESDK_BUILD_ROOT:-${ROOT}/build}"

# Fetch third-party sources (idempotent — skips dirs that already exist).
BARESDK_TLS=mbedtls "${SCRIPT_DIR}/fetch-third-party.sh"

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
NDK_HOST_TAG="$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"
LLVM_BIN="${NDK}/toolchains/llvm/prebuilt/${NDK_HOST_TAG}/bin"

for ABI in ${ABIS}; do
  echo "=== Building Android ${ABI} ==="
  BUILD_DIR="${BUILD_ROOT}/android-${ABI}"
  if [ "${BARESDK_INCREMENTAL:-0}" != "1" ]; then
    rm -rf "${BUILD_DIR}"
  fi

  cmake -S "${ROOT}" -B "${BUILD_DIR}" -GNinja \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" \
    -DANDROID_ABI="${ABI}" \
    -DANDROID_PLATFORM="${API}" \
    -DANDROID_STL=c++_static \
    -DBARESDK_TLS=mbedtls \
    -DBARESDK_MODULES_PROFILE=mobile \
    -DBARESDK_SHARED=ON

  cmake --build "${BUILD_DIR}" --target baresdk baresdk_shared -j"$(nproc)"
  cmake --install "${BUILD_DIR}"

  DIST_DIR="${ROOT}/dist/android/${ABI}"
  SO="${DIST_DIR}/libbaresdk.so"

  # ── Verify the artifact before trusting it ────────────────────────────────
  # (grep -q + pipefail is a footgun: grep exits at first match and llvm-nm
  #  dies with SIGPIPE — capture the symbol table once instead.)
  echo "  === Verifying ${SO} ==="
  DEFINED=$("${LLVM_BIN}/llvm-nm" -D --defined-only "${SO}")
  EXPORTS=$(grep -c ' T baresdk_' <<<"${DEFINED}" || true)
  if [ "${EXPORTS}" -lt 40 ]; then
    echo "ERROR: only ${EXPORTS} baresdk_* symbols exported" >&2; exit 1
  fi
  if ! grep -q '__wrap_websock_connect' <<<"${DEFINED}"; then
    echo "ERROR: websock_connect wrapper missing (--wrap not applied?)" >&2; exit 1
  fi
  UNDEF=$("${LLVM_BIN}/llvm-nm" -D --undefined-only "${SO}" \
          | grep -E ' (mbedtls_|opus_|__real_websock_connect|AAudio)' || true)
  if [ -n "${UNDEF}" ]; then
    echo "ERROR: unresolved symbols in ${SO}:" >&2
    echo "${UNDEF}" >&2; exit 1
  fi
  BAD_ALIGN=$("${LLVM_BIN}/llvm-readelf" -l "${SO}" | awk '/LOAD/ {print $NF}' \
              | grep -v '0x4000' || true)
  if [ -n "${BAD_ALIGN}" ]; then
    echo "ERROR: LOAD segment not 16 KB aligned: ${BAD_ALIGN}" >&2; exit 1
  fi

  echo "  -> dist/android/${ABI}/baresdk.a"
  echo "  -> dist/android/${ABI}/libbaresdk.so"
done

echo ""
echo "Done. Outputs:"
find "${ROOT}/dist/android" \( -name "baresdk.a" -o -name "libbaresdk.so" \) -exec ls -lh {} \;

# Refresh the Flutter plugin's bundled (stripped) copies.
bash "${SCRIPT_DIR}/sync-flutter-jnilibs.sh"
