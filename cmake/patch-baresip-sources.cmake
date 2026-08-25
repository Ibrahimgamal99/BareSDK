# patch-baresip-sources.cmake
# Disable baresip's executables (baresip_exe, test/, webrtc/) on every platform.
# Usage: cmake -DSOURCE_DIR=path/to/baresip -P patch-baresip-sources.cmake
#
# The SDK owns the public websock_connect() and sip_dialog_route() names:
# libre's definitions are renamed to __real_* (cmake/patch-re-sources.cmake)
# and the only provider of the public symbols is baresdk's src/ws_path.c.
# baresip's executables link libre directly without ws_path.c, so they stop
# linking — and the SDK only ever consumes libbaresip.a anyway.  This is the
# executable-disabling half of what fix-msvc-baresip.cmake did for MSVC,
# promoted to every platform now that the rename is unconditional.
#
# Idempotent behind the "# baresdk-disabled" marker (the legacy
# "# baresdk-msvc-disabled" marker from fix-msvc-baresip.cmake also counts, so
# a Windows tree patched by the older script is left alone).

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR must be defined")
endif()

set(BARESIP_CMAKE "${SOURCE_DIR}/CMakeLists.txt")
if(NOT EXISTS "${BARESIP_CMAKE}")
  message(FATAL_ERROR "patch-baresip-sources: ${BARESIP_CMAKE} does not exist")
endif()

file(READ "${BARESIP_CMAKE}" CONTENT)

string(FIND "${CONTENT}" "# baresdk-disabled" _ALREADY)
string(FIND "${CONTENT}" "# baresdk-msvc-disabled" _ALREADY_MSVC)
if(NOT _ALREADY EQUAL -1 OR NOT _ALREADY_MSVC EQUAL -1)
  message(STATUS "patch-baresip-sources: ${BARESIP_CMAKE} already patched (executables disabled)")
  return()
endif()

foreach(_anchor
    "add_subdirectory(webrtc)"
    "add_subdirectory(test)"
    "add_executable(baresip_exe src/main.c)"
    "set_target_properties(baresip_exe PROPERTIES OUTPUT_NAME baresip)"
    "install(TARGETS baresip_exe baresip")
  string(FIND "${CONTENT}" "${_anchor}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR
      "patch-baresip-sources: anchor '${_anchor}' not found in ${BARESIP_CMAKE}. "
      "The pinned baresip revision has moved; re-derive this patch — building "
      "without it fails at link (baresip's executables lack ws_path.c).")
  endif()
endforeach()

string(REPLACE
  "add_subdirectory(webrtc)"
  "# add_subdirectory(webrtc)  # baresdk-disabled"
  CONTENT "${CONTENT}")
string(REPLACE
  "add_subdirectory(test)"
  "# add_subdirectory(test)  # baresdk-disabled"
  CONTENT "${CONTENT}")
string(REPLACE
  "add_executable(baresip_exe src/main.c)"
  "add_executable(baresip_exe EXCLUDE_FROM_ALL src/main.c)  # baresdk-disabled"
  CONTENT "${CONTENT}")
# The exe's OUTPUT_NAME collides with the library target's name: Ninja resolves
# `--build --target baresip` to the FILE named baresip (the excluded exe) and
# builds it anyway. Rename the never-shipped exe so the target name means the
# library on every generator.
string(REPLACE
  "set_target_properties(baresip_exe PROPERTIES OUTPUT_NAME baresip)"
  "set_target_properties(baresip_exe PROPERTIES OUTPUT_NAME baresip_exe)  # baresdk-disabled"
  CONTENT "${CONTENT}")
string(REPLACE
  "install(TARGETS baresip_exe baresip"
  "install(TARGETS baresip  # baresdk-disabled baresip_exe"
  CONTENT "${CONTENT}")

file(WRITE "${BARESIP_CMAKE}" "${CONTENT}")
message(STATUS "patch-baresip-sources: executables disabled in ${BARESIP_CMAKE}")
