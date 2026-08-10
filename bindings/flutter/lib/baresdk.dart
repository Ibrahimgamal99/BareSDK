/// baresdk — Dart/Flutter FFI wrapper.
///
/// Quick start:
///
/// ```dart
/// import 'package:baresdk/baresdk.dart';
///
/// final sdk = await BareSDK.start(
///   config: const BareSDKConfig(statsIntervalMs: 5000),
/// );
/// final account = sdk.createAccount(
///   'alice@pbx.example.com', 'secret',
///   config: const AccountConfig(
///     serverUrl: 'wss://pbx.example.com:8089/ws',   // WS/WSS need a URL
///     mediaEnc: MediaEncryption.dtlsSrtp,
///     audioCodecs: ['opus', 'g722', 'ulaw'],
///   ),
/// );
/// account.register();
///
/// account.events.listen((ev) {
///   if (ev is RegStateEvent && ev.state == RegState.registered) {
///     account.call('bob@pbx.example.com');
///   } else if (ev is IncomingCallEvent) {
///     ev.call.answer();
///   }
/// });
/// ```
library baresdk;

import 'dart:async';
import 'dart:ffi';
import 'package:ffi/ffi.dart';

import 'src/config.dart';
import 'src/enums.dart';
import 'src/ffi_bindings.dart';
import 'src/platform_channel.dart';
import 'src/sdk.dart' as internal;

export 'src/config.dart' show BareSDKConfig, AccountConfig, OpusConfig;
export 'src/enums.dart';
export 'src/platform_channel.dart' show BareSDKPlatform;

// ── Event types ─────────────────────────────────────────────────────────────

abstract class BareSDKEvent {}

class RegStateEvent extends BareSDKEvent {
  final RegState state;
  final BareSDKError error;
  final String? errorStr;
  final int retryAttempt;
  final int retryDelayMs;
  RegStateEvent(this.state, this.error, this.errorStr,
      {this.retryAttempt = 0, this.retryDelayMs = 0});
}

class IncomingCallEvent extends BareSDKEvent {
  final Call call;
  final String fromUri;
  final String? displayName;
  IncomingCallEvent(this.call, this.fromUri, this.displayName);
}

class CallStateEvent extends BareSDKEvent {
  final Call call;
  final CallState state;
  final BareSDKError error;
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

/// Periodic per-call media statistics (see `baresdk_ev_media_stats_t`).
class MediaStats {
  final Call call;
  // Packet counters
  final int packetsSent;
  final int packetsReceived;
  final int packetsLost;
  final int packetsLostRx;
  final int bytesSent;
  final int bytesReceived;
  final int txErrors;
  final int rxErrors;
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
  final int jitterBufferTargetMs;
  final bool jitterBufferAdaptive;
  // PLC
  final int plcFrames;
  final double plcRatio;
  // Bandwidth
  final int bandwidthTx;
  final int bandwidthRx;
  final int avgBandwidthTx;
  final int avgBandwidthRx;
  // MOS
  final double mosLq;
  final double mosCq;
  final double mosLqRx;
  final double mosCqRx;
  final int mosMethod;
  // Codec
  final String codec;
  final int codecClockRate;
  final int codecSampleRate;
  final int codecChannels;
  final int payloadType;
  // Audio level
  final double audioLevelDbov;
  final double micLevelDbov;
  // Stream identity
  final int ssrcTx;
  final int ssrcRx;
  final String remoteAddr;
  // Session history
  final double mosLqMin;
  final double mosLqAvg;
  final int statsTick;
  final int callDurationMs;

  /// True on the last stats event before call teardown.
  final bool isFinal;

  MediaStats({
    required this.call,
    required this.packetsSent,
    required this.packetsReceived,
    required this.packetsLost,
    required this.packetsLostRx,
    required this.bytesSent,
    required this.bytesReceived,
    required this.txErrors,
    required this.rxErrors,
    required this.lossPct,
    required this.lossPctRx,
    required this.jitterMs,
    required this.txJitterMs,
    required this.rttMs,
    required this.jitterBufferMs,
    required this.jitterBufferLoad,
    required this.latePackets,
    required this.discardedPackets,
    required this.jitterBufferTargetMs,
    required this.jitterBufferAdaptive,
    required this.plcFrames,
    required this.plcRatio,
    required this.bandwidthTx,
    required this.bandwidthRx,
    required this.avgBandwidthTx,
    required this.avgBandwidthRx,
    required this.mosLq,
    required this.mosCq,
    required this.mosLqRx,
    required this.mosCqRx,
    required this.mosMethod,
    required this.codec,
    required this.codecClockRate,
    required this.codecSampleRate,
    required this.codecChannels,
    required this.payloadType,
    required this.audioLevelDbov,
    required this.micLevelDbov,
    required this.ssrcTx,
    required this.ssrcRx,
    required this.remoteAddr,
    required this.mosLqMin,
    required this.mosLqAvg,
    required this.statsTick,
    required this.callDurationMs,
    required this.isFinal,
  });
}

class MediaStatsEvent extends BareSDKEvent {
  final MediaStats stats;
  MediaStatsEvent(this.stats);

