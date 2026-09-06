 
              const char *voxsdk_version(void);
 const char *voxsdk_strerror(int err);

typedef struct voxsdk_account *voxsdk_account_handle_t;
typedef struct voxsdk_call *voxsdk_call_handle_t;

typedef enum {
 VOXSDK_TRANSPORT_UDP = 0,
 VOXSDK_TRANSPORT_TCP,
 VOXSDK_TRANSPORT_TLS,
 VOXSDK_TRANSPORT_WS,
 VOXSDK_TRANSPORT_WSS,
} voxsdk_transport_t;

typedef enum {
 VOXSDK_MEDIA_ENC_NONE = 0,
 VOXSDK_MEDIA_ENC_SDES,
 VOXSDK_MEDIA_ENC_DTLS_SRTP,
} voxsdk_media_enc_t;
typedef enum {
 VOXSDK_CODEC_OPUS = 0,
 VOXSDK_CODEC_PCMU,
 VOXSDK_CODEC_PCMA,
 VOXSDK_CODEC_G722,
 VOXSDK_CODEC_G726_32,
} voxsdk_codec_t;

typedef enum {
 VOXSDK_MOS_EMODEL = 0,
 VOXSDK_MOS_SIMPLIFIED,
} voxsdk_mos_method_t;
typedef uint8_t voxsdk_aec_mode_t;
typedef enum {
 VOXSDK_MEDIA_DIR_RX = 0,
 VOXSDK_MEDIA_DIR_TX,
} voxsdk_media_dir_t;

typedef enum {
 VOXSDK_DTMF_RFC4733 = 0,
 VOXSDK_DTMF_SIP_INFO = 1,
 VOXSDK_DTMF_AUTO = 2,
} voxsdk_dtmf_mode_t;

typedef enum {
 VOXSDK_JBUF_ADAPTIVE = 0,
 VOXSDK_JBUF_FIXED = 1,
} voxsdk_jbuf_type_t;
typedef enum {

 VOXSDK_ICE_HANDOVER_BEST_EFFORT = 0,
 VOXSDK_ICE_HANDOVER_FAIL_FAST,
} voxsdk_ice_handover_t;

typedef struct {
 int bitrate;
 int complexity;
 bool cbr;
 bool dtx;
 bool fec;
 bool stereo;
} voxsdk_opus_config_t;

typedef enum {
 VOXSDK_OK = 0,
 VOXSDK_ERR_INVAL = -1,
 VOXSDK_ERR_NOMEM = -2,
 VOXSDK_ERR_STATE = -3,
 VOXSDK_ERR_DNS = -4,
 VOXSDK_ERR_TRANSPORT = -5,
 VOXSDK_ERR_AUTH = -6,
 VOXSDK_ERR_SERVER_5XX = -7,
 VOXSDK_ERR_WS_PROTOCOL_REJECTED = -8,
 VOXSDK_ERR_TIMEOUT = -9,
 VOXSDK_ERR_ALREADY = -10,
} voxsdk_error_t;

typedef enum {
 VOXSDK_CALL_CALLING = 0,
 VOXSDK_CALL_RINGING,
 VOXSDK_CALL_ESTABLISHED,
 VOXSDK_CALL_HELD,
 VOXSDK_CALL_ENDED,
 VOXSDK_CALL_CANCELLED,
 VOXSDK_CALL_FAILED,
} voxsdk_call_state_t;

typedef enum {
 VOXSDK_REG_UNREGISTERED = 0,
 VOXSDK_REG_REGISTERING,
 VOXSDK_REG_REGISTERED,

 VOXSDK_REG_FAILED,
 VOXSDK_REG_UNREGISTERING,
 VOXSDK_REG_RECONNECTING,
} voxsdk_reg_state_t;

typedef enum {
 VOXSDK_PRESENCE_UNKNOWN = 0,
 VOXSDK_PRESENCE_OPEN,
 VOXSDK_PRESENCE_CLOSED,
 VOXSDK_PRESENCE_BUSY,
} voxsdk_presence_status_t;

