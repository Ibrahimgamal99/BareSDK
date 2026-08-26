/**
 * @file test_ice_deadline.c  Unit tests for the ICE media-NAT shim
 *                            (src/ice_shim.c): the gathering deadline, the
 *                            re-offer of an unsignalled candidate, and holding
 *                            on to the remote address ICE selected.
 *
 * Links the real ice_shim.c against libre and stubs the four baresip symbols
 * it touches (baresip_mnatl, mnat_find, _warning, _info) plus g_bsdk, the same
 * way test_fmtp_bitrate stubs baresip for adapt.c.  libbaresip is deliberately
 * not linked: baresip_mnatl() needs a full baresip_init(), and all this code
 * wants is a media-NAT registry entry to interpose on.
 *
 * A fake "ice" media-NAT stands in for the real module.  Its sessh() records
 * the estab handler and then does nothing at all — which is precisely the
 * failure being guarded against: the ice module's no-STUN/TURN path arms a
 * 1 ms timer that walks the session's media list and fires the gather callback
 * once per entry, so an empty list means the callback never comes, and baresip
 * never sends the deferred INVITE.
 *
 * What has to hold, because getting it wrong is silent and shows up only as
 * calls that never dial:
 *   - a gather that never reports  → the deadline releases the offer,
 *   - a gather that reports in time → deadline cancelled, released once only,
 *   - a failure arriving after the deadline → dropped, call left alone,
 *   - a success arriving after the deadline → passed on, so baresip re-offers,
 *   - mediah/updateh unwrap to the inner session (baresip hands them ours),
 *   - timeout 0 → no deadline at all,
 *   - the baseline for "was this signalled?" moves to what the offer actually
 *     carried, so a srflx that was advertised is not re-offered,
 *   - the remote address ICE nominated is re-asserted after every SDP exchange,
 *     because stream_update() replaces it with the peer's signalled address —
 *     for a PBX behind NAT, an RFC 1918 address that silently kills the call,
 *   - and nothing is re-asserted before a pair has been nominated.
 *
 * Build: make test_ice_deadline  (needs the linux-x86_64 sysroot from a host build)
 */

#include <stdio.h>
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "../../src/echosdk_internal.h"

/* ice_shim.c reads g_bsdk.cfg.ice_gathering_timeout_ms; core.c is not linked
 * in, so own the definition here. */
struct bsdk_ctx g_bsdk;

static int g_pass, g_fail;

/* CHECK() reports through plain printf, which knows nothing of re's %J. */
static const char *sa_addr_str(const struct sa *sa)
{
	static char buf[64];

	if (sa_isset(sa, SA_ADDR) && 0 == sa_ntop(sa, buf, sizeof(buf)))
		return buf;
	return "<unset>";
}

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

/* ── baresip stubs ──────────────────────────────────────────────────────── */

/* Signatures come from baresip.h; the `safe` flag is its own. */
void _warning(bool safe, const char *fmt, ...) { (void)safe; (void)fmt; }
void _info(bool safe, const char *fmt, ...)    { (void)safe; (void)fmt; }

static struct list g_mnatl;

struct list *baresip_mnatl(void)
{
	return &g_mnatl;
}

const struct mnat *mnat_find(const struct list *mnatl, const char *id)
{
	struct le *le;

	for (le = list_head(mnatl); le; le = le->next) {
		struct mnat *m = le->data;
		if (0 == str_cmp(m->id, id))
			return m;
	}
	return NULL;
}

/* ── Fake "ice" media-NAT ───────────────────────────────────────────────── */

/* What the fake session_alloc was handed, so a test can fire it by hand. */
static mnat_estab_h *g_inner_estabh;
static void         *g_inner_arg;
static int           g_sessh_calls;
static int           g_mediah_calls;
static int           g_updateh_calls;
static struct mnat_sess *g_last_inner_seen;   /* what mediah/updateh received */
/* The shim substitutes its own connected-handler before delegating; capture it
 * so a test can fire it the way ice.c's conncheck_handler would. */
static mnat_connected_h *g_ice_connh;
static void             *g_ice_connh_arg;

/* Opaque stand-in for the ice module's own session object. */
struct fake_sess { int magic; };
static struct mnat_sess *g_inner_sess;

static int fake_sessh(struct mnat_sess **sessp, const struct mnat *mnat,
                      struct dnsc *dnsc, int af, const struct stun_uri *srv,
                      const char *user, const char *pass,
                      struct sdp_session *sdp, bool offerer,
                      mnat_estab_h *estabh, void *arg)
{
	struct fake_sess *fs;
	(void)mnat; (void)dnsc; (void)af; (void)srv;
	(void)user; (void)pass; (void)sdp; (void)offerer;

	++g_sessh_calls;
	g_inner_estabh = estabh;
	g_inner_arg    = arg;

	fs = mem_zalloc(sizeof(*fs), NULL);
	if (!fs)
		return ENOMEM;
	fs->magic = 0x1CE;

	g_inner_sess = (struct mnat_sess *)fs;
	*sessp = g_inner_sess;

	/* Deliberately never calls estabh: this is the stalled gather. */
	return 0;
}

/* Stand-in for the ice module's media object.  It has to be a real allocation
 * with a destructor: the shim now hands baresip a wrapper and replaces this
 * underneath it on a restart, so "was the old one freed exactly once" is part of
 * what the tests check. */
struct fake_media { int magic; };
static int g_media_frees;
static struct mnat_media *g_inner_media;      /* last one handed out */
static struct sdp_media  *g_last_media_sdpm;  /* what mediah was given */
static struct udp_sock   *g_last_sock1;
static struct udp_sock   *g_last_sock2;

static void fake_media_destructor(void *arg)
{
	(void)arg;
	++g_media_frees;
}