  Call get call => stats.call;
}

/// A call-quality threshold was crossed (or recovered).
/// Thresholds are set in [BareSDKConfig] (`mosAlertThreshold`, ...).
class QualityAlertEvent extends BareSDKEvent {
  final Call call;
  final QualityIssue issue;
  final double value;
  final double threshold;

  /// True when the metric returned to the good side of the threshold.
  final bool recovering;
  QualityAlertEvent(
      this.call, this.issue, this.value, this.threshold, this.recovering);
}

/// Incoming REFER — the peer asks us to transfer this call.
class TransferRequestEvent extends BareSDKEvent {
  final Call call;
  final String referToUri;

  /// True = attended transfer (REFER carries Replaces).
  final bool hasReplaces;
  TransferRequestEvent(this.call, this.referToUri, this.hasReplaces);
}

/// Non-fatal registrar warning.
class RegistrarWarningEvent extends BareSDKEvent {
  final String message;
  RegistrarWarningEvent(this.message);
}

/// Result of an SDP offer/answer negotiation (enable with
/// [BareSDKConfig.traceSdpDiff]).
class SdpNegotiationEvent extends BareSDKEvent {
  final Call call;
  final String? localSdp;
  final String? remoteSdp;
  final String? negotiatedCodec;
  final String? negotiatedCrypto;
  SdpNegotiationEvent(this.call, this.localSdp, this.remoteSdp,
      this.negotiatedCodec, this.negotiatedCrypto);
}

/// Message-waiting indication (voicemail).
class MwiEvent extends BareSDKEvent {
  final bool messagesWaiting;
  final int newVoice;
  final int oldVoice;
  final int newUrgent;
  final int oldUrgent;
  final String? rawBody;
  MwiEvent(this.messagesWaiting, this.newVoice, this.oldVoice, this.newUrgent,
      this.oldUrgent, this.rawBody);
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
  final PresenceStatus status;
  PresenceStateEvent(this.targetUri, this.status);
}

/// Progress of a network handover (Wi-Fi <-> 4G/5G, VPN, dock/undock).
///
/// Delivered on [BareSDK.events]. Most stages are not account-scoped, so
/// they do NOT appear on [Account.events].
class NetworkEvent extends BareSDKEvent {
  final NetworkStage stage;
  final Call? call;
  final String localAddr;
  final int attempt;
  final int maxAttempts;
  final int elapsedMs;

  /// True when the call uses ICE — media recovery is best-effort there.
  final bool ice;
  final BareSDKError error;

  NetworkEvent({
    required this.stage,
    required this.call,
    required this.localAddr,
    required this.attempt,
    required this.maxAttempts,
    required this.elapsedMs,
    required this.ice,
    required this.error,
  });
}

class SipTraceEvent extends BareSDKEvent {
  final MediaDirection direction;
  final String transport;
  final String remoteAddr;
  final String rawMessage;
  final int timestampUs;
  SipTraceEvent(this.direction, this.transport, this.remoteAddr,
      this.rawMessage, this.timestampUs);
}

/// An event type this binding does not know how to decode (newer native
/// library). Carries the raw type id so nothing is silently dropped.
class UnknownEvent extends BareSDKEvent {
  final int rawType;
  UnknownEvent(this.rawType);
}

// ── Call ─────────────────────────────────────────────────────────────────────

class Call {
  final Pointer<baresdk_call> _handle;

  /// The account this call belongs to; null when the SDK could not
  /// resolve it (e.g. events after account destruction).
  final Account? account;

  CallState state = CallState.calling;

  Call._(this._handle, this.account);

  void answer() => internal.nativeBindings.baresdk_call_answer(_handle);
  void hangup() => internal.nativeBindings.baresdk_call_hangup(_handle);
  void hold() => internal.nativeBindings.baresdk_call_hold(_handle);
  void resume() => internal.nativeBindings.baresdk_call_resume(_handle);

  /// True while the call is on local hold.
  bool get isHeld => internal.nativeBindings.baresdk_call_is_held(_handle);

