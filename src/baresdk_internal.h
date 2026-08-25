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
/* Declared in baresip's src/core.h — exported from libbaresip but omitted
 * from the installed baresip.h.  Used by netmon.c for call migration. */
extern int                call_reset_transp(struct call *call,
                                            const struct sa *laddr);
extern const struct sa   *call_laddr(const struct call *call);

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
	/* thrd_t carries no "not a thread" value, and joining a zeroed one
	 * segfaults — which is what baresdk_init()'s failure path used to do
	 * whenever it unwound before the event thread had been created. */
	bool               ev_thread_started;
	mtx_t              ev_lock;
	cnd_t              ev_cond;
	struct list        ev_queue;    /* struct baresdk_queued_event */
	bool               ev_shutdown;
	size_t             ev_queue_len;
	size_t             ev_queue_max;
	/* True while the event thread is inside cfg.event_cb. Own condvar, not
	 * ev_cond: a signal on ev_cond means "work queued" and must reach the
	 * event thread, never a drain waiter. */
	bool               ev_delivering;
	cnd_t              ev_idle_cond;

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

/* Cap on SRV targets kept per account for failover.  dns.c returns up to 16;
 * walking more than a handful of dead proxies is slower than backing off and
 * retrying the first one. */
#define BSDK_SRV_MAX_TARGETS 4

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
	/* WS/WSS connection pinning claimed at create time and released in the
	 * destructor — see bsdk_ws_set_server(). ws_port 0 = nothing claimed. */
	char                      ws_host[256];
	char                      ws_pin_path[256];
	uint16_t                  ws_port;
	baresdk_reg_state_t       reg_state;
	baresdk_error_t           reg_error;
	char                      reg_error_str[256];
	/* True while the SDK is recovering this registration on its own — a
	 * retry loop, a dead keepalive path, a network handover.  It is what
	 * makes the recovery's own REGISTER report RECONNECTING instead of
	 * REGISTERING, so the app's status line does not flicker between the
	 * two for every attempt.  Cleared on REGISTERED, on a terminal FAILED
	 * and on unregister. */
	bool                      reconnecting;
	uint32_t                  retry_attempt;
	struct tmr                retry_tmr;
	/* Registration watchdog — see bsdk_account_watch_registration(). */
	struct tmr                reg_watch_tmr;
	uint32_t                  reg_watch_elapsed_ms;
	bool                      retry_policy_set;
	uint32_t                  retry_initial_ms;
	uint32_t                  retry_max_ms;
	float                     retry_backoff;
	uint32_t                  retry_max_attempts;
	bool                      destroyed;
	/* True between baresdk_account_register() and _unregister(): the app
	 * wants this account registered.  netmon.c re-REGISTERs only these, so
	 * a created-but-never-registered account is left alone on handover. */
	bool                      reg_wanted;
	struct list               custom_hdrs;

	/* ── RFC 3263 SRV failover (account.c; re_main thread only) ──────
	 * Outbound-proxy URIs for this account's SRV targets, in the order a
	 * client must try them.  Populated once, asynchronously, on first
	 * register; srv_idx advances one target per failed attempt. */
	char                      srv_uri[BSDK_SRV_MAX_TARGETS][320];
	uint8_t                   srv_count;
	uint8_t                   srv_idx;
	bool                      srv_pending;   /* lookup in flight */
	bool                      srv_tried;     /* lookup already attempted */

	/* ── Keepalive / reachability probe (account.c) ──────────────────── */
	struct tmr                ka_tmr;
	bool                      ka_in_flight;
	/* Last probe went unanswered.  A probe that answers again is the only
	 * signal that such a path recovered without a re-REGISTER, so it is
	 * what turns RECONNECTING back into REGISTERED. */
	bool                      ka_failed;
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
	/* Local hold state — baresip emits no bevent for our own call_hold(),
	 * so baresdk_call_is_held() reads this flag instead of `state`. */
	bool                       local_hold;
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
	/* ── Incoming REFER (transfer.c; re_main thread only) ───────────── */
	/* Refer-To of a transfer request the app has been told about and has
	 * not answered yet.  Held here because the decision is asynchronous:
	 * BARESDK_EV_TRANSFER_REQUEST is delivered on the event thread and
	 * baresdk_call_transfer_accept() may arrive many seconds later, long
	 * after the bevent that carried the URI has gone. */
	char                       xfer_refer_to[256];
	bool                       xfer_pending;
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
	/* ── Network handover state (netmon.c; re_main thread only) ─────── */
	uint8_t                    net_mig_state;  /* enum bsdk_mig_state */
	/* uint32_t, not uint8_t: cfg.net_max_attempts is a public uint32_t, and
	 * a narrower counter can never reach a value above 255 — the >= test
	 * would never fire and verify_handler() would re-offer for ever. */
	uint32_t                   net_mig_tries;  /* attempts this handover */
	uint32_t                   net_mig_gen;    /* handover generation */
	uint32_t                   net_rx_at_mig;  /* rx packets when re-INVITE sent */
	uint64_t                   net_mig_start;  /* tmr_jiffies() when migration began */
	struct sa                  net_mig_laddr;  /* target local address */
	uint64_t                   net_mig_due;    /* jiffies before which a SENT
	                                            * offer must not be judged */
	bool                       net_ice_stale_sent; /* ICE_STALE emitted this gen */
	bool                       net_ice_restarted;  /* ICE was restarted this gen */
	bool                       net_mig_path_moved; /* the local address changed
	                                                * (vs. a WS-only refresh) */
	/* ── Degraded-link adaptation (adapt.c; re_main thread only) ────── */
	uint32_t                   adapt_bitrate;     /* applied bps; 0 = negotiated */
	uint32_t                   adapt_clean_ticks; /* consecutive low-loss ticks */
	uint32_t                   stall_rx_packets;  /* rx count at last advance */
	uint64_t                   stall_since;       /* tmr_jiffies() of last advance */
	bool                       stall_active;      /* MEDIA_STALL alert outstanding */
	/* Call-setup watchdog (call.c).  tmr_jiffies() when the INVITE went
	 * out, 0 once the call has left CALLING.  A timestamp rather than a
	 * per-call struct tmr: the sweep timer is global, so there is no timer
	 * to cancel from the destructor (which does not run on re_main). */
	uint64_t                   setup_start;
};

