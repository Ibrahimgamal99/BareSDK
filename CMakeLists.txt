cmake_minimum_required(VERSION 3.19)
project(baresdk VERSION 1.0.0 LANGUAGES C)

include(ExternalProject)

# ---------------------------------------------------------------------------
# Source-only mode — used internally by baresdk_core_project ExternalProject.
# When BARESDK_SRC_MODE=ON this file compiles only src/*.c + platform/*.c
# into baresdk_core and returns; the outer build then merges it with re + baresip.
# ---------------------------------------------------------------------------
option(BARESDK_SRC_MODE "Compile baresdk sources only (internal use)" OFF)
if(BARESDK_SRC_MODE)
  cmake_minimum_required(VERSION 3.19)
  project(baresdk_core VERSION 1.0.0 LANGUAGES C)

  set(BARESDK_SYSROOT "" CACHE PATH "Path to installed re+baresip headers")

  # Platform-specific source files
  if(ANDROID_ABI)
    file(GLOB PLAT_SRCS
      "${CMAKE_CURRENT_SOURCE_DIR}/platform/android/*.c")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    file(GLOB PLAT_SRCS
      "${CMAKE_CURRENT_SOURCE_DIR}/platform/ios/*.c"
      "${CMAKE_CURRENT_SOURCE_DIR}/platform/ios/*.m")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    file(GLOB PLAT_SRCS
      "${CMAKE_CURRENT_SOURCE_DIR}/platform/macos/*.c"
      "${CMAKE_CURRENT_SOURCE_DIR}/platform/macos/*.m")
  elseif(WIN32)
    file(GLOB PLAT_SRCS
      "${CMAKE_CURRENT_SOURCE_DIR}/platform/windows/*.c")
  else()
    file(GLOB PLAT_SRCS
      "${CMAKE_CURRENT_SOURCE_DIR}/platform/linux/*.c")
  endif()

  file(GLOB CORE_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

  add_library(baresdk_core STATIC ${CORE_SRCS} ${PLAT_SRCS})

  target_include_directories(baresdk_core PRIVATE
    "${BARESDK_SYSROOT}/include"
    "${BARESDK_SYSROOT}/include/re"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

  # Propagate the modules profile as a compile definition
  if(BARESDK_MODULES_PROFILE STREQUAL "mobile")
    target_compile_definitions(baresdk_core PRIVATE BARESDK_PROFILE_MOBILE=1)
  else()
    target_compile_definitions(baresdk_core PRIVATE BARESDK_PROFILE_DESKTOP=1)
  endif()

  # Platform audio define so modules_init.c picks the right audio module
  if(ANDROID_ABI)
    target_compile_definitions(baresdk_core PRIVATE BARESDK_AUDIO_AAUDIO=1)
  elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_compile_definitions(baresdk_core PRIVATE BARESDK_AUDIO_AUDIOUNIT=1)
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_compile_definitions(baresdk_core PRIVATE BARESDK_AUDIO_COREAUDIO=1)
  elseif(WIN32)
    target_compile_definitions(baresdk_core PRIVATE BARESDK_AUDIO_WASAPI=1)
  else()
    target_compile_definitions(baresdk_core PRIVATE BARESDK_AUDIO_PULSE=1)
  endif()

  set_target_properties(baresdk_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    PREFIX "")

  install(TARGETS baresdk_core ARCHIVE DESTINATION .)
  return()
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
set(BARESDK_TLS "openssl" CACHE STRING "TLS backend: openssl or mbedtls")
set(BARESDK_MODULES_PROFILE "desktop" CACHE STRING "Module profile: desktop or mobile")
option(BARESDK_SHARED "Also produce a shared library (.so/.dylib/.dll) from the merged archive" OFF)

set(BARESDK_RE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/re"
    CACHE PATH "Path to libre source tree")
set(BARESDK_BARESIP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/baresip"
    CACHE PATH "Path to baresip source tree")
set(BARESDK_MBEDTLS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mbedtls"
    CACHE PATH "Path to mbedTLS source tree (required when BARESDK_TLS=mbedtls)")
set(BARESDK_OPUS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/opus"
    CACHE PATH "Path to libopus source tree (required when opus module is enabled)")
set(BARESDK_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist"
    CACHE PATH "Top-level output directory for built archives")

set(SYSROOT "${CMAKE_BINARY_DIR}/sysroot")

# ---------------------------------------------------------------------------
# Module list
# ---------------------------------------------------------------------------
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules-mobile.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules-desktop.cmake")

if(BARESDK_MODULES_PROFILE STREQUAL "mobile")
  set(BARESDK_MODULES "${BARESDK_MOBILE_MODULES_BASE}")
  # Append platform audio
  if(ANDROID_ABI)
    set(BARESDK_MODULES "${BARESDK_MODULES};opensles;aaudio")
  elseif(CMAKE_SYSTEM_NAME MATCHES "iOS|Darwin")
    set(BARESDK_MODULES "${BARESDK_MODULES};audiounit;avcapture;coreaudio")
  endif()
else()
  set(BARESDK_MODULES "${BARESDK_DESKTOP_MODULES}")
endif()

# ---------------------------------------------------------------------------
# Platform ID for dist/ subdirectory naming
# ---------------------------------------------------------------------------
if(ANDROID_ABI)
  set(BARESDK_PLATFORM_ID "android/${ANDROID_ABI}")
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  if(CMAKE_OSX_SYSROOT MATCHES "simulator")
    set(BARESDK_PLATFORM_ID "ios/simulator")
  else()
    set(BARESDK_PLATFORM_ID "ios/device")
  endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  string(REPLACE ";" "_" _ARCH_SLUG "${CMAKE_OSX_ARCHITECTURES}")
  set(BARESDK_PLATFORM_ID "macos/${_ARCH_SLUG}")
elseif(WIN32)
  set(BARESDK_PLATFORM_ID "windows/x64")
else()
  set(BARESDK_PLATFORM_ID "linux/x86_64")
endif()

# ---------------------------------------------------------------------------
# Cross-compilation passthrough args
# ---------------------------------------------------------------------------
set(CROSS_ARGS "")
if(CMAKE_TOOLCHAIN_FILE)
  list(APPEND CROSS_ARGS "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
endif()
foreach(_var
    CMAKE_SYSTEM_NAME
    CMAKE_OSX_ARCHITECTURES CMAKE_OSX_SYSROOT CMAKE_OSX_DEPLOYMENT_TARGET
    ANDROID_ABI ANDROID_PLATFORM ANDROID_STL
    CMAKE_C_COMPILER CMAKE_CXX_COMPILER
    CMAKE_AR CMAKE_RANLIB CMAKE_NM CMAKE_STRIP
    CMAKE_FIND_ROOT_PATH CMAKE_SYSROOT)
  if(DEFINED ${_var} AND NOT "${${_var}}" STREQUAL "")
    # Replace ; with | so multi-value vars (e.g. CMAKE_OSX_ARCHITECTURES)
    # survive cmake list serialization; ExternalProjects use LIST_SEPARATOR "|".
    string(REPLACE ";" "|" _cross_val "${${_var}}")
    list(APPEND CROSS_ARGS "-D${_var}=${_cross_val}")
  endif()
endforeach()

# ---------------------------------------------------------------------------
# Library file naming
# ---------------------------------------------------------------------------
if(MSVC)
  set(_LPFX "")
  set(_EXT  ".lib")
  set(_RE_LIB   "re-static.lib")
  set(_BARE_LIB "libbaresip.lib")
  set(_CORE_LIB "baresdk_core.lib")
else()
  set(_LPFX "lib")
  set(_EXT  ".a")
  set(_RE_LIB   "libre.a")
  set(_BARE_LIB "libbaresip.a")
  set(_CORE_LIB "baresdk_core.a")
endif()

set(_RE_A   "${SYSROOT}/lib/${_RE_LIB}")
set(_BARE_A "${SYSROOT}/lib/${_BARE_LIB}")
set(_CORE_A "${CMAKE_BINARY_DIR}/baresdk-core-build/${_CORE_LIB}")

# ---------------------------------------------------------------------------
# Phase 1 (mobile only): build mbedTLS
# ---------------------------------------------------------------------------
set(MBEDTLS_ARGS "")
set(MBEDTLS_EXTRA_LIBS "")

if(BARESDK_TLS STREQUAL "mbedtls")
  if(NOT EXISTS "${BARESDK_MBEDTLS_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
      "BARESDK_TLS=mbedtls but mbedTLS sources not found at "
      "${BARESDK_MBEDTLS_SOURCE_DIR}. "
      "Run: git submodule update --init third_party/mbedtls")
  endif()

  set(_MBEDTLS_A   "${SYSROOT}/lib/${_LPFX}mbedtls${_EXT}")
  set(_MBEDX509_A  "${SYSROOT}/lib/${_LPFX}mbedx509${_EXT}")
  set(_MBEDCRYPTO_A "${SYSROOT}/lib/${_LPFX}mbedcrypto${_EXT}")

  ExternalProject_Add(mbedtls_project
    SOURCE_DIR "${BARESDK_MBEDTLS_SOURCE_DIR}"
    BINARY_DIR "${CMAKE_BINARY_DIR}/mbedtls-build"
    LIST_SEPARATOR "|"
    CMAKE_ARGS
      -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
      -DCMAKE_INSTALL_PREFIX=${SYSROOT}
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON
      -DENABLE_TESTING=OFF
      -DENABLE_PROGRAMS=OFF
      -DUSE_SHARED_MBEDTLS_LIBRARY=OFF
      -DUSE_STATIC_MBEDTLS_LIBRARY=ON
      ${CROSS_ARGS}
    BUILD_BYPRODUCTS "${_MBEDTLS_A}" "${_MBEDX509_A}" "${_MBEDCRYPTO_A}"
  )

  set(MBEDTLS_ARGS
    "-DUSE_OPENSSL=OFF"
    "-DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=ON"
    "-DUSE_MBEDTLS=ON"
    "-DMBEDTLS_HINTS=${SYSROOT}"
    "-DMBEDTLS_INCLUDE_DIR=${SYSROOT}/include"
    "-DMBEDTLS_LIBRARY=${_MBEDCRYPTO_A}|${_MBEDX509_A}|${_MBEDTLS_A}"
  )
  set(MBEDTLS_EXTRA_LIBS "${_MBEDTLS_A}" "${_MBEDX509_A}" "${_MBEDCRYPTO_A}")
  set(MBEDTLS_DEPENDS mbedtls_project)
  # Suffix appended to RE_LIBRARIES so baresip consumers (webrtc, selftest)
  # get all three mbedTLS archives without relying on FindMBEDTLS running in
  # baresip's transitive cmake context (where its module path is different).
  set(BARESDK_MBEDTLS_LINK "|${_MBEDCRYPTO_A}|${_MBEDX509_A}|${_MBEDTLS_A}")
else()
  set(MBEDTLS_DEPENDS "")
  set(MBEDTLS_ARGS
    "-DUSE_OPENSSL=ON"
    "-DUSE_MBEDTLS=OFF"
  )
  set(BARESDK_MBEDTLS_LINK "")
endif()

# ---------------------------------------------------------------------------
# Phase 1.5: build libopus (if opus module is in the profile)
# ---------------------------------------------------------------------------
set(OPUS_ARGS "")
set(OPUS_EXTRA_LIBS "")
set(OPUS_DEPENDS "")

if("opus" IN_LIST BARESDK_MODULES)
  if(NOT EXISTS "${BARESDK_OPUS_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
      "opus module enabled but sources not found at ${BARESDK_OPUS_SOURCE_DIR}. "
      "Run: git submodule update --init third_party/opus")
  endif()

  set(_OPUS_A "${SYSROOT}/lib/${_LPFX}opus${_EXT}")

  ExternalProject_Add(opus_project
    SOURCE_DIR "${BARESDK_OPUS_SOURCE_DIR}"
    BINARY_DIR "${CMAKE_BINARY_DIR}/opus-build"
    LIST_SEPARATOR "|"
    CMAKE_ARGS
      -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
      -DCMAKE_INSTALL_PREFIX=${SYSROOT}
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON
      -DOPUS_BUILD_SHARED_LIBRARY=OFF
      -DOPUS_BUILD_TESTING=OFF
      -DOPUS_INSTALL_PKG_CONFIG_MODULE=OFF
      ${CROSS_ARGS}
    BUILD_BYPRODUCTS "${_OPUS_A}"
  )

  set(OPUS_ARGS
    "-DOPUS_INCLUDE_DIR=${SYSROOT}/include"
    "-DOPUS_LIBRARY=${_OPUS_A}"
  )
  set(OPUS_EXTRA_LIBS "${_OPUS_A}")
  set(OPUS_DEPENDS opus_project)
endif()

# ---------------------------------------------------------------------------
# Phase 2: build libre (includes librem via USE_REM=ON)
# ---------------------------------------------------------------------------
set(RE_PLATFORM_ARGS "")
if(ANDROID_ABI)
  list(APPEND RE_PLATFORM_ARGS "-DUSE_UNIXSOCK=OFF")
endif()

ExternalProject_Add(re_project
  DEPENDS ${MBEDTLS_DEPENDS}
  SOURCE_DIR "${BARESDK_RE_SOURCE_DIR}"
  BINARY_DIR "${CMAKE_BINARY_DIR}/re-build"
  LIST_SEPARATOR "|"
  CMAKE_ARGS
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DCMAKE_INSTALL_PREFIX=${SYSROOT}
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DLIBRE_BUILD_SHARED=OFF
    -DLIBRE_BUILD_STATIC=ON
    -DUSE_REM=ON
    ${RE_PLATFORM_ARGS}
    ${MBEDTLS_ARGS}
    ${CROSS_ARGS}
  BUILD_BYPRODUCTS "${_RE_A}"
)

# ---------------------------------------------------------------------------
# Phase 3: build libbaresip (STATIC=ON links all modules in)
# ---------------------------------------------------------------------------
string(REPLACE ";" "|" BARESDK_MODULES_SEP "${BARESDK_MODULES}")

ExternalProject_Add(baresip_project
  DEPENDS re_project ${OPUS_DEPENDS}
  SOURCE_DIR "${BARESDK_BARESIP_SOURCE_DIR}"
  BINARY_DIR "${CMAKE_BINARY_DIR}/baresip-build"
  LIST_SEPARATOR "|"
  CMAKE_ARGS
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DCMAKE_INSTALL_PREFIX=${SYSROOT}
    -DCMAKE_PREFIX_PATH=${SYSROOT}
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DRE_INCLUDE_DIR=${SYSROOT}/include/re
    -DRE_INCLUDE_DIRS=${SYSROOT}/include/re
    "-DRE_LIBRARY=${_RE_A}${BARESDK_MBEDTLS_LINK}"
    "-DRE_LIBRARIES=${_RE_A}${BARESDK_MBEDTLS_LINK}"
    -Dre_DIR=${SYSROOT}/lib/cmake/re
    -DSTATIC=ON
    "-DMODULES=${BARESDK_MODULES_SEP}"
    -DBUILD_TESTING=OFF
    ${MBEDTLS_ARGS}
    ${OPUS_ARGS}
    ${CROSS_ARGS}
  BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target baresip
  BUILD_BYPRODUCTS "${_BARE_A}"
)

# ---------------------------------------------------------------------------
# Phase 3.5: compile baresdk source layer against installed headers
# ---------------------------------------------------------------------------
ExternalProject_Add(baresdk_core_project
  DEPENDS baresip_project
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
  BINARY_DIR "${CMAKE_BINARY_DIR}/baresdk-core-build"
  CMAKE_ARGS
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DBARESDK_SRC_MODE=ON
    -DBARESDK_SYSROOT=${SYSROOT}
    -DBARESDK_MODULES_PROFILE=${BARESDK_MODULES_PROFILE}
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    ${CROSS_ARGS}
  BUILD_BYPRODUCTS "${_CORE_A}"
  INSTALL_COMMAND ""
)

# ---------------------------------------------------------------------------
# Phase 4: merge archives into single baresdk.a / bare.lib
# ---------------------------------------------------------------------------
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/MergeStaticLibs.cmake")

if(MSVC)
  set(_OUT "${CMAKE_BINARY_DIR}/bare.lib")
else()
  set(_OUT "${CMAKE_BINARY_DIR}/baresdk.a")
endif()

baresdk_merge(
  OUTPUT   "${_OUT}"
  INPUTS   "${_CORE_A}" "${_RE_A}" "${_BARE_A}" ${MBEDTLS_EXTRA_LIBS} ${OPUS_EXTRA_LIBS}
  DEPENDS  baresdk_core_project
)

# ---------------------------------------------------------------------------
# Phase 4.5 (optional): link a shared library from the merged archive
# ---------------------------------------------------------------------------
if(BARESDK_SHARED)
  if(APPLE)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
      set(_SHARED_EXT ".dylib")
    else()
      set(_SHARED_EXT ".dylib")
    endif()
    set(_SHARED_OUT "${CMAKE_BINARY_DIR}/baresdk${_SHARED_EXT}")
    add_custom_command(
      OUTPUT  "${_SHARED_OUT}"
      COMMAND "${CMAKE_C_COMPILER}" -dynamiclib
              -Wl,-all_load "${_OUT}" -Wl,-noall_load
              -framework CoreFoundation -framework Security
              -o "${_SHARED_OUT}"
      DEPENDS baresdk "${_OUT}"
      COMMENT "Linking baresdk.dylib"
      VERBATIM
    )
  elseif(MSVC)
    set(_SHARED_OUT "${CMAKE_BINARY_DIR}/baresdk.dll")
    add_custom_command(
      OUTPUT  "${_SHARED_OUT}"
      COMMAND link.exe /DLL /NOLOGO "/OUT:${_SHARED_OUT}"
              "/WHOLEARCHIVE:${_OUT}"
              ws2_32.lib iphlpapi.lib crypt32.lib
      DEPENDS baresdk "${_OUT}"
      COMMENT "Linking baresdk.dll"
      VERBATIM
    )
  else()
    set(_SHARED_OUT "${CMAKE_BINARY_DIR}/baresdk.so")
    if(ANDROID_ABI)
      set(_SHARED_PLATFORM_LIBS -llog -lOpenSLES -landroid)
    else()
      set(_SHARED_PLATFORM_LIBS -lpthread -lssl -lcrypto -lz -lm -ldl -lresolv)
    endif()
    add_custom_command(
      OUTPUT  "${_SHARED_OUT}"
      COMMAND "${CMAKE_C_COMPILER}" -shared
              -Wl,--whole-archive "${_OUT}" -Wl,--no-whole-archive
              ${_SHARED_PLATFORM_LIBS}
              -o "${_SHARED_OUT}"
      DEPENDS baresdk "${_OUT}"
      COMMENT "Linking baresdk.so"
      VERBATIM
    )
  endif()

  add_custom_target(baresdk_shared ALL DEPENDS "${_SHARED_OUT}")
endif()

# ---------------------------------------------------------------------------
# Install merged archive + headers to dist/
# ---------------------------------------------------------------------------
set(_DIST "${BARESDK_DIST_DIR}/${BARESDK_PLATFORM_ID}")

install(FILES "${_OUT}" DESTINATION "${_DIST}")
if(BARESDK_SHARED AND DEFINED _SHARED_OUT)
  install(FILES "${_SHARED_OUT}" DESTINATION "${_DIST}")
endif()
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/include/baresdk.h"
              DESTINATION "${_DIST}/include")
install(DIRECTORY "${SYSROOT}/include/re/"  DESTINATION "${_DIST}/include/re")
install(FILES     "${SYSROOT}/include/baresip.h" DESTINATION "${_DIST}/include")
