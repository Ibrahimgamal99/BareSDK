cmake_minimum_required(VERSION 3.19)
project(libbare VERSION 1.0.0 LANGUAGES C)

include(ExternalProject)

# ---------------------------------------------------------------------------
# Source-only mode — used internally by libbare_core_project ExternalProject.
# When LIBBARE_SRC_MODE=ON this file compiles only src/*.c + platform/*.c
# into libbare_core and returns; the outer build then merges it with re + baresip.
# ---------------------------------------------------------------------------
option(LIBBARE_SRC_MODE "Compile libbare sources only (internal use)" OFF)
if(LIBBARE_SRC_MODE)
  cmake_minimum_required(VERSION 3.19)
  project(libbare_core VERSION 1.0.0 LANGUAGES C)

  set(LIBBARE_SYSROOT "" CACHE PATH "Path to installed re+baresip headers")

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

  add_library(libbare_core STATIC ${CORE_SRCS} ${PLAT_SRCS})

  target_include_directories(libbare_core PRIVATE
    "${LIBBARE_SYSROOT}/include"
    "${LIBBARE_SYSROOT}/include/re"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

  # Propagate the modules profile as a compile definition
  if(LIBBARE_MODULES_PROFILE STREQUAL "mobile")
    target_compile_definitions(libbare_core PRIVATE LIBBARE_PROFILE_MOBILE=1)
  else()
    target_compile_definitions(libbare_core PRIVATE LIBBARE_PROFILE_DESKTOP=1)
  endif()

  # Platform audio define so modules_init.c picks the right audio module
  if(ANDROID_ABI)
    target_compile_definitions(libbare_core PRIVATE LIBBARE_AUDIO_AAUDIO=1)
  elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_compile_definitions(libbare_core PRIVATE LIBBARE_AUDIO_AUDIOUNIT=1)
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_compile_definitions(libbare_core PRIVATE LIBBARE_AUDIO_COREAUDIO=1)
  elseif(WIN32)
    target_compile_definitions(libbare_core PRIVATE LIBBARE_AUDIO_WASAPI=1)
  else()
    target_compile_definitions(libbare_core PRIVATE LIBBARE_AUDIO_PULSE=1)
  endif()

  set_target_properties(libbare_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_STANDARD 11
    C_STANDARD_REQUIRED ON)

  install(TARGETS libbare_core ARCHIVE DESTINATION .)
  return()
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
set(LIBBARE_TLS "openssl" CACHE STRING "TLS backend: openssl or mbedtls")
set(LIBBARE_MODULES_PROFILE "desktop" CACHE STRING "Module profile: desktop or mobile")

set(LIBBARE_RE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/re"
    CACHE PATH "Path to libre source tree")
set(LIBBARE_BARESIP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/baresip"
    CACHE PATH "Path to baresip source tree")
set(LIBBARE_MBEDTLS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mbedtls"
    CACHE PATH "Path to mbedTLS source tree (required when LIBBARE_TLS=mbedtls)")
set(LIBBARE_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist"
    CACHE PATH "Top-level output directory for built archives")

set(SYSROOT "${CMAKE_BINARY_DIR}/sysroot")

# ---------------------------------------------------------------------------
# Module list
# ---------------------------------------------------------------------------
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules-mobile.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules-desktop.cmake")

if(LIBBARE_MODULES_PROFILE STREQUAL "mobile")
  set(LIBBARE_MODULES "${LIBBARE_MOBILE_MODULES_BASE}")
  # Append platform audio
  if(ANDROID_ABI)
    set(LIBBARE_MODULES "${LIBBARE_MODULES};opensles;aaudio")
  elseif(CMAKE_SYSTEM_NAME MATCHES "iOS|Darwin")
    set(LIBBARE_MODULES "${LIBBARE_MODULES};audiounit;avcapture;coreaudio")
  endif()
else()
  set(LIBBARE_MODULES "${LIBBARE_DESKTOP_MODULES}")
endif()

# ---------------------------------------------------------------------------
# Platform ID for dist/ subdirectory naming
# ---------------------------------------------------------------------------
if(ANDROID_ABI)
  set(LIBBARE_PLATFORM_ID "android/${ANDROID_ABI}")
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  if(CMAKE_OSX_SYSROOT MATCHES "simulator")
    set(LIBBARE_PLATFORM_ID "ios/simulator")
  else()
    set(LIBBARE_PLATFORM_ID "ios/device")
  endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  string(REPLACE ";" "_" _ARCH_SLUG "${CMAKE_OSX_ARCHITECTURES}")
  set(LIBBARE_PLATFORM_ID "macos/${_ARCH_SLUG}")
elseif(WIN32)
  set(LIBBARE_PLATFORM_ID "windows/x64")
else()
  set(LIBBARE_PLATFORM_ID "linux/x86_64")
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
    list(APPEND CROSS_ARGS "-D${_var}=${${_var}}")
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
  set(_CORE_LIB "libbare_core.lib")
else()
  set(_LPFX "lib")
  set(_EXT  ".a")
  set(_RE_LIB   "libre.a")
  set(_BARE_LIB "libbaresip.a")
  set(_CORE_LIB "liblibbare_core.a")
endif()

set(_RE_A   "${SYSROOT}/lib/${_RE_LIB}")
set(_BARE_A "${SYSROOT}/lib/${_BARE_LIB}")
set(_CORE_A "${CMAKE_BINARY_DIR}/libbare-core-build/${_CORE_LIB}")

# ---------------------------------------------------------------------------
# Phase 1 (mobile only): build mbedTLS
# ---------------------------------------------------------------------------
set(MBEDTLS_ARGS "")
set(MBEDTLS_EXTRA_LIBS "")

if(LIBBARE_TLS STREQUAL "mbedtls")
  if(NOT EXISTS "${LIBBARE_MBEDTLS_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
      "LIBBARE_TLS=mbedtls but mbedTLS sources not found at "
      "${LIBBARE_MBEDTLS_SOURCE_DIR}. "
      "Run: git submodule update --init third_party/mbedtls")
  endif()

  set(_MBEDTLS_A   "${SYSROOT}/lib/${_LPFX}mbedtls${_EXT}")
  set(_MBEDX509_A  "${SYSROOT}/lib/${_LPFX}mbedx509${_EXT}")
  set(_MBEDCRYPTO_A "${SYSROOT}/lib/${_LPFX}mbedcrypto${_EXT}")

  ExternalProject_Add(mbedtls_project
    SOURCE_DIR "${LIBBARE_MBEDTLS_SOURCE_DIR}"
    BINARY_DIR "${CMAKE_BINARY_DIR}/mbedtls-build"
    CMAKE_ARGS
      -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
      -DCMAKE_INSTALL_PREFIX=${SYSROOT}
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
  )
  set(MBEDTLS_EXTRA_LIBS "${_MBEDTLS_A}" "${_MBEDX509_A}" "${_MBEDCRYPTO_A}")
  set(MBEDTLS_DEPENDS mbedtls_project)
else()
  set(MBEDTLS_DEPENDS "")
  set(MBEDTLS_ARGS
    "-DUSE_OPENSSL=ON"
    "-DUSE_MBEDTLS=OFF"
  )
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
  SOURCE_DIR "${LIBBARE_RE_SOURCE_DIR}"
  BINARY_DIR "${CMAKE_BINARY_DIR}/re-build"
  CMAKE_ARGS
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DCMAKE_INSTALL_PREFIX=${SYSROOT}
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
string(REPLACE ";" "|" LIBBARE_MODULES_SEP "${LIBBARE_MODULES}")

ExternalProject_Add(baresip_project
  DEPENDS re_project
  SOURCE_DIR "${LIBBARE_BARESIP_SOURCE_DIR}"
  BINARY_DIR "${CMAKE_BINARY_DIR}/baresip-build"
  LIST_SEPARATOR "|"
  CMAKE_ARGS
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DCMAKE_INSTALL_PREFIX=${SYSROOT}
    -DCMAKE_PREFIX_PATH=${SYSROOT}
    -DRE_INCLUDE_DIR=${SYSROOT}/include/re
    -DRE_INCLUDE_DIRS=${SYSROOT}/include/re
    -DRE_LIBRARY=${_RE_A}
    -DRE_LIBRARIES=${_RE_A}
    -Dre_DIR=${SYSROOT}/lib/cmake/re
    -DSTATIC=ON
    "-DMODULES=${LIBBARE_MODULES_SEP}"
    ${CROSS_ARGS}
  BUILD_BYPRODUCTS "${_BARE_A}"
)

# ---------------------------------------------------------------------------
# Phase 3.5: compile libbare source layer against installed headers
# ---------------------------------------------------------------------------
ExternalProject_Add(libbare_core_project
  DEPENDS baresip_project
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
  BINARY_DIR "${CMAKE_BINARY_DIR}/libbare-core-build"
  CMAKE_ARGS
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DLIBBARE_SRC_MODE=ON
    -DLIBBARE_SYSROOT=${SYSROOT}
    -DLIBBARE_MODULES_PROFILE=${LIBBARE_MODULES_PROFILE}
    ${CROSS_ARGS}
  BUILD_BYPRODUCTS "${_CORE_A}"
  INSTALL_COMMAND ""
)

# ---------------------------------------------------------------------------
# Phase 4: merge archives into single libbare.a / bare.lib
# ---------------------------------------------------------------------------
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/MergeStaticLibs.cmake")

if(MSVC)
  set(_OUT "${CMAKE_BINARY_DIR}/bare.lib")
else()
  set(_OUT "${CMAKE_BINARY_DIR}/libbare.a")
endif()

libbare_merge(
  OUTPUT   "${_OUT}"
  INPUTS   "${_CORE_A}" "${_RE_A}" "${_BARE_A}" ${MBEDTLS_EXTRA_LIBS}
  DEPENDS  libbare_core_project
)

# ---------------------------------------------------------------------------
# Install merged archive + headers to dist/
# ---------------------------------------------------------------------------
set(_DIST "${LIBBARE_DIST_DIR}/${LIBBARE_PLATFORM_ID}")

install(FILES "${_OUT}" DESTINATION "${_DIST}")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/include/libbare.h"
              DESTINATION "${_DIST}/include")
install(DIRECTORY "${SYSROOT}/include/re/"  DESTINATION "${_DIST}/include/re")
install(FILES     "${SYSROOT}/include/baresip.h" DESTINATION "${_DIST}/include")
