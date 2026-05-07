/**
 * @file libbare_internal.h  Internal types — never exposed to consumers.
 */

#ifndef LIBBARE_INTERNAL_H
#define LIBBARE_INTERNAL_H

#include <re.h>
#include <baresip.h>
#include "../include/libbare.h"

/* ── Forward declarations ──────────────────────────────────────────────── */

struct libbare_account;
struct libbare_call;
struct libbare_queued_event;

/* ── Global singleton ──────────────────────────────────────────────────── */

struct bare_ctx {
	mtx_t              lock;        /* guards initialized + shutdown */
	bool               initialized;

	libbare_config_t   cfg;         /* deep-copied from caller */

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

	/* re_main thread */
	thrd_t             re_thread;
	bool               re_thread_running;

	/* Event dispatch thread (separate from re_main to prevent consumer
	 * deadlocks when they call back into libbare from inside an event) */
	thrd_t             ev_thread;
	mtx_t              ev_lock;
	cnd_t              ev_cond;
	struct list        ev_queue;    /* struct libbare_queued_event */
	bool               ev_shutdown;
	size_t             ev_queue_len;
	size_t             ev_queue_max;

	/* Account list */
	struct list        accounts;    /* struct libbare_account */
	mtx_t              acct_lock;

	/* pcap */
	FILE              *pcap_file;
	mtx_t              pcap_lock;

	/* Stats polling timer (fires on re_main) */
	struct tmr         stats_tmr;
};

extern struct bare_ctx g_bare;

/* ── Account ───────────────────────────────────────────────────────────── */

struct libbare_account {
	struct le                 le;
	struct ua                *ua;
	libbare_account_config_t  cfg;
	char                     *cfg_aor;
	char                     *cfg_auth_user;
	char                     *cfg_auth_pass;
	char                     *cfg_display_name;
	char                     *cfg_outbound;
	libbare_reg_state_t       reg_state;
	libbare_error_t           reg_error;
	char                      reg_error_str[256];
	uint32_t                  retry_attempt;
	struct tmr                retry_tmr;
	bool                      destroyed;
	struct list               custom_hdrs;
};

/* ── Call ──────────────────────────────────────────────────────────────── */

struct libbare_call {
	struct le                  le;
	struct call               *bc;          /* baresip call (weak ref) */
	struct libbare_account    *acct;
	libbare_call_state_t       state;
	/* SDP capture for LIBBARE_EV_SDP_NEGOTIATION */
	char                       local_sdp[4096];
	char                       remote_sdp[4096];
	bool                       local_sdp_set;
	bool                       remote_sdp_set;
	/* Media tap */
	libbare_media_tap_cb_t     tap_cb;
	void                      *tap_userdata;
	mtx_t                      tap_lock;
	/* Per-dialog custom headers (linked list of bare_custom_hdr) */
	struct list                custom_hdrs;
};

/* ── Event queue entry ─────────────────────────────────────────────────── */

struct libbare_queued_event {
	struct le         le;
	libbare_event_t   ev;
	/* Inline string storage — pointers in ev.u may point into buf. */
	char              buf[4096];
};

/* ── dispatch.c ────────────────────────────────────────────────────────── */

typedef void (*bare_main_fn)(void *arg);

/* Post fn(arg) to run on the re_main thread. Returns immediately. */
int bare_dispatch(bare_main_fn fn, void *arg);

/* Post fn(arg) to re_main and block until it completes. */
int bare_dispatch_sync(bare_main_fn fn, void *arg);

/* ── event.c ───────────────────────────────────────────────────────────── */

int  bare_event_init(void);
void bare_event_close(void);
void bare_event_post(const libbare_event_t *ev);

/* ── log.c ─────────────────────────────────────────────────────────────── */

int  bare_log_init(void);
void bare_log_close(void);

/* ── re_loop.c ─────────────────────────────────────────────────────────── */

int  bare_re_loop_start(void);
void bare_re_loop_stop(void);

/* ── modules_init.c ────────────────────────────────────────────────────── */

int modules_init(void);

/* ── account.c ─────────────────────────────────────────────────────────── */