  void mute({bool on = true}) =>
      internal.nativeBindings.baresdk_audio_mute(_handle, on);
  bool get isMuted => internal.nativeBindings.baresdk_audio_is_muted(_handle);
  void muteRx({bool on = true}) =>
      internal.nativeBindings.baresdk_audio_mute_rx(_handle, on);

  /// digit: '0'-'9', '*', '#', 'A'-'D'.
  void sendDtmf(String digit) {
    internal.nativeBindings
        .baresdk_call_send_dtmf(_handle, digit.codeUnitAt(0));
  }

  /// Blind transfer (REFER).
  void transfer(String uri) {
    final p = uri.toNativeUtf8().cast<Char>();
    try {
      internal.nativeBindings.baresdk_call_transfer(_handle, p);
    } finally {
      calloc.free(p);
    }
  }

  /// Attended transfer: bridge this call to [other] (REFER w/ Replaces).
  /// `this` is the call being transferred away; [other] is the established
  /// consultation call.
  void attendedTransfer(Call other) {
    internal.nativeBindings
        .baresdk_call_attended_transfer(_handle, other._handle);
  }

  /// Add a custom SIP header to subsequent requests in this dialog.
  void addHeader(String name, String value) {
    final np = name.toNativeUtf8().cast<Char>();
    final vp = value.toNativeUtf8().cast<Char>();
    try {
      internal.nativeBindings.baresdk_call_add_header(_handle, np, vp);
    } finally {
      calloc.free(np);
      calloc.free(vp);
    }
  }

  /// Synchronously fetch current media stats for this call.
  MediaStats? getStats() {
    final out = calloc<baresdk_ev_media_stats_t>();
    try {
      final err =
          internal.nativeBindings.baresdk_call_get_stats(_handle, out);
      if (err != 0) return null;
      return _decodeStats(out.ref, this);
    } finally {
      calloc.free(out);
    }
  }

  /// Record both call directions mixed into one WAV file (PCM S16LE).
  int recordStart(String path) {
    final p = path.toNativeUtf8().cast<Char>();
    try {
      return internal.nativeBindings.baresdk_call_record_start(_handle, p);
    } finally {
      calloc.free(p);
    }
  }

  int recordStop() =>
      internal.nativeBindings.baresdk_call_record_stop(_handle);

  /// Change DSCP/TOS on the RTP socket (46 = EF voice, 0 = best effort).
  void setDscpRtp(int dscp) {
    internal.nativeBindings.baresdk_call_set_dscp_rtp(_handle, dscp);
  }

  Pointer<baresdk_call> get handle => _handle;
}

// ── Account ──────────────────────────────────────────────────────────────────

class Account {
  final Pointer<baresdk_account> _handle;
  final BareSDK _sdk;
  final StreamController<BareSDKEvent> _ctrl =
      StreamController<BareSDKEvent>.broadcast();

  Account._(this._handle, this._sdk);

  /// Account-scoped events (registration, this account's calls, ...).
  Stream<BareSDKEvent> get events => _ctrl.stream;

  void _add(BareSDKEvent ev) {
    if (!_ctrl.isClosed) _ctrl.add(ev);
  }

  void register() =>
      internal.nativeBindings.baresdk_account_register(_handle);

  void unregister() =>
      internal.nativeBindings.baresdk_account_unregister(_handle);

  /// Start an outgoing call.
  Call call(String uri) {
    final uriPtr = uri.toNativeUtf8().cast<Char>();
    final out = calloc<Pointer<baresdk_call>>();
    try {
      final err = internal.nativeBindings
          .baresdk_call_invite(_handle, uriPtr, out);
      if (err != 0 || out.value == nullptr) {
        throw StateError(
            'baresdk_call_invite failed: ${_sdk.strerror(err)} ($err)');
      }
      return _sdk._trackCall(out.value, this);
    } finally {
      calloc.free(out);
      calloc.free(uriPtr);
    }
  }

  // ── Registration retry / reconnection ──────────────────────────────────

  /// Override the retry/backoff policy for this account.
  /// [maxAttempts] 0 = retry forever.
  int setRetryPolicy({
    required int initialMs,
    required int maxMs,
    required double backoff,
    int maxAttempts = 0,
  }) =>
      internal.nativeBindings.baresdk_account_set_retry_policy(
          _handle, initialMs, maxMs, backoff, maxAttempts);

  /// Cancel a pending retry timer (account stays FAILED until [register]).
  int cancelRetry() =>
      internal.nativeBindings.baresdk_account_cancel_retry(_handle);

  /// Skip the current backoff delay and re-register immediately.
  int retryNow() =>
      internal.nativeBindings.baresdk_account_retry_now(_handle);

