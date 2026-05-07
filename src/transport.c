/**
 * @file transport.c  Server URL parsing and transport helpers
 *
 * Parses "wss://host:8089/ws", "sips://host", etc. into components that
 * baresip's account AOR and outbound proxy strings expect.
 */

#include <string.h>
#include <stdlib.h>
#include "baresdk_internal.h"

const char *bsdk_transport_str(baresdk_transport_t t)
{
	switch (t) {
	case BARESDK_TRANSPORT_UDP: return "udp";
	case BARESDK_TRANSPORT_TCP: return "tcp";
	case BARESDK_TRANSPORT_TLS: return "tls";
	case BARESDK_TRANSPORT_WS:  return "ws";
	case BARESDK_TRANSPORT_WSS: return "wss";
	default:                    return "udp";
	}
}

const char *bsdk_mediaenc_str(baresdk_media_enc_t enc)
{
	switch (enc) {
	case BARESDK_MEDIA_ENC_SDES:      return "srtp";
	case BARESDK_MEDIA_ENC_DTLS_SRTP: return "dtls_srtp";
	default:                          return NULL;
	}
}

/**
 * Parse a server URL into transport + host + port + path.
 *
 * Supported schemes and their default ports:
 *   sip:   → UDP, port 5060
 *   sips:  → TLS, port 5061
 *   ws:    → WS,  port 8088
 *   wss:   → WSS, port 8089
 *
 * The ;transport=XXX suffix on sip:/sips: URIs overrides the default
 * transport derived from the scheme. Examples:
 *
 *   "wss://pbx.example.com/ws"               → WSS, host=pbx.example.com, 8089, /ws
 *   "wss://pbx.example.com:443/ws"           → WSS, host=pbx.example.com,  443, /ws
 *   "ws://pbx.internal:8088/ws"              → WS,  host=pbx.internal,    8088, /ws
 *   "sip:pbx.example.com"                    → UDP, host=pbx.example.com,  5060, ""
 *   "sip:xpbx.site:443;transport=wss"        → WSS, host=xpbx.site,        443, ""
 *   "sip:xpbx.site;transport=tls"            → TLS, host=xpbx.site,       5061, ""
 */
int bsdk_parse_server_url(const char *url,
                           baresdk_transport_t *out_transport,
                           char *host, size_t host_sz,
                           uint16_t *port,
                           char *path, size_t path_sz)
{
	if (!url || !out_transport || !host || !port || !path)
		return EINVAL;

	host[0] = '\0';
	path[0] = '\0';
	*port   = 0;

	baresdk_transport_t transport;
	uint16_t default_port;
	const char *rest;

	if (strncmp(url, "wss://", 6) == 0) {
		transport    = BARESDK_TRANSPORT_WSS;
		default_port = 8089;
		rest         = url + 6;
	} else if (strncmp(url, "ws://", 5) == 0) {
		transport    = BARESDK_TRANSPORT_WS;
		default_port = 8088;
		rest         = url + 5;
	} else if (strncmp(url, "sips:", 5) == 0) {
		transport    = BARESDK_TRANSPORT_TLS;
		default_port = 5061;
		rest         = url + 5;
		if (rest[0] == '/' && rest[1] == '/') rest += 2;
	} else if (strncmp(url, "sip:", 4) == 0) {
		transport    = BARESDK_TRANSPORT_UDP;
		default_port = 5060;
		rest         = url + 4;
		if (rest[0] == '/' && rest[1] == '/') rest += 2;
	} else {
		transport    = BARESDK_TRANSPORT_UDP;
		default_port = 5060;
		rest         = url;
	}

	/* Extract ;transport=XXX override from the end of the URI */
	const char *tp = strstr(rest, ";transport=");
	if (tp) {
		const char *tv = tp + 11;
		if (strncasecmp(tv, "wss", 3) == 0) {
			transport    = BARESDK_TRANSPORT_WSS;
			default_port = 8089;
		} else if (strncasecmp(tv, "ws", 2) == 0) {
			transport    = BARESDK_TRANSPORT_WS;
			default_port = 8088;
		} else if (strncasecmp(tv, "tls", 3) == 0) {
			transport    = BARESDK_TRANSPORT_TLS;
			default_port = 5061;
		} else if (strncasecmp(tv, "tcp", 3) == 0) {
			transport    = BARESDK_TRANSPORT_TCP;
			default_port = 5060;
		} else if (strncasecmp(tv, "udp", 3) == 0) {
			transport    = BARESDK_TRANSPORT_UDP;
			default_port = 5060;
		}
	}

	*out_transport = transport;

	/* Authority is everything before '/' or ';' — whichever comes first */
	const char *slash = strchr(rest, '/');
	const char *semi  = strchr(rest, ';');
	size_t authority_len;
	if (slash && semi)
		authority_len = (size_t)(slash < semi ? slash : semi) - (size_t)(uintptr_t)rest;
	else if (slash)
		authority_len = (size_t)(slash - rest);
	else if (semi)
		authority_len = (size_t)(semi - rest);
	else
		authority_len = strlen(rest);

	if (slash && (!semi || slash < semi))
		str_ncpy(path, slash, path_sz);

	const char *colon = memchr(rest, ':', authority_len);
	if (colon) {
		size_t h_len = (size_t)(colon - rest);
		if (h_len >= host_sz) h_len = host_sz - 1;
		memcpy(host, rest, h_len);
		host[h_len] = '\0';
		char *end;
		*port = (uint16_t)strtoul(colon + 1, &end, 10);
		if (*port == 0)
			*port = default_port;
	} else {
		if (authority_len >= host_sz) authority_len = host_sz - 1;
		memcpy(host, rest, authority_len);
		host[authority_len] = '\0';
		*port = default_port;
	}

	return 0;
}

/**
 * Build the baresip outbound proxy string from baresdk config.
 *
 * For WS/WSS with a path:
 *   <sip:host:port;transport=wss>;ob;ws-path=/ws
 *
 * For UDP/TCP/TLS:
 *   <sip:host:port;transport=udp>;ob
 */
int bsdk_build_outbound(const baresdk_config_t *cfg,
                         char *buf, size_t buf_sz)
{
	char host[256] = {0};
	char path[256] = {0};
	uint16_t port  = 0;
	baresdk_transport_t transport = cfg->transport;

	if (cfg->server_url) {
		bsdk_parse_server_url(cfg->server_url, &transport,
		                      host, sizeof(host),
		                      &port, path, sizeof(path));
	} else {
		str_ncpy(host, cfg->server_host ? cfg->server_host : "",
		         sizeof(host));
		port = cfg->server_port;
		if (!port) {
			switch (cfg->transport) {
			case BARESDK_TRANSPORT_TLS: port = 5061; break;
			case BARESDK_TRANSPORT_WS:  port = 8088; break;
			case BARESDK_TRANSPORT_WSS: port = 8089; break;
			default:                    port = 5060; break;
			}
		}
	}

	if (path[0] && (transport == BARESDK_TRANSPORT_WS ||
	                transport == BARESDK_TRANSPORT_WSS)) {
		re_snprintf(buf, buf_sz, "<sip:%s:%u;transport=%s>;ob;ws-path=%s",
		            host, port, bsdk_transport_str(transport), path);
	} else {
		re_snprintf(buf, buf_sz, "<sip:%s:%u;transport=%s>;ob",
		            host, port, bsdk_transport_str(transport));
	}
	return 0;
}
