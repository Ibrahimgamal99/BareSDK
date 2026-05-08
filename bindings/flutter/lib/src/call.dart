import 'dart:ffi';
import 'package:ffi/ffi.dart';

import 'ffi_bindings.dart';
import 'sdk.dart' as internal;

class Call {
  final Pointer<baresdk_call> _handle;
  bool _ended = false;

  Call(this._handle);

  int answer() => internal.nativeBindings.baresdk_call_answer(_handle);

  int hangup() {
    if (_ended) return 0;
    final rc = internal.nativeBindings.baresdk_call_hangup(_handle);
    _ended = true;
    return rc;
  }

  int hold() => internal.nativeBindings.baresdk_call_hold(_handle);

  int resume() => internal.nativeBindings.baresdk_call_resume(_handle);

  int mute({bool on = true}) =>
      internal.nativeBindings.baresdk_audio_mute(_handle, on ? 1 : 0);

  int sendDtmf(String digit) =>
      internal.nativeBindings.baresdk_call_send_dtmf(_handle, digit.codeUnitAt(0));

  int transfer(String uri) {
    final p = uri.toNativeUtf8().cast<Char>();
    final rc = internal.nativeBindings.baresdk_call_transfer(_handle, p);
    calloc.free(p);
    return rc;
  }

  int attendedTransfer(Call other) =>
      internal.nativeBindings.baresdk_call_attended_transfer(_handle, other._handle);

  int addHeader(String name, String value) {
    final np = name.toNativeUtf8().cast<Char>();
    final vp = value.toNativeUtf8().cast<Char>();
    final rc = internal.nativeBindings.baresdk_call_add_header(_handle, np, vp);
    calloc.free(np);
    calloc.free(vp);
    return rc;
  }

  int getStats(Pointer<baresdk_ev_media_stats_t> out) =>
      internal.nativeBindings.baresdk_call_get_stats(_handle, out);

  Pointer<baresdk_call> get handle => _handle;
}
