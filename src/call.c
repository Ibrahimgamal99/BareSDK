/**
 * @file call.c  INVITE FSM, hold/resume, DTMF, blind transfer, per-dialog headers
 *
 * libbare_call_t wraps a baresip struct call. Call objects are created either
 * by libbare_call_invite() (outgoing) or by event.c when BEVENT_CALL_INCOMING
 * fires. The call list is guarded by s_calls_lock.
 */

#include "libbare_internal.h"

static struct list s_calls;
static mtx_t       s_calls_lock;
static bool        s_calls_initialized = false;

static void ensure_calls_init(void)
{
	mtx_lock(&s_calls_lock);
	if (!s_calls_initialized) {
		mtx_init(&s_calls_lock, mtx_plain);
		list_init(&s_calls);
		s_calls_initialized = true;
	}
	mtx_unlock(&s_calls_lock);
}

void bare_call_global_reset(void)
{
	mtx_lock(&s_calls_lock);
	if (s_calls_initialized) {
		struct le *le, *le_tmp;
		LIST_FOREACH_SAFE(&s_calls, le, le_tmp) {
			struct libbare_call *lc = le->data;
			list_unlink(&lc->le);
			mem_deref(lc);
		}
		mtx_destroy(&s_calls_lock);
		s_calls_initialized = false;
	}
	mtx_unlock(&s_calls_lock);
}

/* ── Call lookup ─────────────────────────────────────────────────────────── */

struct libbare_call *bare_call_find(const struct call *bc)
{
	ensure_calls_init();
	struct le *le;
	mtx_lock(&s_calls_lock);
	LIST_FOREACH(&s_calls, le) {
		struct libbare_call *lc = le->data;
		if (lc->bc == bc) {
			mtx_unlock(&s_calls_lock);
			return lc;
		}
	}
	mtx_unlock(&s_calls_lock);
	return NULL;
}

void bare_call_register(struct libbare_call *lc)
{
	ensure_calls_init();
	mtx_lock(&s_calls_lock);
	list_append(&s_calls, &lc->le, lc);
	mtx_unlock(&s_calls_lock);
}

void bare_call_unregister(struct libbare_call *lc)
{
	ensure_calls_init();
	mtx_lock(&s_calls_lock);
	list_unlink(&lc->le);
	mtx_unlock(&s_calls_lock);
}

void bare_call_foreach(void (*fn)(struct libbare_call *, void *), void *arg)
{
	ensure_calls_init();
	mtx_lock(&s_calls_lock);
	struct le *le;
	LIST_FOREACH(&s_calls, le) {
		struct libbare_call *lc = le->data;
		fn(lc, arg);
	}
	mtx_unlock(&s_calls_lock);
}

/* ── call_destructor ─────────────────────────────────────────────────────── */

static void custom_hdr_destructor(void *data)
{
	struct bare_custom_hdr *hdr = data;
	mem_deref(hdr->name);
	mem_deref(hdr->value);
}

static void call_destructor(void *data)
{
	struct libbare_call *lc = data;
	struct le *le, *le_tmp;
	LIST_FOREACH_SAFE(&lc->custom_hdrs, le, le_tmp) {
		struct bare_custom_hdr *hdr = le->data;
		list_unlink(&hdr->le);
		mem_deref(hdr);
	}
	mtx_destroy(&lc->tap_lock);
}

/* ── libbare_call_invite ─────────────────────────────────────────────────── */

typedef struct {
	struct libbare_account   *acct;
	char                      uri[512];
	libbare_call_handle_t    *out;
	int                       result;
} invite_ctx_t;

static void invite_fn(void *arg)
{
	invite_ctx_t *ctx = arg;
	struct call *bc = NULL;

	ctx->result = ua_connect(ctx->acct->ua, &bc, NULL,
	                         ctx->uri, VIDMODE_OFF);
	if (ctx->result || !bc)
		return;

	struct libbare_call *lc = mem_alloc(sizeof(*lc), call_destructor);
	if (!lc) { ctx->result = ENOMEM; return; }
	memset(lc, 0, sizeof(*lc));
	mtx_init(&lc->tap_lock, mtx_plain);
	list_init(&lc->custom_hdrs);
	lc->bc    = bc;
	lc->acct  = ctx->acct;
	lc->state = LIBBARE_CALL_CALLING;

	struct le *le;
	LIST_FOREACH(&ctx->acct->custom_hdrs, le) {
		struct bare_custom_hdr *acct_hdr = le->data;
		struct bare_custom_hdr *ch = mem_alloc(sizeof(*ch),
		                                       custom_hdr_destructor);
		if (!ch) continue;
		ch->name  = bare_strdup(acct_hdr->name);
		ch->value = bare_strdup(acct_hdr->value);
		if (!ch->name || !ch->value) {
			mem_deref(ch);
			continue;
		}
		struct pl name_pl;
		pl_set_str(&name_pl, ch->name);
		call_add_custom_hdr(bc, &name_pl, "%s", ch->value);
		list_append(&lc->custom_hdrs, &ch->le, ch);
	}

	bare_call_register(lc);
	*ctx->out = lc;
}

int libbare_call_invite(libbare_account_handle_t acct,
                         const char *uri,
                         libbare_call_handle_t *out)
{
	if (!acct || !uri || !out)
		return LIBBARE_ERR_INVAL;

	invite_ctx_t ctx = {.acct = acct, .out = out, .result = 0};
	str_ncpy(ctx.uri, uri, sizeof(ctx.uri));

	int err = bare_dispatch_sync(invite_fn, &ctx);
	return err ? err : ctx.result;
}

