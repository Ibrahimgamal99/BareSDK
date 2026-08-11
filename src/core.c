/**
 * @file core.c  Singleton lifecycle — baresdk_init / baresdk_shutdown
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#endif
#include "baresdk_internal.h"

/* ── Global singleton ────────────────────────────────────────────────────── */

struct bsdk_ctx g_bsdk;

#define BARESDK_EV_QUEUE_MAX 4096

/* Cached result of getenv("BARESDK_DEBUG_INIT"); set on first call. */
int bsdk_trace_enabled(void)
{
	static int cached = -1;
	if (cached == -1) {
		const char *v = getenv("BARESDK_DEBUG_INIT");
		cached = (v && *v && *v != '0') ? 1 : 0;
	}
	return cached;
}

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
	ctx->cfg_tmp_dir          = bsdk_strdup(src->tmp_dir);

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
	dst->tmp_dir          = ctx->cfg_tmp_dir;
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
	mem_deref(ctx->cfg_tmp_dir);          ctx->cfg_tmp_dir   = NULL;
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
	cfg->rtcp_mux              = true;
	cfg->aec_mode              = BARESDK_AEC_SUPPRESSOR;
	cfg->aec_suppression_level = 1.0f;
	cfg->opus.complexity       = -1;
	/* mic_gain_db and speaker_gain_db default to 0.0f (unity) from memset */

	/* Network handover — poll as a safety net on platforms with no OS
	 * connectivity callback; mobile apps should set this to 0 and drive
	 * baresdk_network_changed() from ConnectivityManager / NWPathMonitor. */
	cfg->net_monitor_interval_s = 10;
	cfg->net_settle_ms          = 1500;
	cfg->net_reinvite_calls     = true;
	cfg->net_verify_ms          = 4000;
	cfg->net_max_attempts       = 6;
	/* net_hangup_on_migration_failure defaults to false from memset */
}

/* ── Platform temp-dir helper ────────────────────────────────────────────── */

