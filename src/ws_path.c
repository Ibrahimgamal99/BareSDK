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

#include <stdlib.h>
#include <string.h>
#include "baresdk_internal.h"

/* Path substituted into every outbound WS connect, derived from the live
 * accounts by ws_recompute().  Empty means no substitution — libre's trailing
 * "/" is passed through as-is. */
char g_bsdk_ws_path[256] = "";

/* WebSocket server origin to pin outbound connects to, e.g.
 * "wss://pbx.example.com:443".  Empty disables pinning — see the RFC 7118
 * note in the wrapper below. */
char g_bsdk_ws_server[288] = "";

/* ── WS server registry ─────────────────────────────────────────────────────
 *
 * One WS/WSS account is the common case, and pinning it is unambiguous.  Two
 * accounts on *different* servers are not: the wrapper sees only a URI, so it
 * cannot tell which account a connect belongs to, and pinning the wrong one
 * would route one account's signalling to the other's server — worse than the
 * libre bug being worked around.  So pinning switches off while the servers
 * are ambiguous.
 *
 * It must switch back on, though, and it used to latch off for the rest of the
 * process.  That is how one stale account silently killed every later call: an
 * app that creates a fresh account per login attempt (the Flutter example did)
 * leaves the mistyped one behind, its server counts as a second server
 * forever, and with pinning gone the in-dialog ACK follows the dialog's
 * Record-Route instead of the WebSocket flow.  Behind a reverse proxy that
 * Record-Route is the server's own loopback address — Asterisk advertises
 * "127.0.0.1:8088" — so the ACK is dialled to a port on the *phone*, never
 * arrives, and the server tears the dialog down ~32 s later with media still
 * flowing.
 *
 * Refcount the servers instead of latching, and recompute on every account
 * create/destroy: pinning returns as soon as one server is left.
 */

enum { BSDK_WS_MAX_SERVERS = 4 };

struct ws_server {
	char     origin[288];
	char     path[256];
	unsigned refs;
	uint64_t seq;      /* creation order; newest wins the loopback rescue */
};

static struct ws_server ws_srvv[BSDK_WS_MAX_SERVERS];
static uint64_t ws_srv_seq;

/* More distinct servers than the table holds.  The table is no longer a
 * complete picture of the process, so neither pinning nor the loopback rescue
 * can be trusted — stay out of the way entirely. */
static bool ws_srv_overflow;

static void ws_origin(char *buf, size_t sz, baresdk_transport_t tp,
                      const char *host, uint16_t port)
{
	bool ipv6 = strchr(host, ':') != NULL;

	re_snprintf(buf, sz, ipv6 ? "%s://[%s]:%u" : "%s://%s:%u",
	            tp == BARESDK_TRANSPORT_WSS ? "wss" : "ws", host, port);
}

static struct ws_server *ws_find(const char *origin, const char *path)
{
	for (size_t i = 0; i < RE_ARRAY_SIZE(ws_srvv); i++) {
		if (ws_srvv[i].refs &&
		    !strcmp(ws_srvv[i].origin, origin) &&
		    !strcmp(ws_srvv[i].path, path))
			return &ws_srvv[i];
	}
	return NULL;
}

/* Derive the pinned origin and path from the live entries.  The two are
 * decided independently: accounts can disagree on the host while sharing a
 * path (same PBX vendor, different tenants), and the path substitution is
 * still correct in that case. */
static void ws_recompute(void)
{
	const char *origin = NULL, *path = NULL;
	bool same_origin = true, same_path = true;
	unsigned n = 0;

	for (size_t i = 0; i < RE_ARRAY_SIZE(ws_srvv); i++) {
		const struct ws_server *e = &ws_srvv[i];
		if (!e->refs)
			continue;
		n++;
		if (!origin)
			origin = e->origin;
		else if (strcmp(origin, e->origin))
			same_origin = false;
		if (!path)
			path = e->path;
		else if (strcmp(path, e->path))
			same_path = false;
	}

	str_ncpy(g_bsdk_ws_server,
	         (n && same_origin && !ws_srv_overflow) ? origin : "",
	         sizeof(g_bsdk_ws_server));
	str_ncpy(g_bsdk_ws_path, (n && same_path) ? path : "",
	         sizeof(g_bsdk_ws_path));

	/* Every WS account is gone — the entries we lost to overflow are gone
	 * with them, so the table is trustworthy again. */
	if (!n)
		ws_srv_overflow = false;
}

