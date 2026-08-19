/**
 * @file call.c  INVITE FSM, hold/resume, DTMF, blind transfer, per-dialog headers
 *
 * baresdk_call_t wraps a baresip struct call. Call objects are created either
 * by baresdk_call_invite() (outgoing) or by event.c when BEVENT_CALL_INCOMING
 * fires. The call list is guarded by s_calls_lock.
 */

#include "baresdk_internal.h"
#include <math.h>

static struct list s_calls;
static mtx_t       s_calls_lock;
/* Call-setup watchdog sweep timer — see the Timer B section below. */
static struct tmr  s_setup_tmr;

/* Called once from baresdk_init before any call API is reachable. */
void bsdk_call_global_init(void)
{
	mtx_init(&s_calls_lock, mtx_plain);
	list_init(&s_calls);
	tmr_init(&s_setup_tmr);
}

void bsdk_call_global_reset(void)
{
	mtx_lock(&s_calls_lock);
	struct le *le, *le_tmp;
	LIST_FOREACH_SAFE(&s_calls, le, le_tmp) {
		struct baresdk_call *lc = le->data;
		list_unlink(&lc->le);
		mem_deref(lc);
	}
	mtx_unlock(&s_calls_lock);
	mtx_destroy(&s_calls_lock);
}

/* ── Call lookup ─────────────────────────────────────────────────────────── */

struct baresdk_call *bsdk_call_find(const struct call *bc)
{
	struct le *le;
	mtx_lock(&s_calls_lock);
	LIST_FOREACH(&s_calls, le) {
		struct baresdk_call *lc = le->data;
		if (lc->bc == bc) {
			mtx_unlock(&s_calls_lock);
			return lc;
		}
	}
	mtx_unlock(&s_calls_lock);
	return NULL;
}

void bsdk_call_register(struct baresdk_call *lc)
{
	mtx_lock(&s_calls_lock);
	list_append(&s_calls, &lc->le, lc);
	mtx_unlock(&s_calls_lock);
}

void bsdk_call_unregister(struct baresdk_call *lc)
{
	mtx_lock(&s_calls_lock);
	list_unlink(&lc->le);
	mtx_unlock(&s_calls_lock);
}

void bsdk_call_foreach(void (*fn)(struct baresdk_call *, void *), void *arg)
{
	mtx_lock(&s_calls_lock);
	struct le *le;
	LIST_FOREACH(&s_calls, le) {
		struct baresdk_call *lc = le->data;
		fn(lc, arg);
	}
	mtx_unlock(&s_calls_lock);
}

/* ── Call-setup watchdog (RFC 3261 Timer B, SDK-side) ────────────────────────
 *
 * An INVITE sent onto a link that is up but not carrying traffic gets no
 * response of any kind.  libre's transaction layer bounds that at Timer B =
 * 64·T1 = 32 s, and T1 is a compile-time constant (SIP_T1) with no runtime
 * knob — so a mobile app that would rather fail in eight seconds and offer to
 * retry has nowhere to say so.  cfg.sip_timer_b_ms is that place, enforced
 * here.
 *
 * One global sweep timer rather than a struct tmr per call: a per-call timer
 * would have to be cancelled from bsdk_call_destructor(), which runs on
 * whichever thread drops the last reference, and cancelling a timer off
 * re_main is not safe.  Each call carries only a start timestamp.
 *
 * Only BARESDK_CALL_CALLING is watched.  A call that reached RINGING has had a
 * provisional response, which proves the path works; how long to let it ring
 * is the app's decision, not a transport timeout.
 */

#define BSDK_SETUP_TICK_MS  500
#define BSDK_SETUP_MAX_SNAP 64

struct setup_snap {
	struct baresdk_call **v;
	size_t                max;
	size_t                n;
	bool                  more;   /* a call still inside the deadline */
	uint64_t              now;
};

static void setup_visit(struct baresdk_call *lc, void *arg)
{
	struct setup_snap *sn = arg;
	uint32_t limit = g_bsdk.cfg.sip_timer_b_ms;

	if (!lc->setup_start)
		return;

	if (lc->state != BARESDK_CALL_CALLING) {
		lc->setup_start = 0;   /* answered, ringing or gone — done */
		return;
	}

	if (!limit) {
		lc->setup_start = 0;   /* watchdog disabled mid-flight */
		return;
	}

	if ((uint64_t)(sn->now - lc->setup_start) < (uint64_t)limit) {
		sn->more = true;
		return;
	}

	if (sn->n < sn->max)
		sn->v[sn->n++] = lc;
}