static int fake_mediah(struct mnat_media **mp, struct mnat_sess *sess,
                       struct udp_sock *sock1, struct udp_sock *sock2,
                       struct sdp_media *sdpm,
                       mnat_connected_h *connh, void *arg)
{
	struct fake_media *fm;

	++g_mediah_calls;
	g_last_inner_seen = sess;
	g_ice_connh       = connh;
	g_ice_connh_arg   = arg;
	g_last_media_sdpm = sdpm;
	g_last_sock1      = sock1;
	g_last_sock2      = sock2;

	fm = mem_zalloc(sizeof(*fm), fake_media_destructor);
	if (!fm)
		return ENOMEM;
	fm->magic = 0x1CE1;

	g_inner_media = (struct mnat_media *)fm;
	if (mp)
		*mp = g_inner_media;
	return 0;
}

static int fake_updateh(struct mnat_sess *sess)
{
	++g_updateh_calls;
	g_last_inner_seen = sess;
	return 0;
}

static int   g_attrh_calls;
static struct mnat_media *g_attrh_media;   /* what attrh received */

static void fake_attrh(struct mnat_media *mm, const char *name,
                       const char *value)
{
	(void)name; (void)value;
	++g_attrh_calls;
	g_attrh_media = mm;
}

static struct mnat fake_ice = {
	.id             = "ice",
	.ftag           = "+sip.ice",
	.wait_connected = true,
	.sessh          = fake_sessh,
	.mediah         = fake_mediah,
	.updateh        = fake_updateh,
	.attrh          = fake_attrh,
};

/* ── Peer-reflexive re-offer ─────────────────────────────────────────────────
 *
 * ICE learns srflx candidates before the offer/answer, so those are signalled.
 * A peer-reflexive candidate is discovered from the connectivity checks
 * themselves and is never signalled (RFC 8445 §5.1.3) — so when ICE nominates
 * one, media leaves from an address the peer was never told about and a strict
 * peer (Asterisk) drops it.  The shim has to notice and ask for a re-offer.
 */

static int   g_connh_calls;
static struct sa g_connh_raddr;
static bool  g_connh_raddr2_set;

static void fake_connh(const struct sa *raddr1, const struct sa *raddr2,
                       void *arg)
{
	(void)arg;
	++g_connh_calls;
	if (raddr1)
		g_connh_raddr = *raddr1;
	g_connh_raddr2_set = (raddr2 != NULL);
}

/* ── What baresip's mnat_handler would see ──────────────────────────────── */

static int   g_estab_calls;
static int   g_estab_err;
static int   g_estab_scode;
static void *g_estab_arg;

static void outer_estab(int err, uint16_t scode, const char *reason, void *arg)
{
	(void)reason;
	++g_estab_calls;
	g_estab_err   = err;
	g_estab_scode = scode;
	g_estab_arg   = arg;
}

/* ── Loop driver ────────────────────────────────────────────────────────── */

static void stop_handler(void *arg)
{
	(void)arg;
	re_cancel();
}

/* Run the re event loop for `ms`, so timers armed by the code under test fire. */
static void run_loop(unsigned ms)
{
	struct tmr stop;

	tmr_init(&stop);
	tmr_start(&stop, ms, stop_handler, NULL);
	re_main(NULL);
	tmr_cancel(&stop);
}

static void reset_counters(void)
{
	g_inner_estabh = NULL;
	g_inner_arg    = NULL;
	g_sessh_calls  = 0;
	g_mediah_calls = 0;
	g_updateh_calls = 0;
	g_last_inner_seen = NULL;
	g_inner_sess   = NULL;
	g_ice_connh    = NULL;
	g_ice_connh_arg = NULL;
	g_estab_calls  = 0;
	g_estab_err    = 0;
	g_estab_scode  = 0;
	g_estab_arg    = NULL;
	g_media_frees  = 0;
	g_inner_media  = NULL;
	g_last_media_sdpm = NULL;
	g_last_sock1   = NULL;
	g_last_sock2   = NULL;
	g_attrh_calls  = 0;
	g_attrh_media  = NULL;
}

/* Drive a session through the (now wrapped) vtable. */
static struct mnat_sess *open_sess_sdp(void *call_arg, struct sdp_session *sdp)
{
	struct mnat_sess *sess = NULL;
	const struct mnat *m = mnat_find(baresip_mnatl(), "ice");
	int err;

	err = m->sessh(&sess, m, NULL, AF_INET, NULL, NULL, NULL,
	               sdp, true, outer_estab, call_arg);
	if (err)
		return NULL;
	return sess;
}

static struct mnat_sess *open_sess(void *call_arg)
{
	return open_sess_sdp(call_arg, NULL);
}

/* ── Tests ──────────────────────────────────────────────────────────────── */

static void test_install(void)
{
	const struct mnat *m;

	CHECK(0 == bsdk_ice_shim_init(), "install failed");

	m = mnat_find(baresip_mnatl(), "ice");
	CHECK(m != NULL, "mnat vanished");
	CHECK(m->sessh   != fake_sessh,   "sessh not wrapped");
	CHECK(m->mediah  != fake_mediah,  "mediah not wrapped");
	CHECK(m->updateh != fake_updateh, "updateh not wrapped");
	CHECK(m->attrh   != fake_attrh,   "attrh not wrapped");

	/* Identity must survive: baresip matches id against "ice" to advertise
	 * Supported: ice, and ftag puts +sip.ice in the Contact. */
	CHECK(0 == str_cmp(m->id, "ice"), "id changed to '%s'", m->id);
	CHECK(0 == str_cmp(m->ftag, "+sip.ice"), "ftag changed");
	CHECK(m->wait_connected, "wait_connected changed");

	/* Installing again must be a no-op, not a self-capture that recurses. */
	{
		mnat_sess_h *first = m->sessh;
		CHECK(0 == bsdk_ice_shim_init(), "reinstall failed");
		CHECK(m->sessh == first, "reinstall re-wrapped the wrapper");
	}
}

