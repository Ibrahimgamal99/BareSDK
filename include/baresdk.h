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

/* ── DLL export macros (Windows shared library build) ──────────────────── */
#ifdef _WIN32
#  ifdef BARESDK_SHARED_BUILD
#    define BARESDK_EXPORT __declspec(dllexport)
#  elif defined(BARESDK_SHARED)
#    define BARESDK_EXPORT __declspec(dllimport)
#  else
#    define BARESDK_EXPORT
#  endif
#else
#  define BARESDK_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ── Version ─────────────────────────────────────────────────────────────── */

#define BARESDK_VERSION_MAJOR 1
#define BARESDK_VERSION_MINOR 0
#define BARESDK_VERSION_PATCH 0

BARESDK_EXPORT const char *baresdk_version(void);
BARESDK_EXPORT const char *baresdk_strerror(int err);

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
	BARESDK_CODEC_PCMU,     /* G.711 µ-law */
	BARESDK_CODEC_PCMA,     /* G.711 A-law */
	BARESDK_CODEC_G722,
	BARESDK_CODEC_G726_32,  /* G.726 32 kbit/s, 8 kHz */
} baresdk_codec_t;

typedef enum {
	BARESDK_MOS_EMODEL = 0,
	BARESDK_MOS_SIMPLIFIED,
} baresdk_mos_method_t;

/**
 * AEC backend selection.
 * Stored as uint8_t to occupy exactly 1 byte (same as the former bool aec),
 * preserving baresdk_config_t memory layout.
 *
 * BARESDK_AEC_SUPPRESSOR (default): built-in half-duplex TX suppressor.
 *   Works on all platforms, zero external dependencies.
 * BARESDK_AEC_WEBRTC: full-duplex WebRTC acoustic echo cancellation.
 *   Desktop only; requires BARESDK_WITH_WEBRTC_AEC build option and
 *   libwebrtc-audio-processing-1.  Returns ENOTSUP on mobile builds and
 *   when the option is off.
 */
/* BARESDK_NO_PACKED_ENUM: for binding generators (e.g. ffigen) that ignore
 * __attribute__((packed)) and would widen the field to 4 bytes, silently
 * shifting every later struct member. The uint8_t typedef keeps the ABI. */
#if defined(_MSC_VER) || defined(BARESDK_NO_PACKED_ENUM)
typedef uint8_t baresdk_aec_mode_t;
#  define BARESDK_AEC_OFF        ((baresdk_aec_mode_t)0)
#  define BARESDK_AEC_SUPPRESSOR ((baresdk_aec_mode_t)1)
#  define BARESDK_AEC_WEBRTC     ((baresdk_aec_mode_t)2)
#else
typedef enum __attribute__((packed)) {
	BARESDK_AEC_OFF        = 0,
	BARESDK_AEC_SUPPRESSOR = 1,
	BARESDK_AEC_WEBRTC     = 2,
} baresdk_aec_mode_t;
#endif

typedef enum {
	BARESDK_MEDIA_DIR_RX = 0,
	BARESDK_MEDIA_DIR_TX,
} baresdk_media_dir_t;

typedef enum {
	BARESDK_DTMF_RFC4733  = 0, /* RFC 4733 RTP telephony-event (default) */
	BARESDK_DTMF_SIP_INFO = 1, /* SIP INFO application/dtmf-relay */
	BARESDK_DTMF_AUTO     = 2, /* prefer RFC 4733, fall back to SIP INFO */
} baresdk_dtmf_mode_t;

typedef enum {
	BARESDK_JBUF_ADAPTIVE = 0, /* adaptive jitter buffer (default) */
	BARESDK_JBUF_FIXED    = 1, /* fixed-depth jitter buffer */
} baresdk_jbuf_type_t;

typedef struct {
	int  bitrate;    /* 0 = auto/VBR; otherwise bps, e.g. 32000 */
	int  complexity; /* 0-10 CPU trade-off; -1 = opus default (9) */
	bool cbr;        /* constant bitrate; false = VBR (default) */
	bool dtx;        /* discontinuous transmission (silence suppression) */
	bool fec;        /* in-band forward error correction */
	bool stereo;     /* stereo output; false = mono (default) */
} baresdk_opus_config_t;

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

/**
 * Push notification provider — determines the pn-provider URI parameter
 * value placed in the SIP REGISTER Contact header per RFC 8599, or the
 * server-side push dispatch type for non-RFC-8599 servers.
 */
typedef enum {
	BARESDK_PUSH_PROVIDER_NONE         = 0, /* no push params (default) */
	BARESDK_PUSH_PROVIDER_APNS         = 1, /* Apple APNs production    */
	BARESDK_PUSH_PROVIDER_APNS_SANDBOX = 2, /* Apple APNs development   */
	BARESDK_PUSH_PROVIDER_FCM          = 3, /* Firebase Cloud Messaging  */
} baresdk_push_provider_t;

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
	BARESDK_EV_QUALITY_ALERT,
	BARESDK_EV_NETWORK,
} baresdk_event_type_t;

/* ── Network handover ─────────────────────────────────────────────────────── */

/**
 * Stages of a network handover (Wi-Fi ↔ 4G/5G, VPN up/down, dock/undock).
 *
 * A typical Wi-Fi → cellular handover emits, in order:
 *   CHANGE_DETECTED → TRANSPORT_RESET → REREGISTERING
 *                   → CALL_MIGRATING → CALL_MIGRATED
 *
 * When the device is briefly off-network the sequence starts with DOWN and
 * resumes at UP once a usable address appears.
 */
