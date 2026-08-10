/**
 * @file netmon.c  Network handover (Wi-Fi ↔ 4G/5G roaming)
 *
 * When the device moves between networks the local IP changes underneath a
 * live SIP stack.  Three things break at once and all three are repaired here:
 *
 *   1. The listening SIP transports are bound to an address that no longer
 *      exists, and any TCP/TLS/WSS connection over it is a dead socket that
 *      will not fail fast — it stalls until Timer B (32 s).
 *   2. The registrar's binding points at the old Contact, so inbound calls
 *      are routed into the void until the next REGISTER refresh (up to
 *      reg_expires seconds away).
 *   3. Active calls advertise the old address in the SDP `c=` line, so the
 *      peer keeps sending RTP to an address we can no longer receive on.
 *
 * The repair sequence, all on the re_main thread:
 *
 *      detect → settle → [transports] → [REGISTER] → [re-INVITE] → verify
 *
 * Detection is either pushed by the app (baresdk_network_changed(), driven
 * from ConnectivityManager / NWPathMonitor) or pulled by a poll timer.  Both
 * feed the same debounce: an interface coming up produces a burst of address
 * changes, and acting on the first one wastes a full REGISTER round-trip.
 * We wait until the address set has been stable for cfg.net_settle_ms.
 *
 * Media is verified rather than assumed.  A re-INVITE that gets a 200 OK can
 * still leave audio dead (the peer's ACK crosses a NAT that has not rebound,
 * SDP was answered with the wrong direction, the new path is blocked).  After
 * each re-INVITE we sample the stream's RX packet counter and check it has
 * advanced cfg.net_verify_ms later; if it has not, we re-offer, and after
 * cfg.net_max_attempts we report failure (and optionally hang up).
 *
 * RTP sockets themselves are never re-created: baresip binds them to the
 * wildcard address (stream_sock_alloc → rtp_listen with an unset sa), so they
 * follow the new default route automatically.  Only the address advertised in
 * the SDP has to change, which is exactly what call_reset_transp() does.
 *
 * Known limitation: with ICE enabled the re-INVITE carries the old gathered
 * candidates — a full ICE restart (new ufrag/pwd + re-gather against STUN on
 * the new path) is not performed.  Direct RTP and SDES/DTLS-SRTP migrate
 * correctly; ICE calls may need to be re-established.
 */

#include "baresdk_internal.h"

/* Upper bound on accounts/calls processed per handover. Both lists are
 * snapshotted under their lock and walked afterwards, because migrating a
 * call re-enters baresip's event handler (which takes the same locks). */
#define BSDK_NET_MAX_SNAP    64

/* Bound on address add/remove passes per scan — a guard against spinning on
 * an address the kernel reports but baresip refuses to accept. */
#define BSDK_NET_SCAN_MAX    32

#define BSDK_NET_SETTLE_MS   1500
#define BSDK_NET_VERIFY_MS   4000
#define BSDK_NET_MAX_ATTEMPT 6

struct bsdk_netmon {
	struct tmr tmr_poll;    /* periodic interface scan                     */
	struct tmr tmr_settle;  /* debounce / retry of the handover itself     */
	struct tmr tmr_verify;  /* per-call migration progress tick            */

	bool     started;
	bool     reset_pending; /* addresses changed, handover still owed      */
	bool     force;         /* app asserted a change; run even if addrs eq */
	bool     announced;     /* CHANGE_DETECTED already emitted this round  */
	bool     down;          /* no usable local address                     */

	uint32_t gen;           /* handover generation; stamps per-call state  */
	uint32_t attempt;       /* consecutive failed handover attempts        */
	uint64_t handover_start;/* tmr_jiffies() when this round was detected  */

	uint32_t poll_s;
	uint32_t settle_ms;
	uint32_t verify_ms;
	uint32_t max_attempts;
	bool     reinvite_calls;
	bool     hangup_on_fail;