void bsdk_ws_set_server(baresdk_transport_t tp, const char *host,
                        uint16_t port, const char *path)
{
	char origin[288];
	struct ws_server *e, *free_slot = NULL;

	if (!host || !host[0] || !port)
		return;
	if (!path)
		path = "";

	ws_origin(origin, sizeof(origin), tp, host, port);

	e = ws_find(origin, path);
	if (e) {
		e->refs++;
		ws_recompute();
		return;
	}

	for (size_t i = 0; i < RE_ARRAY_SIZE(ws_srvv); i++) {
		if (!ws_srvv[i].refs) {
			free_slot = &ws_srvv[i];
			break;
		}
	}
	if (!free_slot) {
		warning("baresdk: more than %zu WebSocket servers in one "
		        "process; connection pinning disabled\n",
		        RE_ARRAY_SIZE(ws_srvv));
		ws_srv_overflow = true;
		ws_recompute();
		return;
	}

	str_ncpy(free_slot->origin, origin, sizeof(free_slot->origin));
	str_ncpy(free_slot->path, path, sizeof(free_slot->path));
	free_slot->refs = 1;
	free_slot->seq  = ++ws_srv_seq;

	ws_recompute();
}

void bsdk_ws_unset_server(baresdk_transport_t tp, const char *host,
                          uint16_t port, const char *path)
{
	char origin[288];
	struct ws_server *e;

	if (!host || !host[0] || !port)
		return;
	if (!path)
		path = "";

	ws_origin(origin, sizeof(origin), tp, host, port);

	e = ws_find(origin, path);
	if (!e)
		return;

	if (--e->refs == 0)
		memset(e, 0, sizeof(*e));

	ws_recompute();
}

/* Most recently registered server — the best available guess when the servers
 * are ambiguous but the alternative is a connect that cannot work. */
static const struct ws_server *ws_newest(void)
{
	const struct ws_server *best = NULL;

	if (ws_srv_overflow)
		return NULL;

	for (size_t i = 0; i < RE_ARRAY_SIZE(ws_srvv); i++) {
		if (ws_srvv[i].refs &&
		    (!best || ws_srvv[i].seq > best->seq))
			best = &ws_srvv[i];
	}
	return best;
}

/* Origin ("scheme://host:port") of a connect URI, without its path. */
static bool ws_uri_origin(const char *uri, char *buf, size_t sz)
{
	const char *p = strstr(uri, "://");
	const char *end;
	size_t len;

	if (!p)
		return false;

	end = strpbrk(p + 3, "/?");
	len = end ? (size_t)(end - uri) : strlen(uri);
	if (!len || len >= sz)
		return false;

	memcpy(buf, uri, len);
	buf[len] = '\0';
	return true;
}

/* True when this connect cannot possibly reach a server: the authority is a
 * loopback/unspecified address that belongs to no account of ours, i.e. an
 * address the peer advertised for itself and we resolved against our own
 * host.  A local PBX genuinely on 127.0.0.1 is one of ours and excluded. */
