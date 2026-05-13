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

/* Called once from baresdk_init before any call API is reachable. */
void bsdk_call_global_init(void)
{
	mtx_init(&s_calls_lock, mtx_plain);
	list_init(&s_calls);
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

	/* Refuse to dial when the transport never connected — ua_connect would
	 * crash inside baresip trying to use a dead socket. */
	if (ctx->acct->reg_state == BARESDK_REG_FAILED) {
		ctx->result = ENOTCONN;
		return;
	}

	ctx->result = ua_connect(ctx->acct->ua, &bc, NULL,
	                         ctx->uri, VIDMODE_OFF);
	if (ctx->result == EAFNOSUPPORT) {
		/* ICE STUN/TURN lookup failed because this host has no IPv6.
		 * Disable ICE and retry with direct RTP. */
		account_set_medianat(ua_account(ctx->acct->ua), NULL);
		ctx->result = ua_connect(ctx->acct->ua, &bc, NULL,
		                         ctx->uri, VIDMODE_OFF);
	}
	if (ctx->result || !bc)
		return;

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
	uint32_t _nan_bits; float _nan = NAN; memcpy(&_nan_bits, &_nan, 4);
	re_atomic_rlx_set(&lc->rx_level_bits, _nan_bits);
	re_atomic_rlx_set(&lc->tx_level_bits, _nan_bits);

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

	bsdk_call_register(lc);
	*ctx->out = lc;
}

int baresdk_call_invite(baresdk_account_handle_t acct,
                         const char *uri,
                         baresdk_call_handle_t *out)
{
	if (!acct || !uri || !out)
		return BARESDK_ERR_INVAL;

	invite_ctx_t ctx = {.acct = acct, .out = out, .result = 0};
	if (strncasecmp(uri, "sip:", 4) == 0 ||
	    strncasecmp(uri, "sips:", 5) == 0) {
		str_ncpy(ctx.uri, uri, sizeof(ctx.uri));
	} else {
		re_snprintf(ctx.uri, sizeof(ctx.uri), "sip:%s", uri);
	}

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

/* ── baresdk_call_hold ───────────────────────────────────────────────────── */

typedef struct { struct baresdk_call *lc; bool hold; int result; } hold_ctx_t;

static void hold_fn(void *arg)
{
	hold_ctx_t *ctx = arg;
	if (!ctx->lc->bc) { ctx->result = ENOENT; return; }
	ctx->result = call_hold(ctx->lc->bc, ctx->hold);
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
	return lc->state == BARESDK_CALL_HELD;
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
