# patch-baresip-sources.cmake
# Apply VoxSDK's baresip patches at build time.  Two jobs:
#   1. Disable baresip's executables (baresip_exe, test/, webrtc/) everywhere.
#   2. Make dtls_srtp recover from a handshake that is perturbed or stalls.
# Usage: cmake -DSOURCE_DIR=path/to/baresip -P patch-baresip-sources.cmake
#
# third_party/baresip is fetched by scripts/fetch-third-party.sh at a pinned
# revision and is gitignored, so an in-place patch cannot leak into upstream
# history.  Each job is idempotent behind its own marker and fails loudly when
# a baresip bump moves the text it splices at, rather than silently building
# without the fix.
#
# The SDK owns the public websock_connect() and sip_dialog_route() names:
# libre's definitions are renamed to __real_* (cmake/patch-re-sources.cmake)
# and the only provider of the public symbols is VoxSDK's src/ws_path.c.
# baresip's executables link libre directly without ws_path.c, so they stop
# linking — and the SDK only ever consumes libbaresip.a anyway.  This is the
# executable-disabling half of what fix-msvc-baresip.cmake did for MSVC,
# promoted to every platform now that the rename is unconditional.
#
# Idempotent behind the "# VoxSDK-disabled" marker (the legacy
# "# VoxSDK-msvc-disabled" marker from fix-msvc-baresip.cmake also counts, so
# a Windows tree patched by the older script is left alone).

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR must be defined")
endif()

set(BARESIP_CMAKE "${SOURCE_DIR}/CMakeLists.txt")
if(NOT EXISTS "${BARESIP_CMAKE}")
  message(FATAL_ERROR "patch-baresip-sources: ${BARESIP_CMAKE} does not exist")
endif()

file(READ "${BARESIP_CMAKE}" CONTENT)

string(FIND "${CONTENT}" "# VoxSDK-disabled" _ALREADY)
string(FIND "${CONTENT}" "# VoxSDK-msvc-disabled" _ALREADY_MSVC)
if(NOT _ALREADY EQUAL -1 OR NOT _ALREADY_MSVC EQUAL -1)
  message(STATUS "patch-baresip-sources: ${BARESIP_CMAKE} already patched (executables disabled)")
  set(_SKIP_EXES TRUE)
endif()

# A plain return() here would take the dtls_srtp job below down with it on
# every rebuild — the tree stays patched, so this branch is the common case.
if(NOT _SKIP_EXES)
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
  "# add_subdirectory(webrtc)  # VoxSDK-disabled"
  CONTENT "${CONTENT}")
string(REPLACE
  "add_subdirectory(test)"
  "# add_subdirectory(test)  # VoxSDK-disabled"
  CONTENT "${CONTENT}")
string(REPLACE
  "add_executable(baresip_exe src/main.c)"
  "add_executable(baresip_exe EXCLUDE_FROM_ALL src/main.c)  # VoxSDK-disabled"
  CONTENT "${CONTENT}")
# The exe's OUTPUT_NAME collides with the library target's name: Ninja resolves
# `--build --target baresip` to the FILE named baresip (the excluded exe) and
# builds it anyway. Rename the never-shipped exe so the target name means the
# library on every generator.
string(REPLACE
  "set_target_properties(baresip_exe PROPERTIES OUTPUT_NAME baresip)"
  "set_target_properties(baresip_exe PROPERTIES OUTPUT_NAME baresip_exe)  # VoxSDK-disabled"
  CONTENT "${CONTENT}")
string(REPLACE
  "install(TARGETS baresip_exe baresip"
  "install(TARGETS baresip  # VoxSDK-disabled baresip_exe"
  CONTENT "${CONTENT}")

file(WRITE "${BARESIP_CMAKE}" "${CONTENT}")
message(STATUS "patch-baresip-sources: executables disabled in ${BARESIP_CMAKE}")
endif()

# ---------------------------------------------------------------------------
# Job 2: dtls_srtp — a handshake that is perturbed or stalls must recover
#
# media_start() latches `started` and nothing ever clears it, so the module
# starts a DTLS association exactly once per stream for the life of a call.
# Two consequences, both reached on-device (2026-08-26, inbound call over WSS
# behind a NAT presenting more than one egress IP):
#
#   - A re-negotiation that reverses the DTLS role is decoded into `st->active`
#     and then discarded, because media_start() returns early.  Each side ends
#     up waiting for the other to be the client.
#   - A handshake that simply never completes — a ClientHello lost while the
#     remote address moved under it, a peer that restarted its own association
#     across a re-INVITE — is never retried.
#
# Either way `menc_secure` is never set, stream_is_ready() stays false,
# audio_update() is never called, and the call sits fully established with
# tx 0 / rx 0 and no audio in either direction while signalling looks perfect.
#
# src/ice_shim.c stops the SDK from causing the first case itself (it holds its
# candidate re-offer until the handshake finishes).  These patches make the
# module survive the case when something else causes it.
#
# The `setup` hunk goes further and stops the SDK from needing to survive it on
# an inbound call at all: the answerer advertises `a=setup:passive`, so the peer
# is the side that sends the ClientHello and our media never has to be accepted
# from an address the peer was not told about.  Measured on-device (2026-08-31):
# 9 of 9 inbound calls whose ICE-selected candidate differed from the signalled
# one came up silent under the old `active` answer, against a clean outbound
# call in the same capture, where the peer connects to us.  The `role` hunk
# keeps the advertised role and the role taken in step.
#
# The hunks live as verbatim .old/.new file pairs in cmake/patches/ rather than
# as inline strings: the replacement is ~90 lines of C, and CMake escaping of
# tabs, semicolons and quotes at that size is its own source of bugs.
# ---------------------------------------------------------------------------

