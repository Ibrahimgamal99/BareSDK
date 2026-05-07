/**
 * @file core.c  Singleton lifecycle — libbare_init / libbare_shutdown
 */

#include <string.h>
#include "libbare_internal.h"

/* ── Global singleton ────────────────────────────────────────────────────── */

struct bare_ctx g_bare;

#define LIBBARE_EV_QUEUE_MAX 4096

/* ── Deep-copy helpers ───────────────────────────────────────────────────── */

char *bare_strdup(const char *s)
{
	if (!s) return NULL;
	size_t len = strlen(s);
	char *dup = mem_alloc(len + 1, NULL);
	if (dup) {
		memcpy(dup, s, len + 1);
	}
	return dup;
}

static char *bare_strdup_arr_elem(char **arr, const char *s)
{
	(void)arr;
	return bare_strdup(s);
}

static char **bare_strdup_strv(const char * const *src)
{
	if (!src) return NULL;
	size_t count = 0;
	while (src[count]) count++;
	char **dst = mem_alloc((count + 1) * sizeof(char *), NULL);
	if (!dst) return NULL;
	for (size_t i = 0; i < count; i++) {
		dst[i] = bare_strdup(src[i]);
		if (!dst[i]) {
			for (size_t j = 0; j < i; j++) mem_deref(dst[j]);
			mem_deref(dst);
			return NULL;
		}
	}
	dst[count] = NULL;
	return dst;
}

static void bare_free_strv(char **arr)
{
	if (!arr) return;
	for (size_t i = 0; arr[i]; i++)
		mem_deref(arr[i]);
	mem_deref(arr);
}

void bare_cfg_deep_copy(libbare_config_t *dst, const libbare_config_t *src,
                         struct bare_ctx *ctx)
{
	memcpy(dst, src, sizeof(*src));

	ctx->cfg_local_ip         = bare_strdup(src->local_ip);
	ctx->cfg_sip_domain       = bare_strdup(src->sip_domain);
	ctx->cfg_server_url       = bare_strdup(src->server_url);
	ctx->cfg_server_host      = bare_strdup(src->server_host);
	ctx->cfg_outbound_proxy   = bare_strdup(src->outbound_proxy);
	ctx->cfg_ca_cert_path     = bare_strdup(src->ca_cert_path);
	ctx->cfg_client_cert      = bare_strdup(src->client_cert);
	ctx->cfg_client_key       = bare_strdup(src->client_key);
	ctx->cfg_sni_hostname     = bare_strdup(src->sni_hostname);
	ctx->cfg_user_agent       = bare_strdup(src->user_agent);
	ctx->cfg_ws_origin        = bare_strdup(src->ws_origin);
	ctx->cfg_ws_extra_headers = bare_strdup_strv(src->ws_extra_headers);
	ctx->cfg_stun_server      = bare_strdup(src->stun_server);
	ctx->cfg_turn_server      = bare_strdup(src->turn_server);
	ctx->cfg_turn_user        = bare_strdup(src->turn_user);
	ctx->cfg_turn_pass        = bare_strdup(src->turn_pass);
	ctx->cfg_pcap_path        = bare_strdup(src->pcap_path);

	dst->local_ip         = ctx->cfg_local_ip;
	dst->sip_domain       = ctx->cfg_sip_domain;
	dst->server_url       = ctx->cfg_server_url;
	dst->server_host      = ctx->cfg_server_host;
	dst->outbound_proxy   = ctx->cfg_outbound_proxy;
	dst->ca_cert_path     = ctx->cfg_ca_cert_path;
	dst->client_cert      = ctx->cfg_client_cert;
	dst->client_key       = ctx->cfg_client_key;
	dst->sni_hostname     = ctx->cfg_sni_hostname;
	dst->user_agent       = ctx->cfg_user_agent;
	dst->ws_origin        = ctx->cfg_ws_origin;
	dst->ws_extra_headers = (const char **)ctx->cfg_ws_extra_headers;
	dst->stun_server      = ctx->cfg_stun_server;
	dst->turn_server      = ctx->cfg_turn_server;
	dst->turn_user        = ctx->cfg_turn_user;
	dst->turn_pass        = ctx->cfg_turn_pass;
	dst->pcap_path        = ctx->cfg_pcap_path;
}

