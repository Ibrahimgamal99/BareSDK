/// Typed Dart enums mirroring the baresdk C enums.
///
/// Every enum carries its C integer as [raw] and can be decoded with
/// `fromRaw()`. Unknown raw values map to a defined fallback rather than
/// throwing, so newer native libraries never crash older Dart code.
library;

import 'ffi_bindings.dart' as c;

/// SIP transport protocol.
enum Transport {
  udp(c.baresdk_transport_t.BARESDK_TRANSPORT_UDP),
  tcp(c.baresdk_transport_t.BARESDK_TRANSPORT_TCP),
  tls(c.baresdk_transport_t.BARESDK_TRANSPORT_TLS),
  ws(c.baresdk_transport_t.BARESDK_TRANSPORT_WS),
  wss(c.baresdk_transport_t.BARESDK_TRANSPORT_WSS);

  final int raw;
  const Transport(this.raw);
  static Transport fromRaw(int raw) =>
      values.firstWhere((v) => v.raw == raw, orElse: () => udp);
}

/// Media (SRTP) encryption mode.
enum MediaEncryption {
  none(c.baresdk_media_enc_t.BARESDK_MEDIA_ENC_NONE),
  sdes(c.baresdk_media_enc_t.BARESDK_MEDIA_ENC_SDES),
  dtlsSrtp(c.baresdk_media_enc_t.BARESDK_MEDIA_ENC_DTLS_SRTP);

  final int raw;
  const MediaEncryption(this.raw);
  static MediaEncryption fromRaw(int raw) =>
      values.firstWhere((v) => v.raw == raw, orElse: () => none);
}

/// Registration lifecycle state.
enum RegState {
  unregistered(c.baresdk_reg_state_t.BARESDK_REG_UNREGISTERED),
  registering(c.baresdk_reg_state_t.BARESDK_REG_REGISTERING),
  registered(c.baresdk_reg_state_t.BARESDK_REG_REGISTERED),
  failed(c.baresdk_reg_state_t.BARESDK_REG_FAILED),
  unregistering(c.baresdk_reg_state_t.BARESDK_REG_UNREGISTERING);

  final int raw;
  const RegState(this.raw);
  static RegState fromRaw(int raw) =>
      values.firstWhere((v) => v.raw == raw, orElse: () => unregistered);
}

/// Call lifecycle state.
enum CallState {
  calling(c.baresdk_call_state_t.BARESDK_CALL_CALLING),
  ringing(c.baresdk_call_state_t.BARESDK_CALL_RINGING),
  established(c.baresdk_call_state_t.BARESDK_CALL_ESTABLISHED),
  held(c.baresdk_call_state_t.BARESDK_CALL_HELD),
  ended(c.baresdk_call_state_t.BARESDK_CALL_ENDED),
  cancelled(c.baresdk_call_state_t.BARESDK_CALL_CANCELLED),
  failed(c.baresdk_call_state_t.BARESDK_CALL_FAILED);

  final int raw;
  const CallState(this.raw);

  /// True for states after which the call object is dead.
  bool get isTerminal => this == ended || this == cancelled || this == failed;

  static CallState fromRaw(int raw) =>
      values.firstWhere((v) => v.raw == raw, orElse: () => failed);
}

/// Buddy presence status.
enum PresenceStatus {
  unknown(c.baresdk_presence_status_t.BARESDK_PRESENCE_UNKNOWN),
  open(c.baresdk_presence_status_t.BARESDK_PRESENCE_OPEN),
  closed(c.baresdk_presence_status_t.BARESDK_PRESENCE_CLOSED),
  busy(c.baresdk_presence_status_t.BARESDK_PRESENCE_BUSY);

  final int raw;
  const PresenceStatus(this.raw);
  static PresenceStatus fromRaw(int raw) =>
      values.firstWhere((v) => v.raw == raw, orElse: () => unknown);
}

/// DTMF signalling mode.
enum DtmfMode {
  rfc4733(c.baresdk_dtmf_mode_t.BARESDK_DTMF_RFC4733),
  sipInfo(c.baresdk_dtmf_mode_t.BARESDK_DTMF_SIP_INFO),
  auto(c.baresdk_dtmf_mode_t.BARESDK_DTMF_AUTO);

  final int raw;
  const DtmfMode(this.raw);
  static DtmfMode fromRaw(int raw) =>
      values.firstWhere((v) => v.raw == raw, orElse: () => rfc4733);
}

/// Echo cancellation backend.
enum AecMode {
  off(0),
  suppressor(1),
  webrtc(2);

  final int raw;
  const AecMode(this.raw);
  static AecMode fromRaw(int raw) =>
      values.firstWhere((v) => v.raw == raw, orElse: () => suppressor);
}

/// What to do with an ICE call whose candidates went stale on handover.
enum IceHandover {
  /// Send the re-INVITE and let media verification decide (default).
  bestEffort(c.baresdk_ice_handover_t.BARESDK_ICE_HANDOVER_BEST_EFFORT),

  /// Try once, then report `callMigrationFailed`.  Preferred when calls are
  /// ICE+TURN over cellular: repeating an offer built from the wrong
  /// candidates only lengthens the silence before the app can redial.
  failFast(c.baresdk_ice_handover_t.BARESDK_ICE_HANDOVER_FAIL_FAST);

  final int raw;
  const IceHandover(this.raw);
  static IceHandover fromRaw(int raw) =>
      values.firstWhere((v) => v.raw == raw, orElse: () => bestEffort);
}

/// Jitter buffer type.
enum JitterBufferType {
  adaptive(c.baresdk_jbuf_type_t.BARESDK_JBUF_ADAPTIVE),
  fixed(c.baresdk_jbuf_type_t.BARESDK_JBUF_FIXED);