	char     cur_laddr[64];
};

static struct bsdk_netmon g_nm;

static void settle_handler(void *arg);
static void verify_handler(void *arg);
static void send_migration(struct baresdk_call *lc);

/* ── Events ──────────────────────────────────────────────────────────────── */

static baresdk_error_t map_err(int err)
{
	switch (err) {
	case 0:        return BARESDK_OK;
	case ENOMEM:   return BARESDK_ERR_NOMEM;
	case EINVAL:   return BARESDK_ERR_INVAL;
	case ETIMEDOUT: return BARESDK_ERR_TIMEOUT;
	default:       return BARESDK_ERR_TRANSPORT;
	}
}

/* Does this call negotiate ICE?  Media recovery is best-effort when it does
 * — see the `ice` field doc in baresdk.h. */
static bool call_uses_ice(const struct baresdk_call *lc)
{
	if (!lc || !lc->acct)
		return g_bsdk.cfg.ice_enabled;

	return lc->acct->cfg.ice_enabled || g_bsdk.cfg.ice_enabled;
}

static void netmon_emit(baresdk_net_event_t what,
                         struct baresdk_call *lc,
                         struct baresdk_account *acct,
                         int err, uint32_t attempt)
{
	struct baresdk_queued_event *qev = mem_alloc(sizeof(*qev), NULL);
	if (!qev)
		return;
	memset(qev, 0, sizeof(*qev));

	qev->ev.type                   = BARESDK_EV_NETWORK;
	qev->ev.u.network.event        = what;
	qev->ev.u.network.call         = lc;
	qev->ev.u.network.account      = acct;
	qev->ev.u.network.attempt      = attempt;
	qev->ev.u.network.max_attempts = g_nm.max_attempts;
	qev->ev.u.network.error        = map_err(err);

	if (lc) {
		qev->ev.u.network.ice = call_uses_ice(lc);
		if (lc->net_mig_start)
			qev->ev.u.network.elapsed_ms =
				(uint32_t)(tmr_jiffies() - lc->net_mig_start);
	}
	else if (g_nm.handover_start) {
		qev->ev.u.network.elapsed_ms =
			(uint32_t)(tmr_jiffies() - g_nm.handover_start);
	}

	str_ncpy(qev->buf, g_nm.cur_laddr, sizeof(qev->buf));
	qev->ev.u.network.local_addr = qev->buf;

	bsdk_event_post_qev(qev);   /* warns and frees qev when the queue is full */
}

/* ── Address-set scanning ────────────────────────────────────────────────── */

struct scan_ctx {
	struct sa sa;
	bool      found;
};

/* net_laddr_apply predicate: does baresip already know this address? */
static bool laddr_eq_h(const char *ifname, const struct sa *sa, void *arg)
{
	(void)ifname;
	return sa_cmp((const struct sa *)arg, sa, SA_ADDR);
}

/* net_if_apply predicate: an OS address baresip has not been told about. */
static bool if_missing_h(const char *ifname, const struct sa *sa, void *arg)
{
	struct scan_ctx *c = arg;
	struct network  *net = baresip_network();

	/* Honours cfg.net.ifname, use_linklocal and the AF enable flags, and
	 * drops loopback — the same filter baresip applies at startup. */
	if (!net_ifaddr_filter(net, ifname, sa))
		return false;

	if (net_laddr_apply(net, laddr_eq_h, (void *)sa))
		return false;

	sa_cpy(&c->sa, sa);
	c->found = true;
	return true;
}

/* net_laddr_apply predicate: an address baresip knows that is gone from the
 * box.  net_if_getname() answers ENODEV when no interface carries it. */
static bool laddr_gone_h(const char *ifname, const struct sa *sa, void *arg)
{
	struct scan_ctx *c = arg;
	char ifn[2] = "?";
	(void)ifname;

	if (net_if_getname(ifn, sizeof(ifn), sa_af(sa), sa) != ENODEV)
		return false;

	sa_cpy(&c->sa, sa);
	c->found = true;
	return true;
}