  // ── Push ────────────────────────────────────────────────────────────────

  /// Update the RFC 8599 push token at runtime; null clears push params.
  int setPushToken(String? token) {
    final p = token == null
        ? Pointer<Char>.fromAddress(0)
        : token.toNativeUtf8().cast<Char>();
    try {
      return internal.nativeBindings
          .baresdk_account_set_push_token(_handle, p);
    } finally {
      if (p.address != 0) calloc.free(p);
    }
  }

  // ── Custom headers ─────────────────────────────────────────────────────

  /// Header on all outgoing requests for this account.
  void addHeader(String name, String value) =>
      _withHeader(name, value, internal.nativeBindings.baresdk_account_add_header);

  /// Header on REGISTER only (not leaked to call peers).
  void addRegisterHeader(String name, String value) => _withHeader(
      name, value, internal.nativeBindings.baresdk_account_add_register_header);

  void _withHeader(String name, String value,
      int Function(Pointer<baresdk_account>, Pointer<Char>, Pointer<Char>) f) {
    final np = name.toNativeUtf8().cast<Char>();
    final vp = value.toNativeUtf8().cast<Char>();
    try {
      f(_handle, np, vp);
    } finally {
      calloc.free(np);
      calloc.free(vp);
    }
  }

  // ── Messaging / presence ───────────────────────────────────────────────

  void sendMessage(String to, String body,
      {String contentType = 'text/plain'}) {
    final tp = to.toNativeUtf8().cast<Char>();
    final bp = body.toNativeUtf8().cast<Char>();
    final cp = contentType.toNativeUtf8().cast<Char>();
    try {
      internal.nativeBindings.baresdk_message_send(_handle, tp, bp, cp);
    } finally {
      calloc.free(tp);
      calloc.free(bp);
      calloc.free(cp);
    }
  }

  int subscribePresence(String targetUri) =>
      _withUri(targetUri,
          internal.nativeBindings.baresdk_account_subscribe_presence);

  int unsubscribePresence(String targetUri) =>
      _withUri(targetUri,
          internal.nativeBindings.baresdk_account_unsubscribe_presence);

  int publishPresence(PresenceStatus status) => internal.nativeBindings
      .baresdk_account_publish_presence(_handle, status.raw);

  int _withUri(String uri,
      int Function(Pointer<baresdk_account>, Pointer<Char>) f) {
    final p = uri.toNativeUtf8().cast<Char>();
    try {
      return f(_handle, p);
    } finally {
      calloc.free(p);
    }
  }

  void destroy() {
    internal.nativeBindings.baresdk_account_destroy(_handle);
    _sdk._accounts.remove(_handle.address);
    _ctrl.close();
  }
}

// ── SDK ──────────────────────────────────────────────────────────────────────

/// Global event trampoline — must be static/top-level for NativeCallable.
void _cEventCb(Pointer<baresdk_event_t> ev, Pointer<Void> ud) {
  final sdk = BareSDK._instance;
  if (sdk == null) {
    // SDK torn down while events were in flight — still must release.
    internal.nativeBindings.baresdk_event_release(ev);
    return;
  }
  sdk._dispatchEvent(ev);
}

class BareSDK {
  static BareSDK? _instance;

  /// The live instance, if [BareSDK] has been started.
  static BareSDK? get instance => _instance;

  final Map<int, Account> _accounts = {};
  final Map<int, Call> _calls = {};
  final StreamController<BareSDKEvent> _ctrl =
      StreamController<BareSDKEvent>.broadcast();
  late final NativeCallable<
          Void Function(Pointer<baresdk_event_t>, Pointer<Void>)> _nativeCb;
  final bool _manageAudioSession;
  bool _audioSessionActive = false;

  /// Every event from the stack, including the ones that belong to no
  /// account (network handover, log). Account-scoped events also continue
  /// to arrive on [Account.events].
  Stream<BareSDKEvent> get events => _ctrl.stream;

  /// Start the SDK — the recommended entry point.
  ///
  /// On Android this additionally:
  ///  - fills `tmp_dir` with the app cache dir (required there),
  ///  - forces `netMonitorIntervalSeconds: 0` and instead drives handover
  ///    from ConnectivityManager callbacks,
  ///  - requests/abandons voice audio focus around calls
  ///    (disable with [manageAudioSession] = false).
  static Future<BareSDK> start({
    BareSDKConfig config = const BareSDKConfig(),
    bool manageAudioSession = true,
    String? libPath,
  }) async {
    BareSDKPlatform.ensureHandler();
    final cacheDir = await BareSDKPlatform.getCacheDir();
    if (cacheDir != null) {
      config = config.copyWith(
        tmpDir: config.tmpDir ?? cacheDir,
        // OS connectivity callbacks replace polling on Android.
        netMonitorIntervalSeconds: 0,
      );
    }
    final sdk = BareSDK._(config, manageAudioSession, libPath);
    BareSDKPlatform.onNetworkChanged = sdk.networkChanged;
    return sdk;
  }