void bare_cfg_deep_free(struct bare_ctx *ctx)
{
	mem_deref(ctx->cfg_local_ip);         ctx->cfg_local_ip = NULL;
	mem_deref(ctx->cfg_sip_domain);       ctx->cfg_sip_domain = NULL;
	mem_deref(ctx->cfg_server_url);       ctx->cfg_server_url = NULL;
	mem_deref(ctx->cfg_server_host);      ctx->cfg_server_host = NULL;
	mem_deref(ctx->cfg_outbound_proxy);   ctx->cfg_outbound_proxy = NULL;
	mem_deref(ctx->cfg_ca_cert_path);     ctx->cfg_ca_cert_path = NULL;
	mem_deref(ctx->cfg_client_cert);      ctx->cfg_client_cert = NULL;
	mem_deref(ctx->cfg_client_key);       ctx->cfg_client_key = NULL;
	mem_deref(ctx->cfg_sni_hostname);     ctx->cfg_sni_hostname = NULL;
	mem_deref(ctx->cfg_user_agent);       ctx->cfg_user_agent = NULL;
	mem_deref(ctx->cfg_ws_origin);        ctx->cfg_ws_origin = NULL;
	bare_free_strv(ctx->cfg_ws_extra_headers); ctx->cfg_ws_extra_headers = NULL;
	mem_deref(ctx->cfg_stun_server);      ctx->cfg_stun_server = NULL;
	mem_deref(ctx->cfg_turn_server);      ctx->cfg_turn_server = NULL;
	mem_deref(ctx->cfg_turn_user);        ctx->cfg_turn_user = NULL;
	mem_deref(ctx->cfg_turn_pass);        ctx->cfg_turn_pass = NULL;
	mem_deref(ctx->cfg_pcap_path);        ctx->cfg_pcap_path = NULL;
}

/* ── libbare_config_init ─────────────────────────────────────────────────── */

void libbare_config_init(libbare_config_t *cfg)
{
	if (!cfg)
		return;
	memset(cfg, 0, sizeof(*cfg));
	cfg->version     = LIBBARE_CONFIG_VERSION;
	cfg->struct_size = sizeof(libbare_config_t);

	cfg->transport             = LIBBARE_TRANSPORT_UDP;
	cfg->verify_server         = true;
	cfg->reg_expires           = 3600;
	cfg->reg_refresh_pct       = 75;
	cfg->keepalive_interval    = 30000;
	cfg->reg_retry_initial_ms  = 2000;
	cfg->reg_retry_max_ms      = 300000;
	cfg->reg_retry_backoff     = 2.0f;
	cfg->sip_t1_ms             = 500;
	cfg->sip_t2_ms             = 4000;
	cfg->sip_timer_b_ms        = 32000;
	cfg->sip_timer_f_ms        = 32000;
	cfg->session_timer_enabled = true;
	cfg->session_expires_s     = 1800;
	cfg->session_min_se_s      = 90;
	cfg->mos_method            = LIBBARE_MOS_EMODEL;
	cfg->log_level             = 1;
}

/* ── libbare_init ────────────────────────────────────────────────────────── */