/* What kind of local addresses the box currently has.
 *
 * The two are not interchangeable: an interface that is administratively up
 * but has lost its lease still carries an IPv6 link-local address, which can
 * bind a socket but cannot reach an off-link registrar.  Binding decisions
 * use `linklocal || routable`; connectivity reporting uses `routable`. */
struct usable_ctx {
	bool routable;
	bool linklocal;
};

static bool if_usable_h(const char *ifname, const struct sa *sa, void *arg)
{
	struct usable_ctx *c = arg;

	if (!net_ifaddr_filter(baresip_network(), ifname, sa))
		return false;

	if (sa_is_linklocal(sa))
		c->linklocal = true;
	else
		c->routable = true;

	return c->routable;   /* nothing better to find — stop */
}

static void scan_usable(struct usable_ctx *c)
{
	memset(c, 0, sizeof(*c));
	(void)net_if_apply(if_usable_h, c);
}

/* True when a SIP server outside the local link is reachable at all. */
static bool have_routable_addr(void)
{
	struct usable_ctx c;
	scan_usable(&c);
	return c.routable;
}

/**
 * Reconcile baresip's local address list with the kernel's.
 * @return number of addresses added or removed.
 */
static unsigned scan_addresses(void)
{
	struct network *net = baresip_network();
	unsigned changed = 0;
	int i;

	for (i = 0; i < BSDK_NET_SCAN_MAX; i++) {
		struct scan_ctx c;
		memset(&c, 0, sizeof(c));
		(void)net_if_apply(if_missing_h, &c);
		if (!c.found)
			break;
		if (net_add_address(net, &c.sa))
			break;   /* would re-find the same address forever */
		changed++;
	}

	for (i = 0; i < BSDK_NET_SCAN_MAX; i++) {
		struct scan_ctx c;
		memset(&c, 0, sizeof(c));
		(void)net_laddr_apply(net, laddr_gone_h, &c);
		if (!c.found)
			break;
		if (net_rm_address(net, &c.sa))
			break;
		changed++;
	}

	return changed;
}

/* net_if_apply predicate: take the first address that passes baresip's filter. */
static bool if_first_h(const char *ifname, const struct sa *sa, void *arg)
{
	struct scan_ctx *c = arg;

	if (!net_ifaddr_filter(baresip_network(), ifname, sa))
		return false;

	sa_cpy(&c->sa, sa);
	c->found = true;
	return true;
}

static void update_cur_laddr(void)
{
	struct network  *net = baresip_network();
	const struct sa *sa;

	/* net_af() is AF_UNSPEC whenever both families are enabled, and
	 * net_laddr_af(AF_UNSPEC) matches nothing — try each family in turn. */
	sa = net_laddr_af(net, net_af(net));
	if (!sa || !sa_isset(sa, SA_ADDR))
		sa = net_laddr_af(net, g_bsdk.cfg.prefer_ipv6 ? AF_INET6 : AF_INET);
	if (!sa || !sa_isset(sa, SA_ADDR))
		sa = net_laddr_af(net, g_bsdk.cfg.prefer_ipv6 ? AF_INET : AF_INET6);

	if (sa && sa_isset(sa, SA_ADDR)) {
		(void)re_snprintf(g_nm.cur_laddr, sizeof(g_nm.cur_laddr), "%j", sa);
		return;
	}

	/* baresip's list can be empty before the first scan; fall back to the
	 * kernel's view so the reported address is never blank while we are up. */
	{
		struct scan_ctx c;
		memset(&c, 0, sizeof(c));
		(void)net_if_apply(if_first_h, &c);
		if (c.found)
			(void)re_snprintf(g_nm.cur_laddr, sizeof(g_nm.cur_laddr),
			                  "%j", &c.sa);
		else
			g_nm.cur_laddr[0] = '\0';
	}
}

