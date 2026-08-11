# baresdk

Building VoIP from scratch means wrestling with SIP parsing, RTP jitter buffers, NAT traversal, and SRTP key exchange. baresdk handles all of that — one shared library, one header, full thread safety. Register an account and start making calls without touching the underlying stack.

Built on [baresip](https://github.com/baresip/baresip) and [libre](https://github.com/baresip/re). `baresdk.h` is the entire API surface; the SIP/RTP internals stay out of your way.

## Features

| Category | Features |
|---|---|
| **Transport** | UDP · TCP · TLS · WS · WSS |
| **Crypto** | SRTP-SDES · DTLS-SRTP |
| **NAT** | ICE · STUN · TURN |
| **Mobility** | Network handover (Wi-Fi ↔ 4G/5G) — transport rebind · re-REGISTER · call re-INVITE with media verification |
| **Calls** | Invite · Answer · Hangup · Hold/Resume · DTMF |
| **Transfer** | Blind (REFER) · Attended (REFER w/ Replaces) |
| **Messaging** | SIP MESSAGE send/receive |
| **Presence** | PUBLISH · SUBSCRIBE/NOTIFY · BLF · MWI |
| **Reliability** | PRACK / 100rel (RFC 3262) |
| **Push notifications** | RFC 8599 Contact URI params (APNs · FCM) · REGISTER-only custom headers |
| **Custom Headers** | Per-account + per-dialog (call) custom SIP headers |
| **Media** | PCM tap · WAV recording · TX/RX mute · device enumerate + hot-switch · mic/speaker gain · AEC (half-duplex or WebRTC full-duplex) |
| **Observability** | SIP trace · SDP diff · pcap · RTCP/MOS stats · jitter buffer · audio level · bandwidth |
| **Multi-account** | Yes — any number of accounts per stack |
| **Thread safety** | Full — call any API from any thread |

---

## Supported platforms

| Platform | Shared library | Static archive | TLS backend |
|---|---|---|---|
| Linux x86_64 | `dist/linux/x86_64/baresdk.so` | `baresdk.a` | OpenSSL (embedded) |
| macOS universal | `dist/macos/universal/baresdk.dylib` | `baresdk.a` | OpenSSL (embedded) |
| Windows x64 | `dist\windows\x64\baresdk.dll` | `bare.lib` | OpenSSL (embedded via vcpkg) |
| Android arm64-v8a | `dist/android/arm64-v8a/baresdk.so` | `baresdk.a` | mbedTLS (bundled) |
| Android armeabi-v7a | `dist/android/armeabi-v7a/baresdk.so` | `baresdk.a` | mbedTLS (bundled) |
| Android x86_64 | `dist/android/x86_64/baresdk.so` | `baresdk.a` | mbedTLS (bundled) |
| iOS device + simulator | `dist/ios/baresdk.xcframework` | — | mbedTLS (bundled) |

Shared libraries are **fully self-contained** — OpenSSL, zlib, and audio dependencies are baked in; no extra packages are needed at runtime.

---

## Language bindings

| Language | Location | Build |
|---|---|---|
| C / C++ | `include/baresdk.h` · `bindings/cpp/baresdk.hpp` | see below |
| Python | `bindings/python/` | `bash bindings/python/build.sh` |
| Flutter / Dart | `bindings/flutter/` | `dart pub get` in `bindings/flutter/` |

---

## Quick start

```bash
git clone --recurse-submodules https://github.com/Ibrahimgamal99/BareSDK.git
cd BareSDK
```

If you cloned without `--recurse-submodules`:
```bash
git submodule update --init --recursive third_party/re third_party/baresip
# mobile only:
git submodule update --init --recursive third_party/mbedtls
```

### Linux / macOS

```bash
# Build SDK (shared lib + static archive)
bash scripts/build-linux.sh      # or build-macos.sh

# Build and run the C++ example
bash bindings/cpp/build.sh
./bindings/cpp/build/quickstart  examples/account.json
```

### Windows (PowerShell)

**Prerequisites:** Visual Studio 2022, [vcpkg](https://vcpkg.io) with `vcpkg install openssl zlib:x64-windows-static-md`, CMake in PATH.

```powershell
# Build SDK — produces dist\windows\x64\baresdk.dll + bare.lib
.\scripts\build-windows.ps1

# Build and run the C++ example
cd bindings\cpp
.\build-examples.ps1
.\build\Release\quickstart.exe examples\account.json
```

### Python

```bash
bash bindings/python/build.sh
# builds SDK if needed, installs Python package — no LD_LIBRARY_PATH required
```

---

## Debugging

The SDK is silent by default. To see verbose init/shutdown traces (`[bsdk] step N: ...`), set:

```bash
# Linux / macOS
BARESDK_DEBUG_INIT=1 ./quickstart account.json
```

```powershell
# Windows (PowerShell)
$env:BARESDK_DEBUG_INIT=1
.\quickstart.exe account.json
```

For runtime SIP/media log verbosity, raise `baresdk_config_t.log_level` (0=err, 1=warn, 2=info, 3=debug).

---

## Documentation

Full docs live in [`docs/`](docs/index.md):

- [Quick start — C/C++](docs/quickstart/c.md)
- [Quick start — Python](docs/quickstart/python.md)
- [Quick start — Flutter](docs/quickstart/flutter.md)
- [Complete SDK usage guide](docs/guides/complete_example.md)
- [Network handover (Wi-Fi ↔ 4G/5G)](docs/guides/network_handover.md)
- [API reference](docs/api/overview.md)
- [Changelog](docs/CHANGELOG.md)

---

## License

baresdk is released under the [BSD 3-Clause License](LICENSE).

The shipped libraries statically link baresip, libre, Opus (all BSD-3-Clause)
and — on Android/iOS — Mbed TLS (Apache-2.0). Each dependency's own licence text
is in `third_party/<name>/` after
[`scripts/fetch-third-party.sh`](scripts/fetch-third-party.sh) runs.