static void setup_watch_handler(void *arg)
{
	struct baresdk_call *snap[BSDK_SETUP_MAX_SNAP];
	struct setup_snap sn = { .v = snap, .max = RE_ARRAY_SIZE(snap),
	                         .n = 0, .more = false, .now = tmr_jiffies() };
	(void)arg;

	/* Collect under the list lock, act outside it: ua_hangup() re-enters
	 * baresip's event handler, which calls back into bsdk_call_unregister()
	 * and takes the same lock. */
	bsdk_call_foreach(setup_visit, &sn);

	for (size_t i = 0; i < sn.n; i++) {
		struct baresdk_call *lc = snap[i];

		if (!lc->bc || !lc->acct || !lc->acct->ua) {
			lc->setup_start = 0;
			continue;
		}

		info("baresdk: call setup timed out after %u ms; cancelling\n",
		     g_bsdk.cfg.sip_timer_b_ms);

		lc->setup_start = 0;
		/* 408 rather than a local teardown: it reaches the app as
		 * BARESDK_CALL_FAILED / BARESDK_ERR_TIMEOUT through the same
		 * classification every other failure uses, and sends CANCEL so a
		 * proxy that did receive the INVITE stops forking it. */
		ua_hangup(lc->acct->ua, lc->bc, 408, "Request Timeout");
	}

	if (sn.more)
		tmr_start(&s_setup_tmr, BSDK_SETUP_TICK_MS,
		          setup_watch_handler, NULL);
}

void bsdk_call_setup_watch_start(struct baresdk_call *lc)
{
	if (!lc || !g_bsdk.cfg.sip_timer_b_ms)
		return;

	lc->setup_start = tmr_jiffies();

	/* tmr_start on an already-running timer re-arms it, which would push
	 * the deadline of a call that is already being watched out by a tick
	 * on every new dial. */
	if (!tmr_isrunning(&s_setup_tmr))
		tmr_start(&s_setup_tmr, BSDK_SETUP_TICK_MS,
		          setup_watch_handler, NULL);
}

void bsdk_call_setup_watch_cancel(struct baresdk_call *lc)
{
	if (lc)
		lc->setup_start = 0;
}

void bsdk_call_setup_watch_close(void)
{
	tmr_cancel(&s_setup_tmr);
}

/* ── call_destructor ─────────────────────────────────────────────────────── */

static void custom_hdr_destructor(void *data)
{
	struct bsdk_custom_hdr *hdr = data;
	mem_deref(hdr->name);
	mem_deref(hdr->value);
}

void bsdk_call_destructor(void *data)
{
	struct baresdk_call *lc = data;
	struct le *le, *le_tmp;
	LIST_FOREACH_SAFE(&lc->custom_hdrs, le, le_tmp) {
		struct bsdk_custom_hdr *hdr = le->data;
		list_unlink(&hdr->le);
		mem_deref(hdr);
	}
	mtx_destroy(&lc->tap_lock);
	/* Close any open recording files (if record_stop was never called) */
	mtx_lock(&lc->rec_lock);
	lc->rec_active = false;
	if (lc->rec_file) { fclose(lc->rec_file); lc->rec_file = NULL; }
	mtx_unlock(&lc->rec_lock);
	mtx_destroy(&lc->rec_lock);
}

/* ── baresdk_call_invite ─────────────────────────────────────────────────── */

typedef struct {
	struct baresdk_account   *acct;
	char                      uri[512];
	baresdk_call_handle_t    *out;
	int                       result;
} invite_ctx_t;