/* ── Snapshots (see BSDK_NET_MAX_SNAP comment) ───────────────────────────── */

struct call_snap {
	struct baresdk_call **v;
	size_t                max;
	size_t                n;
};

static void call_snap_cb(struct baresdk_call *lc, void *arg)
{
	struct call_snap *s = arg;
	if (s->n < s->max)
		s->v[s->n++] = lc;
}

static size_t snapshot_calls(struct baresdk_call **v, size_t max)
{
	struct call_snap s = { .v = v, .max = max, .n = 0 };
	bsdk_call_foreach(call_snap_cb, &s);
	return s.n;
}

static size_t snapshot_accounts(struct baresdk_account **v, size_t max)
{
	struct le *le;
	size_t n = 0;

	mtx_lock(&g_bsdk.acct_lock);
	LIST_FOREACH(&g_bsdk.accounts, le) {
		struct baresdk_account *a = le->data;
		if (n >= max)
			break;
		if (a->destroyed || !a->ua || !a->reg_wanted)
			continue;
		v[n++] = a;
	}
	mtx_unlock(&g_bsdk.acct_lock);

	return n;
}

/* ── Per-call migration ──────────────────────────────────────────────────── */

static uint32_t rx_packets(const struct baresdk_call *lc)
{
	struct audio  *au;
	struct stream *strm;

	if (!lc->bc)
		return 0;

	au   = call_audio(lc->bc);
	strm = au ? audio_strm(au) : NULL;

	return strm ? stream_metric_get_rx_n_packets(strm) : 0;
}

static uint32_t verify_tick_ms(void)
{
	uint32_t ms = g_nm.verify_ms ? g_nm.verify_ms : 2000;
	return ms < 500 ? 500 : ms;
}

static void fail_migration(struct baresdk_call *lc, int err)
{
	lc->net_mig_state = BSDK_MIG_FAILED;
	netmon_emit(BARESDK_NET_CALL_MIGRATION_FAILED, lc, lc->acct, err,
	            lc->net_mig_tries);

	/* Leaving a call up with dead audio is usually worse than ending it,
	 * but that is the app's call to make. */
	if (g_nm.hangup_on_fail && lc->bc && lc->acct && lc->acct->ua)
		ua_hangup(lc->acct->ua, lc->bc, 0, NULL);
}

static bool migration_pending(void)
{
	struct baresdk_call *snap[BSDK_NET_MAX_SNAP];
	size_t n = snapshot_calls(snap, RE_ARRAY_SIZE(snap));

	for (size_t i = 0; i < n; i++) {
		if (snap[i]->net_mig_gen != g_nm.gen)
			continue;
		if (snap[i]->net_mig_state == BSDK_MIG_DEFERRED ||
		    snap[i]->net_mig_state == BSDK_MIG_SENT)
			return true;
	}
	return false;
}

static void arm_verify(void)
{
	if (migration_pending())
		tmr_start(&g_nm.tmr_verify, verify_tick_ms(), verify_handler, NULL);
}

