# Quick start — Flutter / Dart

## Prerequisites

| Requirement | Minimum version |
|---|---|
| Dart SDK | 3.0 |
| Flutter SDK | 3.10 |
| ffigen | 12.0 (dev dependency, already in pubspec) |
| clang | any recent version (needed only to regenerate bindings) |

Install clang if you plan to regenerate FFI bindings:

```bash
# Fedora / RHEL
sudo dnf install clang

# Ubuntu / Debian
sudo apt install clang

# macOS
xcode-select --install
```

---

## 1 — Build the native library

Build for your target platform before running any Flutter app. The package loads the library at runtime via `dart:ffi`.

```bash
# Android  → dist/android/<ABI>/baresdk.so
./scripts/build-android.sh

# Linux x86_64  → dist/linux/x86_64/baresdk.so
./scripts/build-linux.sh

# macOS universal  → dist/macos/universal/baresdk.dylib
./scripts/build-macos.sh

# Windows x64  → dist\windows\x64\baresdk.dll
.\scripts\build-windows.ps1
```

iOS produces a static xcframework — no separate shared library step is needed:

```bash
./scripts/build-ios.sh   # → dist/ios/baresdk.xcframework
```

---

## 2 — Add the package

In your Flutter app's `pubspec.yaml`:

```yaml
dependencies:
  baresdk:
    path: path/to/baresdk/bindings/flutter
```

Then fetch:

```bash
flutter pub get
```

---

## 3 — Regenerate FFI bindings (when the header changes)

The generated file `lib/src/ffi_bindings.dart` is committed to the repository.
Only re-run this when `include/baresdk.h` changes.

```bash
cd bindings/flutter
dart pub get
dart run ffigen --config ffigen.yaml
```

### How ffigen.yaml is configured

```yaml
compiler-opts:
  - '-I../../include'
  - '-isystem/usr/lib/clang/21/include'   # clang built-in headers (stddef.h etc.)
```

The clang resource directory must point to the version installed on your machine.
Find it with:

```bash
clang -print-resource-dir
# e.g. /usr/lib/clang/21
```

Then set `-isystem<path>/include` accordingly.

### Expected warnings after a clean run

These four warnings are **normal** — `baresdk_account` and `baresdk_call` are
intentionally forward-declared opaque handles with no struct body in the public header:

```
[WARNING]: No definition found for declaration - baresdk_account
[WARNING]: No definition found for declaration - baresdk_call
```

Any other `[SEVERE]` errors or large numbers of `_`-prefixed private declarations
indicate a misconfigured `compiler-opts` or a missing `macros` filter — see
[ffigen.yaml](../../bindings/flutter/ffigen.yaml) for the working configuration.

---

## 4 — Initialize and use the SDK

```dart
import 'dart:ffi';
import 'dart:io';
import 'package:baresdk/baresdk.dart';

// ── Load the native library ──────────────────────────────────────────────────

DynamicLibrary _loadLib() {
  if (Platform.isAndroid || Platform.isLinux) {
    return DynamicLibrary.open('baresdk.so');
  } else if (Platform.isMacOS) {
    return DynamicLibrary.open('baresdk.dylib');
  } else if (Platform.isWindows) {
    return DynamicLibrary.open('baresdk.dll');
  } else if (Platform.isIOS) {
    return DynamicLibrary.process(); // statically linked xcframework
  }
  throw UnsupportedError('Unsupported platform: ${Platform.operatingSystem}');
}

// ── Initialize ───────────────────────────────────────────────────────────────

void main() async {
  final sdk = BareSDK(logLevel: 1, statsIntervalMs: 5000);

  // ── Create an account ───────────────────────────────────────────────────────

  final account = sdk.createAccount(
    'alice@pbx.example.com',
    'secret',
    transport: baresdk_transport_t.BARESDK_TRANSPORT_UDP,
  );

  account.register();

  // ── Event loop ──────────────────────────────────────────────────────────────

  await for (final ev in account.events) {
    if (ev is RegStateEvent &&
        ev.state == baresdk_reg_state_t.BARESDK_REG_REGISTERED) {
      print('Registered');
      account.call('bob@pbx.example.com');

    } else if (ev is IncomingCallEvent) {
      print('Incoming call from ${ev.fromUri}');
      ev.call.answer();

    } else if (ev is CallStateEvent) {
      final done =
          ev.state == baresdk_call_state_t.BARESDK_CALL_ENDED ||
          ev.state == baresdk_call_state_t.BARESDK_CALL_FAILED ||
          ev.state == baresdk_call_state_t.BARESDK_CALL_CANCELLED;
      if (done) break;

    } else if (ev is MediaStatsEvent) {
      print('MOS ${ev.mosLq.toStringAsFixed(2)}  '
            'RTT ${ev.rttMs.toStringAsFixed(0)} ms');
    }
  }

  // ── Tear down ───────────────────────────────────────────────────────────────

  account.destroy();
  sdk.shutdown();
}
```

