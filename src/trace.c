/**
 * @file trace.c  SIP message trace hook → LIBBARE_EV_SIP_TRACE
 *
 * Registers a sip_trace_h with the libre SIP stack via uag_sip().
 * Each raw SIP message is forwarded to:
 *   1. The consumer as LIBBARE_EV_SIP_TRACE (via event queue)
 *   2. bare_pcap_write_sip() if pcap recording is active
 *
 * The handler fires on the re_main thread; we enqueue and return immediately.
 */

#include "libbare_internal.h"

/* ── Transport name helper ───────────────────────────────────────────────── */

static const char *transp_name(enum sip_transp tp)
{
	switch (tp) {
	case SIP_TRANSP_UDP: return "UDP";
	case SIP_TRANSP_TCP: return "TCP";
	case SIP_TRANSP_TLS: return "TLS";
	case SIP_TRANSP_WS:  return "WS";
	case SIP_TRANSP_WSS: return "WSS";
	default:             return "?";
	}
}

/* ── SIP trace callback (re_main thread) ─────────────────────────────────── */

static void sip_trace_handler(bool tx, enum sip_transp tp,
                               const struct sa *src, const struct sa *dst,
                               const uint8_t *pkt, size_t len, void *arg)
{
	(void)arg;

	/* Write to pcap if enabled */
	if (g_bare.pcap_file)
		bare_pcap_write_sip((const char *)pkt, len,
		                    tx ? NULL : src,
		                    tx ? dst  : NULL,
		                    tp == SIP_TRANSP_UDP);

	if (!g_bare.cfg.trace_sip || !g_bare.cfg.event_cb)
		return;

	/* Build remote address string: "host:port" */
	char remote[64] = {0};
	const struct sa *peer = tx ? dst : src;
	if (peer)
		re_snprintf(remote, sizeof(remote), "%H", sa_print_addr, peer);

	struct libbare_queued_event *qev = mem_alloc(sizeof(*qev), NULL);
	if (!qev)
		return;
	memset(qev, 0, sizeof(*qev));

	qev->ev.type = LIBBARE_EV_SIP_TRACE;
	qev->ev.u.sip_trace.timestamp_us = tmr_jiffies() * 1000ULL;

	/* Pack strings into buf: transport\0remote_addr\0raw_message\0 */
	size_t off = 0;

	const char *tn = transp_name(tp);
	str_ncpy(qev->buf + off, tn, sizeof(qev->buf) - off);
	qev->ev.u.sip_trace.transport = qev->buf + off;
	off += strlen(tn) + 1;

	str_ncpy(qev->buf + off, remote, sizeof(qev->buf) - off);
	qev->ev.u.sip_trace.remote_addr = qev->buf + off;
	off += strlen(remote) + 1;

	size_t copy_len = len;
	if (copy_len > sizeof(qev->buf) - off - 1)
		copy_len = sizeof(qev->buf) - off - 1;
	memcpy(qev->buf + off, pkt, copy_len);
	qev->buf[off + copy_len] = '\0';
	qev->ev.u.sip_trace.raw_message = qev->buf + off;

	/* tx/rx stored separately — reuse transport field prefix */
	/* (direction is implicit: TX = sent, RX = received) */

	mtx_lock(&g_bare.ev_lock);
	list_append(&g_bare.ev_queue, &qev->le, qev);
	cnd_signal(&g_bare.ev_cond);
	mtx_unlock(&g_bare.ev_lock);
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

int bare_trace_init(void)
{
	if (!g_bare.cfg.trace_sip && !g_bare.cfg.pcap_path)
		return 0;

	struct sip *sip = uag_sip();
	if (!sip)
		return ENOENT;

	sip_set_trace_handler(sip, sip_trace_handler);
	return 0;
}

void bare_trace_close(void)
{
	struct sip *sip = uag_sip();
	if (sip)
		sip_set_trace_handler(sip, NULL);
}
