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

  void addHeader(String name, String value) {
    final np = name.toNativeUtf8().cast<Char>();
    final vp = value.toNativeUtf8().cast<Char>();
    internal.nativeBindings.baresdk_account_add_header(_handle, np, vp);
    calloc.free(np);
    calloc.free(vp);
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
