/**
 * @file sdp.c  SDP offer/answer capture → LIBBARE_EV_SDP_NEGOTIATION
 *
 * Called from event.c for BEVENT_CALL_LOCAL_SDP and BEVENT_CALL_REMOTE_SDP.
 * Tracks when both local and remote SDP have been exchanged, then fires a
 * single negotiation event with the active codec and crypto info.
 *
 * The negotiation event resets the flags so re-INVITE renegotiation produces
 * another event.
 */

#include "libbare_internal.h"

void bare_sdp_handle_event(enum bevent_ev ev, struct bevent *event)
{
	struct call *bc = bevent_get_call(event);
	if (!bc)
		return;

	struct libbare_call *lc = bare_call_find(bc);
	if (!lc)
		return;

	if (ev == BEVENT_CALL_LOCAL_SDP)
		lc->local_sdp_set = true;
	else
		lc->remote_sdp_set = true;

	if (!lc->local_sdp_set || !lc->remote_sdp_set)
		return;

	/* Reset so re-INVITE renegotiation fires again */
	lc->local_sdp_set  = false;
	lc->remote_sdp_set = false;

	/* Collect negotiated codec from active TX encoder */
	const char *codec_name = NULL;
	struct audio *au = call_audio(bc);
	if (au) {
		const struct aucodec *ac = audio_codec(au, true);
		if (ac)
			codec_name = ac->name;
	}

	/* Determine crypto from global config */
	const char *crypto;
	switch (g_bare.cfg.media_enc) {
	case LIBBARE_MEDIA_ENC_SDES:       crypto = "SDES";      break;
	case LIBBARE_MEDIA_ENC_DTLS_SRTP:  crypto = "DTLS-SRTP"; break;
	default:                           crypto = "NONE";       break;
	}

	struct libbare_queued_event *qev = mem_alloc(sizeof(*qev), NULL);
	if (!qev)
		return;
	memset(qev, 0, sizeof(*qev));

	qev->ev.type      = LIBBARE_EV_SDP_NEGOTIATION;
	qev->ev.u.sdp.call = lc;

	/* Pack strings into the inline buffer */
	size_t off = 0;

	if (codec_name) {
		str_ncpy(qev->buf + off, codec_name, sizeof(qev->buf) - off);
		qev->ev.u.sdp.negotiated_codec = qev->buf + off;
		off += strlen(codec_name) + 1;
	}

	str_ncpy(qev->buf + off, crypto, sizeof(qev->buf) - off);
	qev->ev.u.sdp.negotiated_crypto = qev->buf + off;

	mtx_lock(&g_bare.ev_lock);
	list_append(&g_bare.ev_queue, &qev->le, qev);
	cnd_signal(&g_bare.ev_cond);
	mtx_unlock(&g_bare.ev_lock);
}
