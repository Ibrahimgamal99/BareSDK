# fix-msvc-headers.cmake
# Patch libre headers for MSVC compatibility
# Usage: cmake -DSYSROOT=path/to/sysroot -P fix-msvc-headers.cmake

if(NOT DEFINED SYSROOT)
  message(FATAL_ERROR "SYSROOT must be defined")
endif()

set(HEADERS_DIR "${SYSROOT}/include/re")

# Convert function-type typedefs to function-pointer typedefs.
# MSVC /std:c11 rejects 'typedef int (NAME)(args)' (C89/K&R form).
# Transform to 'typedef int (*NAME)(args)' which is valid C11.
file(GLOB _HEADERS "${HEADERS_DIR}/*.h")
foreach(_H ${_HEADERS})
  file(READ "${_H}" CONTENT)
  string(REGEX REPLACE
    "typedef ([^\n(]+) [(]([A-Za-z_][A-Za-z0-9_]*)[)]"
    "typedef \\1 (*\\2)"
    CONTENT "${CONTENT}")
  file(WRITE "${_H}" "${CONTENT}")
endforeach()
message(STATUS "Patched ${HEADERS_DIR} typedef forms for MSVC /std:c11")

# Fix re_dbg.h: Replace #warning with #pragma message for MSVC
set(DBG_H "${HEADERS_DIR}/re_dbg.h")
if(EXISTS "${DBG_H}")
  file(READ "${DBG_H}" CONTENT)
  string(REGEX REPLACE
    "# warning \"([^\"]+)\""
    "#ifdef _MSC_VER\n#pragma message(\"\\1\")\n#else\n# warning \"\\1\"\n#endif"
    CONTENT "${CONTENT}")
  file(WRITE "${DBG_H}" "${CONTENT}")
  message(STATUS "Patched ${DBG_H} for MSVC #warning")
endif()

message(STATUS "MSVC header patching complete")