  final int raw;
  const JitterBufferType(this.raw);
}

/// Push notification provider (RFC 8599 pn-provider).
enum PushProvider {
  none(c.baresdk_push_provider_t.BARESDK_PUSH_PROVIDER_NONE),
  apns(c.baresdk_push_provider_t.BARESDK_PUSH_PROVIDER_APNS),
  apnsSandbox(c.baresdk_push_provider_t.BARESDK_PUSH_PROVIDER_APNS_SANDBOX),
  fcm(c.baresdk_push_provider_t.BARESDK_PUSH_PROVIDER_FCM);

  final int raw;
  const PushProvider(this.raw);
}

/// Which metric crossed its threshold in a [QualityAlertEvent].
enum QualityIssue {
  mos(c.baresdk_quality_issue_t.BARESDK_QUALITY_MOS),
  loss(c.baresdk_quality_issue_t.BARESDK_QUALITY_LOSS),
  jitter(c.baresdk_quality_issue_t.BARESDK_QUALITY_JITTER),
  rtt(c.baresdk_quality_issue_t.BARESDK_QUALITY_RTT),

  /// No inbound RTP for `BareSDKConfig.mediaStallMs` while the call is not on
  /// hold — the link is up and the dialog is healthy, but no audio is
  /// arriving.  `value` is the stall duration in ms.  Non-fatal: it fires
  /// again with `recovering = true` when packets resume.  Use
  /// `BareSDKConfig.rtpTimeoutSeconds` to end such a call instead.
  mediaStall(c.baresdk_quality_issue_t.BARESDK_QUALITY_MEDIA_STALL);

  final int raw;
  const QualityIssue(this.raw);
  static QualityIssue fromRaw(int raw) =>
      values.firstWhere((v) => v.raw == raw, orElse: () => mos);
}

/// Media direction for SIP trace / media tap.
enum MediaDirection {
  rx(c.baresdk_media_dir_t.BARESDK_MEDIA_DIR_RX),
  tx(c.baresdk_media_dir_t.BARESDK_MEDIA_DIR_TX);

  final int raw;
  const MediaDirection(this.raw);
  static MediaDirection fromRaw(int raw) => raw == tx.raw ? tx : rx;
}

/// Stages of a network handover (Wi-Fi <-> 4G/5G, VPN up/down).
///
/// A typical Wi-Fi -> cellular handover emits, in order:
/// [changeDetected] -> [transportReset] -> [reregistering]
/// -> [callMigrating] -> [callMigrated].
enum NetworkStage {
  changeDetected(c.baresdk_net_event_t.BARESDK_NET_CHANGE_DETECTED),
  down(c.baresdk_net_event_t.BARESDK_NET_DOWN),
  up(c.baresdk_net_event_t.BARESDK_NET_UP),
  transportReset(c.baresdk_net_event_t.BARESDK_NET_TRANSPORT_RESET),
  reregistering(c.baresdk_net_event_t.BARESDK_NET_REREGISTERING),
  callMigrating(c.baresdk_net_event_t.BARESDK_NET_CALL_MIGRATING),
  callMigrateAccepted(c.baresdk_net_event_t.BARESDK_NET_CALL_MIGRATE_ACCEPTED),
  callMigrated(c.baresdk_net_event_t.BARESDK_NET_CALL_MIGRATED),
  callMigrationFailed(c.baresdk_net_event_t.BARESDK_NET_CALL_MIGRATION_FAILED),
  callDeferred(c.baresdk_net_event_t.BARESDK_NET_CALL_DEFERRED),
  handoverFailed(c.baresdk_net_event_t.BARESDK_NET_HANDOVER_FAILED),

  /// This call negotiated ICE, so its gathered candidates are now stale and
  /// cannot be re-gathered mid-call.  The re-INVITE is still sent — it
  /// recovers a direct or still-valid TURN-relayed path — but if media does
  /// not resume, the remedy is to re-place the call.
  /// `BareSDKConfig.netIceHandover` decides whether to keep retrying.
  callIceStale(c.baresdk_net_event_t.BARESDK_NET_CALL_ICE_STALE);

  final int raw;
  const NetworkStage(this.raw);
  static NetworkStage fromRaw(int raw) =>
      values.firstWhere((v) => v.raw == raw, orElse: () => changeDetected);
}

/// baresdk error codes (negative ints from the C API).
enum BareSDKError {
  ok(c.baresdk_error_t.BARESDK_OK),
  invalidArgument(c.baresdk_error_t.BARESDK_ERR_INVAL),
  outOfMemory(c.baresdk_error_t.BARESDK_ERR_NOMEM),
  wrongState(c.baresdk_error_t.BARESDK_ERR_STATE),
  dns(c.baresdk_error_t.BARESDK_ERR_DNS),
  transport(c.baresdk_error_t.BARESDK_ERR_TRANSPORT),
  auth(c.baresdk_error_t.BARESDK_ERR_AUTH),
  server5xx(c.baresdk_error_t.BARESDK_ERR_SERVER_5XX),
  wsProtocolRejected(c.baresdk_error_t.BARESDK_ERR_WS_PROTOCOL_REJECTED),
  timeout(c.baresdk_error_t.BARESDK_ERR_TIMEOUT),
  already(c.baresdk_error_t.BARESDK_ERR_ALREADY);

  final int raw;
  const BareSDKError(this.raw);
  static BareSDKError fromRaw(int raw) =>
      values.firstWhere((v) => v.raw == raw, orElse: () => invalidArgument);
}
