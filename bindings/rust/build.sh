#!/usr/bin/env bash
# Build the baresdk Rust crate and examples.
# Builds the SDK first if it hasn't been built yet.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
DIST_DIR="${ROOT}/dist/linux/x86_64"

if [[ ! -f "${DIST_DIR}/baresdk.a" ]]; then
  echo "==> SDK not found — building it first..."
  bash "${ROOT}/scripts/build-linux.sh"
fi

echo "==> Building Rust crate and examples..."
cd "${SCRIPT_DIR}"
cargo build --release --examples

echo ""
echo "Done. Run an example:"
echo "  cargo run --release --example quickstart -- alice@pbx.example.com secret"
echo "  cargo run --release --example quickstart -- alice@pbx.example.com secret bob@pbx.example.com"