/* Per-call migration state machine (struct baresdk_call.net_mig_state). */
enum bsdk_mig_state {
	BSDK_MIG_IDLE = 0,   /* nothing to do                                  */
	BSDK_MIG_WAIT_ADDR,  /* new source address not discoverable yet        */
	BSDK_MIG_DEFERRED,   /* re-INVITE not allowed yet (early/pending xact) */
	BSDK_MIG_SENT,       /* re-INVITE sent, waiting for RTP on new path    */
	BSDK_MIG_DONE,       /* RTP confirmed on the new path                  */
	BSDK_MIG_FAILED,     /* gave up                                        */
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
/* Allocate a zeroed queued event with the destructor that releases
 * deref_after_deliver.  Always use this rather than mem_alloc/mem_zalloc
 * directly, or the call-wrapper reference leaks on the drop path. */
struct baresdk_queued_event *bsdk_qev_alloc(void);

bool bsdk_event_post_qev(struct baresdk_queued_event *qev);

/* ── log.c ─────────────────────────────────────────────────────────────── */

int  bsdk_log_init(void);
void bsdk_log_close(void);

/* ── re_loop.c ─────────────────────────────────────────────────────────── */

int  bsdk_re_loop_start(void);
void bsdk_re_loop_stop(void);

/* ── modules_init.c ────────────────────────────────────────────────────── */

int modules_init(void);

/* True when the platform audio driver that modules_init() settled on captures
 * through the OS voice path, so the device has already cancelled the echo
 * before we see a sample (Android sles_vc, iOS audiounit).  Valid only after
 * modules_init(); false everywhere else, including the stock opensles
 * fallback and every desktop driver. */
bool bsdk_platform_has_aec(void);

/* Name of the platform device module modules_init() selected ("sles_vc",
 * "audiounit", "pulse", ...), or NULL when this build has none.  Used to
 * restore the SDK-owned device after baresdk_audio_use_external(false). */
const char *bsdk_platform_audio_mod(void);

/* ── audio_external.c ──────────────────────────────────────────────────── */

int  bsdk_audio_external_init(void);
void bsdk_audio_external_close(void);

/* True while the app-owned device is the one selected, i.e. between
 * baresdk_audio_use_external(true) and (false).  Reads conf_config(), so it
 * must be called on the re thread.  Defined in audio.c next to the switch. */
bool bsdk_audio_external_selected(void);

#ifdef __ANDROID__
/* platform/android/sles_vc.c — OpenSLES driver with the Android
 * voice-communication recording preset (platform AEC/NS) and voice
 * playback stream.  Registered as ausrc/auplay "sles_vc". */
int  bsdk_sles_vc_init(void);
void bsdk_sles_vc_close(void);
#endif

/* ── platform/<os>/audio_*.c ───────────────────────────────────────────── */

/* One-time platform audio setup, called from baresdk_init() after the
 * modules are loaded.  iOS: configures the AVAudioSession
 * (PlayAndRecord + VoiceChat) — required before any VoIP audio works.
 * Every other platform provides a no-op stub.
 *
 * activate: cfg.platform_audio_activate — when false, configure the session
 * but leave activation to the app (CallKit's didActivateAudioSession). */
int bsdk_platform_audio_init(bool activate);

/* ── account.c ─────────────────────────────────────────────────────────── */

struct baresdk_account *bsdk_account_find_by_ua(const struct ua *ua);
void bsdk_account_schedule_retry(struct baresdk_account *acct);
void bsdk_account_watch_registration(struct baresdk_account *acct);

/* Which state a registration failure should be reported as: RECONNECTING when
 * the SDK will keep trying by itself, FAILED when it is done.  Updates
 * acct->reconnecting to match, so call it once per failure, before posting the
 * event.  re_main thread only. */
baresdk_reg_state_t bsdk_account_reg_fail_state(struct baresdk_account *acct,
                                                baresdk_error_t err);

/* Report that a registration is recovering (network handover, link loss):
 * moves a REGISTERED/REGISTERING account to RECONNECTING and posts the event.
 * A no-op for an account that is already there, or that the app never asked to
 * register.  re_main thread only. */
void bsdk_account_reg_reconnecting(struct baresdk_account *acct);

/* Keepalive / reachability probe — cfg.keepalive_interval.  _arm() on a
 * successful registration, _cancel() when the account stops being
 * registered. */
void bsdk_account_keepalive_arm(struct baresdk_account *acct);
void bsdk_account_keepalive_cancel(struct baresdk_account *acct);
/* Kick off the one-shot SRV lookup that feeds failover, if eligible. */
void bsdk_account_srv_resolve(struct baresdk_account *acct);

/* ── call.c ────────────────────────────────────────────────────────────── */

void bsdk_call_destructor(void *data);  /* mem_alloc destructor for call wrappers */
/* ── record.c ──────────────────────────────────────────────────────────── */

void bsdk_record_write_frame(struct baresdk_call *lc, baresdk_media_dir_t dir,
                              const int16_t *pcm, size_t samples,
                              uint32_t srate, uint8_t ch);
struct baresdk_call *bsdk_call_find(const struct call *bc);
/* Allocate, initialise and register a call wrapper.  Used by the outgoing,
 * incoming and transfer-accept paths — see the doc comment in call.c. */
struct baresdk_call *bsdk_call_wrap_new(struct call *bc,
                                        struct baresdk_account *acct,
                                        baresdk_call_state_t state,
                                        bool inherit_hdrs);

void bsdk_call_register(struct baresdk_call *lc);
void bsdk_call_unregister(struct baresdk_call *lc);
void bsdk_call_foreach(void (*fn)(struct baresdk_call *, void *), void *arg);

/* Call-setup watchdog — cfg.sip_timer_b_ms.  _start() on a fresh outgoing
 * call, _cancel() once it is answered or gone; _close() at shutdown. */
void bsdk_call_setup_watch_start(struct baresdk_call *lc);
void bsdk_call_setup_watch_cancel(struct baresdk_call *lc);
void bsdk_call_setup_watch_close(void);

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

/* Whether the half-duplex TX suppressor should run: SUPPRESSOR mode AND no
 * platform echo canceller underneath us (see bsdk_platform_has_aec). */
bool bsdk_aec_suppressor_wanted(baresdk_aec_mode_t mode);

/* Atomic gain accessors used by the setters in audio.c. */
void bsdk_mic_gain_store(float linear);
void bsdk_spk_gain_store(float linear);
void bsdk_aec_floor_store(float floor);

/* ── stats.c ───────────────────────────────────────────────────────────── */

int  bsdk_stats_init(void);
void bsdk_stats_close(void);
void bsdk_stats_collect_final(struct baresdk_call *lc);
void bsdk_post_quality_alert(struct baresdk_call *lc,
                             baresdk_quality_issue_t issue,
                             float value, float threshold, bool recovering);

/* ── Degraded-link adaptation (adapt.c) ────────────────────────────────── */

/* Reset per-call adaptation state; call when the call reaches established. */
void bsdk_adapt_call_start(struct baresdk_call *lc);
/* Evaluate stall detection and bitrate adaptation for one stats sample. */
void bsdk_adapt_tick(struct baresdk_call *lc,
                     const baresdk_ev_media_stats_t *s);
/* Re-run the encoder update with `bitrate` (bps; 0 = negotiated rate).
 * Returns 0, or ENOTSUP for a codec with no encoder-update handler. */
int  bsdk_adapt_apply_bitrate(struct baresdk_call *lc, uint32_t bitrate);
/* Rewrite an SDP fmtp string with a new maxaveragebitrate (0 = strip only).
 * Internal to adapt.c; exposed for test/unit/test_fmtp_bitrate.c.
 * Returns 0, or EINVAL when the result would not fit in `sz`. */
int  bsdk_adapt_fmtp_set_bitrate(char *buf, size_t sz, const char *src,
                                 uint32_t bitrate);

/* ── netmon.c ──────────────────────────────────────────────────────────── */

int  bsdk_netmon_init(void);
void bsdk_netmon_stop(void);   /* app thread: stop acting, before teardown */
void bsdk_netmon_close(void);  /* re loop stopped: cancel timers */
/* Called from event.c when a call reaches a state where a re-INVITE becomes
 * legal again, so a deferred migration can proceed without waiting for the
 * next retry tick. */
void bsdk_netmon_call_refreshable(struct baresdk_call *lc);
/* Called from event.c when the peer answers our SDP offer, so a migration
 * in flight can report "accepted — waiting for audio". */
void bsdk_netmon_call_sdp_answer(struct baresdk_call *lc);

/* ── dns.c ─────────────────────────────────────────────────────────────── */

/* Opaque result types — defined in dns.c, used only within the module */
struct bsdk_dns_target;
struct bsdk_dns_result;

typedef void (bsdk_dns_done_h)(const struct bsdk_dns_result *res, void *arg);

int  bsdk_dns_init(void);
void bsdk_dns_close(void);

/* ── ICE gathering deadline (ice_shim.c) ─────────────────────────────────── */

/* Interposes cfg.ice_gathering_timeout_ms on the "ice" media-NAT so a stalled
 * gather can never hold an outgoing INVITE forever.  _init() must run after
 * modules_init(), which registers the struct it mutates; ENOENT means the ice
 * module is not in this build and nothing was changed. */
int  bsdk_ice_shim_init(void);
void bsdk_ice_shim_close(void);

/* Restart ICE for one call (RFC 8445 §9) and re-offer on `laddr`: new
 * ice-ufrag/ice-pwd, a fresh gather on the interface that now carries the
 * default route, and a re-INVITE driven from the estab handler once it reports
 * or the gathering deadline expires.  This is how a handover moves an ICE call
 * — see the ICE-restart section in ice_shim.c for why call_reset_transp() alone
 * cannot.  `call` is a `struct call *`.
 *
 * Returns 0 when a restart is under way, ENOENT when the call has no ICE
 * media-NAT (the plain re-offer is correct then), EALREADY when one is already
 * gathering, or an errorcode when nothing was changed. */
int  bsdk_ice_restart(void *call, const struct sa *laddr);

/* True when `call` (a `struct call *`) has a live ICE media-NAT with at least
 * one stream — the negotiated truth, as opposed to cfg.ice_enabled. */
bool bsdk_ice_call_active(void *call);

/* Async RFC 3263 resolution. done_h fires on re_main thread.
 * port_hint > 0 skips NAPTR/SRV (explicit port in URI). */
int  bsdk_dns_resolve(const char *domain,
                      baresdk_transport_t transport_hint,
                      uint16_t port_hint,
                      bsdk_dns_done_h *done_h, void *arg);

/* Result accessors — targets are ordered lowest-priority-value first, then
 * highest weight, i.e. the order a client must try them in (RFC 2782 §3). */
size_t bsdk_dns_result_count(const struct bsdk_dns_result *res);
int    bsdk_dns_result_err(const struct bsdk_dns_result *res);
int    bsdk_dns_result_get(const struct bsdk_dns_result *res, size_t idx,
                           baresdk_transport_t *transport,
                           char *host, size_t host_sz, uint16_t *port);

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

/* Record/release the WebSocket server to pin outbound connections to.  Call
 * once per WS/WSS account on create, and the matching unset on destroy — the
 * servers are refcounted, and pinning is only active while all live accounts
 * agree on one server. */
void bsdk_ws_set_server(baresdk_transport_t tp, const char *host,
                         uint16_t port, const char *path);
void bsdk_ws_unset_server(baresdk_transport_t tp, const char *host,
                           uint16_t port, const char *path);

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
