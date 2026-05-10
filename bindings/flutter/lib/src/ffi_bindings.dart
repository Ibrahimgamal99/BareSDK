// AUTO-GENERATED Dart FFI bindings for baresdk.
// Generated from include/baresdk.h — do not edit manually.
// Regenerate: dart run ffigen --config ffigen.yaml

import 'dart:ffi';
import 'package:ffi/ffi.dart';

// ── Opaque handles ────────────────────────────────────────────────────────────

final class baresdk_account extends Opaque {}
final class baresdk_call extends Opaque {}

// ── Enumerations ──────────────────────────────────────────────────────────────

abstract class baresdk_transport_t {
  static const int BARESDK_TRANSPORT_UDP = 0;
  static const int BARESDK_TRANSPORT_TCP = 1;
  static const int BARESDK_TRANSPORT_TLS = 2;
  static const int BARESDK_TRANSPORT_WS = 3;
  static const int BARESDK_TRANSPORT_WSS = 4;
}

abstract class baresdk_media_enc_t {
  static const int BARESDK_MEDIA_ENC_NONE = 0;
  static const int BARESDK_MEDIA_ENC_SDES = 1;
  static const int BARESDK_MEDIA_ENC_DTLS_SRTP = 2;
}

abstract class baresdk_codec_t {
  static const int BARESDK_CODEC_OPUS = 0;
  static const int BARESDK_CODEC_PCMU = 1;
  static const int BARESDK_CODEC_PCMA = 2;
  static const int BARESDK_CODEC_G722 = 3;
}

abstract class baresdk_mos_method_t {
  static const int BARESDK_MOS_EMODEL = 0;
  static const int BARESDK_MOS_SIMPLIFIED = 1;
}

abstract class baresdk_media_dir_t {
  static const int BARESDK_MEDIA_DIR_RX = 0;
  static const int BARESDK_MEDIA_DIR_TX = 1;
}

abstract class baresdk_error_t {
  static const int BARESDK_OK = 0;
  static const int BARESDK_ERR_INVAL = -1;
  static const int BARESDK_ERR_NOMEM = -2;
  static const int BARESDK_ERR_STATE = -3;
  static const int BARESDK_ERR_DNS = -4;
  static const int BARESDK_ERR_TRANSPORT = -5;
  static const int BARESDK_ERR_AUTH = -6;
  static const int BARESDK_ERR_SERVER_5XX = -7;
  static const int BARESDK_ERR_WS_PROTOCOL_REJECTED = -8;
  static const int BARESDK_ERR_TIMEOUT = -9;
  static const int BARESDK_ERR_ALREADY = -10;
}

abstract class baresdk_call_state_t {
  static const int BARESDK_CALL_CALLING = 0;
  static const int BARESDK_CALL_RINGING = 1;
  static const int BARESDK_CALL_ESTABLISHED = 2;
  static const int BARESDK_CALL_HELD = 3;
  static const int BARESDK_CALL_ENDED = 4;
  static const int BARESDK_CALL_CANCELLED = 5;
  static const int BARESDK_CALL_FAILED = 6;
}

abstract class baresdk_reg_state_t {
  static const int BARESDK_REG_UNREGISTERED = 0;
  static const int BARESDK_REG_REGISTERING = 1;
  static const int BARESDK_REG_REGISTERED = 2;
  static const int BARESDK_REG_FAILED = 3;
  static const int BARESDK_REG_UNREGISTERING = 4;
}

abstract class baresdk_presence_status_t {
  static const int BARESDK_PRESENCE_UNKNOWN = 0;
  static const int BARESDK_PRESENCE_OPEN = 1;
  static const int BARESDK_PRESENCE_CLOSED = 2;
  static const int BARESDK_PRESENCE_BUSY = 3;
}

abstract class baresdk_100rel_mode_t {
  static const int BARESDK_100REL_DISABLED = 0;
  static const int BARESDK_100REL_ENABLED = 1;
  static const int BARESDK_100REL_REQUIRED = 2;
}