static bool ws_uri_is_dead(const char *uri)
{
	char origin[288];
	const char *host, *end;
	char hbuf[256];
	struct sa sa;
	size_t hlen;

	if (!ws_uri_origin(uri, origin, sizeof(origin)))
		return false;

	for (size_t i = 0; i < RE_ARRAY_SIZE(ws_srvv); i++) {
		if (ws_srvv[i].refs && !strcmp(ws_srvv[i].origin, origin))
			return false;   /* one of ours — leave it alone */
	}

	host = strstr(origin, "://");
	host += 3;

	if (*host == '[') {           /* IPv6 literal */
		host++;
		end = strchr(host, ']');
	}
	else {
		end = strrchr(host, ':');
	}
	hlen = end ? (size_t)(end - host) : strlen(host);
	if (!hlen || hlen >= sizeof(hbuf))
		return false;
	memcpy(hbuf, host, hlen);
	hbuf[hlen] = '\0';

	if (!str_casecmp(hbuf, "localhost"))
		return true;

	/* Not an IP literal — a hostname we cannot judge from here. */
	if (sa_set_str(&sa, hbuf, 0))
		return false;

	return sa_is_loopback(&sa) || sa_is_any(&sa);
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
	else {
		/* Not pinned: several WS servers are live (or none was recorded).
		 * Real targets are left alone — guessing which account a connect
		 * belongs to is worse than the libre bug above.
		 *
		 * A target that cannot work is the exception.  When the authority
		 * is a loopback address that belongs to no account of ours, the
		 * request is already lost: nothing on this device is listening
		 * there.  Dialling the most recent server is a guess, but it is a
		 * guess against a certainty, so take it and say so. */
		const struct ws_server *rescue =
			uri && ws_uri_is_dead(uri) ? ws_newest() : NULL;

		if (rescue) {
			const char *path = rescue->path[0] ? rescue->path : "/";
			int r = re_snprintf(patched, sizeof(patched), "%s%s",
			                    rescue->origin, path);
			if (r > 0) {
				warning("baresdk: WebSocket connect to '%s' cannot "
				        "reach a server; sending to '%s' instead "
				        "(several WS servers are configured — pin "
				        "one by destroying unused accounts)\n",
				        uri, patched);
				use_uri = patched;
			}
		}
		else if (g_bsdk_ws_path[0] != '\0') {
			/* Keep libre's authority and swap the path only.  libre
			 * always passes "scheme://host:port/", so replace the
			 * trailing "/".  %b is libre's bounded-string specifier:
			 * re_snprintf does not support the standard %.*s
			 * precision syntax. */
			size_t n = strlen(uri);
			if (n > 0 && uri[n - 1] == '/') {
				int r = re_snprintf(patched, sizeof(patched),
				                    "%b%s", uri,
				                    (size_t)(n - 1),
				                    g_bsdk_ws_path);
				if (r > 0)
					use_uri = patched;
			}
		}
	}

	debug("baresdk: ws connect '%s' -> '%s' (pin='%s' path='%s')\n",
	      uri ? uri : "(null)", use_uri ? use_uri : "(null)",
	      g_bsdk_ws_server, g_bsdk_ws_path);

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

/* ── In-dialog WebSocket connection reuse ───────────────────────────────────
 *
 * RFC 7118 says a WebSocket client has exactly one connection and every
 * request travels over it.  libre routes by address instead: for an in-dialog
 * request it resolves the dialog's Route/Contact and looks the connection up by
 * peer address (ws_conn_find), so a target that is not the registration peer
 * gets a brand-new WebSocket.
 *
 * Behind a reverse proxy that target is the server's own loopback address —
 * Asterisk advertises "127.0.0.1:8088" in Record-Route.  The wrapper above
 * rewrites the *URI* so the connection still lands on the right host, and
 * signalling therefore works, but it is a *second* connection, and the dialog
 * is bound to it.  When the call ends the server closes that socket, and libre
 * reports the close to baresip as ECONNRESET before the BYE it just answered
 * reaches the session layer — so a perfectly normal remote hangup surfaced as
 * BARESDK_CALL_FAILED with "Connection reset by peer [104]" instead of
 * BARESDK_CALL_ENDED.
 *
 * Fix it where libre makes the decision: rewrite a loopback destination back to
 * the address the registration is already connected to, so ws_conn_find()
 * matches and the existing connection is reused.  One connection, one lifetime,
 * and the BYE is the thing that closes the session.
 *
 * The peer address is learned rather than resolved: the first WS send of the
 * REGISTER goes to the real server, so remember that and use it.  No DNS here —
 * this runs on the re thread in the send path.
 *
 * Only rewritten while pinning is active (see ws_recompute()): with two
 * accounts on different servers there is no single right answer, and the same
 * reasoning that disables URI pinning applies here.
 */

/* Last known-good WS/WSS peer — the address the registration is connected to.
 * Written and read on the re thread only. */
static struct sa s_ws_peer;

/* The transport that peer is actually reached on.  Learned alongside the
 * address, because rewriting one without the other does not work — see the
 * transport note in __wrap_sip_transp_send(). */
static enum sip_transp s_ws_peer_tp = SIP_TRANSP_NONE;

/* ── In-dialog routing for WebSocket dialogs (RFC 7118 §B.2) ────────────────
 *
 * An incoming call arrives with no Record-Route and a Contact naming the PBX's
 * own internal address:
 *
 *     INVITE sip:100@127.0.0.1:60828;transport=WS
 *     Contact: <sip:asterisk@echo:5060;transport=ws>
 *
 * With an empty route set libre falls back to the remote target, so every
 * in-dialog request we send — BYE, re-INVITE — is addressed to host "echo" on
 * port 5060 over plain ws.  "echo" is the PBX's internal hostname and resolves
 * nowhere, 5060 is not our port, and ws is not our transport.  libre cannot
 * parse that as an address literal, falls into addr_lookup(), and the DNS query
 * fails *asynchronously* — long after sipsess_bye() already returned 0.
 *
 * The result was a hangup that reported success and never left the device: the
 * caller stayed connected until they gave up, and their BYE then arrived at a
 * dialog we had dismantled, so we answered 481 Call Does Not Exist.  The
 * re-INVITE that carries updated ICE candidates died the same way.
 *
 * RFC 7118 §B.2 is unambiguous: a SIP WebSocket Client reaches its peers only
 * through its WebSocket Server, so in-dialog requests belong on the flow the
 * registration already established — not at whatever address the Contact
 * happens to name.  Substituting the route here, before any resolution is
 * attempted, is what the transport rewrite further down cannot do: by the time
 * sip_transp_send() is reached, the request has already failed.
 *
 * Only WebSocket dialogs are touched.  A dialog whose route carries no
 * transport parameter, or a non-WS one, is left exactly as libre built it.
 */

struct sip_dialog;

static char       s_route_buf[352];
static struct uri s_route_uri;
static bool       s_route_ok;

/* Rebuild the substitute route from the pinned WS server ("wss://host:port"). */
static void ws_route_rebuild(void)
{
	const char *rest, *scheme, *colon;
	char host[256];
	unsigned port;
	struct pl pl;

	s_route_ok = false;

	if (!g_bsdk_ws_server[0])
		return;

	if (!strncmp(g_bsdk_ws_server, "wss://", 6)) {
		scheme = "wss";
		rest   = g_bsdk_ws_server + 6;
	}
	else if (!strncmp(g_bsdk_ws_server, "ws://", 5)) {
		scheme = "ws";
		rest   = g_bsdk_ws_server + 5;
	}
	else {
		return;
	}

	/* ws_origin() writes "scheme://host:port", bracketing IPv6 hosts, so the
	 * port is whatever follows the last colon outside the brackets. */
	colon = strrchr(rest, ':');
	if (!colon || colon == rest)
		return;

	if ((size_t)(colon - rest) >= sizeof(host))
		return;

	memcpy(host, rest, (size_t)(colon - rest));
	host[colon - rest] = '\0';
	port = (unsigned)strtoul(colon + 1, NULL, 10);
	if (!port)
		return;

	if (re_snprintf(s_route_buf, sizeof(s_route_buf),
	                "sip:%s:%u;transport=%s;lr", host, port, scheme) < 0)
		return;

	pl_set_str(&pl, s_route_buf);
	if (uri_decode(&s_route_uri, &pl))
		return;

	s_route_ok = true;
}

/* True when this dialog route belongs to a WebSocket flow. */
static bool ws_route_is_websocket(const struct uri *route)
{
	struct pl tp;

	if (!route || msg_param_decode(&route->params, "transport", &tp))
		return false;

	return !pl_strcasecmp(&tp, "ws") || !pl_strcasecmp(&tp, "wss");
}

/**
 * Decide the route a request should actually take.
 *
 * Split out from the wrapper so it can be tested directly: __real_* is a name
 * the linker owns under --wrap, so a test cannot stand in for it, and handing
 * libre's real accessor a synthetic dialog is not safe.
 */
const struct uri *bsdk_ws_route_override(const struct uri *route);

const struct uri *bsdk_ws_route_override(const struct uri *route)
{
	if (!route || !g_bsdk_ws_server[0] || !ws_route_is_websocket(route))
		return route;

	if (!s_route_ok || pl_strcmp(&s_route_uri.host, g_bsdk_ws_server)) {
		/* Cheap guard against a server change between calls; rebuilding
		 * is a snprintf and a parse. */
		ws_route_rebuild();
	}

	if (!s_route_ok)
		return route;

	if (!pl_cmp(&route->host, &s_route_uri.host) &&
	    route->port == s_route_uri.port)
		return route;   /* already the flow — nothing to do */

	debug("baresdk: ws in-dialog route %r:%u is not the registration flow;"
	      " routing over %r:%u instead (RFC 7118 B.2)\n",
	      &route->host, route->port,
	      &s_route_uri.host, s_route_uri.port);

	return &s_route_uri;
}

const struct uri *__real_sip_dialog_route(const struct sip_dialog *dlg);
const struct uri *__wrap_sip_dialog_route(const struct sip_dialog *dlg);

const struct uri *__wrap_sip_dialog_route(const struct sip_dialog *dlg)
{
	return bsdk_ws_route_override(__real_sip_dialog_route(dlg));
}

/* Both are private to libre (src/sip/sip.h), so mirror them here — the same
 * approach baresdk_internal.h already takes for baresip internals. */
struct sip_connqent;
typedef void(sip_transp_h)(int err, void *arg);

int __real_sip_transp_send(struct sip_connqent **qentp, struct sip *sip,
                           void *sock, enum sip_transp tp,
                           const struct sa *dst, char *host, struct mbuf *mb,
                           sip_conn_h *connh, sip_transp_h *transph,
                           void *arg);

int __wrap_sip_transp_send(struct sip_connqent **qentp, struct sip *sip,
                           void *sock, enum sip_transp tp,
                           const struct sa *dst, char *host, struct mbuf *mb,
                           sip_conn_h *connh, sip_transp_h *transph,
                           void *arg);

int __wrap_sip_transp_send(struct sip_connqent **qentp, struct sip *sip,
                           void *sock, enum sip_transp tp,
                           const struct sa *dst, char *host, struct mbuf *mb,
                           sip_conn_h *connh, sip_transp_h *transph,
                           void *arg)
{
	struct sa fixed;

	if ((tp == SIP_TRANSP_WS || tp == SIP_TRANSP_WSS) && dst) {

		if (!sa_is_loopback(dst)) {
			/* The registration's own destination: remember it. */
			s_ws_peer    = *dst;
			s_ws_peer_tp = tp;
		}
		else if (g_bsdk_ws_server[0] && sa_isset(&s_ws_peer, SA_ALL)) {
			fixed = s_ws_peer;
			debug("baresdk: ws in-dialog target %J is loopback;"
			      " reusing the registration flow to %J\n",
			      dst, &fixed);
			dst = &fixed;

			/* Fix the transport too, or the address rewrite is
			 * pointless.
			 *
			 * A UAS dialog's route set is just the peer's
			 * Record-Route — unlike a UAC's, which starts with our
			 * own outbound proxy.  Asterisk behind a reverse proxy
			 * Record-Routes the leg *it* sees, which is plain WS on
			 * 127.0.0.1:8088, so every in-dialog request we send on
			 * an incoming call is built for SIP_TRANSP_WS while the
			 * only connection we hold is WSS.  libre looks a
			 * connection up by (transport, address), finds nothing,
			 * and tries to open a fresh ws:// to a TLS port.
			 *
			 * The request is accepted and then simply never arrives:
			 * sipsess_bye() returns 0, so hanging up an answered
			 * incoming call reported success while the caller stayed
			 * connected, and the ICE re-offer went the same way,
			 * leaving media dead.  RFC 7118 §B.2 is explicit that a
			 * WebSocket client reaches its peers through its one
			 * WebSocket Server, so following the registration flow
			 * is also the correct reading. */
			if (s_ws_peer_tp != SIP_TRANSP_NONE &&
			    s_ws_peer_tp != tp) {
				debug("baresdk: ws in-dialog transport %s does "
				      "not match the registration flow (%s); "
				      "using the flow\n",
				      sip_transp_name(tp),
				      sip_transp_name(s_ws_peer_tp));
				tp = s_ws_peer_tp;
			}
		}
		else if (g_bsdk_ws_server[0]) {
			/* Pinning is on but no peer learned yet — the URI
			 * rewrite in the wrapper above still gets the request
			 * delivered, just on its own connection. */
			warning("baresdk: ws in-dialog target %J is loopback and"
			        " no registration peer is known yet; a second"
			        " WebSocket will be opened\n", dst);
		}
	}

	return __real_sip_transp_send(qentp, sip, sock, tp, dst, host, mb,
	                              connh, transph, arg);
}
