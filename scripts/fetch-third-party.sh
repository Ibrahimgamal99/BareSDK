#!/usr/bin/env bash
# Clone the third_party sources the build needs, at the revisions below.
# third_party/ is gitignored, so this is how a fresh checkout gets them.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# name|url|revision
DEPS=(
  "re|https://github.com/baresip/re.git|2049ea9c5dea689f93485c26bbc244d16d5e7809"
  "baresip|https://github.com/baresip/baresip.git|a9b3749608d129f0017ce940b24b777fa1f2d38b"
  "opus|https://github.com/xiph/opus.git|f8f99516092f4311a9b0784f190ff982df8eb2e6"
)
if [[ "${BARESDK_TLS:-openssl}" == "mbedtls" ]]; then
  DEPS+=("mbedtls|https://github.com/Mbed-TLS/mbedtls.git|2f2b202f8e72ef01aa0b743ef9df2abb0a3527d9")
fi

for dep in "${DEPS[@]}"; do
  IFS='|' read -r name url rev <<<"${dep}"
  dir="${ROOT}/third_party/${name}"

  # CMake gates on CMakeLists.txt, so that marks an already-usable tree.
  if [[ -f "${dir}/CMakeLists.txt" ]]; then
    echo "==> ${name}: present"
    continue
  fi

  echo "==> ${name}: cloning ${rev:0:12} from ${url}"
  git init -q "${dir}"
  git -C "${dir}" remote add origin "${url}" 2>/dev/null || true
  git -C "${dir}" fetch -q --depth 1 origin "${rev}"
  git -C "${dir}" checkout -q FETCH_HEAD
done

# mbedTLS needs its `framework` submodule (scripts/config.py imports
# mbedtls_framework from it). Fetch it shallowly at the pinned gitlink rev.
MBEDTLS_DIR="${ROOT}/third_party/mbedtls"
if [[ -f "${MBEDTLS_DIR}/.gitmodules" && ! -f "${MBEDTLS_DIR}/framework/CMakeLists.txt" ]]; then
  fw_rev="$(git -C "${MBEDTLS_DIR}" ls-tree HEAD framework | awk '{print $3}')"
  if [[ -n "${fw_rev}" ]]; then
    echo "==> mbedtls/framework: cloning ${fw_rev:0:12}"
    git init -q "${MBEDTLS_DIR}/framework"
    git -C "${MBEDTLS_DIR}/framework" remote add origin \
      "https://github.com/Mbed-TLS/mbedtls-framework.git" 2>/dev/null || true
    git -C "${MBEDTLS_DIR}/framework" fetch -q --depth 1 origin "${fw_rev}"
    git -C "${MBEDTLS_DIR}/framework" checkout -q FETCH_HEAD
  fi
fi
