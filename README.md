# libbare

A thread-safe C SDK for VoIP — SIP/RTP built on [baresip](https://github.com/baresip/baresip) and [libre](https://github.com/baresip/re), delivered as a single static archive per platform.

`libbare.h` is the only header you need. The baresip/libre internals are an implementation detail.

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
| Linux x86_64 | `dist/linux/x86_64/libbare.a` | OpenSSL (system) |
| Android arm64-v8a | `dist/android/arm64-v8a/libbare.a` | mbedTLS (bundled) |
| Android armeabi-v7a | `dist/android/armeabi-v7a/libbare.a` | mbedTLS (bundled) |
| Android x86_64 | `dist/android/x86_64/libbare.a` | mbedTLS (bundled) |
| iOS device | `dist/ios/libbare.xcframework` | mbedTLS (bundled) |
| iOS simulator | (included in xcframework) | mbedTLS (bundled) |
| macOS universal | `dist/macos/universal/libbare.a` | OpenSSL (system) |
| Windows x64 | `dist/windows/x64/bare.lib` | OpenSSL (vcpkg) |

---

## Quick start

```bash
git clone --recurse-submodules https://github.com/your-org/libbare
cd libbare

./scripts/build-linux.sh
./tools/verify.sh dist/linux/x86_64/libbare.a link
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
void libbare_config_init(libbare_config_t *cfg);   // zero-fill + set defaults
int  libbare_init(const libbare_config_t *cfg);    // start stack (once per process)
void libbare_shutdown(void);                        // graceful teardown
const char *libbare_version(void);
```

### Accounts

```c
int  libbare_account_create(const libbare_account_config_t *cfg,
                             libbare_account_handle_t *out);
void libbare_account_destroy(libbare_account_handle_t acct);
int  libbare_account_register(libbare_account_handle_t acct);
int  libbare_account_unregister(libbare_account_handle_t acct);
int  libbare_account_add_header(libbare_account_handle_t acct,
                                 const char *name, const char *value);
int  libbare_account_publish_presence(libbare_account_handle_t acct,
                                       libbare_presence_status_t status);
int  libbare_account_set_100rel(libbare_account_handle_t acct,
                                 libbare_100rel_mode_t mode);
int  libbare_account_subscribe_presence(libbare_account_handle_t acct,
                                         const char *target_uri);
int  libbare_account_unsubscribe_presence(libbare_account_handle_t acct,
                                           const char *target_uri);
```

### Calls

```c
int  libbare_call_invite(libbare_account_handle_t acct,
                          const char *uri, libbare_call_handle_t *out);
int  libbare_call_answer(libbare_call_handle_t call);
int  libbare_call_hangup(libbare_call_handle_t call);
int  libbare_call_hold(libbare_call_handle_t call);
int  libbare_call_resume(libbare_call_handle_t call);
int  libbare_call_send_dtmf(libbare_call_handle_t call, char digit);
int  libbare_call_transfer(libbare_call_handle_t call, const char *uri);
int  libbare_call_attended_transfer(libbare_call_handle_t call_a,
                                     libbare_call_handle_t call_b);
int  libbare_call_add_header(libbare_call_handle_t call,
                              const char *name, const char *value);
```

### Audio

```c
int  libbare_audio_mute(libbare_call_handle_t call, bool mute);
int  libbare_audio_set_input_device(const char *name);
int  libbare_audio_set_output_device(const char *name);
int  libbare_call_set_media_tap(libbare_call_handle_t call,
                                 libbare_media_tap_cb_t cb, void *userdata);
```

### Messaging & presence

```c
int  libbare_message_send(libbare_account_handle_t acct,
                           const char *to_uri, const char *body,
                           const char *content_type);
```

### Observability

```c
int  libbare_call_get_stats(libbare_call_handle_t call,
                             libbare_ev_media_stats_t *out);
int  libbare_pcap_start(const char *path);
int  libbare_pcap_stop(void);
```

### Events

All events arrive on a dedicated dispatch thread (never on `re_main`). The callback is set in `libbare_config_t.event_cb`.

**Thread-safety note:** Calling most libbare APIs from inside the event callback is safe — the dispatch thread is deliberately separate from `re_main` to allow this. The one exception is calling a blocking or long-running operation that itself waits for another event (e.g., synchronously waiting for a call to end inside `LIBBARE_EV_INCOMING_CALL`). For complex flows, post work to your own thread and call libbare APIs from there.

| Event type | Payload field | Fired when |
|---|---|---|
| `LIBBARE_EV_LOG` | `.log` | baresip log message |
| `LIBBARE_EV_REG_STATE` | `.reg` | registration state change |
| `LIBBARE_EV_INCOMING_CALL` | `.incoming` | INVITE received |
| `LIBBARE_EV_CALL_STATE` | `.call_state` | call FSM transition |
| `LIBBARE_EV_CALL_DTMF` | `.dtmf` | DTMF digit received |
| `LIBBARE_EV_SDP_NEGOTIATION` | `.sdp` | offer/answer complete |
| `LIBBARE_EV_SIP_TRACE` | `.sip_trace` | raw SIP message |
| `LIBBARE_EV_MEDIA_STATS` | `.stats` | RTCP/MOS stats tick |
| `LIBBARE_EV_TRANSFER_REQUEST` | `.transfer_req` | incoming REFER |
| `LIBBARE_EV_MWI` | `.mwi` | voicemail NOTIFY |
| `LIBBARE_EV_MESSAGE` | `.msg` | SIP MESSAGE received |
| `LIBBARE_EV_PRESENCE_STATE` | `.presence` | buddy state changed |

---

## Config reference (selected fields)

```c
libbare_config_t cfg;
libbare_config_init(&cfg);          // always call this first

// All string fields are deep-copied by libbare_init().
// You can free/overwrite your strings immediately after calling libbare_init().

// Transport — use server_url OR server_host, not both.
// server_url takes precedence if both are set.
// Use server_url for WS/WSS (it encodes scheme + path).
// Use server_host + transport for plain UDP/TCP/TLS.
cfg.server_url  = "wss://pbx:8089/ws";  // full URL — required for WS/WSS
cfg.server_host = "pbx.example.com";    // simple form (UDP/TCP/TLS)
cfg.transport   = LIBBARE_TRANSPORT_TLS;

// TLS
cfg.ca_cert_path    = "/etc/ssl/certs/ca-certificates.crt";
cfg.verify_server   = true;

// NAT
cfg.ice_enabled = true;
cfg.stun_server = "stun:stun.example.com";

// Media
cfg.media_enc          = LIBBARE_MEDIA_ENC_DTLS_SRTP;
cfg.audio_codecs[0]    = LIBBARE_CODEC_OPUS;
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

libbare speaks SIP-over-WebSocket ([RFC 7118](https://tools.ietf.org/html/rfc7118)) — the same protocol used by browser clients like SIP.js and JsSIP.

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

## Consuming libbare

### CMake

```cmake
add_executable(my_app main.c)
target_include_directories(my_app PRIVATE
    path/to/dist/linux/x86_64/include)
target_link_libraries(my_app
    path/to/dist/linux/x86_64/libbare.a
    pthread ssl crypto m dl resolv)
```

### Flutter (dart:ffi)

```yaml
# ffigen.yaml
name: LibBareBindings
output: lib/src/libbare_bindings.dart
headers:
  entry-points:
    - dist/linux/x86_64/include/libbare.h
```

### Kotlin Multiplatform (cinterop)

```def
# libbare.def
headers = libbare.h
staticLibraries = libbare.a
libraryPaths = dist/android/arm64-v8a
includePaths = dist/android/arm64-v8a/include
```

### Swift Package (iOS)

```swift
// Package.swift
.binaryTarget(name: "libbare",
              path: "dist/ios/libbare.xcframework")
```

### Rust (bindgen)

```rust
// build.rs
bindgen::Builder::default()
    .header("dist/linux/x86_64/include/libbare.h")
    .generate().unwrap()
    .write_to_file("src/bindings.rs").unwrap();
```

### Python (cffi)

libbare is a static archive, so cffi cannot `dlopen` it directly. First build a thin shared wrapper:

```bash
gcc -shared -fPIC -o libbare_shared.so \
    -Wl,--whole-archive dist/linux/x86_64/libbare.a -Wl,--no-whole-archive \
    -lpthread -lssl -lcrypto -lm -ldl -lresolv
```

Then load the shared wrapper from Python:

```python
from cffi import FFI
ffi = FFI()
ffi.cdef(open("dist/linux/x86_64/include/libbare.h").read())
lib = ffi.dlopen("./libbare_shared.so")
```

### Go (cgo)

```go
// #cgo CFLAGS: -I/path/to/dist/linux/x86_64/include
// #cgo LDFLAGS: /path/to/dist/linux/x86_64/libbare.a -lpthread -lssl -lcrypto -lm -ldl
// #include <libbare.h>
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
| `LIBBARE_TLS` | `openssl` | TLS backend: `openssl` or `mbedtls` |
| `LIBBARE_MODULES_PROFILE` | `desktop` | `desktop` or `mobile` |
| `LIBBARE_RE_SOURCE_DIR` | `third_party/re` | libre source path |
| `LIBBARE_BARESIP_SOURCE_DIR` | `third_party/baresip` | baresip source path |
| `LIBBARE_MBEDTLS_SOURCE_DIR` | `third_party/mbedtls` | mbedTLS source (mobile) |
| `LIBBARE_DIST_DIR` | `dist/` | output root |

---

## Repository layout

```
libbare/
├── include/
│   └── libbare.h               # sole public header
├── src/
│   ├── core.c                  # singleton lifecycle
│   ├── dispatch.c              # consumer-thread → re_main bridge
│   ├── event.c                 # bevent → libbare_event_t queue
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

