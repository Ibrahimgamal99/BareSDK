#!/usr/bin/env bash
# Build the SDK (if needed), regenerate the cffi header, and install the Python package.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
DIST_DIR="${ROOT}/dist/linux/x86_64"
INPUT="${ROOT}/include/baresdk.h"
OUTPUT="${ROOT}/bindings/python/baresdk/_baresdk_clean.h"

if [[ ! -f "${DIST_DIR}/baresdk.so" ]]; then
  echo "==> SDK not found — building it first..."
  bash "${ROOT}/scripts/build-linux.sh"
fi

echo "==> Regenerating clean header..."
gcc -E \
    -D'__extension__=' \
    -D'__attribute__(x)=' \
    -D'__restrict=' \
    -D'__inline__=inline' \
    -D'inline=' \
    "${INPUT}" \
    | awk -v src="${INPUT}" '
        /^# [0-9]+ / {
            match($0, /"([^"]+)"/, a)
            in_sdk = (a[1] == src)
            next
        }
        in_sdk { print }
    ' \
    | grep -v '__typeof__' \
    | grep -v 'nullptr_t' \
    | grep -v '__max_align' \
    | grep -v 'max_align_t' \
    | sed 's/_Bool/int/g' \
    | sed '/^[[:space:]]*$/N;/^\n$/d' \
    > "${OUTPUT}"
{
  printf 'typedef unsigned long size_t;\n'
  printf 'typedef int wchar_t;\n'
  cat "${OUTPUT}"
} > "${OUTPUT}.tmp" && mv "${OUTPUT}.tmp" "${OUTPUT}"

echo "==> Copying shared library..."
cp "${DIST_DIR}/baresdk.so" "${SCRIPT_DIR}/baresdk/baresdk.so"

echo "==> Installing Python package..."
pip install -e "${SCRIPT_DIR}"

echo "==> Building wheel..."
pip install --quiet wheel
cd "${SCRIPT_DIR}"
rm -rf dist build

# Default to manylinux_2_28_x86_64 for broad compatibility
MANYLINUX_TAG="manylinux_2_28_x86_64"

python setup.py bdist_wheel --quiet

echo "==> Retagging wheel to ${MANYLINUX_TAG}..."
python -m wheel tags --platform-tag "${MANYLINUX_TAG}" \
  dist/baresdk-*.whl --remove

WHL="$(ls "${SCRIPT_DIR}/dist"/baresdk-*-${MANYLINUX_TAG}.whl)"
echo ""
echo "Done. Wheel ready at:"
echo "  ${WHL}"
echo ""
echo "To upload to PyPI:"
echo "  twine upload ${WHL}"
echo ""
echo "Run an example:"
echo "  python ${SCRIPT_DIR}/examples/quickstart.py account.json                        # receive mode"
echo "  python ${SCRIPT_DIR}/examples/quickstart.py account.json bob@pbx.example.com    # dial"
echo "  python ${SCRIPT_DIR}/examples/quickstart.py alice@pbx.example.com secret        # legacy CLI (receive)"
echo "  python ${SCRIPT_DIR}/examples/quickstart.py alice@pbx.example.com secret bob@.. # legacy CLI (dial)"
