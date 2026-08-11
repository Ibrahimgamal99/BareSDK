# Quick start — Flutter / Dart

The `baresdk` Flutter package (`bindings/flutter/`) is a hybrid plugin:

- **Dart FFI** drives the native SIP/media core (`libbaresdk`).
- On **Android** the plugin ships prebuilt `libbaresdk.so` for
  `arm64-v8a`, `armeabi-v7a` and `x86_64` inside its `jniLibs`, plus a small
  Kotlin shim (`BaresdkPlugin`) that provides the app cache dir, voice-call
  audio focus, speakerphone routing, and network-change callbacks.
- On **iOS** the plugin vendors a prebuilt dynamic `baresdk.xcframework`
  (device arm64 + simulator arm64/x86_64) plus a Swift shim
  (`BaresdkPlugin`) for audio-session activation, speakerphone routing, and
  `NWPathMonitor` network-change callbacks. Capture uses Apple's
  VoiceProcessingIO audio unit (hardware echo cancellation).
- **Desktop** (Linux/Windows/macOS) works as a plain FFI binding — the app
  bundles the native library itself (or passes `BareSDK(libPath: ...)`).

## 1 — Add the package

```yaml
dependencies:
  baresdk:
    git:
      url: https://github.com/Ibrahimgamal99/BareSDK.git
      path: bindings/flutter
  # or, from a checkout:
  # baresdk:
  #   path: path/to/baresdk/bindings/flutter
```

Android needs nothing else — the plugin already carries the native libraries
and manifest permissions (`INTERNET`, `RECORD_AUDIO`,
`MODIFY_AUDIO_SETTINGS`, `ACCESS_NETWORK_STATE`). Your app must still
*request* the microphone permission at runtime (e.g. with
`permission_handler`) before the first call. `minSdkVersion` must be >= 24.

iOS (>= 13.0) additionally needs, in your app's `Info.plist`:

```xml
<key>NSMicrophoneUsageDescription</key>
<string>Microphone access is required for voice calls.</string>
<key>UIBackgroundModes</key>
<array><string>audio</string></array>
```

For production VoIP apps, add PushKit + CallKit in your app (Apple kills
background sockets; the push→CallKit→re-register flow is the sanctioned
model — pair it with `account.setPushToken()` / RFC 8599, or your own
server-side push).

With CallKit, hand it the audio session:

```dart
final sdk = await BareSDK.start(
  config: const BareSDKConfig(platformAudioActivate: false),
  manageAudioSession: false,   // CXProvider owns activation
);
```

`CXProvider` must be the only thing that activates the `AVAudioSession`
(Apple requires it in `provider(_:didActivateAudioSession:)`). Left at the
default, starting the SDK — at launch, or on a PushKit wake while CallKit is
still reporting the call — seizes the exclusive PlayAndRecord route out from
under CallKit. The category and mode are still configured, so audio works as
soon as CallKit activates the session.

The vendored xcframework must exist at
`bindings/flutter/ios/Frameworks/baresdk.xcframework` — it is built by CI
(`.github/workflows/build-mobile.yml`) or on any Mac with
`scripts/build-ios.sh`; `pod install` fails with a clear error when it is
missing.

## 2 — Start the SDK

```dart
import 'package:baresdk/baresdk.dart';

final sdk = await BareSDK.start(
  config: const BareSDKConfig(
    statsIntervalMs: 5000,        // MediaStatsEvent every 5 s
    mosAlertThreshold: 3.5,       // QualityAlertEvent when MOS drops
    lossAlertThreshold: 5.0,
    jitterAlertThreshold: 40.0,
  ),
);
```

`BareSDK.start()` is required on Android: it injects the app cache dir as
the SDK's `tmp_dir`, switches network monitoring from polling to
ConnectivityManager callbacks, and manages audio focus around calls. On
desktop it behaves like the plain constructor.

The native stack is a **process-wide singleton** — constructing a second
`BareSDK` from an isolate that already has one throws until you call
`shutdown()`.

### Background isolates (push wakeups)

An Android headless engine destroys its Dart isolate when its task ends, while
the process — and the SIP stack inside it, still registered — stays alive. The
next push runs your start-up code in a *new* isolate against that live stack, so
`BareSDK.start()` reattaches to it instead of failing:

```dart
final sdk = await BareSDK.start(config: cfg);

if (sdk.reattached) {
  // The stack was already up: `cfg` was NOT applied (config only takes effect
  // at init), and the accounts/calls of the previous isolate are adopted.
  for (final a in sdk.accounts) print('${a.aor} is ${a.regState}');

  // An INVITE that arrived while no isolate was listening fired no event here —
  // but the call is live, so pick it up from state instead.
  for (final c in sdk.calls) {
    if (c.state == CallState.ringing) showIncomingCallUi(c);
  }
} else {
  final account = sdk.createAccount('alice@pbx.example.com', 'secret');
  account.register();
}

// When the background task is done: park delivery, leave the stack up and
// push-reachable for the next wakeup. Use shutdown() only to really stop.
sdk.detach();
```

Pass `reattachIfRunning: false` to get the hard failure instead.

## 3 — Register an account (any transport)

