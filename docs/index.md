# VoxSDK

A general-purpose SIP client SDK built on [baresip](https://github.com/baresip/baresip)
and [libre](https://github.com/baresip/re). It registers against any
standards-compliant SIP server, ships no UI, and imposes no call flow —
everything app-specific is configuration you pass in.

---

## What it does

| Capability | Details |
|---|---|
| **SIP** | INVITE, BYE, REGISTER, MESSAGE, SUBSCRIBE/NOTIFY, REFER (blind + attended, plus accept/reject of an incoming REFER), PRACK/100rel |
| **WebRTC media** | DTLS-SRTP, SDES, ICE, STUN, TURN, RTCP-mux |
| **Transports** | UDP, TCP, TLS, WebSocket, WSS |
| **Audio** | Opus, G.711 (PCMU/PCMA), G.722, G.726; AEC, NS, AGC; PCM tap, WAV recording, device hot-switch, app-owned audio device |
| **Mobility** | Wi-Fi ↔ 4G/5G handover with media verification, `RECONNECTING` registration state, RFC 8599 push |
| **Degraded links** | Media-stall alerts, RTP timeout, OPTIONS keepalive, SRV failover, adaptive Opus bitrate |
| **Observability** | RTCP stats, MOS (E-model + simplified, per direction), quality alerts, SIP trace, pcap, SDP diff |
| **Messaging** | SIP MESSAGE (in/out), MWI, presence (PUBLISH/SUBSCRIBE, BLF) |
| **Multi-account** | Any number of accounts per stack; every API is thread-safe |

---

## Platform support

| Platform | Architecture | TLS backend | Shared lib output |
|---|---|---|---|
| Linux | x86_64 | OpenSSL (embedded) | `dist/linux/x86_64/voxsdk.so` |
| macOS | x86_64 + arm64 (universal) | OpenSSL (embedded) | `dist/macos/universal/voxsdk.dylib` |
| Windows | x64 | OpenSSL (embedded via vcpkg) | `dist\windows\x64\voxsdk.dll` |
| Android | arm64-v8a, armeabi-v7a, x86_64 | mbedTLS (embedded) | `dist/android/<ABI>/voxsdk.so` |
| iOS | device + simulator | mbedTLS (embedded) | `dist/ios/VoxSDK.xcframework` |

Each platform build script produces both the static archive (`voxsdk.a` / `vox.lib`) and the shared library in one step. Shared libraries are **fully self-contained** — consumers need no extra packages at runtime.

---

## Language support

| Language | Binding | Location |
|---|---|---|
| C / C++ | Header-only RAII wrapper | `bindings/cpp/voxsdk.hpp` |
| Python | cffi | `bindings/python/` |
| Flutter / Dart | dart:ffi + ffigen | `bindings/flutter/` |

---

## Quick start by language

- [C / C++](quickstart/c.md)
- [Python](quickstart/python.md)
- [Flutter / Dart](quickstart/flutter.md)

---

## SDK usage guide

**[How to use the SDK — all languages (C/C++, Python, Flutter)](guides/complete_example.md)**

Covers every operation in one place: account setup, transport, STUN/TURN/ICE,
calls, hold, mute, transfer, stats, audio devices, and more — with code for
all three languages side by side.

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
- [Network handover (Wi-Fi ↔ 4G/5G)](guides/network_handover.md)
- [Degraded links (bad signal, slow uplink, dead paths)](guides/degraded_links.md)
- [TLS and WSS setup](guides/tls_wss.md)
- [Multiple accounts](guides/multi_account.md)
- [WebRTC browser interop](guides/webrtc_browser.md)
- [Debugging SIP](guides/debugging.md)
