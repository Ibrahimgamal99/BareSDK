/**
 * @file baresdk.h  baresdk public API
 *
 * Single header for all platforms. Include this and link baresdk.a.
 * Never include re.h or baresip.h directly from consumer code.
 *
 * ABI stability contract:
 *   - Fields are only appended to structs; never reordered or removed.
 *   - baresdk_config_t carries version + struct_size for forward compat.
 *   - Opaque handle types are stable across minor versions.
 */

#ifndef BARESDK_H
#define BARESDK_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Version ─────────────────────────────────────────────────────────────── */

#define BARESDK_VERSION_MAJOR 1
#define BARESDK_VERSION_MINOR 0
#define BARESDK_VERSION_PATCH 0

const char *baresdk_version(void);

/* ── Opaque handles ───────────────────────────────────────────────────────── */

typedef struct baresdk_account  *baresdk_account_handle_t;
typedef struct baresdk_call     *baresdk_call_handle_t;

/* ── Enumerations ─────────────────────────────────────────────────────────── */

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
	BARESDK_CODEC_PCMU,   /* G.711 µ-law */
	BARESDK_CODEC_PCMA,   /* G.711 A-law */
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

/* ── Error codes ──────────────────────────────────────────────────────────── */

typedef enum {
	BARESDK_OK                       =  0,
	BARESDK_ERR_INVAL                = -1,
	BARESDK_ERR_NOMEM                = -2,
	BARESDK_ERR_STATE                = -3,  /* wrong lifecycle state */
	BARESDK_ERR_DNS                  = -4,
	BARESDK_ERR_TRANSPORT            = -5,
	BARESDK_ERR_AUTH                 = -6,
	BARESDK_ERR_SERVER_5XX           = -7,
	BARESDK_ERR_WS_PROTOCOL_REJECTED = -8,
	BARESDK_ERR_TIMEOUT              = -9,
	BARESDK_ERR_ALREADY              = -10,
} baresdk_error_t;

/* ── Call states ──────────────────────────────────────────────────────────── */

typedef enum {
	BARESDK_CALL_CALLING = 0,
	BARESDK_CALL_RINGING,
	BARESDK_CALL_ESTABLISHED,
	BARESDK_CALL_HELD,
	BARESDK_CALL_ENDED,
	BARESDK_CALL_CANCELLED,
	BARESDK_CALL_FAILED,
} baresdk_call_state_t;

/* ── Registration states ──────────────────────────────────────────────────── */

typedef enum {
	BARESDK_REG_UNREGISTERED = 0,
	BARESDK_REG_REGISTERING,
	BARESDK_REG_REGISTERED,
	BARESDK_REG_FAILED,
	BARESDK_REG_UNREGISTERING,
} baresdk_reg_state_t;

/* ── Presence enums ────────────────────────────────────────────────────────── */

typedef enum {
	BARESDK_PRESENCE_UNKNOWN = 0,
	BARESDK_PRESENCE_OPEN,     /* available */
	BARESDK_PRESENCE_CLOSED,   /* offline / DND */
	BARESDK_PRESENCE_BUSY,     /* on a call */
} baresdk_presence_status_t;

typedef enum {
	BARESDK_100REL_DISABLED = 0, /* never send/require 100rel */
	BARESDK_100REL_ENABLED  = 1, /* support 100rel if peer offers */
	BARESDK_100REL_REQUIRED = 2, /* require 100rel; reject if unsupported */
} baresdk_100rel_mode_t;

/* ── Event types ──────────────────────────────────────────────────────────── */

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

/* ── Event payload structs ────────────────────────────────────────────────── */
	baresdk_account_handle_t account;
	baresdk_reg_state_t      state;
	baresdk_error_t          error;         /* BARESDK_OK when REGISTERED */
	uint32_t                 retry_attempt;
	uint32_t                 retry_delay_ms;
	const char              *error_str;     /* human-readable; NULL on OK */
} baresdk_ev_reg_state_t;

typedef struct {
	baresdk_account_handle_t account;
	baresdk_call_handle_t    call;
	const char              *from_uri;
	const char              *display_name;
} baresdk_ev_incoming_call_t;

typedef struct {
	baresdk_account_handle_t account;
	baresdk_call_handle_t    call;
	baresdk_call_state_t     state;
	baresdk_error_t          error;
	const char              *reason;
} baresdk_ev_call_state_t;

