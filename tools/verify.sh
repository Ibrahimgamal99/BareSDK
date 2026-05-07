#!/usr/bin/env bash
# Verify a built baresdk.a archive.
# Usage:
#   ./tools/verify.sh <path/to/baresdk.a> [link]
#
# Without "link": checks symbol presence only.
# With "link":    also compiles and runs a linkability smoke-test
#                 (requires matching system headers + deps).
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

ARCHIVE="${1:?Usage: verify.sh <path/to/baresdk.a> [link]}"
MODE="${2:-check}"

if [ ! -f "${ARCHIVE}" ]; then
  echo "ERROR: archive not found: ${ARCHIVE}" >&2
  exit 1
fi

echo "============================================================"
echo "Verifying: ${ARCHIVE}"
echo "Size     : $(du -h "${ARCHIVE}" | cut -f1)"
echo "============================================================"

# ---------------------------------------------------------------------------
# 1. Symbol presence
# ---------------------------------------------------------------------------
echo ""
echo "--- Symbol presence ---"

# Cache nm output once — avoids nm getting SIGPIPE from grep -q exiting early
# (which pipefail would then misreport as a pipeline failure)
NM_CACHE="$(nm -A "${ARCHIVE}" 2>/dev/null)"

check_sym() {
  local SYM="$1"
  # Use here-string (<<<) to avoid echo|grep SIGPIPE → pipefail false-negative
  if grep -q " T ${SYM}\b" <<< "${NM_CACHE}"; then
    printf "  %-40s  [OK]\n" "${SYM}"
  else
    printf "  %-40s  [MISSING]\n" "${SYM}"
    MISSING_SYMS=$((MISSING_SYMS + 1))
  fi
}

MISSING_SYMS=0

# baresdk public API
check_sym "baresdk_init"
check_sym "baresdk_shutdown"
check_sym "baresdk_config_init"
check_sym "baresdk_account_create"
check_sym "baresdk_account_destroy"
check_sym "baresdk_account_register"
check_sym "baresdk_account_unregister"
check_sym "baresdk_call_invite"
check_sym "baresdk_call_answer"
check_sym "baresdk_call_hangup"
check_sym "baresdk_call_hold"
check_sym "baresdk_call_resume"
check_sym "baresdk_call_send_dtmf"
check_sym "baresdk_call_transfer"
check_sym "baresdk_call_set_media_tap"
check_sym "baresdk_call_get_stats"
check_sym "baresdk_audio_mute"
check_sym "baresdk_audio_set_input_device"
check_sym "baresdk_audio_set_output_device"
check_sym "baresdk_pcap_start"
check_sym "baresdk_pcap_stop"
check_sym "baresdk_version"

# Phase 2
check_sym "baresdk_call_attended_transfer"
check_sym "baresdk_message_send"
check_sym "baresdk_account_publish_presence"
check_sym "baresdk_account_set_100rel"

# Underlying baresip/libre internals (should still be in the merged archive)
check_sym "baresip_init"
check_sym "ua_init"
check_sym "re_main"
check_sym "websock_connect"
check_sym "sip_transp_add_websock"
check_sym "srtp_decrypt"

echo ""
if [ "${MISSING_SYMS}" -gt 0 ]; then
  echo "WARNING: ${MISSING_SYMS} expected symbol(s) not found."
  echo "         Check that STATIC=ON and the correct modules were built."
else
  echo "All expected symbols present."
fi

# ---------------------------------------------------------------------------
# 2. Object member count (sanity: merged archive should be non-trivial)
# ---------------------------------------------------------------------------
echo ""
echo "--- Object member count ---"
MEMBER_COUNT=$(grep -c " T " <<< "${NM_CACHE}" || true)
echo "  Exported text symbols : ${MEMBER_COUNT}"
if [ "${MEMBER_COUNT}" -lt 100 ]; then
  echo "WARNING: suspiciously few symbols — archive may be incomplete."
fi

# ---------------------------------------------------------------------------
# 3. Header parseability (preprocessor sanity)
# ---------------------------------------------------------------------------
INCLUDE_DIR="$(dirname "${ARCHIVE}")/include"
if [ -f "${INCLUDE_DIR}/baresip.h" ]; then
  echo ""
  echo "--- Header parseability ---"
  if clang -E -x c "${INCLUDE_DIR}/baresip.h" -I "${INCLUDE_DIR}" > /dev/null 2>&1; then
    echo "  baresip.h: [OK] (clang -E passed)"
  elif gcc  -E -x c "${INCLUDE_DIR}/baresip.h" -I "${INCLUDE_DIR}" > /dev/null 2>&1; then
    echo "  baresip.h: [OK] (gcc -E passed)"
  else
    echo "  baresip.h: [WARN] preprocessor failed — check include paths"
  fi
fi

# ---------------------------------------------------------------------------
# 4. Linkability smoke-test (optional, requires system deps)
# ---------------------------------------------------------------------------
if [ "${MODE}" = "link" ]; then
  echo ""
  echo "--- Linkability smoke-test ---"

  SMOKETEST_C="${ROOT}/tools/_smoketest.c"
  SMOKETEST_BIN="${ROOT}/tools/_smoketest"

  cat > "${SMOKETEST_C}" <<'CSRC'
/* baresdk linkability smoke-test */
#include "baresdk.h"
#include <stdio.h>

static void evt(const baresdk_event_t *ev, void *ud) { (void)ev; (void)ud; }

int main(void)
{
    baresdk_config_t cfg;
    baresdk_config_init(&cfg);
    cfg.event_cb = evt;

    int err = baresdk_init(&cfg);
    if (err) { fprintf(stderr, "baresdk_init: %d\n", err); return 1; }

    printf("baresdk_init OK — version %s\n", baresdk_version());

    baresdk_shutdown();
    printf("smoketest PASSED\n");
    return 0;
}
CSRC

  INCS="-I ${INCLUDE_DIR} -I ${INCLUDE_DIR}/re"

  # Try to link — platform link flags
  case "$(uname -s)" in
    Linux)
      LDFLAGS="-lpthread -lssl -lcrypto -lz -lm -ldl -lresolv"
      ;;
    Darwin)
      LDFLAGS="-framework CoreFoundation -framework Security"
      ;;
    *)
      LDFLAGS=""
      ;;
  esac

  if ${CC:-gcc} -o "${SMOKETEST_BIN}" "${SMOKETEST_C}" \
      ${INCS} "${ARCHIVE}" ${LDFLAGS} 2>&1; then
    echo "  Compilation: [OK]"
    if "${SMOKETEST_BIN}"; then
      echo "  Execution  : [OK]"
    else
      echo "  Execution  : [FAILED]"
    fi
  else
    echo "  Compilation: [FAILED] — check system deps and link flags"
  fi

  rm -f "${SMOKETEST_C}" "${SMOKETEST_BIN}"
fi

echo ""
echo "============================================================"
echo "Verification complete."
echo "============================================================"
