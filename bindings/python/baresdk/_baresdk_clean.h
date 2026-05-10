typedef unsigned long size_t;
typedef int wchar_t;
typedef unsigned char __u_char;
typedef unsigned short int __u_short;
typedef unsigned int __u_int;
typedef unsigned long int __u_long;
typedef signed char __int8_t;
typedef unsigned char __uint8_t;
typedef signed short int __int16_t;
typedef unsigned short int __uint16_t;
typedef signed int __int32_t;
typedef unsigned int __uint32_t;
 typedef signed long long int __int64_t;
 typedef unsigned long long int __uint64_t;
typedef __int8_t __int_least8_t;
typedef __uint8_t __uint_least8_t;
typedef __int16_t __int_least16_t;
typedef __uint16_t __uint_least16_t;
typedef __int32_t __int_least32_t;
typedef __uint32_t __uint_least32_t;
typedef __int64_t __int_least64_t;
typedef __uint64_t __uint_least64_t;
 typedef long long int __quad_t;
 typedef unsigned long long int __u_quad_t;
 typedef long long int __intmax_t;
 typedef unsigned long long int __uintmax_t;
 typedef __uint64_t __dev_t;
 typedef unsigned int __uid_t;
 typedef unsigned int __gid_t;
 typedef unsigned long int __ino_t;
 typedef __uint64_t __ino64_t;
 typedef unsigned int __mode_t;
 typedef unsigned int __nlink_t;
 typedef long int __off_t;
 typedef __int64_t __off64_t;
 typedef int __pid_t;
 typedef struct { int __val[2]; } __fsid_t;
 typedef long int __clock_t;
 typedef unsigned long int __rlim_t;
 typedef __uint64_t __rlim64_t;
 typedef unsigned int __id_t;
 typedef long int __time_t;
 typedef unsigned int __useconds_t;
 typedef long int __suseconds_t;
 typedef __int64_t __suseconds64_t;
 typedef int __daddr_t;
 typedef int __key_t;
 typedef int __clockid_t;
 typedef void * __timer_t;
 typedef long int __blksize_t;
 typedef long int __blkcnt_t;
 typedef __int64_t __blkcnt64_t;
 typedef unsigned long int __fsblkcnt_t;
 typedef __uint64_t __fsblkcnt64_t;
 typedef unsigned long int __fsfilcnt_t;
 typedef __uint64_t __fsfilcnt64_t;
 typedef int __fsword_t;
 typedef int __ssize_t;
 typedef long int __syscall_slong_t;
 typedef unsigned long int __syscall_ulong_t;
typedef __off64_t __loff_t;
typedef char *__caddr_t;
 typedef int __intptr_t;
 typedef unsigned int __socklen_t;
typedef int __sig_atomic_t;
 typedef __int64_t __time64_t;
typedef __int8_t int8_t;
typedef __int16_t int16_t;
typedef __int32_t int32_t;
typedef __int64_t int64_t;
typedef __uint8_t uint8_t;
typedef __uint16_t uint16_t;
typedef __uint32_t uint32_t;
typedef __uint64_t uint64_t;
typedef __int_least8_t int_least8_t;
typedef __int_least16_t int_least16_t;
typedef __int_least32_t int_least32_t;
typedef __int_least64_t int_least64_t;
typedef __uint_least8_t uint_least8_t;
typedef __uint_least16_t uint_least16_t;
typedef __uint_least32_t uint_least32_t;
typedef __uint_least64_t uint_least64_t;
typedef signed char int_fast8_t;
typedef int int_fast16_t;
typedef int int_fast32_t;

typedef long long int int_fast64_t;
typedef unsigned char uint_fast8_t;
typedef unsigned int uint_fast16_t;
typedef unsigned int uint_fast32_t;

typedef unsigned long long int uint_fast64_t;
typedef int intptr_t;
typedef unsigned int uintptr_t;
typedef __intmax_t intmax_t;
typedef __uintmax_t uintmax_t;
typedef long int ptrdiff_t;
typedef long unsigned int size_t;
typedef int wchar_t;
typedef struct {
const char *baresdk_version(void);
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
} baresdk_codec_t;
typedef enum {
 BARESDK_MOS_EMODEL = 0,
 BARESDK_MOS_SIMPLIFIED,
} baresdk_mos_method_t;
typedef enum {
 BARESDK_MEDIA_DIR_RX = 0,
 BARESDK_MEDIA_DIR_TX,
} baresdk_media_dir_t;
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
 float loss_pct;
 float jitter_ms;
 float rtt_ms;
 float mos_lq;
 float mos_cq;
 baresdk_mos_method_t mos_method;
 const char *codec_name;
 uint32_t codec_clock_rate;
 uint32_t bandwidth_kbps_tx;
 uint32_t bandwidth_kbps_rx;
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
 const char *stun_server;
 const char *turn_server;
 const char *turn_user;
 const char *turn_pass;
 bool ice_enabled;
 baresdk_media_enc_t media_enc;
 baresdk_codec_t audio_codecs[8];
 int audio_codec_count;
 uint8_t dscp_sip;
 uint8_t dscp_rtp;
 bool enable_video;
 bool aec;
 bool ns;
 bool agc;
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
 const char *stun_server;
 const char *turn_server;
 const char *turn_user;
 const char *turn_pass;
 const char *outbound;
 bool verify_tls;
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
                                      uint32_t initial_ms, uint32_t max_ms,
                                      float backoff, uint32_t max_attempts);
int baresdk_account_cancel_retry(baresdk_account_handle_t acct);
int baresdk_account_retry_now(baresdk_account_handle_t acct);
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
int baresdk_call_send_dtmf(baresdk_call_handle_t call, char digit);
int baresdk_call_transfer(baresdk_call_handle_t call, const char *uri);
int baresdk_call_add_header(baresdk_call_handle_t call,
                             const char *name, const char *value);
int baresdk_call_attended_transfer(baresdk_call_handle_t call_a,
                                    baresdk_call_handle_t call_b);
int baresdk_message_send(baresdk_account_handle_t account,
                          const char *to_uri,
                          const char *body,
                          const char *content_type);
int baresdk_account_publish_presence(baresdk_account_handle_t account,
                                      baresdk_presence_status_t status);
int baresdk_account_set_100rel(baresdk_account_handle_t account,
                                baresdk_100rel_mode_t mode);
int baresdk_audio_mute(baresdk_call_handle_t call, bool mute);
int baresdk_audio_set_input_device(const char *name);
int baresdk_audio_set_output_device(const char *name);
int baresdk_call_set_media_tap(baresdk_call_handle_t call,
                                baresdk_media_tap_cb_t cb,
                                void *userdata);
int baresdk_call_get_stats(baresdk_call_handle_t call,
                            baresdk_ev_media_stats_t *out);
int baresdk_call_record_start(baresdk_call_handle_t call, const char *path);
int baresdk_call_record_stop(baresdk_call_handle_t call);
int baresdk_pcap_start(const char *path);
int baresdk_pcap_stop(void);
