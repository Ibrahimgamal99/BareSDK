/// Typed Dart enums mirroring the VoxSDK C enums.
///
/// Every enum carries its C integer as [raw] and can be decoded with
/// `fromRaw()`. Unknown raw values map to a defined fallback rather than
/// throwing, so newer native libraries never crash older Dart code.
library;

import 'ffi_bindings.dart' as c;

/// SIP transport protocol.
enum Transport {
  udp(c.voxsdk_transport_t.VOXSDK_TRANSPORT_UDP),
  tcp(c.voxsdk_transport_t.VOXSDK_TRANSPORT_TCP),
  tls(c.voxsdk_transport_t.VOXSDK_TRANSPORT_TLS),
  ws(c.voxsdk_transport_t.VOXSDK_TRANSPORT_WS),
  wss(c.voxsdk_transport_t.VOXSDK_TRANSPORT_WSS);

  final int raw;
  const Transport(this.raw);
  static Transport fromRaw(int raw) =>
      values.firstWhere((v) => v.raw == raw, orElse: () => udp);
}

/// Media (SRTP) encryption mode.
enum MediaEncryption {
  none(c.voxsdk_media_enc_t.VOXSDK_MEDIA_ENC_NONE),
  sdes(c.voxsdk_media_enc_t.VOXSDK_MEDIA_ENC_SDES),
  dtlsSrtp(c.voxsdk_media_enc_t.VOXSDK_MEDIA_ENC_DTLS_SRTP);

  final int raw;
  const MediaEncryption(this.raw);
  static MediaEncryption fromRaw(int raw) =>
      values.firstWhere((v) => v.raw == raw, orElse: () => none);
}

/// Registration lifecycle state.
enum RegState {
  unregistered(c.voxsdk_reg_state_t.VOXSDK_REG_UNREGISTERED),
  registering(c.voxsdk_reg_state_t.VOXSDK_REG_REGISTERING),
  registered(c.voxsdk_reg_state_t.VOXSDK_REG_REGISTERED),

  /// Terminal: the SDK has stopped trying — wrong credentials, the retry
  /// budget ran out, or the app cancelled the retry.  Needs the app or the
  /// user: [Account.retryNow], or new credentials.
  failed(c.voxsdk_reg_state_t.VOXSDK_REG_FAILED),
  unregistering(c.voxsdk_reg_state_t.VOXSDK_REG_UNREGISTERING),

  /// Transient: the registration is down and the SDK is getting it back on
  /// its own — a retry armed after a timeout or 5xx, a keepalive probe the
  /// proxy stopped answering, or a network handover (Wi-Fi ↔ cellular, VPN,
  /// dock).  Show "Reconnecting…"; there is nothing for the app to do.
  ///
  /// It holds for the whole recovery — the state does not flip back to
  /// [registering] for each attempt — and ends at [registered] or [failed].
  /// One event per attempt: [RegStateEvent.retryAttempt] counts up 1, 2, 3…
  /// for as long as the outage lasts and [RegStateEvent.retryDelayMs] is the
  /// backoff armed for the next one, so the latest event always renders a
  /// complete status line.
  reconnecting(c.voxsdk_reg_state_t.VOXSDK_REG_RECONNECTING);

  final int raw;
  const RegState(this.raw);
  static RegState fromRaw(int raw) =>
      values.firstWhere((v) => v.raw == raw, orElse: () => unregistered);
}

/// Call lifecycle state.
enum CallState {
  calling(c.voxsdk_call_state_t.VOXSDK_CALL_CALLING),
  ringing(c.voxsdk_call_state_t.VOXSDK_CALL_RINGING),
  established(c.voxsdk_call_state_t.VOXSDK_CALL_ESTABLISHED),
  held(c.voxsdk_call_state_t.VOXSDK_CALL_HELD),
  ended(c.voxsdk_call_state_t.VOXSDK_CALL_ENDED),
  cancelled(c.voxsdk_call_state_t.VOXSDK_CALL_CANCELLED),
  failed(c.voxsdk_call_state_t.VOXSDK_CALL_FAILED);