/* The bug: a gather that never reports must not hold the offer forever. */
static void test_deadline_releases_offer(void)
{
	struct mnat_sess *sess;
	int marker = 42;

	reset_counters();
	g_bsdk.cfg.ice_gathering_timeout_ms = 120;

	sess = open_sess(&marker);
	CHECK(sess != NULL, "sessh failed");
	CHECK(g_sessh_calls == 1, "inner sessh not called");
	CHECK(g_estab_calls == 0, "released before the deadline");

	run_loop(60);
	CHECK(g_estab_calls == 0, "released early at 60 ms of a 120 ms deadline");

	run_loop(120);
	CHECK(g_estab_calls == 1, "deadline did not release the offer (calls=%d)",
	      g_estab_calls);
	/* Success, not an error: baresip's mnat_handler answers an error with
	 * CALL_EVENT_CLOSED, which is the outcome the deadline exists to avoid. */
	CHECK(g_estab_err == 0, "released with err=%d", g_estab_err);
	CHECK(g_estab_scode == 0, "released with scode=%u", g_estab_scode);
	CHECK(g_estab_arg == &marker, "arg not passed through");

	mem_deref(sess);
}

/* Normal path: gathering finishes first, so the deadline never fires. */
static void test_gather_beats_deadline(void)
{
	struct mnat_sess *sess;

	reset_counters();
	g_bsdk.cfg.ice_gathering_timeout_ms = 120;

	sess = open_sess(NULL);
	CHECK(sess != NULL, "sessh failed");

	g_inner_estabh(0, 0, NULL, g_inner_arg);
	CHECK(g_estab_calls == 1, "gather completion not passed on");

	/* Past the deadline: it must have been cancelled, not merely ignored. */
	run_loop(200);
	CHECK(g_estab_calls == 1, "deadline fired after gathering completed "
	      "(calls=%d)", g_estab_calls);

	mem_deref(sess);
}

/* A gather failure that lands after the deadline must not end the call: the
 * offer is already on the wire and baresip would answer it with CALL_CLOSED. */
static void test_late_failure_dropped(void)
{
	struct mnat_sess *sess;

	reset_counters();
	g_bsdk.cfg.ice_gathering_timeout_ms = 60;

	sess = open_sess(NULL);
	CHECK(sess != NULL, "sessh failed");

	run_loop(140);
	CHECK(g_estab_calls == 1, "deadline did not release the offer");

	g_inner_estabh(ETIMEDOUT, 0, "STUN timed out", g_inner_arg);
	CHECK(g_estab_calls == 1, "late failure reached baresip (calls=%d)",
	      g_estab_calls);
	CHECK(g_estab_err == 0, "late failure overwrote the released status");

	mem_deref(sess);
}

/* A gather success that lands after the deadline must be passed on, so baresip
 * re-offers the fuller candidate set in a re-INVITE. */
static void test_late_success_passed_on(void)
{
	struct mnat_sess *sess;

	reset_counters();
	g_bsdk.cfg.ice_gathering_timeout_ms = 60;

	sess = open_sess(NULL);
	CHECK(sess != NULL, "sessh failed");

	run_loop(140);
	CHECK(g_estab_calls == 1, "deadline did not release the offer");

	g_inner_estabh(0, 0, NULL, g_inner_arg);
	CHECK(g_estab_calls == 2, "late completion not passed on for a re-offer "
	      "(calls=%d)", g_estab_calls);

	mem_deref(sess);
}

/* baresip hands our wrapper back to mediah/updateh; both must unwrap. Getting
 * this wrong hands the ice module a pointer that is not its session. */
static void test_media_and_update_unwrap(void)
{
	struct mnat_sess *sess;
	const struct mnat *m = mnat_find(baresip_mnatl(), "ice");
	struct mnat_media *mm = NULL;
	struct mnat_sess *inner;

	reset_counters();
	g_bsdk.cfg.ice_gathering_timeout_ms = 0;   /* no deadline in this test */

	sess = open_sess(NULL);
	CHECK(sess != NULL, "sessh failed");
	inner = g_inner_sess;
	CHECK(sess != inner, "wrapper handed baresip the inner session");

	m->mediah(&mm, sess, NULL, NULL, NULL, NULL, NULL);
	CHECK(g_mediah_calls == 1, "inner mediah not called");
	CHECK(g_last_inner_seen == inner, "mediah got the wrapper, not the "
	      "inner session");

	/* And the other direction: what baresip gets back is *our* media object,
	 * not the ice module's.  It has to be, because a restart replaces the
	 * inner one while baresip keeps holding this pointer as stream->mns. */
	CHECK(mm != NULL, "mediah returned no media");
	CHECK(mm != g_inner_media, "handed baresip the inner media object");

	/* attrh is the one place baresip hands that pointer back, so it is the
	 * one place that has to unwrap it. */
	m->attrh(mm, "candidate", "1 1 UDP 2130706431 10.0.0.5 44690 typ host");
	CHECK(g_attrh_calls == 1, "inner attrh not called");
	CHECK(g_attrh_media == g_inner_media, "attrh got the wrapper, not the "
	      "inner media");

	m->updateh(sess);
	CHECK(g_updateh_calls == 1, "inner updateh not called");
	CHECK(g_last_inner_seen == inner, "updateh got the wrapper, not the "
	      "inner session");

	mem_deref(mm);
	mem_deref(sess);
}

/* 0 means "wait indefinitely" — the documented escape hatch. */
static void test_zero_disables_deadline(void)
{
	struct mnat_sess *sess;

	reset_counters();
	g_bsdk.cfg.ice_gathering_timeout_ms = 0;

	sess = open_sess(NULL);
	CHECK(sess != NULL, "sessh failed");

	run_loop(150);
	CHECK(g_estab_calls == 0, "a deadline fired with the timeout disabled");

	mem_deref(sess);
}

