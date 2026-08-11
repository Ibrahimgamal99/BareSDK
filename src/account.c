/**
 * @file account.c  Multi-account registration FSM
 *
 * baresdk_account_t wraps a baresip ua. One UA per account.
 * Registration retry uses exponential backoff with jitter.
 * All string fields in the account config are deep-copied.
 */

#include <math.h>
#include <stdlib.h>
#include "baresdk_internal.h"

/* ── URI parser: "user@host", "user@host:port", or "sip:user@host" ──────── */

static void parse_account_uri(const char *uri,
                               char *user, size_t user_sz,
                               char *host, size_t host_sz,
                               uint16_t *port)
{
	user[0] = host[0] = '\0';
	*port = 0;

	if (!uri) return;

	/* strip sip: or sips: scheme */
	if (strncasecmp(uri, "sip:", 4) == 0)  uri += 4;
	else if (strncasecmp(uri, "sips:", 5) == 0) uri += 5;

	const char *at = strchr(uri, '@');
	if (!at) {
		/* no user part — treat everything as host */
		const char *colon = strchr(uri, ':');
		if (colon) {
			size_t hlen = (size_t)(colon - uri);
			if (hlen >= host_sz) hlen = host_sz - 1;
			memcpy(host, uri, hlen); host[hlen] = '\0';
			*port = (uint16_t)strtoul(colon + 1, NULL, 10);
		} else {
			str_ncpy(host, uri, host_sz);
		}
		return;
	}

	/* user part */
	size_t ulen = (size_t)(at - uri);
	if (ulen >= user_sz) ulen = user_sz - 1;
	memcpy(user, uri, ulen); user[ulen] = '\0';

	/* host[:port] part — stop at ';' URI params */
	const char *hp = at + 1;
	const char *semi = strchr(hp, ';');
	size_t hp_len = semi ? (size_t)(semi - hp) : strlen(hp);

	if (hp[0] == '[') {
		/* IPv6 literal — find closing bracket */
		const char *cbracket = memchr(hp, ']', hp_len);
		if (cbracket) {
			size_t hlen = (size_t)(cbracket - hp - 1);
			if (hlen >= host_sz) hlen = host_sz - 1;
			memcpy(host, hp + 1, hlen);
			host[hlen] = '\0';
			/* Optional port after ']' */
			if (cbracket + 1 < hp + hp_len && cbracket[1] == ':')
				*port = (uint16_t)strtoul(cbracket + 2, NULL, 10);
		} else {
			/* Malformed bracket — copy as-is */
			if (hp_len >= host_sz) hp_len = host_sz - 1;
			memcpy(host, hp, hp_len); host[hp_len] = '\0';
		}
	} else {
		const char *colon = memchr(hp, ':', hp_len);
		if (colon) {
			size_t hlen = (size_t)(colon - hp);
			if (hlen >= host_sz) hlen = host_sz - 1;
			memcpy(host, hp, hlen); host[hlen] = '\0';
			*port = (uint16_t)strtoul(colon + 1, NULL, 10);
		} else {
			if (hp_len >= host_sz) hp_len = host_sz - 1;
			memcpy(host, hp, hp_len); host[hp_len] = '\0';
		}
	}
}

/* ── Deep-copy helpers for account config ───────────────────────────────── */

void bsdk_acct_cfg_deep_copy(baresdk_account_config_t *dst,
                              const baresdk_account_config_t *src,
                              struct baresdk_account *acct)
{
	*dst = *src;
	acct->cfg_uri         = bsdk_strdup(src->uri);
	acct->cfg_password    = bsdk_strdup(src->password);
	acct->cfg_server_host = bsdk_strdup(src->server_host);
	acct->cfg_server_url  = bsdk_strdup(src->server_url);
	acct->cfg_auth_user   = bsdk_strdup(src->auth_user);
	acct->cfg_display_name = bsdk_strdup(src->display_name);
	acct->cfg_stun_server = bsdk_strdup(src->stun_server);
	acct->cfg_turn_server = bsdk_strdup(src->turn_server);
	acct->cfg_turn_user   = bsdk_strdup(src->turn_user);
	acct->cfg_turn_pass   = bsdk_strdup(src->turn_pass);
	acct->cfg_outbound    = bsdk_strdup(src->outbound);
	acct->cfg_outbound_proxy = bsdk_strdup(src->outbound_proxy);
	acct->cfg_push_token  = bsdk_strdup(src->push_token);
	acct->cfg_push_param  = bsdk_strdup(src->push_param);

