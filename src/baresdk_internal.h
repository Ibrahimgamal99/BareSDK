/**
 * @file baresdk_internal.h  Internal types — never exposed to consumers.
 */

#ifndef BARESDK_INTERNAL_H
#define BARESDK_INTERNAL_H

#include <string.h>
#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif
#include <re.h>
#include <baresip.h>
#include "../include/baresdk.h"

/* ── Forward declarations ──────────────────────────────────────────────── */

struct baresdk_account;
struct baresdk_call;
struct baresdk_queued_event;

/* ── baresip internal functions (defined in libbaresip but not in baresip.h) */
extern struct rtp_sock   *stream_rtp_sock(const struct stream *strm);
extern const struct sa   *stream_raddr(const struct stream *strm);
extern int                stream_pt_enc(const struct stream *strm);
extern int                stream_ssrc_rx(const struct stream *strm,
                                         uint32_t *ssrc);

/* ── Global singleton ──────────────────────────────────────────────────── */

struct bsdk_ctx {
	mtx_t              lock;        /* guards initialized + shutdown */
	bool               initialized;

	baresdk_config_t   cfg;         /* deep-copied from caller */

	/* Deep-copied string storage for cfg pointer fields */
	char              *cfg_local_ip;
	char              *cfg_sip_domain;
	char              *cfg_server_url;
	char              *cfg_server_host;
	char              *cfg_outbound_proxy;
	char              *cfg_ca_cert_path;
	char              *cfg_client_cert;
	char              *cfg_client_key;
	char              *cfg_sni_hostname;
	char              *cfg_user_agent;
	char              *cfg_ws_origin;
	char             **cfg_ws_extra_headers;
	char              *cfg_stun_server;
	char              *cfg_turn_server;
	char              *cfg_turn_user;
	char              *cfg_turn_pass;
	char              *cfg_pcap_path;
	char              *cfg_tmp_dir;

	/* re_main thread */
	thrd_t             re_thread;
	bool               re_thread_running;
	struct mqueue     *re_wakeup_mq;  /* wakes select() so re_cancel takes effect */

	/* Event dispatch thread (separate from re_main to prevent consumer
	 * deadlocks when they call back into baresdk from inside an event) */
	thrd_t             ev_thread;
	mtx_t              ev_lock;
	cnd_t              ev_cond;
	struct list        ev_queue;    /* struct baresdk_queued_event */
	bool               ev_shutdown;
	size_t             ev_queue_len;
	size_t             ev_queue_max;

	/* Account list */
	struct list        accounts;    /* struct baresdk_account */
	mtx_t              acct_lock;

	/* pcap */
	FILE              *pcap_file;
	mtx_t              pcap_lock;

	/* Stats polling timer (fires on re_main) */
	struct tmr         stats_tmr;
};

extern struct bsdk_ctx g_bsdk;

/* ── Init/shutdown trace ───────────────────────────────────────────────────
 * Verbose "[bsdk] step N: ..." traces are gated behind the BARESDK_DEBUG_INIT
 * env var. Set BARESDK_DEBUG_INIT=1 to see them; default is silent. */
int bsdk_trace_enabled(void);
#define BSDK_TRACE(...) \
	do { if (bsdk_trace_enabled()) { printf(__VA_ARGS__); fflush(stdout); } } while (0)

/* ── Account ───────────────────────────────────────────────────────────── */

struct baresdk_account {
	struct le                 le;
	struct ua                *ua;
	baresdk_account_config_t  cfg;
	/* heap-allocated copies of cfg string fields */
	char                     *cfg_uri;
	char                     *cfg_password;
	char                     *cfg_server_host;
	char                     *cfg_server_url;
	char                     *cfg_auth_user;
	char                     *cfg_display_name;
	char                     *cfg_stun_server;
	char                     *cfg_turn_server;
	char                     *cfg_turn_user;
	char                     *cfg_turn_pass;
	char                     *cfg_outbound;
	char                     *cfg_outbound_proxy;  /* alias for outbound */
	char                     *cfg_push_token;
	char                     *cfg_push_param;
	/* derived from uri at create time */
	char                      parsed_user[64];
	char                      parsed_host[256];
	uint16_t                  parsed_port;
	baresdk_transport_t       parsed_transport;
	char                      auto_server_url[512];
	baresdk_reg_state_t       reg_state;
	baresdk_error_t           reg_error;
	char                      reg_error_str[256];
	uint32_t                  retry_attempt;
	struct tmr                retry_tmr;
	bool                      retry_policy_set;
	uint32_t                  retry_initial_ms;
	uint32_t                  retry_max_ms;
	float                     retry_backoff;
	uint32_t                  retry_max_attempts;
	bool                      destroyed;
	struct list               custom_hdrs;
};

/* ── Call ──────────────────────────────────────────────────────────────── */