abstract class baresdk_event_type_t {
  static const int BARESDK_EV_LOG = 0;
  static const int BARESDK_EV_REG_STATE = 1;
  static const int BARESDK_EV_INCOMING_CALL = 2;
  static const int BARESDK_EV_CALL_STATE = 3;
  static const int BARESDK_EV_CALL_DTMF = 4;
  static const int BARESDK_EV_SDP_NEGOTIATION = 5;
  static const int BARESDK_EV_SIP_TRACE = 6;
  static const int BARESDK_EV_MEDIA_STATS = 7;
  static const int BARESDK_EV_REGISTRAR_WARNING = 8;
  static const int BARESDK_EV_TRANSFER_REQUEST = 9;
  static const int BARESDK_EV_MWI = 10;
  static const int BARESDK_EV_MESSAGE = 11;
  static const int BARESDK_EV_PRESENCE_STATE = 12;
}

// ── Event payload structs ─────────────────────────────────────────────────────

final class baresdk_ev_log_t extends Struct {
  external Pointer<Char> message;
}

final class baresdk_ev_reg_state_t extends Struct {
  external Pointer<baresdk_account> account;
  @Int32()
  external int state;
  @Int32()
  external int error;
  @Uint32()
  external int retry_attempt;
  @Uint32()
  external int retry_delay_ms;
  external Pointer<Char> error_str;
}

final class baresdk_ev_incoming_call_t extends Struct {
  external Pointer<baresdk_account> account;
  external Pointer<baresdk_call> call;
  external Pointer<Char> from_uri;
  external Pointer<Char> display_name;
}

final class baresdk_ev_call_state_t extends Struct {
  external Pointer<baresdk_account> account;
  external Pointer<baresdk_call> call;
  @Int32()
  external int state;
  @Int32()
  external int error;
  external Pointer<Char> reason;
}

final class baresdk_ev_call_dtmf_t extends Struct {
  external Pointer<baresdk_call> call;
  @Uint8()
  external int digit;
}

final class baresdk_ev_sdp_negotiation_t extends Struct {
  external Pointer<baresdk_call> call;
  external Pointer<Char> local_sdp;
  external Pointer<Char> remote_sdp;
  external Pointer<Char> negotiated_codec;
  external Pointer<Char> negotiated_crypto;
  external Pointer<Pointer<Char>> rejected_codecs;
  external Pointer<Pointer<Char>> warnings;
}

final class baresdk_ev_sip_trace_t extends Struct {
  @Int32()
  external int dir;
  external Pointer<Char> transport;
  external Pointer<Char> remote_addr;
  external Pointer<Char> raw_message;
  @Uint64()
  external int timestamp_us;
}

final class baresdk_ev_media_stats_t extends Struct {
  external Pointer<baresdk_call> call;
  // Packet counters
  @Uint32() external int packets_sent;
  @Uint32() external int packets_received;
  @Uint32() external int packets_lost;
  @Uint32() external int packets_lost_rx;
  @Uint32() external int bytes_sent;
  @Uint32() external int bytes_received;
  @Uint32() external int tx_errors;
  @Uint32() external int rx_errors;
  // Loss
  @Float()  external double loss_pct;
  @Float()  external double loss_pct_rx;
  // Delay / jitter
  @Float()  external double jitter_ms;
  @Float()  external double tx_jitter_ms;
  @Float()  external double rtt_ms;
  // Jitter buffer
  @Uint32() external int jitter_buffer_ms;
  @Uint32() external int jitter_buffer_load;
  @Uint32() external int late_packets;
  @Uint32() external int discarded_packets;
  // Bandwidth
  @Uint32() external int bandwidth_kbps_tx;
  @Uint32() external int bandwidth_kbps_rx;
  @Uint32() external int avg_bandwidth_kbps_tx;
  @Uint32() external int avg_bandwidth_kbps_rx;
  // MOS
  @Float()  external double mos_lq;
  @Float()  external double mos_cq;
  @Int32()  external int    mos_method;
  // Codec
  external Pointer<Char> codec_name;
  @Uint32() external int codec_clock_rate;
  @Uint32() external int codec_sample_rate;
  @Uint8()  external int codec_channels;
  @Int32()  external int payload_type;
  // Audio level
  @Float()  external double audio_level_dbov;
  // Stream identity
  @Uint32() external int ssrc_tx;
  @Uint32() external int ssrc_rx;
  @Array(64) external Array<Uint8> remote_addr;
}

final class baresdk_audio_device_t extends Struct {
  @Array(128) external Array<Uint8> name;
  @Array(256) external Array<Uint8> description;
  @Bool()     external bool         is_default;
}

