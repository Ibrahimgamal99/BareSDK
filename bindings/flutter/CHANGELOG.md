# Changelog

## Unreleased

### Added

- Degraded-link handling on `BareSDKConfig` — for the failure handover cannot
  see, where the address stays put and the link itself goes bad:
  `mediaStallMs`, `rtpTimeoutSeconds`, `adaptiveBitrate` with its
  `adaptMin/MaxBitrate`, `adaptLossDown/UpPct` and `adaptRecoverTicks` bounds,
  `opusExpectedLossPct`, `keepaliveReregister`, `dnsSrvFailover`,
  `regRetryJitter`, `sipTimerBMs`, `sipTimerFMs` and `netIceHandover`.
- `QualityIssue.mediaStall` — no inbound RTP for `mediaStallMs` while the call
  is neither held nor mid-handover. Non-fatal; fires again with
  `recovering = true` when packets resume.
- `NetworkStage.callIceStale` — this call negotiated ICE, so its candidates are
  stale and cannot be re-gathered mid-call. Emitted before the handover
  re-INVITE so the UI can say something useful while the attempt is in flight.
- `IceHandover` enum for `netIceHandover`: `bestEffort` (default) or `failFast`.
- `Call.setRtpTimeout()`, `Call.setBitrate()`, `Account.keepaliveNow()`,
  `BareSDK.setAdaptiveBitrate()`.

### Changed

- `statsIntervalMs` now defaults to 2000 (was 0) and the three quality-alert
  thresholds to 3.5 / 5.0 / 40.0. These are written to the native config
  unconditionally, so leaving them at 0 disabled RTCP accounting outright —
  which also disabled quality alerts, media-stall detection and adaptive
  bitrate. Set `statsIntervalMs: 0` explicitly to opt out.

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