typedef enum {
 VOXSDK_100REL_DISABLED = 0,
 VOXSDK_100REL_ENABLED = 1,
 VOXSDK_100REL_REQUIRED = 2,
} voxsdk_100rel_mode_t;
typedef enum {
 VOXSDK_PUSH_PROVIDER_NONE = 0,
 VOXSDK_PUSH_PROVIDER_APNS = 1,
 VOXSDK_PUSH_PROVIDER_APNS_SANDBOX = 2,
 VOXSDK_PUSH_PROVIDER_FCM = 3,
} voxsdk_push_provider_t;

typedef enum {
 VOXSDK_EV_LOG = 0,
 VOXSDK_EV_REG_STATE,
 VOXSDK_EV_INCOMING_CALL,
 VOXSDK_EV_CALL_STATE,
 VOXSDK_EV_CALL_DTMF,
 VOXSDK_EV_SDP_NEGOTIATION,
 VOXSDK_EV_SIP_TRACE,
 VOXSDK_EV_MEDIA_STATS,
 VOXSDK_EV_REGISTRAR_WARNING,
 VOXSDK_EV_TRANSFER_REQUEST,
 VOXSDK_EV_MWI,
 VOXSDK_EV_MESSAGE,
 VOXSDK_EV_PRESENCE_STATE,
 VOXSDK_EV_QUALITY_ALERT,
 VOXSDK_EV_NETWORK,

 VOXSDK_EV_TRANSFER_FAILED,
} voxsdk_event_type_t;
typedef enum {
 VOXSDK_NET_CHANGE_DETECTED = 0,
 VOXSDK_NET_DOWN,
 VOXSDK_NET_UP,
 VOXSDK_NET_TRANSPORT_RESET,
 VOXSDK_NET_REREGISTERING,
 VOXSDK_NET_CALL_MIGRATING,
 VOXSDK_NET_CALL_MIGRATE_ACCEPTED,
 VOXSDK_NET_CALL_MIGRATED,
 VOXSDK_NET_CALL_MIGRATION_FAILED,
 VOXSDK_NET_CALL_DEFERRED,
 VOXSDK_NET_HANDOVER_FAILED,
 VOXSDK_NET_CALL_ICE_STALE,
} voxsdk_net_event_t;
typedef struct {
 voxsdk_net_event_t event;
 voxsdk_call_handle_t call;
 voxsdk_account_handle_t account;
 const char *local_addr;
 uint32_t attempt;
 uint32_t max_attempts;
 uint32_t elapsed_ms;
 bool ice;
 voxsdk_error_t error;
} voxsdk_ev_network_t;
typedef struct {
 voxsdk_account_handle_t account;
 voxsdk_reg_state_t state;
 voxsdk_error_t error;
 uint32_t retry_attempt;
 uint32_t retry_delay_ms;
 const char *error_str;
} voxsdk_ev_reg_state_t;

typedef struct {
 voxsdk_account_handle_t account;
 voxsdk_call_handle_t call;
 const char *from_uri;
 const char *display_name;
} voxsdk_ev_incoming_call_t;

typedef struct {
 voxsdk_account_handle_t account;
 voxsdk_call_handle_t call;
 voxsdk_call_state_t state;
 voxsdk_error_t error;
 const char *reason;
} voxsdk_ev_call_state_t;

typedef struct {
 voxsdk_call_handle_t call;
 char digit;
} voxsdk_ev_call_dtmf_t;

typedef struct {
 voxsdk_call_handle_t call;
 const char *local_sdp;
 const char *remote_sdp;
 const char *negotiated_codec;
 const char *negotiated_crypto;
 const char * const *rejected_codecs;
 const char * const *warnings;
} voxsdk_ev_sdp_negotiation_t;

typedef struct {
 voxsdk_media_dir_t dir;
 const char *transport;
 const char *remote_addr;
 const char *raw_message;
 uint64_t timestamp_us;
} voxsdk_ev_sip_trace_t;

