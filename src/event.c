/**
 * @file event.c  baresip bevent → baresdk_event_t bridge
 *
 * baresip's global event handler fires on re_main. We CANNOT call consumer
 * callbacks from re_main because consumers may call back into baresdk
 * (e.g. baresdk_call_answer() from inside BARESDK_EV_INCOMING_CALL), which
 * would deadlock if we tried to dispatch to re_main from re_main.
 *
 * Solution: re_main-side handler enqueues to a mutex+condvar queue.
 * A dedicated event thread drains the queue and calls the consumer callback.
 * The consumer callback runs on the event thread — never on re_main.
 */

#include <string.h>
#include "baresdk_internal.h"

/* ── Event thread ────────────────────────────────────────────────────────── */

static int event_thread_fn(void *arg)
{
	(void)arg;

	mtx_lock(&g_bsdk.ev_lock);

	for (;;) {
		/* Wait for work or shutdown signal */
		while (list_isempty(&g_bsdk.ev_queue) && !g_bsdk.ev_shutdown)
			cnd_wait(&g_bsdk.ev_cond, &g_bsdk.ev_lock);

		/* Drain the whole queue before checking shutdown again */
		struct le *le = list_head(&g_bsdk.ev_queue);
		if (le) {
			struct baresdk_queued_event *qev = le->data;
			list_unlink(&qev->le);
			g_bsdk.ev_queue_len--;
			mtx_unlock(&g_bsdk.ev_lock);

			if (g_bsdk.cfg.event_cb)
				g_bsdk.cfg.event_cb(&qev->ev,
				                    g_bsdk.cfg.event_userdata);
			mem_deref(qev);

			mtx_lock(&g_bsdk.ev_lock);
		} else if (g_bsdk.ev_shutdown) {
			break;
		}
	}

	mtx_unlock(&g_bsdk.ev_lock);
	return 0;
}

/* ── Internal helpers ────────────────────────────────────────────────────── */

/* Post a copy of ev to the event queue. Safe to call from re_main. */
void bsdk_event_post(const baresdk_event_t *ev)
{
	struct baresdk_queued_event *qev = mem_alloc(sizeof(*qev), NULL);
	if (!qev) {
		warning("baresdk/event: dropped event type %d (OOM)\n", ev->type);
		return;
	}

	memset(qev, 0, sizeof(*qev));
	memcpy(&qev->ev, ev, sizeof(*ev));

	mtx_lock(&g_bsdk.ev_lock);
	if (g_bsdk.ev_queue_len >= g_bsdk.ev_queue_max) {
		mtx_unlock(&g_bsdk.ev_lock);
		mem_deref(qev);
		warning("baresdk/event: dropped event type %d (queue full)\n",
		        ev->type);
		return;
	}
	list_append(&g_bsdk.ev_queue, &qev->le, qev);
	g_bsdk.ev_queue_len++;
	cnd_signal(&g_bsdk.ev_cond);
	mtx_unlock(&g_bsdk.ev_lock);
}

/* ── bevent handler (runs on re_main) ────────────────────────────────────── */