final class baresdk_ev_registrar_warning_t extends Struct {
  external Pointer<Char> message;
}

final class baresdk_ev_transfer_req_t extends Struct {
  external Pointer<baresdk_account> account;
  external Pointer<baresdk_call> call;
  external Pointer<Char> refer_to_uri;
  @Bool()
  external bool has_replaces;
}

final class baresdk_ev_mwi_t extends Struct {
  external Pointer<baresdk_account> account;
  @Bool()
  external bool messages_waiting;
  @Uint32()
  external int new_voice;
  @Uint32()
  external int old_voice;
  @Uint32()
  external int new_urgent;
  @Uint32()
  external int old_urgent;
  external Pointer<Char> raw_body;
}

final class baresdk_ev_message_t extends Struct {
  external Pointer<baresdk_account> account;
  external Pointer<Char> from_uri;
  external Pointer<Char> body;
  external Pointer<Char> content_type;
}

final class baresdk_ev_presence_state_t extends Struct {
  external Pointer<baresdk_account> account;
  external Pointer<Char> target_uri;
  @Int32()
  external int status;
}

// ── Event union ───────────────────────────────────────────────────────────────

final class baresdk_event_u extends Union {
  external baresdk_ev_log_t log;
  external baresdk_ev_reg_state_t reg;
  external baresdk_ev_incoming_call_t incoming;
  external baresdk_ev_call_state_t call_state;
  external baresdk_ev_call_dtmf_t dtmf;
  external baresdk_ev_sdp_negotiation_t sdp;
  external baresdk_ev_sip_trace_t sip_trace;
  external baresdk_ev_media_stats_t stats;
  external baresdk_ev_registrar_warning_t reg_warn;
  external baresdk_ev_transfer_req_t transfer_req;
  external baresdk_ev_mwi_t mwi;
  external baresdk_ev_message_t msg;
  external baresdk_ev_presence_state_t presence;
}

final class baresdk_event_t extends Struct {
  @Int32()
  external int type;
  external baresdk_event_u u;
}

// ── Callback typedefs ─────────────────────────────────────────────────────────

typedef baresdk_event_cb_t = Void Function(
  Pointer<baresdk_event_t>,
  Pointer<Void>,
);

typedef NativeBaresdkEventCb = NativeFunction<baresdk_event_cb_t>;

// ── Config structs ────────────────────────────────────────────────────────────

final class baresdk_config_t extends Struct {
  @Uint32()
  external int version;
  @IntPtr()
  external int struct_size;

  @Int32()
  external int transport;
  external Pointer<Char> local_ip;
  @Uint16()
  external int local_port;
  external Pointer<Char> bind_interface;
  @Bool()
  external bool prefer_ipv6;
  external Pointer<Char> sip_domain;

  external Pointer<Char> server_url;
  external Pointer<Char> server_host;
  @Uint16()
  external int server_port;
  external Pointer<Char> outbound_proxy;

  external Pointer<Char> ca_cert_path;
  external Pointer<Char> client_cert;
  external Pointer<Char> client_key;
  @Bool()
  external bool verify_server;
  external Pointer<Char> sni_hostname;
  external Pointer<Char> user_agent;

  external Pointer<Char> ws_origin;
  external Pointer<Pointer<Char>> ws_extra_headers;

  external Pointer<Char> stun_server;
  external Pointer<Char> turn_server;
  external Pointer<Char> turn_user;
  external Pointer<Char> turn_pass;
  @Bool()
  external bool ice_enabled;

  @Int32()
  external int media_enc;
  @Array(8)
  external Array<Int32> audio_codecs;
  @Int32()
  external int audio_codec_count;
  @Uint8()
  external int dscp_sip;
  @Uint8()
  external int dscp_rtp;
  @Bool()
  external bool enable_video;

  @Bool()
  external bool aec;
  @Bool()
  external bool ns;
  @Bool()
  external bool agc;

  @Uint32()
  external int reg_expires;
  @Uint32()
  external int reg_refresh_pct;
  @Uint32()
  external int keepalive_interval;
  @Uint32()
  external int reg_retry_initial_ms;
  @Uint32()
  external int reg_retry_max_ms;
  @Float()
  external double reg_retry_backoff;
  @Uint32()
  external int reg_retry_max_attempts;