typedef struct {
 voxsdk_call_handle_t call;
 uint32_t packets_sent;
 uint32_t packets_received;
 uint32_t packets_lost;
 uint32_t packets_lost_rx;
 uint32_t bytes_sent;
 uint32_t bytes_received;
 uint32_t tx_errors;
 uint32_t rx_errors;
 float loss_pct;
 float loss_pct_rx;
 float jitter_ms;
 float tx_jitter_ms;
 float rtt_ms;
 uint32_t jitter_buffer_ms;
 uint32_t jitter_buffer_load;
 uint32_t late_packets;
 uint32_t discarded_packets;
 uint32_t jitter_buffer_target_ms;
 bool jitter_buffer_adaptive;
 uint32_t plc_frames;
 float plc_ratio;
 uint32_t bandwidth_kbps_tx;
 uint32_t bandwidth_kbps_rx;
 uint32_t avg_bandwidth_kbps_tx;
 uint32_t avg_bandwidth_kbps_rx;
 float mos_lq;
 float mos_cq;
 float mos_lq_rx;
 float mos_cq_rx;
 voxsdk_mos_method_t mos_method;
 const char *codec_name;
 uint32_t codec_clock_rate;
 uint32_t codec_sample_rate;
 uint8_t codec_channels;
 int payload_type;
 float audio_level_dbov;

 float mic_level_dbov;

 uint32_t ssrc_tx;
 uint32_t ssrc_rx;
 char remote_addr[64];
 float mos_lq_min;
 float mos_lq_avg;
 uint32_t stats_tick;
 uint64_t call_duration_ms;
 bool is_final;
} voxsdk_ev_media_stats_t;

typedef struct {
 const char *message;
} voxsdk_ev_log_t;

typedef struct {
 const char *message;
} voxsdk_ev_registrar_warning_t;
typedef struct {
 voxsdk_account_handle_t account;
 voxsdk_call_handle_t call;
 const char *refer_to_uri;
 bool has_replaces;
 bool auto_followed;
} voxsdk_ev_transfer_req_t;
typedef struct {
 voxsdk_account_handle_t account;
 voxsdk_call_handle_t call;
 const char *reason;
} voxsdk_ev_transfer_failed_t;
typedef struct {
 voxsdk_account_handle_t account;
 bool messages_waiting;
 uint32_t new_voice;
 uint32_t old_voice;
 uint32_t new_urgent;
 uint32_t old_urgent;
 const char *raw_body;
} voxsdk_ev_mwi_t;
typedef struct {
 voxsdk_account_handle_t account;
 const char *from_uri;
 const char *body;
 const char *content_type;
} voxsdk_ev_message_t;
typedef struct {
 voxsdk_account_handle_t account;
 const char *target_uri;
 voxsdk_presence_status_t status;
} voxsdk_ev_presence_state_t;

typedef enum {
 VOXSDK_QUALITY_MOS = 0,
 VOXSDK_QUALITY_LOSS,
 VOXSDK_QUALITY_JITTER,
 VOXSDK_QUALITY_RTT,
 VOXSDK_QUALITY_MEDIA_STALL,
} voxsdk_quality_issue_t;

typedef struct {
 voxsdk_call_handle_t call;
 voxsdk_quality_issue_t issue;
 float value;
 float threshold;
 bool recovering;
} voxsdk_ev_quality_alert_t;

typedef struct {
 voxsdk_event_type_t type;
 union {
  voxsdk_ev_log_t log;
  voxsdk_ev_reg_state_t reg;
  voxsdk_ev_incoming_call_t incoming;
  voxsdk_ev_call_state_t call_state;
  voxsdk_ev_call_dtmf_t dtmf;
  voxsdk_ev_sdp_negotiation_t sdp;
  voxsdk_ev_sip_trace_t sip_trace;
  voxsdk_ev_media_stats_t stats;
  voxsdk_ev_registrar_warning_t reg_warn;
  voxsdk_ev_transfer_req_t transfer_req;
  voxsdk_ev_transfer_failed_t transfer_failed;
  voxsdk_ev_mwi_t mwi;
  voxsdk_ev_message_t msg;
  voxsdk_ev_presence_state_t presence;
  voxsdk_ev_quality_alert_t quality_alert;
  voxsdk_ev_network_t network;
 } u;
} voxsdk_event_t;
typedef void (*voxsdk_event_cb_t)(const voxsdk_event_t *ev, void *userdata);

 void voxsdk_event_release(const voxsdk_event_t *ev);

