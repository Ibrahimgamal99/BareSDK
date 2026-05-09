/// baresdk — Dart/Flutter FFI wrapper.
///
/// Quick start:
///
/// ```dart
/// import 'package:baresdk/baresdk.dart';
///
/// final sdk = BareSDK();
/// final account = sdk.createAccount('alice@pbx.example.com', 'secret');
/// account.register();
///
/// account.events.listen((ev) {
///   if (ev is RegStateEvent && ev.state == RegState.registered) {
///     final call = account.call('bob@pbx.example.com');
///   } else if (ev is IncomingCallEvent) {
///     ev.call.answer();
///   }
/// });
/// ```
library baresdk;

import 'dart:async';
import 'dart:ffi';
import 'dart:isolate';
import 'package:ffi/ffi.dart';

import 'src/ffi_bindings.dart';
import 'src/sdk.dart' as internal;

export 'src/ffi_bindings.dart'
    show
        baresdk_call_state_t,
        baresdk_reg_state_t,
        baresdk_transport_t,
        baresdk_media_enc_t,
        baresdk_presence_status_t;

// ── Event types ─────────────────────────────────────────────────────────────

abstract class BareSDKEvent {}

class RegStateEvent extends BareSDKEvent {
  final int state;           // baresdk_reg_state_t values
  final int error;
  final String? errorStr;
  RegStateEvent(this.state, this.error, this.errorStr);
}

class IncomingCallEvent extends BareSDKEvent {
  final Call call;
  final String fromUri;
  final String? displayName;
  IncomingCallEvent(this.call, this.fromUri, this.displayName);
}

class CallStateEvent extends BareSDKEvent {
  final Call call;
  final int state;           // baresdk_call_state_t values
  final int error;
  final String? reason;
  CallStateEvent(this.call, this.state, this.error, this.reason);
}

class CallDtmfEvent extends BareSDKEvent {
  final Call call;
  final String digit;
  CallDtmfEvent(this.call, this.digit);
}

class AudioDevice {
  final String name;
  final String description;
  final bool isDefault;
  AudioDevice(this.name, this.description, this.isDefault);
}

class MediaStatsEvent extends BareSDKEvent {
  final Call call;
  // Packet counters
  final int packetsSent;
  final int packetsReceived;
  final int packetsLost;
  final int packetsLostRx;
  final int bytesSent;
  final int bytesReceived;
  // Loss
  final double lossPct;
  final double lossPctRx;
  // Delay / jitter
  final double jitterMs;
  final double txJitterMs;
  final double rttMs;
  // Jitter buffer
  final int jitterBufferMs;
  final int jitterBufferLoad;
  final int latePackets;
  final int discardedPackets;
  // Bandwidth
  final int bandwidthTx;
  final int bandwidthRx;
  final int avgBandwidthTx;
  final int avgBandwidthRx;
  // MOS
  final double mosLq;
  final double mosCq;
  final int mosMethod;
  // Codec
  final String codec;
  final int codecClockRate;
  final int codecSampleRate;
  final int codecChannels;
  final int payloadType;
  // Audio level
  final double audioLevelDbov;
  // Stream identity
  final int ssrcTx;
  final int ssrcRx;
  final String remoteAddr;

  MediaStatsEvent({
    required this.call,
    required this.packetsSent,
    required this.packetsReceived,
    required this.packetsLost,
    required this.packetsLostRx,
    required this.bytesSent,
    required this.bytesReceived,
    required this.lossPct,
    required this.lossPctRx,
    required this.jitterMs,
    required this.txJitterMs,
    required this.rttMs,
    required this.jitterBufferMs,
    required this.jitterBufferLoad,
    required this.latePackets,
    required this.discardedPackets,
    required this.bandwidthTx,
    required this.bandwidthRx,
    required this.avgBandwidthTx,
    required this.avgBandwidthRx,
    required this.mosLq,
    required this.mosCq,
    required this.mosMethod,
    required this.codec,
    required this.codecClockRate,
    required this.codecSampleRate,
    required this.codecChannels,
    required this.payloadType,
    required this.audioLevelDbov,
    required this.ssrcTx,
    required this.ssrcRx,
    required this.remoteAddr,
  });
}

