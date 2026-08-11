/// Configuration classes mirroring `baresdk_config_t` and
/// `baresdk_account_config_t`.
library;

import 'dart:ffi';
import 'package:ffi/ffi.dart';

import 'enums.dart';
import 'ffi_bindings.dart' as c;

/// Opus encoder tuning (`baresdk_opus_config_t`).
class OpusConfig {
  /// 0 = auto/VBR; otherwise bits per second, e.g. 32000.
  final int bitrate;

  /// 0-10 CPU trade-off; -1 = opus default (9).
  final int complexity;

  /// Constant bitrate; false = VBR (default).
  final bool cbr;

  /// Discontinuous transmission (silence suppression).
  final bool dtx;

  /// In-band forward error correction.
  final bool fec;

  /// Stereo output; false = mono (default).
  final bool stereo;

  const OpusConfig({
    this.bitrate = 0,
    this.complexity = -1,
    this.cbr = false,
    this.dtx = false,
    this.fec = false,
    this.stereo = false,
  });
}

/// Global stack configuration, applied once at [BareSDK] startup.
///
/// Everything is optional; native defaults (see baresdk.h) apply for any
/// field left at its Dart default.
class BareSDKConfig {
  // ── Transport ──────────────────────────────────────────────────────────
  final Transport transport;
  final String? localIp;
  final int localPort;
  final String? bindInterface;
  final bool preferIpv6;
  final String? sipDomain;

  /// Full server URL — required for WS/WSS and custom paths, e.g.
  /// `wss://pbx.example.com:8089/ws`. Overrides [transport]/[serverHost]/
  /// [serverPort] when set.
  final String? serverUrl;
  final String? serverHost;
  final int serverPort;
  final String? outboundProxy;

  // ── TLS / WSS ──────────────────────────────────────────────────────────
  final String? caCertPath;
  final String? clientCert;
  final String? clientKey;
  final bool verifyServer;

  /// SNI hostname override when the proxy cert differs from the SIP domain.
  final String? sniHostname;
  final String? userAgent;

  // ── WebSocket ──────────────────────────────────────────────────────────
  final String? wsOrigin;

  /// Extra WebSocket handshake headers, each as `"Header: value"`.
  final List<String> wsExtraHeaders;

  /// WS ping interval in ms; 0 = libre default (15 s); 20000-30000 advised.
  final int wsKeepaliveMs;

  // ── NAT ────────────────────────────────────────────────────────────────
  final String? stunServer;
  final String? turnServer;
  final String? turnUser;
  final String? turnPass;
  final bool iceEnabled;
  final bool rtcpMux;

  // ── Media ──────────────────────────────────────────────────────────────
  final MediaEncryption mediaEnc;

  /// Global codec preference, most preferred first, by name: `opus`,
  /// `ulaw`/`g711u`/`pcmu`, `alaw`/`g711a`/`pcma`, `g722`, `g729`,
  /// `g726`/`g726-32`. Matched case-insensitively; an unrecognized name is
  /// passed through to baresip, so any codec a loaded module registers works.
  ///
  /// Applies to every account that does not set [AccountConfig.audioCodecs].
  /// Only the first 8 entries are used. A list where nothing resolves logs a
  /// warning and leaves all codecs on offer.
  final List<String> audioCodecs;
  final int dscpSip;
  final int dscpRtp;

  // ── Audio processing ───────────────────────────────────────────────────
  final AecMode aecMode;
  final bool noiseSuppression;
  final bool autoGainControl;

  /// 0 = none .. 1 = maximum; SUPPRESSOR mode only.
  final double aecSuppressionLevel;

  /// Mic gain dB, clamped [-20, +20]; 0 = unity.
  final double micGainDb;

  /// Speaker gain dB, clamped [-20, +20]; 0 = unity.
  final double speakerGainDb;
  final OpusConfig opus;

  // ── Jitter buffer ──────────────────────────────────────────────────────
  final JitterBufferType jitterBufferType;

  /// 0 = native default (40 ms).
  final int jitterBufferMinMs;

  /// 0 = native default (400 ms).
  final int jitterBufferMaxMs;