/* Dropping the call must cancel the deadline: the timer closes over the
 * session, so a fire after teardown is a use-after-free. */
static void test_destroy_cancels_deadline(void)
{
	struct mnat_sess *sess;

	reset_counters();
	g_bsdk.cfg.ice_gathering_timeout_ms = 80;

	sess = open_sess(NULL);
	CHECK(sess != NULL, "sessh failed");
	mem_deref(sess);

	run_loop(200);
	CHECK(g_estab_calls == 0, "deadline fired after the session was freed");
}

/* Build a real sdp_media so laddr changes can be driven the way ice.c drives
 * them (refresh_laddr -> sdp_media_set_laddr). */
static int make_sdp(struct sdp_session **sessp, struct sdp_media **mp,
                    const struct sa *laddr)
{
	int err = sdp_session_alloc(sessp, laddr);
	if (err)
		return err;
	/* As baresip builds it: the session carries the address, sdp_media_add()
	 * contributes only the port.  sdp_media_laddr() therefore reads back as
	 * 0.0.0.0:<port> until ICE sets one. */
	return sdp_media_add(mp, *sessp, "audio", sa_port(laddr), "RTP/AVP");
}

/* Give the session a remote address the only way libre allows: by decoding an
 * SDP body, which is what an arriving offer does.  The re-assert guard reads
 * sdp_media_raddr(), so a test of it needs the real thing rather than a stub. */
static int decode_remote(struct sdp_session *sdpsess, const char *ip,
                         uint16_t port)
{
	struct mbuf *mb = mbuf_alloc(512);
	int err;

	if (!mb)
		return ENOMEM;

	err = mbuf_printf(mb,
	                  "v=0\r\n"
	                  "o=- 0 0 IN IP4 %s\r\n"
	                  "s=-\r\n"
	                  "c=IN IP4 %s\r\n"
	                  "t=0 0\r\n"
	                  "m=audio %u RTP/AVP 0\r\n"
	                  "a=rtpmap:0 PCMU/8000\r\n",
	                  ip, ip, port);
	if (!err) {
		mb->pos = 0;
		err = sdp_decode(sdpsess, mb, true);
	}

	mem_deref(mb);
	return err;
}


/* The whole point: ICE settles on an address we never put in the SDP, so the
 * peer must be told about it. */
static void test_reoffer_on_unsignalled_candidate(void)
{
	struct sdp_session *sdpsess = NULL;
	struct sdp_media *sdpm = NULL;
	struct mnat_sess *sess;
	struct mnat_media *mm = NULL;
	const struct mnat *m = mnat_find(baresip_mnatl(), "ice");
	struct sa host, prflx;

	reset_counters();
	g_connh_calls = 0;
	g_bsdk.cfg.ice_gathering_timeout_ms = 0;

	sa_set_str(&host,  "10.0.0.5", 44690);
	sa_set_str(&prflx, "213.212.207.242", 44690);

	CHECK(0 == make_sdp(&sdpsess, &sdpm, &host), "sdp setup failed");

	sess = open_sess_sdp(NULL, sdpsess);
	CHECK(sess != NULL, "sessh failed");

	m->mediah(&mm, sess, NULL, NULL, sdpm, fake_connh, NULL);

	/* ICE concludes on a peer-reflexive candidate: not the address that
	 * went into the answer. */
	sdp_media_set_laddr(sdpm, &prflx);
	g_ice_connh(NULL, NULL, g_ice_connh_arg);

	CHECK(g_connh_calls == 1, "baresip's connected handler was not called");
	CHECK(g_estab_calls == 1, "no re-offer for an unsignalled candidate "
	      "(calls=%d)", g_estab_calls);

	/* Once per session: two streams must not fire two re-INVITEs. */
	g_ice_connh(NULL, NULL, g_ice_connh_arg);
	CHECK(g_estab_calls == 1, "re-offered more than once (calls=%d)",
	      g_estab_calls);

	mem_deref(mm);
	mem_deref(sess);
	mem_deref(sdpm);
	mem_deref(sdpsess);
}

/* The common case — ICE picks what we already advertised. Re-offering there
 * would put a pointless re-INVITE on every single call. */
static void test_no_reoffer_when_unchanged(void)
{
	struct sdp_session *sdpsess = NULL;
	struct sdp_media *sdpm = NULL;
	struct mnat_sess *sess;
	struct mnat_media *mm = NULL;
	const struct mnat *m = mnat_find(baresip_mnatl(), "ice");
	struct sa host;

	reset_counters();
	g_connh_calls = 0;
	g_bsdk.cfg.ice_gathering_timeout_ms = 0;

	sa_set_str(&host, "10.0.0.5", 44690);
	CHECK(0 == make_sdp(&sdpsess, &sdpm, &host), "sdp setup failed");

	sess = open_sess_sdp(NULL, sdpsess);
	CHECK(sess != NULL, "sessh failed");

	m->mediah(&mm, sess, NULL, NULL, sdpm, fake_connh, NULL);

	/* ICE concludes on the host candidate — the address already advertised. */
	sdp_media_set_laddr(sdpm, &host);
	g_ice_connh(NULL, NULL, g_ice_connh_arg);

	CHECK(g_connh_calls == 1, "connected handler not delegated");
	CHECK(g_estab_calls == 0, "re-offered an address the peer already had "
	      "(calls=%d)", g_estab_calls);

	mem_deref(mm);
	mem_deref(sess);
	mem_deref(sdpm);
	mem_deref(sdpsess);
}