typedef void (*voxsdk_media_tap_cb_t)(
 voxsdk_call_handle_t call,
 voxsdk_media_dir_t direction,
 const int16_t *pcm,
 size_t samples,
 uint32_t sample_rate,
 uint8_t channels,
 uint64_t timestamp_us,
 void *userdata);

typedef struct {

 uint32_t version;
 size_t struct_size;
 voxsdk_transport_t transport;
 const char *local_ip;
 uint16_t local_port;
 const char *bind_interface;
 bool prefer_ipv6;

 const char *sip_domain;
 const char *server_url;
 const char *server_host;
 uint16_t server_port;

 const char *outbound_proxy;
 const char *ca_cert_path;
 const char *client_cert;
 const char *client_key;
 bool verify_server;
 const char *sni_hostname;
 const char *user_agent;
 const char *ws_origin;
 const char **ws_extra_headers;
 uint32_t ws_keepalive_ms;
 const char *stun_server;
 const char *turn_server;
 const char *turn_user;
 const char *turn_pass;
 bool ice_enabled;
 bool rtcp_mux;
 voxsdk_media_enc_t media_enc;
 voxsdk_codec_t audio_codecs[8];
 int audio_codec_count;
 uint8_t dscp_sip;
 uint8_t dscp_rtp;
 bool enable_video;
 voxsdk_aec_mode_t aec_mode;
 bool ns;
 bool agc;
 float aec_suppression_level;
 float mic_gain_db;
 float speaker_gain_db;
 voxsdk_opus_config_t opus;
 voxsdk_jbuf_type_t jbuf_type;
 uint32_t jitter_buffer_min_ms;
 uint32_t jitter_buffer_max_ms;
 uint32_t reg_expires;
 uint32_t reg_refresh_pct;
 uint32_t keepalive_interval;
 uint32_t reg_retry_initial_ms;
 uint32_t reg_retry_max_ms;
 float reg_retry_backoff;
 uint32_t reg_retry_max_attempts;
 float reg_retry_jitter;
 uint32_t sip_t1_ms;
 uint32_t sip_t2_ms;
 uint32_t sip_timer_b_ms;

 uint32_t sip_timer_f_ms;
 bool session_timer_enabled;
 uint32_t session_expires_s;
 uint32_t session_min_se_s;
 uint32_t stats_interval_ms;
 voxsdk_mos_method_t mos_method;

 float mos_alert_threshold;
 float loss_alert_threshold;
 float jitter_alert_threshold;
 const char *tmp_dir;
 bool trace_sip;
 bool trace_sdp_diff;
 const char *pcap_path;
 int log_level;
 voxsdk_event_cb_t event_cb;
 void *event_userdata;
 uint32_t net_monitor_interval_s;
 uint32_t net_settle_ms;
 bool net_reinvite_calls;

 bool net_hangup_on_migration_failure;
 uint32_t net_verify_ms;

 uint32_t net_max_attempts;
 bool deliver_owned_events;
 char audio_codec_names[8][32];
 int audio_codec_name_count;
 bool platform_audio_activate;
 uint32_t rtp_timeout_s;
 uint32_t media_stall_ms;
 bool adaptive_bitrate;
 uint32_t adapt_min_bitrate;
 uint32_t adapt_max_bitrate;
 float adapt_loss_down_pct;
 float adapt_loss_up_pct;
 uint32_t adapt_recover_ticks;
 uint32_t opus_expected_loss_pct;
 bool keepalive_reregister;
 bool dns_srv_failover;

 voxsdk_ice_handover_t net_ice_handover;
 uint32_t ice_gathering_timeout_ms;

} voxsdk_config_t;

