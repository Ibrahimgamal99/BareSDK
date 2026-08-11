# Changelog

## 1.0.0

First release of the Flutter/Dart binding.

- FFI binding over the full `baresdk.h` surface: accounts, calls, hold/resume,
  DTMF, blind + attended transfer, SIP MESSAGE, presence/BLF/MWI, custom
  headers, push tokens (RFC 8599).
- Transports: UDP · TCP · TLS · WS · WSS, with SRTP-SDES and DTLS-SRTP.
- Reconnection and network handover (Wi-Fi ↔ cellular) driven by OS
  connectivity callbacks.
- Media: codec selection and Opus tuning, PCM tap, WAV recording, mute, device
  enumeration and hot-switch, gain, AEC.
- Observability: RTCP/MOS media statistics and quality alerts, SIP trace.
- Android: prebuilt `libbaresdk.so` for arm64-v8a, armeabi-v7a and x86_64 plus
  a Kotlin shim (cache dir, audio focus, speakerphone, ConnectivityManager).
- iOS: vendored dynamic `baresdk.xcframework` plus a Swift shim
  (`AVAudioSession`, speaker routing, `NWPathMonitor`), VoiceProcessingIO
  capture with hardware echo cancellation.
- Desktop (Linux/macOS/Windows) usable as a plain FFI binding.
