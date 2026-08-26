#!/usr/bin/env bash
# Download the prebuilt native libraries from a build-mobile CI run into the
# Flutter plugin, where the podspec and the Gradle ffiPlugin vendor them.
#
#   scripts/fetch-prebuilt.sh                 latest successful run on main
#   scripts/fetch-prebuilt.sh --wait          wait for the newest run, then sync
#   scripts/fetch-prebuilt.sh --run-id 123    a specific run
#   scripts/fetch-prebuilt.sh --branch dev    latest successful run on a branch
#
# Why this exists: the plugin ships *prebuilt* binaries, so a consumer adding
# it as a git dependency gets whatever is committed — not whatever CI last
# built. CI builds both platforms and uploads them as artifacts, but nothing
# commits them back, so the two drift silently. That drift has already shipped
# once: the committed libechosdk.so predated the baresdk_* -> echosdk_* rename
# and exported none of the symbols ffi_bindings.dart looks up, so apps built,
# installed, and then failed at the first SDK call.
#
# Nothing here is committed for you — review, then `git add` the two paths
# printed at the end.
#
# Requires: gh (authenticated). Symbol verification additionally wants readelf
# for Android and nm for iOS; it is skipped with a warning where unavailable.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

WORKFLOW="build-mobile.yml"
BRANCH="main"
RUN_ID=""
WAIT=0
EXPLICIT=0

IOS_DEST="${ROOT}/bindings/flutter/ios/Frameworks/EchoSDK.xcframework"
AND_DEST="${ROOT}/bindings/flutter/android/src/main/jniLibs"

while [ $# -gt 0 ]; do
  case "$1" in
    --run-id) RUN_ID="$2"; EXPLICIT=1; shift 2 ;;
    --branch) BRANCH="$2"; shift 2 ;;
    --wait)   WAIT=1; shift ;;
    -h|--help) sed -n '2,20p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "ERROR: unknown argument '$1' (try --help)" >&2; exit 1 ;;
  esac
done

if ! command -v gh >/dev/null; then
  echo "ERROR: gh not found. Install the GitHub CLI: https://cli.github.com" >&2
  exit 1
fi
if ! gh auth status >/dev/null 2>&1; then
  echo "ERROR: gh is not authenticated. Run: gh auth login" >&2
  exit 1
fi

cd "${ROOT}"

# ---------------------------------------------------------------------------
# Pick the run
# ---------------------------------------------------------------------------
if [ -n "${RUN_ID}" ]; then
  : # explicit
elif [ "${WAIT}" = "1" ]; then
  # Newest run on the branch whatever its state, then block until it settles.
  RUN_ID="$(gh run list --workflow="${WORKFLOW}" --branch="${BRANCH}" \
              --limit 1 --json databaseId --jq '.[0].databaseId')"
  if [ -z "${RUN_ID}" ]; then
    echo "ERROR: no ${WORKFLOW} runs found on '${BRANCH}'" >&2
    exit 1
  fi
  echo "=== Waiting for run ${RUN_ID} ==="
  # `gh run watch` exits non-zero when the run fails; the status check below
  # reports which job died, so don't let -e kill us before we get there.
  gh run watch "${RUN_ID}" --exit-status || true
else
  RUN_ID="$(gh run list --workflow="${WORKFLOW}" --branch="${BRANCH}" \
              --status=success --limit 1 --json databaseId --jq '.[0].databaseId')"
  if [ -z "${RUN_ID}" ]; then
    echo "ERROR: no successful ${WORKFLOW} run on '${BRANCH}'." >&2
    echo "       Pass --run-id to sync from a specific run anyway." >&2
    exit 1
  fi
fi

# gh has jq built in (--jq), so this needs no jq on PATH.
read -r SHA STATUS CONCL <<<"$(gh run view "${RUN_ID}" \
  --json headSha,status,conclusion \
  --jq '[.headSha, .status, .conclusion] | @tsv')"
TITLE="$(gh run view "${RUN_ID}" --json displayTitle --jq .displayTitle)"

echo "=== Run ${RUN_ID} — ${TITLE} (${SHA:0:12}) — ${STATUS} ${CONCL} ==="

# A failed run can still have uploaded one artifact and not the other, which
# would leave the plugin half-updated: a fresh xcframework beside stale .so
# files is worse than either, because Android then fails at runtime instead of
# at build time. Refuse unless the run is explicitly named.
if [ "${CONCL}" != "success" ] && [ "${EXPLICIT}" != "1" ]; then
  echo "ERROR: run ${RUN_ID} concluded '${CONCL}', not 'success'." >&2
  echo "       Re-run with --run-id ${RUN_ID} to sync from it anyway." >&2
  exit 1
fi

TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT

# ---------------------------------------------------------------------------
# iOS — the artifact holds the framework's *contents* (the workflow's upload
# path ends in "/"), so the destination directory has to supply the
# EchoSDK.xcframework name itself.
# ---------------------------------------------------------------------------
echo "=== iOS: ios-xcframework ==="
gh run download "${RUN_ID}" -n ios-xcframework -D "${TMP}/ios"
if [ ! -f "${TMP}/ios/Info.plist" ]; then
  echo "ERROR: artifact is not an xcframework payload (no top-level Info.plist)" >&2
  exit 1
fi
rm -rf "${IOS_DEST}"
mkdir -p "$(dirname "${IOS_DEST}")"
mv "${TMP}/ios" "${IOS_DEST}"

# The framework binary must stay executable. actions/upload-artifact has
# historically dropped the exec bit, and git tracks it — a 644 binary is a
# broken framework that looks fine in a diff.
find "${IOS_DEST}" -name EchoSDK -type f -exec chmod 755 {} +

SLICES=$(find "${IOS_DEST}" -name EchoSDK -type f | wc -l | tr -d ' ')
if [ "${SLICES}" -lt 2 ]; then
  echo "ERROR: expected device + simulator slices, found ${SLICES}" >&2
  exit 1
fi
echo "  ${SLICES} slices:"
find "${IOS_DEST}" -name EchoSDK -type f | while read -r BIN; do
  echo "    $(basename "$(dirname "$(dirname "${BIN}")")")"
done

# ---------------------------------------------------------------------------
# Android — overwrite the three ABIs in place.
# ---------------------------------------------------------------------------
echo "=== Android: android-jniLibs ==="
gh run download "${RUN_ID}" -n android-jniLibs -D "${TMP}/jni"
for ABI in arm64-v8a armeabi-v7a x86_64; do
  SRC="${TMP}/jni/${ABI}/libechosdk.so"
  if [ ! -f "${SRC}" ]; then
    echo "ERROR: artifact is missing ${ABI}/libechosdk.so" >&2
    exit 1
  fi
  mkdir -p "${AND_DEST}/${ABI}"
  cp "${SRC}" "${AND_DEST}/${ABI}/libechosdk.so"
done

# ---------------------------------------------------------------------------
# Verify the symbols the Dart side actually looks up. This is the check that
# would have caught the baresdk_* blobs before they shipped.
# ---------------------------------------------------------------------------
if command -v readelf >/dev/null; then
  READELF=readelf
elif command -v llvm-readelf >/dev/null; then
  READELF=llvm-readelf
else
  READELF=""
fi

if [ -n "${READELF}" ]; then
  for ABI in arm64-v8a armeabi-v7a x86_64; do
    SYMS="$(${READELF} --dyn-syms -W "${AND_DEST}/${ABI}/libechosdk.so" 2>/dev/null || true)"
    NEW=$(grep -c ' echosdk_' <<<"${SYMS}" || true)
    OLD=$(grep -c ' baresdk_' <<<"${SYMS}" || true)
    if [ "${NEW}" -lt 40 ]; then
      echo "ERROR: ${ABI}: only ${NEW} echosdk_* symbols exported" >&2
      exit 1
    fi
    if [ "${OLD}" -gt 0 ]; then
      echo "ERROR: ${ABI}: ${OLD} baresdk_* symbols — this library predates the rename" >&2
      exit 1
    fi
    echo "  ${ABI}: ${NEW} echosdk_* symbols"
  done
else
  echo "  (readelf not found — skipping Android symbol check)"
fi

# nm reads Mach-O only on macOS; elsewhere fall back to probing the string
# table, which is enough to catch an empty or wrong-project binary.
# Process substitution, not a pipe: a piped `while` runs in a subshell, where
# `exit 1` below would end the subshell and let the script report success.
while read -r BIN; do
  SLICE="$(basename "$(dirname "$(dirname "${BIN}")")")"
  if command -v nm >/dev/null && nm -gU "${BIN}" >/dev/null 2>&1; then
    N=$(nm -gU "${BIN}" | grep -c ' _echosdk_' || true)
  else
    N=$(strings -a "${BIN}" 2>/dev/null | grep -c '^_\?echosdk_' || true)
  fi
  if [ "${N}" -lt 40 ]; then
    echo "ERROR: ios/${SLICE}: only ${N} echosdk_* symbols" >&2
    exit 1
  fi
  echo "  ios/${SLICE}: ${N} echosdk_* symbols"
done < <(find "${IOS_DEST}" -name EchoSDK -type f)

echo ""
echo "Synced from run ${RUN_ID} (${SHA:0:12}). Review and commit:"
echo "  git add bindings/flutter/ios/Frameworks bindings/flutter/android/src/main/jniLibs"
