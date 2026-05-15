 
              const char *baresdk_version(void);
 const char *baresdk_strerror(int err);

typedef struct baresdk_account *baresdk_account_handle_t;
typedef struct baresdk_call *baresdk_call_handle_t;

typedef enum {
 BARESDK_TRANSPORT_UDP = 0,
 BARESDK_TRANSPORT_TCP,
 BARESDK_TRANSPORT_TLS,
 BARESDK_TRANSPORT_WS,
 BARESDK_TRANSPORT_WSS,
} baresdk_transport_t;

typedef enum {
 BARESDK_MEDIA_ENC_NONE = 0,
 BARESDK_MEDIA_ENC_SDES,
 BARESDK_MEDIA_ENC_DTLS_SRTP,
} baresdk_media_enc_t;

typedef enum {
 BARESDK_CODEC_OPUS = 0,
 BARESDK_CODEC_PCMU,
 BARESDK_CODEC_PCMA,
 BARESDK_CODEC_G722,
 BARESDK_CODEC_G726_32,
} baresdk_codec_t;

typedef enum {
 BARESDK_MOS_EMODEL = 0,
 BARESDK_MOS_SIMPLIFIED,
} baresdk_mos_method_t;
typedef enum {
 BARESDK_AEC_OFF = 0,
 BARESDK_AEC_SUPPRESSOR = 1,
 BARESDK_AEC_WEBRTC = 2,
} baresdk_aec_mode_t;
typedef enum {
 BARESDK_MEDIA_DIR_RX = 0,
 BARESDK_MEDIA_DIR_TX,
} baresdk_media_dir_t;

typedef enum {
 BARESDK_DTMF_RFC4733 = 0,
 BARESDK_DTMF_SIP_INFO = 1,
 BARESDK_DTMF_AUTO = 2,
} baresdk_dtmf_mode_t;

typedef enum {
 BARESDK_JBUF_ADAPTIVE = 0,
 BARESDK_JBUF_FIXED = 1,
} baresdk_jbuf_type_t;

typedef struct {
 int bitrate;
 int complexity;
 bool cbr;
 bool dtx;
 bool fec;
 bool stereo;
} baresdk_opus_config_t;

typedef enum {
 BARESDK_OK = 0,
 BARESDK_ERR_INVAL = -1,
 BARESDK_ERR_NOMEM = -2,
 BARESDK_ERR_STATE = -3,
 BARESDK_ERR_DNS = -4,
 BARESDK_ERR_TRANSPORT = -5,
 BARESDK_ERR_AUTH = -6,
 BARESDK_ERR_SERVER_5XX = -7,
 BARESDK_ERR_WS_PROTOCOL_REJECTED = -8,
 BARESDK_ERR_TIMEOUT = -9,
 BARESDK_ERR_ALREADY = -10,
} baresdk_error_t;

typedef enum {
 BARESDK_CALL_CALLING = 0,
 BARESDK_CALL_RINGING,
 BARESDK_CALL_ESTABLISHED,
 BARESDK_CALL_HELD,
 BARESDK_CALL_ENDED,
 BARESDK_CALL_CANCELLED,
 BARESDK_CALL_FAILED,
} baresdk_call_state_t;

typedef enum {
 BARESDK_REG_UNREGISTERED = 0,
 BARESDK_REG_REGISTERING,
 BARESDK_REG_REGISTERED,
 BARESDK_REG_FAILED,
 BARESDK_REG_UNREGISTERING,
} baresdk_reg_state_t;

typedef enum {
 BARESDK_PRESENCE_UNKNOWN = 0,
 BARESDK_PRESENCE_OPEN,
 BARESDK_PRESENCE_CLOSED,
 BARESDK_PRESENCE_BUSY,
} baresdk_presence_status_t;