  @Uint32()
  external int sip_t1_ms;
  @Uint32()
  external int sip_t2_ms;
  @Uint32()
  external int sip_timer_b_ms;
  @Uint32()
  external int sip_timer_f_ms;

  @Bool()
  external bool session_timer_enabled;
  @Uint32()
  external int session_expires_s;
  @Uint32()
  external int session_min_se_s;

  @Uint32()
  external int stats_interval_ms;
  @Int32()
  external int mos_method;

  @Bool()
  external bool trace_sip;
  @Bool()
  external bool trace_sdp_diff;
  external Pointer<Char> pcap_path;

  @Int32()
  external int log_level;
  external Pointer<NativeFunction<baresdk_event_cb_t>> event_cb;
  external Pointer<Void> event_userdata;
}

final class baresdk_account_config_t extends Struct {
  external Pointer<Char> uri;
  external Pointer<Char> password;
  @Int32()
  external int transport;
  external Pointer<Char> server_host;
  @Uint16()
  external int server_port;
  external Pointer<Char> server_url;
  external Pointer<Char> auth_user;
  external Pointer<Char> display_name;
  @Int32()
  external int media_enc;
  @Bool()
  external bool ice_enabled;
  external Pointer<Char> stun_server;
  external Pointer<Char> turn_server;
  external Pointer<Char> turn_user;
  external Pointer<Char> turn_pass;
  external Pointer<Char> outbound;
  @Bool()
  external bool verify_tls;
}

// ── Bindings class ────────────────────────────────────────────────────────────

class BareSDKBindings {
  final DynamicLibrary _lib;

  BareSDKBindings(this._lib);

  late final Pointer<Utf8> Function() baresdk_version =
      _lib.lookupFunction<Pointer<Utf8> Function(), Pointer<Utf8> Function()>(
        'baresdk_version',
      );

  late final void Function(Pointer<baresdk_config_t>) baresdk_config_init =
      _lib.lookupFunction<
        Void Function(Pointer<baresdk_config_t>),
        void Function(Pointer<baresdk_config_t>)
      >('baresdk_config_init');

  late final int Function(Pointer<baresdk_config_t>) baresdk_init =
      _lib.lookupFunction<
        Int32 Function(Pointer<baresdk_config_t>),
        int Function(Pointer<baresdk_config_t>)
      >('baresdk_init');

  late final void Function() baresdk_shutdown =
      _lib.lookupFunction<Void Function(), void Function()>('baresdk_shutdown');

  late final int Function(Pointer<baresdk_account_config_t>, Pointer<Pointer<baresdk_account>>)
      baresdk_account_create = _lib.lookupFunction<
        Int32 Function(Pointer<baresdk_account_config_t>, Pointer<Pointer<baresdk_account>>),
        int Function(Pointer<baresdk_account_config_t>, Pointer<Pointer<baresdk_account>>)
      >('baresdk_account_create');

  late final void Function(Pointer<baresdk_account>) baresdk_account_destroy =
      _lib.lookupFunction<
        Void Function(Pointer<baresdk_account>),
        void Function(Pointer<baresdk_account>)
      >('baresdk_account_destroy');

  late final int Function(Pointer<baresdk_account>) baresdk_account_register =
      _lib.lookupFunction<
        Int32 Function(Pointer<baresdk_account>),
        int Function(Pointer<baresdk_account>)
      >('baresdk_account_register');

  late final int Function(Pointer<baresdk_account>) baresdk_account_unregister =
      _lib.lookupFunction<
        Int32 Function(Pointer<baresdk_account>),
        int Function(Pointer<baresdk_account>)
      >('baresdk_account_unregister');

  late final int Function(Pointer<baresdk_account>, int, int, double, int)
      baresdk_account_set_retry_policy = _lib.lookupFunction<
        Int32 Function(Pointer<baresdk_account>, Uint32, Uint32, Float, Uint32),
        int Function(Pointer<baresdk_account>, int, int, double, int)
      >('baresdk_account_set_retry_policy');

  late final int Function(Pointer<baresdk_account>) baresdk_account_cancel_retry =
      _lib.lookupFunction<
        Int32 Function(Pointer<baresdk_account>),
        int Function(Pointer<baresdk_account>)
      >('baresdk_account_cancel_retry');

