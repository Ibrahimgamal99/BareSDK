# Changelog

All notable changes to baresdk are documented here.

---

## [1.2.0] — 2026-05-10

### Added

#### Audio recording
- `baresdk_call_record_start(call, path)` — record call audio to a single mixed WAV file (PCM S16LE). Both the received (RX) and sent (TX) audio are clip-summed into one stream.
- `baresdk_call_record_stop(call)` — stop recording and finalize the WAV header with correct sizes. The file is also closed automatically if the call is destroyed.
- Recording runs independently of the PCM media tap — both can be active simultaneously on the same call.

#### Registration retry control
- `baresdk_account_set_retry_policy(acct, initial_ms, max_ms, backoff, max_attempts)` — override the retry policy for a specific account at runtime without recreating it. Overrides the global `reg_retry_*` fields in `baresdk_config_t` for that account only.
- `baresdk_account_cancel_retry(acct)` — cancel a pending retry timer and reset the attempt counter. The account stays in `FAILED` state; call `baresdk_account_register()` to restart manually.
- `baresdk_account_retry_now(acct)` — skip the current backoff delay and re-register immediately. Resets the attempt counter.

---

## [1.1.0] — 2026-05-08

### Fixed — Critical call flow bugs

#### Incoming calls
- **AOR construction** now includes port and IPv6 brackets. Previously, registrations against a non-default SIP port received `404 Not Found` on every inbound call because Asterisk/FreeSWITCH matched AOR by exact string.
- **Silent INCOMING_CALL drop under load** — the call wrapper is now registered only after a queue slot is confirmed. Previously under memory pressure the wrapper was registered but the event dropped, leaving baresip in EARLY state with the app unaware.
- **Race on fast cancel** — if the caller cancels before the event thread delivers INCOMING_CALL, the CLOSED event no longer nulls the call handle first. `baresdk_call_answer()` no longer spuriously returns `ENOENT`.
- **Incoming call destructor** — tap lock and custom header list now have a proper destructor; previously leaked on every received call.

#### Outgoing calls
- **Orphaned baresip call on alloc failure** — if wrapper `mem_alloc` fails after `ua_connect` succeeds, the SIP INVITE is now cancelled with 500. Previously the call rang on the wire while the app received `ENOMEM` with no handle.

#### Registration
- **SIP error code parsing** — `strstr(reason, "5")` replaced with proper 3-digit extraction; 415, 451, 486 are no longer misclassified as 5xx. 407 Proxy Auth now correctly maps to `BARESDK_ERR_AUTH`.

#### NAT / ICE
- **STUN + TURN co-existence** — when both `stun_server` and `turn_server` are configured, TURN takes precedence as the active ICE server. Previously configuring TURN always silently overwrote the STUN setting.

#### Memory
- **Call wrapper leak** — wrappers are freed after `BARESDK_EV_CALL_CLOSED` is delivered. Previously every completed call leaked one wrapper.
- **Stats queue bypass** — stats events now respect `ev_queue_max` and update `ev_queue_len`; previously they bypassed the limit and could grow the queue without bound on long calls.

#### Mutex safety
- **Init-time mutex lock** (UB on Windows/RTOS) — `mtx_lock` before `mtx_init` replaced with `bsdk_call_global_init()` called from `baresdk_init`.
- **Shutdown mutex order** — `mtx_destroy` now called after `mtx_unlock` in `bsdk_call_global_reset`.

#### IPv6
- **AOR formatting** — AOR now correctly produces `sip:user@[2001:db8::1]:5060;transport=tls`.
- **URI parsing** — `parse_account_uri` now skips `[…]` brackets before scanning for `:port`, preventing the host from splitting at the first colon inside an IPv6 literal.

---

### Changed — Build system
- Platform build scripts (`build-linux.sh`, `build-android.sh`, `build-macos.sh`, `build-windows.ps1`) now produce **both** the static archive and the shared library in one run. No separate shared-lib scripts required.
- Shared libraries are now **fully self-contained** — no extra packages needed at runtime:
  - **Linux** — OpenSSL, zlib, pthreads, libm, libresolv baked in as `.a`; only `libdl` / `libc` (glibc) remain dynamic.
  - **macOS** — Homebrew OpenSSL `.a` embedded; only Apple system frameworks remain.
  - **Windows** — vcpkg `libssl.lib`, `libcrypto.lib`, `zlib.lib` linked directly into the DLL.
  - **Android / iOS** — mbedTLS was already merged in; no change.
- Removed: `scripts/build_shared_linux.sh`, `scripts/build_shared_macos.sh`, `scripts/build_shared_android.sh`, `scripts/build_shared_windows.sh`.

---

### Changed — Bindings
- C / C++ compile commands simplified — no extra `-l` flags needed when linking against the self-contained shared lib.
- **One-command setup scripts** added for every language binding — each `build.sh` builds the SDK if needed and then installs/compiles the binding in one step:
  - `bash bindings/cpp/build.sh`
  - `bash bindings/python/build.sh`
  - `bash bindings/nodejs/build.sh`
  - `bash bindings/rust/build.sh`