typedef enum {
	BARESDK_NET_CHANGE_DETECTED = 0,   /* local address set changed          */
	BARESDK_NET_DOWN,                  /* no usable local address            */
	BARESDK_NET_UP,                    /* usable local address again         */
	BARESDK_NET_TRANSPORT_RESET,       /* SIP transports flushed + re-bound  */
	BARESDK_NET_REREGISTERING,         /* REGISTER re-sent on the new path   */
	BARESDK_NET_CALL_MIGRATING,        /* re-INVITE sent to move the media   */
	BARESDK_NET_CALL_MIGRATE_ACCEPTED, /* peer answered; awaiting audio      */
	BARESDK_NET_CALL_MIGRATED,         /* RTP confirmed on the new path      */
	BARESDK_NET_CALL_MIGRATION_FAILED, /* gave up; see `error`               */
	BARESDK_NET_CALL_DEFERRED,         /* not refreshable yet; will retry    */
	BARESDK_NET_HANDOVER_FAILED,       /* transport reset failed; retrying   */
} baresdk_net_event_t;

/**
 * Network handover progress.
 *
 * `attempt` / `max_attempts` render a progress counter:
 *     "link settled — rebuilding the media path %u/%u"
 * `elapsed_ms` on CALL_MIGRATED is how long audio was interrupted:
 *     "media recovered after %.1f s"
 */
typedef struct {
	baresdk_net_event_t      event;
	baresdk_call_handle_t    call;     /* CALL_* events only; else NULL    */
	baresdk_account_handle_t account;  /* REREGISTERING only; else NULL    */
	const char              *local_addr; /* new local IP, "" when unknown  */
	uint32_t                 attempt;  /* 1-based retry counter            */
	uint32_t                 max_attempts; /* ceiling for `attempt`        */
	uint32_t                 elapsed_ms;   /* ms since this migration began */
	/**
	 * True when the call negotiated ICE.  Media recovery is best-effort
	 * for these: the re-INVITE re-runs ICE against the existing local
	 * candidates but does NOT perform an RFC 8445 §9 ICE restart, because
	 * baresip fixes the local ufrag/pwd when the media session is created
	 * and exposes no way to regenerate them mid-call.
	 */
	bool                     ice;
	baresdk_error_t          error;    /* BARESDK_OK unless *_FAILED       */
} baresdk_ev_network_t;