/* ── The remote address must survive an SDP exchange ─────────────────────────
 *
 * baresip writes the address it sends media to from two places, and the second
 * overwrites the first: stream_mnat_connected() puts the remote candidate ICE
 * nominated there, and stream_update() — reached from call_update_media() on
 * every re-INVITE, ours or the peer's — replaces it with sdp_media_raddr(), the
 * address in the peer's SDP verbatim.  For a PBX behind NAT that is a private
 * address on the far side of the internet, and nothing puts the ICE one back:
 * the ice module's update handler refreshes local addresses only.  With
 * DTLS-SRTP the handshake is still retransmitting when the address moves under
 * it, so it never completes, stream_is_ready() stays false, and the call runs
 * with no audio in either direction.
 *
 * call_apply_sdp() calls the update handler last, after every stream_update(),
 * so re-asserting there covers all of those paths.
 */
static void test_updateh_reasserts_selected_raddr(void)
{
	struct sdp_session *sdpsess = NULL;
	struct sdp_media *sdpm = NULL;
	struct mnat_sess *sess;
	struct mnat_media *mm = NULL;
	const struct mnat *m = mnat_find(baresip_mnatl(), "ice");
	struct sa host, selected;

	reset_counters();
	g_connh_calls = 0;
	g_connh_raddr2_set = true;
	sa_init(&g_connh_raddr, AF_UNSPEC);
	g_bsdk.cfg.ice_gathering_timeout_ms = 0;

	sa_set_str(&host, "10.0.0.5", 44690);
	CHECK(0 == make_sdp(&sdpsess, &sdpm, &host), "sdp setup failed");
	/* What the PBX signalled: a private address on the far side of a NAT. */
	CHECK(0 == decode_remote(sdpsess, "172.16.11.52", 19914),
	      "remote sdp decode failed");

	sess = open_sess_sdp(NULL, sdpsess);
	CHECK(sess != NULL, "sessh failed");

	m->mediah(&mm, sess, NULL, NULL, sdpm, fake_connh, NULL);

	/* ICE nominates a pair: this is the address media has to go to, and the
	 * only place it is ever announced. */
	sa_set_str(&selected, "82.129.158.253", 19914);
	g_ice_connh(&selected, NULL, g_ice_connh_arg);
	CHECK(g_connh_calls == 1, "connected handler not delegated");
	CHECK(sa_cmp(&g_connh_raddr, &selected, SA_ALL),
	      "delegated the wrong address (%s:%u)",
	      sa_addr_str(&g_connh_raddr), sa_port(&g_connh_raddr));
	/* No RTCP component here, so nothing may be invented for it: passing a
	 * zeroed sa instead of NULL would point RTCP at 0.0.0.0. */
	CHECK(!g_connh_raddr2_set, "invented an RTCP address");

	/* stream_update() has now reverted the stream to the peer's SDP address;
	 * the update handler that follows it must undo that. */
	sa_init(&g_connh_raddr, AF_UNSPEC);
	m->updateh(sess);
	CHECK(g_updateh_calls == 1, "inner updateh not called");
	CHECK(g_connh_calls == 2, "the selected remote address was not "
	      "re-asserted after the SDP exchange (connh calls=%d)",
	      g_connh_calls);
	CHECK(sa_cmp(&g_connh_raddr, &selected, SA_ALL),
	      "re-asserted the wrong address (%s:%u)",
	      sa_addr_str(&g_connh_raddr), sa_port(&g_connh_raddr));
	CHECK(!g_connh_raddr2_set, "invented an RTCP address on re-assert");

	mem_deref(mm);
	mem_deref(sess);
	mem_deref(sdpm);
	mem_deref(sdpsess);
}

/* The other side of that guard.  A peer that signals a *different* address in
 * its re-INVITE has genuinely moved its media — transferred, or re-bridged onto
 * another media server — and stream_update() is right to follow it.  Re-applying
 * the pair ICE nominated against the old address would send audio to a place
 * nobody is listening any more, which is the same failure in reverse. */
static void test_updateh_yields_when_peer_moves(void)
{
	struct sdp_session *sdpsess = NULL;
	struct sdp_media *sdpm = NULL;
	struct mnat_sess *sess;
	struct mnat_media *mm = NULL;
	const struct mnat *m = mnat_find(baresip_mnatl(), "ice");
	struct sa host, selected;

	reset_counters();
	g_connh_calls = 0;
	g_bsdk.cfg.ice_gathering_timeout_ms = 0;

	sa_set_str(&host, "10.0.0.5", 44690);
	CHECK(0 == make_sdp(&sdpsess, &sdpm, &host), "sdp setup failed");
	CHECK(0 == decode_remote(sdpsess, "172.16.11.52", 19914),
	      "remote sdp decode failed");

	sess = open_sess_sdp(NULL, sdpsess);
	CHECK(sess != NULL, "sessh failed");
	m->mediah(&mm, sess, NULL, NULL, sdpm, fake_connh, NULL);

	sa_set_str(&selected, "82.129.158.253", 19914);
	g_ice_connh(&selected, NULL, g_ice_connh_arg);
	CHECK(g_connh_calls == 1, "connected handler not delegated");

	/* The peer's re-INVITE points somewhere else entirely. */
	CHECK(0 == decode_remote(sdpsess, "203.0.113.9", 40000),
	      "second remote sdp decode failed");

	m->updateh(sess);
	CHECK(g_connh_calls == 1, "overrode a peer that moved its media "
	      "(connh calls=%d)", g_connh_calls);

	/* And the stale pair is dropped, so a later exchange does not resurrect
	 * it either. */
	m->updateh(sess);
	CHECK(g_connh_calls == 1, "stale pair resurrected on a later exchange "
	      "(connh calls=%d)", g_connh_calls);

	mem_deref(mm);
	mem_deref(sess);
	mem_deref(sdpm);
	mem_deref(sdpsess);
}