  /// Synchronous constructor for desktop platforms (Linux/Windows/macOS).
  ///
  /// On Android use [BareSDK.start] instead — it provides the required
  /// `tmp_dir` and wires connectivity callbacks. Constructing this directly
  /// on Android without [BareSDKConfig.tmpDir] throws.
  factory BareSDK({
    BareSDKConfig config = const BareSDKConfig(),
    String? libPath,
  }) =>
      BareSDK._(config, false, libPath);

  BareSDK._(BareSDKConfig config, this._manageAudioSession, String? libPath) {
    if (_instance != null) {
      throw StateError(
          'BareSDK is already running (the native stack is a process-wide '
          'singleton). Call shutdown() first.');
    }
    if (libPath != null) internal.setLibPath(libPath);

    _nativeCb = NativeCallable<
        Void Function(Pointer<baresdk_event_t>, Pointer<Void>)>.listener(
      _cEventCb,
    );

    final cfg = calloc<baresdk_config_t>();
    final scope = fillNativeConfig(cfg, config, internal.nativeBindings);
    // Layout guard: baresdk_config_init (C) wrote its compile-time
    // sizeof into struct_size. If the ffigen-generated struct disagrees,
    // field offsets have drifted (e.g. a packed enum widened) and every
    // config write after the drift lands in the wrong place.
    if (cfg.ref.struct_size != sizeOf<baresdk_config_t>()) {
      final cSize = cfg.ref.struct_size;
      scope.free();
      calloc.free(cfg);
      _nativeCb.close();
      throw StateError(
          'baresdk_config_t layout mismatch: native $cSize bytes vs Dart '
          '${sizeOf<baresdk_config_t>()} — regenerate ffi_bindings.dart '
          '(dart run ffigen) against the native library baresdk.h.');
    }
    // The callback runs asynchronously (NativeCallable.listener), so the
    // native side must hand over event ownership; we release in _dispatch.
    cfg.ref.deliver_owned_events = true;
    cfg.ref.event_cb = _nativeCb.nativeFunction;
    cfg.ref.event_userdata = nullptr;
    final err = internal.nativeBindings.baresdk_init(cfg);
    scope.free();
    calloc.free(cfg);
    if (err != 0) {
      _nativeCb.close();
      throw StateError('baresdk_init failed: ${strerror(err)} ($err)');
    }
    _instance = this;
  }

  String get version =>
      internal.nativeBindings.baresdk_version().cast<Utf8>().toDartString();

  /// Human-readable message for a BARESDK_ERR_* code.
  String strerror(int err) =>
      internal.nativeBindings.baresdk_strerror(err).cast<Utf8>().toDartString();

  /// Create a SIP account. Registration starts when you call
  /// [Account.register].
  Account createAccount(String uri, String password,
      {AccountConfig config = const AccountConfig()}) {
    final cfg = calloc<baresdk_account_config_t>();
    final out = calloc<Pointer<baresdk_account>>();
    final scope = fillNativeAccountConfig(cfg, uri, password, config);
    try {
      final err =
          internal.nativeBindings.baresdk_account_create(cfg, out);
      if (err != 0) {
        throw StateError(
            'baresdk_account_create failed: ${strerror(err)} ($err)');
      }
      final handle = out.value;
      final account = Account._(handle, this);
      _accounts[handle.address] = account;
      return account;
    } finally {
      scope.free();
      calloc.free(out);
      calloc.free(cfg);
    }
  }

  // ── Audio devices & processing ──────────────────────────────────────────

  List<AudioDevice> listInputDevices() =>
      _listDevices(internal.nativeBindings.baresdk_audio_list_input_devices);
  List<AudioDevice> listOutputDevices() =>
      _listDevices(internal.nativeBindings.baresdk_audio_list_output_devices);