struct baresdk_call {
	struct le                  le;
	struct call               *bc;          /* baresip call (weak ref) */
	struct baresdk_account    *acct;
	baresdk_call_state_t       state;
	/* SDP capture for BARESDK_EV_SDP_NEGOTIATION */
	char                       local_sdp[4096];
	char                       remote_sdp[4096];
	bool                       local_sdp_set;
	bool                       remote_sdp_set;
	/* Media tap */
	baresdk_media_tap_cb_t     tap_cb;
	void                      *tap_userdata;
	mtx_t                      tap_lock;
	/* Audio recording (WAV) */
	FILE                      *rec_file;        /* single mixed output file */
	mtx_t                      rec_lock;
	uint32_t                   rec_data_bytes;
	uint32_t                   rec_srate;
	uint8_t                    rec_ch;
	bool                       rec_hdr_written;
	bool                       rec_active;
	int16_t                    rec_tx_buf[4096]; /* last TX frame for mixing */
	size_t                     rec_tx_count;
	/* Per-dialog custom headers (linked list of bsdk_custom_hdr) */
	struct list                custom_hdrs;
	/* Session stats history — maintained by stats.c */
	float                      stats_mos_min;    /* worst mos_lq this call */
	float                      stats_mos_sum;    /* running sum for average */
	uint32_t                   stats_tick;       /* poll counter (1-based) */
	uint64_t                   stats_call_start; /* tmr_jiffies() at ESTABLISHED */
	/* Previous-tick values for quality alert threshold crossing detection */
	float                      last_mos_lq;
	float                      last_loss_pct;
	float                      last_jitter_ms;
	/* RX/TX audio levels — written by audio thread, read by stats timer.
	 * Stored as bit-pattern of float dBov (0=max, -127=silence).
	 * Initialized to -127 (silence) so reads before the first frame
	 * return a usable value rather than NaN. */
	RE_ATOMIC uint32_t         rx_level_bits;
	RE_ATOMIC uint32_t         tx_level_bits;
};

/* ── Event queue entry ─────────────────────────────────────────────────── */

struct baresdk_queued_event {
	struct le            le;
	baresdk_event_t      ev;
	/* Inline string storage — pointers in ev.u may point into buf. */
	char                 buf[4096];
	/* If set, mem_deref'd on the event thread after the app callback returns.
	 * Used to defer call-wrapper cleanup until after CALL_ENDED is delivered. */
	struct baresdk_call *deref_after_deliver;
};

/* ── dispatch.c ────────────────────────────────────────────────────────── */

typedef void (*bsdk_main_fn)(void *arg);

/* Post fn(arg) to run on the re_main thread. Returns immediately. */
int bsdk_dispatch(bsdk_main_fn fn, void *arg);

/* Post fn(arg) to re_main and block until it completes. */
int bsdk_dispatch_sync(bsdk_main_fn fn, void *arg);

/* ── event.c ───────────────────────────────────────────────────────────── */

int  bsdk_event_init(void);
void bsdk_event_close(void);
void bsdk_event_post(const baresdk_event_t *ev);
/* Takes ownership of qev; frees it if queue is full. Returns true on success. */
bool bsdk_event_post_qev(struct baresdk_queued_event *qev);

/* ── log.c ─────────────────────────────────────────────────────────────── */

int  bsdk_log_init(void);
void bsdk_log_close(void);

/* ── re_loop.c ─────────────────────────────────────────────────────────── */

int  bsdk_re_loop_start(void);
void bsdk_re_loop_stop(void);

/* ── modules_init.c ────────────────────────────────────────────────────── */

int modules_init(void);

/* ── account.c ─────────────────────────────────────────────────────────── */

struct baresdk_account *bsdk_account_find_by_ua(const struct ua *ua);
void bsdk_account_schedule_retry(struct baresdk_account *acct);

/* ── call.c ────────────────────────────────────────────────────────────── */

void bsdk_call_destructor(void *data);  /* mem_alloc destructor for call wrappers */
/* ── record.c ──────────────────────────────────────────────────────────── */

void bsdk_record_write_frame(struct baresdk_call *lc, baresdk_media_dir_t dir,
                              const int16_t *pcm, size_t samples,
                              uint32_t srate, uint8_t ch);
struct baresdk_call *bsdk_call_find(const struct call *bc);
void bsdk_call_register(struct baresdk_call *lc);
void bsdk_call_unregister(struct baresdk_call *lc);
void bsdk_call_foreach(void (*fn)(struct baresdk_call *, void *), void *arg);

/* ── timers.c ──────────────────────────────────────────────────────────── */

void bsdk_timers_configure(const baresdk_config_t *cfg);

/* ── trace.c ───────────────────────────────────────────────────────────── */

int  bsdk_trace_init(void);
void bsdk_trace_close(void);