class LogEvent extends BareSDKEvent {
  final String message;
  LogEvent(this.message);
}

class MessageEvent extends BareSDKEvent {
  final String fromUri;
  final String body;
  final String contentType;
  MessageEvent(this.fromUri, this.body, this.contentType);
}

class PresenceStateEvent extends BareSDKEvent {
  final String targetUri;
  final int status;
  PresenceStateEvent(this.targetUri, this.status);
}

class SipTraceEvent extends BareSDKEvent {
  final int direction;
  final String transport;
  final String remoteAddr;
  final String rawMessage;
  final int timestampUs;
  SipTraceEvent(this.direction, this.transport, this.remoteAddr, this.rawMessage, this.timestampUs);
}

// ── Call ─────────────────────────────────────────────────────────────────────

class Call {
  final Pointer<baresdk_call> _handle;

  Call(this._handle);

  void answer()             => internal.nativeBindings.baresdk_call_answer(_handle);
  void hangup()             => internal.nativeBindings.baresdk_call_hangup(_handle);
  void hold()               => internal.nativeBindings.baresdk_call_hold(_handle);
  void resume()             => internal.nativeBindings.baresdk_call_resume(_handle);
  void mute({bool on = true})   => internal.nativeBindings.baresdk_audio_mute(_handle, on ? 1 : 0);
  void muteRx({bool on = true}) => internal.nativeBindings.baresdk_audio_mute_rx(_handle, on ? 1 : 0);

  void sendDtmf(String digit) {
    internal.nativeBindings.baresdk_call_send_dtmf(_handle, digit.codeUnitAt(0));
  }

  void transfer(String uri) {
    final p = uri.toNativeUtf8().cast<Char>();
    internal.nativeBindings.baresdk_call_transfer(_handle, p);
    calloc.free(p);
  }

  Pointer<baresdk_call> get handle => _handle;
}

// ── Account ──────────────────────────────────────────────────────────────────

class Account {
  final Pointer<baresdk_account> _handle;
  final StreamController<BareSDKEvent> _ctrl =
      StreamController<BareSDKEvent>.broadcast();

  Account(this._handle);

  Stream<BareSDKEvent> get events => _ctrl.stream;

  void _add(BareSDKEvent ev) => _ctrl.add(ev);

  void register() {
    internal.nativeBindings.baresdk_account_register(_handle);
  }

  void unregister() {
    internal.nativeBindings.baresdk_account_unregister(_handle);
  }

  Call call(String uri) {
    final uriPtr = uri.toNativeUtf8().cast<Char>();
    final out = calloc<Pointer<baresdk_call>>();
    internal.nativeBindings.baresdk_call_invite(_handle, uriPtr, out);
    final ch = out.value;
    calloc.free(out);
    calloc.free(uriPtr);
    return Call(ch);
  }

  void sendMessage(String to, String body, {String contentType = 'text/plain'}) {
    final tp = to.toNativeUtf8().cast<Char>();
    final bp = body.toNativeUtf8().cast<Char>();
    final cp = contentType.toNativeUtf8().cast<Char>();
    internal.nativeBindings.baresdk_message_send(_handle, tp, bp, cp);
    calloc.free(tp); calloc.free(bp); calloc.free(cp);
  }

  void addHeader(String name, String value) {
    final np = name.toNativeUtf8().cast<Char>();
    final vp = value.toNativeUtf8().cast<Char>();
    internal.nativeBindings.baresdk_account_add_header(_handle, np, vp);
    calloc.free(np); calloc.free(vp);
  }