/* ── Event payload structs ────────────────────────────────────────────────── */
typedef struct {
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

	/* ── Packet counters ───────────────────────────────────────────────── */
	uint32_t packets_sent;
	uint32_t packets_received;
	uint32_t packets_lost;      /* TX-side: packets remote did not receive */
	uint32_t packets_lost_rx;   /* RX-side: packets we did not receive */
	uint32_t bytes_sent;        /* total RTP bytes sent */
	uint32_t bytes_received;    /* total RTP bytes received */
	uint32_t tx_errors;         /* RTP transmit errors */
	uint32_t rx_errors;         /* RTP receive errors */

	/* ── Loss ──────────────────────────────────────────────────────────── */
	float    loss_pct;          /* TX-side loss % */
	float    loss_pct_rx;       /* RX-side loss % */

	/* ── Delay / jitter ────────────────────────────────────────────────── */
	float    jitter_ms;         /* RX interarrival jitter (what we observe) */
	float    tx_jitter_ms;      /* TX interarrival jitter (what remote reports) */
	float    rtt_ms;            /* round-trip time ms */

	/* ── Jitter buffer ─────────────────────────────────────────────────── */
	uint32_t jitter_buffer_ms;         /* current adaptive buffer depth (ms) */
	uint32_t jitter_buffer_load;       /* packets currently held in buffer */
	uint32_t late_packets;             /* packets that arrived too late */
	uint32_t discarded_packets;        /* packets discarded (overflow / flush) */
	uint32_t jitter_buffer_target_ms;  /* JB current jitter estimate (ms) */
	bool     jitter_buffer_adaptive;   /* true when JB is in adaptive mode */

	/* ── PLC (packet loss concealment) ─────────────────────────────────── */
	uint32_t plc_frames;  /* frames lost at jitter buffer (PLC trigger count) */
	float    plc_ratio;   /* plc_frames / total_rx_frames (0.0–1.0) */

	/* ── Bandwidth ─────────────────────────────────────────────────────── */
	uint32_t bandwidth_kbps_tx;     /* current TX bitrate (kbps) */
	uint32_t bandwidth_kbps_rx;     /* current RX bitrate (kbps) */
	uint32_t avg_bandwidth_kbps_tx; /* session-average TX bitrate (kbps) */
	uint32_t avg_bandwidth_kbps_rx; /* session-average RX bitrate (kbps) */

	/* ── MOS scores — zero when RTCP not yet available ─────────────────── */
	float    mos_lq;            /* TX-path listening quality  (1.0–4.5) */
	float    mos_cq;            /* TX-path conversational quality (1.0–4.5) */
	float    mos_lq_rx;         /* RX-path listening quality (patient → you) */
	float    mos_cq_rx;         /* RX-path conversational quality */
	baresdk_mos_method_t mos_method;

	/* ── Codec ─────────────────────────────────────────────────────────── */
	const char *codec_name;     /* e.g. "opus", "PCMU" */
	uint32_t    codec_clock_rate; /* RTP clock rate Hz */
	uint32_t    codec_sample_rate;/* audio sample rate Hz */
	uint8_t     codec_channels; /* 1=mono, 2=stereo */
	int         payload_type;   /* RTP payload type number (0-127) */

	/* ── Audio level ───────────────────────────────────────────────────── */
	float    audio_level_dbov;  /* speaker (RX) level dBov (0=max, -127=silent);
	                               NaN when unavailable */
	float    mic_level_dbov;    /* microphone (TX) level dBov (0=max, -127=silent);
	                               NaN when unavailable */

	/* ── Stream identity ───────────────────────────────────────────────── */
	uint32_t ssrc_tx;           /* our SSRC */
	uint32_t ssrc_rx;           /* remote SSRC (0 if not yet received) */
	char     remote_addr[64];   /* remote RTP address "ip:port\0" */

	/* ── Session history — populated after first stats tick ────────────── */
	float    mos_lq_min;        /* worst mos_lq tick this call */
	float    mos_lq_avg;        /* session average mos_lq */
	uint32_t stats_tick;        /* which poll this is (1-based) */
	uint64_t call_duration_ms;  /* elapsed ms since CALL_ESTABLISHED */
	bool     is_final;          /* true on the last event before teardown */
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

typedef enum {
	BARESDK_QUALITY_MOS    = 0,
	BARESDK_QUALITY_LOSS,
	BARESDK_QUALITY_JITTER,
	BARESDK_QUALITY_RTT,
} baresdk_quality_issue_t;

typedef struct {
	baresdk_call_handle_t   call;
	baresdk_quality_issue_t issue;
	float                   value;       /* current metric value */
	float                   threshold;   /* threshold that was crossed */
	bool                    recovering;  /* true = value returned above threshold */
} baresdk_ev_quality_alert_t;

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
		baresdk_ev_quality_alert_t     quality_alert;
		baresdk_ev_network_t           network;
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
 * Release an event delivered with cfg.deliver_owned_events = true.
 * Must be called exactly once per delivered event; may be called from any
 * thread, at any time after delivery.  No-op on NULL.  Passing an event
 * that was NOT delivered in owned mode is undefined behavior.
 */
BARESDK_EXPORT void baresdk_event_release(const baresdk_event_t *ev);

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

	const char  *outbound_proxy; /* NULL = auto from server info */

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
	uint32_t     ws_keepalive_ms;   /* WS ping interval; 0 = libre default (15s); recommended 20000-30000 */

	/* ── NAT ──────────────────────────────────────────────────────── */
	const char  *stun_server;
	const char  *turn_server;
	const char  *turn_user;
	const char  *turn_pass;
	bool         ice_enabled;
	bool         rtcp_mux;    /* multiplex RTCP on RTP port (RFC 5761); default true */

	/* ── Media ────────────────────────────────────────────────────── */
	baresdk_media_enc_t  media_enc;
	baresdk_codec_t      audio_codecs[8]; /* ordered preference list */
	int                  audio_codec_count;
	uint8_t              dscp_sip;  /* 0 = OS default; 24 = AF31 */
	uint8_t              dscp_rtp;  /* 0 = OS default; 46 = EF */
	bool                 enable_video; /* reserved for future video support */

	/* ── Audio processing ─────────────────────────────────────────── */
	baresdk_aec_mode_t aec_mode;  /* echo cancellation backend; default SUPPRESSOR */
	bool  ns;   /* noise suppression */
	bool  agc;  /* automatic gain control */
	float aec_suppression_level;  /* 0=no suppression .. 1=maximum; default 1.0; SUPPRESSOR only */
	float mic_gain_db;            /* TX manual gain dB, clamped [-20,+20]; 0=unity */
	float speaker_gain_db;        /* RX manual gain dB, clamped [-20,+20]; 0=unity */

	/* ── Opus tuning ──────────────────────────────────────────────── */
	baresdk_opus_config_t opus; /* all fields zero/false = use opus defaults */

	/* ── Jitter buffer ────────────────────────────────────────────── */
	baresdk_jbuf_type_t jbuf_type;          /* adaptive (default) or fixed */
	uint32_t jitter_buffer_min_ms; /* JB min depth (default: 40 ms) */
	uint32_t jitter_buffer_max_ms; /* JB max depth (default: 400 ms) */

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
	/* Quality alert thresholds — 0 disables each alert */
	float  mos_alert_threshold;     /* fire QUALITY_ALERT when mos_lq < this (recommended 3.5) */
	float  loss_alert_threshold;    /* fire QUALITY_ALERT when loss_pct > this (recommended 5.0) */
	float  jitter_alert_threshold;  /* fire QUALITY_ALERT when jitter_ms > this (recommended 40.0) */

	/* ── Platform ────────────────────────────────────────────────── */
	/**
	 * Writable temporary directory for internal SDK state (e.g. uuid cache).
	 * NULL = auto-detect: $TMPDIR on POSIX/iOS, GetTempPath() on Windows,
	 * /tmp on Linux.  Android callers MUST set this to the app cache dir
	 * (e.g. context.getCacheDir().getAbsolutePath()) because /tmp does not
	 * exist on Android and $TMPDIR is not reliably set from native code.
	 */
	const char *tmp_dir;

	/* ── Tracing ─────────────────────────────────────────────────── */
	bool        trace_sip;      /* emit BARESDK_EV_SIP_TRACE per message */
	bool        trace_sdp_diff; /* emit BARESDK_EV_SDP_NEGOTIATION */
	const char *pcap_path;      /* NULL = no pcap; path = live capture */

	/* ── Logging & events ────────────────────────────────────────── */
	int                 log_level;      /* 0=err, 1=warn, 2=info, 3=debug */
	baresdk_event_cb_t  event_cb;       /* required */
	void               *event_userdata;

	/* ── Network handover (Wi-Fi ↔ 4G/5G roaming) ────────────────
	 *
	 * On a network change the SDK re-binds its SIP transports,
	 * re-REGISTERs, and re-INVITEs every active call onto the new local
	 * address.  On mobile, drive this from the OS connectivity callback
	 * by calling baresdk_network_changed(); the built-in poller below is
	 * a safety net for platforms with no such callback.
	 */

	/** Interface poll interval in seconds; 0 disables polling.
	 *  Default 10.  Set 0 on mobile and call baresdk_network_changed(). */
	uint32_t  net_monitor_interval_s;

	/** Debounce window: how long the address set must stay stable before
	 *  the handover is applied.  Default 1500 ms.  Prevents thrashing
	 *  while an interface is still coming up. */
	uint32_t  net_settle_ms;

	/** Re-INVITE active calls onto the new local address. Default true. */
	bool      net_reinvite_calls;

	/** Hang up a call whose media could not be migrated instead of
	 *  leaving it up with (probably) dead audio.  Default false. */
	bool      net_hangup_on_migration_failure;

	/** How long to wait for RTP on the new path before declaring the
	 *  migration failed and retrying.  Default 4000 ms; 0 disables the
	 *  media check (the re-INVITE is then assumed to have worked). */
	uint32_t  net_verify_ms;

	/** Maximum handover / re-INVITE attempts before giving up. Default 6. */
	uint32_t  net_max_attempts;

	/* ── Event delivery ownership ─────────────────────────────────
	 *
	 * false (default): event_cb receives a borrowed event, valid only
	 * for the duration of the callback (the historical contract).
	 *
	 * true: event_cb receives a heap-owned clone of the event; the
	 * consumer MUST call baresdk_event_release(ev) exactly once when
	 * done — which may be after the callback has returned.  Required
	 * for bindings that dispatch events asynchronously, e.g. Dart's
	 * NativeCallable.listener, where the callback body runs on the
	 * isolate's event loop after the C call has already returned. */
	bool      deliver_owned_events;

} baresdk_config_t;

