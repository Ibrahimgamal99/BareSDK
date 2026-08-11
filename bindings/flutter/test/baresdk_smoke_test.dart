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

  // One body on purpose: the event callback is bound to the zone that created
  // it, so a second test's events would be dropped.
  test('reattaches to a stack this isolate did not start', () async {
    // Stand-in for the Android headless engine's first push wakeup.
    final first = BareSDK(
      config: const BareSDKConfig(
        logLevel: 3,
        netMonitorIntervalSeconds: 0,
        // Global (not per-account) codec list — the marshalling this exercises
        // used to stop at the Dart object.
        audioCodecs: ['ulaw', 'opus'],
      ),
      libPath: libPath,
    );
    expect(first.reattached, isFalse);

    final account = first.createAccount('alice@192.0.2.1:5060', 'secret',
        config: const AccountConfig(transport: Transport.udp));
    account.setRetryPolicy(initialMs: 200, maxMs: 1000, backoff: 1.5);
    account.register();
    await Future.delayed(const Duration(seconds: 1));

    // The isolate goes away; the native stack, its registration and its calls
    // do not. detach() is the graceful form of what a destroyed headless
    // isolate does to this process abruptly.
    first.detach();
    expect(BareSDK.instance, isNull);

    // Second push wakeup: same start-up code, new isolate, stack still up.
    final second = BareSDK(
      config: const BareSDKConfig(netMonitorIntervalSeconds: 0),
      libPath: libPath,
    );
    expect(second.reattached, isTrue,
        reason: 'construction on a live stack did not reattach');
    expect(BareSDK.instance, same(second));

    // The account the dead isolate created is adopted, not lost or duplicated.
    expect(second.accounts.length, 1);
    final adopted = second.accounts.first;
    expect(adopted.aor, 'sip:alice@192.0.2.1');
    expect(adopted.regState,
        anyOf(RegState.registering, RegState.failed, RegState.registered));
    expect(second.calls, isEmpty);

    // Events now reach the new isolate, and route to the adopted account.
    final sdkEvents = <BareSDKEvent>[];
    final acctEvents = <BareSDKEvent>[];
    second.events.listen(sdkEvents.add);
    adopted.events.listen(acctEvents.add);
    adopted.unregister();
    await Future.delayed(const Duration(milliseconds: 300));
    adopted.register();
    await Future.delayed(const Duration(seconds: 2));

    expect(sdkEvents, isNotEmpty, reason: 'no events after reattach');
    expect(acctEvents.whereType<RegStateEvent>(), isNotEmpty,
        reason: 'adopted account received no registration events');

    // Opting out restores the hard failure: stack up, no Dart instance
    // holding it, and init refused rather than reattached.
    second.detach();
    expect(() => BareSDK(libPath: libPath, reattachIfRunning: false),
        throwsStateError);

    // Reattach once more — this time to tear the stack down for real.
    final third = BareSDK(libPath: libPath);
    expect(third.reattached, isTrue);
    expect(third.accounts.length, 1);
    third.accounts.first.destroy();
    third.shutdown();
    expect(BareSDK.instance, isNull);
  }, skip: haveLib ? false : 'dist/linux/x86_64/baresdk.so not built');
}