  void destroy() {
    internal.nativeBindings.baresdk_account_destroy(_handle);
    _ctrl.close();
  }
}

// ── SDK ──────────────────────────────────────────────────────────────────────

/// Global event callback — must be a static/top-level function for NativeCallable.
late void Function(Pointer<baresdk_event_t>) _eventDispatch;

void _cEventCb(Pointer<baresdk_event_t> ev, Pointer<Void> ud) {
  _eventDispatch(ev);
}

class BareSDK {
  final Map<int, Account> _accounts = {};
  late final NativeCallable<Void Function(Pointer<baresdk_event_t>, Pointer<Void>)> _nativeCb;

  BareSDK({
    int logLevel        = 1,
    int statsIntervalMs = 0,
    bool traceSip       = false,
  }) {
    _eventDispatch = _dispatchEvent;

    _nativeCb = NativeCallable<
        Void Function(Pointer<baresdk_event_t>, Pointer<Void>)>.listener(
      _cEventCb,
    );

    final cfg = calloc<baresdk_config_t>();
    internal.nativeBindings.baresdk_config_init(cfg);
    cfg.ref.log_level         = logLevel;
    cfg.ref.stats_interval_ms = statsIntervalMs;
    cfg.ref.trace_sip         = traceSip ? 1 : 0;
    cfg.ref.event_cb          = _nativeCb.nativeFunction
        .cast<NativeFunction<baresdk_event_cb_t>>();
    cfg.ref.event_userdata    = nullptr;
    internal.nativeBindings.baresdk_init(cfg);
    calloc.free(cfg);
  }

  String get version =>
      internal.nativeBindings.baresdk_version().cast<Utf8>().toDartString();

  Account createAccount(String uri, String password, {
    int transport = baresdk_transport_t.BARESDK_TRANSPORT_UDP,
  }) {
    final cfg = calloc<baresdk_account_config_t>();
    final uriPtr  = uri.toNativeUtf8().cast<Char>();
    final passPtr = password.toNativeUtf8().cast<Char>();
    cfg.ref.uri       = uriPtr;
    cfg.ref.password  = passPtr;
    cfg.ref.transport = transport;
    final out = calloc<Pointer<baresdk_account>>();
    internal.nativeBindings.baresdk_account_create(cfg, out);
    final handle = out.value;
    calloc.free(out); calloc.free(uriPtr); calloc.free(passPtr); calloc.free(cfg);

    final account = Account(handle);
    _accounts[handle.address] = account;
    return account;
  }

  List<AudioDevice> listInputDevices()  => _listDevices(internal.nativeBindings.baresdk_audio_list_input_devices);
  List<AudioDevice> listOutputDevices() => _listDevices(internal.nativeBindings.baresdk_audio_list_output_devices);

  List<AudioDevice> _listDevices(int Function(Pointer<baresdk_audio_device_t>, int) fn) {
    final buf = calloc<baresdk_audio_device_t>(32);
    final n   = fn(buf, 32);
    final out = <AudioDevice>[];
    for (int i = 0; i < n; i++) {
      final d = buf[i];
      String nameStr = '', descStr = '';
      for (int j = 0; j < 128 && d.name[j] != 0; j++) nameStr += String.fromCharCode(d.name[j]);
      for (int j = 0; j < 256 && d.description[j] != 0; j++) descStr += String.fromCharCode(d.description[j]);
      out.add(AudioDevice(nameStr, descStr, d.is_default));
    }
    calloc.free(buf);
    return out;
  }

  void shutdown() {
    internal.nativeBindings.baresdk_shutdown();
    _nativeCb.close();
  }

  // ── event dispatcher ─────────────────────────────────────────────────────