typedef struct {
	baresdk_call_handle_t call;
	char                  digit;
} baresdk_ev_call_dtmf_t;

typedef struct {
	baresdk_call_handle_t  call;
	const char            *local_sdp;
	const char            *remote_sdp;
	const char            *negotiated_codec;
	const char            *negotiated_crypto; /* "NONE", "SDES", "DTLS-SRTP" */
	const char * const    *rejected_codecs;   /* NULL-terminated array */
	const char * const    *warnings;          /* NULL-terminated array */
} baresdk_ev_sdp_negotiation_t;

typedef struct {
	baresdk_media_dir_t  dir;
	const char          *transport;    /* "UDP", "TCP", "TLS", "WS", "WSS" */
	const char          *remote_addr;  /* "1.2.3.4:5060" */
	const char          *raw_message;
	uint64_t             timestamp_us;
} baresdk_ev_sip_trace_t;

typedef struct {
	baresdk_call_handle_t call;
	/* RTP counters */
	uint32_t packets_sent;
	uint32_t packets_received;
	uint32_t packets_lost;
	float    loss_pct;
	float    jitter_ms;
	float    rtt_ms;
	/* MOS scores */
	float    mos_lq;  /* listening quality  */
	float    mos_cq;  /* conversational quality */
	baresdk_mos_method_t mos_method;
	/* Codec */
	const char *codec_name;
	uint32_t    codec_clock_rate;
	/* Bandwidth kbps */
	uint32_t bandwidth_kbps_tx;
	uint32_t bandwidth_kbps_rx;
} baresdk_ev_media_stats_t;

typedef struct {
	const char *message;
} baresdk_ev_log_t;

typedef struct {
	const char *message;
} baresdk_ev_registrar_warning_t;

/* ── Transfer, MWI, MESSAGE, Presence payload structs ─────────────────────── */

/** Incoming REFER request — blind transfer or attended (has_replaces=true). */
typedef struct {
	baresdk_account_handle_t account;
	baresdk_call_handle_t    call;          /* call receiving the REFER */
	const char              *refer_to_uri;  /* Refer-To header value */
	bool                     has_replaces;  /* true = attended transfer */
} baresdk_ev_transfer_req_t;

/** MWI NOTIFY — raw body is parsed into counters; all fields may be 0. */
typedef struct {
	baresdk_account_handle_t account;
	bool                     messages_waiting;
	uint32_t                 new_voice;
	uint32_t                 old_voice;
	uint32_t                 new_urgent;
	uint32_t                 old_urgent;
	const char              *raw_body;      /* full NOTIFY body */
} baresdk_ev_mwi_t;

/** Incoming SIP MESSAGE (instant message). */
typedef struct {
	baresdk_account_handle_t account;
	const char              *from_uri;
	const char              *body;
	const char              *content_type;  /* e.g. "text/plain" */
} baresdk_ev_message_t;

/** Buddy / contact presence state changed. */
typedef struct {
	baresdk_account_handle_t  account;
	const char               *target_uri;
	baresdk_presence_status_t status;
} baresdk_ev_presence_state_t;

/* ── Master event union ───────────────────────────────────────────────────── */

typedef struct {
	baresdk_event_type_t type;
	union {
		baresdk_ev_log_t               log;
		baresdk_ev_reg_state_t         reg;
		baresdk_ev_incoming_call_t     incoming;
		baresdk_ev_call_state_t        call_state;
		baresdk_ev_call_dtmf_t         dtmf;
		baresdk_ev_sdp_negotiation_t   sdp;
		baresdk_ev_sip_trace_t         sip_trace;
		baresdk_ev_media_stats_t       stats;
		baresdk_ev_registrar_warning_t reg_warn;
		baresdk_ev_transfer_req_t      transfer_req;
		baresdk_ev_mwi_t               mwi;
		baresdk_ev_message_t           msg;
		baresdk_ev_presence_state_t    presence;
	} u;
} baresdk_event_t;

/* ── Callbacks ────────────────────────────────────────────────────────────── */

/**
 * Event callback — fired from baresdk's internal event thread.
 * Must return within 10 ms. Do not call baresdk APIs synchronously
 * from inside this callback (use a separate thread or post to your
 * own queue and return).
 */
typedef void (*baresdk_event_cb_t)(const baresdk_event_t *ev, void *userdata);

/**
 * Media tap callback — fired from the audio thread on each audio frame.
 * Must be non-blocking. Copy PCM data if you need it beyond the callback.
 */