typedef enum {
 BARESDK_100REL_DISABLED = 0,
 BARESDK_100REL_ENABLED = 1,
 BARESDK_100REL_REQUIRED = 2,
} baresdk_100rel_mode_t;
typedef enum {
 BARESDK_PUSH_PROVIDER_NONE = 0,
 BARESDK_PUSH_PROVIDER_APNS = 1,
 BARESDK_PUSH_PROVIDER_APNS_SANDBOX = 2,
 BARESDK_PUSH_PROVIDER_FCM = 3,
} baresdk_push_provider_t;

typedef enum {
 BARESDK_EV_LOG = 0,
 BARESDK_EV_REG_STATE,
 BARESDK_EV_INCOMING_CALL,
 BARESDK_EV_CALL_STATE,
 BARESDK_EV_CALL_DTMF,
 BARESDK_EV_SDP_NEGOTIATION,
 BARESDK_EV_SIP_TRACE,
 BARESDK_EV_MEDIA_STATS,
 BARESDK_EV_REGISTRAR_WARNING,
 BARESDK_EV_TRANSFER_REQUEST,
 BARESDK_EV_MWI,
 BARESDK_EV_MESSAGE,
 BARESDK_EV_PRESENCE_STATE,
 BARESDK_EV_QUALITY_ALERT,
} baresdk_event_type_t;
typedef struct {
 baresdk_account_handle_t account;
 baresdk_reg_state_t state;
 baresdk_error_t error;
 uint32_t retry_attempt;
 uint32_t retry_delay_ms;
 const char *error_str;
} baresdk_ev_reg_state_t;

typedef struct {
 baresdk_account_handle_t account;
 baresdk_call_handle_t call;
 const char *from_uri;
 const char *display_name;
} baresdk_ev_incoming_call_t;

typedef struct {
 baresdk_account_handle_t account;
 baresdk_call_handle_t call;
 baresdk_call_state_t state;
 baresdk_error_t error;
 const char *reason;
} baresdk_ev_call_state_t;

typedef struct {
 baresdk_call_handle_t call;
 char digit;
} baresdk_ev_call_dtmf_t;

typedef struct {
 baresdk_call_handle_t call;
 const char *local_sdp;
 const char *remote_sdp;
 const char *negotiated_codec;
 const char *negotiated_crypto;
 const char * const *rejected_codecs;
 const char * const *warnings;
} baresdk_ev_sdp_negotiation_t;

typedef struct {
 baresdk_media_dir_t dir;
 const char *transport;
 const char *remote_addr;
 const char *raw_message;
 uint64_t timestamp_us;
} baresdk_ev_sip_trace_t;

typedef struct {
 baresdk_call_handle_t call;
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
 baresdk_mos_method_t mos_method;
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
} baresdk_ev_media_stats_t;

typedef struct {
 const char *message;
} baresdk_ev_log_t;

typedef struct {
 const char *message;
} baresdk_ev_registrar_warning_t;
typedef struct {
 baresdk_account_handle_t account;
 baresdk_call_handle_t call;
 const char *refer_to_uri;
 bool has_replaces;
} baresdk_ev_transfer_req_t;
typedef struct {
 baresdk_account_handle_t account;
 bool messages_waiting;
 uint32_t new_voice;
 uint32_t old_voice;
 uint32_t new_urgent;
 uint32_t old_urgent;
 const char *raw_body;
} baresdk_ev_mwi_t;
typedef struct {
 baresdk_account_handle_t account;
 const char *from_uri;
 const char *body;
 const char *content_type;
} baresdk_ev_message_t;
typedef struct {
 baresdk_account_handle_t account;
 const char *target_uri;
 baresdk_presence_status_t status;
} baresdk_ev_presence_state_t;

typedef enum {
 BARESDK_QUALITY_MOS = 0,
 BARESDK_QUALITY_LOSS,
 BARESDK_QUALITY_JITTER,
 BARESDK_QUALITY_RTT,
} baresdk_quality_issue_t;

typedef struct {
 baresdk_call_handle_t call;
 baresdk_quality_issue_t issue;
 float value;
 float threshold;
 bool recovering;
} baresdk_ev_quality_alert_t;