  List<AudioDevice> _listDevices(
      int Function(Pointer<baresdk_audio_device_t>, int) fn) {
    final buf = calloc<baresdk_audio_device_t>(32);
    try {
      final n = fn(buf, 32);
      final out = <AudioDevice>[];
      for (var i = 0; i < n; i++) {
        final d = buf[i];
        final name = StringBuffer();
        final desc = StringBuffer();
        for (var j = 0; j < 128 && d.name[j] != 0; j++) {
          name.writeCharCode(d.name[j]);
        }
        for (var j = 0; j < 256 && d.description[j] != 0; j++) {
          desc.writeCharCode(d.description[j]);
        }
        out.add(AudioDevice(name.toString(), desc.toString(), d.is_default));
      }
      return out;
    } finally {
      calloc.free(buf);
    }
  }

  /// Select audio input device by name; null = platform default.
  int setInputDevice(String? name) =>
      _withOptStr(name, internal.nativeBindings.baresdk_audio_set_input_device);

  /// Select audio output device by name; null = platform default.
  int setOutputDevice(String? name) => _withOptStr(
      name, internal.nativeBindings.baresdk_audio_set_output_device);

  int _withOptStr(String? s, int Function(Pointer<Char>) f) {
    if (s == null) return f(nullptr);
    final p = s.toNativeUtf8().cast<Char>();
    try {
      return f(p);
    } finally {
      calloc.free(p);
    }
  }

  void setAec(bool enable) => internal.nativeBindings.baresdk_set_aec(enable);
  void setNs(bool enable) => internal.nativeBindings.baresdk_set_ns(enable);
  void setAgc(bool enable) => internal.nativeBindings.baresdk_set_agc(enable);
  void setAecSuppressionLevel(double level) =>
      internal.nativeBindings.baresdk_set_aec_suppression_level(level);
  void setMicGainDb(double db) =>
      internal.nativeBindings.baresdk_set_mic_gain_db(db);
  void setSpeakerGainDb(double db) =>
      internal.nativeBindings.baresdk_set_speaker_gain_db(db);

  void setJitterBuffer(int minMs, int maxMs) =>
      internal.nativeBindings.baresdk_set_jitter_buffer(minMs, maxMs);
  void setJitterBufferType(JitterBufferType type) =>
      internal.nativeBindings.baresdk_set_jitter_buffer_type(type.raw);

  /// Route audio to loudspeaker (Android; no-op elsewhere).
  Future<void> setSpeakerphone(bool on) =>
      BareSDKPlatform.setSpeakerphone(on);

  // ── pcap ────────────────────────────────────────────────────────────────

  int pcapStart(String path) {
    final p = path.toNativeUtf8().cast<Char>();
    try {
      return internal.nativeBindings.baresdk_pcap_start(p);
    } finally {
      calloc.free(p);
    }
  }

  int pcapStop() => internal.nativeBindings.baresdk_pcap_stop();

  // ── Network handover (Wi-Fi <-> 4G/5G) ─────────────────────────────────

  /// Tell the SDK the network may have changed. Safe from any thread.
  /// On Android [BareSDK.start] wires this to ConnectivityManager already.
  int networkChanged() => internal.nativeBindings.baresdk_network_changed();

  /// Interface poll period in seconds; 0 disables polling.
  int networkSetMonitorInterval(int seconds) =>
      internal.nativeBindings.baresdk_network_set_monitor_interval(seconds);

  /// Re-INVITE active calls on handover; optionally hang up on failure.
  int networkSetHandoverPolicy(bool reinviteCalls, bool hangupOnFailure) =>
      internal.nativeBindings
          .baresdk_network_set_handover_policy(reinviteCalls, hangupOnFailure);

  /// Local IP currently in use, or '' when the device has no address.
  String networkLocalAddr() {
    final buf = calloc<Uint8>(64);
    try {
      if (internal.nativeBindings
              .baresdk_network_local_addr(buf.cast<Char>(), 64) !=
          0) {
        return '';
      }
      return buf.cast<Utf8>().toDartString();
    } finally {
      calloc.free(buf);
    }
  }

  /// False while the device has no usable (non-loopback) local address.
  bool networkIsUp() => internal.nativeBindings.baresdk_network_is_up();

  /// Tear down the stack. All accounts and calls are terminated.
  void shutdown() {
    internal.nativeBindings.baresdk_shutdown();
    _nativeCb.close();
    _ctrl.close();
    for (final a in _accounts.values) {
      a._ctrl.close();
    }
    _accounts.clear();
    _calls.clear();
    BareSDKPlatform.onNetworkChanged = null;
    if (identical(_instance, this)) _instance = null;
  }

  // ── internals ───────────────────────────────────────────────────────────

  Call _trackCall(Pointer<baresdk_call> handle, Account? account) {
    return _calls.putIfAbsent(
        handle.address, () => Call._(handle, account));
  }

