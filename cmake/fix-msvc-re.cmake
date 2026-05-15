# fix-msvc-re.cmake
# Patch libre sources for MSVC builds.
# Usage: cmake -DSOURCE_DIR=path/to/re -P fix-msvc-re.cmake
#
# Rationale: MSVC's linker has no equivalent of GNU ld's --wrap, which we use
# on Linux/macOS to intercept websock_connect() (see src/ws_path.c).  Instead
# we rename the function *definition* in websock.c from `websock_connect` to
# `__real_websock_connect`.  Other libre TUs (transp.c) still call
# `websock_connect`, which the linker then resolves to our wrapper in
# ws_path.c.  The wrapper calls `__real_websock_connect` to reach the
# original implementation — the same call shape as the Linux build.

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR must be defined")
endif()

set(WEBSOCK_C "${SOURCE_DIR}/src/websock/websock.c")
if(EXISTS "${WEBSOCK_C}")
  file(READ "${WEBSOCK_C}" CONTENT)
  string(FIND "${CONTENT}" "int __real_websock_connect(" ALREADY_PATCHED)
  if(ALREADY_PATCHED EQUAL -1)
    string(REPLACE
      "int websock_connect(struct websock_conn **connp, struct websock *sock,"
      "int __real_websock_connect(struct websock_conn **connp, struct websock *sock,"
      CONTENT "${CONTENT}")
    file(WRITE "${WEBSOCK_C}" "${CONTENT}")
    message(STATUS "Patched ${WEBSOCK_C} for MSVC (websock_connect rename)")
  else()
    message(STATUS "${WEBSOCK_C} already patched")
  endif()
endif()

message(STATUS "MSVC libre source patching complete")