struct libbare_account *bare_account_find_by_ua(const struct ua *ua);
void bare_account_schedule_retry(struct libbare_account *acct);

/* ── call.c ────────────────────────────────────────────────────────────── */

struct libbare_call *bare_call_find(const struct call *bc);
void bare_call_register(struct libbare_call *lc);
void bare_call_unregister(struct libbare_call *lc);
void bare_call_foreach(void (*fn)(struct libbare_call *, void *), void *arg);

/* ── timers.c ──────────────────────────────────────────────────────────── */

void bare_timers_configure(const libbare_config_t *cfg);

/* ── trace.c ───────────────────────────────────────────────────────────── */

int  bare_trace_init(void);
void bare_trace_close(void);

/* ── sdp.c ─────────────────────────────────────────────────────────────── */

void bare_sdp_handle_event(enum bevent_ev ev, struct bevent *event);

/* ── transfer.c ────────────────────────────────────────────────────────── */

void bare_transfer_handle_event(struct bevent *event);

/* ── message.c ─────────────────────────────────────────────────────────── */

int  bare_message_init(void);
void bare_message_close(void);

/* ── presence.c ────────────────────────────────────────────────────────── */

int  bare_presence_init(void);
void bare_presence_close(void);
void bare_presence_handle_mwi(struct bevent *event);

/* ── pcap.c ────────────────────────────────────────────────────────────── */

int  bare_pcap_open(const char *path);
void bare_pcap_close(void);
void bare_pcap_write_sip(const char *data, size_t len,
                          const struct sa *src, const struct sa *dst,
                          bool is_udp);

/* ── stats.c ───────────────────────────────────────────────────────────── */

int  bare_stats_init(void);
void bare_stats_close(void);

/* ── dns.c ─────────────────────────────────────────────────────────────── */

/* Opaque result types — defined in dns.c, used only within the module */
struct bare_dns_target;
struct bare_dns_result;

typedef void (bare_dns_done_h)(const struct bare_dns_result *res, void *arg);

int  bare_dns_init(void);
void bare_dns_close(void);

/* Async RFC 3263 resolution. done_h fires on re_main thread.
 * port_hint > 0 skips NAPTR/SRV (explicit port in URI). */
int  bare_dns_resolve(const char *domain,
                      libbare_transport_t transport_hint,
                      uint16_t port_hint,
                      bare_dns_done_h *done_h, void *arg);

/* ── transport.c ───────────────────────────────────────────────────────── */

int bare_parse_server_url(const char *url,
                           libbare_transport_t *out_transport,
                           char *host, size_t host_sz,
                           uint16_t *port,
                           char *path, size_t path_sz);

int bare_build_outbound(const libbare_config_t *cfg, char *buf, size_t buf_sz);

const char *bare_transport_str(libbare_transport_t t);
const char *bare_mediaenc_str(libbare_media_enc_t enc);

/* ── Utility macros ────────────────────────────────────────────────────── */

/* Safe string copy into a fixed buffer inside a struct. */
#define BARE_STRCPY(dst, src) \
	do { if (src) str_ncpy((dst), (src), sizeof(dst)); \
	     else (dst)[0] = '\0'; } while (0)

/* ── Per-dialog custom header storage ──────────────────────────────────── */

struct bare_custom_hdr {
	struct le  le;
	char      *name;
	char      *value;
};

/* ── Deep-copy helpers ─────────────────────────────────────────────────── */

char *bare_strdup(const char *s);
void bare_cfg_deep_copy(libbare_config_t *dst, const libbare_config_t *src,
                         struct bare_ctx *ctx);
void bare_cfg_deep_free(struct bare_ctx *ctx);
void bare_acct_cfg_deep_copy(libbare_account_config_t *dst,
                              const libbare_account_config_t *src,
                              struct libbare_account *acct);
void bare_acct_cfg_deep_free(struct libbare_account *acct);

/* ── Global state reset (call from shutdown) ───────────────────────────── */

void bare_call_global_reset(void);
void bare_tap_global_reset(void);

#endif /* LIBBARE_INTERNAL_H */