/* ── Account config ───────────────────────────────────────────────────────── */

typedef struct {
	/* ── Required ─────────────────────────────────────────────────────── */

	/**
	 * Login URI — who you are and where the server is.
	 * Accepted forms:
	 *   "user@host"            →  AOR sip:user@host, server = host:default_port
	 *   "user@host:port"       →  AOR sip:user@host, server = host:port
	 *   "sip:user@host"        →  same as "user@host"
	 * Required.
	 */
	const char          *uri;

	const char          *password;    /* digest auth password — required */

	/* ── Transport & server ────────────────────────────────────────────── */

	/**
	 * SIP transport protocol.  Defaults to BARESDK_TRANSPORT_UDP (0).
	 * Ignored when server_url is set (transport is derived from the URL scheme).
	 */
	baresdk_transport_t  transport;

	/**
	 * Override the server address.  NULL = use the host:port from uri.
	 * Useful when the SIP domain differs from the physical server address.
	 */
	const char          *server_host;  /* NULL = host from uri */
	uint16_t             server_port;  /* 0    = port from uri, or transport default */

	/**
	 * Full server URL for WebSocket transports or non-standard paths.
	 *   "wss://pbx.example.com:443/ws"
	 *   "ws://pbx.internal:8088/"
	 * When set, overrides transport, server_host, and server_port.
	 */
	const char          *server_url;

	/* ── Identity ──────────────────────────────────────────────────────── */

	const char          *auth_user;    /* NULL = user part of uri */
	const char          *display_name; /* NULL = omit */

	/* ── Media / NAT ──────────────────────────────────────────────────── */

	baresdk_media_enc_t  media_enc;   /* BARESDK_MEDIA_ENC_NONE / SDES / DTLS_SRTP */
	bool                 ice_enabled; /* false by default */
	bool                 rtcp_mux;     /* used when rtcp_mux_set is true */
	bool                 rtcp_mux_set; /* false = inherit global; true = use rtcp_mux above */
	const char          *stun_server; /* NULL = no STUN, e.g. "stun:stun.l.google.com:19302" */
	const char          *turn_server; /* NULL = no TURN, e.g. "turn:turn.example.com:3478" */
	const char          *turn_user;
	const char          *turn_pass;

	/* ── Advanced overrides ────────────────────────────────────────────── */

	const char          *outbound;   /* NULL = auto-derived from server */
	const char          *outbound_proxy; /* alias for outbound; NULL = auto */
	bool                 verify_tls; /* false = skip TLS cert verification */

	/* ── Push notifications ─────────────────────────────────────────────── */

	/**
	 * Push notification provider.  NONE (0) disables push params (default).
	 *
	 * When set, pn-provider/pn-prid/pn-param URI parameters are added to
	 * the Contact header of every REGISTER per RFC 8599.  The server reads
	 * these and sends an APNs/FCM push when a SIP INVITE arrives for an
	 * offline device.
	 *
	 * The host app is responsible for obtaining the OS push token
	 * (PKPushRegistry on iOS, FirebaseMessaging on Android) and passing it
	 * here before calling baresdk_account_register().
	 */
	baresdk_push_provider_t  push_provider;

	/**
	 * Device push token string.
	 * APNs: hex-encoded device token (128 hex chars, 64 bytes).
	 * FCM:  registration token (~152 chars).
	 * NULL or empty → push params omitted from Contact URI.
	 */
	const char              *push_token;

	/**
	 * Provider-specific parameter (pn-param).
	 * APNs: app bundle ID, e.g. "com.example.MyApp".
	 * FCM:  app package name, e.g. "com.example.myapp".
	 * NULL → pn-param omitted.
	 */
	const char              *push_param;

	/* ── Audio codec override ──────────────────────────────────────────────── */

	/**
	 * Per-account audio codec preference list (ordered, highest priority first).
	 * When audio_codec_count > 0, this list overrides the global cfg.audio_codecs.
	 * When audio_codec_count == 0 (default), the global list is used.
	 *
	 * Example — prefer PCMU, fall back to Opus:
	 *   cfg.audio_codecs[0]   = BARESDK_CODEC_PCMU;
	 *   cfg.audio_codecs[1]   = BARESDK_CODEC_OPUS;
	 *   cfg.audio_codec_count = 2;
	 */
	baresdk_codec_t  audio_codecs[8];
	int              audio_codec_count; /* 0 = use global cfg codecs */

	/**
	 * String-based codec list — takes precedence over audio_codecs[] when
	 * audio_codec_name_count > 0.  Names are matched case-insensitively.
	 *
	 * Common values (aliases accepted):
	 *   "opus"              — Opus 48 kHz stereo
	 *   "ulaw" / "pcmu"     — G.711 µ-law
	 *   "alaw" / "pcma"     — G.711 A-law
	 *   "g722"              — G.722 wideband
	 *   "g729"              — G.729 (requires licensed module)
	 *   "g726" / "g726-32"  — G.726 at 32 kbps
	 *
	 * Example — prefer µ-law, fall back to Opus:
	 *   strcpy(cfg.audio_codec_names[0], "ulaw");
	 *   strcpy(cfg.audio_codec_names[1], "opus");
	 *   cfg.audio_codec_name_count = 2;
	 */
	char  audio_codec_names[8][32];  /* codec name strings, each ≤ 31 chars */
	int   audio_codec_name_count;    /* 0 = fall back to audio_codecs[] / global */

	/* ── DTMF ──────────────────────────────────────────────────────────── */
	baresdk_dtmf_mode_t dtmf_mode; /* default BARESDK_DTMF_RFC4733 (0) */

} baresdk_account_config_t;

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