typedef void (*baresdk_media_tap_cb_t)(
	baresdk_call_handle_t  call,
	baresdk_media_dir_t    direction,
	const int16_t         *pcm,
	size_t                 samples,
	uint32_t               sample_rate,
	uint8_t                channels,
	uint64_t               timestamp_us,
	void                  *userdata);

/* ── Config ───────────────────────────────────────────────────────────────── */

#define BARESDK_CONFIG_VERSION 1

typedef struct {
	/* Forward-compat guard — MUST be set by baresdk_config_init(). */
	uint32_t  version;      /* BARESDK_CONFIG_VERSION */
	size_t    struct_size;  /* sizeof(baresdk_config_t) at compile time */

	/* ── Transport ────────────────────────────────────────────────── */
	baresdk_transport_t  transport;
	const char          *local_ip;        /* NULL = auto */
	uint16_t             local_port;      /* 0 = OS-assigned */
	const char          *bind_interface;  /* NULL = any, e.g. "wlan0" */
	bool                 prefer_ipv6;

	const char          *sip_domain;      /* AOR domain, e.g. "pbx.example.com" */

	/**
	 * Server endpoint — two ways to specify, pick one:
	 *
	 * Simple form (UDP/TCP/TLS, default ports):
	 *   transport   = BARESDK_TRANSPORT_TLS
	 *   server_host = "pbx.example.com"
	 *   server_port = 0  → uses 5061 for TLS
	 *
	 * Full URL form (required for WS/WSS, custom paths):
	 *   server_url = "wss://pbx.example.com/ws"
	 *   server_url = "wss://pbx.example.com:8089/ws"
	 *   server_url = "ws://pbx.internal:8088/ws"
	 *
	 * server_url overrides transport/server_host/server_port when set.
	 */
	const char  *server_url;    /* full URL, takes precedence */
	const char  *server_host;   /* simple form */
	uint16_t     server_port;   /* 0 = transport default */

	const char  *outbound_proxy; /* NULL = direct; same URL grammar */

	/* ── TLS / WSS ────────────────────────────────────────────────── */
	const char  *ca_cert_path;
	const char  *client_cert;
	const char  *client_key;
	bool         verify_server;
	/**
	 * SNI hostname — NULL derives from server_url host.
	 * Set explicitly when the reverse-proxy cert SNI differs from
	 * the SIP domain (e.g. proxy cert for "proxy.example.com" but
	 * SIP domain is "pbx.internal").
	 */
	const char  *sni_hostname;
	const char  *user_agent;    /* SIP User-Agent header value */

	/* ── WebSocket-specific ───────────────────────────────────────── */
	const char  *ws_origin;         /* NULL = auto-derived from server_url */
	const char **ws_extra_headers;  /* NULL-terminated "Header: value" strings */

	/* ── NAT ──────────────────────────────────────────────────────── */
	const char  *stun_server;
	const char  *turn_server;
	const char  *turn_user;
	const char  *turn_pass;
	bool         ice_enabled;

	/* ── Media ────────────────────────────────────────────────────── */
	baresdk_media_enc_t  media_enc;
	baresdk_codec_t      audio_codecs[8]; /* ordered preference list */
	int                  audio_codec_count;
	uint8_t              dscp_sip;  /* 0 = OS default; 24 = AF31 */
	uint8_t              dscp_rtp;  /* 0 = OS default; 46 = EF */
	bool                 enable_video; /* reserved for future video support */

	/* ── Audio processing ─────────────────────────────────────────── */
	bool  aec;  /* acoustic echo cancellation */
	bool  ns;   /* noise suppression */
	bool  agc;  /* automatic gain control */

	/* ── Registration ─────────────────────────────────────────────── */
	uint32_t  reg_expires;           /* seconds; default 3600 */
	uint32_t  reg_refresh_pct;       /* refresh at N% of expires; default 75 */
	uint32_t  keepalive_interval;    /* ms; 0 = transport default */

	/* Registration retry policy */
	uint32_t  reg_retry_initial_ms;  /* default 2000 */
	uint32_t  reg_retry_max_ms;      /* default 300000 (5 min) */
	float     reg_retry_backoff;     /* multiplier; default 2.0 */
	uint32_t  reg_retry_max_attempts;/* 0 = retry forever */

	/* ── SIP timers (RFC 3261 §17) ───────────────────────────────── */
	uint32_t  sip_t1_ms;        /* default 500 */
	uint32_t  sip_t2_ms;        /* default 4000 */
	uint32_t  sip_timer_b_ms;   /* default 32000 */
	uint32_t  sip_timer_f_ms;   /* default 32000 */

	/* ── Session timers (RFC 4028) ───────────────────────────────── */
	bool      session_timer_enabled; /* default true */
	uint32_t  session_expires_s;     /* default 1800 */
	uint32_t  session_min_se_s;      /* default 90 */

	/* ── Quality / observability ─────────────────────────────────── */
	uint32_t              stats_interval_ms; /* 0 = disabled */
	baresdk_mos_method_t  mos_method;

	/* ── Tracing ─────────────────────────────────────────────────── */
	bool        trace_sip;      /* emit BARESDK_EV_SIP_TRACE per message */
	bool        trace_sdp_diff; /* emit BARESDK_EV_SDP_NEGOTIATION */
	const char *pcap_path;      /* NULL = no pcap; path = live capture */

	/* ── Logging & events ────────────────────────────────────────── */
	int                 log_level;      /* 0=err, 1=warn, 2=info, 3=debug */
	baresdk_event_cb_t  event_cb;       /* required */
	void               *event_userdata;

} baresdk_config_t;

