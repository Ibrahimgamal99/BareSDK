#!/usr/bin/env bash
# Install the baresdk Python package.
# Builds the SDK first if it hasn't been built yet.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
DIST_DIR="${ROOT}/dist/linux/x86_64"

if [[ ! -f "${DIST_DIR}/baresdk.so" ]]; then
  echo "==> SDK not found — building it first..."
  bash "${ROOT}/scripts/build-linux.sh"
fi

echo "==> Installing Python package..."
pip install -e "${SCRIPT_DIR}"

echo ""
echo "Done. Run an example:"
echo "  python ${SCRIPT_DIR}/examples/quickstart.py alice@pbx.example.com secret"
echo "  python ${SCRIPT_DIR}/examples/quickstart.py alice@pbx.example.com secret bob@pbx.example.com"