/* ── libbare_call_answer ─────────────────────────────────────────────────── */

typedef struct { struct libbare_call *lc; int result; } simple_call_ctx_t;

static void answer_fn(void *arg)
{
	simple_call_ctx_t *ctx = arg;
	struct libbare_call *lc = ctx->lc;
	if (!lc->bc) { ctx->result = ENOENT; return; }
	ctx->result = ua_answer(lc->acct->ua, lc->bc, VIDMODE_OFF);
}

int libbare_call_answer(libbare_call_handle_t call)
{
	if (!call) return LIBBARE_ERR_INVAL;
	simple_call_ctx_t ctx = {.lc = call, .result = 0};
	int err = bare_dispatch_sync(answer_fn, &ctx);
	return err ? err : ctx.result;
}

/* ── libbare_call_hangup ─────────────────────────────────────────────────── */

static void hangup_fn(void *arg)
{
	struct libbare_call *lc = arg;
	if (!lc->bc) return;
	ua_hangup(lc->acct->ua, lc->bc, 0, NULL);
	lc->state = LIBBARE_CALL_ENDED;
}

int libbare_call_hangup(libbare_call_handle_t call)
{
	if (!call) return LIBBARE_ERR_INVAL;
	return bare_dispatch(hangup_fn, call);
}

/* ── libbare_call_hold ───────────────────────────────────────────────────── */

typedef struct { struct libbare_call *lc; bool hold; int result; } hold_ctx_t;

static void hold_fn(void *arg)
{
	hold_ctx_t *ctx = arg;
	if (!ctx->lc->bc) { ctx->result = ENOENT; return; }
	ctx->result = call_hold(ctx->lc->bc, ctx->hold);
}

int libbare_call_hold(libbare_call_handle_t call)
{
	if (!call) return LIBBARE_ERR_INVAL;
	hold_ctx_t ctx = {.lc = call, .hold = true, .result = 0};
	int err = bare_dispatch_sync(hold_fn, &ctx);
	return err ? err : ctx.result;
}

int libbare_call_resume(libbare_call_handle_t call)
{
	if (!call) return LIBBARE_ERR_INVAL;
	hold_ctx_t ctx = {.lc = call, .hold = false, .result = 0};
	int err = bare_dispatch_sync(hold_fn, &ctx);
	return err ? err : ctx.result;
}

/* ── libbare_call_send_dtmf ──────────────────────────────────────────────── */

typedef struct { struct libbare_call *lc; char digit; int result; } dtmf_ctx_t;

static void dtmf_fn(void *arg)
{
	dtmf_ctx_t *ctx = arg;
	if (!ctx->lc->bc) { ctx->result = ENOENT; return; }
	ctx->result = call_send_digit(ctx->lc->bc, ctx->digit);
}

int libbare_call_send_dtmf(libbare_call_handle_t call, char digit)
{
	if (!call) return LIBBARE_ERR_INVAL;
	dtmf_ctx_t ctx = {.lc = call, .digit = digit, .result = 0};
	int err = bare_dispatch_sync(dtmf_fn, &ctx);
	return err ? err : ctx.result;
}

/* ── libbare_call_transfer ───────────────────────────────────────────────── */

typedef struct { struct libbare_call *lc; char uri[512]; int result; } xfer_ctx_t;

static void transfer_fn(void *arg)
{
	xfer_ctx_t *ctx = arg;
	if (!ctx->lc->bc) { ctx->result = ENOENT; return; }
	ctx->result = call_transfer(ctx->lc->bc, ctx->uri);
}

int libbare_call_transfer(libbare_call_handle_t call, const char *uri)
{
	if (!call || !uri) return LIBBARE_ERR_INVAL;
	xfer_ctx_t ctx = {.lc = call, .result = 0};
	str_ncpy(ctx.uri, uri, sizeof(ctx.uri));
	int err = bare_dispatch_sync(transfer_fn, &ctx);
	return err ? err : ctx.result;
}

/* ── Per-dialog custom headers ─────────────────────────────────────────── */

typedef struct {
	struct libbare_call *lc;
	const char          *name;
	const char          *value;
	int                  result;
} call_hdr_ctx_t;

static void add_call_hdr_fn(void *arg)
{
	call_hdr_ctx_t *ctx = arg;
	if (!ctx->lc->bc) { ctx->result = ENOENT; return; }

	struct bare_custom_hdr *ch = mem_alloc(sizeof(*ch),
	                                       custom_hdr_destructor);
	if (!ch) { ctx->result = ENOMEM; return; }
	ch->name  = bare_strdup(ctx->name);
	ch->value = bare_strdup(ctx->value);
	if (!ch->name || !ch->value) {
		mem_deref(ch);
		ctx->result = ENOMEM;
		return;
	}

	struct pl name_pl;
	pl_set_str(&name_pl, ch->name);
	ctx->result = call_add_custom_hdr(ctx->lc->bc, &name_pl,
	                                  "%s", ch->value);
	if (ctx->result) {
		mem_deref(ch);
		return;
	}
	list_append(&ctx->lc->custom_hdrs, &ch->le, ch);
}

int libbare_call_add_header(libbare_call_handle_t call,
                             const char *name, const char *value)
{
	if (!call || !name || !value) return LIBBARE_ERR_INVAL;
	call_hdr_ctx_t ctx = {.lc = call, .name = name,
	                       .value = value, .result = 0};
	int err = bare_dispatch_sync(add_call_hdr_fn, &ctx);
	return err ? err : ctx.result;
}