  final int raw;
  const CallState(this.raw);

  /// True for states after which the call object is dead.
  bool get isTerminal => this == ended || this == cancelled || this == failed;

  static CallState fromRaw(int raw) =>
      values.firstWhere((v) => v.raw == raw, orElse: () => failed);
}

/// Buddy presence status.
enum PresenceStatus {
  unknown(c.voxsdk_presence_status_t.VOXSDK_PRESENCE_UNKNOWN),
  open(c.voxsdk_presence_status_t.VOXSDK_PRESENCE_OPEN),
  closed(c.voxsdk_presence_status_t.VOXSDK_PRESENCE_CLOSED),
  busy(c.voxsdk_presence_status_t.VOXSDK_PRESENCE_BUSY);

  final int raw;
  const PresenceStatus(this.raw);
  static PresenceStatus fromRaw(int raw) =>
      values.firstWhere((v) => v.raw == raw, orElse: () => unknown);
}

/// DTMF signalling mode.
enum DtmfMode {
  rfc4733(c.voxsdk_dtmf_mode_t.VOXSDK_DTMF_RFC4733),
  sipInfo(c.voxsdk_dtmf_mode_t.VOXSDK_DTMF_SIP_INFO),
  auto(c.voxsdk_dtmf_mode_t.VOXSDK_DTMF_AUTO);

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

/// What to do with an ICE call whose candidates could not be re-gathered on
/// handover.
///
/// An ICE call is normally migrated with a full RFC 8445 §9 ICE restart — new
/// credentials, a fresh gather on the new interface — which needs nothing from
/// the app and is not affected by this setting.  It applies to the calls the
/// restart could not be performed for, which fall back to a re-INVITE carrying
/// the pre-handover candidates and are marked [NetworkStage.callIceStale].
enum IceHandover {
  /// Send the re-INVITE and let media verification decide (default).
  bestEffort(c.voxsdk_ice_handover_t.VOXSDK_ICE_HANDOVER_BEST_EFFORT),

  /// Try once, then report `callMigrationFailed`.  Preferred when calls are
  /// ICE+TURN over cellular: repeating an offer built from the wrong
  /// candidates only lengthens the silence before the app can redial.
  ///
  /// A call whose ICE was restarted keeps the full retry budget regardless.
  failFast(c.voxsdk_ice_handover_t.VOXSDK_ICE_HANDOVER_FAIL_FAST);

  final int raw;
  const IceHandover(this.raw);
  static IceHandover fromRaw(int raw) =>
      values.firstWhere((v) => v.raw == raw, orElse: () => bestEffort);
}

/// Jitter buffer type.
enum JitterBufferType {
  adaptive(c.voxsdk_jbuf_type_t.VOXSDK_JBUF_ADAPTIVE),
  fixed(c.voxsdk_jbuf_type_t.VOXSDK_JBUF_FIXED);

  final int raw;
  const JitterBufferType(this.raw);
}

/// Push notification provider (RFC 8599 pn-provider).
enum PushProvider {
  none(c.voxsdk_push_provider_t.VOXSDK_PUSH_PROVIDER_NONE),
  apns(c.voxsdk_push_provider_t.VOXSDK_PUSH_PROVIDER_APNS),
  apnsSandbox(c.voxsdk_push_provider_t.VOXSDK_PUSH_PROVIDER_APNS_SANDBOX),
  fcm(c.voxsdk_push_provider_t.VOXSDK_PUSH_PROVIDER_FCM);

  final int raw;
  const PushProvider(this.raw);
}

/// Which metric crossed its threshold in a [QualityAlertEvent].
enum QualityIssue {
  mos(c.voxsdk_quality_issue_t.VOXSDK_QUALITY_MOS),
  loss(c.voxsdk_quality_issue_t.VOXSDK_QUALITY_LOSS),
  jitter(c.voxsdk_quality_issue_t.VOXSDK_QUALITY_JITTER),
  rtt(c.voxsdk_quality_issue_t.VOXSDK_QUALITY_RTT),