- **Python** — `_loader.py` now auto-discovers `baresdk.so` in `dist/<platform>/<arch>/` when running from a source checkout. Manual `LD_LIBRARY_PATH` or file copy no longer required.
- **Rust** — `build.rs` auto-selects the correct `dist/` sub-directory (`linux/x86_64`, `linux/arm64`, `macos/universal`, `windows/x64`) based on the Cargo target. `BARESDK_LIB_DIR` export no longer required for native builds.
- **Node.js** — `binding.gyp` replaced fragile relative `-L` paths with absolute paths resolved at build time via `node -p`. Sets `-Wl,-rpath` so the addon finds `baresdk.so` at runtime without `LD_LIBRARY_PATH`. Supports `BARESDK_DIST_DIR` env var to override.
- **C++** — `CMakeLists.txt` auto-detects platform and architecture (`linux/x86_64`, `linux/arm64`, `macos/universal`, `windows/x64`) instead of hardcoding `linux/x86_64`.
- Quickstart docs updated for all languages to reflect one-command setup.

---

## [1.0.0] — 2025-05-08

### Added

#### Core SDK
- SIP UA with full INVITE/BYE/REGISTER flow
- Transports: UDP, TCP, TLS, WebSocket (WS), secure WebSocket (WSS)
- Media encryption: none, SDES (RFC 4568), DTLS-SRTP (RFC 5764)
- ICE / STUN / TURN NAT traversal
- Audio codecs: Opus, G.711 (PCMU/PCMA), G.722
- Audio processing: AEC, noise suppression, AGC
- DTMF via RFC 4733 RTP events
- Blind and attended call transfer (REFER)
- SIP MESSAGE (in/out of dialog)
- Presence: PUBLISH and SUBSCRIBE/NOTIFY
- MWI (message-waiting indication)
- 100rel / PRACK support (RFC 3262)
- Session timers (RFC 4028)
- Multi-account support with per-account config overrides
- Custom SIP headers (per-account and per-call)
- Audio device selection (input/output by name)
- PCM media tap (per-call RX/TX audio frame callback)

#### Observability
- RTCP media stats: loss, jitter, RTT, bandwidth
- MOS scoring: E-Model (ITU-T G.107) and simplified
- SIP trace (per-message TX/RX capture)
- SDP negotiation trace (codec + crypto result)
- Pcap capture (Wireshark-compatible output)
- Configurable log levels (error/warn/info/debug)
- Registrar warning events

#### Platforms
- Linux x86_64 (OpenSSL)
- macOS universal (x86_64 + arm64, OpenSSL)
- Windows x64 (OpenSSL via vcpkg)
- Android (arm64-v8a, armeabi-v7a, x86_64, mbedTLS)
- iOS device + simulator (mbedTLS)

#### Build system
- CMake with ExternalProject (re + baresip + optional mbedTLS)
- Static archive merge (`baresdk.a`) via libtool / lib.exe / ar MRI
- `BARESDK_SHARED` option for shared library output (`.so` / `.dylib` / `.dll`)
- Platform build scripts: `build-linux.sh`, `build-macos.sh`, `build-ios.sh`, `build-android.sh`, `build-windows.ps1`

#### C++ binding
- Header-only RAII wrapper (`bindings/cpp/baresdk.hpp`)
- `SDK`, `Account`, `Call` classes with automatic resource cleanup

#### Python binding
- cffi-based wrapper (`bindings/python/`)
- `SDK`, `Account`, `Call` Pythonic classes
- Event delivery via `queue.SimpleQueue` generator pattern
- Clean header preprocessing via `generate_clean_header.sh`

#### Rust binding
- `baresdk-sys` crate (bindgen auto-generated raw FFI)
- `baresdk` crate (safe wrapper with `Result` error handling)
- Event delivery via `std::sync::mpsc` channel

#### Node.js binding
- N-API C++ addon (`bindings/nodejs/`)
- `SDK`, `Account`, `Call` JavaScript classes
- TypeScript declarations (`.d.ts`)
- `node-gyp` build configuration

#### Flutter / Dart binding
- dart:ffi + ffigen wrapper (`bindings/flutter/`)
- `BareSDK`, `Account`, `Call` Dart classes
- Event delivery via `Stream<BareSDKEvent>` with `StreamController`
- Multi-platform library loading (Android, iOS, macOS, Windows, Linux)

#### Documentation
- API reference: overview, config, accounts, calls, media, events, observability
- Quickstart guides: C/C++, Python, Rust, Node.js, Flutter
- How-to guides: NAT traversal, TLS/WSS, multi-account, WebRTC browser interop, debugging
- Configuration examples (`accounts_example.json`)