/* Send (or re-send) the re-INVITE that moves this call's media. */
static void send_migration(struct baresdk_call *lc)
{
	int err;

	if (!lc->bc) {
		lc->net_mig_state = BSDK_MIG_IDLE;
		return;
	}

	/* An early dialog has no negotiated SDP to refresh, and a dialog with
	 * an offer already in flight may not send a second one (RFC 3261 §14.1).
	 * Both resolve themselves — wait and retry rather than tearing the call
	 * down, which is what baresip's own uag_reset_transp() would do here. */
	if (!call_refresh_allowed(lc->bc)) {
		if (lc->net_mig_state != BSDK_MIG_DEFERRED) {
			lc->net_mig_state = BSDK_MIG_DEFERRED;
			netmon_emit(BARESDK_NET_CALL_DEFERRED, lc, lc->acct, 0,
			            (uint32_t)lc->net_mig_tries + 1u);
		}
		return;
	}

	lc->net_rx_at_mig = rx_packets(lc);
	lc->net_mig_tries++;

	/* Rewrites the SDP session address and sends the re-INVITE.  A 491
	 * glare or a 401/407 challenge is retried by libre's sipsess layer. */
	err = call_reset_transp(lc->bc, &lc->net_mig_laddr);
	if (err) {
		if (lc->net_mig_tries >= g_nm.max_attempts)
			fail_migration(lc, err);
		else
			lc->net_mig_state = BSDK_MIG_DEFERRED;
		return;
	}

	lc->net_mig_state = BSDK_MIG_SENT;
	netmon_emit(BARESDK_NET_CALL_MIGRATING, lc, lc->acct, 0,
	            lc->net_mig_tries);

	if (!g_nm.verify_ms) {
		lc->net_mig_state = BSDK_MIG_DONE;
		netmon_emit(BARESDK_NET_CALL_MIGRATED, lc, lc->acct, 0,
		            lc->net_mig_tries);
	}
}

static void start_migration(struct baresdk_call *lc)
{
	struct stream   *strm;
	const struct sa *raddr, *old;
	struct sa        laddr;

	if (!lc->bc) {
		lc->net_mig_state = BSDK_MIG_IDLE;
		return;
	}

	strm = call_audio(lc->bc) ? audio_strm(call_audio(lc->bc)) : NULL;
	if (!strm)
		return;

	/* Prefer the address RTP is actually being sent to (learned from the
	 * peer) over the one in the SDP. */
	raddr = stream_raddr(strm);
	if (!raddr || !sa_isset(raddr, SA_ADDR))
		raddr = sdp_media_raddr(stream_sdpmedia(strm));
	if (!raddr || !sa_isset(raddr, SA_ADDR))
		return;   /* no peer media address yet — nothing to re-point */

	/* Ask the routing table which source address now reaches the peer. */
	if (net_dst_source_addr_get(raddr, &laddr))
		return;   /* no route yet; the next settle tick will retry */
	if (!sa_isset(&laddr, SA_ADDR))
		return;

	old = call_laddr(lc->bc);
	if (old && sa_cmp(&laddr, old, SA_ADDR)) {
		lc->net_mig_state = BSDK_MIG_IDLE;   /* same path — no re-INVITE */
		return;
	}

	sa_cpy(&lc->net_mig_laddr, &laddr);
	lc->net_mig_gen   = g_nm.gen;
	lc->net_mig_tries = 0;
	/* Clock the audio outage from the moment the network changed, not from
	 * the re-INVITE — the gap the user hears starts earlier. */
	lc->net_mig_start = g_nm.handover_start ? g_nm.handover_start
	                                        : tmr_jiffies();

	send_migration(lc);
}

static void migrate_calls(void)
{
	struct baresdk_call *snap[BSDK_NET_MAX_SNAP];
	size_t n = snapshot_calls(snap, RE_ARRAY_SIZE(snap));

	for (size_t i = 0; i < n; i++)
		start_migration(snap[i]);

	arm_verify();
}

/**
 * Progress tick for calls that are mid-migration.
 *
 * DEFERRED — the dialog was not refreshable; retry until it is.
 * SENT     — a re-INVITE went out; confirm RTP resumed, else re-offer.
 */