/** Zero-fill cfg and set version/struct_size correctly. Call before populating. */
BARESDK_EXPORT void baresdk_config_init(baresdk_config_t *cfg);

/**
 * Initialize the baresdk stack. Call once per process before any other API.
 * Spawns re_main thread and event dispatch thread internally.
 * Returns BARESDK_OK or a negative BARESDK_ERR_* code.
 */
BARESDK_EXPORT int baresdk_init(const baresdk_config_t *cfg);

/**
 * Tear down the stack. Blocks until all internal threads have exited.
 * All active accounts and calls are forcibly terminated first.
 */
BARESDK_EXPORT void baresdk_shutdown(void);

/* ── Accounts ─────────────────────────────────────────────────────────────── */

/**
 * Create a SIP account. Does NOT register — call baresdk_account_register().
 * Thread-safe; may be called from any thread after baresdk_init().
 */
BARESDK_EXPORT int baresdk_account_create(const baresdk_account_config_t *cfg,
                             baresdk_account_handle_t *out);

/** Destroy account and all associated calls. Unregisters first if registered. */
BARESDK_EXPORT void baresdk_account_destroy(baresdk_account_handle_t acct);

/** Begin registration. Fires BARESDK_EV_REG_STATE events on state changes. */
BARESDK_EXPORT int baresdk_account_register(baresdk_account_handle_t acct);

/** Unregister (sends REGISTER with Expires: 0). */
BARESDK_EXPORT int baresdk_account_unregister(baresdk_account_handle_t acct);

/**
 * Override the retry policy for this account. Takes effect on the next retry.
 * Overrides the global reg_retry_* fields in baresdk_config_t for this account only.
 * @param initial_ms   First retry delay in ms (e.g. 2000)
 * @param max_ms       Maximum retry delay cap in ms (e.g. 300000)
 * @param backoff      Delay multiplier per attempt (e.g. 2.0)
 * @param max_attempts Max attempts before giving up; 0 = retry forever
 */
BARESDK_EXPORT int baresdk_account_set_retry_policy(baresdk_account_handle_t acct,
                                                     uint32_t initial_ms,
                                                     uint32_t max_ms,
                                                     float    backoff,
                                                     uint32_t max_attempts);

/** Cancel a pending retry timer and reset the attempt counter.
 *  The account stays in FAILED state; call baresdk_account_register() to restart. */
BARESDK_EXPORT int baresdk_account_cancel_retry(baresdk_account_handle_t acct);

/** Skip the current backoff delay and re-register immediately.
 *  Resets the attempt counter. No-op if the account is not in a retry loop. */