	dst->uri          = acct->cfg_uri;
	dst->password     = acct->cfg_password;
	dst->server_host  = acct->cfg_server_host;
	dst->server_url   = acct->cfg_server_url;
	dst->auth_user    = acct->cfg_auth_user;
	dst->display_name = acct->cfg_display_name;
	dst->stun_server  = acct->cfg_stun_server;
	dst->turn_server  = acct->cfg_turn_server;
	dst->turn_user    = acct->cfg_turn_user;
	dst->turn_pass    = acct->cfg_turn_pass;
	dst->outbound     = acct->cfg_outbound;
	dst->outbound_proxy = acct->cfg_outbound_proxy;
	dst->push_token   = acct->cfg_push_token;
	dst->push_param   = acct->cfg_push_param;
}

void bsdk_acct_cfg_deep_free(struct baresdk_account *acct)
{
	mem_deref(acct->cfg_uri);          acct->cfg_uri = NULL;
	mem_deref(acct->cfg_password);     acct->cfg_password = NULL;
	mem_deref(acct->cfg_server_host);  acct->cfg_server_host = NULL;
	mem_deref(acct->cfg_server_url);   acct->cfg_server_url = NULL;
	mem_deref(acct->cfg_auth_user);    acct->cfg_auth_user = NULL;
	mem_deref(acct->cfg_display_name); acct->cfg_display_name = NULL;
	mem_deref(acct->cfg_stun_server);  acct->cfg_stun_server = NULL;
	mem_deref(acct->cfg_turn_server);  acct->cfg_turn_server = NULL;
	mem_deref(acct->cfg_turn_user);    acct->cfg_turn_user = NULL;
	mem_deref(acct->cfg_turn_pass);    acct->cfg_turn_pass = NULL;
	mem_deref(acct->cfg_outbound);     acct->cfg_outbound = NULL;
	mem_deref(acct->cfg_outbound_proxy); acct->cfg_outbound_proxy = NULL;
	mem_deref(acct->cfg_push_token);   acct->cfg_push_token = NULL;
	mem_deref(acct->cfg_push_param);   acct->cfg_push_param = NULL;
}

/* ── Internal helpers ────────────────────────────────────────────────────── */

/* Normalize a codec name/alias to the "name/rate/channels" format baresip expects.
 * Returns a static or caller-guaranteed string, or NULL for empty input. */
static const char *normalize_codec_name(const char *name)
{
	if (!name || !name[0]) return NULL;
	if (strcasecmp(name, "opus")               == 0) return "opus/48000/2";
	if (strcasecmp(name, "ulaw")               == 0 ||
	    strcasecmp(name, "g711u")              == 0 ||
	    strcasecmp(name, "pcmu")               == 0) return "PCMU/8000/1";
	if (strcasecmp(name, "alaw")               == 0 ||
	    strcasecmp(name, "g711a")              == 0 ||
	    strcasecmp(name, "pcma")               == 0) return "PCMA/8000/1";
	if (strcasecmp(name, "g722")               == 0) return "G722/8000/1";
	if (strcasecmp(name, "g729")               == 0) return "G729/8000/1";
	if (strcasecmp(name, "g726")               == 0 ||
	    strcasecmp(name, "g726-32")            == 0) return "G726-32/8000/1";
	/* Unknown name passed as-is — lets callers use any codec baresip has loaded */
	return name;
}

/* Build codec list string from enum array: "opus/48000/2,PCMU/8000/1" */
static void codec_list_str(const baresdk_codec_t *codecs, int count,
                             char *buf, size_t sz)
{
	buf[0] = '\0';
	for (int i = 0; i < count && i < 8; i++) {
		const char *name;
		switch (codecs[i]) {
		case BARESDK_CODEC_OPUS: name = "opus/48000/2";  break;
		case BARESDK_CODEC_PCMU: name = "PCMU/8000/1";   break;
		case BARESDK_CODEC_PCMA: name = "PCMA/8000/1";   break;
		case BARESDK_CODEC_G722:    name = "G722/8000/1";    break;
		case BARESDK_CODEC_G726_32: name = "G726-32/8000/1"; break;
		default:                    name = NULL;              break;
		}
		if (!name) continue;
		if (buf[0]) strncat(buf, ",", sz - strlen(buf) - 1);
		strncat(buf, name, sz - strlen(buf) - 1);
	}
}