  /// No inbound RTP for `VoxSDKConfig.mediaStallMs` while the call is not on
  /// hold — the link is up and the dialog is healthy, but no audio is
  /// arriving.  `value` is the stall duration in ms.  Non-fatal: it fires
  /// again with `recovering = true` when packets resume.  Use
  /// `VoxSDKConfig.rtpTimeoutSeconds` to end such a call instead.
  mediaStall(c.voxsdk_quality_issue_t.VOXSDK_QUALITY_MEDIA_STALL);

  final int raw;
  const QualityIssue(this.raw);
  static QualityIssue fromRaw(int raw) =>
      values.firstWhere((v) => v.raw == raw, orElse: () => mos);
}

/// Media direction for SIP trace / media tap.
enum MediaDirection {
  rx(c.voxsdk_media_dir_t.VOXSDK_MEDIA_DIR_RX),
  tx(c.voxsdk_media_dir_t.VOXSDK_MEDIA_DIR_TX);

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
  changeDetected(c.voxsdk_net_event_t.VOXSDK_NET_CHANGE_DETECTED),
  down(c.voxsdk_net_event_t.VOXSDK_NET_DOWN),
  up(c.voxsdk_net_event_t.VOXSDK_NET_UP),
  transportReset(c.voxsdk_net_event_t.VOXSDK_NET_TRANSPORT_RESET),
  reregistering(c.voxsdk_net_event_t.VOXSDK_NET_REREGISTERING),
  callMigrating(c.voxsdk_net_event_t.VOXSDK_NET_CALL_MIGRATING),
  callMigrateAccepted(c.voxsdk_net_event_t.VOXSDK_NET_CALL_MIGRATE_ACCEPTED),
  callMigrated(c.voxsdk_net_event_t.VOXSDK_NET_CALL_MIGRATED),
  callMigrationFailed(c.voxsdk_net_event_t.VOXSDK_NET_CALL_MIGRATION_FAILED),
  callDeferred(c.voxsdk_net_event_t.VOXSDK_NET_CALL_DEFERRED),
  handoverFailed(c.voxsdk_net_event_t.VOXSDK_NET_HANDOVER_FAILED),

  /// This call has ICE that could not be re-gathered — the ICE restart the
  /// SDK normally performs on handover was not possible for it — so the
  /// re-INVITE carries the pre-handover candidates.  That recovers a direct or
  /// still-valid TURN-relayed path; if media does not resume, the remedy is to
  /// re-place the call.  `VoxSDKConfig.netIceHandover` decides whether to keep
  /// retrying.  An ICE call that was restarted does not emit this.
  callIceStale(c.voxsdk_net_event_t.VOXSDK_NET_CALL_ICE_STALE);

  final int raw;
  const NetworkStage(this.raw);
  static NetworkStage fromRaw(int raw) =>
      values.firstWhere((v) => v.raw == raw, orElse: () => changeDetected);
}

/// VoxSDK error codes (negative ints from the C API).
enum VoxSDKError {
  ok(c.voxsdk_error_t.VOXSDK_OK),
  invalidArgument(c.voxsdk_error_t.VOXSDK_ERR_INVAL),
  outOfMemory(c.voxsdk_error_t.VOXSDK_ERR_NOMEM),
  wrongState(c.voxsdk_error_t.VOXSDK_ERR_STATE),
  dns(c.voxsdk_error_t.VOXSDK_ERR_DNS),
  transport(c.voxsdk_error_t.VOXSDK_ERR_TRANSPORT),
  auth(c.voxsdk_error_t.VOXSDK_ERR_AUTH),
  server5xx(c.voxsdk_error_t.VOXSDK_ERR_SERVER_5XX),
  wsProtocolRejected(c.voxsdk_error_t.VOXSDK_ERR_WS_PROTOCOL_REJECTED),
  timeout(c.voxsdk_error_t.VOXSDK_ERR_TIMEOUT),
  already(c.voxsdk_error_t.VOXSDK_ERR_ALREADY);

  final int raw;
  const VoxSDKError(this.raw);
  static VoxSDKError fromRaw(int raw) =>
      values.firstWhere((v) => v.raw == raw, orElse: () => invalidArgument);
}