typedef struct {
 const char *uri;

 const char *password;

 voxsdk_transport_t transport;

 const char *server_host;
 uint16_t server_port;

 const char *server_url;

 const char *auth_user;
 const char *display_name;

 voxsdk_media_enc_t media_enc;
 bool ice_enabled;
 bool rtcp_mux;
 bool rtcp_mux_set;
 const char *stun_server;
 const char *turn_server;
 const char *turn_user;
 const char *turn_pass;

 const char *outbound;
 const char *outbound_proxy;
 bool verify_tls;
 voxsdk_push_provider_t push_provider;

 const char *push_token;

 const char *push_param;
 voxsdk_codec_t audio_codecs[8];
 int audio_codec_count;
 char audio_codec_names[8][32];
 int audio_codec_name_count;
 voxsdk_dtmf_mode_t dtmf_mode;

} voxsdk_account_config_t;
 void voxsdk_config_init(voxsdk_config_t *cfg);
 int voxsdk_init(const voxsdk_config_t *cfg);
 bool voxsdk_is_initialized(void);
 int voxsdk_set_event_handler(voxsdk_event_cb_t cb,
                                             void *userdata,
                                             bool deliver_owned_events);

 void voxsdk_shutdown(void);

 int voxsdk_account_create(const voxsdk_account_config_t *cfg,
                             voxsdk_account_handle_t *out);
 void voxsdk_account_destroy(voxsdk_account_handle_t acct);

 int voxsdk_account_register(voxsdk_account_handle_t acct);
 int voxsdk_account_unregister(voxsdk_account_handle_t acct);
 int voxsdk_account_set_retry_policy(voxsdk_account_handle_t acct,
                                                     uint32_t initial_ms,
                                                     uint32_t max_ms,
                                                     float backoff,
                                                     uint32_t max_attempts);
 int voxsdk_account_cancel_retry(voxsdk_account_handle_t acct);

 int voxsdk_account_retry_now(voxsdk_account_handle_t acct);
 int voxsdk_account_set_push_token(voxsdk_account_handle_t acct,
                                                   const char *push_token);
 int voxsdk_account_add_register_header(
        voxsdk_account_handle_t acct,
        const char *name, const char *value);
 int voxsdk_account_add_header(voxsdk_account_handle_t acct,
                                const char *name, const char *value);

 int voxsdk_account_subscribe_presence(voxsdk_account_handle_t acct,
                                        const char *target_uri);
 int voxsdk_account_unsubscribe_presence(voxsdk_account_handle_t acct,
                                          const char *target_uri);
typedef void (*voxsdk_account_iter_fn)(voxsdk_account_handle_t acct, void *arg);
 void voxsdk_account_foreach(voxsdk_account_iter_fn fn, void *arg);
 int voxsdk_account_get_aor(voxsdk_account_handle_t acct,
                                            char *buf, size_t sz);
 voxsdk_reg_state_t voxsdk_account_get_reg_state(
        voxsdk_account_handle_t acct);

 int voxsdk_call_invite(voxsdk_account_handle_t acct,
                         const char *uri,
                         voxsdk_call_handle_t *out);
 int voxsdk_call_answer(voxsdk_call_handle_t call);
 int voxsdk_call_hangup(voxsdk_call_handle_t call);
 int voxsdk_call_reject(voxsdk_call_handle_t call,
                                        uint16_t scode, const char *reason);
 int voxsdk_call_hold(voxsdk_call_handle_t call);
 int voxsdk_call_resume(voxsdk_call_handle_t call);
 bool voxsdk_call_is_held(voxsdk_call_handle_t call);
 int voxsdk_call_send_dtmf(voxsdk_call_handle_t call, char digit);
 int voxsdk_call_transfer(voxsdk_call_handle_t call, const char *uri);
 int voxsdk_call_add_header(voxsdk_call_handle_t call,
                             const char *name, const char *value);
 int voxsdk_call_transfer_accept(voxsdk_call_handle_t call,
                                                 voxsdk_call_handle_t *out);
 int voxsdk_call_transfer_reject(voxsdk_call_handle_t call,
                                                 uint16_t scode,
                                                 const char *reason);

 int voxsdk_call_attended_transfer(voxsdk_call_handle_t call_a,
                                    voxsdk_call_handle_t call_b);
