cmake_minimum_required(VERSION 3.16)
project(voxsdk_cpp_examples LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Path to the pre-built SDK distribution (can be overridden on the command line)
if(NOT VOXSDK_DIST_DIR)
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(VOXSDK_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/macos/universal")
  elseif(WIN32)
    set(VOXSDK_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/windows/x64")
  else()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
      set(VOXSDK_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/linux/arm64")
    else()
      set(VOXSDK_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/linux/x86_64")
    endif()
  endif()
endif()

if(WIN32)
  set(_sdk_lib "${VOXSDK_DIST_DIR}/voxsdk.dll")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(_sdk_lib "${VOXSDK_DIST_DIR}/voxsdk.dylib")
else()
  set(_sdk_lib "${VOXSDK_DIST_DIR}/voxsdk.so")
endif()

if(NOT EXISTS "${_sdk_lib}")
  message(FATAL_ERROR
    "VoxSDK shared library not found at ${_sdk_lib}.\n"
    "Run the appropriate build script from the repo root first:\n"
    "  Linux:  bash scripts/build-linux.sh\n"
    "  macOS:  bash scripts/build-macos.sh\n"
    "  Windows: scripts\\build-windows.ps1")
endif()

add_library(voxsdk SHARED IMPORTED)
set_target_properties(voxsdk PROPERTIES
  IMPORTED_LOCATION             "${_sdk_lib}"
  INTERFACE_INCLUDE_DIRECTORIES "${VOXSDK_DIST_DIR}/include;${CMAKE_CURRENT_SOURCE_DIR}")

if(WIN32)
  set_target_properties(voxsdk PROPERTIES
    IMPORTED_IMPLIB "${VOXSDK_DIST_DIR}/voxsdk.lib")
endif()

# ── Examples ──────────────────────────────────────────────────────────────────
file(GLOB EXAMPLE_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp")
foreach(src ${EXAMPLE_SRCS})
  get_filename_component(name ${src} NAME_WE)
  add_executable(${name} ${src})
  target_link_libraries(${name} PRIVATE voxsdk)
  if(UNIX AND NOT APPLE)
    target_link_libraries(${name} PRIVATE pulse opus)
  endif()

  # Copy the shared library next to the binary so it runs without LD_LIBRARY_PATH
  add_custom_command(TARGET ${name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${_sdk_lib}"
      "$<TARGET_FILE_DIR:${name}>/$<TARGET_FILE_NAME:voxsdk>")
endforeach()