  late final int Function(Pointer<baresdk_account>) baresdk_account_retry_now =
      _lib.lookupFunction<
        Int32 Function(Pointer<baresdk_account>),
        int Function(Pointer<baresdk_account>)
      >('baresdk_account_retry_now');

  late final int Function(Pointer<baresdk_account>, Pointer<Char>, Pointer<Char>)
      baresdk_account_add_header = _lib.lookupFunction<
        Int32 Function(Pointer<baresdk_account>, Pointer<Char>, Pointer<Char>),
        int Function(Pointer<baresdk_account>, Pointer<Char>, Pointer<Char>)
      >('baresdk_account_add_header');

  late final int Function(Pointer<baresdk_account>, Pointer<Char>)
      baresdk_account_subscribe_presence = _lib.lookupFunction<
        Int32 Function(Pointer<baresdk_account>, Pointer<Char>),
        int Function(Pointer<baresdk_account>, Pointer<Char>)
      >('baresdk_account_subscribe_presence');

  late final int Function(Pointer<baresdk_account>, Pointer<Char>)
      baresdk_account_unsubscribe_presence = _lib.lookupFunction<
        Int32 Function(Pointer<baresdk_account>, Pointer<Char>),
        int Function(Pointer<baresdk_account>, Pointer<Char>)
      >('baresdk_account_unsubscribe_presence');

  late final int Function(Pointer<baresdk_account>, Pointer<Char>, Pointer<Pointer<baresdk_call>>)
      baresdk_call_invite = _lib.lookupFunction<
        Int32 Function(Pointer<baresdk_account>, Pointer<Char>, Pointer<Pointer<baresdk_call>>),
        int Function(Pointer<baresdk_account>, Pointer<Char>, Pointer<Pointer<baresdk_call>>)
      >('baresdk_call_invite');

  late final int Function(Pointer<baresdk_call>) baresdk_call_answer =
      _lib.lookupFunction<
        Int32 Function(Pointer<baresdk_call>),
        int Function(Pointer<baresdk_call>)
      >('baresdk_call_answer');

  late final int Function(Pointer<baresdk_call>) baresdk_call_hangup =
      _lib.lookupFunction<
        Int32 Function(Pointer<baresdk_call>),
        int Function(Pointer<baresdk_call>)
      >('baresdk_call_hangup');

  late final int Function(Pointer<baresdk_call>) baresdk_call_hold =
      _lib.lookupFunction<
        Int32 Function(Pointer<baresdk_call>),
        int Function(Pointer<baresdk_call>)
      >('baresdk_call_hold');

  late final int Function(Pointer<baresdk_call>) baresdk_call_resume =
      _lib.lookupFunction<
        Int32 Function(Pointer<baresdk_call>),
        int Function(Pointer<baresdk_call>)
      >('baresdk_call_resume');

  late final int Function(Pointer<baresdk_call>, Uint8) baresdk_call_send_dtmf =
      _lib.lookupFunction<
        Int32 Function(Pointer<baresdk_call>, Uint8),
        int Function(Pointer<baresdk_call>, int)
      >('baresdk_call_send_dtmf');

  late final int Function(Pointer<baresdk_call>, Pointer<Char>) baresdk_call_transfer =
      _lib.lookupFunction<
        Int32 Function(Pointer<baresdk_call>, Pointer<Char>),
        int Function(Pointer<baresdk_call>, Pointer<Char>)
      >('baresdk_call_transfer');

  late final int Function(Pointer<baresdk_call>, Pointer<Char>, Pointer<Char>)
      baresdk_call_add_header = _lib.lookupFunction<
        Int32 Function(Pointer<baresdk_call>, Pointer<Char>, Pointer<Char>),
        int Function(Pointer<baresdk_call>, Pointer<Char>, Pointer<Char>)
      >('baresdk_call_add_header');

  late final int Function(Pointer<baresdk_call>, Pointer<baresdk_call>)
      baresdk_call_attended_transfer = _lib.lookupFunction<
        Int32 Function(Pointer<baresdk_call>, Pointer<baresdk_call>),
        int Function(Pointer<baresdk_call>, Pointer<baresdk_call>)
      >('baresdk_call_attended_transfer');

  late final int Function(
    Pointer<baresdk_account>,
    Pointer<Char>,
    Pointer<Char>,
    Pointer<Char>,
  ) baresdk_message_send = _lib.lookupFunction<
    Int32 Function(Pointer<baresdk_account>, Pointer<Char>, Pointer<Char>, Pointer<Char>),
    int Function(Pointer<baresdk_account>, Pointer<Char>, Pointer<Char>, Pointer<Char>)
  >('baresdk_message_send');

