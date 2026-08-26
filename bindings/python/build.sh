#!/usr/bin/env bash
# Build the SDK (if needed), regenerate the cffi header, and install the Python package.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
DIST_DIR="${ROOT}/dist/linux/x86_64"
INPUT="${ROOT}/include/echosdk.h"
OUTPUT="${ROOT}/bindings/python/echo_sdk/_echosdk_clean.h"

if [[ ! -f "${DIST_DIR}/echosdk.so" ]]; then
  echo "==> SDK not found — building it first..."
  bash "${ROOT}/scripts/build-linux.sh"
fi

echo "==> Regenerating clean header..."
bash "${SCRIPT_DIR}/gen_header.sh" "${INPUT}" "${OUTPUT}"

echo "==> Copying shared library..."
cp "${DIST_DIR}/echosdk.so" "${SCRIPT_DIR}/echo_sdk/echosdk.so"
rm -f "${SCRIPT_DIR}/echo_sdk/echosdk.dll" "${SCRIPT_DIR}/echo_sdk/echosdk.dylib"

echo "==> Uninstalling existing package..."
pip uninstall -y echo-sdk 2>/dev/null || true

echo "==> Installing Python package..."
pip install -e "${SCRIPT_DIR}"

echo "==> Building wheel..."
pip install --quiet wheel
rm -rf "${SCRIPT_DIR}/build"
mkdir -p "${SCRIPT_DIR}/dist"
rm -f "${SCRIPT_DIR}/dist"/echo_sdk-*-manylinux*.whl "${SCRIPT_DIR}/dist"/echo_sdk-*-linux*.whl

# Default to manylinux_2_34_x86_64 for broad compatibility
MANYLINUX_TAG="manylinux_2_34_x86_64"

# Run from the package dir: setuptools reads name/version from the pyproject.toml
# in the *current* directory, so invoking setup.py by path from elsewhere silently
# produces an empty unknown-0.0.0 wheel.
( cd "${SCRIPT_DIR}" && python setup.py bdist_wheel --quiet --dist-dir "${SCRIPT_DIR}/dist" --bdist-dir "${SCRIPT_DIR}/build" )

echo "==> Retagging wheel to ${MANYLINUX_TAG}..."
python -m wheel tags --platform-tag "${MANYLINUX_TAG}" \
  "${SCRIPT_DIR}/dist"/echo_sdk-*-linux_x86_64.whl --remove

WHL="$(ls "${SCRIPT_DIR}/dist"/echo_sdk-*-${MANYLINUX_TAG}.whl)"
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
