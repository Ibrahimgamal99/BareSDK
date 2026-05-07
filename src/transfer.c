/**
 * @file transfer.c  Blind + attended transfer (REFER / REFER w/ Replaces)
 *
 * Blind transfer   — libbare_call_transfer() (in call.c)
 *                    sends REFER with Refer-To: <uri>
 *
 * Attended transfer — libbare_call_attended_transfer(call_a, call_b)
 *                    sends REFER with Refer-To: <call_b_uri>;Replaces=...
 *                    baresip: call_replace_transfer(call_a->bc, call_b->bc)
 *
 * Incoming REFER   — BEVENT_CALL_TRANSFER fires with Refer-To URI in text.
 *                    If URI contains "?Replaces=" it is an attended transfer.
 *                    Surfaced as LIBBARE_EV_TRANSFER_REQUEST.
 */

#include <string.h>
#include "libbare_internal.h"

/* ── Attended transfer ───────────────────────────────────────────────────── */

typedef struct {
	struct libbare_call *call_a;
	struct libbare_call *call_b;
	int                  result;
} atxfer_ctx_t;

static void attended_transfer_fn(void *arg)
{
	atxfer_ctx_t *ctx = arg;
	if (!ctx->call_a->bc || !ctx->call_b->bc) {
		ctx->result = ENOENT;
		return;
	}
	ctx->result = call_replace_transfer(ctx->call_a->bc, ctx->call_b->bc);
}

int libbare_call_attended_transfer(libbare_call_handle_t call_a,
                                    libbare_call_handle_t call_b)
{
	if (!call_a || !call_b) return LIBBARE_ERR_INVAL;
	atxfer_ctx_t ctx = {.call_a = call_a, .call_b = call_b, .result = 0};
	int err = bare_dispatch_sync(attended_transfer_fn, &ctx);
	return err ? err : ctx.result;
}

/* ── Incoming REFER handler — called from event.c ────────────────────────── */

void bare_transfer_handle_event(struct bevent *event)
{
	struct call *bc   = bevent_get_call(event);
	struct ua   *ua   = bevent_get_ua(event);
	const char  *text = bevent_get_text(event);   /* Refer-To URI */

	struct libbare_call    *lc   = bc ? bare_call_find(bc)          : NULL;
	struct libbare_account *acct = ua ? bare_account_find_by_ua(ua) : NULL;

	struct libbare_queued_event *qev = mem_alloc(sizeof(*qev), NULL);
	if (!qev)
		return;
	memset(qev, 0, sizeof(*qev));

	qev->ev.type = LIBBARE_EV_TRANSFER_REQUEST;
	libbare_ev_transfer_req_t *tr = &qev->ev.u.transfer_req;
	tr->call    = lc;
	tr->account = acct;

	/* Pack Refer-To URI into buf */
	if (text) {
		str_ncpy(qev->buf, text, sizeof(qev->buf));
		tr->refer_to_uri = qev->buf;
		/* Attended transfer if Replaces parameter is present */
		tr->has_replaces = (strstr(text, "?Replaces=") != NULL ||
		                    strstr(text, "&Replaces=") != NULL);
	}

	mtx_lock(&g_bare.ev_lock);
	list_append(&g_bare.ev_queue, &qev->le, qev);
	cnd_signal(&g_bare.ev_cond);
	mtx_unlock(&g_bare.ev_lock);
}