BARESDK_EXPORT int baresdk_account_retry_now(baresdk_account_handle_t acct);

/**
 * Update the push token for an account at runtime (e.g. on OS token rotation).
 *
 * The new token is stored and the Contact URI params are updated.  A new
 * REGISTER is sent immediately unless the account is mid-call, mid-transaction,
 * or in retry backoff — in those cases the update is deferred to the next
 * natural re-registration.  Callers that need the token applied before the
 * next call should wait for BARESDK_EV_CALL_STATE ENDED first.
 *
 * Pass NULL to clear push params and re-register without pn-* Contact params.
 *
 * @param acct        Account handle (must have push_provider set in config)
 * @param push_token  New device token string, or NULL to clear
 * @return BARESDK_OK on success, BARESDK_ERR_STATE if account has no UA yet
 */
BARESDK_EXPORT int baresdk_account_set_push_token(baresdk_account_handle_t acct,
                                                   const char *push_token);

/**
 * Add a custom SIP header that is sent on REGISTER requests only.
 *
 * Use this for vendor push schemes that use non-standard headers (e.g.
 * "X-Push-Token", "X-Apple-Push-Bundle-Id") on hosted servers where you
 * cannot configure server-side push dispatch.  Unlike
 * baresdk_account_add_header(), this header is NOT included on INVITE,
 * BYE, or REFER — the token is not leaked to call peers.
 *
 * Multiple calls accumulate headers.  The list is append-only for the
 * lifetime of the account; recreate the account to clear it.
 *
 * @param name   Header field name  (e.g. "X-Push-Token")
 * @param value  Header field value (e.g. "a1b2c3...")
 */
BARESDK_EXPORT int baresdk_account_add_register_header(
        baresdk_account_handle_t acct,
        const char *name, const char *value);

/**
 * Add a custom SIP header to all outgoing requests for this account.
 * @param name   Header field name  (e.g. "X-Tenant-Id")
 * @param value  Header field value (e.g. "12345")
 */
BARESDK_EXPORT int baresdk_account_add_header(baresdk_account_handle_t acct,
                                const char *name, const char *value);

/**
 * Subscribe to presence state changes for a contact (SUBSCRIBE/NOTIFY).
 * Presence updates are delivered via BARESDK_EV_PRESENCE_STATE events.
 * @param target_uri  SIP URI of the contact to subscribe to
 * @return BARESDK_OK on success, or negative error code
 */
BARESDK_EXPORT int baresdk_account_subscribe_presence(baresdk_account_handle_t acct,
                                        const char *target_uri);

/**
 * Unsubscribe from presence state changes for a contact.
 * @param target_uri  SIP URI of the contact to unsubscribe from
 * @return BARESDK_OK on success, or negative error code
 */
BARESDK_EXPORT int baresdk_account_unsubscribe_presence(baresdk_account_handle_t acct,
                                          const char *target_uri);

/* ── Calls ────────────────────────────────────────────────────────────────── */

/**
 * Initiate an outgoing call. Returns immediately; fires BARESDK_EV_CALL_STATE
 * CALLING, then RINGING, then ESTABLISHED (or FAILED).
 */
BARESDK_EXPORT int baresdk_call_invite(baresdk_account_handle_t acct,
                         const char *uri,
                         baresdk_call_handle_t *out);

/** Answer an incoming call (received via BARESDK_EV_INCOMING_CALL). */
BARESDK_EXPORT int baresdk_call_answer(baresdk_call_handle_t call);

/** Terminate a call with BYE. */
BARESDK_EXPORT int baresdk_call_hangup(baresdk_call_handle_t call);

/** Put call on hold (re-INVITE with sendonly). */
BARESDK_EXPORT int baresdk_call_hold(baresdk_call_handle_t call);

/** Resume a held call (re-INVITE with sendrecv). */
BARESDK_EXPORT int baresdk_call_resume(baresdk_call_handle_t call);

/** Return true if the call is currently on local hold. */
BARESDK_EXPORT bool baresdk_call_is_held(baresdk_call_handle_t call);

/** Send DTMF digit via RFC 4733 RTP events. digit: '0'-'9', '*', '#', 'A'-'D'. */
BARESDK_EXPORT int baresdk_call_send_dtmf(baresdk_call_handle_t call, char digit);

/** Blind transfer via REFER. */
BARESDK_EXPORT int baresdk_call_transfer(baresdk_call_handle_t call, const char *uri);

/**
 * Add a custom SIP header to a specific call/dialog.
 * Headers are applied to subsequent re-INVITEs, BYE, and REFER messages
 * within this dialog. Must be called while the call is active.
 * @param name   Header field name  (e.g. "X-Call-Id")
 * @param value  Header field value (e.g. "12345")
 */
BARESDK_EXPORT int baresdk_call_add_header(baresdk_call_handle_t call,
                             const char *name, const char *value);

/**
 * Attended transfer: send REFER w/ Replaces on call_a, bridging it to call_b.
 * call_a is the call to transfer away; call_b is the already-established
 * consultation call whose dialog info is embedded in Replaces.
 */
BARESDK_EXPORT int baresdk_call_attended_transfer(baresdk_call_handle_t call_a,
                                    baresdk_call_handle_t call_b);

/** Called once per active call by baresdk_call_foreach(). */
typedef void (*baresdk_call_iter_fn)(baresdk_call_handle_t call, void *arg);