  // ── Registration & retry ───────────────────────────────────────────────
  /// 0 = native default (3600 s).
  final int regExpires;

  /// Refresh at N% of expires; 0 = native default (75).
  final int regRefreshPct;

  /// Keepalive interval ms; 0 = transport default.
  final int keepaliveIntervalMs;
  final int regRetryInitialMs;
  final int regRetryMaxMs;
  final double regRetryBackoff;

  /// 0 = retry forever.
  final int regRetryMaxAttempts;

  // ── Quality / observability ────────────────────────────────────────────
  /// Emit [MediaStatsEvent] every N ms; 0 = disabled.
  final int statsIntervalMs;

  /// Fire [QualityAlertEvent] when MOS-LQ drops below this (3.5 advised);
  /// 0 disables.
  final double mosAlertThreshold;

  /// Fire when TX loss % exceeds this (5.0 advised); 0 disables.
  final double lossAlertThreshold;

  /// Fire when RX jitter ms exceeds this (40.0 advised); 0 disables.
  final double jitterAlertThreshold;

  // ── Platform ───────────────────────────────────────────────────────────
  /// Writable temp dir for SDK state. REQUIRED on Android (use
  /// [BareSDK.start], which fills it with the app cache dir automatically).
  final String? tmpDir;

  // ── Tracing / logging ──────────────────────────────────────────────────
  /// Emit [SipTraceEvent] for every SIP message.
  final bool traceSip;

  /// Emit [SdpNegotiationEvent] per negotiation.
  final bool traceSdpDiff;

  /// Live SIP+RTP capture to this pcap file; null = off.
  final String? pcapPath;

  /// 0=err, 1=warn, 2=info, 3=debug.
  final int logLevel;

  // ── Network handover ───────────────────────────────────────────────────
  /// Interface poll period seconds; set 0 on mobile — the plugin drives
  /// [BareSDK.networkChanged] from the OS connectivity callback instead.
  final int netMonitorIntervalSeconds;

  const BareSDKConfig({
    this.transport = Transport.udp,
    this.localIp,
    this.localPort = 0,
    this.bindInterface,
    this.preferIpv6 = false,
    this.sipDomain,
    this.serverUrl,
    this.serverHost,
    this.serverPort = 0,
    this.outboundProxy,
    this.caCertPath,
    this.clientCert,
    this.clientKey,
    this.verifyServer = true,
    this.sniHostname,
    this.userAgent,
    this.wsOrigin,
    this.wsExtraHeaders = const [],
    this.wsKeepaliveMs = 0,
    this.stunServer,
    this.turnServer,
    this.turnUser,
    this.turnPass,
    this.iceEnabled = false,
    this.rtcpMux = true,
    this.mediaEnc = MediaEncryption.none,
    this.audioCodecs = const [],
    this.dscpSip = 0,
    this.dscpRtp = 0,
    this.aecMode = AecMode.suppressor,
    this.noiseSuppression = false,
    this.autoGainControl = false,
    this.aecSuppressionLevel = 1.0,
    this.micGainDb = 0,
    this.speakerGainDb = 0,
    this.opus = const OpusConfig(),
    this.jitterBufferType = JitterBufferType.adaptive,
    this.jitterBufferMinMs = 0,
    this.jitterBufferMaxMs = 0,
    this.regExpires = 0,
    this.regRefreshPct = 0,
    this.keepaliveIntervalMs = 0,
    this.regRetryInitialMs = 0,
    this.regRetryMaxMs = 0,
    this.regRetryBackoff = 0,
    this.regRetryMaxAttempts = 0,
    this.statsIntervalMs = 0,
    this.mosAlertThreshold = 0,
    this.lossAlertThreshold = 0,
    this.jitterAlertThreshold = 0,
    this.tmpDir,
    this.traceSip = false,
    this.traceSdpDiff = false,
    this.pcapPath,
    this.logLevel = 1,
    this.netMonitorIntervalSeconds = 10,
  });

