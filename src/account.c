/**
 * @file account.c  Multi-account registration FSM
 *
 * libbare_account_t wraps a baresip ua. One UA per account.
 * Registration retry uses exponential backoff with jitter.
 * All string fields in the account config are deep-copied.
 */

#include <math.h>
#include "libbare_internal.h"

/* ── Deep-copy helpers for account config ───────────────────────────────── */

void bare_acct_cfg_deep_copy(libbare_account_config_t *dst,
                              const libbare_account_config_t *src,
                              struct libbare_account *acct)
{
	*dst = *src;
	acct->cfg_aor         = bare_strdup(src->aor);
	acct->cfg_auth_user   = bare_strdup(src->auth_user);
	acct->cfg_auth_pass   = bare_strdup(src->auth_pass);
	acct->cfg_display_name = bare_strdup(src->display_name);
	acct->cfg_outbound    = bare_strdup(src->outbound);

	dst->aor          = acct->cfg_aor;
	dst->auth_user    = acct->cfg_auth_user;
	dst->auth_pass    = acct->cfg_auth_pass;
	dst->display_name = acct->cfg_display_name;
	dst->outbound     = acct->cfg_outbound;
}

void bare_acct_cfg_deep_free(struct libbare_account *acct)
{
	mem_deref(acct->cfg_aor);         acct->cfg_aor = NULL;
	mem_deref(acct->cfg_auth_user);   acct->cfg_auth_user = NULL;
	mem_deref(acct->cfg_auth_pass);   acct->cfg_auth_pass = NULL;
	mem_deref(acct->cfg_display_name); acct->cfg_display_name = NULL;
	mem_deref(acct->cfg_outbound);    acct->cfg_outbound = NULL;
}

/* ── Internal helpers ────────────────────────────────────────────────────── */

/* Build codec list string: "opus/48000/2,PCMU/8000/1" */
static void codec_list_str(const libbare_codec_t *codecs, int count,
                             char *buf, size_t sz)
{
	buf[0] = '\0';
	for (int i = 0; i < count && i < 8; i++) {
		const char *name;
		switch (codecs[i]) {
		case LIBBARE_CODEC_OPUS: name = "opus/48000/2";  break;
		case LIBBARE_CODEC_PCMU: name = "PCMU/8000/1";   break;
		case LIBBARE_CODEC_PCMA: name = "PCMA/8000/1";   break;
		case LIBBARE_CODEC_G722: name = "G722/8000/1";   break;
		default:                 name = NULL;             break;
		}
		if (!name) continue;
		if (buf[0]) strncat(buf, ",", sz - strlen(buf) - 1);
		strncat(buf, name, sz - strlen(buf) - 1);
	}
}

/* Configure a baresip account object from the global stack config */
static void configure_baresip_account(struct libbare_account *acct)
{
	struct account *ba = ua_account(acct->ua);
	const libbare_config_t *cfg = &g_bare.cfg;

	/* Auth */
	if (acct->cfg.auth_user)
		account_set_auth_user(ba, acct->cfg.auth_user);
	if (acct->cfg.auth_pass)
		account_set_auth_pass(ba, acct->cfg.auth_pass);
	if (acct->cfg.display_name)
		account_set_display_name(ba, acct->cfg.display_name);

	/* Registration interval */
	account_set_regint(ba, cfg->reg_expires);

	/* Outbound proxy */
	{
		char ob[512];
		const char *ob_str = acct->cfg.outbound ? acct->cfg.outbound
		                   : cfg->outbound_proxy ? cfg->outbound_proxy
		                   : NULL;
		if (ob_str) {
			account_set_outbound(ba, ob_str, 0);
		} else if (cfg->server_url || cfg->server_host) {
			bare_build_outbound(cfg, ob, sizeof(ob));
			account_set_outbound(ba, ob, 0);
		}
	}

	/* Media encryption */
	{
		const char *menc = bare_mediaenc_str(cfg->media_enc);
		if (menc)
			account_set_mediaenc(ba, menc);
	}

	/* ICE / NAT */
	if (cfg->ice_enabled)
		account_set_medianat(ba, "ice");

	/* STUN */
	if (cfg->stun_server)
		account_set_stun_uri(ba, cfg->stun_server);
	if (cfg->turn_user)
		account_set_stun_user(ba, cfg->turn_user);
	if (cfg->turn_pass)
		account_set_stun_pass(ba, cfg->turn_pass);

	/* Audio codecs */
	if (cfg->audio_codec_count > 0) {
		char codecs[256];
		codec_list_str(cfg->audio_codecs, cfg->audio_codec_count,
		               codecs, sizeof(codecs));
		if (codecs[0])
			account_set_audio_codecs(ba, codecs);
	}

	/* DTMF via RTP events */
	account_set_dtmfmode(ba, DTMFMODE_RTP_EVENT);

	/* Call transfer support */
	account_set_call_transfer(ba, true);
}

/* ── Retry timer (fires on re_main) ─────────────────────────────────────── */

static void retry_timer_handler(void *arg)
{
	struct libbare_account *acct = arg;
	if (acct->destroyed || !acct->ua)
		return;
	info("libbare: re-registering account (attempt %u)\n",
	     acct->retry_attempt + 1);
	ua_register(acct->ua);
}

