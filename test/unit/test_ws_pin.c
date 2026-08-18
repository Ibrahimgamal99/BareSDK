/**
 * @file test_ws_pin.c  Unit tests for WebSocket connection pinning (ws_path.c)
 *
 * Links the real ws_path.c (compiled into this binary) against libre, stubs
 * __real_websock_connect to capture the URI the wrapper decided on, and drives
 * the registry the way account.c does — one set() per account create, one
 * unset() per destroy.
 *
 * What is worth pinning down here, because getting it wrong is silent and only
 * shows up as calls that die ~32 s in:
 *   - one server  → pinned, and a loopback Record-Route is redirected to it,
 *   - two servers → not pinned (never route one account to the other's server),
 *     but a target that cannot work is still redirected rather than dialled,
 *   - destroying the extra account brings pinning back (it used to latch off
 *     for the life of the process).
 *
 * Build: make test_ws_pin  (needs the linux-x86_64 sysroot from a host build)
 */

#include <stdio.h>
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "../../src/baresdk_internal.h"

/* ws_path.c reads g_bsdk.cfg for ws_origin / ws_extra_headers / keepalive;
 * core.c is not linked in, so own the definition here. */
struct bsdk_ctx g_bsdk;

static int g_pass, g_fail;

#define CHECK(cond, ...)                                                     \
	do {                                                                 \
		if (cond) { g_pass++; }                                      \
		else {                                                       \
			g_fail++;                                            \
			printf("  FAIL %s:%d: ", __func__, __LINE__);        \
			printf(__VA_ARGS__);                                 \
			printf("\n");                                        \
		}                                                            \
	} while (0)

/* ── Capture what the wrapper passes down ──────────────────────────────── */

static char g_used[512];

int __real_websock_connect(struct websock_conn **connp, struct websock *sock,
                           struct http_cli *cli, const char *uri,
                           unsigned kaint,
                           websock_estab_h *estabh, websock_recv_h *recvh,
                           websock_close_h *closeh, void *arg,
                           const char *fmt, ...);

int __real_websock_connect(struct websock_conn **connp, struct websock *sock,
                           struct http_cli *cli, const char *uri,
                           unsigned kaint,
                           websock_estab_h *estabh, websock_recv_h *recvh,
                           websock_close_h *closeh, void *arg,
                           const char *fmt, ...)
{
	(void)connp; (void)sock; (void)cli; (void)kaint;
	(void)estabh; (void)recvh; (void)closeh; (void)arg; (void)fmt;

	str_ncpy(g_used, uri ? uri : "(null)", sizeof(g_used));
	return 0;
}

int __wrap_websock_connect(struct websock_conn **connp, struct websock *sock,
                           struct http_cli *cli, const char *uri,
                           unsigned kaint,
                           websock_estab_h *estabh, websock_recv_h *recvh,
                           websock_close_h *closeh, void *arg,
                           const char *fmt, ...);

/* The URI the wrapper would actually connect to, for a libre-style call. */
static const char *connect_to(const char *uri)
{
	g_used[0] = '\0';
	__wrap_websock_connect(NULL, NULL, NULL, uri, 0, NULL, NULL, NULL,
	                       NULL, "Sec-WebSocket-Protocol: sip\r\n");
	return g_used;
}

/* ── Tests ─────────────────────────────────────────────────────────────── */

static void test_single_server(void)
{
	bsdk_ws_set_server(BARESDK_TRANSPORT_WSS, "pbx.example.com", 443,
	                   "/ws");

	CHECK(!strcmp(g_bsdk_ws_server, "wss://pbx.example.com:443"),
	      "pin is '%s'", g_bsdk_ws_server);
	CHECK(!strcmp(g_bsdk_ws_path, "/ws"), "path is '%s'", g_bsdk_ws_path);

	/* libre hands us the address it resolved from the dialog's
	 * Record-Route; behind a reverse proxy that is the server's loopback. */
	CHECK(!strcmp(connect_to("wss://127.0.0.1:8088/"),
	              "wss://pbx.example.com:443/ws"),
	      "loopback went to '%s'", g_used);

	/* The initial connect libre builds from the account's own address. */
	CHECK(!strcmp(connect_to("wss://82.129.158.253:443/"),
	              "wss://pbx.example.com:443/ws"),
	      "pinned connect went to '%s'", g_used);

	bsdk_ws_unset_server(BARESDK_TRANSPORT_WSS, "pbx.example.com", 443,
	                     "/ws");
	CHECK(!g_bsdk_ws_server[0], "pin survived destroy: '%s'",
	      g_bsdk_ws_server);
	CHECK(!g_bsdk_ws_path[0], "path survived destroy: '%s'",
	      g_bsdk_ws_path);
}

/* A second account on another server makes pinning ambiguous — and destroying
 * it must make it unambiguous again.  The regression: a mistyped account left
 * behind by the app disabled pinning for every later call in the process. */