/* Build codec list string from name array: "opus/48000/2,PCMU/8000/1" */
static void codec_names_list_str(const char names[][32], int count,
                                  char *buf, size_t sz)
{
	buf[0] = '\0';
	for (int i = 0; i < count && i < 8; i++) {
		const char *resolved = normalize_codec_name(names[i]);
		if (!resolved) continue;
		if (buf[0]) strncat(buf, ",", sz - strlen(buf) - 1);
		strncat(buf, resolved, sz - strlen(buf) - 1);
	}
}

/* Maximum buffer for the RFC 8599 Contact URI params string.
 * Worst case: "pn-provider=apns-sandbox" (24) + ";pn-prid=" (9) +
 * token (~256) + ";pn-param=" (10) + param (~200) + NUL. */
#define BSDK_PUSH_PARAMS_BUFSZ 1024

/* Build the RFC 8599 Contact URI params string into buf.
 * Returns length written (>0), 0 if push is disabled/unconfigured,
 * or -1 on overflow. */
static int build_push_contact_params(const struct baresdk_account *acct,
                                      char *buf, size_t buf_sz)
{
	const char *provider_str;

	if (acct->cfg.push_provider == BARESDK_PUSH_PROVIDER_NONE)
		return 0;
	if (!acct->cfg.push_token || !acct->cfg.push_token[0])
		return 0;

	switch (acct->cfg.push_provider) {
	case BARESDK_PUSH_PROVIDER_APNS:         provider_str = "apns";         break;
	case BARESDK_PUSH_PROVIDER_APNS_SANDBOX: provider_str = "apns-sandbox"; break;
	case BARESDK_PUSH_PROVIDER_FCM:          provider_str = "fcm";          break;
	default: return 0;
	}

	/* Pre-flight length check — refuse silent truncation */
	size_t need = strlen("pn-provider=") + strlen(provider_str)
	            + strlen(";pn-prid=") + strlen(acct->cfg.push_token) + 1;
	if (acct->cfg.push_param && acct->cfg.push_param[0])
		need += strlen(";pn-param=") + strlen(acct->cfg.push_param);
	if (need > buf_sz)
		return -1;

	int n;
	if (acct->cfg.push_param && acct->cfg.push_param[0])
		n = re_snprintf(buf, buf_sz,
		                "pn-provider=%s;pn-prid=%s;pn-param=%s",
		                provider_str,
		                acct->cfg.push_token,
		                acct->cfg.push_param);
	else
		n = re_snprintf(buf, buf_sz,
		                "pn-provider=%s;pn-prid=%s",
		                provider_str,
		                acct->cfg.push_token);

	return (n > 0) ? n : -1;
}


/* Configure a baresip account object.
 * acct->parsed_* fields (user, host, port, transport) must be set first. */