void bare_account_schedule_retry(struct libbare_account *acct)
{
	const libbare_config_t *cfg = &g_bare.cfg;

	if (cfg->reg_retry_max_attempts > 0 &&
	    acct->retry_attempt >= cfg->reg_retry_max_attempts) {
		info("libbare: max retry attempts reached for account\n");
		return;
	}

	uint32_t delay = cfg->reg_retry_initial_ms;
	for (uint32_t i = 0; i < acct->retry_attempt; i++) {
		delay = (uint32_t)((float)delay * cfg->reg_retry_backoff);
		if (delay >= cfg->reg_retry_max_ms) {
			delay = cfg->reg_retry_max_ms;
			break;
		}
	}

	acct->retry_attempt++;

	/* Post retry event so consumer can show UI */
	libbare_event_t ev = {0};
	ev.type                    = LIBBARE_EV_REG_STATE;
	ev.u.reg.state             = LIBBARE_REG_FAILED;
	ev.u.reg.error             = acct->reg_error;
	ev.u.reg.account           = acct;
	ev.u.reg.retry_attempt     = acct->retry_attempt;
	ev.u.reg.retry_delay_ms    = delay;
	bare_event_post(&ev);

	tmr_start(&acct->retry_tmr, delay, retry_timer_handler, acct);
}

/* ── account_destructor ──────────────────────────────────────────────────── */

static void account_destructor(void *data)
{
	struct libbare_account *acct = data;
	tmr_cancel(&acct->retry_tmr);
	struct le *le, *le_tmp;
	LIST_FOREACH_SAFE(&acct->custom_hdrs, le, le_tmp) {
		struct bare_custom_hdr *hdr = le->data;
		list_unlink(&hdr->le);
		mem_deref(hdr);
	}
	bare_acct_cfg_deep_free(acct);
}

/* ── bare_account_find_by_ua ─────────────────────────────────────────────── */

struct libbare_account *bare_account_find_by_ua(const struct ua *ua)
{
	struct le *le;
	mtx_lock(&g_bare.acct_lock);
	LIST_FOREACH(&g_bare.accounts, le) {
		struct libbare_account *acct = le->data;
		if (acct->ua == ua) {
			mtx_unlock(&g_bare.acct_lock);
			return acct;
		}
	}
	mtx_unlock(&g_bare.acct_lock);
	return NULL;
}

/* ── Public API — dispatch wrappers ─────────────────────────────────────── */

typedef struct {
	libbare_account_config_t  cfg;
	libbare_account_handle_t *out;
	int                       result;
} create_ctx_t;

static void create_fn(void *arg)
{
	create_ctx_t *ctx = arg;
	int err;

	struct libbare_account *acct = mem_alloc(sizeof(*acct), account_destructor);
	if (!acct) { ctx->result = ENOMEM; return; }
	memset(acct, 0, sizeof(*acct));
	tmr_init(&acct->retry_tmr);
	list_init(&acct->custom_hdrs);

	bare_acct_cfg_deep_copy(&acct->cfg, &ctx->cfg, acct);
	acct->reg_state = LIBBARE_REG_UNREGISTERED;

	if (!acct->cfg.aor) {
		mem_deref(acct);
		ctx->result = LIBBARE_ERR_INVAL;
		return;
	}

	char aor[512];
	const libbare_config_t *gcfg = &g_bare.cfg;
	re_snprintf(aor, sizeof(aor), "%s;transport=%s",
	            acct->cfg.aor, bare_transport_str(gcfg->transport));

	err = ua_alloc(&acct->ua, aor);
	if (err) { mem_deref(acct); ctx->result = err; return; }

	configure_baresip_account(acct);

	mtx_lock(&g_bare.acct_lock);
	list_append(&g_bare.accounts, &acct->le, acct);
	mtx_unlock(&g_bare.acct_lock);

	*ctx->out = acct;
	ctx->result = 0;
}

int libbare_account_create(const libbare_account_config_t *cfg,
                            libbare_account_handle_t *out)
{
	if (!cfg || !cfg->aor || !out)
		return LIBBARE_ERR_INVAL;

	create_ctx_t ctx = { .cfg = *cfg, .out = out, .result = 0 };
	int err = bare_dispatch_sync(create_fn, &ctx);
	return err ? err : ctx.result;
}

static void destroy_fn(void *arg)
{
	struct libbare_account *acct = arg;
	acct->destroyed = true;
	tmr_cancel(&acct->retry_tmr);
	if (acct->ua) {
		ua_hangup(acct->ua, NULL, 0, NULL);
		mem_deref(acct->ua);
		acct->ua = NULL;
	}
	mtx_lock(&g_bare.acct_lock);
	list_unlink(&acct->le);
	mtx_unlock(&g_bare.acct_lock);
	mem_deref(acct);
}

void libbare_account_destroy(libbare_account_handle_t acct)
{
	if (!acct) return;
	bare_dispatch(destroy_fn, acct);
}

static void register_fn(void *arg)
{
	struct libbare_account *acct = arg;
	if (acct->ua)
		ua_register(acct->ua);
}

int libbare_account_register(libbare_account_handle_t acct)
{
	if (!acct) return LIBBARE_ERR_INVAL;
	return bare_dispatch(register_fn, acct);
}

static void unregister_fn(void *arg)
{
	struct libbare_account *acct = arg;
	if (acct->ua) {
		acct->reg_state = LIBBARE_REG_UNREGISTERING;
		ua_unregister(acct->ua);
	}
}

int libbare_account_unregister(libbare_account_handle_t acct)
{
	if (!acct) return LIBBARE_ERR_INVAL;
	return bare_dispatch(unregister_fn, acct);
}