---

## 5 — Runtime audio quality controls

```dart
// At init time — enable processing:
final sdk = BareSDK(logLevel: 1);
// (set fields on cfg.ref before baresdk_init — see BareSDK constructor)

// Toggle filters at any point during a call:
sdk.setAec(true);
sdk.setNs(false);
sdk.setAgc(true);

// Widen jitter buffer on a poor network (takes effect on new calls):
sdk.setJitterBuffer(20, 200);

// Set per-call RTP DSCP on an established call (EF = 46):
call.setDscpRtp(46);
```

---

## 6 — Flutter widget example

A minimal widget is in [bindings/flutter/example/lib/main.dart](../../bindings/flutter/example/lib/main.dart).

```bash
cd bindings/flutter/example
flutter run
```

---

## Changing the native library path

By default the SDK looks up the library by name and the OS resolves it through
its standard search paths (`LD_LIBRARY_PATH` on Linux, `PATH` on Windows,
`DYLD_LIBRARY_PATH` on macOS). Pass `libPath` to `BareSDK` to override this
with an absolute path:

```dart
// Load from an explicit location instead of the system search path
final sdk = BareSDK(
  libPath: '/opt/myapp/libs/baresdk.so',
);
```

**Default search paths per platform**

| Platform | Default name | Where the OS looks |
|---|---|---|
| Android | `baresdk.so` | `jniLibs/<ABI>/` inside the APK (automatic) |
| Linux | `baresdk.so` | `LD_LIBRARY_PATH`, then `/usr/lib`, etc. |
| macOS | `baresdk.dylib` | `DYLD_LIBRARY_PATH`, then `@rpath` |
| Windows | `baresdk.dll` | `PATH`, then the executable directory |
| iOS | *(process)* | Statically linked — no file path needed |

**Linux desktop:** Flutter's Linux runner sets `rpath=$ORIGIN/lib`, so copy
`baresdk.so` into the `lib/` subdirectory next to the executable:

```
build/linux/x64/release/bundle/
  myapp
  lib/
    baresdk.so   ← here
```

**Windows desktop:** no rpath — copy `baresdk.dll` directly next to the
executable (Windows checks the exe directory first):

```
build\windows\x64\runner\Release\
  myapp.exe
  baresdk.dll   ← here
```

**Shipping to another machine:** copy the entire bundle directory — the library
placement within it is the same, the OS finds it automatically:

```bash
# Linux — copy the whole bundle and run it on the target
scp -r build/linux/x64/release/bundle/ user@target:/opt/myapp/
ssh user@target /opt/myapp/bundle/myapp
```

For Windows, zip `build\windows\x64\runner\Release\` and extract it on the
target machine. Run `myapp.exe` from inside that folder.

Before shipping on Linux, verify that `baresdk.so`'s own dependencies are
present on the target:

```bash
ldd build/linux/x64/release/bundle/lib/baresdk.so
```

Any `not found` line means that library must either be installed on the target
or copied into `bundle/lib/` alongside `baresdk.so`.

---

## Troubleshooting

| Error | Cause | Fix |
|---|---|---|
| `fatal error: 'stddef.h' file not found` | clang resource headers missing from `compiler-opts` | Set `-isystem$(clang -print-resource-dir)/include` in `ffigen.yaml` |
| `Unknown key - 'typ-map'` | Old/invalid ffigen.yaml key | Remove the `typ-map` block; ffigen maps C primitives automatically |
| `baresdk.so: cannot open shared object file` | Library not on the search path | Pass `libPath:` to `BareSDK()`, or set `LD_LIBRARY_PATH` |
| `Invalid argument(s): Failed to load dynamic library` (iOS) | xcframework not embedded | Add `baresdk.xcframework` to *Embed Frameworks* in Xcode |

---

## See also

- [bindings/flutter/lib/baresdk.dart](../../bindings/flutter/lib/baresdk.dart) — public Dart API
- [bindings/flutter/ffigen.yaml](../../bindings/flutter/ffigen.yaml) — binding generation config
- [include/baresdk.h](../../include/baresdk.h) — C public header