/* The first update handler call is how ICE gets started — it runs when the
 * peer's SDP arrives, long before any pair is nominated.  There is nothing to
 * re-assert then, and calling baresip's connected handler would tell it a
 * stream is connected when it is not. */
static void test_updateh_quiet_before_conncheck(void)
{
	struct mnat_sess *sess;
	struct mnat_media *mm = NULL;
	const struct mnat *m = mnat_find(baresip_mnatl(), "ice");

	reset_counters();
	g_connh_calls = 0;
	g_bsdk.cfg.ice_gathering_timeout_ms = 0;

	sess = open_sess(NULL);
	CHECK(sess != NULL, "sessh failed");

	m->mediah(&mm, sess, NULL, NULL, NULL, fake_connh, NULL);
	m->updateh(sess);

	CHECK(g_updateh_calls == 1, "inner updateh not called");
	CHECK(g_connh_calls == 0, "reported a connected stream before any "
	      "connectivity check (calls=%d)", g_connh_calls);

	mem_deref(mm);
	mem_deref(sess);
}

/* The baseline for "was this address signalled?" is seeded from the session
 * address, which is all that exists before gathering — the private host address
 * on a phone behind NAT.  The offer that actually goes out carries the srflx
 * the gather found, so the baseline has to move with it; otherwise every NAT'd
 * call re-offers a candidate the peer was already told about. */
static void test_signalled_rebased_at_gather(void)
{
	struct sdp_session *sdpsess = NULL;
	struct sdp_media *sdpm = NULL;
	struct mnat_sess *sess;
	struct mnat_media *mm = NULL;
	const struct mnat *m = mnat_find(baresip_mnatl(), "ice");
	struct sa host, srflx;

	reset_counters();
	g_connh_calls = 0;
	g_bsdk.cfg.ice_gathering_timeout_ms = 0;

	sa_set_str(&host,  "10.100.4.206", 62209);
	sa_set_str(&srflx, "41.33.94.42",  62209);

	CHECK(0 == make_sdp(&sdpsess, &sdpm, &host), "sdp setup failed");

	sess = open_sess_sdp(NULL, sdpsess);
	CHECK(sess != NULL, "sessh failed");
	m->mediah(&mm, sess, NULL, NULL, sdpm, fake_connh, NULL);

	/* Gathering finds the srflx and writes it into the SDP, then reports —
	 * and baresip builds the offer from the SDP as it stands now. */
	sdp_media_set_laddr(sdpm, &srflx);
	g_inner_estabh(0, 0, NULL, g_inner_arg);
	CHECK(g_estab_calls == 1, "gather completion not passed on");

	/* ICE then nominates that same srflx: already signalled, nothing to say. */
	g_ice_connh(NULL, NULL, g_ice_connh_arg);
	CHECK(g_estab_calls == 1, "re-offered the srflx that the offer already "
	      "carried (calls=%d)", g_estab_calls);

	mem_deref(mm);
	mem_deref(sess);
	mem_deref(sdpm);
	mem_deref(sdpsess);
}

/* ── ICE restart on network handover ────────────────────────────────────────
 *
 * The reason this exists: call_reset_transp() moves the *session* address, and
 * ice.c owns the *media* address (it writes the selected candidate there, and a
 * media-level `c=` overrides the session-level one, RFC 4566 §5.7).  Nothing
 * re-gathers either.  So a handover re-INVITE on an ICE call re-advertises the
 * network the call just left, and the peer keeps sending RTP into the void while
 * dropping what arrives from the new source.  bsdk_ice_restart() replaces the
 * whole ICE session — new credentials, new gather — and the offer that follows
 * carries the new address at both levels.
 */

/* A real socket, so the port the media address keeps can be read back from the
 * thing that owns it rather than assumed. */
static struct udp_sock *open_sock(uint16_t *port)
{
	struct udp_sock *us = NULL;
	struct sa any, local;

	sa_init(&any, AF_INET);
	if (udp_listen(&us, &any, NULL, NULL))
		return NULL;
	if (udp_local_get(us, &local)) {
		mem_deref(us);
		return NULL;
	}
	if (port)
		*port = sa_port(&local);
	return us;
}

