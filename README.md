# baresdk

A thread-safe C SDK for VoIP — SIP/RTP built on [baresip](https://github.com/baresip/baresip) and [libre](https://github.com/baresip/re), delivered as a single static archive per platform.

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
| **Custom Headers** | Per-account + per-dialog (call) custom SIP headers |
| **Media** | PCM tap (TX/RX) · mute · device select |
| **Observability** | SIP trace · SDP diff · pcap (UDP + TCP framing) · RTCP/MOS stats (E-model + simplified) |
| **Multi-account** | Yes — any number of accounts per stack |
| **Thread safety** | Full — call any API from any thread |
| **Memory safety** | Deep-copied config strings — caller can free immediately after init |

## Supported platforms

| Platform | Output | TLS backend |
|---|---|---|
| Linux x86_64 | `dist/linux/x86_64/baresdk.a` | OpenSSL (system) |
| Android arm64-v8a | `dist/android/arm64-v8a/baresdk.a` | mbedTLS (bundled) |
| Android armeabi-v7a | `dist/android/armeabi-v7a/baresdk.a` | mbedTLS (bundled) |
| Android x86_64 | `dist/android/x86_64/baresdk.a` | mbedTLS (bundled) |
| iOS device | `dist/ios/baresdk.xcframework` | mbedTLS (bundled) |
| iOS simulator | (included in xcframework) | mbedTLS (bundled) |
| macOS universal | `dist/macos/universal/baresdk.a` | OpenSSL (system) |
| Windows x64 | `dist/windows/x64/bare.lib` | OpenSSL (vcpkg) |

---

## Quick start

```bash
git clone --recurse-submodules https://github.com/your-org/baresdk
cd baresdk

./scripts/build-linux.sh
./tools/verify.sh dist/linux/x86_64/baresdk.a link
```

If you cloned without `--recurse-submodules`:

```bash
git submodule update --init --recursive third_party/re third_party/baresip
# mobile only:
git submodule update --init --recursive third_party/mbedtls
```

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
void baresdk_account_destroy(baresdk_account_handle_t acct);
int  baresdk_account_register(baresdk_account_handle_t acct);
int  baresdk_account_unregister(baresdk_account_handle_t acct);
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
int  baresdk_audio_mute(baresdk_call_handle_t call, bool mute);
int  baresdk_audio_set_input_device(const char *name);
int  baresdk_audio_set_output_device(const char *name);
int  baresdk_call_set_media_tap(baresdk_call_handle_t call,
                                 baresdk_media_tap_cb_t cb, void *userdata);
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

### Events

All events arrive on a dedicated dispatch thread (never on `re_main`). The callback is set in `baresdk_config_t.event_cb`.

**Thread-safety note:** Calling most baresdk APIs from inside the event callback is safe — the dispatch thread is deliberately separate from `re_main` to allow this. The one exception is calling a blocking or long-running operation that itself waits for another event (e.g., synchronously waiting for a call to end inside `BARESDK_EV_INCOMING_CALL`). For complex flows, post work to your own thread and call baresdk APIs from there.

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

## Config reference (selected fields)

```c
baresdk_config_t cfg;
baresdk_config_init(&cfg);          // always call this first

// All string fields are deep-copied by baresdk_init().
// You can free/overwrite your strings immediately after calling baresdk_init().

// Transport — use server_url OR server_host, not both.
// server_url takes precedence if both are set.
// Use server_url for WS/WSS (it encodes scheme + path).
// Use server_host + transport for plain UDP/TCP/TLS.
cfg.server_url  = "wss://pbx:8089/ws";  // full URL — required for WS/WSS
cfg.server_host = "pbx.example.com";    // simple form (UDP/TCP/TLS)
cfg.transport   = BARESDK_TRANSPORT_TLS;

// TLS
cfg.ca_cert_path    = "/etc/ssl/certs/ca-certificates.crt";
cfg.verify_server   = true;

// NAT
cfg.ice_enabled = true;
cfg.stun_server = "stun:stun.example.com";

// Media
cfg.media_enc          = BARESDK_MEDIA_ENC_DTLS_SRTP;
cfg.audio_codecs[0]    = BARESDK_CODEC_OPUS;
cfg.audio_codec_count  = 1;

// Observability
cfg.trace_sip        = true;
cfg.stats_interval_ms = 5000;
cfg.pcap_path        = "/tmp/capture.pcap";

// Registration retry (exponential backoff)
cfg.reg_retry_initial_ms  = 2000;
cfg.reg_retry_max_ms      = 300000;
cfg.reg_retry_backoff     = 2.0f;
```

---

## WebSocket connectivity

baresdk speaks SIP-over-WebSocket ([RFC 7118](https://tools.ietf.org/html/rfc7118)) — the same protocol used by browser clients like SIP.js and JsSIP.

### Direct WSS (SIP.js-style)

Point `server_url` at the PBX WebSocket endpoint:

```c
cfg.server_url = "wss://pbx.example.com:8089/ws";   // FreeSWITCH default
cfg.server_url = "wss://pbx.example.com:5443/ws";   // Kamailio / OpenSIPS
cfg.server_url = "wss://pbx.example.com:443/ws";    // behind nginx on 443
```

The scheme, port, and path are all parsed from the URL. No extra fields are needed.

### nginx as a TLS-terminating reverse proxy

nginx terminates TLS and forwards WebSocket traffic to the backend SIP server over plain `ws://`:

```nginx
server {
    listen 443 ssl;
    server_name pbx.example.com;

    location /ws {
        proxy_pass http://127.0.0.1:8188;    # FreeSWITCH / Asterisk / Kamailio
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
    }
}
```

Your app still points at the nginx address. If the nginx TLS certificate CN differs from your SIP domain, set `sni_hostname`:

```c
cfg.server_url   = "wss://pbx.example.com/ws";
cfg.sni_hostname = "proxy.example.com";   // cert name, if different from SIP domain
```

Use `ws_extra_headers` to inject any headers nginx requires (e.g. `X-Real-IP`, auth tokens):

```c
const char *extra[] = { "X-Custom-Header: value", NULL };
cfg.ws_extra_headers = extra;
```

---

## Consuming baresdk

### CMake

```cmake
add_executable(my_app main.c)
target_include_directories(my_app PRIVATE
    path/to/dist/linux/x86_64/include)
target_link_libraries(my_app
    path/to/dist/linux/x86_64/baresdk.a
    pthread ssl crypto m dl resolv)
```

### Flutter (dart:ffi)

```yaml
# ffigen.yaml
name: LibBareBindings
output: lib/src/baresdk_bindings.dart
headers:
  entry-points:
    - dist/linux/x86_64/include/baresdk.h
```

### Kotlin Multiplatform (cinterop)

```def
# baresdk.def
headers = baresdk.h
staticLibraries = baresdk.a
libraryPaths = dist/android/arm64-v8a
includePaths = dist/android/arm64-v8a/include
```

### Swift Package (iOS)

```swift
// Package.swift
.binaryTarget(name: "baresdk",
              path: "dist/ios/baresdk.xcframework")
```

### Rust (bindgen)

```rust
// build.rs
bindgen::Builder::default()
    .header("dist/linux/x86_64/include/baresdk.h")
    .generate().unwrap()
    .write_to_file("src/bindings.rs").unwrap();
```

### Python (cffi)

baresdk is a static archive, so cffi cannot `dlopen` it directly. First build a thin shared wrapper:

```bash
gcc -shared -fPIC -o baresdk_shared.so \
    -Wl,--whole-archive dist/linux/x86_64/baresdk.a -Wl,--no-whole-archive \
    -lpthread -lssl -lcrypto -lm -ldl -lresolv
```

Then load the shared wrapper from Python:

```python
from cffi import FFI
ffi = FFI()
ffi.cdef(open("dist/linux/x86_64/include/baresdk.h").read())
lib = ffi.dlopen("./baresdk_shared.so")
```

### Go (cgo)

```go
// #cgo CFLAGS: -I/path/to/dist/linux/x86_64/include
// #cgo LDFLAGS: /path/to/dist/linux/x86_64/baresdk.a -lpthread -lssl -lcrypto -lm -ldl
// #include <baresdk.h>
import "C"
```

---

## Link flags

| Platform | Flags |
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
│   ├── transfer.c              # blind + attended transfer
│   ├── message.c               # SIP MESSAGE send/receive
│   ├── presence.c              # PUBLISH, BLF, MWI, 100rel
│   ├── sdp.c                   # SDP offer/answer capture
│   ├── stats.c                 # RTCP polling + MOS (E-model + simplified)
│   ├── trace.c                 # SIP trace hook
│   ├── pcap.c                  # pcap file writer (UDP + TCP framing)
│   ├── timers.c                # config → baresip config mapping
│   ├── transport.c             # URL parsing, outbound builder
│   ├── modules_init.c          # static module loading
│   ├── log.c                   # baresip log → event queue
│   ├── re_loop.c               # re_main thread
│   ├── dns.c                   # RFC 3263 NAPTR→SRV resolution
│   └── headers.c               # per-account + per-dialog custom headers
├── platform/
│   ├── linux/audio_linux.c     # platform audio stub
│   ├── android/audio_android.c
│   ├── ios/audio_ios.m         # AVAudioSession (PlayAndRecord + VoiceChat)
│   ├── macos/audio_macos.c
│   └── windows/audio_windows.c
├── test/
│   ├── unit/                      # unit tests (no PBX required)
│   │   ├── test_mos.c             # MOS calculation tests
│   │   ├── test_url_parser.c      # transport URL parsing tests
│   │   ├── test_mwi_parser.c      # MWI body parser tests
│   │   ├── test_codec_str.c       # codec list builder tests
│   │   └── test_transport_str.c   # transport/media-enc string tests
│   ├── cli_harness.c           # register + call integration test
│   ├── thread_safety_test.c    # N-thread TSan hammer
│   └── pbx_compat.c            # 3CX · FreeSWITCH · Kamailio scenarios
├── cmake/
│   ├── MergeStaticLibs.cmake
│   ├── modules-desktop.cmake
│   └── modules-mobile.cmake
├── scripts/
│   ├── build-linux.sh
│   ├── build-android.sh
│   ├── build-ios.sh
│   ├── build-macos.sh
│   └── build-windows.ps1
├── tools/
│   └── verify.sh               # symbol + linkability checks
└── third_party/
    ├── re/                     # libre + librem (git submodule)
    ├── baresip/                # baresip (git submodule)
    └── mbedtls/                # mobile TLS (git submodule)
```