static void bevent_handler(enum bevent_ev bev,
                            struct bevent *event,
                            void *arg)
{
	(void)arg;

	baresdk_event_t ev = {0};
	bool post = true;
	struct ua   *ua   = bevent_get_ua(event);
	struct call *bc   = bevent_get_call(event);

	struct baresdk_account *acct =
		ua ? bsdk_account_find_by_ua(ua) : NULL;
	struct baresdk_call    *lc   =
		bc ? bsdk_call_find(bc) : NULL;

	switch (bev) {

	case BEVENT_REGISTERING:
		ev.type        = BARESDK_EV_REG_STATE;
		ev.u.reg.state = BARESDK_REG_REGISTERING;
		ev.u.reg.account = acct;
		if (acct) acct->reg_state = BARESDK_REG_REGISTERING;
		break;

	case BEVENT_REGISTER_OK:
		ev.type        = BARESDK_EV_REG_STATE;
		ev.u.reg.state = BARESDK_REG_REGISTERED;
		ev.u.reg.error = BARESDK_OK;
		ev.u.reg.account = acct;
		if (acct) {
			acct->reg_state    = BARESDK_REG_REGISTERED;
			acct->retry_attempt = 0;
		}
		break;

	case BEVENT_REGISTER_FAIL: {
		const char *reason = bevent_get_text(event);
		ev.type            = BARESDK_EV_REG_STATE;
		ev.u.reg.state     = BARESDK_REG_FAILED;
		ev.u.reg.account   = acct;
		/* Classify error — AUTH failures are terminal */
		if (reason && (strstr(reason, "401") || strstr(reason, "403")))
			ev.u.reg.error = BARESDK_ERR_AUTH;
		else if (reason && strstr(reason, "5"))
			ev.u.reg.error = BARESDK_ERR_SERVER_5XX;
		else
			ev.u.reg.error = BARESDK_ERR_TRANSPORT;

		/* Enqueue via a queued_event so the string lives long enough */
		struct baresdk_queued_event *qev = mem_alloc(sizeof(*qev), NULL);
		if (qev) {
			memset(qev, 0, sizeof(*qev));
			if (reason)
				str_ncpy(qev->buf, reason, sizeof(qev->buf));
			memcpy(&qev->ev, &ev, sizeof(ev));
			qev->ev.u.reg.error_str = reason ? qev->buf : NULL;
			if (acct) {
				acct->reg_state  = BARESDK_REG_FAILED;
				acct->reg_error  = ev.u.reg.error;
				if (reason)
					str_ncpy(acct->reg_error_str, reason,
					         sizeof(acct->reg_error_str));
				/* Trigger retry if not an auth failure */
				if (ev.u.reg.error != BARESDK_ERR_AUTH)
					bsdk_account_schedule_retry(acct);
			}
			mtx_lock(&g_bsdk.ev_lock);
			if (g_bsdk.ev_queue_len < g_bsdk.ev_queue_max) {
				list_append(&g_bsdk.ev_queue, &qev->le, qev);
				g_bsdk.ev_queue_len++;
				cnd_signal(&g_bsdk.ev_cond);
			} else {
				mem_deref(qev);
				warning("baresdk/event: dropped REGISTER_FAIL (queue full)\n");
			}
			mtx_unlock(&g_bsdk.ev_lock);
		}
		post = false;
		break;
	}

	case BEVENT_UNREGISTERING:
		ev.type        = BARESDK_EV_REG_STATE;
		ev.u.reg.state = BARESDK_REG_UNREGISTERING;
		ev.u.reg.account = acct;
		if (acct) acct->reg_state = BARESDK_REG_UNREGISTERING;
		break;

	case BEVENT_CALL_INCOMING: {
		struct baresdk_call *new_lc = mem_alloc(sizeof(*new_lc), NULL);
		if (!new_lc) { post = false; break; }
		memset(new_lc, 0, sizeof(*new_lc));
		new_lc->bc    = bc;
		new_lc->acct  = acct;
		new_lc->state = BARESDK_CALL_RINGING;
		mtx_init(&new_lc->tap_lock, mtx_plain);
		list_init(&new_lc->custom_hdrs);
		bsdk_call_register(new_lc);

		struct baresdk_queued_event *qev = mem_alloc(sizeof(*qev), NULL);
		if (qev) {
			memset(qev, 0, sizeof(*qev));
			qev->ev.type = BARESDK_EV_INCOMING_CALL;
			qev->ev.u.incoming.account = acct;
			qev->ev.u.incoming.call    = new_lc;
			const char *peer = call_peeruri(bc);
			if (peer) {
				str_ncpy(qev->buf, peer, sizeof(qev->buf));
				qev->ev.u.incoming.from_uri = qev->buf;
			}
			mtx_lock(&g_bsdk.ev_lock);
			if (g_bsdk.ev_queue_len < g_bsdk.ev_queue_max) {
				list_append(&g_bsdk.ev_queue, &qev->le, qev);
				g_bsdk.ev_queue_len++;
				cnd_signal(&g_bsdk.ev_cond);
			} else {
				mem_deref(qev);
				warning("baresdk/event: dropped INCOMING_CALL (queue full)\n");
			}
			mtx_unlock(&g_bsdk.ev_lock);
		}
		post = false;
		break;
	}

	case BEVENT_CALL_RINGING:
		ev.type = BARESDK_EV_CALL_STATE;
		ev.u.call_state.call    = lc;
		ev.u.call_state.account = acct;
		ev.u.call_state.state   = BARESDK_CALL_RINGING;
		if (lc) lc->state = BARESDK_CALL_RINGING;
		break;

	case BEVENT_CALL_ANSWERED:
	case BEVENT_CALL_ESTABLISHED:
		ev.type = BARESDK_EV_CALL_STATE;
		ev.u.call_state.call    = lc;
		ev.u.call_state.account = acct;
		ev.u.call_state.state   = BARESDK_CALL_ESTABLISHED;
		if (lc) lc->state = BARESDK_CALL_ESTABLISHED;
		break;

	case BEVENT_CALL_CLOSED: {
		const char *reason = bevent_get_text(event);
		ev.type = BARESDK_EV_CALL_STATE;
		ev.u.call_state.call    = lc;
		ev.u.call_state.account = acct;
		ev.u.call_state.state   = BARESDK_CALL_ENDED;

		struct baresdk_queued_event *qev = mem_alloc(sizeof(*qev), NULL);
		if (qev) {
			memset(qev, 0, sizeof(*qev));
			memcpy(&qev->ev, &ev, sizeof(ev));
			if (reason) {
				str_ncpy(qev->buf, reason, sizeof(qev->buf));
				qev->ev.u.call_state.reason = qev->buf;
			}
			mtx_lock(&g_bsdk.ev_lock);
			if (g_bsdk.ev_queue_len < g_bsdk.ev_queue_max) {
				list_append(&g_bsdk.ev_queue, &qev->le, qev);
				g_bsdk.ev_queue_len++;
				cnd_signal(&g_bsdk.ev_cond);
			} else {
				mem_deref(qev);
				warning("baresdk/event: dropped CALL_CLOSED (queue full)\n");
			}
			mtx_unlock(&g_bsdk.ev_lock);
		}
		if (lc) {
			lc->state = BARESDK_CALL_ENDED;
			lc->bc    = NULL; /* baresip will free the call */
			bsdk_call_unregister(lc);
		}
		post = false;
		break;
	}

	case BEVENT_CALL_HOLD:
		ev.type = BARESDK_EV_CALL_STATE;
		ev.u.call_state.call    = lc;
		ev.u.call_state.account = acct;
		ev.u.call_state.state   = BARESDK_CALL_HELD;
		if (lc) lc->state = BARESDK_CALL_HELD;
		break;

	case BEVENT_CALL_RESUME:
		ev.type = BARESDK_EV_CALL_STATE;
		ev.u.call_state.call    = lc;
		ev.u.call_state.account = acct;
		ev.u.call_state.state   = BARESDK_CALL_ESTABLISHED;
		if (lc) lc->state = BARESDK_CALL_ESTABLISHED;
		break;

	case BEVENT_CALL_DTMF_START:
	case BEVENT_CALL_DTMF_END: {
		const char *txt = bevent_get_text(event);
		if (!txt || !txt[0]) { post = false; break; }
		ev.type = BARESDK_EV_CALL_DTMF;
		ev.u.dtmf.call  = lc;
		ev.u.dtmf.digit = txt[0];
		break;
	}

	case BEVENT_CALL_LOCAL_SDP:
	case BEVENT_CALL_REMOTE_SDP:
		bsdk_sdp_handle_event(bev, event);
		post = false;
		break;

	/* ── Transfer / MWI ──────────────────────────────────────────────────── */

	case BEVENT_CALL_TRANSFER:
	case BEVENT_REFER:
		/* Refer-To URI is in bevent_get_text(); attended = has "Replaces=" */
		bsdk_transfer_handle_event(event);
		post = false;
		break;

	case BEVENT_CALL_TRANSFER_FAILED: {
		const char *reason = bevent_get_text(event);
		ev.type = BARESDK_EV_CALL_STATE;
		ev.u.call_state.call    = lc;
		ev.u.call_state.account = acct;
		ev.u.call_state.state   = BARESDK_CALL_FAILED;
		if (lc) lc->state = BARESDK_CALL_FAILED;

		if (reason) {
			struct baresdk_queued_event *qev = mem_alloc(sizeof(*qev), NULL);
			if (qev) {
				memset(qev, 0, sizeof(*qev));
				memcpy(&qev->ev, &ev, sizeof(ev));
				str_ncpy(qev->buf, reason, sizeof(qev->buf));
				qev->ev.u.call_state.reason = qev->buf;
				mtx_lock(&g_bsdk.ev_lock);
				if (g_bsdk.ev_queue_len < g_bsdk.ev_queue_max) {
					list_append(&g_bsdk.ev_queue, &qev->le, qev);
					g_bsdk.ev_queue_len++;
					cnd_signal(&g_bsdk.ev_cond);
				} else {
					mem_deref(qev);
					warning("baresdk/event: dropped TRANSFER_FAILED (queue full)\n");
				}
				mtx_unlock(&g_bsdk.ev_lock);
			}
			post = false;
		}
		break;
	}

	case BEVENT_MWI_NOTIFY:
		bsdk_presence_handle_mwi(event);
		post = false;
		break;

	default:
		post = false;
		break;
	}

	if (post)
		bsdk_event_post(&ev);
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

int bsdk_event_init(void)
{
	g_bsdk.ev_shutdown = false;

	int rc = thrd_create(&g_bsdk.ev_thread, event_thread_fn, NULL);
	if (rc != thrd_success)
		return ENOMEM;

	return bevent_register(bevent_handler, NULL);
}

void bsdk_event_close(void)
{
	/* Unregister the baresip hook first so no new events arrive */
	bevent_unregister(bevent_handler);

	/* Signal the event thread to drain and exit */
	mtx_lock(&g_bsdk.ev_lock);
	g_bsdk.ev_shutdown = true;
	cnd_signal(&g_bsdk.ev_cond);
	mtx_unlock(&g_bsdk.ev_lock);

	thrd_join(g_bsdk.ev_thread, NULL);

	/* Free any remaining queued events */
	struct le *le, *le_tmp;
	LIST_FOREACH_SAFE(&g_bsdk.ev_queue, le, le_tmp) {
		struct baresdk_queued_event *qev = le->data;
		list_unlink(&qev->le);
		mem_deref(qev);
	}
}
