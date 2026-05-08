#!/usr/bin/env bash
# Build the baresdk Node.js native addon.
# Builds the SDK first if it hasn't been built yet.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
DIST_DIR="${ROOT}/dist/linux/x86_64"

if [[ ! -f "${DIST_DIR}/baresdk.so" ]]; then
  echo "==> SDK not found — building it first..."
  bash "${ROOT}/scripts/build-linux.sh"
fi

echo "==> Installing Node.js dependencies and building native addon..."
cd "${SCRIPT_DIR}"
npm install

echo ""
echo "Done. Run an example:"
echo "  node ${SCRIPT_DIR}/examples/quickstart.js alice@pbx.example.com secret"
echo "  node ${SCRIPT_DIR}/examples/quickstart.js alice@pbx.example.com secret bob@pbx.example.com"