/* ── Account config ───────────────────────────────────────────────────────── */

typedef struct {
	const char *aor;          /* "sip:user@domain" — required */
	const char *auth_user;    /* digest auth username; NULL = use AOR user */
	const char *auth_pass;    /* digest auth password — required */
	const char *display_name; /* NULL = omit */
	const char *outbound;     /* override outbound proxy for this account */
} baresdk_account_config_t;

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

/** Zero-fill cfg and set version/struct_size correctly. Call before populating. */
void baresdk_config_init(baresdk_config_t *cfg);

/**
 * Initialize the baresdk stack. Call once per process before any other API.
 * Spawns re_main thread and event dispatch thread internally.
 * Returns BARESDK_OK or a negative BARESDK_ERR_* code.
 */
int baresdk_init(const baresdk_config_t *cfg);

/**
 * Tear down the stack. Blocks until all internal threads have exited.
 * All active accounts and calls are forcibly terminated first.
 */
void baresdk_shutdown(void);

/* ── Accounts ─────────────────────────────────────────────────────────────── */

/**
 * Create a SIP account. Does NOT register — call baresdk_account_register().
 * Thread-safe; may be called from any thread after baresdk_init().
 */
int baresdk_account_create(const baresdk_account_config_t *cfg,
                            baresdk_account_handle_t *out);

/** Destroy account and all associated calls. Unregisters first if registered. */
void baresdk_account_destroy(baresdk_account_handle_t acct);

/** Begin registration. Fires BARESDK_EV_REG_STATE events on state changes. */
int baresdk_account_register(baresdk_account_handle_t acct);

/** Unregister (sends REGISTER with Expires: 0). */
int baresdk_account_unregister(baresdk_account_handle_t acct);

/**
 * Add a custom SIP header to all outgoing requests for this account.
 * @param name   Header field name  (e.g. "X-Tenant-Id")
 * @param value  Header field value (e.g. "12345")
 */
int baresdk_account_add_header(baresdk_account_handle_t acct,
                                const char *name, const char *value);

/**
 * Subscribe to presence state changes for a contact (SUBSCRIBE/NOTIFY).
 * Presence updates are delivered via BARESDK_EV_PRESENCE_STATE events.
 * @param target_uri  SIP URI of the contact to subscribe to
 * @return BARESDK_OK on success, or negative error code
 */
int baresdk_account_subscribe_presence(baresdk_account_handle_t acct,
                                        const char *target_uri);

/**
 * Unsubscribe from presence state changes for a contact.
 * @param target_uri  SIP URI of the contact to unsubscribe from
 * @return BARESDK_OK on success, or negative error code
 */
int baresdk_account_unsubscribe_presence(baresdk_account_handle_t acct,
                                          const char *target_uri);

/* ── Calls ────────────────────────────────────────────────────────────────── */

/**
 * Initiate an outgoing call. Returns immediately; fires BARESDK_EV_CALL_STATE
 * CALLING, then RINGING, then ESTABLISHED (or FAILED).
 */
int baresdk_call_invite(baresdk_account_handle_t acct,
                         const char *uri,
                         baresdk_call_handle_t *out);