int libbare_init(const libbare_config_t *cfg)
{
	int err;

	if (!cfg || !cfg->event_cb)
		return LIBBARE_ERR_INVAL;
	if (cfg->version != LIBBARE_CONFIG_VERSION)
		return LIBBARE_ERR_INVAL;
	if (cfg->struct_size != sizeof(libbare_config_t))
		return LIBBARE_ERR_INVAL;
	if (cfg->audio_codec_count < 0 || cfg->audio_codec_count > 8)
		return LIBBARE_ERR_INVAL;

	static bool once = false;
	if (!once) {
		once = true;
		mtx_init(&g_bare.lock, mtx_plain);
	}

	mtx_lock(&g_bare.lock);

	if (g_bare.initialized) {
		mtx_unlock(&g_bare.lock);
		return LIBBARE_ERR_ALREADY;
	}

	bare_cfg_deep_copy(&g_bare.cfg, cfg, &g_bare);

	list_init(&g_bare.ev_queue);
	list_init(&g_bare.accounts);
	g_bare.ev_queue_max = LIBBARE_EV_QUEUE_MAX;
	g_bare.ev_queue_len = 0;
	mtx_init(&g_bare.ev_lock, mtx_plain);
	cnd_init(&g_bare.ev_cond);
	mtx_init(&g_bare.acct_lock, mtx_plain);
	mtx_init(&g_bare.pcap_lock, mtx_plain);

	err = bare_log_init();
	if (err)
		goto fail;

	err = libre_init();
	if (err)
		goto fail;

	err = baresip_init(conf_config());
	if (err)
		goto fail;

	err = bare_dns_init();
	if (err)
		goto fail;

	bare_timers_configure(&g_bare.cfg);

	{
		const char *sw = g_bare.cfg.user_agent ? g_bare.cfg.user_agent
		                                       : "libbare/1.0";
		err = ua_init(sw, true, false, false);
	}
	if (err)
		goto fail;

	err = bare_event_init();
	if (err)
		goto fail;

	if (g_bare.cfg.trace_sip) {
		err = bare_trace_init();
		if (err)
			goto fail;
	}

	err = modules_init();
	if (err)
		goto fail;

	err = bare_message_init();
	if (err)
		goto fail;

	err = bare_presence_init();
	if (err)
		goto fail;

	if (g_bare.cfg.pcap_path) {
		err = bare_pcap_open(g_bare.cfg.pcap_path);
		if (err)
			goto fail;
	}

	if (g_bare.cfg.stats_interval_ms > 0) {
		err = bare_stats_init();
		if (err)
			goto fail;
	}

	err = bare_re_loop_start();
	if (err)
		goto fail;

	g_bare.initialized = true;
	mtx_unlock(&g_bare.lock);
	return LIBBARE_OK;

fail:
	warning("libbare: init failed: %m\n", err);
	bare_re_loop_stop();
	bare_stats_close();
	bare_trace_close();
	bare_event_close();
	ua_close();
	bare_dns_close();
	baresip_close();
	libre_close();
	bare_log_close();
	bare_pcap_close();
	bare_cfg_deep_free(&g_bare);
	memset(&g_bare, 0, sizeof(g_bare));
	mtx_unlock(&g_bare.lock);
	return err ? err : LIBBARE_ERR_STATE;
}

/* ── libbare_shutdown ────────────────────────────────────────────────────── */

void libbare_shutdown(void)
{
	mtx_lock(&g_bare.lock);
	if (!g_bare.initialized) {
		mtx_unlock(&g_bare.lock);
		return;
	}

	bare_event_close();

	bare_re_loop_stop();

	bare_stats_close();
	bare_trace_close();
	bare_message_close();
	bare_presence_close();

	struct le *le, *le_tmp;
	LIST_FOREACH_SAFE(&g_bare.accounts, le, le_tmp) {
		struct libbare_account *acct = le->data;
		acct->destroyed = true;
		tmr_cancel(&acct->retry_tmr);
		if (acct->ua) {
			ua_hangup(acct->ua, NULL, 0, NULL);
			mem_deref(acct->ua);
			acct->ua = NULL;
		}
		bare_acct_cfg_deep_free(acct);
		list_unlink(&acct->le);
		mem_deref(acct);
	}

	ua_close();
	module_app_unload();
	bare_dns_close();
	baresip_close();
	libre_close();
	bare_log_close();
	bare_pcap_close();
	bare_cfg_deep_free(&g_bare);

	bare_call_global_reset();
	bare_tap_global_reset();

	g_bare.initialized = false;
	mtx_unlock(&g_bare.lock);
}

/* ── libbare_version ─────────────────────────────────────────────────────── */

const char *libbare_version(void)
{
	return "1.0.0";
}