static void verify_handler(void *arg)
{
	struct baresdk_call *snap[BSDK_NET_MAX_SNAP];
	size_t n;
	(void)arg;

	if (!g_nm.started)
		return;

	n = snapshot_calls(snap, RE_ARRAY_SIZE(snap));

	for (size_t i = 0; i < n; i++) {
		struct baresdk_call *lc = snap[i];

		if (lc->net_mig_gen != g_nm.gen)
			continue;

		if (!lc->bc) {
			lc->net_mig_state = BSDK_MIG_IDLE;
			continue;
		}

		switch (lc->net_mig_state) {

		case BSDK_MIG_DEFERRED:
			if (call_refresh_allowed(lc->bc))
				send_migration(lc);
			else if (++lc->net_mig_tries >= g_nm.max_attempts)
				fail_migration(lc, ETIMEDOUT);
			break;

		case BSDK_MIG_SENT:
			/* A held call carries no RTP, so the counter can never
			 * advance — the answered re-INVITE is all the confirmation
			 * available. */
			if (lc->state == BARESDK_CALL_HELD ||
			    call_is_onhold(lc->bc) ||
			    rx_packets(lc) > lc->net_rx_at_mig) {
				lc->net_mig_state = BSDK_MIG_DONE;
				netmon_emit(BARESDK_NET_CALL_MIGRATED, lc, lc->acct,
				            0, lc->net_mig_tries);
			}
			else if (lc->net_mig_tries >= g_nm.max_attempts)
				fail_migration(lc, ETIMEDOUT);
			else
				send_migration(lc);   /* re-offer on the new path */
			break;

		default:
			break;
		}
	}

	arm_verify();
}

void bsdk_netmon_call_refreshable(struct baresdk_call *lc)
{
	if (!g_nm.started || !lc)
		return;
	if (lc->net_mig_state != BSDK_MIG_DEFERRED || lc->net_mig_gen != g_nm.gen)
		return;

	/* We are inside a baresip event emit right now; call_modify() would
	 * emit nested events from the same handler.  Bounce off the timer. */
	tmr_start(&g_nm.tmr_verify, 1, verify_handler, NULL);
}

void bsdk_netmon_call_sdp_answer(struct baresdk_call *lc)
{
	if (!g_nm.started || !lc)
		return;
	if (lc->net_mig_state != BSDK_MIG_SENT || lc->net_mig_gen != g_nm.gen)
		return;

	/* The peer took our new address.  Media has not necessarily resumed —
	 * that is what the verify tick is still watching for. */
	netmon_emit(BARESDK_NET_CALL_MIGRATE_ACCEPTED, lc, lc->acct, 0,
	            lc->net_mig_tries);
}

/* ── Registration refresh ────────────────────────────────────────────────── */

static void reregister_accounts(void)
{
	struct baresdk_account *snap[BSDK_NET_MAX_SNAP];
	size_t n = snapshot_accounts(snap, RE_ARRAY_SIZE(snap));

	for (size_t i = 0; i < n; i++) {
		struct baresdk_account *a = snap[i];

		/* A new network deserves a fresh attempt: an account sitting in a
		 * five-minute backoff from the old network must not wait it out. */
		tmr_cancel(&a->retry_tmr);
		a->retry_attempt = 0;

		netmon_emit(BARESDK_NET_REREGISTERING, NULL, a, 0, 0);
		(void)ua_register(a->ua);
	}
}

/* ── Handover state machine ──────────────────────────────────────────────── */

/* 1, 2, 4, 8, 16, 32 s */
static uint32_t backoff_ms(uint32_t attempt)
{
	uint32_t shift = attempt > 5 ? 5 : attempt;
	return 1000u << shift;
}

