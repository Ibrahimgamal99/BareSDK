/**
 * @file core.c  Singleton lifecycle — baresdk_init / baresdk_shutdown
 */

#include <string.h>
#include "baresdk_internal.h"

/* ── Global singleton ────────────────────────────────────────────────────── */

struct bsdk_ctx g_bsdk;

#define BARESDK_EV_QUEUE_MAX 4096

/* ── Deep-copy helpers ───────────────────────────────────────────────────── */

char *bsdk_strdup(const char *s)
{
	if (!s) return NULL;
	size_t len = strlen(s);
	char *dup = mem_alloc(len + 1, NULL);
	if (dup) {
		memcpy(dup, s, len + 1);
	}
	return dup;
}

static char *bsdk_strdup_arr_elem(char **arr, const char *s)
{
	(void)arr;
	return bsdk_strdup(s);
}

static char **bsdk_strdup_strv(const char * const *src)
{
	if (!src) return NULL;
	size_t count = 0;
	while (src[count]) count++;
	char **dst = mem_alloc((count + 1) * sizeof(char *), NULL);
	if (!dst) return NULL;
	for (size_t i = 0; i < count; i++) {
		dst[i] = bsdk_strdup(src[i]);
		if (!dst[i]) {
			for (size_t j = 0; j < i; j++) mem_deref(dst[j]);
			mem_deref(dst);
			return NULL;
		}
	}
	dst[count] = NULL;
	return dst;
}

static void bsdk_free_strv(char **arr)
{
	if (!arr) return;
	for (size_t i = 0; arr[i]; i++)
		mem_deref(arr[i]);
	mem_deref(arr);
}

void bsdk_cfg_deep_copy(baresdk_config_t *dst, const baresdk_config_t *src,
                         struct bsdk_ctx *ctx)
{
	memcpy(dst, src, sizeof(*src));

	ctx->cfg_local_ip         = bsdk_strdup(src->local_ip);
	ctx->cfg_sip_domain       = bsdk_strdup(src->sip_domain);
	ctx->cfg_server_url       = bsdk_strdup(src->server_url);
	ctx->cfg_server_host      = bsdk_strdup(src->server_host);
	ctx->cfg_outbound_proxy   = bsdk_strdup(src->outbound_proxy);
	ctx->cfg_ca_cert_path     = bsdk_strdup(src->ca_cert_path);
	ctx->cfg_client_cert      = bsdk_strdup(src->client_cert);
	ctx->cfg_client_key       = bsdk_strdup(src->client_key);
	ctx->cfg_sni_hostname     = bsdk_strdup(src->sni_hostname);
	ctx->cfg_user_agent       = bsdk_strdup(src->user_agent);
	ctx->cfg_ws_origin        = bsdk_strdup(src->ws_origin);
	ctx->cfg_ws_extra_headers = bsdk_strdup_strv(src->ws_extra_headers);
	ctx->cfg_stun_server      = bsdk_strdup(src->stun_server);
	ctx->cfg_turn_server      = bsdk_strdup(src->turn_server);
	ctx->cfg_turn_user        = bsdk_strdup(src->turn_user);
	ctx->cfg_turn_pass        = bsdk_strdup(src->turn_pass);
	ctx->cfg_pcap_path        = bsdk_strdup(src->pcap_path);

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

void bsdk_cfg_deep_free(struct bsdk_ctx *ctx)
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
	bsdk_free_strv(ctx->cfg_ws_extra_headers); ctx->cfg_ws_extra_headers = NULL;
	mem_deref(ctx->cfg_stun_server);      ctx->cfg_stun_server = NULL;
	mem_deref(ctx->cfg_turn_server);      ctx->cfg_turn_server = NULL;
	mem_deref(ctx->cfg_turn_user);        ctx->cfg_turn_user = NULL;
	mem_deref(ctx->cfg_turn_pass);        ctx->cfg_turn_pass = NULL;
	mem_deref(ctx->cfg_pcap_path);        ctx->cfg_pcap_path = NULL;
}

/* ── baresdk_config_init ─────────────────────────────────────────────────── */

void baresdk_config_init(baresdk_config_t *cfg)
{
	if (!cfg)
		return;
	memset(cfg, 0, sizeof(*cfg));
	cfg->version     = BARESDK_CONFIG_VERSION;
	cfg->struct_size = sizeof(baresdk_config_t);

	cfg->transport             = BARESDK_TRANSPORT_UDP;
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
	cfg->mos_method            = BARESDK_MOS_EMODEL;
	cfg->log_level             = 1;
}

/* ── baresdk_init ────────────────────────────────────────────────────────── */

