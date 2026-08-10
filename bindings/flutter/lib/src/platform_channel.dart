/// Android platform shim.
///
/// Talks to the plugin's Kotlin side (`BaresdkPlugin`) over the `baresdk`
/// MethodChannel for the few things FFI cannot do:
///  - app cache dir (baresdk's required tmp_dir on Android),
///  - audio focus / MODE_IN_COMMUNICATION around calls,
///  - speakerphone routing,
///  - ConnectivityManager network-change callbacks (drives handover).
///
/// Every call is a no-op off Android so the package still works as a pure
/// FFI binding on desktop.
library;

import 'dart:io' show Platform;

import 'package:flutter/services.dart';

class BareSDKPlatform {
  static const MethodChannel _channel = MethodChannel('baresdk');
  static bool _handlerInstalled = false;
  static void Function()? onNetworkChanged;

  static bool get _isAndroid {
    try {
      return Platform.isAndroid;
    } catch (_) {
      return false;
    }
  }

  /// Install the inbound handler (network-change pings from Kotlin).
  static void ensureHandler() {
    if (_handlerInstalled || !_isAndroid) return;
    _handlerInstalled = true;
    _channel.setMethodCallHandler((call) async {
      if (call.method == 'onNetworkChanged') {
        onNetworkChanged?.call();
      }
      return null;
    });
  }

  /// App cache directory — used as baresdk `tmp_dir`. Null off Android.
  static Future<String?> getCacheDir() async {
    if (!_isAndroid) return null;
    return _channel.invokeMethod<String>('getCacheDir');
  }

  /// Request/abandon voice-call audio focus and MODE_IN_COMMUNICATION.
  static Future<void> configureAudioSession(bool active) async {
    if (!_isAndroid) return;
    await _channel.invokeMethod('configureAudioSession', {'active': active});
  }

  /// Route audio to the loudspeaker (true) or earpiece/default (false).
  static Future<void> setSpeakerphone(bool on) async {
    if (!_isAndroid) return;
    await _channel.invokeMethod('setSpeakerphone', {'on': on});
  }
}
