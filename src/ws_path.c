/**
 * @file ws_path.c  WebSocket path workaround and header injection
 *
 * libre's transp.c hardcodes the WebSocket request path as "/" with no public
 * API to change it.  We intercept websock_connect() and substitute the
 * configured path before the HTTP upgrade request is sent.
 *
 * Additionally, we inject the configured Origin header and any extra
 * WebSocket headers (ws_origin / ws_extra_headers from baresdk_config_t),
 * and override the keepalive interval (ws_keepalive_ms).
 *
 * Linux: the shared-library link adds -Wl,--wrap=websock_connect, which
 *   rewrites callers to __wrap_websock_connect (defined here) and exposes the
 *   original as __real_websock_connect.
 *
 * Windows/MSVC and Apple (ld64) have no --wrap.  There the re build is
 *   passed -DRE_WEBSOCK_CONNECT_OVERRIDE=1, which makes libre's websock.c
 *   define __real_websock_connect instead of websock_connect.  Call sites
 *   (transp.c) still reference websock_connect; the linker resolves them to
 *   our wrapper below, which takes the public name on those platforms.
 *   No special linker flag required at any link step.
 */

#include <string.h>
#include "baresdk_internal.h"

/* Path set by account.c from the server_url when a WS/WSS account is created.
 * Empty means no substitution — libre's trailing "/" is passed through as-is. */
char g_bsdk_ws_path[256] = "";

/* Configured WebSocket server origin, e.g. "wss://pbx.example.com:443".
 * Empty disables pinning — see the RFC 7118 note in the wrapper below. */
char g_bsdk_ws_server[288] = "";

/* Set once a second account names a different server; pinning stays off for
 * the rest of the process even if that account is later destroyed. */
static bool ws_pin_conflict = false;

void bsdk_ws_set_server(baresdk_transport_t tp, const char *host, uint16_t port)
{
	char origin[288];
	bool ipv6;

	if (ws_pin_conflict || !host || !host[0] || !port)
		return;

	ipv6 = strchr(host, ':') != NULL;
	re_snprintf(origin, sizeof(origin), ipv6 ? "%s://[%s]:%u" : "%s://%s:%u",
	            tp == BARESDK_TRANSPORT_WSS ? "wss" : "ws", host, port);

	/* The wrapper sees only a URI — it cannot tell which account a connect
	 * belongs to.  With two accounts on different servers, pinning would
	 * silently route one account's signalling to the other's server, which
	 * is worse than the libre routing bug this works around.  Bail out and
	 * leave both accounts on stock behaviour. */
	if (g_bsdk_ws_server[0] && strcmp(g_bsdk_ws_server, origin) != 0) {
		g_bsdk_ws_server[0] = '\0';
		ws_pin_conflict = true;
		return;
	}

	str_ncpy(g_bsdk_ws_server, origin, sizeof(g_bsdk_ws_server));
}

/* Windows and Apple have no --wrap: re's websock.c is compiled with
 * RE_WEBSOCK_CONNECT_OVERRIDE (renaming the real function to
 * __real_websock_connect), so our wrapper takes the public name.
 * Linux keeps the linker wrap and our wrapper is __wrap_websock_connect. */
#if defined(_WIN32) || defined(__APPLE__)
#  define BSDK_WS_WRAP_NAME websock_connect
#else
#  define BSDK_WS_WRAP_NAME __wrap_websock_connect
#endif

int __real_websock_connect(struct websock_conn **connp, struct websock *sock,
                            struct http_cli *cli, const char *uri,
                            unsigned kaint,
                            websock_estab_h *estabh, websock_recv_h *recvh,
                            websock_close_h *closeh, void *arg,
                            const char *fmt, ...);