  BareSDKConfig copyWith({String? tmpDir, int? netMonitorIntervalSeconds}) {
    return BareSDKConfig(
      transport: transport,
      localIp: localIp,
      localPort: localPort,
      bindInterface: bindInterface,
      preferIpv6: preferIpv6,
      sipDomain: sipDomain,
      serverUrl: serverUrl,
      serverHost: serverHost,
      serverPort: serverPort,
      outboundProxy: outboundProxy,
      caCertPath: caCertPath,
      clientCert: clientCert,
      clientKey: clientKey,
      verifyServer: verifyServer,
      sniHostname: sniHostname,
      userAgent: userAgent,
      wsOrigin: wsOrigin,
      wsExtraHeaders: wsExtraHeaders,
      wsKeepaliveMs: wsKeepaliveMs,
      stunServer: stunServer,
      turnServer: turnServer,
      turnUser: turnUser,
      turnPass: turnPass,
      iceEnabled: iceEnabled,
      rtcpMux: rtcpMux,
      mediaEnc: mediaEnc,
      audioCodecs: audioCodecs,
      dscpSip: dscpSip,
      dscpRtp: dscpRtp,
      aecMode: aecMode,
      noiseSuppression: noiseSuppression,
      autoGainControl: autoGainControl,
      aecSuppressionLevel: aecSuppressionLevel,
      micGainDb: micGainDb,
      speakerGainDb: speakerGainDb,
      opus: opus,
      jitterBufferType: jitterBufferType,
      jitterBufferMinMs: jitterBufferMinMs,
      jitterBufferMaxMs: jitterBufferMaxMs,
      regExpires: regExpires,
      regRefreshPct: regRefreshPct,
      keepaliveIntervalMs: keepaliveIntervalMs,
      regRetryInitialMs: regRetryInitialMs,
      regRetryMaxMs: regRetryMaxMs,
      regRetryBackoff: regRetryBackoff,
      regRetryMaxAttempts: regRetryMaxAttempts,
      statsIntervalMs: statsIntervalMs,
      mosAlertThreshold: mosAlertThreshold,
      lossAlertThreshold: lossAlertThreshold,
      jitterAlertThreshold: jitterAlertThreshold,
      tmpDir: tmpDir ?? this.tmpDir,
      traceSip: traceSip,
      traceSdpDiff: traceSdpDiff,
      pcapPath: pcapPath,
      logLevel: logLevel,
      netMonitorIntervalSeconds:
          netMonitorIntervalSeconds ?? this.netMonitorIntervalSeconds,
    );
  }
}

/// Per-account configuration (`baresdk_account_config_t`).
class AccountConfig {
  /// SIP transport; ignored when [serverUrl] is set (derived from scheme).
  final Transport transport;

  /// Full server URL for WS/WSS or non-standard paths:
  /// `wss://pbx.example.com:8089/ws`. Overrides transport/host/port.
  final String? serverUrl;

  /// Override the physical server address; null = host from the AOR uri.
  final String? serverHost;
  final int serverPort;

  /// Auth username; null = user part of the AOR uri.
  final String? authUser;
  final String? displayName;

  /// Outbound proxy; null = auto-derived from server.
  final String? outboundProxy;

  final MediaEncryption mediaEnc;
  final bool iceEnabled;

  /// null = inherit the global rtcp_mux setting.
  final bool? rtcpMux;
  final String? stunServer;
  final String? turnServer;
  final String? turnUser;
  final String? turnPass;
  final bool verifyTls;

  final PushProvider pushProvider;
  final String? pushToken;
  final String? pushParam;

  /// Ordered codec preference by name; empty = global config codecs.
  final List<String> audioCodecs;
  final DtmfMode dtmfMode;

  const AccountConfig({
    this.transport = Transport.udp,
    this.serverUrl,
    this.serverHost,
    this.serverPort = 0,
    this.authUser,
    this.displayName,
    this.outboundProxy,
    this.mediaEnc = MediaEncryption.none,
    this.iceEnabled = false,
    this.rtcpMux,
    this.stunServer,
    this.turnServer,
    this.turnUser,
    this.turnPass,
    this.verifyTls = true,
    this.pushProvider = PushProvider.none,
    this.pushToken,
    this.pushParam,
    this.audioCodecs = const [],
    this.dtmfMode = DtmfMode.rfc4733,
  });
}