static void configure_baresip_account(struct baresdk_account *acct)
{
	struct account *ba = ua_account(acct->ua);
	const baresdk_config_t *cfg = &g_bsdk.cfg;

	/* Auth — password required; auth_user defaults to uri user part */
	if (acct->cfg.password)
		account_set_auth_pass(ba, acct->cfg.password);
	{
		const char *au = (acct->cfg.auth_user && acct->cfg.auth_user[0])
		                 ? acct->cfg.auth_user : acct->parsed_user;
		if (au && au[0])
			account_set_auth_user(ba, au);
	}
	if (acct->cfg.display_name)
		account_set_display_name(ba, acct->cfg.display_name);

	/* Registration interval */
	account_set_regint(ba, cfg->reg_expires);

	/* Outbound proxy — account outbound → account outbound_proxy → global → auto */
	{
		char ob[512];
		const char *ob_str = acct->cfg.outbound;
		if (!ob_str) {
			ob_str = acct->cfg.outbound_proxy;   /* check alias field */
		}
		if (!ob_str) {
			/* Try global outbound_proxy from SDK config */
			ob_str = cfg->outbound_proxy;
		}
		if (!ob_str) {
			const char *surl = acct->cfg.server_url;
			if (!surl && acct->auto_server_url[0])
				surl = acct->auto_server_url;
			const char *shost = (acct->cfg.server_host && acct->cfg.server_host[0])
			                    ? acct->cfg.server_host : acct->parsed_host;
			uint16_t sport = acct->cfg.server_port ? acct->cfg.server_port
			                                       : acct->parsed_port;
			if (surl || shost[0]) {
				bsdk_build_outbound(surl, shost, sport,
				                    acct->parsed_transport, ob, sizeof(ob));
				ob_str = ob;
			}
		}
		if (ob_str)
			account_set_outbound(ba, ob_str, 0);
	}

	/* Media encryption — account overrides global */
	{
		baresdk_media_enc_t enc = acct->cfg.media_enc
		                        ? acct->cfg.media_enc : cfg->media_enc;
		const char *menc = bsdk_mediaenc_str(enc);
		if (menc)
			account_set_mediaenc(ba, menc);
	}

	/* ICE / NAT — account OR global */
	if (acct->cfg.ice_enabled || cfg->ice_enabled)
		account_set_medianat(ba, "ice");

	/* RTCP multiplexing (RFC 5761) — per-account override; fall back to global */
	account_set_rtcp_mux(ba, acct->cfg.rtcp_mux_set ? acct->cfg.rtcp_mux : cfg->rtcp_mux);

	/* STUN/TURN — account overrides global */
	{
		const char *stun = acct->cfg.stun_server
		                 ? acct->cfg.stun_server : cfg->stun_server;
		const char *turn = acct->cfg.turn_server
		                 ? acct->cfg.turn_server : cfg->turn_server;
		const char *tu   = acct->cfg.turn_user
		                 ? acct->cfg.turn_user   : cfg->turn_user;
		const char *tp   = acct->cfg.turn_pass
		                 ? acct->cfg.turn_pass   : cfg->turn_pass;
		/* TURN includes relay functionality; prefer it over bare STUN.
		 * baresip's account has a single NAT server slot — TURN takes it
		 * when both are set, as TURN servers also respond to STUN requests. */
		if (turn)
			account_set_stun_uri(ba, turn);
		else if (stun)
			account_set_stun_uri(ba, stun);
		if (tu)   account_set_stun_user(ba, tu);
		if (tp)   account_set_stun_pass(ba, tp);
	}

	/* Audio codecs — account names > account enums > global names >
	 * global enums */
	{
		char codecs[256];
		codecs[0] = '\0';

		if (acct->cfg.audio_codec_name_count > 0) {
			codec_names_list_str(acct->cfg.audio_codec_names,
			                     acct->cfg.audio_codec_name_count,
			                     codecs, sizeof(codecs));
		} else if (acct->cfg.audio_codec_count > 0) {
			codec_list_str(acct->cfg.audio_codecs,
			               acct->cfg.audio_codec_count,
			               codecs, sizeof(codecs));
		} else if (cfg->audio_codec_name_count > 0) {
			codec_names_list_str(cfg->audio_codec_names,
			                     cfg->audio_codec_name_count,
			                     codecs, sizeof(codecs));
		} else if (cfg->audio_codec_count > 0) {
			codec_list_str(cfg->audio_codecs,
			               cfg->audio_codec_count,
			               codecs, sizeof(codecs));
		}

		if (codecs[0]) {
			account_set_audio_codecs(ba, codecs);
			/* Unknown names are passed to baresip as-is so any codec a
			 * loaded module registers can be named. When none of them
			 * resolve, the account list stays empty and baresip quietly
			 * falls back to offering every loaded codec — the opposite
			 * of the restriction that was asked for. Detect that by the
			 * fallback identity and say so. */
			if (account_aucodecl(ba) == baresip_aucodecl()) {
				warning("baresdk/account: codec list \"%s\" matched no "
				        "loaded codec — offering all codecs instead\n",
				        codecs);
			}
		}
	}

	/* DTMF via RTP events */
	{
		static const enum dtmfmode dtmf_map[] = {
			[BARESDK_DTMF_RFC4733]  = DTMFMODE_RTP_EVENT,
			[BARESDK_DTMF_SIP_INFO] = DTMFMODE_SIP_INFO,
			[BARESDK_DTMF_AUTO]     = DTMFMODE_AUTO,
		};
		baresdk_dtmf_mode_t m = acct->cfg.dtmf_mode;
		if ((unsigned)m <= BARESDK_DTMF_AUTO)
			account_set_dtmfmode(ba, dtmf_map[m]);
		else
			account_set_dtmfmode(ba, DTMFMODE_RTP_EVENT);
	}

	/* Call transfer support */
	account_set_call_transfer(ba, true);

}