static void apply_handover(void)
{
	struct network   *net = baresip_network();
	struct usable_ctx use;
	int err;

	scan_usable(&use);

	/* Between "Wi-Fi is gone" and "cellular is up" there is no address to
	 * bind at all.  Flushing the transports here would strand the stack
	 * with nothing to re-bind onto, so hold the pending handover and keep
	 * looking with a backoff. */
	if (!use.routable && !use.linklocal) {
		if (!g_nm.down) {
			g_nm.down = true;
			g_nm.cur_laddr[0] = '\0';
			netmon_emit(BARESDK_NET_DOWN, NULL, NULL, 0, 0);
		}
		tmr_start(&g_nm.tmr_settle, backoff_ms(g_nm.attempt++),
		          settle_handler, NULL);
		return;
	}

	/* Link-local only: enough to re-bind (some LANs really do run SIP that
	 * way) but not enough to call the network up. */
	if (!use.routable) {
		if (!g_nm.down) {
			g_nm.down = true;
			netmon_emit(BARESDK_NET_DOWN, NULL, NULL, 0, 0);
		}
	}
	else if (g_nm.down) {
		g_nm.down = false;
		update_cur_laddr();
		netmon_emit(BARESDK_NET_UP, NULL, NULL, 0, 0);
	}

	/* Cellular and Wi-Fi hand out different resolvers; the old ones are
	 * unreachable and every SIP lookup would stall on them.  Skipped when
	 * the app pinned its own nameservers. */
	if (!conf_config()->net.nsc)
		net_dns_refresh(net);

	/* Flush every SIP transport and re-bind on the current addresses.  This
	 * also drops the connection hash, which is what kills the half-open
	 * TCP/TLS/WSS sockets from the old network.  reg/reinvite are passed
	 * false: we drive both ourselves so that accounts the app never asked
	 * to register stay unregistered, and so calls are re-INVITEd under our
	 * retry policy instead of being hung up. */
	err = uag_reset_transp(false, false);
	if (err) {
		g_nm.attempt++;
		netmon_emit(BARESDK_NET_HANDOVER_FAILED, NULL, NULL, err,
		            g_nm.attempt);
		if (g_nm.attempt < g_nm.max_attempts)
			tmr_start(&g_nm.tmr_settle, backoff_ms(g_nm.attempt),
			          settle_handler, NULL);
		return;
	}

	g_nm.attempt       = 0;
	g_nm.reset_pending = false;
	g_nm.force         = false;
	g_nm.announced     = false;
	g_nm.gen++;

	update_cur_laddr();
	netmon_emit(BARESDK_NET_TRANSPORT_RESET, NULL, NULL, 0, 0);

	reregister_accounts();

	if (g_nm.reinvite_calls)
		migrate_calls();
}

static void settle_handler(void *arg)
{
	(void)arg;

	if (!g_nm.started)
		return;

	if (scan_addresses()) {
		/* Still moving — an interface is mid-configuration.  Restart the
		 * debounce so the handover runs once, on the final address set. */
		g_nm.reset_pending = true;
		tmr_start(&g_nm.tmr_settle, g_nm.settle_ms, settle_handler, NULL);
		return;
	}

	if (!g_nm.reset_pending && !g_nm.force)
		return;

	apply_handover();
}

static void netmon_trigger(bool forced)
{
	if (!g_nm.started)
		return;

	if (scan_addresses())
		g_nm.reset_pending = true;
	if (forced)
		g_nm.force = true;

	if (!g_nm.reset_pending && !g_nm.force)
		return;

	if (!g_nm.announced) {
		g_nm.announced      = true;
		g_nm.handover_start = tmr_jiffies();
		update_cur_laddr();
		netmon_emit(BARESDK_NET_CHANGE_DETECTED, NULL, NULL, 0, 0);
	}

	tmr_start(&g_nm.tmr_settle, g_nm.settle_ms, settle_handler, NULL);
}