/* ── sdp.c ─────────────────────────────────────────────────────────────── */

void bsdk_sdp_handle_event(enum bevent_ev ev, struct bevent *event);

/* ── transfer.c ────────────────────────────────────────────────────────── */

void bsdk_transfer_handle_event(struct bevent *event);

/* ── message.c ─────────────────────────────────────────────────────────── */

int  bsdk_message_init(void);
void bsdk_message_close(void);

/* ── presence.c ────────────────────────────────────────────────────────── */

int  bsdk_presence_init(void);
void bsdk_presence_close(void);
void bsdk_presence_handle_mwi(struct bevent *event);

/* ── pcap.c ────────────────────────────────────────────────────────────── */

int  bsdk_pcap_open(const char *path);
void bsdk_pcap_close(void);
void bsdk_pcap_write_sip(const char *data, size_t len,
                          const struct sa *src, const struct sa *dst,
                          bool is_udp);

/* ── audio_processing.c ────────────────────────────────────────────────── */

void bsdk_audio_processing_init(bool ns, bool agc,
                                baresdk_aec_mode_t aec_mode,
                                float aec_suppression_level,
                                float mic_db, float spk_db);
void bsdk_audio_processing_close(void);

/* Atomic gain accessors used by the setters in audio.c. */
void bsdk_mic_gain_store(float linear);
void bsdk_spk_gain_store(float linear);
void bsdk_aec_floor_store(float floor);

/* ── stats.c ───────────────────────────────────────────────────────────── */

int  bsdk_stats_init(void);
void bsdk_stats_close(void);
void bsdk_stats_collect_final(struct baresdk_call *lc);

/* ── dns.c ─────────────────────────────────────────────────────────────── */

/* Opaque result types — defined in dns.c, used only within the module */
struct bsdk_dns_target;
struct bsdk_dns_result;

typedef void (bsdk_dns_done_h)(const struct bsdk_dns_result *res, void *arg);

int  bsdk_dns_init(void);
void bsdk_dns_close(void);

/* Async RFC 3263 resolution. done_h fires on re_main thread.
 * port_hint > 0 skips NAPTR/SRV (explicit port in URI). */
int  bsdk_dns_resolve(const char *domain,
                      baresdk_transport_t transport_hint,
                      uint16_t port_hint,
                      bsdk_dns_done_h *done_h, void *arg);

/* ── transport.c ───────────────────────────────────────────────────────── */

int bsdk_parse_server_url(const char *url,
                           baresdk_transport_t *out_transport,
                           char *host, size_t host_sz,
                           uint16_t *port,
                           char *path, size_t path_sz);

int bsdk_build_outbound(const char *server_url,
                         const char *server_host, uint16_t server_port,
                         baresdk_transport_t transport,
                         char *buf, size_t buf_sz);

const char *bsdk_transport_str(baresdk_transport_t t);
const char *bsdk_mediaenc_str(baresdk_media_enc_t enc);

/* ── WebSocket path (ws_path.c) ────────────────────────────────────────── */

extern char g_bsdk_ws_path[256];
extern char g_bsdk_ws_server[288];

/* Record the WebSocket server to pin outbound connections to.  Call once per
 * WS/WSS account; a second account naming a different server disables pinning
 * process-wide. */
void bsdk_ws_set_server(baresdk_transport_t tp, const char *host,
                         uint16_t port);

/* ── Utility macros ────────────────────────────────────────────────────── */

/* Safe string copy into a fixed buffer inside a struct. */
#define BSDK_STRCPY(dst, src) \
	do { if (src) str_ncpy((dst), (src), sizeof(dst)); \
	     else (dst)[0] = '\0'; } while (0)

/* ── Per-dialog custom header storage ──────────────────────────────────── */

struct bsdk_custom_hdr {
	struct le  le;
	char      *name;
	char      *value;
};

/* ── Deep-copy helpers ─────────────────────────────────────────────────── */

char *bsdk_strdup(const char *s);
void bsdk_cfg_deep_copy(baresdk_config_t *dst, const baresdk_config_t *src,
                         struct bsdk_ctx *ctx);
void bsdk_cfg_deep_free(struct bsdk_ctx *ctx);
void bsdk_acct_cfg_deep_copy(baresdk_account_config_t *dst,
                              const baresdk_account_config_t *src,
                              struct baresdk_account *acct);
void bsdk_acct_cfg_deep_free(struct baresdk_account *acct);

/* ── Global state reset (call from shutdown) ───────────────────────────── */

void bsdk_call_global_init(void);   /* call once from baresdk_init */
void bsdk_call_global_reset(void);  /* call from baresdk_shutdown / fail path */
void bsdk_tap_global_init(void);
void bsdk_tap_global_reset(void);

#endif /* BARESDK_INTERNAL_H */
