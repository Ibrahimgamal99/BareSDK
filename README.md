# baresdk

A thread-safe C SDK for VoIP — SIP/RTP built on [baresip](https://github.com/baresip/baresip) and [libre](https://github.com/baresip/re), delivered as a self-contained shared library and static archive per platform.

`baresdk.h` is the only header you need. The baresip/libre internals are an implementation detail.

## Features

| Category | Features |
|---|---|
| **Transport** | UDP · TCP · TLS · WS · WSS |
| **Crypto** | SRTP-SDES · DTLS-SRTP |
| **NAT** | ICE · STUN · TURN |
| **Calls** | Invite · Answer · Hangup · Hold/Resume · DTMF |
| **Transfer** | Blind (REFER) · Attended (REFER w/ Replaces) |
| **Messaging** | SIP MESSAGE send/receive |
| **Presence** | PUBLISH · SUBSCRIBE/NOTIFY · BLF · MWI |
| **Reliability** | PRACK / 100rel (RFC 3262) |
| **Push notifications** | RFC 8599 Contact URI params (APNs · FCM) · REGISTER-only custom headers for hosted servers |
| **Custom Headers** | Per-account + per-dialog (call) custom SIP headers |
| **Media** | PCM tap (TX/RX) · Audio recording to WAV (per-direction) · TX mute · RX mute (speaker) · device enumerate + hot-switch · mic gain · speaker gain · echo cancellation (half-duplex suppressor or WebRTC full-duplex) |
| **Observability** | SIP trace · SDP diff · pcap · RTCP/MOS stats (E-model + simplified) · jitter buffer · audio level · bandwidth (instant + avg, TX/RX) |
| **Multi-account** | Yes — any number of accounts per stack |
| **Thread safety** | Full — call any API from any thread |
| **Memory safety** | Deep-copied config strings — caller can free immediately after init |

---

## Supported platforms

Each build script produces both the **shared library** and the **static archive** in one step.
Shared libraries are **fully self-contained** — OpenSSL, zlib, and pthreads are baked in; consumers need no extra packages at runtime.

| Platform | Shared library | Static archive | TLS backend |
|---|---|---|---|
| Linux x86_64 | `dist/linux/x86_64/baresdk.so` | `baresdk.a` | OpenSSL (embedded) |
| macOS universal | `dist/macos/universal/baresdk.dylib` | `baresdk.a` | OpenSSL (embedded) |
| Windows x64 | `dist/windows/x64/baresdk.dll` | `bare.lib` | OpenSSL (embedded via vcpkg) |
| Android arm64-v8a | `dist/android/arm64-v8a/baresdk.so` | `baresdk.a` | mbedTLS (bundled) |
| Android armeabi-v7a | `dist/android/armeabi-v7a/baresdk.so` | `baresdk.a` | mbedTLS (bundled) |
| Android x86_64 | `dist/android/x86_64/baresdk.so` | `baresdk.a` | mbedTLS (bundled) |
| iOS device + simulator | `dist/ios/baresdk.xcframework` | — | mbedTLS (bundled) |

---

## Language bindings

| Language | Location | One-command setup |
|---|---|---|
| C / C++ | `include/baresdk.h` · `bindings/cpp/baresdk.hpp` | `bash bindings/cpp/build.sh` |
| Python | `bindings/python/` | `bash bindings/python/build.sh` |
| Flutter / Dart | `bindings/flutter/` | `dart pub get` in `bindings/flutter/` |

---

## Quick start

```bash
git clone --recurse-submodules https://github.com/Ibrahimgamal99/BareSDK.git
cd BareSDK

# Build SDK (shared lib + static archive)
bash scripts/build-linux.sh

# Verify
./tools/verify.sh dist/linux/x86_64/baresdk.so link
```

If you cloned without `--recurse-submodules`:

```bash
git submodule update --init --recursive third_party/re third_party/baresip
# mobile only:
git submodule update --init --recursive third_party/mbedtls
```

### C++ — one command

```bash
bash bindings/cpp/build.sh
# builds SDK if needed, compiles all examples, places binary + .so together
```

### Python — one command

```bash
bash bindings/python/build.sh
# 1. builds SDK if needed
# 2. regenerates the cffi header from include/baresdk.h
# 3. copies .so into the package directory
# 4. installs the Python package
# no LD_LIBRARY_PATH required; set it to override the bundled .so
```

### Flutter

```bash
bash scripts/build-android.sh    # or build-linux.sh / build-macos.sh
cd bindings/flutter
dart pub get
dart run ffigen --config ffigen.yaml   # only needed when baresdk.h changes
```

---

## Documentation

**[SDK usage guide — all operations, all languages](docs/guides/complete_example.md)**

Covers every operation in one place with code for C, C++, Python, and Flutter:
account setup, transport, TLS, STUN/TURN/ICE, calls, hold, mute, transfer,
stats, audio devices, pcap, and teardown.

Full docs: [docs/index.md](docs/index.md)

---

## API reference

### Lifecycle

```c
void baresdk_config_init(baresdk_config_t *cfg);   // zero-fill + set defaults
int  baresdk_init(const baresdk_config_t *cfg);    // start stack (once per process)
void baresdk_shutdown(void);                        // graceful teardown
const char *baresdk_version(void);
```

### Accounts

```c
int  baresdk_account_create(const baresdk_account_config_t *cfg,
                             baresdk_account_handle_t *out);
void baresdk_account_destroy(baresdk_account_handle_t acct);     // blocks until complete
int  baresdk_account_register(baresdk_account_handle_t acct);
int  baresdk_account_unregister(baresdk_account_handle_t acct);

// Retry control — override policy, cancel, or force immediate retry
int  baresdk_account_set_retry_policy(baresdk_account_handle_t acct,
                                       uint32_t initial_ms, uint32_t max_ms,
                                       float backoff, uint32_t max_attempts);
int  baresdk_account_cancel_retry(baresdk_account_handle_t acct);
int  baresdk_account_retry_now(baresdk_account_handle_t acct);

int  baresdk_account_add_header(baresdk_account_handle_t acct,
                                 const char *name, const char *value);
int  baresdk_account_publish_presence(baresdk_account_handle_t acct,
                                       baresdk_presence_status_t status);
int  baresdk_account_set_100rel(baresdk_account_handle_t acct,
                                 baresdk_100rel_mode_t mode);
int  baresdk_account_subscribe_presence(baresdk_account_handle_t acct,
                                         const char *target_uri);
int  baresdk_account_unsubscribe_presence(baresdk_account_handle_t acct,
                                           const char *target_uri);
```

### Calls

```c
int  baresdk_call_invite(baresdk_account_handle_t acct,
                          const char *uri, baresdk_call_handle_t *out);
int  baresdk_call_answer(baresdk_call_handle_t call);
int  baresdk_call_hangup(baresdk_call_handle_t call);
int  baresdk_call_hold(baresdk_call_handle_t call);
int  baresdk_call_resume(baresdk_call_handle_t call);
int  baresdk_call_send_dtmf(baresdk_call_handle_t call, char digit);
int  baresdk_call_transfer(baresdk_call_handle_t call, const char *uri);
int  baresdk_call_attended_transfer(baresdk_call_handle_t call_a,
                                     baresdk_call_handle_t call_b);
int  baresdk_call_add_header(baresdk_call_handle_t call,
                              const char *name, const char *value);
```

### Audio

```c
/* Mute / unmute */
int  baresdk_audio_mute(baresdk_call_handle_t call, bool mute);     // TX (microphone)
int  baresdk_audio_mute_rx(baresdk_call_handle_t call, bool mute);  // RX (speaker)

/* Device enumeration */
int  baresdk_audio_list_input_devices(baresdk_audio_device_t *devices, int max_count);
int  baresdk_audio_list_output_devices(baresdk_audio_device_t *devices, int max_count);

/* Device selection — takes effect immediately on active calls */
int  baresdk_audio_set_input_device(const char *name);   // NULL = platform default
int  baresdk_audio_set_output_device(const char *name);

/* Gain control — dB, clamped to [-20, +20]; 0.0 = unity (fast-path bypass) */
void baresdk_set_mic_gain_db(float db);      // TX manual gain, safe from any thread
void baresdk_set_speaker_gain_db(float db);  // RX manual gain, safe from any thread

/* Echo cancellation */
void baresdk_set_aec(bool enable);                        // simple on/off
int  baresdk_set_aec_mode(baresdk_aec_mode_t mode);       // AEC_OFF / AEC_SUPPRESSOR / AEC_WEBRTC
void baresdk_set_aec_suppression_level(float level);      // 0=none .. 1=max (SUPPRESSOR only)

/* Other DSP filters */
void baresdk_set_ns(bool enable);   // noise suppression
void baresdk_set_agc(bool enable);  // automatic gain control

/* PCM tap */
int  baresdk_call_set_media_tap(baresdk_call_handle_t call,
                                 baresdk_media_tap_cb_t cb, void *userdata);

/* Audio recording — single mixed WAV file (RX+TX clip-summed) */
int  baresdk_call_record_start(baresdk_call_handle_t call, const char *path);
int  baresdk_call_record_stop(baresdk_call_handle_t call);
```

### Messaging & presence

```c
int  baresdk_message_send(baresdk_account_handle_t acct,
                           const char *to_uri, const char *body,
                           const char *content_type);
```

### Observability

```c
int  baresdk_call_get_stats(baresdk_call_handle_t call,
                             baresdk_ev_media_stats_t *out);
int  baresdk_pcap_start(const char *path);
int  baresdk_pcap_stop(void);
```

`baresdk_ev_media_stats_t` covers: packet counters (TX + RX), loss % (TX + RX), RTT, jitter (TX + RX), jitter buffer depth/load/late/discards, bandwidth (instant + session-avg, TX + RX), MOS-LQ + MOS-CQ (E-model or simplified), codec (name/rate/channels/PT), audio level (dBov), SSRC and remote address.

### Events

All events arrive on a dedicated dispatch thread. The callback is set in `baresdk_config_t.event_cb`.

**Thread-safety note:** Calling baresdk APIs from inside the event callback is safe. The one exception is blocking inside the callback waiting for another event — for complex flows, post work to your own thread.

| Event type | Payload field | Fired when |
|---|---|---|
| `BARESDK_EV_LOG` | `.log` | baresip log message |
| `BARESDK_EV_REG_STATE` | `.reg` | registration state change |
| `BARESDK_EV_INCOMING_CALL` | `.incoming` | INVITE received |
| `BARESDK_EV_CALL_STATE` | `.call_state` | call FSM transition |
| `BARESDK_EV_CALL_DTMF` | `.dtmf` | DTMF digit received |
| `BARESDK_EV_SDP_NEGOTIATION` | `.sdp` | offer/answer complete |
| `BARESDK_EV_SIP_TRACE` | `.sip_trace` | raw SIP message |
| `BARESDK_EV_MEDIA_STATS` | `.stats` | RTCP/MOS stats tick |
| `BARESDK_EV_TRANSFER_REQUEST` | `.transfer_req` | incoming REFER |
| `BARESDK_EV_MWI` | `.mwi` | voicemail NOTIFY |
| `BARESDK_EV_MESSAGE` | `.msg` | SIP MESSAGE received |
| `BARESDK_EV_PRESENCE_STATE` | `.presence` | buddy state changed |

---

## Account config

`uri` + `password` + `transport` is all that's required. Everything else is auto-derived but overridable.

```c
baresdk_account_config_t acct = {
    // ── Required ───────────────────────────────────────────────────────────
    .uri      = "120@pbx.example.com",  // "user@host", "user@host:port", "sip:user@host"
    .password = "secret",

    // ── Transport & server ─────────────────────────────────────────────────
    // Option A — UDP/TCP/TLS
    .transport   = BARESDK_TRANSPORT_TLS,  // UDP(0) TCP(1) TLS(2) WS(3) WSS(4)
    .server_host = "192.168.1.1",          // NULL = host from uri
    .server_port = 5061,                   // 0 = transport default

    // Option B — WebSocket / WSS (overrides transport/server_host/server_port)
    .server_url  = "wss://pbx.example.com:443/ws",

    // ── Identity ────────────────────────────────────────────────────────────
    .auth_user    = "120",       // NULL = user part of uri
    .display_name = "Alice",     // NULL = omit

    // ── Media / NAT ─────────────────────────────────────────────────────────
    .media_enc   = BARESDK_MEDIA_ENC_DTLS_SRTP,  // NONE / SDES / DTLS_SRTP
    .ice_enabled = true,
    .stun_server = "stun:stun.l.google.com:19302",
    .turn_server = "turn:turn.example.com:3478",  // TURN takes priority over STUN
    .turn_user   = "turnuser",
    .turn_pass   = "turnpass",

    // ── Audio codecs (per-account override) ────────────────────────────────
    // String names — most flexible, aliases accepted:
    //   "opus"  "ulaw"/"pcmu"  "alaw"/"pcma"  "g722"  "g729"  "g726"
    // Leave audio_codec_name_count = 0 to use the global cfg.audio_codecs list.
    .audio_codec_names      = {"ulaw", "alaw", "opus"},
    .audio_codec_name_count = 3,

    // ── Advanced ────────────────────────────────────────────────────────────
    .verify_tls  = true,     // false = skip TLS cert check (testing only)
};
```

### Common patterns

```c
// Minimum — UDP
baresdk_account_config_t acct = { .uri = "100@192.168.1.1", .password = "pass" };

// SIP domain differs from physical server
baresdk_account_config_t acct = {
    .uri = "100@company.com", .password = "pass",
    .server_host = "192.168.1.1", .server_port = 5060,
};

// WSS via reverse proxy
baresdk_account_config_t acct = {
    .uri        = "120@pbx.example.com",
    .password   = "secret",
    .server_url = "wss://pbx.example.com:443/ws",
    .verify_tls = false,
};

// WSS + DTLS-SRTP + ICE (WebRTC-compatible)
baresdk_account_config_t acct = {
    .uri         = "120@pbx.example.com",
    .password    = "secret",
    .server_url  = "wss://pbx.example.com:443/ws",
    .media_enc   = BARESDK_MEDIA_ENC_DTLS_SRTP,
    .ice_enabled = true,
    .stun_server = "stun:stun.l.google.com:19302",
};
```

### Codec selection

Pass codec names as strings — aliases are resolved automatically:

| Name | Resolves to |
|---|---|
| `"opus"` | Opus 48 kHz stereo |
| `"ulaw"` / `"pcmu"` / `"g711u"` | G.711 µ-law |
| `"alaw"` / `"pcma"` / `"g711a"` | G.711 A-law |
| `"g722"` | G.722 wideband |
| `"g729"` | G.729 |
| `"g726"` / `"g726-32"` | G.726 32 kbps |

**C:**
```c
strcpy(cfg.audio_codec_names[0], "ulaw");
strcpy(cfg.audio_codec_names[1], "alaw");
strcpy(cfg.audio_codec_names[2], "opus");
cfg.audio_codec_name_count = 3;
```

**Python:**
```python
account = create_account(sdk, "alice@pbx.example.com", "secret",
                         audio_codecs=["ulaw", "alaw", "opus"])
```

**Flutter:**
```dart
sdk.createAccount("alice@pbx.example.com", "secret",
                  audioCodecs: ["ulaw", "alaw", "opus"]);
```

**C++:**
```cpp
sdk.create_account("alice@pbx.example.com", "secret",
                   BARESDK_TRANSPORT_UDP,
                   {BARESDK_CODEC_PCMU, BARESDK_CODEC_PCMA, BARESDK_CODEC_OPUS});
```

Priority: per-account string names → per-account enum list → global `cfg.audio_codecs`.

Per-account fields override the global defaults from `baresdk_config_t` when set.

---

## Global config (selected fields)

```c
baresdk_config_t cfg;
baresdk_config_init(&cfg);   // always call first

// TLS
cfg.ca_cert_path  = "/etc/ssl/certs/ca-certificates.crt";
cfg.verify_server = true;

// NAT defaults (overridable per-account)
cfg.ice_enabled = true;
cfg.stun_server = "stun:stun.example.com";

// Media defaults — global codec list (overridable per-account)
cfg.media_enc         = BARESDK_MEDIA_ENC_DTLS_SRTP;
cfg.audio_codecs[0]   = BARESDK_CODEC_OPUS;
cfg.audio_codecs[1]   = BARESDK_CODEC_PCMU;
cfg.audio_codec_count = 2;
cfg.aec = cfg.ns = cfg.agc = true;

// Observability
cfg.trace_sip         = true;
cfg.stats_interval_ms = 5000;
cfg.pcap_path         = "/tmp/capture.pcap";

// Registration retry (exponential backoff)
cfg.reg_retry_initial_ms = 2000;
cfg.reg_retry_max_ms     = 300000;
cfg.reg_retry_backoff    = 2.0f;
```

---

## Consuming baresdk

### Shared library (recommended — no extra link flags needed)

```bash
# Linux
gcc main.c dist/linux/x86_64/baresdk.so -I dist/linux/x86_64/include -o app

# macOS
clang main.c dist/macos/universal/baresdk.dylib -I dist/macos/universal/include -o app

# Windows
cl main.c dist\windows\x64\baresdk.dll /I dist\windows\x64\include
```

The shared library has OpenSSL, zlib, and pthreads baked in — no `-lssl`, `-lcrypto`, or `-lpthread` needed on the link line.

### Static archive

```bash
gcc main.c dist/linux/x86_64/baresdk.a \
    -I dist/linux/x86_64/include \
    -lpthread -lssl -lcrypto -lz -lm -ldl -lresolv -o app
```

### CMake

```cmake
add_executable(my_app main.c)
target_include_directories(my_app PRIVATE dist/linux/x86_64/include)
# Shared lib — no extra link flags:
target_link_libraries(my_app dist/linux/x86_64/baresdk.so)
# Static archive — add platform libs:
# target_link_libraries(my_app dist/linux/x86_64/baresdk.a pthread ssl crypto m dl resolv)
```

### Python

```bash
bash bindings/python/build.sh
```

Builds the SDK, regenerates the cffi header, copies the `.so` into the package directory, and installs the package. No `LD_LIBRARY_PATH` needed. Set it to override the bundled copy, e.g. to test a different build:

```bash
export LD_LIBRARY_PATH=/path/to/other/build:$LD_LIBRARY_PATH
```

Then:

```python
from baresdk import SDK, create_account, register, dial, hangup, answer

with SDK(log_level=1) as sdk:
    account = create_account(sdk, "alice@pbx.example.com", "secret",
                             transport="tls", server_host="pbx.example.com")
    register(account)

    for ev in account.events(timeout=30):
        if ev.type == "reg_state" and ev.state == "registered":
            print("Registered!")
            call = dial(account, "bob@pbx.example.com")

        elif ev.type == "incoming_call":
            answer(ev.call)

        elif ev.type == "call_state" and ev.state in ("ended", "failed", "cancelled"):
            break

    account.destroy()
```

### C++ (header-only wrapper)

```bash
bash bindings/cpp/build.sh
```

```cpp
#include "baresdk.hpp"

baresdk::SDK sdk;
sdk.config().transport    = BARESDK_TRANSPORT_TLS;
sdk.config().server_host  = "pbx.example.com";
sdk.config().log_level    = 1;

sdk.on_event([](const baresdk_event_t& ev) { /* handle events */ });

auto acct = sdk.create_account("alice@pbx.example.com", "secret", BARESDK_TRANSPORT_TLS);
acct.register_account();
```

### Flutter (dart:ffi)

```yaml
# pubspec.yaml
dependencies:
  baresdk:
    path: path/to/baresdk/bindings/flutter
```

```bash
dart run ffigen --config ffigen.yaml   # only needed when baresdk.h changes
```

### Swift Package (iOS)

```swift
// Package.swift
.binaryTarget(name: "baresdk", path: "dist/ios/baresdk.xcframework")
```

### Kotlin Multiplatform (cinterop)

```def
# baresdk.def
headers = baresdk.h
staticLibraries = baresdk.a
libraryPaths = dist/android/arm64-v8a
includePaths = dist/android/arm64-v8a/include
```

### Go (cgo)

```go
// #cgo CFLAGS: -I/path/to/dist/linux/x86_64/include
// #cgo LDFLAGS: /path/to/dist/linux/x86_64/baresdk.so
// #include <baresdk.h>
import "C"
```

---

## Link flags (static archive only)

When linking against the **shared library** no extra flags are needed.
When linking against the **static archive**:

| Platform | Additional flags |
|---|---|
| Linux | `-lpthread -lssl -lcrypto -lz -lm -ldl -lresolv` |
| Android | `-llog -lOpenSLES -landroid` (mbedTLS bundled — no OpenSSL) |
| macOS | `-framework CoreFoundation -framework Security` |
| iOS | `-framework AudioToolbox -framework AVFoundation -framework Foundation -framework CoreMedia -framework Security` |
| Windows | `ws2_32.lib iphlpapi.lib crypt32.lib` + vcpkg `openssl` |

---

## Build requirements

| Platform | Requirements |
|---|---|
| Linux | `cmake ≥ 3.19`, `ninja`, `gcc`, `libssl-dev` |
| Android | Android NDK (`ANDROID_NDK` set), `cmake ≥ 3.19`, `ninja` |
| iOS / macOS | Xcode + CLT, `cmake ≥ 3.19`; macOS adds `brew install openssl@3` |
| Windows | VS 2022, vcpkg (`VCPKG_ROOT` set), `vcpkg install openssl:x64-windows-static-md` |

## CMake options

| Option | Default | Description |
|---|---|---|
| `BARESDK_TLS` | `openssl` | TLS backend: `openssl` or `mbedtls` |
| `BARESDK_MODULES_PROFILE` | `desktop` | `desktop` or `mobile` |
| `BARESDK_RE_SOURCE_DIR` | `third_party/re` | libre source path |
| `BARESDK_BARESIP_SOURCE_DIR` | `third_party/baresip` | baresip source path |
| `BARESDK_MBEDTLS_SOURCE_DIR` | `third_party/mbedtls` | mbedTLS source (mobile) |
| `BARESDK_DIST_DIR` | `dist/` | output root |

---

## Repository layout

```
baresdk/
├── include/
│   └── baresdk.h               # sole public header
├── src/
│   ├── core.c                  # singleton lifecycle
│   ├── dispatch.c              # consumer-thread → re_main bridge
│   ├── event.c                 # bevent → baresdk_event_t queue
│   ├── account.c               # multi-account registration + retry
│   ├── call.c                  # INVITE FSM, hold, DTMF, transfer
│   ├── audio.c                 # mute, device selection
│   ├── media_tap.c             # PCM tap via aufilt
│   ├── record.c                # per-call WAV audio recording
│   ├── transfer.c              # blind + attended transfer
│   ├── message.c               # SIP MESSAGE send/receive
│   ├── presence.c              # PUBLISH, BLF, MWI, 100rel
│   ├── sdp.c                   # SDP offer/answer capture
│   ├── stats.c                 # RTCP polling + MOS (E-model + simplified)
│   ├── trace.c                 # SIP trace hook
│   ├── pcap.c                  # pcap file writer
│   ├── transport.c             # URL parsing, outbound builder
│   └── modules_init.c          # static module loading
├── bindings/
│   ├── cpp/baresdk.hpp         # header-only C++17 RAII wrapper
│   ├── python/                 # cffi-based Python package
│   └── flutter/                # dart:ffi + ffigen Flutter package
├── platform/
│   ├── linux/audio_linux.c
│   ├── android/audio_android.c
│   ├── ios/audio_ios.m
│   ├── macos/audio_macos.c
│   └── windows/audio_windows.c
├── test/
│   ├── unit/                   # unit tests (no PBX required)
│   ├── cli_harness.c           # register + call integration test
│   ├── thread_safety_test.c    # TSan hammer
│   └── pbx_compat.c            # 3CX · FreeSWITCH · Kamailio
├── docs/
│   ├── index.md
│   └── guides/complete_example.md   # SDK usage guide (all languages)
├── scripts/
│   ├── build-linux.sh
│   ├── build-android.sh
│   ├── build-ios.sh
│   ├── build-macos.sh
│   └── build-windows.ps1
└── third_party/
    ├── re/                     # libre (git submodule)
    ├── baresip/                # baresip (git submodule)
    └── mbedtls/                # mobile TLS (git submodule)
```