/** Iterate all active calls. Safe to call from any thread. */
BARESDK_EXPORT void baresdk_call_foreach(baresdk_call_iter_fn fn, void *arg);

/* ── SIP MESSAGE ─────────────────────────────────────────────────────────── */

/**
 * Send a SIP MESSAGE (instant message) out of dialog.
 * content_type defaults to "text/plain" if NULL.
 * Fires BARESDK_EV_MESSAGE on the remote end when received.
 */
BARESDK_EXPORT int baresdk_message_send(baresdk_account_handle_t account,
                          const char *to_uri,
                          const char *body,
                          const char *content_type);

/* ── Presence ────────────────────────────────────────────────────────────── */

/**
 * Publish presence status for the account (PUBLISH request).
 * Fires BARESDK_EV_PRESENCE_STATE on subscribed watchers.
 */
BARESDK_EXPORT int baresdk_account_publish_presence(baresdk_account_handle_t account,
                                      baresdk_presence_status_t status);

/* ── 100rel / PRACK ──────────────────────────────────────────────────────── */

/**
 * Set the RFC 3262 100rel mode for an account.
 * Must be called before baresdk_call_invite / baresdk_call_answer.
 */
BARESDK_EXPORT int baresdk_account_set_100rel(baresdk_account_handle_t account,
                                baresdk_100rel_mode_t mode);

/* ── Audio ────────────────────────────────────────────────────────────────── */

/* ── Audio device enumeration ─────────────────────────────────────────────── */

typedef struct {
	char name[128];         /* device name passed to set_input/output_device */
	char description[256];  /* human-readable label (may be empty) */
	bool is_default;        /* true for the platform default device */
} baresdk_audio_device_t;

/**
 * Fill devices[] with available audio input devices, up to max_count entries.
 * Returns the number of entries written (0 if no devices found yet).
 * Call after baresdk_init(); device lists may be empty until the audio
 * module has finished async enumeration.
 */
BARESDK_EXPORT int baresdk_audio_list_input_devices(baresdk_audio_device_t *devices,
                                                     int max_count);

/** Same for audio output (speaker/playback) devices. */
BARESDK_EXPORT int baresdk_audio_list_output_devices(baresdk_audio_device_t *devices,
                                                      int max_count);

/* ── Audio processing — runtime toggles ──────────────────────────────────── */

/**
 * Enable or disable echo cancellation (takes effect on next audio frame).
 * Re-enables the aec_mode that was configured at baresdk_init().
 * set_aec(false) → AEC_OFF; set_aec(true) → restores init mode.
 * Back-compat: behavior is identical to the former bool aec API.
 */
BARESDK_EXPORT void baresdk_set_aec(bool enable);

/**
 * Switch the AEC backend.  Only AEC_OFF ↔ init_mode transitions are valid
 * at runtime — module_load is one-way.  Switching between SUPPRESSOR and
 * WEBRTC at runtime returns EINVAL.  WEBRTC returns ENOTSUP on mobile builds
 * and when BARESDK_WITH_WEBRTC_AEC was not set at build time.
 * Requires bsdk_dispatch_sync — NOT safe to call from the audio thread.
 */
BARESDK_EXPORT int baresdk_set_aec_mode(baresdk_aec_mode_t mode);

/**
 * Tune the built-in TX echo suppressor aggressiveness.  SUPPRESSOR mode only.
 * 0.0 = no TX suppression (passes through even when far end is loud).
 * 1.0 = maximum suppression (default — −16.5 dB floor on TX when RX active).
 * Takes effect on the next audio frame via atomic store; safe from any thread.
 */
BARESDK_EXPORT void baresdk_set_aec_suppression_level(float level);

/** Enable/disable noise suppression globally (takes effect next frame). */
BARESDK_EXPORT void baresdk_set_ns(bool enable);

/** Enable/disable automatic gain control globally (takes effect next frame). */
BARESDK_EXPORT void baresdk_set_agc(bool enable);

/**
 * Set manual microphone (TX) gain in dB.  Clamped to [-20, +20].
 * 0.0 = unity — fast-path bypass, no per-sample work.
 * Applied before NS/AGC/AEC in the encode chain (raw pre-boost).
 * Takes effect on the next audio frame via atomic store; safe from any thread.
 */
BARESDK_EXPORT void baresdk_set_mic_gain_db(float db);

/**
 * Set manual speaker (RX) gain in dB.  Clamped to [-20, +20].
 * 0.0 = unity — fast-path bypass.
 * Applied after the jitter buffer, before playback.
 * Takes effect on the next audio frame via atomic store; safe from any thread.
 */
BARESDK_EXPORT void baresdk_set_speaker_gain_db(float db);

/**
 * Change DSCP/TOS on the RTP socket of an active call.
 * Common values: 46 (EF — voice), 34 (AF41 — video), 0 (best-effort).
 * Takes effect immediately on the next outgoing RTP packet.
 */
BARESDK_EXPORT int baresdk_call_set_dscp_rtp(baresdk_call_handle_t call,
                                              uint8_t dscp);

/**
 * Update jitter buffer bounds.  Takes effect on calls established after
 * this call — existing streams keep their current adaptive depth.
 * Pass 0 for either bound to restore the baresip default (~0 / 150 ms).
 */
BARESDK_EXPORT void baresdk_set_jitter_buffer(uint32_t min_ms, uint32_t max_ms);

