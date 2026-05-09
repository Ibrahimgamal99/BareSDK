# baresdk

A production-ready SIP/WebRTC/NAT SDK built on [baresip](https://github.com/baresip/baresip) and [libre](https://github.com/baresip/re).

---

## What it does

| Capability | Details |
|---|---|
| **SIP** | INVITE, BYE, REGISTER, MESSAGE, SUBSCRIBE/NOTIFY, REFER (blind + attended) |
| **WebRTC media** | DTLS-SRTP, ICE, STUN, TURN |
| **Transports** | UDP, TCP, TLS, WebSocket, WSS |
| **Audio** | Opus, G.711 (PCMU/PCMA), G.722; AEC, NS, AGC |
| **Observability** | RTCP stats, MOS (E-model + simplified), SIP trace, pcap, SDP diff |
| **Messaging** | SIP MESSAGE (in/out), MWI, presence (PUBLISH/SUBSCRIBE) |

---

## Platform support

| Platform | Architecture | TLS backend | Shared lib output |
|---|---|---|---|
| Linux | x86_64 | OpenSSL (embedded) | `dist/linux/x86_64/baresdk.so` |
| macOS | x86_64 + arm64 (universal) | OpenSSL (embedded) | `dist/macos/universal/baresdk.dylib` |
| Windows | x64 | OpenSSL (embedded via vcpkg) | `dist\windows\x64\baresdk.dll` |
| Android | arm64-v8a, armeabi-v7a, x86_64 | mbedTLS (embedded) | `dist/android/<ABI>/baresdk.so` |
| iOS | device + simulator | mbedTLS (embedded) | `dist/ios/baresdk.xcframework` |

Each platform build script produces both the static archive (`baresdk.a` / `bare.lib`) and the shared library in one step. Shared libraries are **fully self-contained** — consumers need no extra packages at runtime.

---

## Language support

| Language | Binding | Location |
|---|---|---|
| C / C++ | Header-only RAII wrapper | `bindings/cpp/baresdk.hpp` |
| Python | cffi | `bindings/python/` |
| Flutter / Dart | dart:ffi + ffigen | `bindings/flutter/` |

---

## Quick start by language

- [C / C++](quickstart/c.md)
- [Python](quickstart/python.md)
- [Flutter / Dart](quickstart/flutter.md)

---

## API reference

- [Overview & architecture](api/overview.md)
- [Configuration reference](api/config.md)
- [Accounts & registration](api/accounts.md)
- [Calls](api/calls.md)
- [Media & audio](api/media.md)
- [Observability](api/observability.md)
- [Events reference](api/events.md)

---

## Guides

- [NAT traversal (ICE / STUN / TURN)](guides/nat_traversal.md)
- [TLS and WSS setup](guides/tls_wss.md)
- [Multiple accounts](guides/multi_account.md)
- [WebRTC browser interop](guides/webrtc_browser.md)
- [Debugging SIP](guides/debugging.md)