// ── Native marshalling helpers (internal) ─────────────────────────────────

/// Tracks native allocations so they can be freed after the C call.
class NativeScope {
  final List<Pointer<NativeType>> _allocs = [];

  Pointer<Char> str(String? s) {
    if (s == null) return nullptr;
    final p = s.toNativeUtf8().cast<Char>();
    _allocs.add(p);
    return p;
  }

  /// NULL-terminated `const char **` from a string list; nullptr if empty.
  Pointer<Pointer<Char>> strv(List<String> list) {
    if (list.isEmpty) return nullptr;
    final arr = calloc<Pointer<Char>>(list.length + 1);
    for (var i = 0; i < list.length; i++) {
      arr[i] = str(list[i]);
    }
    arr[list.length] = nullptr;
    _allocs.add(arr);
    return arr;
  }

  void free() {
    for (final p in _allocs) {
      calloc.free(p);
    }
    _allocs.clear();
  }
}

/// Copy codec names into a fixed `char[8][32]` native array, returning how
/// many entries were written. Extra codecs past the 8th, and characters past
/// the 31st of a name, are dropped — the native array cannot hold them.
int writeCodecNamesInto(Array<Array<Char>> names, List<String> codecs) {
  final count = codecs.length > 8 ? 8 : codecs.length;
  for (var i = 0; i < count; i++) {
    final bytes = codecs[i].codeUnits;
    final len = bytes.length > 31 ? 31 : bytes.length;
    for (var j = 0; j < len; j++) {
      names[i][j] = bytes[j];
    }
    names[i][len] = 0;
  }
  return count;
}

/// Copy codec names into the fixed `char[8][32]` array of an account config.
void writeCodecNames(c.baresdk_account_config_t cfg, List<String> codecs) {
  cfg.audio_codec_name_count =
      writeCodecNamesInto(cfg.audio_codec_names, codecs);
}