  void _audioSession(bool active) {
    if (!_manageAudioSession || active == _audioSessionActive) return;
    _audioSessionActive = active;
    // Fire and forget — never block the event dispatcher.
    unawaited(BareSDKPlatform.configureAudioSession(active));
  }

  void _dispatchEvent(Pointer<baresdk_event_t> ev) {
    try {
      _decodeAndRoute(ev);
    } finally {
      internal.nativeBindings.baresdk_event_release(ev);
    }
  }

  void _decodeAndRoute(Pointer<baresdk_event_t> ev) {
    final type = ev.ref.type;

    BareSDKEvent? decoded;
    Account? target;

    switch (type) {
      case baresdk_event_type_t.BARESDK_EV_LOG:
        decoded = LogEvent(_str(ev.ref.u.log.message) ?? '');
        _broadcast(decoded);
        return;

      case baresdk_event_type_t.BARESDK_EV_REG_STATE:
        final r = ev.ref.u.reg;
        decoded = RegStateEvent(
          RegState.fromRaw(r.state),
          BareSDKError.fromRaw(r.error),
          _str(r.error_str),
          retryAttempt: r.retry_attempt,
          retryDelayMs: r.retry_delay_ms,
        );
        target = _accounts[r.account.address];
        break;

      case baresdk_event_type_t.BARESDK_EV_INCOMING_CALL:
        final ic = ev.ref.u.incoming;
        target = _accounts[ic.account.address];
        final call = _trackCall(ic.call, target);
        call.state = CallState.ringing;
        _audioSession(true);
        decoded = IncomingCallEvent(
            call, _str(ic.from_uri) ?? '', _str(ic.display_name));
        break;

      case baresdk_event_type_t.BARESDK_EV_CALL_STATE:
        final cs = ev.ref.u.call_state;
        target = _accounts[cs.account.address];
        final call = _trackCall(cs.call, target);
        final state = CallState.fromRaw(cs.state);
        call.state = state;
        if (state.isTerminal) {
          _calls.remove(cs.call.address);
          if (_calls.isEmpty) _audioSession(false);
        } else {
          _audioSession(true);
        }
        decoded = CallStateEvent(
            call, state, BareSDKError.fromRaw(cs.error), _str(cs.reason));
        break;

      case baresdk_event_type_t.BARESDK_EV_CALL_DTMF:
        final d = ev.ref.u.dtmf;
        final call = _trackCall(d.call, null);
        decoded = CallDtmfEvent(call, String.fromCharCode(d.digit));
        target = call.account;
        break;

      case baresdk_event_type_t.BARESDK_EV_MEDIA_STATS:
        final s = ev.ref.u.stats;
        final call = _trackCall(s.call, null);
        decoded = MediaStatsEvent(_decodeStats(s, call));
        target = call.account;
        break;

      case baresdk_event_type_t.BARESDK_EV_QUALITY_ALERT:
        final q = ev.ref.u.quality_alert;
        final call = _trackCall(q.call, null);
        decoded = QualityAlertEvent(call, QualityIssue.fromRaw(q.issue),
            q.value, q.threshold, q.recovering);
        target = call.account;
        break;

      case baresdk_event_type_t.BARESDK_EV_TRANSFER_REQUEST:
        final t = ev.ref.u.transfer_req;
        target = _accounts[t.account.address];
        final call = _trackCall(t.call, target);
        decoded = TransferRequestEvent(
            call, _str(t.refer_to_uri) ?? '', t.has_replaces);
        break;

      case baresdk_event_type_t.BARESDK_EV_REGISTRAR_WARNING:
        decoded = RegistrarWarningEvent(_str(ev.ref.u.reg_warn.message) ?? '');
        _broadcast(decoded);
        return;

      case baresdk_event_type_t.BARESDK_EV_SDP_NEGOTIATION:
        final sd = ev.ref.u.sdp;
        final call = _trackCall(sd.call, null);
        decoded = SdpNegotiationEvent(call, _str(sd.local_sdp),
            _str(sd.remote_sdp), _str(sd.negotiated_codec),
            _str(sd.negotiated_crypto));
        target = call.account;
        break;

      case baresdk_event_type_t.BARESDK_EV_MWI:
        final m = ev.ref.u.mwi;
        decoded = MwiEvent(m.messages_waiting, m.new_voice, m.old_voice,
            m.new_urgent, m.old_urgent, _str(m.raw_body));
        target = _accounts[m.account.address];
        break;

      case baresdk_event_type_t.BARESDK_EV_MESSAGE:
        final m = ev.ref.u.msg;
        decoded = MessageEvent(_str(m.from_uri) ?? '', _str(m.body) ?? '',
            _str(m.content_type) ?? 'text/plain');
        target = _accounts[m.account.address];
        break;

      case baresdk_event_type_t.BARESDK_EV_PRESENCE_STATE:
        final p = ev.ref.u.presence;
        decoded = PresenceStateEvent(
            _str(p.target_uri) ?? '', PresenceStatus.fromRaw(p.status));
        target = _accounts[p.account.address];
        break;

      case baresdk_event_type_t.BARESDK_EV_SIP_TRACE:
        final t = ev.ref.u.sip_trace;
        decoded = SipTraceEvent(
          MediaDirection.fromRaw(t.dir),
          _str(t.transport) ?? '',
          _str(t.remote_addr) ?? '',
          _str(t.raw_message) ?? '',
          t.timestamp_us,
        );
        break;

      case baresdk_event_type_t.BARESDK_EV_NETWORK:
        final n = ev.ref.u.network;
        decoded = NetworkEvent(
          stage: NetworkStage.fromRaw(n.event),
          call: n.call == nullptr ? null : _trackCall(n.call, null),
          localAddr: _str(n.local_addr) ?? '',
          attempt: n.attempt,
          maxAttempts: n.max_attempts,
          elapsedMs: n.elapsed_ms,
          ice: n.ice,
          error: BareSDKError.fromRaw(n.error),
        );
        if (n.account != nullptr) target = _accounts[n.account.address];
        break;

      default:
        decoded = UnknownEvent(type);
        break;
    }

    // Everything reaches the SDK-wide stream; account-scoped events also go
    // to their own account so existing listeners keep working.
    if (!_ctrl.isClosed) _ctrl.add(decoded);
    target?._add(decoded);
  }