static void poll_handler(void *arg)
{
	(void)arg;

	if (!g_nm.started)
		return;

	netmon_trigger(false);

	if (g_nm.poll_s)
		tmr_start(&g_nm.tmr_poll, g_nm.poll_s * 1000, poll_handler, NULL);
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

int bsdk_netmon_init(void)
{
	const baresdk_config_t *cfg = &g_bsdk.cfg;

	memset(&g_nm, 0, sizeof(g_nm));
	tmr_init(&g_nm.tmr_poll);
	tmr_init(&g_nm.tmr_settle);
	tmr_init(&g_nm.tmr_verify);

	g_nm.poll_s         = cfg->net_monitor_interval_s;
	g_nm.settle_ms      = cfg->net_settle_ms ? cfg->net_settle_ms
	                                         : BSDK_NET_SETTLE_MS;
	g_nm.verify_ms      = cfg->net_verify_ms;   /* 0 = media check disabled */
	g_nm.max_attempts   = cfg->net_max_attempts ? cfg->net_max_attempts
	                                            : BSDK_NET_MAX_ATTEMPT;
	g_nm.reinvite_calls = cfg->net_reinvite_calls;
	g_nm.hangup_on_fail = cfg->net_hangup_on_migration_failure;
	g_nm.started        = true;

	update_cur_laddr();
	g_nm.down = !have_routable_addr();

	if (g_nm.poll_s)
		tmr_start(&g_nm.tmr_poll, g_nm.poll_s * 1000, poll_handler, NULL);

	return 0;
}

/* Two-step teardown.  bsdk_netmon_stop() is called from the app thread at the
 * top of baresdk_shutdown(), before UAs and calls are torn down: the handlers
 * bail on !started, so no timer that fires meanwhile touches a half-freed
 * account or call.  The timers themselves are cancelled from
 * bsdk_netmon_close() once the re loop has stopped and nothing can fire. */
void bsdk_netmon_stop(void)
{
	g_nm.started = false;
}

void bsdk_netmon_close(void)
{
	g_nm.started = false;
	tmr_cancel(&g_nm.tmr_poll);
	tmr_cancel(&g_nm.tmr_settle);
	tmr_cancel(&g_nm.tmr_verify);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

static void changed_fn(void *arg)
{
	(void)arg;
	netmon_trigger(true);
}

int baresdk_network_changed(void)
{
	return bsdk_dispatch(changed_fn, NULL);
}

typedef struct {
	uint32_t seconds;
} monitor_ctx_t;

static void set_monitor_fn(void *arg)
{
	monitor_ctx_t *ctx = arg;

	g_nm.poll_s = ctx->seconds;
	tmr_cancel(&g_nm.tmr_poll);
	if (g_nm.poll_s)
		tmr_start(&g_nm.tmr_poll, g_nm.poll_s * 1000, poll_handler, NULL);
}

int baresdk_network_set_monitor_interval(uint32_t seconds)
{
	monitor_ctx_t ctx = { .seconds = seconds };
	return bsdk_dispatch_sync(set_monitor_fn, &ctx);
}

typedef struct {
	bool reinvite;
	bool hangup;
} policy_ctx_t;

static void set_policy_fn(void *arg)
{
	policy_ctx_t *ctx = arg;

	g_nm.reinvite_calls = ctx->reinvite;
	g_nm.hangup_on_fail = ctx->hangup;
}

int baresdk_network_set_handover_policy(bool reinvite_calls,
                                         bool hangup_on_failure)
{
	policy_ctx_t ctx = { .reinvite = reinvite_calls,
	                     .hangup   = hangup_on_failure };
	return bsdk_dispatch_sync(set_policy_fn, &ctx);
}

typedef struct {
	char  *buf;
	size_t sz;
} laddr_ctx_t;

static void local_addr_fn(void *arg)
{
	laddr_ctx_t *ctx = arg;

	update_cur_laddr();
	str_ncpy(ctx->buf, g_nm.cur_laddr, ctx->sz);
}

int baresdk_network_local_addr(char *buf, size_t sz)
{
	laddr_ctx_t ctx = { .buf = buf, .sz = sz };

	if (!buf || sz == 0)
		return BARESDK_ERR_INVAL;

	buf[0] = '\0';
	return bsdk_dispatch_sync(local_addr_fn, &ctx);
}

static void is_up_fn(void *arg)
{
	bool *out = arg;
	*out = have_routable_addr();
}

bool baresdk_network_is_up(void)
{
	bool up = false;

	if (bsdk_dispatch_sync(is_up_fn, &up))
		return false;

	return up;
}
