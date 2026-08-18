/**
 * @file timers.c  Map baresdk_config_t timer/TOS/TLS fields → conf_config()
 *
 * Called from core.c after baresip_init() and before ua_init().
 * Only fields that baresip exposes in struct config are set here.
 * SIP RFC-3261 timers T1/T2 are compile-time constants in libre (SIP_T1/T2)
 * and are not runtime-configurable via the baresip config struct.
 */

#include "baresdk_internal.h"

void bsdk_timers_configure(const baresdk_config_t *cfg)
{
	struct config *c = conf_config();

	/* TOS / DSCP */
	c->sip.tos      = cfg->dscp_sip;
	c->avt.rtp_tos  = cfg->dscp_rtp;

	/* TLS CA certificate */
	if (cfg->ca_cert_path)
		str_ncpy(c->sip.cafile, cfg->ca_cert_path,
		         sizeof(c->sip.cafile));

	/* TLS client certificate */
	if (cfg->client_cert)
		str_ncpy(c->sip.cert, cfg->client_cert,
		         sizeof(c->sip.cert));

	/* Server certificate verification */
	c->sip.verify_server = cfg->verify_server;

	/* IPv6 preference */
	c->net.af = cfg->prefer_ipv6 ? AF_INET6 : AF_UNSPEC;

	/* Enable RTCP stats collection */
	c->avt.rtp_stats = (cfg->stats_interval_ms > 0);

	/* No-inbound-RTP call timeout.  baresip checks this per stream, only
	 * while the SDP direction is sendrecv, so a held call is not affected.
	 * 0 leaves it off, which is baresip's own default. */
	c->avt.rtp_timeout = cfg->rtp_timeout_s;

	/* Jitter buffer — adaptive mode with caller-supplied min/max bounds.
	 * Only applied when at least one bound is non-zero to preserve baresip
	 * defaults for callers that don't set either field. */
	if (cfg->jitter_buffer_min_ms || cfg->jitter_buffer_max_ms) {
		c->avt.audio.jbtype        = JBUF_ADAPTIVE;
		c->avt.audio.jbuf_del.min  = cfg->jitter_buffer_min_ms;
		c->avt.audio.jbuf_del.max  = cfg->jitter_buffer_max_ms
		                             ? cfg->jitter_buffer_max_ms : 150u;
	}

	/* Bind interface */
	if (cfg->bind_interface)
		str_ncpy(c->net.ifname, cfg->bind_interface,
		         sizeof(c->net.ifname));

	/* Session timer — disable at baresip level if not wanted */
	if (!cfg->session_timer_enabled)
		c->call.local_timeout = 0;

	/* Never verify client cert */
	c->sip.verify_client = false;
}