typedef struct {
 baresdk_event_type_t type;
 union {
  baresdk_ev_log_t log;
  baresdk_ev_reg_state_t reg;
  baresdk_ev_incoming_call_t incoming;
  baresdk_ev_call_state_t call_state;
  baresdk_ev_call_dtmf_t dtmf;
  baresdk_ev_sdp_negotiation_t sdp;
  baresdk_ev_sip_trace_t sip_trace;
  baresdk_ev_media_stats_t stats;
  baresdk_ev_registrar_warning_t reg_warn;
  baresdk_ev_transfer_req_t transfer_req;
  baresdk_ev_mwi_t mwi;
  baresdk_ev_message_t msg;
  baresdk_ev_presence_state_t presence;
  baresdk_ev_quality_alert_t quality_alert;
 } u;
} baresdk_event_t;
typedef void (*baresdk_event_cb_t)(const baresdk_event_t *ev, void *userdata);

typedef void (*baresdk_media_tap_cb_t)(
 baresdk_call_handle_t call,
 baresdk_media_dir_t direction,
 const int16_t *pcm,
 size_t samples,
 uint32_t sample_rate,
 uint8_t channels,
 uint64_t timestamp_us,
 void *userdata);

typedef struct {

 uint32_t version;
 size_t struct_size;
 baresdk_transport_t transport;
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
 baresdk_media_enc_t media_enc;
 baresdk_codec_t audio_codecs[8];
 int audio_codec_count;
 uint8_t dscp_sip;
 uint8_t dscp_rtp;
 bool enable_video;
 baresdk_aec_mode_t aec_mode;
 bool ns;
 bool agc;
 float aec_suppression_level;
 float mic_gain_db;
 float speaker_gain_db;
 baresdk_opus_config_t opus;
 baresdk_jbuf_type_t jbuf_type;
 uint32_t jitter_buffer_min_ms;
 uint32_t jitter_buffer_max_ms;
 uint32_t reg_expires;
 uint32_t reg_refresh_pct;
 uint32_t keepalive_interval;
 uint32_t reg_retry_initial_ms;
 uint32_t reg_retry_max_ms;
 float reg_retry_backoff;
 uint32_t reg_retry_max_attempts;
 uint32_t sip_t1_ms;
 uint32_t sip_t2_ms;
 uint32_t sip_timer_b_ms;
 uint32_t sip_timer_f_ms;
 bool session_timer_enabled;
 uint32_t session_expires_s;
 uint32_t session_min_se_s;
 uint32_t stats_interval_ms;
 baresdk_mos_method_t mos_method;

 float mos_alert_threshold;
 float loss_alert_threshold;
 float jitter_alert_threshold;
 const char *tmp_dir;
 bool trace_sip;
 bool trace_sdp_diff;
 const char *pcap_path;
 int log_level;
 baresdk_event_cb_t event_cb;
 void *event_userdata;

} baresdk_config_t;