```dart
// UDP / TCP / TLS — host comes from the AOR:
final account = sdk.createAccount('alice@pbx.example.com', 'secret',
    config: const AccountConfig(transport: Transport.tls));

// WS / WSS — a full server URL is required (scheme picks the transport):
final account = sdk.createAccount('alice@pbx.example.com', 'secret',
    config: const AccountConfig(
      serverUrl: 'wss://pbx.example.com:8089/ws',
      mediaEnc: MediaEncryption.dtlsSrtp,   // typical for WebRTC gateways
      iceEnabled: true,
      audioCodecs: ['opus', 'g722', 'ulaw'],  // ordered preference
    ));

account.register();
account.events.listen((ev) {
  if (ev is RegStateEvent) print('reg: ${ev.state}');
});
```

`AccountConfig` also covers STUN/TURN (`stunServer`, `turnServer`,
`turnUser`, `turnPass`), auth user, display name, outbound proxy, push
tokens (RFC 8599), DTMF mode, and per-account codec overrides.

## 4 — Calls

```dart
final call = account.call('bob@pbx.example.com');
call.hangup();  call.hold();  call.resume();
call.mute();    call.sendDtmf('5');
call.transfer('carol@pbx.example.com');        // blind
call.attendedTransfer(otherCall);               // REFER w/ Replaces
final stats = call.getStats();                  // on-demand MediaStats

sdk.events.listen((ev) {
  switch (ev) {
    case IncomingCallEvent e:  e.call.answer();
    case CallStateEvent e:     print('${e.state} ${e.reason ?? ''}');
    case MediaStatsEvent e:    print('MOS ${e.stats.mosLq}');
    case QualityAlertEvent e:  print('alert: ${e.issue} ${e.value}');
    default: break;
  }
});
```

## 5 — Reconnection & network handover

Registration retries automatically with exponential backoff
(`BareSDKConfig.regRetry*` or per-account `setRetryPolicy`). On a network
change (Wi-Fi ↔ cellular) the SDK re-binds transports, re-REGISTERs, and
re-INVITEs active calls; progress arrives as `NetworkEvent` stages:

```dart
account.setRetryPolicy(initialMs: 2000, maxMs: 60000, backoff: 2.0);
account.retryNow();     // skip the current backoff
account.cancelRetry();  // stop retrying

sdk.events.listen((ev) {
  if (ev is NetworkEvent && ev.stage == NetworkStage.callMigrated) {
    print('audio recovered after ${ev.elapsedMs} ms');
  }
});
```

On Android the plugin feeds ConnectivityManager changes to the SDK
automatically. On desktop the built-in poller
(`netMonitorIntervalSeconds`) covers it, or call `sdk.networkChanged()`
from your own connectivity hook.

Calls that negotiated **ICE** recover best-effort only: baresip cannot
regenerate ICE credentials mid-call (`NetworkEvent.ice` is true for these).

## 6 — Audio

```dart
sdk.listInputDevices();  sdk.setInputDevice('...');   // desktop devices
sdk.setSpeakerphone(true);                            // Android routing
sdk.setAec(true);  sdk.setNs(true);  sdk.setAgc(true);
sdk.setMicGainDb(6);  sdk.setSpeakerGainDb(-3);
```

Android capture/playback uses baresip's OpenSLES module (works on every
supported API level). Opus can be tuned via `BareSDKConfig.opus`
(`OpusConfig(bitrate: 32000, fec: true, dtx: true, ...)`).

## 7 — Logging & tracing

The SDK never writes to stdout/stderr. Everything arrives as events:
`LogEvent` (log lines), `SipTraceEvent` (raw SIP, enable
`BareSDKConfig.traceSip`), `SdpNegotiationEvent` (enable `traceSdpDiff`),
plus `sdk.pcapStart('/path/trace.pcap')` for Wireshark captures.

## Example app

`bindings/flutter/example/` is a runnable softphone (Android):

```bash
cd bindings/flutter/example
flutter run
```

Three tabs: Account (all five transports, codec ordering, retry controls),
Call (dial/answer/hold/mute/DTMF/transfer/speaker), Diagnostics (live
stats, quality alerts, handover timeline, in-app log console).

## Rebuilding the native libraries (maintainers)

```bash
# Android — builds all ABIs, verifies symbols/alignment, refreshes the
# plugin's stripped jniLibs copies:
ANDROID_NDK=$HOME/Android/Sdk/ndk/<ver> \
BARESDK_BUILD_ROOT=$HOME/.cache/baresdk-build \
  ./scripts/build-android.sh

# iOS (macOS + Xcode only) — builds the dynamic xcframework (device +
# simulator), verifies exports, refreshes the plugin's vendored copy:
./scripts/build-ios.sh

# Or let CI do both: .github/workflows/build-mobile.yml builds Android on
# ubuntu and the iOS xcframework on macos, uploading both as artifacts.

# Regenerate FFI bindings after changing include/baresdk.h:
cd bindings/flutter && dart run ffigen --config ffigen.yaml
```

`BARESDK_BUILD_ROOT` should point at a native filesystem when the repo
lives on NTFS — incremental builds there silently reuse stale objects.
The desktop smoke test for the binding is
`bindings/flutter/test/baresdk_smoke_test.dart` (needs
`scripts/build-linux.sh` first); the C-side owned-events gate is
`test/owned_events_test.c`.