  late final int Function(Pointer<baresdk_account>, Int32)
      baresdk_account_publish_presence = _lib.lookupFunction<
        Int32 Function(Pointer<baresdk_account>, Int32),
        int Function(Pointer<baresdk_account>, int)
      >('baresdk_account_publish_presence');

  late final int Function(Pointer<baresdk_account>, Int32)
      baresdk_account_set_100rel = _lib.lookupFunction<
        Int32 Function(Pointer<baresdk_account>, Int32),
        int Function(Pointer<baresdk_account>, int)
      >('baresdk_account_set_100rel');

  late final int Function(Pointer<baresdk_call>, int) baresdk_audio_mute =
      _lib.lookupFunction<
        Int32 Function(Pointer<baresdk_call>, Bool),
        int Function(Pointer<baresdk_call>, int)
      >('baresdk_audio_mute');

  late final int Function(Pointer<baresdk_call>, int) baresdk_audio_mute_rx =
      _lib.lookupFunction<
        Int32 Function(Pointer<baresdk_call>, Bool),
        int Function(Pointer<baresdk_call>, int)
      >('baresdk_audio_mute_rx');

  late final int Function(Pointer<baresdk_audio_device_t>, int)
      baresdk_audio_list_input_devices = _lib.lookupFunction<
        Int32 Function(Pointer<baresdk_audio_device_t>, Int32),
        int Function(Pointer<baresdk_audio_device_t>, int)
      >('baresdk_audio_list_input_devices');

  late final int Function(Pointer<baresdk_audio_device_t>, int)
      baresdk_audio_list_output_devices = _lib.lookupFunction<
        Int32 Function(Pointer<baresdk_audio_device_t>, Int32),
        int Function(Pointer<baresdk_audio_device_t>, int)
      >('baresdk_audio_list_output_devices');

  late final int Function(Pointer<Char>) baresdk_audio_set_input_device =
      _lib.lookupFunction<
        Int32 Function(Pointer<Char>),
        int Function(Pointer<Char>)
      >('baresdk_audio_set_input_device');

  late final int Function(Pointer<Char>) baresdk_audio_set_output_device =
      _lib.lookupFunction<
        Int32 Function(Pointer<Char>),
        int Function(Pointer<Char>)
      >('baresdk_audio_set_output_device');

  late final int Function(Pointer<baresdk_call>, Pointer<NativeFunction>, Pointer<Void>)
      baresdk_call_set_media_tap = _lib.lookupFunction<
        Int32 Function(Pointer<baresdk_call>, Pointer<NativeFunction>, Pointer<Void>),
        int Function(Pointer<baresdk_call>, Pointer<NativeFunction>, Pointer<Void>)
      >('baresdk_call_set_media_tap');

  late final int Function(Pointer<baresdk_call>, Pointer<baresdk_ev_media_stats_t>)
      baresdk_call_get_stats = _lib.lookupFunction<
        Int32 Function(Pointer<baresdk_call>, Pointer<baresdk_ev_media_stats_t>),
        int Function(Pointer<baresdk_call>, Pointer<baresdk_ev_media_stats_t>)
      >('baresdk_call_get_stats');

  late final int Function(Pointer<Char>) baresdk_pcap_start = _lib.lookupFunction<
    Int32 Function(Pointer<Char>),
    int Function(Pointer<Char>)
  >('baresdk_pcap_start');

  late final int Function() baresdk_pcap_stop = _lib.lookupFunction<
    Int32 Function(),
    int Function()
  >('baresdk_pcap_stop');

  late final int Function(Pointer<baresdk_call>, Pointer<Char>)
      baresdk_call_record_start = _lib.lookupFunction<
        Int32 Function(Pointer<baresdk_call>, Pointer<Char>),
        int Function(Pointer<baresdk_call>, Pointer<Char>)
      >('baresdk_call_record_start');

  late final int Function(Pointer<baresdk_call>) baresdk_call_record_stop =
      _lib.lookupFunction<
        Int32 Function(Pointer<baresdk_call>),
        int Function(Pointer<baresdk_call>)
      >('baresdk_call_record_stop');
}
