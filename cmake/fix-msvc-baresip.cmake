# fix-msvc-baresip.cmake
# Patch baresip sources for MSVC compatibility
# Usage: cmake -DSOURCE_DIR=path/to/baresip -P fix-msvc-baresip.cmake

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR must be defined")
endif()

# Fix net.c: the header patch converts 'typedef bool (net_ifaddr_h)(...)' to
# 'typedef bool (*net_ifaddr_h)(...)' making net_ifaddr_h a function-pointer type.
# Therefore 'net_ifaddr_h *ifh' becomes a double pointer (bool (**)()), causing
# the compiler to double-dereference when calling (*ifh)(...) — crash.
# Fix: remove the extra '*' so 'net_ifaddr_h ifh' is a plain function pointer.
set(NET_C "${SOURCE_DIR}/src/net.c")
if(EXISTS "${NET_C}")
  file(READ "${NET_C}" CONTENT)
  string(REPLACE "net_ifaddr_h *ifh" "net_ifaddr_h ifh" CONTENT "${CONTENT}")
  file(WRITE "${NET_C}" "${CONTENT}")
  message(STATUS "Patched ${NET_C} for MSVC (net_ifaddr_h double-pointer fix)")
endif()

# Fix include/baresip.h: same declaration in the public header.
set(BARESIP_H "${SOURCE_DIR}/include/baresip.h")
if(EXISTS "${BARESIP_H}")
  file(READ "${BARESIP_H}" CONTENT)
  string(REPLACE "net_ifaddr_h *ifh" "net_ifaddr_h ifh" CONTENT "${CONTENT}")
  file(WRITE "${BARESIP_H}" "${CONTENT}")
  message(STATUS "Patched ${BARESIP_H} for MSVC (net_ifaddr_h double-pointer fix)")
endif()

# NOTE: disabling baresip's executables (baresip_exe, test/, webrtc/) used to
# live here, MSVC-only. It moved to cmake/patch-baresip-sources.cmake and runs
# on every platform now that the websock_connect/sip_dialog_route renames are
# unconditional (see cmake/patch-re-sources.cmake).

message(STATUS "MSVC baresip source patching complete")
