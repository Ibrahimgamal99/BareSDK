/**
 * @file transfer.c  Blind + attended transfer (REFER / REFER w/ Replaces)
 *
 * Blind transfer   — baresdk_call_transfer() (in call.c)
 *                    sends REFER with Refer-To: <uri>
 *
 * Attended transfer — baresdk_call_attended_transfer(call_a, call_b)
 *                    sends REFER with Refer-To: <call_b_uri>;Replaces=...
 *                    baresip: call_replace_transfer(call_a->bc, call_b->bc)
 *
 * Incoming REFER   — BEVENT_CALL_TRANSFER fires with Refer-To URI in text.
 *                    If URI contains "?Replaces=" it is an attended transfer.
 *                    Surfaced as BARESDK_EV_TRANSFER_REQUEST.
 */

#include <string.h>
#include "baresdk_internal.h"

/* ── Attended transfer ───────────────────────────────────────────────────── */

typedef struct {
	struct baresdk_call *call_a;
	struct baresdk_call *call_b;
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

int baresdk_call_attended_transfer(baresdk_call_handle_t call_a,
                                    baresdk_call_handle_t call_b)
{
	if (!call_a || !call_b) return BARESDK_ERR_INVAL;
	atxfer_ctx_t ctx = {.call_a = call_a, .call_b = call_b, .result = 0};
	int err = bsdk_dispatch_sync(attended_transfer_fn, &ctx);
	return err ? err : ctx.result;
}

/* ── Incoming REFER handler — called from event.c ────────────────────────── */

void bsdk_transfer_handle_event(struct bevent *event)
{
	struct call *bc   = bevent_get_call(event);
	struct ua   *ua   = bevent_get_ua(event);
	const char  *text = bevent_get_text(event);   /* Refer-To URI */

	struct baresdk_call    *lc   = bc ? bsdk_call_find(bc)          : NULL;
	struct baresdk_account *acct = ua ? bsdk_account_find_by_ua(ua) : NULL;

	struct baresdk_queued_event *qev = mem_alloc(sizeof(*qev), NULL);
	if (!qev)
		return;
	memset(qev, 0, sizeof(*qev));

	qev->ev.type = BARESDK_EV_TRANSFER_REQUEST;
	baresdk_ev_transfer_req_t *tr = &qev->ev.u.transfer_req;
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

	mtx_lock(&g_bsdk.ev_lock);
	list_append(&g_bsdk.ev_queue, &qev->le, qev);
	cnd_signal(&g_bsdk.ev_cond);
	mtx_unlock(&g_bsdk.ev_lock);
}
