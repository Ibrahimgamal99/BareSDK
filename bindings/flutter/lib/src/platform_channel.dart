/// Mobile platform shim.
///
/// Talks to the plugin's native side (`BaresdkPlugin` — Kotlin on Android,
/// Swift on iOS) over the `baresdk` MethodChannel for the few things FFI
/// cannot do:
///  - a writable temp dir (baresdk's required tmp_dir on Android),
///  - audio focus / session activation around calls,
///  - speakerphone routing,
///  - network-change callbacks (ConnectivityManager / NWPathMonitor) that
///    drive handover via networkChanged().
///
/// Every call is a no-op on desktop so the package still works as a pure
/// FFI binding there.
library;

import 'dart:io' show Platform;

import 'package:flutter/services.dart';

class BareSDKPlatform {
  static const MethodChannel _channel = MethodChannel('baresdk');
  static bool _handlerInstalled = false;
  static void Function()? onNetworkChanged;

  static bool get _isMobile {
    try {
      return Platform.isAndroid || Platform.isIOS;
    } catch (_) {
      return false;
    }
  }

  /// Install the inbound handler (network-change pings from Kotlin).
  static void ensureHandler() {
    if (_handlerInstalled || !_isMobile) return;
    _handlerInstalled = true;
    _channel.setMethodCallHandler((call) async {
      if (call.method == 'onNetworkChanged') {
        onNetworkChanged?.call();
      }
      return null;
    });
  }

  /// Writable temp directory — used as baresdk `tmp_dir`. Null on desktop.
  static Future<String?> getCacheDir() async {
    if (!_isMobile) return null;
    return _channel.invokeMethod<String>('getCacheDir');
  }

  /// Request/abandon the voice-call audio session (Android: audio focus +
  /// MODE_IN_COMMUNICATION; iOS: AVAudioSession activation).
  static Future<void> configureAudioSession(bool active) async {
    if (!_isMobile) return;
    await _channel.invokeMethod('configureAudioSession', {'active': active});
  }

  /// Route audio to the loudspeaker (true) or earpiece/default (false).
  static Future<void> setSpeakerphone(bool on) async {
    if (!_isMobile) return;
    await _channel.invokeMethod('setSpeakerphone', {'on': on});
  }
}