  void _dispatchEvent(Pointer<baresdk_event_t> ev) {
    final type = ev.ref.type;

    BareSDKEvent? decoded;
    Account? target;

    switch (type) {
      case baresdk_event_type_t.BARESDK_EV_LOG:
        decoded = LogEvent(ev.ref.u.log.message.cast<Utf8>().toDartString());
        for (final a in _accounts.values) a._add(decoded!);
        return;

      case baresdk_event_type_t.BARESDK_EV_REG_STATE:
        final r = ev.ref.u.reg;
        decoded = RegStateEvent(
          r.state,
          r.error,
          r.error_str == nullptr ? null : r.error_str.cast<Utf8>().toDartString(),
        );
        target = _accounts[r.account.address];
        break;

      case baresdk_event_type_t.BARESDK_EV_INCOMING_CALL:
        final ic = ev.ref.u.incoming;
        decoded = IncomingCallEvent(
          Call(ic.call),
          ic.from_uri.cast<Utf8>().toDartString(),
          ic.display_name == nullptr ? null : ic.display_name.cast<Utf8>().toDartString(),
        );
        target = _accounts[ic.account.address];
        break;

      case baresdk_event_type_t.BARESDK_EV_CALL_STATE:
        final cs = ev.ref.u.call_state;
        decoded = CallStateEvent(
          Call(cs.call),
          cs.state,
          cs.error,
          cs.reason == nullptr ? null : cs.reason.cast<Utf8>().toDartString(),
        );
        target = _accounts[cs.account.address];
        break;

      case baresdk_event_type_t.BARESDK_EV_MEDIA_STATS:
        final s = ev.ref.u.stats;
        String addrStr = '';
        for (int i = 0; i < 64; i++) {
          final b = s.remote_addr[i];
          if (b == 0) break;
          addrStr += String.fromCharCode(b);
        }
        decoded = MediaStatsEvent(
          call:               Call(s.call),
          packetsSent:        s.packets_sent,
          packetsReceived:    s.packets_received,
          packetsLost:        s.packets_lost,
          packetsLostRx:      s.packets_lost_rx,
          bytesSent:          s.bytes_sent,
          bytesReceived:      s.bytes_received,
          lossPct:            s.loss_pct,
          lossPctRx:          s.loss_pct_rx,
          jitterMs:           s.jitter_ms,
          txJitterMs:         s.tx_jitter_ms,
          rttMs:              s.rtt_ms,
          jitterBufferMs:     s.jitter_buffer_ms,
          jitterBufferLoad:   s.jitter_buffer_load,
          latePackets:        s.late_packets,
          discardedPackets:   s.discarded_packets,
          bandwidthTx:        s.bandwidth_kbps_tx,
          bandwidthRx:        s.bandwidth_kbps_rx,
          avgBandwidthTx:     s.avg_bandwidth_kbps_tx,
          avgBandwidthRx:     s.avg_bandwidth_kbps_rx,
          mosLq:              s.mos_lq,
          mosCq:              s.mos_cq,
          mosMethod:          s.mos_method,
          codec:              s.codec_name == nullptr ? '' : s.codec_name.cast<Utf8>().toDartString(),
          codecClockRate:     s.codec_clock_rate,
          codecSampleRate:    s.codec_sample_rate,
          codecChannels:      s.codec_channels,
          payloadType:        s.payload_type,
          audioLevelDbov:     s.audio_level_dbov,
          ssrcTx:             s.ssrc_tx,
          ssrcRx:             s.ssrc_rx,
          remoteAddr:         addrStr,
        );
        for (final a in _accounts.values) a._add(decoded!);
        return;

      case baresdk_event_type_t.BARESDK_EV_MESSAGE:
        final m = ev.ref.u.msg;
        decoded = MessageEvent(
          m.from_uri.cast<Utf8>().toDartString(),
          m.body.cast<Utf8>().toDartString(),
          m.content_type.cast<Utf8>().toDartString(),
        );
        target = _accounts[m.account.address];
        break;

      default:
        return;
    }

    if (decoded != null && target != null) {
      target._add(decoded);
    }
  }
}