int baresdk_init(const baresdk_config_t *cfg)
{
	int err;

	if (!cfg || !cfg->event_cb)
		return BARESDK_ERR_INVAL;
	if (cfg->version != BARESDK_CONFIG_VERSION)
		return BARESDK_ERR_INVAL;
	if (cfg->struct_size != sizeof(baresdk_config_t))
		return BARESDK_ERR_INVAL;
	if (cfg->audio_codec_count < 0 || cfg->audio_codec_count > 8)
		return BARESDK_ERR_INVAL;

	static bool once = false;
	if (!once) {
		once = true;
		mtx_init(&g_bsdk.lock, mtx_plain);
	}

	mtx_lock(&g_bsdk.lock);

	if (g_bsdk.initialized) {
		mtx_unlock(&g_bsdk.lock);
		return BARESDK_ERR_ALREADY;
	}

	bsdk_cfg_deep_copy(&g_bsdk.cfg, cfg, &g_bsdk);

	list_init(&g_bsdk.ev_queue);
	list_init(&g_bsdk.accounts);
	g_bsdk.ev_queue_max = BARESDK_EV_QUEUE_MAX;
	g_bsdk.ev_queue_len = 0;
	mtx_init(&g_bsdk.ev_lock, mtx_plain);
	cnd_init(&g_bsdk.ev_cond);
	mtx_init(&g_bsdk.acct_lock, mtx_plain);
	mtx_init(&g_bsdk.pcap_lock, mtx_plain);
	bsdk_call_global_init();

	err = bsdk_log_init();
	if (err)
		goto fail;

	err = libre_init();
	if (err)
		goto fail;

	/* Redirect baresip's config directory to /tmp so it never finds or reads
	 * ~/.config/baresip/{config,accounts,contacts,...} from disk.
	 * All SDK configuration is driven exclusively through baresdk_config_t. */
	conf_path_set("/tmp/.baresdk-empty");
	conf_configure_buf((const uint8_t *)"#\n", 2);

	err = baresip_init(conf_config());
	if (err)
		goto fail;

	/* baresip_init tries to re-read the (missing) config path and may replace
	 * conf_cur() with NULL. Re-seed so modules_init never sees a NULL conf. */
	conf_configure_buf((const uint8_t *)"#\n", 2);

	conf_config()->call.accept = true;

	err = bsdk_dns_init();
	if (err)
		goto fail;

	bsdk_timers_configure(&g_bsdk.cfg);

	{
		const char *sw = g_bsdk.cfg.user_agent ? g_bsdk.cfg.user_agent
		                                       : "baresdk/1.0";
		err = ua_init(sw, true, true, true);
	}
	if (err)
		goto fail;

	{
		struct tls *tls = uag_tls();
		if (tls) {
			if (g_bsdk.cfg.ca_cert_path)
				tls_add_ca(tls, g_bsdk.cfg.ca_cert_path);
			if (!g_bsdk.cfg.verify_server)
				tls_disable_verify_server(tls);
		}
	}

	err = bsdk_event_init();
	if (err)
		goto fail;

	if (g_bsdk.cfg.trace_sip) {
		err = bsdk_trace_init();
		if (err)
			goto fail;
	}

	err = modules_init();
	if (err)
		goto fail;

	bsdk_audio_processing_init(g_bsdk.cfg.ns, g_bsdk.cfg.agc, g_bsdk.cfg.aec);

	err = bsdk_message_init();
	if (err)
		goto fail;

	err = bsdk_presence_init();
	if (err)
		goto fail;

	if (g_bsdk.cfg.pcap_path) {
		err = bsdk_pcap_open(g_bsdk.cfg.pcap_path);
		if (err)
			goto fail;
	}

	if (g_bsdk.cfg.stats_interval_ms > 0) {
		err = bsdk_stats_init();
		if (err)
			goto fail;
	}

	err = bsdk_re_loop_start();
	if (err)
		goto fail;

	g_bsdk.initialized = true;
	mtx_unlock(&g_bsdk.lock);
	return BARESDK_OK;

fail:
	warning("baresdk: init failed: %m\n", err);
	bsdk_re_loop_stop();
	bsdk_stats_close();
	bsdk_trace_close();
	bsdk_event_close();
	ua_close();
	bsdk_call_global_reset();
	bsdk_dns_close();
	baresip_close();
	libre_close();
	bsdk_log_close();
	bsdk_pcap_close();
	bsdk_cfg_deep_free(&g_bsdk);
	memset(&g_bsdk, 0, sizeof(g_bsdk));
	mtx_unlock(&g_bsdk.lock);
	return err ? err : BARESDK_ERR_STATE;
}

/* ── baresdk_shutdown ────────────────────────────────────────────────────── */

/* Runs on the re thread: hang up all calls and free the UA. Audio drivers
 * (PulseAudio, CoreAudio, WASAPI, …) must be torn down from the same thread
 * that opened the streams, which is the re thread. */
static void hangup_ua_fn(void *arg)
{
	struct ua **pua = arg;
	ua_hangup(*pua, NULL, 0, NULL);
	*pua = mem_deref(*pua);
}

void baresdk_shutdown(void)
{
	mtx_lock(&g_bsdk.lock);
	if (!g_bsdk.initialized) {
		mtx_unlock(&g_bsdk.lock);
		return;
	}

	bsdk_event_close();

	bsdk_stats_close();
	bsdk_trace_close();
	bsdk_message_close();
	bsdk_presence_close();
	bsdk_audio_processing_close();

	/* Hang up all active calls and free UAs on the re thread BEFORE stopping
	 * the event loop.  ua_hangup() triggers audio stream teardown; audio
	 * drivers interact with their own mainloops and must be called from the
	 * same thread that opened the streams. */
	struct le *le, *le_tmp;
	LIST_FOREACH_SAFE(&g_bsdk.accounts, le, le_tmp) {
		struct baresdk_account *acct = le->data;
		acct->destroyed = true;
		tmr_cancel(&acct->retry_tmr);
		if (acct->ua)
			bsdk_dispatch_sync(hangup_ua_fn, &acct->ua);
		bsdk_acct_cfg_deep_free(acct);
		list_unlink(&acct->le);
		mem_deref(acct);
	}

	bsdk_re_loop_stop();

	ua_close();
	module_app_unload();
	bsdk_dns_close();
	baresip_close();
	libre_close();
	bsdk_log_close();
	bsdk_pcap_close();
	bsdk_cfg_deep_free(&g_bsdk);

	bsdk_call_global_reset();
	bsdk_tap_global_reset();

	g_bsdk.initialized = false;
	mtx_unlock(&g_bsdk.lock);
}

/* ── baresdk_version ─────────────────────────────────────────────────────── */

const char *baresdk_version(void)
{
	return "1.0.0";
}