static void test_restart_replaces_session(void)
{
	struct sdp_session *sdpsess = NULL;
	struct sdp_media *sdpm = NULL;
	struct mnat_sess *sess, *first_inner;
	struct mnat_media *mm = NULL;
	const struct mnat *m = mnat_find(baresip_mnatl(), "ice");
	struct udp_sock *sock;
	struct sa wifi, lte;
	uint16_t rtp_port = 0;
	int marker = 7;

	reset_counters();
	g_bsdk.cfg.ice_gathering_timeout_ms = 0;   /* restart uses its own bound */

	sock = open_sock(&rtp_port);
	CHECK(sock != NULL, "socket setup failed");
	sa_set_str(&wifi, "10.0.0.5", rtp_port);
	sa_set_str(&lte,  "100.82.7.19", 0);

	CHECK(0 == make_sdp(&sdpsess, &sdpm, &wifi), "sdp setup failed");

	sess = open_sess_sdp(&marker, sdpsess);
	CHECK(sess != NULL, "sessh failed");
	first_inner = g_inner_sess;

	m->mediah(&mm, sess, sock, NULL, sdpm, fake_connh, NULL);
	CHECK(g_mediah_calls == 1, "inner mediah not called");

	/* What the gather settled on before the handover: a candidate on the
	 * network that is about to disappear. */
	sdp_media_set_laddr(sdpm, &wifi);
	CHECK(0 == sdp_media_set_lattr(sdpm, false, "candidate",
	      "1 1 UDP 2130706431 10.0.0.5 %u typ host", rtp_port),
	      "candidate attr setup failed");
	g_inner_estabh(0, 0, NULL, g_inner_arg);
	CHECK(g_estab_calls == 1, "gather completion not passed on");

	/* Handover. */
	CHECK(0 == bsdk_ice_restart(&marker, &lte), "restart refused");

	CHECK(g_sessh_calls == 2, "no replacement ICE session (sessh calls=%d)",
	      g_sessh_calls);
	CHECK(g_inner_sess != first_inner, "reused the same inner session");
	CHECK(g_media_frees == 1, "the old inner media was not freed exactly "
	      "once (frees=%d)", g_media_frees);
	CHECK(g_mediah_calls == 2, "the stream was not re-created against the "
	      "new session (mediah calls=%d)", g_mediah_calls);
	CHECK(g_last_inner_seen == g_inner_sess, "the replacement media was "
	      "attached to the old session");
	CHECK(g_last_media_sdpm == sdpm, "the replacement media got a different "
	      "sdp media line");
	/* The RTP socket is deliberately NOT re-created: it is wildcard-bound so
	 * it already follows the new route, and the media encryption is keyed to
	 * it. */
	CHECK(g_last_sock1 == sock, "the RTP socket was not reused");

	/* The pointer baresip holds must not have moved — it is stream->mns. */
	CHECK(mm != NULL && mm != g_inner_media, "wrapper identity changed");
	m->attrh(mm, "candidate", "1 1 UDP 2130706431 100.82.7.19 1 typ host");
	CHECK(g_attrh_media == g_inner_media, "attrh still points at the old "
	      "inner media after the restart");

	/* Both levels of the SDP now name the new interface, and the stale
	 * candidate is gone rather than waiting to be checked against. */
	CHECK(sa_cmp(sdp_session_laddr(sdpsess), &lte, SA_ADDR),
	      "session address not moved (%s)",
	      sa_addr_str(sdp_session_laddr(sdpsess)));
	CHECK(sa_cmp(sdp_media_laddr(sdpm), &lte, SA_ADDR),
	      "media address still on the old network (%s)",
	      sa_addr_str(sdp_media_laddr(sdpm)));
	CHECK(sa_port(sdp_media_laddr(sdpm)) == rtp_port,
	      "media port changed with the address (%u, want %u)",
	      sa_port(sdp_media_laddr(sdpm)), rtp_port);
	CHECK(sdp_media_lattr_apply(sdpm, "candidate", NULL, NULL) == NULL,
	      "the pre-handover candidate is still in the offer");

	/* The re-gather reports, and that is what puts the re-INVITE on the
	 * wire: baresip's mnat_handler turns an estab on an established call
	 * into call_modify(). */
	g_inner_estabh(0, 0, NULL, g_inner_arg);
	CHECK(g_estab_calls == 2, "the restart produced no re-offer (calls=%d)",
	      g_estab_calls);
	CHECK(g_estab_err == 0, "re-offer reported err=%d", g_estab_err);
	CHECK(g_estab_arg == &marker, "arg not carried across the restart");

	mem_deref(mm);
	mem_deref(sess);
	CHECK(g_media_frees == 2, "the replacement media leaked (frees=%d)",
	      g_media_frees);
	mem_deref(sock);
	mem_deref(sdpm);
	mem_deref(sdpsess);
}

/* Teardown in the other order: the session going first must leave the media
 * wrapper baresip still holds safe to free.  call_destructor() drops the streams
 * before call->mnats today, but nothing in the API says it has to. */
static void test_restart_teardown_order(void)
{
	struct mnat_sess *sess;
	struct mnat_media *mm = NULL;
	const struct mnat *m = mnat_find(baresip_mnatl(), "ice");
	int marker = 8;

	reset_counters();
	g_bsdk.cfg.ice_gathering_timeout_ms = 0;

	sess = open_sess(&marker);
	CHECK(sess != NULL, "sessh failed");
	m->mediah(&mm, sess, NULL, NULL, NULL, fake_connh, NULL);

	mem_deref(sess);
	CHECK(g_media_frees == 1, "the inner media was not freed with the "
	      "session (frees=%d)", g_media_frees);

	/* Detached, so this must neither double-free nor reach a dead session. */
	m->attrh(mm, "candidate", "x");
	m->attrh(mm, "ice-ufrag", "x");
	mem_deref(mm);
}

/* A call with no ICE — the ordinary case for a direct-RTP account — must be
 * left to netmon's plain re-offer rather than reported as a failure. */
static void test_restart_unknown_call(void)
{
	int other = 0;

	reset_counters();
	CHECK(ENOENT == bsdk_ice_restart(&other, NULL),
	      "a call with no ICE session was not reported as ENOENT");
	CHECK(!bsdk_ice_call_active(&other), "reported ICE on a call with none");
}

/* A session whose streams all had the media-NAT disabled (BUNDLE mux does this)
 * has no ICE state to restart and no candidates in the SDP to be stale. */
static void test_restart_needs_media(void)
{
	struct mnat_sess *sess;
	int marker = 9;

	reset_counters();
	g_bsdk.cfg.ice_gathering_timeout_ms = 0;

	sess = open_sess(&marker);
	CHECK(sess != NULL, "sessh failed");
	CHECK(ENOENT == bsdk_ice_restart(&marker, NULL),
	      "restarted a session with no media");
	CHECK(!bsdk_ice_call_active(&marker),
	      "reported ICE on a session with no media");
	CHECK(g_sessh_calls == 1, "allocated a replacement session anyway");

	mem_deref(sess);
}

/* netmon retries on its verify tick.  A retry that lands while the first
 * restart is still gathering must not start a second one: the offer from the
 * first is still coming and is bounded by the deadline. */