static void test_stale_account_releases_pin(void)
{
	bsdk_ws_set_server(BARESDK_TRANSPORT_WSS, "typo.example.com", 443, "");
	bsdk_ws_set_server(BARESDK_TRANSPORT_WSS, "pbx.example.com", 443, "");

	CHECK(!g_bsdk_ws_server[0], "pinned to '%s' with two servers live",
	      g_bsdk_ws_server);

	/* Ambiguous: a real address is left alone, because sending it to the
	 * wrong server is worse than libre's routing. */
	CHECK(!strcmp(connect_to("wss://82.129.158.253:443/"),
	              "wss://82.129.158.253:443/"),
	      "real target rewritten to '%s'", g_used);

	/* A loopback target cannot work either way, so the newest server wins. */
	CHECK(!strcmp(connect_to("wss://127.0.0.1:8088/"),
	              "wss://pbx.example.com:443/"),
	      "dead target went to '%s'", g_used);

	bsdk_ws_unset_server(BARESDK_TRANSPORT_WSS, "typo.example.com", 443,
	                     "");
	CHECK(!strcmp(g_bsdk_ws_server, "wss://pbx.example.com:443"),
	      "pin after destroy is '%s'", g_bsdk_ws_server);

	bsdk_ws_unset_server(BARESDK_TRANSPORT_WSS, "pbx.example.com", 443, "");
}

/* Same server twice (two accounts on one PBX) is not a conflict. */
static void test_refcount_same_server(void)
{
	bsdk_ws_set_server(BARESDK_TRANSPORT_WSS, "pbx.example.com", 443,
	                   "/ws");
	bsdk_ws_set_server(BARESDK_TRANSPORT_WSS, "pbx.example.com", 443,
	                   "/ws");

	CHECK(!strcmp(g_bsdk_ws_server, "wss://pbx.example.com:443"),
	      "pin is '%s'", g_bsdk_ws_server);

	bsdk_ws_unset_server(BARESDK_TRANSPORT_WSS, "pbx.example.com", 443,
	                     "/ws");
	CHECK(!strcmp(g_bsdk_ws_server, "wss://pbx.example.com:443"),
	      "one destroy dropped the pin: '%s'", g_bsdk_ws_server);

	bsdk_ws_unset_server(BARESDK_TRANSPORT_WSS, "pbx.example.com", 443,
	                     "/ws");
	CHECK(!g_bsdk_ws_server[0], "pin is '%s' after both destroys",
	      g_bsdk_ws_server);
}

/* Host ambiguous, path agreed: substituting the path is still correct. */
static void test_path_survives_host_conflict(void)
{
	bsdk_ws_set_server(BARESDK_TRANSPORT_WSS, "a.example.com", 443, "/ws");
	bsdk_ws_set_server(BARESDK_TRANSPORT_WSS, "b.example.com", 443, "/ws");

	CHECK(!g_bsdk_ws_server[0], "pinned to '%s'", g_bsdk_ws_server);
	CHECK(!strcmp(g_bsdk_ws_path, "/ws"), "path is '%s'", g_bsdk_ws_path);
	CHECK(!strcmp(connect_to("wss://a.example.com:443/"),
	              "wss://a.example.com:443/ws"),
	      "path-only substitution gave '%s'", g_used);

	bsdk_ws_unset_server(BARESDK_TRANSPORT_WSS, "a.example.com", 443, "/ws");
	bsdk_ws_unset_server(BARESDK_TRANSPORT_WSS, "b.example.com", 443, "/ws");
}

/* Paths disagree: substituting either one would break the other's handshake. */
static void test_conflicting_paths_disable_substitution(void)
{
	bsdk_ws_set_server(BARESDK_TRANSPORT_WSS, "a.example.com", 443, "/ws");
	bsdk_ws_set_server(BARESDK_TRANSPORT_WSS, "b.example.com", 443, "/sip");

	CHECK(!g_bsdk_ws_path[0], "path is '%s' with two paths live",
	      g_bsdk_ws_path);
	CHECK(!strcmp(connect_to("wss://a.example.com:443/"),
	              "wss://a.example.com:443/"),
	      "path substituted anyway: '%s'", g_used);

	bsdk_ws_unset_server(BARESDK_TRANSPORT_WSS, "a.example.com", 443, "/ws");
	bsdk_ws_unset_server(BARESDK_TRANSPORT_WSS, "b.example.com", 443,
	                     "/sip");
}

/* A PBX genuinely on loopback (desktop against a local Asterisk) is ours, and
 * must not be "rescued" to somewhere else. */
static void test_own_loopback_server_untouched(void)
{
	bsdk_ws_set_server(BARESDK_TRANSPORT_WS, "127.0.0.1", 8088, "/ws");
	bsdk_ws_set_server(BARESDK_TRANSPORT_WSS, "pbx.example.com", 443, "");

	CHECK(!strcmp(connect_to("ws://127.0.0.1:8088/"),
	              "ws://127.0.0.1:8088/"),
	      "own loopback server rewritten to '%s'", g_used);

	bsdk_ws_unset_server(BARESDK_TRANSPORT_WS, "127.0.0.1", 8088, "/ws");
	bsdk_ws_unset_server(BARESDK_TRANSPORT_WSS, "pbx.example.com", 443, "");
}

/* Nothing registered (non-WS accounts only): pass everything through. */
static void test_no_servers_passthrough(void)
{
	CHECK(!strcmp(connect_to("wss://127.0.0.1:8088/"),
	              "wss://127.0.0.1:8088/"),
	      "rewrote '%s' with no servers registered", g_used);
}

int main(void)
{
	printf("ws pinning tests\n");

	test_single_server();
	test_stale_account_releases_pin();
	test_refcount_same_server();
	test_path_survives_host_conflict();
	test_conflicting_paths_disable_substitution();
	test_own_loopback_server_untouched();
	test_no_servers_passthrough();

	printf("%d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