static void invite_fn(void *arg)
{
	invite_ctx_t *ctx = arg;
	struct call *bc = NULL;
	char *ruri = NULL;
	const char *dial = ctx->uri;
	struct pl pl;

	/* Refuse to dial when the transport never connected — ua_connect would
	 * crash inside baresip trying to use a dead socket. */
	if (ctx->acct->reg_state == BARESDK_REG_FAILED) {
		ctx->result = ENOTCONN;
		return;
	}

	/* Complete the dial string into a full SIP URI.
	 *
	 * ua_connect() sends the request URI verbatim — it only appends the
	 * account's URI parameters — so a bare extension has to be turned into
	 * user@domain here. Sending "INVITE sip:*43 SIP/2.0" (no host) is not a
	 * valid request line: a strict registrar does not answer 4xx, it drops
	 * the whole packet at the parser ("PJSIP syntax error ... 'Request
	 * Line' ... col 12" on Asterisk), so the call just hangs with no
	 * response and no media.
	 *
	 * account_uri_complete_strdup() adds the scheme, leaves IP literals and
	 * anything already carrying user@host alone, and appends the account
	 * domain (and port, when not 5060) otherwise. It skips completion
	 * entirely for a string that already has a scheme, so drop a leading
	 * "sip:" when there is no host yet — "sips:" is left alone rather than
	 * silently downgraded to "sip:". */
	if (!strchr(dial, '@') && strncasecmp(dial, "sip:", 4) == 0)
		dial += 4;

	pl_set_str(&pl, dial);
	ctx->result = account_uri_complete_strdup(ua_account(ctx->acct->ua),
	                                          &ruri, &pl);
	if (ctx->result)
		return;

	ctx->result = ua_connect(ctx->acct->ua, &bc, NULL,
	                         ruri, VIDMODE_OFF);
	if (ctx->result == EAFNOSUPPORT) {
		/* ICE STUN/TURN lookup failed because this host has no IPv6.
		 * Disable ICE and retry with direct RTP.
		 *
		 * Put it back afterwards.  The account outlives the call, and
		 * leaving the media-NAT cleared meant one dial on an IPv4-only
		 * network silently downgraded every later call on that account
		 * to no-ICE — including calls placed after the device moved to a
		 * network where ICE was both available and needed. */
		struct account *ba = ua_account(ctx->acct->ua);
		const char *mnatid = account_medianat(ba);
		char keep[32] = "";

		if (mnatid)
			str_ncpy(keep, mnatid, sizeof(keep));

		account_set_medianat(ba, NULL);
		ctx->result = ua_connect(ctx->acct->ua, &bc, NULL,
		                         ruri, VIDMODE_OFF);
		if (keep[0])
			account_set_medianat(ba, keep);
	}
	mem_deref(ruri);
	if (ctx->result)
		return;
	if (!bc) {
		/* ua_connect reported success without producing a call. Nothing
		 * downstream can work with that, and returning 0 would hand the
		 * caller an untouched *out — a stale or uninitialised handle for
		 * every binding that does not pre-zero it. */
		ctx->result = EINVAL;
		return;
	}

	struct baresdk_call *lc = mem_alloc(sizeof(*lc), bsdk_call_destructor);
	if (!lc) {
		ua_hangup(ctx->acct->ua, bc, 500, "Out of Memory");
		ctx->result = ENOMEM;
		return;
	}
	memset(lc, 0, sizeof(*lc));
	mtx_init(&lc->tap_lock, mtx_plain);
	mtx_init(&lc->rec_lock, mtx_plain);
	list_init(&lc->custom_hdrs);
	lc->bc    = bc;
	lc->acct  = ctx->acct;
	lc->state = BARESDK_CALL_CALLING;
	/* The SDP offer was created inside ua_connect(), before this wrapper
	 * existed — the BEVENT_CALL_LOCAL_SDP that fired then could not be
	 * matched by bsdk_sdp_handle_event().  Record it here so the
	 * SDP_NEGOTIATION event fires when the remote answer arrives. */
	lc->local_sdp_set = true;
	/* NaN, not -127: the header documents NaN for "unavailable" and -127 for
	 * "silent", and tap_compute_dbov() genuinely returns -127 for all-zero
	 * PCM.  Seeding -127 collapsed the two, so a microphone delivering
	 * digital silence and a level that was never measured at all read back
	 * identically — which is precisely the distinction you need when audio
	 * is missing and you are trying to work out whether the capture path is
	 * dead or merely quiet. */
	uint32_t _sil_bits; float _sil = NAN; memcpy(&_sil_bits, &_sil, 4);
	re_atomic_rlx_set(&lc->rx_level_bits, _sil_bits);
	re_atomic_rlx_set(&lc->tx_level_bits, _sil_bits);

	struct le *le;
	LIST_FOREACH(&ctx->acct->custom_hdrs, le) {
		struct bsdk_custom_hdr *acct_hdr = le->data;
		struct bsdk_custom_hdr *ch = mem_alloc(sizeof(*ch),
		                                       custom_hdr_destructor);
		if (!ch) continue;
		ch->name  = bsdk_strdup(acct_hdr->name);
		ch->value = bsdk_strdup(acct_hdr->value);
		if (!ch->name || !ch->value) {
			mem_deref(ch);
			continue;
		}
		list_append(&lc->custom_hdrs, &ch->le, ch);
	}

	bsdk_call_setup_watch_start(lc);

	bsdk_call_register(lc);
	*ctx->out = lc;
}

