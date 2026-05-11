/**
 * @file ws_path.c  WebSocket path workaround
 *
 * libre's transp.c hardcodes the WebSocket request path as "/" with no public
 * API to change it.  We intercept websock_connect() at link time via
 * -Wl,--wrap=websock_connect and substitute the configured path before the
 * HTTP upgrade request is sent.
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

	/* fmt is always "Sec-WebSocket-Protocol: sip\r\n" with no format args. */
	return __real_websock_connect(connp, sock, cli, use_uri, kaint,
	                              estabh, recvh, closeh, arg, fmt);
}