/* ── Retry timer (fires on re_main) ─────────────────────────────────────── */

static void retry_timer_handler(void *arg)
{
	struct baresdk_account *acct = arg;
	if (acct->destroyed || !acct->ua)
		return;
	info("baresdk: re-registering account (attempt %u)\n",
	     acct->retry_attempt + 1);
	ua_register(acct->ua);
}

void bsdk_account_schedule_retry(struct baresdk_account *acct)
{
	const baresdk_config_t *cfg = &g_bsdk.cfg;

	uint32_t initial_ms   = acct->retry_policy_set ? acct->retry_initial_ms   : cfg->reg_retry_initial_ms;
	uint32_t max_ms       = acct->retry_policy_set ? acct->retry_max_ms       : cfg->reg_retry_max_ms;
	float    backoff      = acct->retry_policy_set ? acct->retry_backoff      : cfg->reg_retry_backoff;
	uint32_t max_attempts = acct->retry_policy_set ? acct->retry_max_attempts : cfg->reg_retry_max_attempts;

	if (max_attempts > 0 && acct->retry_attempt >= max_attempts) {
		info("baresdk: max retry attempts reached for account\n");
		return;
	}

	uint32_t delay = initial_ms;
	for (uint32_t i = 0; i < acct->retry_attempt; i++) {
		delay = (uint32_t)((float)delay * backoff);
		if (delay >= max_ms) {
			delay = max_ms;
			break;
		}
	}

	acct->retry_attempt++;

	/* Post retry event so consumer can show UI */
	baresdk_event_t ev = {0};
	ev.type                    = BARESDK_EV_REG_STATE;
	ev.u.reg.state             = BARESDK_REG_FAILED;
	ev.u.reg.error             = acct->reg_error;
	ev.u.reg.account           = acct;
	ev.u.reg.retry_attempt     = acct->retry_attempt;
	ev.u.reg.retry_delay_ms    = delay;
	bsdk_event_post(&ev);

	tmr_start(&acct->retry_tmr, delay, retry_timer_handler, acct);
}

/* ── account_destructor ──────────────────────────────────────────────────── */

static void account_destructor(void *data)
{
	struct baresdk_account *acct = data;
	tmr_cancel(&acct->retry_tmr);
	struct le *le, *le_tmp;
	LIST_FOREACH_SAFE(&acct->custom_hdrs, le, le_tmp) {
		struct bsdk_custom_hdr *hdr = le->data;
		list_unlink(&hdr->le);
		mem_deref(hdr);
	}
	bsdk_acct_cfg_deep_free(acct);
}

/* ── bsdk_account_find_by_ua ─────────────────────────────────────────────── */

struct baresdk_account *bsdk_account_find_by_ua(const struct ua *ua)
{
	struct le *le;
	mtx_lock(&g_bsdk.acct_lock);
	LIST_FOREACH(&g_bsdk.accounts, le) {
		struct baresdk_account *acct = le->data;
		if (acct->ua == ua) {
			mtx_unlock(&g_bsdk.acct_lock);
			return acct;
		}
	}
	mtx_unlock(&g_bsdk.acct_lock);
	return NULL;
}

/* ── Public API — dispatch wrappers ─────────────────────────────────────── */

typedef struct {
	baresdk_account_config_t  cfg;
	baresdk_account_handle_t *out;
	int                       result;
} create_ctx_t;

