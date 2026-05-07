/**
 * @file timers.c  Map libbare_config_t timer/TOS/TLS fields → conf_config()
 *
 * Called from core.c after baresip_init() and before ua_init().
 * Only fields that baresip exposes in struct config are set here.
 * SIP RFC-3261 timers T1/T2 are compile-time constants in libre (SIP_T1/T2)
 * and are not runtime-configurable via the baresip config struct.
 */

#include "libbare_internal.h"

void bare_timers_configure(const libbare_config_t *cfg)
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
