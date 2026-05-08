# Quick start — Flutter / Dart

## Prerequisites

Build the shared library for your target platform — the Flutter package loads it at runtime via dart:ffi:

```bash
./scripts/build-android.sh        # Android → dist/android/<ABI>/baresdk.so
./scripts/build-macos.sh          # macOS   → dist/macos/universal/baresdk.dylib
./scripts/build-linux.sh          # Linux   → dist/linux/x86_64/baresdk.so
.\scripts\build-windows.ps1       # Windows → dist\windows\x64\baresdk.dll
```

For iOS the build produces `dist/ios/baresdk.xcframework` (static xcframework — no separate shared lib step needed).

## Add to pubspec.yaml

```yaml
dependencies:
  baresdk:
    path: path/to/baresdk/bindings/flutter
```

## Generate FFI bindings (one-time)

```bash
cd bindings/flutter
dart pub get
dart run ffigen --config ffigen.yaml
```

The generated `lib/src/ffi_bindings.dart` is committed to the repository so you only need to re-run this when `include/baresdk.h` changes.

---

## Register and handle calls

```dart
import 'package:baresdk/baresdk.dart';

void main() async {
  final sdk     = BareSDK(logLevel: 1, statsIntervalMs: 5000);
  final account = sdk.createAccount(
    'alice@pbx.example.com',
    'secret',
    transport: baresdk_transport_t.BARESDK_TRANSPORT_TLS,
  );

  account.register();

  await for (final ev in account.events) {
    if (ev is RegStateEvent && ev.state == baresdk_reg_state_t.BARESDK_REG_REGISTERED) {
      print('Registered!');
      final call = account.call('bob@pbx.example.com');

    } else if (ev is IncomingCallEvent) {
      print('Incoming from ${ev.fromUri}');
      ev.call.answer();

    } else if (ev is CallStateEvent &&
               ev.state == baresdk_call_state_t.BARESDK_CALL_ENDED) {
      print('Call ended.');
      break;

    } else if (ev is MediaStatsEvent) {
      print('MOS: ${ev.mosLq.toStringAsFixed(2)}  RTT: ${ev.rttMs.toStringAsFixed(0)} ms');
    }
  }

  account.destroy();
  sdk.shutdown();
}
```

---

## Flutter widget example

A minimal Flutter widget is at [bindings/flutter/example/lib/main.dart](../../bindings/flutter/example/lib/main.dart).

Run it:
```bash
cd bindings/flutter/example
flutter run
```

---

## See also
- [bindings/flutter/lib/baresdk.dart](../../bindings/flutter/lib/baresdk.dart) — public API
- [ffigen.yaml](../../bindings/flutter/ffigen.yaml) — binding generation config