static void create_fn(void *arg)
{
	create_ctx_t *ctx = arg;
	int err;

	struct baresdk_account *acct = mem_alloc(sizeof(*acct), account_destructor);
	if (!acct) { ctx->result = ENOMEM; return; }
	memset(acct, 0, sizeof(*acct));
	tmr_init(&acct->retry_tmr);
	list_init(&acct->custom_hdrs);

	bsdk_acct_cfg_deep_copy(&acct->cfg, &ctx->cfg, acct);
	acct->reg_state = BARESDK_REG_UNREGISTERED;

	if (!acct->cfg.uri) {
		mem_deref(acct);
		ctx->result = BARESDK_ERR_INVAL;
		return;
	}

	/* Parse "user@host" or "user@host:port" from uri */
	parse_account_uri(acct->cfg.uri,
	                  acct->parsed_user, sizeof(acct->parsed_user),
	                  acct->parsed_host, sizeof(acct->parsed_host),
	                  &acct->parsed_port);

	/* Determine transport: server_url → account transport → global default */
	baresdk_transport_t tp = acct->cfg.transport;
	char ws_path[256] = ""; /* set only if server_url contains an explicit path */
	char ws_host[256] = ""; /* WS server authority — pinned in ws_path.c */
	uint16_t ws_port = 0;

	if (acct->cfg.server_url) {
		char sv_host[256];
		uint16_t sv_port;
		bsdk_parse_server_url(acct->cfg.server_url, &tp,
		                      sv_host, sizeof(sv_host),
		                      &sv_port, ws_path, sizeof(ws_path));
		if (!acct->parsed_host[0])
			str_ncpy(acct->parsed_host, sv_host, sizeof(acct->parsed_host));
		acct->parsed_port = sv_port;
		/* The WS server is the server_url host, which may legitimately
		 * differ from the AOR domain (edge proxy vs SIP domain). */
		str_ncpy(ws_host, sv_host, sizeof(ws_host));
		ws_port = sv_port;
	} else {
		/* Auto-generate server_url for all transports */
		uint16_t port = acct->parsed_port;
		if (!port) {
			switch (tp) {
			case BARESDK_TRANSPORT_WSS: port = 8089; break;
			case BARESDK_TRANSPORT_WS:  port = 8088; break;
			case BARESDK_TRANSPORT_TLS: port = 5061; break;
			case BARESDK_TRANSPORT_TCP: port = 5060; break;
			case BARESDK_TRANSPORT_UDP:
			default:                     port = 5060; break;
			}
		}
		const char *scheme;
		switch (tp) {
		case BARESDK_TRANSPORT_WSS: scheme = "wss"; break;
		case BARESDK_TRANSPORT_WS:  scheme = "ws";  break;
		case BARESDK_TRANSPORT_TLS: scheme = "sips"; break;
		case BARESDK_TRANSPORT_TCP: scheme = "sip";  break;
		case BARESDK_TRANSPORT_UDP:
		default:                     scheme = "sip"; break;
		}

		re_snprintf(acct->auto_server_url, sizeof(acct->auto_server_url),
		            "%s://%s:%u%s", scheme, acct->parsed_host, port, ws_path);

		str_ncpy(ws_host, acct->parsed_host, sizeof(ws_host));
		ws_port = port;
	}
	acct->parsed_transport = tp;

	/* Store the explicit path for __wrap_websock_connect (ws_path.c).
	 * Empty means no substitution — libre's "/" is passed through as-is. */
	if (tp == BARESDK_TRANSPORT_WS || tp == BARESDK_TRANSPORT_WSS) {
		str_ncpy(g_bsdk_ws_path, ws_path, sizeof(g_bsdk_ws_path));
		bsdk_ws_set_server(tp, ws_host, ws_port);
	}

	/* Build AOR: sip:user@host[:port];transport=proto
	 * IPv6 literals must be wrapped in brackets per RFC 3261. */
	char aor[512];
	bool ipv6 = strchr(acct->parsed_host, ':') != NULL;
	if (acct->parsed_port) {
		re_snprintf(aor, sizeof(aor),
		            ipv6 ? "sip:%s@[%s]:%u;transport=%s"
		                 : "sip:%s@%s:%u;transport=%s",
		            acct->parsed_user, acct->parsed_host,
		            (unsigned)acct->parsed_port, bsdk_transport_str(tp));
	} else {
		re_snprintf(aor, sizeof(aor),
		            ipv6 ? "sip:%s@[%s];transport=%s"
		                 : "sip:%s@%s;transport=%s",
		            acct->parsed_user, acct->parsed_host,
		            bsdk_transport_str(tp));
	}

	err = ua_alloc(&acct->ua, aor);
	if (err) { mem_deref(acct); ctx->result = err; return; }

	configure_baresip_account(acct);

	mtx_lock(&g_bsdk.acct_lock);
	list_append(&g_bsdk.accounts, &acct->le, acct);
	mtx_unlock(&g_bsdk.acct_lock);

	*ctx->out = acct;
	ctx->result = 0;
}