static void bsdk_resolve_tmpdir(const char *override, char *buf, size_t sz)
{
	if (override && *override) {
		str_ncpy(buf, override, sz);
		return;
	}
#if defined(_WIN32)
	DWORD n = GetTempPath((DWORD)sz, buf);
	if (n > 0 && n < (DWORD)sz)
		return;
	str_ncpy(buf, "C:\\Temp", sz);
#else
	/* $TMPDIR is set by iOS for the app sandbox and may be set on Android */
	const char *t = getenv("TMPDIR");
	str_ncpy(buf, (t && *t) ? t : "/tmp", sz);
#endif
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

	BSDK_TRACE("[bsdk] step 1: deep_copy\n");
	bsdk_cfg_deep_copy(&g_bsdk.cfg, cfg, &g_bsdk);

	list_init(&g_bsdk.ev_queue);
	list_init(&g_bsdk.accounts);
	g_bsdk.ev_queue_max = BARESDK_EV_QUEUE_MAX;
	g_bsdk.ev_queue_len = 0;
	mtx_init(&g_bsdk.ev_lock, mtx_plain);
	cnd_init(&g_bsdk.ev_cond);
	mtx_init(&g_bsdk.acct_lock, mtx_plain);
	mtx_init(&g_bsdk.pcap_lock, mtx_plain);
	{
		const uint64_t *p = (const uint64_t*)&g_bsdk.pcap_lock;
		BSDK_TRACE("[bsdk] pcap_lock init: addr=%p bytes=%016llx %016llx %016llx %016llx %016llx\n",
		       (void*)p, p[0], p[1], p[2], p[3], p[4]);
	}
	bsdk_call_global_init();

	BSDK_TRACE("[bsdk] step 2: log_init\n");
	err = bsdk_log_init();
	if (err)
		goto fail;

	BSDK_TRACE("[bsdk] step 3: libre_init\n");
	err = libre_init();
	if (err)
		goto fail;

	BSDK_TRACE("[bsdk] step 4: conf_path\n");
	/* Redirect baresip's config directory so it never finds or reads
	 * ~/.config/baresip/{config,accounts,contacts,...} from disk.
	 * All SDK configuration is driven exclusively through baresdk_config_t.
	 * Directory must exist: the uuid module (required for WSS/outbound) writes into it. */
	{
		char _tmpbase[512];
		char _confdir[640];
		bsdk_resolve_tmpdir(g_bsdk.cfg.tmp_dir, _tmpbase, sizeof(_tmpbase));
		(void)re_snprintf(_confdir, sizeof(_confdir), "%s/.baresdk", _tmpbase);
		(void)fs_mkdir(_confdir, 0700);
		conf_path_set(_confdir);
	}
	conf_configure_buf((const uint8_t *)"#\n", 2);

	BSDK_TRACE("[bsdk] step 5: baresip_init (cfg=%p)\n", (void*)conf_config());
#ifdef _WIN32
	{
		DWORD _seh = 0;
		PVOID _crash_addr = NULL;
		__try { err = baresip_init(conf_config()); }
		__except ((_crash_addr = ((EXCEPTION_POINTERS*)GetExceptionInformation())->ExceptionRecord->ExceptionAddress),
		          EXCEPTION_EXECUTE_HANDLER) {
			_seh = GetExceptionCode();
			err = -1;
		}
		if (_seh) {
			HMODULE _hm = GetModuleHandleA("baresdk.dll");
			BSDK_TRACE("[bsdk] baresip_init SEH crash! code=0x%08lX at %p (RVA=0x%llX baresdk_base=%p)\n",
			       _seh, _crash_addr,
			       _hm ? (unsigned long long)((char*)_crash_addr - (char*)_hm) : 0,
			       (void*)_hm);
			goto fail;
		}
	}
#else
	err = baresip_init(conf_config());
#endif
	BSDK_TRACE("[bsdk] step 5 done: err=%d\n", err);
	if (err)
		goto fail;

	/* baresip_init tries to re-read the (missing) config path and may replace
	 * conf_cur() with NULL. Re-seed so modules_init never sees a NULL conf. */
	conf_configure_buf((const uint8_t *)"#\n", 2);

	conf_config()->call.accept = true;

	BSDK_TRACE("[bsdk] step 6: dns_init\n");
	err = bsdk_dns_init();
	if (err)
		goto fail;

	bsdk_timers_configure(&g_bsdk.cfg);

	/* Pre-configure the transport mask before ua_init.
	 * Bits: UDP=1<<0, TCP=1<<1, TLS=1<<2, WS=1<<3, WSS=1<<4 */
	conf_config()->sip.transports = (1u << SIP_TRANSP_UDP) |
	                                (1u << SIP_TRANSP_TCP) |
	                                (1u << SIP_TRANSP_TLS) |
	                                (1u << SIP_TRANSP_WS)  |
	                                (1u << SIP_TRANSP_WSS);

	BSDK_TRACE("[bsdk] step 7: ua_init\n");
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

	BSDK_TRACE("[bsdk] step 8: event_init\n");
	err = bsdk_event_init();
	if (err)
		goto fail;

	if (g_bsdk.cfg.trace_sip) {
		err = bsdk_trace_init();
		if (err)
			goto fail;
	}

	BSDK_TRACE("[bsdk] step 9: modules_init\n");
	err = modules_init();
	if (err)
		goto fail;
	BSDK_TRACE("[bsdk] step 9 done\n");

	/* Platform audio session setup (iOS AVAudioSession; no-op elsewhere).
	 * Non-fatal: a session category the OS refuses right now (e.g. during
	 * a CallKit-owned activation) still leaves the stack usable. */
	if (bsdk_platform_audio_init())
		warning("baresdk: platform audio init failed\n");

	{
		const baresdk_opus_config_t *op = &g_bsdk.cfg.opus;
		char obuf[256];
		int  olen = 0;
		if (op->bitrate > 0)
			olen += re_snprintf(obuf + olen, sizeof(obuf) - olen,
			                   "opus_bitrate %d\n", op->bitrate);
		if (op->complexity >= 0)
			olen += re_snprintf(obuf + olen, sizeof(obuf) - olen,
			                   "opus_complexity %d\n", op->complexity);
		if (op->cbr)
			olen += re_snprintf(obuf + olen, sizeof(obuf) - olen, "opus_cbr yes\n");
		if (op->dtx)
			olen += re_snprintf(obuf + olen, sizeof(obuf) - olen, "opus_dtx yes\n");
		if (op->fec)
			olen += re_snprintf(obuf + olen, sizeof(obuf) - olen, "opus_inbandfec yes\n");
		if (op->stereo)
			olen += re_snprintf(obuf + olen, sizeof(obuf) - olen, "opus_stereo yes\n");
		if (olen > 0)
			conf_configure_buf((const uint8_t *)obuf, (size_t)olen);
	}
	if (g_bsdk.cfg.jbuf_type == BARESDK_JBUF_FIXED) {
		struct config *c = conf_config();
		c->avt.audio.jbtype = JBUF_FIXED;
	}

	BSDK_TRACE("[bsdk] step 10: audio_processing_init\n");
	bsdk_audio_processing_init(g_bsdk.cfg.ns, g_bsdk.cfg.agc,
	                           g_bsdk.cfg.aec_mode,
	                           g_bsdk.cfg.aec_suppression_level,
	                           g_bsdk.cfg.mic_gain_db,
	                           g_bsdk.cfg.speaker_gain_db);
	bsdk_tap_global_init();

	BSDK_TRACE("[bsdk] step 11: message_init\n");
	err = bsdk_message_init();
	if (err)
		goto fail;

	BSDK_TRACE("[bsdk] step 12: presence_init\n");
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

	BSDK_TRACE("[bsdk] step 13: netmon_init\n");
	err = bsdk_netmon_init();
	if (err)
		goto fail;

	BSDK_TRACE("[bsdk] step 14: re_loop_start\n");
	err = bsdk_re_loop_start();
	if (err)
		goto fail;

	BSDK_TRACE("[bsdk] step 15: done\n");
	g_bsdk.initialized = true;
	mtx_unlock(&g_bsdk.lock);
	return BARESDK_OK;

fail:
	BSDK_TRACE("[bsdk] fail: cleanup start\n");
	warning("baresdk: init failed: %m\n", err);
	bsdk_re_loop_stop();
	bsdk_netmon_close();
	bsdk_stats_close();
	bsdk_trace_close();
	bsdk_event_close();
	ua_close();
	bsdk_call_global_reset();
	bsdk_dns_close();
#ifdef _WIN32
	{ __try { baresip_close(); } __except(EXCEPTION_EXECUTE_HANDLER) {
		BSDK_TRACE("[bsdk] baresip_close crash: 0x%08lX\n", GetExceptionCode()); } }
#else
	baresip_close();
#endif
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

	/* Stop the handover state machine before anything it touches (UAs,
	 * calls) is torn down.  Timers are cancelled after the re loop stops. */
	bsdk_netmon_stop();

	BSDK_TRACE("[bsdk] shutdown: event_close\n");
	bsdk_event_close();

	BSDK_TRACE("[bsdk] shutdown: stats/trace/msg/presence/audio\n");
	bsdk_stats_close();
	bsdk_trace_close();
	bsdk_message_close();
	bsdk_presence_close();
	bsdk_audio_processing_close();

	BSDK_TRACE("[bsdk] shutdown: account loop\n");
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

	BSDK_TRACE("[bsdk] shutdown: re_loop_stop\n");
	bsdk_re_loop_stop();

	/* After the loop has stopped so no handover timer can fire mid-teardown. */
	BSDK_TRACE("[bsdk] shutdown: netmon_close\n");
	bsdk_netmon_close();

	BSDK_TRACE("[bsdk] shutdown: ua_close\n");
	ua_close();
	BSDK_TRACE("[bsdk] shutdown: module_app_unload\n");
	module_app_unload();
#ifdef __ANDROID__
	/* After ua_close/module unload — all audio streams are gone, the
	 * OpenSLES engine can be destroyed. */
	bsdk_sles_vc_close();
#endif
	BSDK_TRACE("[bsdk] shutdown: dns_close\n");
	bsdk_dns_close();
	BSDK_TRACE("[bsdk] shutdown: baresip_close\n");
	baresip_close();
	BSDK_TRACE("[bsdk] shutdown: cfg_deep_free\n");
	bsdk_cfg_deep_free(&g_bsdk);
	BSDK_TRACE("[bsdk] shutdown: pcap_close\n");
	bsdk_pcap_close();
	BSDK_TRACE("[bsdk] shutdown: libre_close\n");
	libre_close();
	BSDK_TRACE("[bsdk] shutdown: log_close\n");
	bsdk_log_close();

	BSDK_TRACE("[bsdk] shutdown: call/tap reset\n");
	bsdk_call_global_reset();
	bsdk_tap_global_reset();

	BSDK_TRACE("[bsdk] shutdown: done\n");
	g_bsdk.initialized = false;
	mtx_unlock(&g_bsdk.lock);
}

/* ── baresdk_version ─────────────────────────────────────────────────────── */

const char *baresdk_version(void)
{
	return "1.0.0";
}
