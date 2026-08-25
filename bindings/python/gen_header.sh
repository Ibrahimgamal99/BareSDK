#!/usr/bin/env bash
# Regenerate bindings/python/baresdk/_baresdk_clean.h from include/baresdk.h.
#
# This lives in its own script because two callers need it and they must not
# drift: bindings/python/build.sh (local development) and the "Generate CFFI
# header" step of .github/workflows/publish.yml (the wheels users install).
# They were separate copies once, and the CI copy was missing
# BARESDK_NO_PACKED_ENUM — see below for what that costs.
#
# Usage: gen_header.sh [<input baresdk.h> [<output _baresdk_clean.h>]]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

INPUT="${1:-${ROOT}/include/baresdk.h}"
OUTPUT="${2:-${SCRIPT_DIR}/baresdk/_baresdk_clean.h}"

# BARESDK_NO_PACKED_ENUM is required, not cosmetic.  baresdk_aec_mode_t is a
# packed (1-byte) enum in the real ABI, and -D'__attribute__(x)=' below strips
# the packed attribute — so cffi widens the field to 4 bytes and every struct
# member after cfg.aec_mode lands at the wrong offset.  The total size can
# still match by padding coincidence, which is why the struct_size check in
# baresdk_init() does not catch it.  The define selects the uint8_t typedef
# that preserves the layout, the same way bindings/flutter/ffigen.yaml does.
gcc -E \
    -DBARESDK_NO_PACKED_ENUM=1 \
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

# No size_t/wchar_t prologue: cffi resolves both itself in cdef(), the awk
# filter above already drops the system-header typedefs, and the committed
# header has never carried one.  publish.yml used to prepend them — a second,
# quieter way the two pipelines had diverged.

echo "==> Wrote ${OUTPUT}"