static void test_restart_already_gathering(void)
{
	struct mnat_sess *sess;
	struct mnat_media *mm = NULL;
	const struct mnat *m = mnat_find(baresip_mnatl(), "ice");
	int marker = 10;

	reset_counters();
	g_bsdk.cfg.ice_gathering_timeout_ms = 0;

	sess = open_sess(&marker);
	CHECK(sess != NULL, "sessh failed");
	m->mediah(&mm, sess, NULL, NULL, NULL, fake_connh, NULL);
	CHECK(bsdk_ice_call_active(&marker), "live ICE session not reported");

	CHECK(0 == bsdk_ice_restart(&marker, NULL), "first restart refused");
	CHECK(EALREADY == bsdk_ice_restart(&marker, NULL),
	      "a second restart was started while the first was gathering");
	CHECK(g_sessh_calls == 2, "sessh called %d times", g_sessh_calls);

	mem_deref(mm);
	mem_deref(sess);
}

/* A restart gathers under a call that is up.  baresip answers an mnat failure
 * with CALL_EVENT_CLOSED, so reporting one here would hang up the very call the
 * restart was trying to rescue. */
static void test_restart_failure_not_propagated(void)
{
	struct mnat_sess *sess;
	struct mnat_media *mm = NULL;
	const struct mnat *m = mnat_find(baresip_mnatl(), "ice");
	int marker = 11;

	reset_counters();
	g_bsdk.cfg.ice_gathering_timeout_ms = 0;

	sess = open_sess(&marker);
	CHECK(sess != NULL, "sessh failed");
	m->mediah(&mm, sess, NULL, NULL, NULL, fake_connh, NULL);

	CHECK(0 == bsdk_ice_restart(&marker, NULL), "restart refused");

	/* STUN is unreachable on the new network. */
	g_inner_estabh(ETIMEDOUT, 0, "STUN timed out", g_inner_arg);
	CHECK(g_estab_calls == 1, "the restart produced no offer at all "
	      "(calls=%d)", g_estab_calls);
	CHECK(g_estab_err == 0, "a restart failure reached baresip (err=%d) — "
	      "that closes the call", g_estab_err);
	CHECK(g_estab_scode == 0, "a restart failure reached baresip (scode=%u)",
	      g_estab_scode);

	mem_deref(mm);
	mem_deref(sess);
}

/* And a restart whose gather never reports at all: the call is already without
 * audio, so an offer has to go out on a bound.  Unlike the initial INVITE, this
 * one is bounded even when the configured timeout is 0. */
static void test_restart_deadline_releases_offer(void)
{
	struct mnat_sess *sess;
	struct mnat_media *mm = NULL;
	const struct mnat *m = mnat_find(baresip_mnatl(), "ice");
	int marker = 12;

	reset_counters();
	g_bsdk.cfg.ice_gathering_timeout_ms = 120;

	sess = open_sess(&marker);
	CHECK(sess != NULL, "sessh failed");
	m->mediah(&mm, sess, NULL, NULL, NULL, fake_connh, NULL);

	/* Let the first deadline release the initial offer, so what the next one
	 * releases can only be the restart's. */
	run_loop(200);
	CHECK(g_estab_calls == 1, "initial deadline did not release the offer");

	CHECK(0 == bsdk_ice_restart(&marker, NULL), "restart refused");
	run_loop(60);
	CHECK(g_estab_calls == 1, "restart offer released early");

	run_loop(150);
	CHECK(g_estab_calls == 2, "the restart's deadline did not release an "
	      "offer (calls=%d)", g_estab_calls);
	CHECK(g_estab_err == 0, "released with err=%d", g_estab_err);

	/* A gather that finally reports afterwards re-offers the full set, the
	 * same way a late initial gather does. */
	g_inner_estabh(0, 0, NULL, g_inner_arg);
	CHECK(g_estab_calls == 3, "a late restart gather was not passed on "
	      "(calls=%d)", g_estab_calls);

	mem_deref(mm);
	mem_deref(sess);
}

static void test_close_restores(void)
{
	const struct mnat *m;

	bsdk_ice_shim_close();

	m = mnat_find(baresip_mnatl(), "ice");
	CHECK(m->sessh   == fake_sessh,   "sessh not restored");
	CHECK(m->mediah  == fake_mediah,  "mediah not restored");
	CHECK(m->updateh == fake_updateh, "updateh not restored");
	CHECK(m->attrh   == fake_attrh,   "attrh not restored");

	/* Idempotent — shutdown runs it on the init failure path too. */
	bsdk_ice_shim_close();
	CHECK(m->sessh == fake_sessh, "second close disturbed the vtable");
}

int main(void)
{
	int err;

	err = libre_init();
	if (err) {
		printf("libre_init failed: %d\n", err);
		return 1;
	}

	list_init(&g_mnatl);
	/* What mnat_register() does, inlined: it lives in libbaresip, which this
	 * test deliberately does not link. */
	list_append(&g_mnatl, &fake_ice.le, &fake_ice);

	printf("ice gathering deadline tests\n");

	test_install();
	test_deadline_releases_offer();
	test_gather_beats_deadline();
	test_late_failure_dropped();
	test_late_success_passed_on();
	test_media_and_update_unwrap();
	test_reoffer_on_unsignalled_candidate();
	test_no_reoffer_when_unchanged();
	test_updateh_reasserts_selected_raddr();
	test_updateh_yields_when_peer_moves();
	test_updateh_quiet_before_conncheck();
	test_signalled_rebased_at_gather();
	test_restart_replaces_session();
	test_restart_teardown_order();
	test_restart_unknown_call();
	test_restart_needs_media();
	test_restart_already_gathering();
	test_restart_failure_not_propagated();
	test_restart_deadline_releases_offer();
	test_zero_disables_deadline();
	test_destroy_cancels_deadline();
	test_close_restores();

	printf("%d passed, %d failed\n", g_pass, g_fail);

	/* Before libre_close(): the timer list belongs to the re context, and
	 * asking for it afterwards just reports "re not ready". */
	tmr_debug();
	mem_debug();
	libre_close();

	return g_fail ? 1 : 0;
}