/** Set jitter buffer type (adaptive or fixed). Takes effect on new calls. */
BARESDK_EXPORT void baresdk_set_jitter_buffer_type(baresdk_jbuf_type_t type);

/* ── Audio mute / device control ─────────────────────────────────────────── */

/** Mute/unmute the microphone (TX path) for a call. */
BARESDK_EXPORT int baresdk_audio_mute(baresdk_call_handle_t call, bool mute);

/** Return true if TX audio is currently muted on this call. */
BARESDK_EXPORT bool baresdk_audio_is_muted(baresdk_call_handle_t call);

/** Mute/unmute the speaker (RX path) for a call — silences incoming audio. */
BARESDK_EXPORT int baresdk_audio_mute_rx(baresdk_call_handle_t call, bool mute);

/** Set the system audio input device by name. NULL = platform default. */
BARESDK_EXPORT int baresdk_audio_set_input_device(const char *name);

/** Set the system audio output device by name. NULL = platform default. */
BARESDK_EXPORT int baresdk_audio_set_output_device(const char *name);

/* ── Media tap ────────────────────────────────────────────────────────────── */

/**
 * Install a PCM tap on an active call.
 * cb is called from the audio thread for both RX (decoded) and TX (pre-encode)
 * directions. Must be non-blocking — heavy work must go to your own thread.
 * Pass NULL cb to remove the tap.
 */
BARESDK_EXPORT int baresdk_call_set_media_tap(baresdk_call_handle_t   call,
                                baresdk_media_tap_cb_t  cb,
                                void                   *userdata);

/* ── Audio recording ──────────────────────────────────────────────────────── */

/**
 * Start recording call audio to a single mixed WAV file (PCM S16LE).
 * Both the received (RX) and sent (TX) audio are clip-summed into one stream.
 * The WAV header is written on the first audio frame and finalized on stop.
 * Returns EALREADY if recording is already active.
 */
BARESDK_EXPORT int baresdk_call_record_start(baresdk_call_handle_t call,
                                              const char *path);

/** Stop recording and finalize WAV headers. Idempotent. */
BARESDK_EXPORT int baresdk_call_record_stop(baresdk_call_handle_t call);

/* ── Stats ────────────────────────────────────────────────────────────────── */

/**
 * Synchronously retrieve current stats for a call.
 * Also delivered automatically via BARESDK_EV_MEDIA_STATS if
 * cfg.stats_interval_ms > 0.
 */
BARESDK_EXPORT int baresdk_call_get_stats(baresdk_call_handle_t     call,
                            baresdk_ev_media_stats_t *out);

/* ── Network handover ─────────────────────────────────────────────────────── */

/**
 * Tell the SDK that the underlying network may have changed.
 *
 * Call this from the platform's connectivity callback:
 *   Android — ConnectivityManager.NetworkCallback onAvailable / onLost /
 *             onCapabilitiesChanged
 *   iOS     — NWPathMonitor pathUpdateHandler
 *   Desktop — NetworkManager / SCNetworkReachability / NotifyAddrChange, or
 *             leave it to the built-in poller (cfg.net_monitor_interval_s).
 *
 * Safe to call from any thread and as often as the OS fires.  Calls are
 * coalesced: the handover runs once the address set has been stable for
 * cfg.net_settle_ms.  Returns immediately — progress is reported through
 * BARESDK_EV_NETWORK events.
 *
 * The handover performs, in order:
 *   1. re-scan local addresses and refresh the DNS resolver list
 *   2. flush and re-bind all SIP transports (drops dead TCP/TLS/WS sockets)
 *   3. re-REGISTER every account the app asked to be registered
 *   4. re-INVITE every active call with the new local address in the SDP
 *   5. verify RTP resumes on the new path, retrying the re-INVITE if not
 *
 * @return BARESDK_OK, or BARESDK_ERR_STATE if the SDK is not initialized.
 */
BARESDK_EXPORT int baresdk_network_changed(void);

/**
 * Change the built-in interface poll interval at runtime.
 * @param seconds  Poll period; 0 disables polling entirely.
 */
BARESDK_EXPORT int baresdk_network_set_monitor_interval(uint32_t seconds);

/**
 * Adjust handover behaviour at runtime (overrides the cfg.net_* fields).
 * @param reinvite_calls    Re-INVITE active calls onto the new address.
 * @param hangup_on_failure Hang up calls whose media could not be migrated.
 */
BARESDK_EXPORT int baresdk_network_set_handover_policy(bool reinvite_calls,
                                                        bool hangup_on_failure);

/**
 * Copy the local IP the SDK is currently using into buf.
 * Writes an empty string when no usable address exists.
 * @return BARESDK_OK, or BARESDK_ERR_INVAL / BARESDK_ERR_STATE.
 */
BARESDK_EXPORT int baresdk_network_local_addr(char *buf, size_t sz);

/** False while the device has no usable (non-loopback) local address. */
BARESDK_EXPORT bool baresdk_network_is_up(void);

/* ── pcap ─────────────────────────────────────────────────────────────────── */

/**
 * Start capturing SIP + RTP to a Wireshark-compatible pcap file.
 * Writes synthetic Ethernet/IP/UDP headers around each SIP message.
 */
BARESDK_EXPORT int baresdk_pcap_start(const char *path);

/** Stop capture and flush/close the pcap file. */
BARESDK_EXPORT int baresdk_pcap_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* BARESDK_H */
