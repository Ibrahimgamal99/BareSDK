#!/usr/bin/env python3
"""
Re-embed the C declarations from include/baresdk.h into bindings/cpp/baresdk.hpp.

Run whenever baresdk.h changes:
    python3 tools/gen_hpp.py

Or add to CMake (see bottom of this file) so it runs automatically at build time.
"""

import sys
from pathlib import Path

ROOT       = Path(__file__).resolve().parent.parent
C_HEADER   = ROOT / "include" / "baresdk.h"
CPP_HEADER = ROOT / "bindings" / "cpp" / "baresdk.hpp"

# Markers that delimit the embedded C block inside baresdk.hpp
START_MARKER = "#ifndef BARESDK_H\n"
END_MARKER   = "#endif /* BARESDK_H — embedded C declarations end */"

# ── Extract the body of baresdk.h, stripping its own header guard ─────────

c_text  = C_HEADER.read_text()
c_lines = c_text.splitlines(keepends=True)

body = []
skipped = 0
for line in c_lines:
    s = line.strip()
    # Skip the two guard lines at the top
    if skipped < 2 and s in ("#ifndef BARESDK_H", "#define BARESDK_H"):
        skipped += 1
        continue
    body.append(line)

# Drop the closing #endif /* BARESDK_H */ (last non-blank line that starts with #endif)
for i in range(len(body) - 1, -1, -1):
    if body[i].strip().startswith("#endif"):
        body.pop(i)
        break

c_body = "".join(body).rstrip("\n") + "\n"

# ── Splice into baresdk.hpp ────────────────────────────────────────────────

hpp = CPP_HEADER.read_text()

start = hpp.find(START_MARKER)
end   = hpp.find(END_MARKER)

if start == -1 or end == -1:
    sys.exit(
        f"ERROR: markers not found in {CPP_HEADER}\n"
        f"  expected: {START_MARKER!r}  ...  {END_MARKER!r}"
    )

new_block = START_MARKER + c_body + END_MARKER
hpp_new   = hpp[:start] + new_block + hpp[end + len(END_MARKER):]

if hpp_new == hpp:
    print("baresdk.hpp is already up to date.")
else:
    CPP_HEADER.write_text(hpp_new)
    print(f"Updated {CPP_HEADER}  ({len(c_lines)} C lines embedded)")

# ── CMake integration (copy into CMakeLists.txt) ───────────────────────────
#
# find_package(Python3 REQUIRED COMPONENTS Interpreter)
#
# add_custom_command(
#     OUTPUT  ${CMAKE_SOURCE_DIR}/bindings/cpp/baresdk.hpp
#     COMMAND ${Python3_EXECUTABLE}
#             ${CMAKE_SOURCE_DIR}/tools/gen_hpp.py
#     DEPENDS ${CMAKE_SOURCE_DIR}/include/baresdk.h
#     COMMENT "Regenerating baresdk.hpp from baresdk.h"
#     VERBATIM
# )
# add_custom_target(gen_hpp ALL
#     DEPENDS ${CMAKE_SOURCE_DIR}/bindings/cpp/baresdk.hpp)
