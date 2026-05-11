import 'dart:ffi';
import 'package:ffi/ffi.dart';

import 'ffi_bindings.dart';
import 'sdk.dart' as internal;

class Account {
  final Pointer<baresdk_account> _handle;
  bool _destroyed = false;

  Account(this._handle);

  void register() {
    internal.nativeBindings.baresdk_account_register(_handle);
  }

  void unregister() {
    internal.nativeBindings.baresdk_account_unregister(_handle);
  }

  int setRetryPolicy({
    required int initialMs,
    required int maxMs,
    required double backoff,
    int maxAttempts = 0,
  }) =>
      internal.nativeBindings.baresdk_account_set_retry_policy(
          _handle, initialMs, maxMs, backoff, maxAttempts);

  int cancelRetry() =>
      internal.nativeBindings.baresdk_account_cancel_retry(_handle);

  int retryNow() =>
      internal.nativeBindings.baresdk_account_retry_now(_handle);

  void addHeader(String name, String value) {
    final np = name.toNativeUtf8().cast<Char>();
    final vp = value.toNativeUtf8().cast<Char>();
    internal.nativeBindings.baresdk_account_add_header(_handle, np, vp);
    calloc.free(np);
    calloc.free(vp);
  }

  /// Add a custom SIP header sent on REGISTER requests only.
  ///
  /// Use for vendor push schemes (X-Push-Token, X-Apple-Push-Bundle-Id, etc.)
  /// on hosted servers. Unlike [addHeader], this header is NOT included on
  /// INVITE, BYE, or REFER — the token is not leaked to call peers.
  void addRegisterHeader(String name, String value) {
    final np = name.toNativeUtf8().cast<Char>();
    final vp = value.toNativeUtf8().cast<Char>();
    try {
      internal.nativeBindings
          .baresdk_account_add_register_header(_handle, np, vp);
    } finally {
      calloc.free(np);
      calloc.free(vp);
    }
  }

  /// Update the push notification token at runtime (e.g. on OS token rotation).
  ///
  /// Stores the new token and sends a fresh REGISTER unless a call is active
  /// or a transaction is in flight (deferred to next natural re-registration).
  /// Pass null to clear push params.
  int setPushToken(String? pushToken) {
    if (pushToken == null) {
      return internal.nativeBindings
          .baresdk_account_set_push_token(_handle, nullptr);
    }
    final tp = pushToken.toNativeUtf8().cast<Char>();
    try {
      return internal.nativeBindings
          .baresdk_account_set_push_token(_handle, tp);
    } finally {
      calloc.free(tp);
    }
  }

  void subscribePresence(String targetUri) {
    final p = targetUri.toNativeUtf8().cast<Char>();
    internal.nativeBindings.baresdk_account_subscribe_presence(_handle, p);
    calloc.free(p);
  }

  void unsubscribePresence(String targetUri) {
    final p = targetUri.toNativeUtf8().cast<Char>();
    internal.nativeBindings.baresdk_account_unsubscribe_presence(_handle, p);
    calloc.free(p);
  }

  void publishPresence(int status) {
    internal.nativeBindings.baresdk_account_publish_presence(_handle, status);
  }

  void set100rel(int mode) {
    internal.nativeBindings.baresdk_account_set_100rel(_handle, mode);
  }

  void destroy() {
    if (_destroyed) return;
    internal.nativeBindings.baresdk_account_destroy(_handle);
    _destroyed = true;
  }

  Pointer<baresdk_account> get handle => _handle;
}