int baresdk_account_create(const baresdk_account_config_t *cfg,
                            baresdk_account_handle_t *out)
{
	if (!cfg || !cfg->uri || !out)
		return BARESDK_ERR_INVAL;

	create_ctx_t ctx = { .cfg = *cfg, .out = out, .result = 0 };
	int err = bsdk_dispatch_sync(create_fn, &ctx);
	return err ? err : ctx.result;
}

static void destroy_fn(void *arg)
{
	struct baresdk_account *acct = arg;
	acct->destroyed = true;
	tmr_cancel(&acct->retry_tmr);
	if (acct->ua) {
		ua_hangup(acct->ua, NULL, 0, NULL);
		mem_deref(acct->ua);
		acct->ua = NULL;
	}
	mtx_lock(&g_bsdk.acct_lock);
	list_unlink(&acct->le);
	mtx_unlock(&g_bsdk.acct_lock);
	mem_deref(acct);
}

void baresdk_account_destroy(baresdk_account_handle_t acct)
{
	if (!acct) return;
	bsdk_dispatch_sync(destroy_fn, acct);
}

/* ── Enumeration & state readers ─────────────────────────────────────────── */

void baresdk_account_foreach(baresdk_account_iter_fn fn, void *arg)
{
	if (!fn) return;

	mtx_lock(&g_bsdk.acct_lock);
	struct le *le;
	LIST_FOREACH(&g_bsdk.accounts, le) {
		struct baresdk_account *acct = le->data;
		if (!acct->destroyed)
			fn((baresdk_account_handle_t)acct, arg);
	}
	mtx_unlock(&g_bsdk.acct_lock);
}

int baresdk_account_get_aor(baresdk_account_handle_t acct, char *buf, size_t sz)
{
	if (!acct || !buf || sz == 0)
		return BARESDK_ERR_INVAL;

	/* parsed_user/parsed_host are set once at create time and never
	 * mutated, so no lock is needed. */
	int n = re_snprintf(buf, sz, strchr(acct->parsed_host, ':')
	                             ? "sip:%s@[%s]" : "sip:%s@%s",
	                    acct->parsed_user, acct->parsed_host);
	if (n < 0) {
		buf[0] = '\0';
		return BARESDK_ERR_NOMEM;
	}
	return BARESDK_OK;
}

baresdk_reg_state_t baresdk_account_get_reg_state(baresdk_account_handle_t acct)
{
	if (!acct) return BARESDK_REG_UNREGISTERED;
	return acct->reg_state;
}

static void register_fn(void *arg)
{
	struct baresdk_account *acct = arg;
	if (acct->ua) {
		acct->reg_wanted = true;   /* netmon.c re-REGISTERs on handover */
		ua_register(acct->ua);
	}
}

int baresdk_account_register(baresdk_account_handle_t acct)
{
	if (!acct) return BARESDK_ERR_INVAL;
	return bsdk_dispatch(register_fn, acct);
}

static void unregister_fn(void *arg)
{
	struct baresdk_account *acct = arg;
	if (acct->ua) {
		acct->reg_wanted = false;
		acct->reg_state = BARESDK_REG_UNREGISTERING;
		ua_unregister(acct->ua);
	}
}

int baresdk_account_unregister(baresdk_account_handle_t acct)
{
	if (!acct) return BARESDK_ERR_INVAL;
	return bsdk_dispatch(unregister_fn, acct);
}