/** Answer an incoming call (received via BARESDK_EV_INCOMING_CALL). */
int baresdk_call_answer(baresdk_call_handle_t call);

/** Terminate a call with BYE. */
int baresdk_call_hangup(baresdk_call_handle_t call);

/** Put call on hold (re-INVITE with sendonly). */
int baresdk_call_hold(baresdk_call_handle_t call);

/** Resume a held call (re-INVITE with sendrecv). */
int baresdk_call_resume(baresdk_call_handle_t call);

/** Send DTMF digit via RFC 4733 RTP events. digit: '0'-'9', '*', '#', 'A'-'D'. */
int baresdk_call_send_dtmf(baresdk_call_handle_t call, char digit);

/** Blind transfer via REFER. */
int baresdk_call_transfer(baresdk_call_handle_t call, const char *uri);

/**
 * Add a custom SIP header to a specific call/dialog.
 * Headers are applied to subsequent re-INVITEs, BYE, and REFER messages
 * within this dialog. Must be called while the call is active.
 * @param name   Header field name  (e.g. "X-Call-Id")
 * @param value  Header field value (e.g. "12345")
 */
int baresdk_call_add_header(baresdk_call_handle_t call,
                             const char *name, const char *value);

/**
 * Attended transfer: send REFER w/ Replaces on call_a, bridging it to call_b.
 * call_a is the call to transfer away; call_b is the already-established
 * consultation call whose dialog info is embedded in Replaces.
 */
int baresdk_call_attended_transfer(baresdk_call_handle_t call_a,
                                    baresdk_call_handle_t call_b);

/* ── SIP MESSAGE ─────────────────────────────────────────────────────────── */

/**
 * Send a SIP MESSAGE (instant message) out of dialog.
 * content_type defaults to "text/plain" if NULL.
 * Fires BARESDK_EV_MESSAGE on the remote end when received.
 */
int baresdk_message_send(baresdk_account_handle_t account,
                          const char *to_uri,
                          const char *body,
                          const char *content_type);

/* ── Presence ────────────────────────────────────────────────────────────── */

/**
 * Publish presence status for the account (PUBLISH request).
 * Fires BARESDK_EV_PRESENCE_STATE on subscribed watchers.
 */
int baresdk_account_publish_presence(baresdk_account_handle_t account,
                                      baresdk_presence_status_t status);

/* ── 100rel / PRACK ──────────────────────────────────────────────────────── */

/**
 * Set the RFC 3262 100rel mode for an account.
 * Must be called before baresdk_call_invite / baresdk_call_answer.
 */
int baresdk_account_set_100rel(baresdk_account_handle_t account,
                                baresdk_100rel_mode_t mode);

/* ── Audio ────────────────────────────────────────────────────────────────── */

/** Mute/unmute the microphone for a call. */
int baresdk_audio_mute(baresdk_call_handle_t call, bool mute);

/** Set the system audio input device by name. NULL = platform default. */
int baresdk_audio_set_input_device(const char *name);

/** Set the system audio output device by name. NULL = platform default. */
int baresdk_audio_set_output_device(const char *name);

/* ── Media tap ────────────────────────────────────────────────────────────── */

/**
 * Install a PCM tap on an active call.
 * cb is called from the audio thread for both RX (decoded) and TX (pre-encode)
 * directions. Must be non-blocking — heavy work must go to your own thread.
 * Pass NULL cb to remove the tap.
 */
int baresdk_call_set_media_tap(baresdk_call_handle_t   call,
                                baresdk_media_tap_cb_t  cb,
                                void                   *userdata);

/* ── Stats ────────────────────────────────────────────────────────────────── */

/**
 * Synchronously retrieve current stats for a call.
 * Also delivered automatically via BARESDK_EV_MEDIA_STATS if
 * cfg.stats_interval_ms > 0.
 */
int baresdk_call_get_stats(baresdk_call_handle_t     call,
                            baresdk_ev_media_stats_t *out);

/* ── pcap ─────────────────────────────────────────────────────────────────── */

/**
 * Start capturing SIP + RTP to a Wireshark-compatible pcap file.
 * Writes synthetic Ethernet/IP/UDP headers around each SIP message.
 */
int baresdk_pcap_start(const char *path);

/** Stop capture and flush/close the pcap file. */
int baresdk_pcap_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* BARESDK_H */
