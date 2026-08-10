/// Desktop smoke test for the Dart binding against the real Linux build.
///
/// Requires `scripts/build-linux.sh` to have produced
/// `dist/linux/x86_64/baresdk.so`. Skipped automatically when missing.
///
/// Run: flutter test test/baresdk_smoke_test.dart
@Timeout(Duration(minutes: 2))
library;

import 'dart:io';

import 'package:baresdk/baresdk.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  final libPath = File('../../dist/linux/x86_64/baresdk.so').absolute.path;
  final haveLib = File(libPath).existsSync() && Platform.isLinux;

  test('register failure path delivers owned events safely', () async {
    final sdk = BareSDK(
      config: const BareSDKConfig(
        logLevel: 3,
        traceSip: true,
        netMonitorIntervalSeconds: 0,
      ),
      libPath: libPath,
    );
    expect(sdk.version, isNotEmpty);
    expect(BareSDK.instance, same(sdk));

    // Second construction must be refused (native stack is process-global).
    expect(() => BareSDK(), throwsStateError);

    final logs = <LogEvent>[];
    final regs = <RegStateEvent>[];
    sdk.events.listen((ev) {
      if (ev is LogEvent) logs.add(ev);
    });

    // Black-hole registrar (TEST-NET-1) — deterministic failure path.
    final account = sdk.createAccount(
      'alice@192.0.2.1:5060',
      'secret',
      config: const AccountConfig(
        transport: Transport.udp,
        audioCodecs: ['opus', 'ulaw'],
      ),
    );
    account.events.listen((ev) {
      if (ev is RegStateEvent) regs.add(ev);
    });
    account.setRetryPolicy(initialMs: 200, maxMs: 1000, backoff: 1.5);
    account.register();

    // Wait for the REGISTERING event (immediate) and some logs.
    await Future.delayed(const Duration(seconds: 3));

    expect(regs, isNotEmpty, reason: 'no RegStateEvent received');
    expect(regs.first.state, RegState.registering);
    expect(logs, isNotEmpty, reason: 'no LogEvent received');

    // Strings decoded from owned events must be intact well after delivery.
    for (final l in logs) {
      expect(l.message, isNotNull);
    }

    account.destroy();
    sdk.shutdown();
    expect(BareSDK.instance, isNull);
  }, skip: haveLib ? false : 'dist/linux/x86_64/baresdk.so not built');
}