/// Populate a native `baresdk_config_t` from [BareSDKConfig].
/// Returns a [NativeScope] the caller must free after `baresdk_init`
/// (the SDK deep-copies all config strings).
NativeScope fillNativeConfig(
    Pointer<c.baresdk_config_t> cfg, BareSDKConfig conf, c.BareSDKBindings b) {
  final scope = NativeScope();
  b.baresdk_config_init(cfg);
  final r = cfg.ref;

  r.transport = conf.transport.raw;
  r.local_ip = scope.str(conf.localIp);
  r.local_port = conf.localPort;
  r.bind_interface = scope.str(conf.bindInterface);
  r.prefer_ipv6 = conf.preferIpv6;
  r.sip_domain = scope.str(conf.sipDomain);
  r.server_url = scope.str(conf.serverUrl);
  r.server_host = scope.str(conf.serverHost);
  r.server_port = conf.serverPort;
  r.outbound_proxy = scope.str(conf.outboundProxy);

  r.ca_cert_path = scope.str(conf.caCertPath);
  r.client_cert = scope.str(conf.clientCert);
  r.client_key = scope.str(conf.clientKey);
  r.verify_server = conf.verifyServer;
  r.sni_hostname = scope.str(conf.sniHostname);
  r.user_agent = scope.str(conf.userAgent);

  r.ws_origin = scope.str(conf.wsOrigin);
  r.ws_extra_headers = scope.strv(conf.wsExtraHeaders);
  r.ws_keepalive_ms = conf.wsKeepaliveMs;

  r.stun_server = scope.str(conf.stunServer);
  r.turn_server = scope.str(conf.turnServer);
  r.turn_user = scope.str(conf.turnUser);
  r.turn_pass = scope.str(conf.turnPass);
  r.ice_enabled = conf.iceEnabled;
  r.rtcp_mux = conf.rtcpMux;

  r.media_enc = conf.mediaEnc.raw;
  if (conf.audioCodecs.isNotEmpty) {
    r.audio_codec_name_count =
        writeCodecNamesInto(r.audio_codec_names, conf.audioCodecs);
  }
  r.dscp_sip = conf.dscpSip;
  r.dscp_rtp = conf.dscpRtp;

  r.aec_mode = conf.aecMode.raw;
  r.ns = conf.noiseSuppression;
  r.agc = conf.autoGainControl;
  r.aec_suppression_level = conf.aecSuppressionLevel;
  r.mic_gain_db = conf.micGainDb;
  r.speaker_gain_db = conf.speakerGainDb;

  r.opus.bitrate = conf.opus.bitrate;
  r.opus.complexity = conf.opus.complexity;
  r.opus.cbr = conf.opus.cbr;
  r.opus.dtx = conf.opus.dtx;
  r.opus.fec = conf.opus.fec;
  r.opus.stereo = conf.opus.stereo;

  r.jbuf_type = conf.jitterBufferType.raw;
  if (conf.jitterBufferMinMs > 0) r.jitter_buffer_min_ms = conf.jitterBufferMinMs;
  if (conf.jitterBufferMaxMs > 0) r.jitter_buffer_max_ms = conf.jitterBufferMaxMs;

  if (conf.regExpires > 0) r.reg_expires = conf.regExpires;
  if (conf.regRefreshPct > 0) r.reg_refresh_pct = conf.regRefreshPct;
  if (conf.keepaliveIntervalMs > 0) r.keepalive_interval = conf.keepaliveIntervalMs;
  if (conf.regRetryInitialMs > 0) r.reg_retry_initial_ms = conf.regRetryInitialMs;
  if (conf.regRetryMaxMs > 0) r.reg_retry_max_ms = conf.regRetryMaxMs;
  if (conf.regRetryBackoff > 0) r.reg_retry_backoff = conf.regRetryBackoff;
  if (conf.regRetryMaxAttempts > 0) {
    r.reg_retry_max_attempts = conf.regRetryMaxAttempts;
  }

  r.stats_interval_ms = conf.statsIntervalMs;
  r.mos_alert_threshold = conf.mosAlertThreshold;
  r.loss_alert_threshold = conf.lossAlertThreshold;
  r.jitter_alert_threshold = conf.jitterAlertThreshold;

  r.tmp_dir = scope.str(conf.tmpDir);
  r.trace_sip = conf.traceSip;
  r.trace_sdp_diff = conf.traceSdpDiff;
  r.pcap_path = scope.str(conf.pcapPath);
  r.log_level = conf.logLevel;
  r.net_monitor_interval_s = conf.netMonitorIntervalSeconds;

  return scope;
}

/// Populate a native `baresdk_account_config_t` from [AccountConfig].
/// Returns a [NativeScope] the caller must free after `baresdk_account_create`.
NativeScope fillNativeAccountConfig(Pointer<c.baresdk_account_config_t> cfg,
    String uri, String password, AccountConfig conf) {
  final scope = NativeScope();
  final r = cfg.ref;

  r.uri = scope.str(uri);
  r.password = scope.str(password);
  r.transport = conf.transport.raw;
  r.server_url = scope.str(conf.serverUrl);
  r.server_host = scope.str(conf.serverHost);
  r.server_port = conf.serverPort;
  r.auth_user = scope.str(conf.authUser);
  r.display_name = scope.str(conf.displayName);
  r.outbound_proxy = scope.str(conf.outboundProxy);

  r.media_enc = conf.mediaEnc.raw;
  r.ice_enabled = conf.iceEnabled;
  if (conf.rtcpMux != null) {
    r.rtcp_mux = conf.rtcpMux!;
    r.rtcp_mux_set = true;
  }
  r.stun_server = scope.str(conf.stunServer);
  r.turn_server = scope.str(conf.turnServer);
  r.turn_user = scope.str(conf.turnUser);
  r.turn_pass = scope.str(conf.turnPass);
  r.verify_tls = conf.verifyTls;

  r.push_provider = conf.pushProvider.raw;
  r.push_token = scope.str(conf.pushToken);
  r.push_param = scope.str(conf.pushParam);

  if (conf.audioCodecs.isNotEmpty) {
    writeCodecNames(r, conf.audioCodecs);
  }
  r.dtmf_mode = conf.dtmfMode.raw;

  return scope;
}