  void _broadcast(BareSDKEvent ev) {
    if (!_ctrl.isClosed) _ctrl.add(ev);
    for (final a in _accounts.values) {
      a._add(ev);
    }
  }
}

// ── helpers ──────────────────────────────────────────────────────────────────

String? _str(Pointer<Char> p) =>
    p == nullptr ? null : p.cast<Utf8>().toDartString();

MediaStats _decodeStats(baresdk_ev_media_stats_t s, Call call) {
  final addr = StringBuffer();
  for (var i = 0; i < 64; i++) {
    final b = s.remote_addr[i];
    if (b == 0) break;
    addr.writeCharCode(b);
  }
  return MediaStats(
    call: call,
    packetsSent: s.packets_sent,
    packetsReceived: s.packets_received,
    packetsLost: s.packets_lost,
    packetsLostRx: s.packets_lost_rx,
    bytesSent: s.bytes_sent,
    bytesReceived: s.bytes_received,
    txErrors: s.tx_errors,
    rxErrors: s.rx_errors,
    lossPct: s.loss_pct,
    lossPctRx: s.loss_pct_rx,
    jitterMs: s.jitter_ms,
    txJitterMs: s.tx_jitter_ms,
    rttMs: s.rtt_ms,
    jitterBufferMs: s.jitter_buffer_ms,
    jitterBufferLoad: s.jitter_buffer_load,
    latePackets: s.late_packets,
    discardedPackets: s.discarded_packets,
    jitterBufferTargetMs: s.jitter_buffer_target_ms,
    jitterBufferAdaptive: s.jitter_buffer_adaptive,
    plcFrames: s.plc_frames,
    plcRatio: s.plc_ratio,
    bandwidthTx: s.bandwidth_kbps_tx,
    bandwidthRx: s.bandwidth_kbps_rx,
    avgBandwidthTx: s.avg_bandwidth_kbps_tx,
    avgBandwidthRx: s.avg_bandwidth_kbps_rx,
    mosLq: s.mos_lq,
    mosCq: s.mos_cq,
    mosLqRx: s.mos_lq_rx,
    mosCqRx: s.mos_cq_rx,
    mosMethod: s.mos_method,
    codec: _str(s.codec_name) ?? '',
    codecClockRate: s.codec_clock_rate,
    codecSampleRate: s.codec_sample_rate,
    codecChannels: s.codec_channels,
    payloadType: s.payload_type,
    audioLevelDbov: s.audio_level_dbov,
    micLevelDbov: s.mic_level_dbov,
    ssrcTx: s.ssrc_tx,
    ssrcRx: s.ssrc_rx,
    remoteAddr: addr.toString(),
    mosLqMin: s.mos_lq_min,
    mosLqAvg: s.mos_lq_avg,
    statsTick: s.stats_tick,
    callDurationMs: s.call_duration_ms,
    isFinal: s.is_final,
  );
}
