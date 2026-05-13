/**
 * @file ws_path.c  WebSocket path workaround and header injection
 *
 * libre's transp.c hardcodes the WebSocket request path as "/" with no public
 * API to change it.  We intercept websock_connect() at link time via
 * -Wl,--wrap=websock_connect and substitute the configured path before the
 * HTTP upgrade request is sent.
 *
 * Additionally, we inject the configured Origin header and any extra
 * WebSocket headers (ws_origin / ws_extra_headers from baresdk_config_t),
 * and override the keepalive interval (ws_keepalive_ms).
 *
 * The shared library CMakeLists.txt adds -Wl,--wrap=websock_connect to the
 * baresdk.so link command.  The static archive is unaffected (no link step).
 */

#include <string.h>
#include "baresdk_internal.h"

/* Path set by account.c from the server_url when a WS/WSS account is created.
 * Empty means no substitution — libre's trailing "/" is passed through as-is. */
char g_bsdk_ws_path[256] = "";

/* Declarations supplied by the linker --wrap machinery. */
int __real_websock_connect(struct websock_conn **connp, struct websock *sock,
                            struct http_cli *cli, const char *uri,
                            unsigned kaint,
                            websock_estab_h *estabh, websock_recv_h *recvh,
                            websock_close_h *closeh, void *arg,
                            const char *fmt, ...);

int __wrap_websock_connect(struct websock_conn **connp, struct websock *sock,
                            struct http_cli *cli, const char *uri,
                            unsigned kaint,
                            websock_estab_h *estabh, websock_recv_h *recvh,
                            websock_close_h *closeh, void *arg,
                            const char *fmt, ...)
{
	char patched[512];
	const char *use_uri = uri;

	/* libre always passes "scheme://host:port/" — replace the trailing "/"
	 * with the configured path.  %b is libre's bounded-string specifier:
	 * re_snprintf does not support the standard %.*s precision syntax. */
	if (g_bsdk_ws_path[0] != '\0') {
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
