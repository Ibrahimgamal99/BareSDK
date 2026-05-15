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

# Skip building baresip's executables (selftest, baresip_exe, webrtc demo).
# Our build renames websock_connect() in libre's websock.c (see
# fix-msvc-re.cmake) so the only consumer that provides the symbol is our
# SDK's ws_path.c.  These executables link libre directly without ws_path
# and would fail with LNK2019.  We only need libbaresip.lib for the SDK.
set(BARESIP_CMAKE "${SOURCE_DIR}/CMakeLists.txt")
if(EXISTS "${BARESIP_CMAKE}")
  file(READ "${BARESIP_CMAKE}" CONTENT)
  string(FIND "${CONTENT}" "# baresdk-msvc-disabled" _ALREADY)
  if(_ALREADY EQUAL -1)
    string(REPLACE
      "add_subdirectory(webrtc)"
      "# add_subdirectory(webrtc)  # baresdk-msvc-disabled"
      CONTENT "${CONTENT}")
    string(REPLACE
      "add_subdirectory(test)"
      "# add_subdirectory(test)  # baresdk-msvc-disabled"
      CONTENT "${CONTENT}")
    string(REPLACE
      "add_executable(baresip_exe src/main.c)"
      "add_executable(baresip_exe EXCLUDE_FROM_ALL src/main.c)  # baresdk-msvc-disabled"
      CONTENT "${CONTENT}")
    string(REPLACE
      "install(TARGETS baresip_exe baresip"
      "install(TARGETS baresip  # baresdk-msvc-disabled baresip_exe"
      CONTENT "${CONTENT}")
    file(WRITE "${BARESIP_CMAKE}" "${CONTENT}")
    message(STATUS "Patched ${BARESIP_CMAKE} (executables disabled for MSVC)")
  else()
    message(STATUS "${BARESIP_CMAKE} already patched (executables disabled)")
  endif()
endif()

message(STATUS "MSVC baresip source patching complete")