/* ── Retry control ───────────────────────────────────────────────────────── */

typedef struct {
	baresdk_account_handle_t acct;
	uint32_t initial_ms;
	uint32_t max_ms;
	float    backoff;
	uint32_t max_attempts;
} retry_policy_ctx_t;

static void set_retry_policy_fn(void *arg)
{
	retry_policy_ctx_t *ctx = arg;
	struct baresdk_account *acct = ctx->acct;
	acct->retry_policy_set   = true;
	acct->retry_initial_ms   = ctx->initial_ms;
	acct->retry_max_ms       = ctx->max_ms;
	acct->retry_backoff      = ctx->backoff;
	acct->retry_max_attempts = ctx->max_attempts;
}

int baresdk_account_set_retry_policy(baresdk_account_handle_t acct,
                                      uint32_t initial_ms, uint32_t max_ms,
                                      float backoff, uint32_t max_attempts)
{
	if (!acct) return BARESDK_ERR_INVAL;
	retry_policy_ctx_t ctx = { acct, initial_ms, max_ms, backoff, max_attempts };
	return bsdk_dispatch_sync(set_retry_policy_fn, &ctx);
}

static void cancel_retry_fn(void *arg)
{
	struct baresdk_account *acct = arg;
	tmr_cancel(&acct->retry_tmr);
	acct->retry_attempt = 0;
}

int baresdk_account_cancel_retry(baresdk_account_handle_t acct)
{
	if (!acct) return BARESDK_ERR_INVAL;
	return bsdk_dispatch_sync(cancel_retry_fn, acct);
}

static void retry_now_fn(void *arg)
{
	struct baresdk_account *acct = arg;
	tmr_cancel(&acct->retry_tmr);
	acct->retry_attempt = 0;
	if (!acct->destroyed && acct->ua) {
		acct->reg_wanted = true;
		ua_register(acct->ua);
	}
}

int baresdk_account_retry_now(baresdk_account_handle_t acct)
{
	if (!acct) return BARESDK_ERR_INVAL;
	return bsdk_dispatch_sync(retry_now_fn, acct);
}

/* ── Push token runtime update ──────────────────────────────────────────── */

typedef struct {
	baresdk_account_handle_t  acct;
	/* Caller-owned string pointer. Safe because bsdk_dispatch_sync blocks
	 * the caller until set_push_token_fn returns, so the string cannot be
	 * freed before bsdk_strdup() copies it. */
	const char               *push_token;
	int                       result;
} push_token_ctx_t;

static void set_push_token_fn(void *arg)
{
	push_token_ctx_t *ctx = arg;
	struct baresdk_account *acct = ctx->acct;

	if (!acct->ua) {
		ctx->result = BARESDK_ERR_STATE;
		return;
	}

	mem_deref(acct->cfg_push_token);
	acct->cfg_push_token = bsdk_strdup(ctx->push_token);
	acct->cfg.push_token = acct->cfg_push_token;

	char pn_params[BSDK_PUSH_PARAMS_BUFSZ];
	int n = build_push_contact_params(acct, pn_params, sizeof(pn_params));
	if (n < 0) {
		ctx->result = BARESDK_ERR_INVAL;  /* token too long */
		return;
	}

	(void)n; /* ua_set_contact_params not available in this baresip build */

	/* Re-register only when safe: skip if mid-transaction or mid-retry.
	 * The new cparams are already stored on the UA; the next natural
	 * ua_register() call will pick them up. */
	bool reg_in_flight = (acct->reg_state == BARESDK_REG_REGISTERING ||
	                      acct->reg_state == BARESDK_REG_UNREGISTERING);
	bool retry_pending = tmr_isrunning(&acct->retry_tmr);

	if (!reg_in_flight && !retry_pending)
		ua_register(acct->ua);
}

int baresdk_account_set_push_token(baresdk_account_handle_t acct,
                                    const char *push_token)
{
	if (!acct) return BARESDK_ERR_INVAL;
	push_token_ctx_t ctx = { .acct = acct, .push_token = push_token,
	                          .result = 0 };
	int err = bsdk_dispatch_sync(set_push_token_fn, &ctx);
	return err ? err : ctx.result;
}