typedef void (*voxsdk_call_iter_fn)(voxsdk_call_handle_t call, void *arg);
 void voxsdk_call_foreach(voxsdk_call_iter_fn fn, void *arg);
 voxsdk_account_handle_t voxsdk_call_get_account(
        voxsdk_call_handle_t call);
 voxsdk_call_state_t voxsdk_call_get_state(
        voxsdk_call_handle_t call);
 int voxsdk_message_send(voxsdk_account_handle_t account,
                          const char *to_uri,
                          const char *body,
                          const char *content_type);

 int voxsdk_account_publish_presence(voxsdk_account_handle_t account,
                                      voxsdk_presence_status_t status);

 int voxsdk_account_set_100rel(voxsdk_account_handle_t account,
                                voxsdk_100rel_mode_t mode);
 int voxsdk_audio_use_external(bool enable);
 int voxsdk_audio_external_push(const int16_t *pcm, size_t nsamp);
 int voxsdk_audio_external_pull(int16_t *pcm, size_t nsamp);
 int voxsdk_audio_external_format(uint32_t *srate, uint8_t *ch,
                                                  uint32_t *ptime);
 bool voxsdk_audio_external_is_active(void);

typedef struct {
 char name[128];
 char description[256];
 bool is_default;
} voxsdk_audio_device_t;

 int voxsdk_audio_list_input_devices(voxsdk_audio_device_t *devices,
                                                     int max_count);
 int voxsdk_audio_list_output_devices(voxsdk_audio_device_t *devices,
                                                      int max_count);
 void voxsdk_set_aec(bool enable);
 int voxsdk_set_aec_mode(voxsdk_aec_mode_t mode);

 void voxsdk_set_aec_suppression_level(float level);
 void voxsdk_set_ns(bool enable);
 void voxsdk_set_agc(bool enable);

 void voxsdk_set_mic_gain_db(float db);

 void voxsdk_set_speaker_gain_db(float db);
 int voxsdk_call_set_dscp_rtp(voxsdk_call_handle_t call,
                                              uint8_t dscp);
 void voxsdk_set_jitter_buffer(uint32_t min_ms, uint32_t max_ms);
 void voxsdk_set_jitter_buffer_type(voxsdk_jbuf_type_t type);
 int voxsdk_call_set_rtp_timeout(voxsdk_call_handle_t call,
                                                 uint32_t seconds);
 int voxsdk_call_set_bitrate(voxsdk_call_handle_t call,
                                             uint32_t bitrate_bps);
 void voxsdk_set_adaptive_bitrate(bool enabled,
                                                  uint32_t min_bps,
                                                  uint32_t max_bps);
 int voxsdk_account_keepalive_now(
                                        voxsdk_account_handle_t account);
 int voxsdk_audio_mute(voxsdk_call_handle_t call, bool mute);
 bool voxsdk_audio_is_muted(voxsdk_call_handle_t call);
 int voxsdk_audio_mute_rx(voxsdk_call_handle_t call, bool mute);
 int voxsdk_audio_set_input_device(const char *name);
 int voxsdk_audio_set_output_device(const char *name);
 int voxsdk_call_set_media_tap(voxsdk_call_handle_t call,
                                voxsdk_media_tap_cb_t cb,
                                void *userdata);
 int voxsdk_call_record_start(voxsdk_call_handle_t call,
                                              const char *path);
 int voxsdk_call_record_stop(voxsdk_call_handle_t call);
typedef struct {
 char peer_uri[256];
 char peer_display_name[128];
 char local_uri[256];
 char contact_uri[256];
 char call_id[128];

 char diverter_uri[256];
 bool is_outgoing;

 bool is_remote_hold;
 uint16_t sip_status;
 uint64_t duration_ms;
 uint32_t setup_duration_ms;
 uint32_t line_number;
 voxsdk_transport_t transport;
 voxsdk_call_state_t state;
} voxsdk_call_info_t;
 int voxsdk_call_get_info(voxsdk_call_handle_t call,
                                          voxsdk_call_info_t *out);
 int voxsdk_call_get_stats(voxsdk_call_handle_t call,
                            voxsdk_ev_media_stats_t *out);
 int voxsdk_network_changed(void);

 int voxsdk_network_set_monitor_interval(uint32_t seconds);
 int voxsdk_network_set_handover_policy(bool reinvite_calls,
                                                        bool hangup_on_failure);
 int voxsdk_network_local_addr(char *buf, size_t sz);
 bool voxsdk_network_is_up(void);

 int voxsdk_pcap_start(const char *path);
 int voxsdk_pcap_stop(void);
