#!/usr/bin/env bash
# generate_clean_header.sh
#
# Preprocesses include/baresdk.h into a cffi-compatible form:
#   - Strips all #include directives and system headers
#   - Expands macros
#   - Removes compiler-specific extensions
#
# Run from the repository root:
#   ./bindings/python/generate_clean_header.sh
#
# The output is committed as baresdk/_baresdk_clean.h so users don't
# need to run this unless the C header changes.

set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
INPUT="${REPO}/include/baresdk.h"
OUTPUT="${REPO}/bindings/python/baresdk/_baresdk_clean.h"

echo "Preprocessing ${INPUT} -> ${OUTPUT}"

gcc -E -P \
    -undef \
    -D'__extension__=' \
    -D'__attribute__(x)=' \
    -D'__restrict=' \
    -D'__inline__=inline' \
    -D'inline=' \
    "${INPUT}" \
    | grep -v '^#' \
    | grep -v '__typeof__' \
    | grep -v 'nullptr_t' \
    | grep -v '__max_align' \
    | grep -v 'max_align_t' \
    | sed 's/_Bool/int/g' \
    | sed '/^[[:space:]]*$/N;/^\n$/d' \
    > "${OUTPUT}"

# Prepend minimal typedefs cffi needs that we stripped above
{
  printf 'typedef unsigned long size_t;\n'
  printf 'typedef int wchar_t;\n'
  cat "${OUTPUT}"
} > "${OUTPUT}.tmp" && mv "${OUTPUT}.tmp" "${OUTPUT}"

echo "Done: ${OUTPUT}"