set(DTLS_SRTP_C "${SOURCE_DIR}/modules/dtls_srtp/dtls_srtp.c")
if(NOT EXISTS "${DTLS_SRTP_C}")
  message(FATAL_ERROR "patch-baresip-sources: ${DTLS_SRTP_C} does not exist")
endif()

file(READ "${DTLS_SRTP_C}" DCONTENT)
string(FIND "${DCONTENT}" "VoxSDK-patched" _DTLS_DONE)

if(NOT _DTLS_DONE EQUAL -1)
  message(STATUS "patch-baresip-sources: ${DTLS_SRTP_C} already patched (dtls recovery)")
else()
  set(_HUNKS state setup recovery role estab destr)

  # Verify every anchor before splicing any of them, so a baresip bump can
  # never leave the file half-patched.
  foreach(_h ${_HUNKS})
    set(_of "${CMAKE_CURRENT_LIST_DIR}/patches/dtls_srtp-${_h}.old")
    set(_nf "${CMAKE_CURRENT_LIST_DIR}/patches/dtls_srtp-${_h}.new")
    if(NOT EXISTS "${_of}" OR NOT EXISTS "${_nf}")
      message(FATAL_ERROR "patch-baresip-sources: missing hunk files for '${_h}'")
    endif()
    file(READ "${_of}" _old)
    string(FIND "${DCONTENT}" "${_old}" _pos)
    if(_pos EQUAL -1)
      message(FATAL_ERROR
        "patch-baresip-sources: anchor not found in ${DTLS_SRTP_C} for hunk "
        "'${_h}'.\nThe pinned baresip revision has moved this code. Re-derive "
        "cmake/patches/dtls_srtp-${_h}.old against the new revision — building "
        "without it reintroduces permanently silent calls.")
    endif()
  endforeach()

  foreach(_h ${_HUNKS})
    file(READ "${CMAKE_CURRENT_LIST_DIR}/patches/dtls_srtp-${_h}.old" _old)
    file(READ "${CMAKE_CURRENT_LIST_DIR}/patches/dtls_srtp-${_h}.new" _new)
    string(REPLACE "${_old}" "${_new}" DCONTENT "${DCONTENT}")
  endforeach()

  file(WRITE "${DTLS_SRTP_C}" "${DCONTENT}")
  message(STATUS "patch-baresip-sources: patched ${DTLS_SRTP_C} (dtls handshake recovery)")
endif()

# ---------------------------------------------------------------------------
# Job 3: ice — gather only on the interface that carries the signalling
#
# One component, one wildcard-bound socket: every interface if_handler walks
# adds another candidate on that same socket, and the kernel still routes its
# packets by destination.  A second interface therefore adds a label, not a
# path, and on-device (2026-08-31, Wi-Fi and cellular both up) the label was
# wrong — the srflx gathered from the cellular base came back holding the Wi-Fi
# WAN's address, became the default candidate, and went into the SDP as the
# address our media comes from while every packet left over Wi-Fi.  ICE then
# learned the real address as peer-reflexive, so the call ran with a signalled
# address and a selected address that disagreed, which is the state src/
# ice_shim.c was written to paper over with a candidate re-offer.
#
# The full rationale, including what this does *not* fix, is in the hunk.
# ---------------------------------------------------------------------------

set(ICE_C "${SOURCE_DIR}/modules/ice/ice.c")
if(NOT EXISTS "${ICE_C}")
  message(FATAL_ERROR "patch-baresip-sources: ${ICE_C} does not exist")
endif()

file(READ "${ICE_C}" ICONTENT)
string(FIND "${ICONTENT}" "VoxSDK-patched" _ICE_DONE)

if(NOT _ICE_DONE EQUAL -1)
  message(STATUS "patch-baresip-sources: ${ICE_C} already patched (interface selection)")
else()
  set(_IOF "${CMAKE_CURRENT_LIST_DIR}/patches/ice-ifsel.old")
  set(_INF "${CMAKE_CURRENT_LIST_DIR}/patches/ice-ifsel.new")
  if(NOT EXISTS "${_IOF}" OR NOT EXISTS "${_INF}")
    message(FATAL_ERROR "patch-baresip-sources: missing hunk files for 'ice-ifsel'")
  endif()

  file(READ "${_IOF}" _iold)
  file(READ "${_INF}" _inew)
  string(FIND "${ICONTENT}" "${_iold}" _ipos)
  if(_ipos EQUAL -1)
    message(FATAL_ERROR
      "patch-baresip-sources: anchor not found in ${ICE_C} for hunk "
      "'ice-ifsel'.\nThe pinned baresip revision has moved if_handler(). "
      "Re-derive cmake/patches/ice-ifsel.old against the new revision — "
      "building without it puts an address the media never uses in the SDP.")
  endif()

  string(REPLACE "${_iold}" "${_inew}" ICONTENT "${ICONTENT}")
  file(WRITE "${ICE_C}" "${ICONTENT}")
  message(STATUS "patch-baresip-sources: patched ${ICE_C} (interface selection)")
endif()