int baresdk_call_invite(baresdk_account_handle_t acct,
                         const char *uri,
                         baresdk_call_handle_t *out)
{
	if (!acct || !uri || !out)
		return BARESDK_ERR_INVAL;

	/* Store the dial string as given. Completing it needs the account's
	 * domain, which may only be read on re_main — invite_fn does it. */
	invite_ctx_t ctx = {.acct = acct, .out = out, .result = 0};
	str_ncpy(ctx.uri, uri, sizeof(ctx.uri));

	int err = bsdk_dispatch_sync(invite_fn, &ctx);
	return err ? err : ctx.result;
}

/* ── baresdk_call_answer ─────────────────────────────────────────────────── */

typedef struct { struct baresdk_call *lc; int result; } simple_call_ctx_t;

static void answer_fn(void *arg)
{
	simple_call_ctx_t *ctx = arg;
	struct baresdk_call *lc = ctx->lc;
	if (!lc->bc) { ctx->result = ENOENT; return; }
	ctx->result = ua_answer(lc->acct->ua, lc->bc, VIDMODE_OFF);
}

int baresdk_call_answer(baresdk_call_handle_t call)
{
	if (!call) return BARESDK_ERR_INVAL;
	simple_call_ctx_t ctx = {.lc = call, .result = 0};
	int err = bsdk_dispatch_sync(answer_fn, &ctx);
	return err ? err : ctx.result;
}

/* ── baresdk_call_hangup ─────────────────────────────────────────────────── */

static void hangup_fn(void *arg)
{
	struct baresdk_call *lc = arg;
	if (!lc->bc) return;
	ua_hangup(lc->acct->ua, lc->bc, 0, NULL);
	lc->state = BARESDK_CALL_ENDED;
}

int baresdk_call_hangup(baresdk_call_handle_t call)
{
	if (!call) return BARESDK_ERR_INVAL;
	return bsdk_dispatch(hangup_fn, call);
}

/* ── baresdk_call_reject ─────────────────────────────────────────────────── */

typedef struct {
	struct baresdk_call *lc;
	uint16_t             scode;
	char                 reason[128];
} reject_ctx_t;

static void reject_fn(void *arg)
{
	reject_ctx_t *ctx = arg;
	struct baresdk_call *lc = ctx->lc;
	if (lc->bc)
		ua_hangup(lc->acct->ua, lc->bc, ctx->scode,
		          ctx->reason[0] ? ctx->reason : NULL);
	lc->state = BARESDK_CALL_ENDED;
	mem_deref(ctx);
}

int baresdk_call_reject(baresdk_call_handle_t call,
                         uint16_t scode, const char *reason)
{
	if (!call) return BARESDK_ERR_INVAL;

	reject_ctx_t *ctx = mem_alloc(sizeof(*ctx), NULL);
	if (!ctx) return BARESDK_ERR_NOMEM;
	memset(ctx, 0, sizeof(*ctx));
	ctx->lc    = call;
	ctx->scode = scode;
	if (reason)
		str_ncpy(ctx->reason, reason, sizeof(ctx->reason));

	int err = bsdk_dispatch(reject_fn, ctx);
	if (err)
		mem_deref(ctx);
	return err;
}

/* ── baresdk_call_hold ───────────────────────────────────────────────────── */

typedef struct { struct baresdk_call *lc; bool hold; int result; } hold_ctx_t;

static void hold_fn(void *arg)
{
	hold_ctx_t *ctx = arg;
	if (!ctx->lc->bc) { ctx->result = ENOENT; return; }
	ctx->result = call_hold(ctx->lc->bc, ctx->hold);
	if (!ctx->result)
		ctx->lc->local_hold = ctx->hold;
}

int baresdk_call_hold(baresdk_call_handle_t call)
{
	if (!call) return BARESDK_ERR_INVAL;
	hold_ctx_t ctx = {.lc = call, .hold = true, .result = 0};
	int err = bsdk_dispatch_sync(hold_fn, &ctx);
	return err ? err : ctx.result;
}

int baresdk_call_resume(baresdk_call_handle_t call)
{
	if (!call) return BARESDK_ERR_INVAL;
	hold_ctx_t ctx = {.lc = call, .hold = false, .result = 0};
	int err = bsdk_dispatch_sync(hold_fn, &ctx);
	return err ? err : ctx.result;
}