typedef struct {
 const char *uri;

 const char *password;

 baresdk_transport_t transport;

 const char *server_host;
 uint16_t server_port;

 const char *server_url;

 const char *auth_user;
 const char *display_name;

 baresdk_media_enc_t media_enc;
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
 baresdk_push_provider_t push_provider;

 const char *push_token;

 const char *push_param;
 baresdk_codec_t audio_codecs[8];
 int audio_codec_count;
 char audio_codec_names[8][32];
 int audio_codec_name_count;
 baresdk_dtmf_mode_t dtmf_mode;

} baresdk_account_config_t;
 void baresdk_config_init(baresdk_config_t *cfg);
 int baresdk_init(const baresdk_config_t *cfg);

 void baresdk_shutdown(void);

 int baresdk_account_create(const baresdk_account_config_t *cfg,
                             baresdk_account_handle_t *out);
 void baresdk_account_destroy(baresdk_account_handle_t acct);
 int baresdk_account_register(baresdk_account_handle_t acct);
 int baresdk_account_unregister(baresdk_account_handle_t acct);
 int baresdk_account_set_retry_policy(baresdk_account_handle_t acct,
                                                     uint32_t initial_ms,
                                                     uint32_t max_ms,
                                                     float backoff,
                                                     uint32_t max_attempts);

 int baresdk_account_cancel_retry(baresdk_account_handle_t acct);

 int baresdk_account_retry_now(baresdk_account_handle_t acct);
 int baresdk_account_set_push_token(baresdk_account_handle_t acct,
                                                   const char *push_token);
 int baresdk_account_add_register_header(
        baresdk_account_handle_t acct,
        const char *name, const char *value);
 int baresdk_account_add_header(baresdk_account_handle_t acct,
                                const char *name, const char *value);

 int baresdk_account_subscribe_presence(baresdk_account_handle_t acct,
                                        const char *target_uri);
 int baresdk_account_unsubscribe_presence(baresdk_account_handle_t acct,
                                          const char *target_uri);

 int baresdk_call_invite(baresdk_account_handle_t acct,
                         const char *uri,
                         baresdk_call_handle_t *out);
 int baresdk_call_answer(baresdk_call_handle_t call);
 int baresdk_call_hangup(baresdk_call_handle_t call);
 int baresdk_call_hold(baresdk_call_handle_t call);
 int baresdk_call_resume(baresdk_call_handle_t call);
 bool baresdk_call_is_held(baresdk_call_handle_t call);
 int baresdk_call_send_dtmf(baresdk_call_handle_t call, char digit);
 int baresdk_call_transfer(baresdk_call_handle_t call, const char *uri);
 int baresdk_call_add_header(baresdk_call_handle_t call,
                             const char *name, const char *value);
 int baresdk_call_attended_transfer(baresdk_call_handle_t call_a,
                                    baresdk_call_handle_t call_b);
typedef void (*baresdk_call_iter_fn)(baresdk_call_handle_t call, void *arg);
 void baresdk_call_foreach(baresdk_call_iter_fn fn, void *arg);
 int baresdk_message_send(baresdk_account_handle_t account,
                          const char *to_uri,
                          const char *body,
                          const char *content_type);

 int baresdk_account_publish_presence(baresdk_account_handle_t account,
                                      baresdk_presence_status_t status);

 int baresdk_account_set_100rel(baresdk_account_handle_t account,
                                baresdk_100rel_mode_t mode);

typedef struct {
 char name[128];
 char description[256];
 bool is_default;
} baresdk_audio_device_t;

 int baresdk_audio_list_input_devices(baresdk_audio_device_t *devices,
                                                     int max_count);
 int baresdk_audio_list_output_devices(baresdk_audio_device_t *devices,
                                                      int max_count);
 void baresdk_set_aec(bool enable);
 int baresdk_set_aec_mode(baresdk_aec_mode_t mode);

 void baresdk_set_aec_suppression_level(float level);
 void baresdk_set_ns(bool enable);
 void baresdk_set_agc(bool enable);

 void baresdk_set_mic_gain_db(float db);

 void baresdk_set_speaker_gain_db(float db);
 int baresdk_call_set_dscp_rtp(baresdk_call_handle_t call,
                                              uint8_t dscp);
 void baresdk_set_jitter_buffer(uint32_t min_ms, uint32_t max_ms);
 void baresdk_set_jitter_buffer_type(baresdk_jbuf_type_t type);
 int baresdk_audio_mute(baresdk_call_handle_t call, bool mute);
 bool baresdk_audio_is_muted(baresdk_call_handle_t call);
 int baresdk_audio_mute_rx(baresdk_call_handle_t call, bool mute);
 int baresdk_audio_set_input_device(const char *name);
 int baresdk_audio_set_output_device(const char *name);
 int baresdk_call_set_media_tap(baresdk_call_handle_t call,
                                baresdk_media_tap_cb_t cb,
                                void *userdata);
 int baresdk_call_record_start(baresdk_call_handle_t call,
                                              const char *path);
 int baresdk_call_record_stop(baresdk_call_handle_t call);
 int baresdk_call_get_stats(baresdk_call_handle_t call,
                            baresdk_ev_media_stats_t *out);

 int baresdk_pcap_start(const char *path);
 int baresdk_pcap_stop(void);