int BSDK_WS_WRAP_NAME(struct websock_conn **connp, struct websock *sock,
                       struct http_cli *cli, const char *uri,
                       unsigned kaint,
                       websock_estab_h *estabh, websock_recv_h *recvh,
                       websock_close_h *closeh, void *arg,
                       const char *fmt, ...)
{
	char patched[512];
	const char *use_uri = uri;

	if (g_bsdk_ws_server[0] != '\0') {
		/* Pin every outbound WebSocket to the configured server.
		 *
		 * RFC 7118 §B.2: a SIP WebSocket Client reaches its peers only
		 * through its WebSocket Server, so in-dialog requests must go back
		 * out over that flow.  libre instead resolves the Record-Route of
		 * the dialog and dials whatever address it holds.  Behind a reverse
		 * proxy the server Record-Routes its own internal address — an
		 * Asterisk behind nginx advertises "127.0.0.1:8088" — so libre opens
		 * a fresh connection to an unroutable address, the ACK for the 200
		 * OK never lands, and the dialog dies on the server's ~32 s timeout
		 * with media still flowing.  See baresip issues #807 and #859.
		 *
		 * Overriding the authority here also keeps the configured hostname
		 * in the TLS handshake: libre derives its URI from an already
		 * resolved sockaddr, so unpinned wss:// connects present an IP
		 * literal for SNI and certificate validation. */
		const char *path = g_bsdk_ws_path[0] ? g_bsdk_ws_path : "/";
		int r = re_snprintf(patched, sizeof(patched), "%s%s",
		                    g_bsdk_ws_server, path);
		if (r > 0)
			use_uri = patched;
	}
	else if (g_bsdk_ws_path[0] != '\0') {
		/* Not pinned — keep libre's authority and swap the path only.
		 * libre always passes "scheme://host:port/", so replace the trailing
		 * "/".  %b is libre's bounded-string specifier: re_snprintf does not
		 * support the standard %.*s precision syntax. */
		size_t n = strlen(uri);
		if (n > 0 && uri[n - 1] == '/') {
			int r = re_snprintf(patched, sizeof(patched), "%b%s",
			                    uri, (size_t)(n - 1), g_bsdk_ws_path);
			if (r > 0)
				use_uri = patched;
		}
	}

	/* Override keepalive if configured (0 = disabled, otherwise ms). */
	unsigned use_kaint = g_bsdk.cfg.ws_keepalive_ms
	                     ? g_bsdk.cfg.ws_keepalive_ms
	                     : kaint;

	/* Build extra headers string: Origin + ws_extra_headers.
	 * libre's http_request uses %v (va_list passthrough) for fmt, so we
	 * construct a new format string that appends our headers after the
	 * caller's original fmt content.  The caller always passes
	 * "Sec-WebSocket-Protocol: sip\r\n" with no format args. */
	char extra[1024];
	extra[0] = '\0';

	if (g_bsdk.cfg.ws_origin && g_bsdk.cfg.ws_origin[0])
		re_snprintf(extra + strlen(extra), sizeof(extra) - strlen(extra),
		            "Origin: %s\r\n", g_bsdk.cfg.ws_origin);

	if (g_bsdk.cfg.ws_extra_headers) {
		for (int i = 0; g_bsdk.cfg.ws_extra_headers[i]; i++)
			re_snprintf(extra + strlen(extra),
			            sizeof(extra) - strlen(extra),
			            "%s\r\n", g_bsdk.cfg.ws_extra_headers[i]);
	}

	if (extra[0]) {
		char new_fmt[2048];
		re_snprintf(new_fmt, sizeof(new_fmt), "%s%s", fmt, extra);
		return __real_websock_connect(connp, sock, cli, use_uri, use_kaint,
		                              estabh, recvh, closeh, arg,
		                              new_fmt);
	}

	/* No extra headers — forward with original fmt as-is.
	 * libre's transp.c always calls with
	 * "Sec-WebSocket-Protocol: sip\r\n" and zero format args,
	 * so no variadic forwarding is needed. */
	return __real_websock_connect(connp, sock, cli, use_uri, use_kaint,
	                              estabh, recvh, closeh, arg, fmt);
}