/* ── baresdk_call_is_held ────────────────────────────────────────────────── */

bool baresdk_call_is_held(baresdk_call_handle_t call)
{
	if (!call) return false;
	struct baresdk_call *lc = call;
	/* local_hold: our own call_hold(); state HELD: peer put us on hold. */
	return lc->local_hold || lc->state == BARESDK_CALL_HELD;
}

/* ── baresdk_call_send_dtmf ──────────────────────────────────────────────── */

typedef struct { struct baresdk_call *lc; char digit; int result; } dtmf_ctx_t;

static void dtmf_fn(void *arg)
{
	dtmf_ctx_t *ctx = arg;
	if (!ctx->lc->bc) { ctx->result = ENOENT; return; }
	ctx->result = call_send_digit(ctx->lc->bc, ctx->digit);
}

int baresdk_call_send_dtmf(baresdk_call_handle_t call, char digit)
{
	if (!call) return BARESDK_ERR_INVAL;
	dtmf_ctx_t ctx = {.lc = call, .digit = digit, .result = 0};
	int err = bsdk_dispatch_sync(dtmf_fn, &ctx);
	return err ? err : ctx.result;
}

/* ── baresdk_call_transfer ───────────────────────────────────────────────── */

typedef struct { struct baresdk_call *lc; char uri[512]; int result; } xfer_ctx_t;

static void transfer_fn(void *arg)
{
	xfer_ctx_t *ctx = arg;
	if (!ctx->lc->bc) { ctx->result = ENOENT; return; }
	ctx->result = call_transfer(ctx->lc->bc, ctx->uri);
}

int baresdk_call_transfer(baresdk_call_handle_t call, const char *uri)
{
	if (!call || !uri) return BARESDK_ERR_INVAL;
	xfer_ctx_t ctx = {.lc = call, .result = 0};
	str_ncpy(ctx.uri, uri, sizeof(ctx.uri));
	int err = bsdk_dispatch_sync(transfer_fn, &ctx);
	return err ? err : ctx.result;
}

/* ── baresdk_call_foreach ────────────────────────────────────────────────── */

typedef struct {
	baresdk_call_iter_fn fn;
	void                *arg;
} foreach_ctx_t;

static void public_foreach_cb(struct baresdk_call *lc, void *arg)
{
	foreach_ctx_t *ctx = arg;
	ctx->fn((baresdk_call_handle_t)lc, ctx->arg);
}

void baresdk_call_foreach(baresdk_call_iter_fn fn, void *arg)
{
	if (!fn) return;
	foreach_ctx_t ctx = { .fn = fn, .arg = arg };
	bsdk_call_foreach(public_foreach_cb, &ctx);
}

baresdk_account_handle_t baresdk_call_get_account(baresdk_call_handle_t call)
{
	if (!call) return NULL;
	return (baresdk_account_handle_t)call->acct;
}

baresdk_call_state_t baresdk_call_get_state(baresdk_call_handle_t call)
{
	if (!call) return BARESDK_CALL_ENDED;
	/* Local hold is tracked separately (baresdk_call_is_held); a call we
	 * put on hold ourselves still reads as ESTABLISHED here. */
	return call->state;
}

/* ── Per-dialog custom headers ─────────────────────────────────────────── */

typedef struct {
	struct baresdk_call *lc;
	const char          *name;
	const char          *value;
	int                  result;
} call_hdr_ctx_t;

static void add_call_hdr_fn(void *arg)
{
	call_hdr_ctx_t *ctx = arg;
	if (!ctx->lc->bc) { ctx->result = ENOENT; return; }

	struct bsdk_custom_hdr *ch = mem_alloc(sizeof(*ch),
	                                       custom_hdr_destructor);
	if (!ch) { ctx->result = ENOMEM; return; }
	ch->name  = bsdk_strdup(ctx->name);
	ch->value = bsdk_strdup(ctx->value);
	if (!ch->name || !ch->value) {
		mem_deref(ch);
		ctx->result = ENOMEM;
		return;
	}

	list_append(&ctx->lc->custom_hdrs, &ch->le, ch);
}

int baresdk_call_add_header(baresdk_call_handle_t call,
                             const char *name, const char *value)
{
	if (!call || !name || !value) return BARESDK_ERR_INVAL;
	call_hdr_ctx_t ctx = {.lc = call, .name = name,
	                       .value = value, .result = 0};
	int err = bsdk_dispatch_sync(add_call_hdr_fn, &ctx);
	return err ? err : ctx.result;
}
