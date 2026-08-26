/**
 * @file ice_shim.c  Deadline on ICE candidate gathering
 *
 * An outgoing call with a media-NAT does not send its INVITE when it is
 * placed.  baresip defers it (src/call.c, call_connect):
 *
 *     if (!call->acc->mnat)
 *             err = send_invite(call);
 *     else
 *             err = call_streams_alloc(call);   // INVITE deferred
 *
 * and sends it later from mnat_handler(), which runs when the media-NAT
 * reports that candidate gathering is done.  Nothing bounds how long that
 * takes, and there is one path where the report never comes at all: the ice
 * module's no-STUN/TURN case arms a 1 ms timer whose handler walks
 * `sess->medial` and calls the gather handler once per media entry, so an
 * empty list means the estab handler is never invoked.  The call then sits in
 * ECHOSDK_CALL_CALLING forever — no INVITE on the wire, no event, nothing for
 * the app to react to.  `ua_connect()` returned 0, so the SDK cannot see it
 * either.
 *
 * Every other SIP client bounds this wait and offers whatever it has when the
 * clock runs out.  JsSIP and SIP.js resolve their offer promise on either
 * `icegatheringstatechange == complete` *or* a timer; dart-sip-ua does the
 * same with `ice_gathering_timeout`, default 500 ms; pjsua has
 * PJSUA_ICE_TRANSPORT_INIT_TIMEOUT (30 s) whose own comment calls it "a safety
 * net so the calling thread cannot block indefinitely if the callback never
 * arrives".  This file is that bound for EchoSDK, as cfg.ice_gathering_timeout_ms.
 *
 * ── How it hooks in ─────────────────────────────────────────────────────────
 *
 * `send_invite()` is static inside baresip, so there is no seam to call it
 * from.  The seam that does exist is the media-NAT vtable: baresip finds it
 * with mnat_find(baresip_mnatl(), "ice") and calls through the function
 * pointers.  We overwrite three of those pointers on the registered struct and
 * keep the originals, which is the same interposition ws_path.c performs on
 * libre with --wrap, applied to a vtable instead of the linker.
 *
 * Replacing the struct rather than registering a second media-NAT keeps its
 * identity intact: baresip matches `mnat->id` against "ice" to advertise
 * `Supported: ice` (src/ua.c), and `ftag` puts "+sip.ice" in the Contact.  A
 * differently-named clone would silently drop both.
 *
 * Wrapping sessh alone is not enough — baresip hands whatever `*sessp` we
 * return back to mediah() and updateh(), so those two have to unwrap it.  For
 * the same reason mediah() returns a wrapper of its own and attrh() unwraps it:
 * see the ICE-restart section below for why the media object has to be ours.
 *
 * ── ICE restart on network handover ─────────────────────────────────────────
 *
 * The second job of this file is the one thing netmon.c cannot do from where it
 * stands.  When the device moves between Wi-Fi and cellular, netmon migrates a
 * call with call_reset_transp(), which is sdp_session_set_laddr() plus a
 * re-INVITE.  That is the whole fix for a direct-RTP call, and no fix at all for
 * an ICE call:
 *
 *   - ice.c writes the selected local candidate into the *media* address
 *     (refresh_comp_laddr → sdp_media_set_laddr), and libre emits a
 *     media-level `c=` line whenever that is set (re/src/sdp/msg.c).  Per
 *     RFC 4566 §5.7 it overrides the session-level line netmon just rewrote,
 *     so the offer re-advertises the address that just went away.
 *   - Nothing re-gathers.  The ice module's update handler runs ice_start()
 *     with `started` already true, which only calls icem_update() and
 *     re-encodes the candidate list it already has, under the same
 *     ice-ufrag/ice-pwd.
 *
 * So the peer keeps sending RTP to the old address, and drops what arrives from
 * the new one — with ICE, a source that is not in the candidate list is not
 * accepted (Asterisk: "Source not in ICE active candidate list").  The call
 * stays up with dead audio until netmon runs out of attempts.
 *
 * The repair is an RFC 8445 §9 ICE restart, which needs a fresh
 * ice-ufrag/ice-pwd pair and a fresh gather — neither of which the ice module
 * exposes.  What it does expose, to us, is that we own its session object: the
 * inner `struct mnat_sess` is ours to throw away and allocate again.  A new one
 * mints new credentials (rand_str in session_alloc), writes them into the SDP,
 * and re-gathers against the interface that now carries the default route; when
 * it reports, baresip's mnat_handler turns it into the re-INVITE, which
 * therefore carries the new candidates *and* the new `c=` at both levels.
 *
 * Two things make that safe to do under a live call:
 *
 *   - the media object must survive the swap, because baresip stores it as
 *     `stream->mns` and hands it back to attrh().  Hence the wrapper: the
 *     pointer baresip holds is ours and does not move, while the ice module's
 *     media underneath it is replaced.
 *   - a gather that fails must not be reported as a failure.  baresip answers
 *     an mnat error with CALL_EVENT_CLOSED, which would hang up a call that is
 *     up and merely needs a new offer, so a failed restart releases the offer
 *     with whatever candidates exist instead.
 *
 * The RTP sockets are reused as they are — they are wildcard-bound and follow
 * the new default route on their own, and re-creating them would break the
 * media encryption that is keyed to them.
 */

#include "echosdk_internal.h"

/* Originals, captured at install time. */
static mnat_sess_h   *real_sessh;
static mnat_media_h  *real_mediah;
static mnat_update_h *real_updateh;
static mnat_attr_h   *real_attrh;

/* The registered struct we mutated, so close() can put it back. */
static struct mnat *s_mnat;

/* Live sessions, so a restart can find the one belonging to a call.  baresip
 * keeps no accessor for `call->mnats`, and the call pointer is what netmon has
 * — it is the `arg` the ice session was allocated with. */
static struct list s_sessl = LIST_INIT;

/* Deadline for a restart gather when cfg.ice_gathering_timeout_ms is 0.
 *
 * Zero means "no deadline" for the initial INVITE, where waiting is baresip's
 * own behaviour and the call has not started yet.  A restart is different: it
 * happens under a live call whose audio is already gone, and a gather that
 * never reports would leave it that way with no offer ever sent.  So the
 * restart always has a bound, configured or not. */
#define BSDK_ICE_RESTART_DEADLINE_MS 3000

/* How long the candidate re-offer waits for a media-encryption handshake.
 *
 * The re-offer is a re-INVITE, and a re-INVITE mid-DTLS is what breaks the
 * handshake it interrupts: dtls_srtp starts the association exactly once
 * (media_start() latches `started`), so an offer/answer that perturbs it
 * leaves `menc_secure` unset for good.  stream_is_ready() then stays false,
 * audio_update() is never called, and the call sits established with no
 * audio in either direction — the failure reassert_selected() documents,
 * reached by a second route.
 *
 * So the re-offer waits for the handshake instead of racing it.  The wait is
 * bounded: a handshake that is never going to finish must not also cost the
 * peer the candidate it was never told about, so the offer goes out anyway
 * when the deadline expires. */
#define BSDK_ICE_REOFFER_SECURE_DEADLINE_MS 4000
#define BSDK_ICE_REOFFER_POLL_MS             100

/**
 * Our stand-in for the ice module's session object.
 *
 * baresip holds this as `call->mnats` and passes it to mediah()/updateh();
 * both unwrap to `inner` before delegating.  It owns `inner`, so dropping the
 * call frees the real session through our destructor.
 *
 * The fields below `medial` are the arguments the ice module was allocated
 * with, kept so an equivalent session can be built again on a restart.
 */
struct bsdk_ice_sess {
	struct le         le;       /* s_sessl */
	struct mnat_sess *inner;    /* the ice module's own session */
	mnat_estab_h     *estabh;   /* baresip's mnat_handler */
	void             *arg;      /* its `struct call *` */
	struct tmr        tmr;      /* gathering deadline */
	struct tmr        tmr_reoffer;/* re-offer held for the DTLS handshake */
	uint64_t          reoffer_due;/* jiffies at which to stop holding it */
	bool              released; /* the offer has been handed to baresip */
	bool              reoffered;/* a candidate re-offer has been requested */
	bool              restarting;/* a restart gather is outstanding */
	struct sdp_session *sdp;    /* borrowed; the call's SDP session */
	struct list       medial;   /* struct bsdk_ice_media */

	const struct mnat *mnat;
	struct dnsc       *dnsc;    /* borrowed; owned by the SIP stack */
	int                af;
	bool               have_srv;
	struct stun_uri    srv;     /* .host is ours (see sess_destructor) */
	char              *user;
	char              *pass;
};

/**
 * One media stream.
 *
 * This is what baresip holds as `stream->mns`, so its address must not change
 * for the life of the stream — `inner` is the ice module's media object and is
 * what gets replaced on a restart.
 *
 * It also remembers the address we signalled, so it can be compared with the
 * one ICE ends up using.  See the peer-reflexive note on ice_connected().
 */
struct bsdk_ice_media {
	struct le              le;
	struct bsdk_ice_sess  *sess;    /* NULL once the session is gone */
	struct mnat_media     *inner;   /* the ice module's own media */
	struct udp_sock       *sock1;   /* refs: re-creating `inner` needs them */
	struct udp_sock       *sock2;   /* NULL with rtcp-mux */
	struct sdp_media      *sdpm;    /* borrowed; owned by the call's SDP */
	mnat_connected_h      *connh;   /* baresip's stream handler */
	void                  *arg;
	struct sa              signalled; /* laddr at offer/answer time */
	struct sa              selected[2]; /* raddr ICE nominated: RTP, RTCP */
	struct sa              rsig;      /* peer's signalled raddr back then */
	bool                   sel_set;   /* selected[] has been filled in */
	bool                   reasserted;/* logged the first re-assert */
};

static void media_destructor(void *data)
{
	struct bsdk_ice_media *m = data;

	list_unlink(&m->le);
	mem_deref(m->inner);
	mem_deref(m->sock1);
	mem_deref(m->sock2);
}

static void sess_destructor(void *data)
{
	struct bsdk_ice_sess *s = data;
	struct le *le, *tmp;

	/* Timers first: both close over `s`. */
	tmr_cancel(&s->tmr);
	tmr_cancel(&s->tmr_reoffer);
	list_unlink(&s->le);

	/* The media wrappers belong to baresip's streams, which may outlive us:
	 * call_destructor() drops call->mnats after call->audio, but nothing
	 * guarantees that order on every teardown path.  Cut them loose — their
	 * `inner` is about to be freed with the inner session (its destructor
	 * list_flushes its own media list), and a wrapper left pointing at it
	 * would double-free from media_destructor. */
	LIST_FOREACH_SAFE(&s->medial, le, tmp) {
		struct bsdk_ice_media *m = le->data;
		m->inner = mem_deref(m->inner);
		m->sess  = NULL;
		list_unlink(&m->le);
	}

	mem_deref(s->inner);
	mem_deref(s->srv.host);
	mem_deref(s->user);
	mem_deref(s->pass);
}

static struct bsdk_ice_sess *sess_find(const void *call)
{
	struct le *le;

	LIST_FOREACH(&s_sessl, le) {
		struct bsdk_ice_sess *s = le->data;
		if (s->arg == call)
			return s;
	}

	return NULL;
}

/**
 * Re-baseline what each stream has signalled, at the moment baresip is about to
 * build the offer or answer from the SDP as it now stands.
 *
 * `signalled` is seeded in ice_mediah() from the *session* address, which is
 * all there is before gathering produces anything — on a phone behind NAT the
 * private host address.  The SDP that actually goes out carries the srflx the
 * gather just found, so comparing against that seed reports "never signalled"
 * for the srflx on every NAT'd call and re-offers a candidate the peer was
 * already told about.  Take the baseline from the addresses ICE has written
 * into the SDP by now instead.
 */
static void rebase_signalled(struct bsdk_ice_sess *s)
{
	struct le *le;

	LIST_FOREACH(&s->medial, le) {
		struct bsdk_ice_media *m = le->data;
		const struct sa *laddr = sdp_media_laddr(m->sdpm);

		if (laddr && sa_isset(laddr, SA_ADDR))
			m->signalled = *laddr;
	}
}


/* ── Estab handler — runs on re_main ─────────────────────────────────────── */

static void ice_estab(int err, uint16_t scode, const char *reason, void *arg)
{
	struct bsdk_ice_sess *s = arg;

	tmr_cancel(&s->tmr);

	if (s->released) {
		/* The deadline already released the offer.
		 *
		 * A success here is the real gather finishing late.  Pass it on:
		 * baresip sees a media-NAT established on a call that is no
		 * longer waiting for one and turns it into a re-INVITE, which
		 * re-offers the now-complete candidate set.  That is the same
		 * refresh the ice module already performs from its connectivity
		 * check.
		 *
		 * A failure is not actionable any more.  The offer is on the
		 * wire and the call may well be up; baresip's handler answers a
		 * failure with CALL_EVENT_CLOSED, which would tear down a
		 * working call because a STUN server we no longer need did not
		 * answer.  Log it and stop here. */
		s->restarting = false;

		if (err || scode) {
			warning("EchoSDK/ice: gathering failed after the "
			        "deadline had already released the offer "
			        "(%m, %u %s) — call left alone\n",
			        err, scode, reason ? reason : "");
			return;
		}

		info("EchoSDK/ice: gathering completed after the deadline; "
		     "re-offering the full candidate set\n");
	}

	/* A restart gathers under a call that is already up, so a failure here
	 * must not travel: baresip's mnat_handler answers one with
	 * CALL_EVENT_CLOSED and the call the restart was trying to rescue would
	 * be hung up instead.  Release the offer with whatever the ice module
	 * managed to put in the SDP — even a host-only candidate on the new
	 * interface is better than the old network's candidate list, which is
	 * what not offering at all would leave in place. */
	if (s->restarting && (err || scode)) {
		warning("EchoSDK/ice: restart gathering failed (%m, %u %s) —"
		        " offering the candidates gathered so far\n",
		        err, scode, reason ? reason : "");
		err    = 0;
		scode  = 0;
		reason = NULL;
	}
	s->restarting = false;

	if (!err && !scode)
		rebase_signalled(s);

	s->released = true;
	s->estabh(err, scode, reason, s->arg);
}

/* ── Deadline — runs on re_main ──────────────────────────────────────────── */

static void gather_deadline(void *arg)
{
	struct bsdk_ice_sess *s = arg;

	if (s->released)
		return;

	/* Report success with no error: baresip's mnat_handler treats that as
	 * "media-nat established/gathered" and sends the INVITE with whatever
	 * the ice module has put into the SDP so far.  Reporting an error
	 * instead would close the call, which is the outcome the deadline
	 * exists to avoid. */
	warning("EchoSDK/ice: %s gathering did not complete in time; offering "
	        "the candidates gathered so far "
	        "(cfg.ice_gathering_timeout_ms=%u)\n",
	        s->restarting ? "restart" : "candidate",
	        g_bsdk.cfg.ice_gathering_timeout_ms);

	/* `released` is not cleared: a real gather that reports after this takes
	 * the `released` path in ice_estab(), which re-offers the complete set.
	 *
	 * `restarting` IS cleared, and must be.  It gates bsdk_ice_restart()
	 * against re-entry, and the gather this deadline just gave up on may
	 * never report at all — that is the failure the deadline exists for.
	 * Leaving the flag set would make every later restart on this call
	 * return EALREADY, so a second handover would silently skip the ICE
	 * restart and migrate with the candidates of a network that is gone. */
	s->restarting = false;
	s->released   = true;
	s->estabh(0, 0, NULL, s->arg);
}

/* ── Wrapped vtable entries ──────────────────────────────────────────────── */

static int ice_sessh(struct mnat_sess **sessp, const struct mnat *mnat,
                     struct dnsc *dnsc, int af, const struct stun_uri *srv,
                     const char *user, const char *pass,
                     struct sdp_session *sdp, bool offerer,
                     mnat_estab_h *estabh, void *arg)
{
	struct bsdk_ice_sess *s;
	int err = 0;

	if (!sessp || !estabh)
		return EINVAL;

	s = mem_zalloc(sizeof(*s), sess_destructor);
	if (!s)
		return ENOMEM;

	tmr_init(&s->tmr);
	tmr_init(&s->tmr_reoffer);
	list_init(&s->medial);
	s->estabh = estabh;
	s->arg    = arg;
	s->sdp    = sdp;

	/* Everything a replacement session needs.  `srv` points into the
	 * account, which can be re-configured under a live call, so the host
	 * string is copied rather than referenced. */
	s->mnat = mnat;
	s->dnsc = dnsc;
	s->af   = af;
	if (srv) {
		s->have_srv   = true;
		s->srv.scheme = srv->scheme;
		s->srv.port   = srv->port;
		s->srv.proto  = srv->proto;
		if (srv->host)
			err = str_dup(&s->srv.host, srv->host);
	}
	if (!err && user)
		err = str_dup(&s->user, user);
	if (!err && pass)
		err = str_dup(&s->pass, pass);
	if (err) {
		mem_deref(s);
		return err;
	}

	err = real_sessh(&s->inner, mnat, dnsc, af, srv, user, pass,
	                 sdp, offerer, ice_estab, s);
	if (err) {
		mem_deref(s);
		return err;
	}

	/* Arm after the inner session exists.  `released` is checked because a
	 * media-NAT is free to report synchronously; the ice module does not,
	 * but a deadline armed after the fact would never be cancelled. */
	if (g_bsdk.cfg.ice_gathering_timeout_ms && !s->released) {
		tmr_start(&s->tmr, g_bsdk.cfg.ice_gathering_timeout_ms,
		          gather_deadline, s);
	}

	list_append(&s_sessl, &s->le, s);

	*sessp = (struct mnat_sess *)s;
	return 0;
}

/**
 * Is a media-encryption handshake still in flight on this call?
 *
 * Two questions, because `menc_secure` is only ever set by an encryption that
 * has something to secure: a plain-RTP call leaves it false for the life of
 * the stream, and waiting on it there would hold every re-offer to the
 * deadline for nothing.  So the fingerprint attribute decides whether there is
 * a handshake at all, and stream_is_secure() decides whether it has finished.
 *
 * sdp_media_session_rattr() reads the media level first and falls back to the
 * session level, which is where baresip's dtls_srtp puts our own — and where a
 * peer that offers one fingerprint for the whole session puts its.
 */
static bool sess_awaiting_secure(const struct bsdk_ice_sess *s)
{
	struct list *streaml;
	struct le *le;
	bool dtls = false;

	if (!s || !s->sdp || !s->arg)
		return false;

	LIST_FOREACH(&s->medial, le) {
		const struct bsdk_ice_media *m = le->data;

		if (m->sdpm &&
		    sdp_media_session_rattr(m->sdpm, s->sdp, "fingerprint")) {
			dtls = true;
			break;
		}
	}

	if (!dtls)
		return false;

	streaml = call_streaml(s->arg);
	if (!streaml)
		return false;

	LIST_FOREACH(streaml, le) {
		const struct stream *strm = le->data;

		if (!stream_is_secure(strm))
			return true;
	}

	return false;
}


/* Hand the re-offer to baresip.  mnat_wait is false by now, so it turns into
 * call_modify(), i.e. a re-INVITE.  It is guarded by sipsess_refresh_allowed(),
 * so a call whose negotiation is still in flight ignores it rather than
 * sending a malformed request. */
static void reoffer_release(struct bsdk_ice_sess *s)
{
	tmr_cancel(&s->tmr_reoffer);
	s->estabh(0, 0, NULL, s->arg);
}


static void reoffer_deadline(void *arg)
{
	struct bsdk_ice_sess *s = arg;

	if (sess_awaiting_secure(s)) {
		if (tmr_jiffies() < s->reoffer_due) {
			tmr_start(&s->tmr_reoffer, BSDK_ICE_REOFFER_POLL_MS,
			          reoffer_deadline, s);
			return;
		}

		/* The handshake is not coming back.  Releasing the offer is the
		 * lesser evil: it cannot damage an association that never
		 * established, and it is the peer's only chance to learn the
		 * address our media actually comes from. */
		warning("EchoSDK/ice: media encryption still not secure after "
		        "%u ms — releasing the candidate re-offer anyway\n",
		        BSDK_ICE_REOFFER_SECURE_DEADLINE_MS);
	}

	reoffer_release(s);
}


/**
 * ICE finished its connectivity checks for one stream.
 *
 * This is where a peer-reflexive candidate has to be re-offered.  ICE learns
 * srflx candidates from STUN *before* the offer/answer, so those are signalled
 * and the peer accepts media from them.  A *peer-reflexive* candidate is
 * different: it is discovered from the connectivity checks themselves, after
 * the SDP has gone out, and RFC 8445 §5.1.3 does not signal it.  When ICE then
 * nominates that candidate — which is exactly what happens on a phone behind
 * NAT with no STUN server configured, where the host candidate is the only one
 * offered — we start sending media from an address the peer has never been
 * told about.
 *
 * A strict peer drops it.  Asterisk logs
 *
 *     res_rtp_asterisk.c: DTLS packet from <ip:port> dropped.
 *     Source not in ICE active candidate list.
 *
 * and since DTLS-SRTP gates RTP behind the handshake (`wait_secure`), the call
 * connects, signalling looks perfect, and neither side hears anything.  It hits
 * the answerer hardest: the answerer picks `a=setup:active` per RFC 5763 and so
 * is the side that sends first, into an address the peer will not accept.
 *
 * baresip already knows how to fix this — ice.c sets `send_reinvite` when the
 * selected local candidate changes — but only acts on it when its conncheck
 * handler is called with `update` set, which does not happen on this path
 * (`connectivity check is complete (update=0)`).  So do it here: if the address
 * ICE settled on is not the one we signalled, ask baresip to re-offer.  By the
 * time this runs, ice.c has already written the new candidates into the SDP,
 * so the re-INVITE carries them.
 */
static void ice_connected(const struct sa *raddr1, const struct sa *raddr2,
                          void *arg)
{
	struct bsdk_ice_media *m = arg;
	struct bsdk_ice_sess *s = m->sess;
	const struct sa *laddr;

	if (!s) {
		/* Detached by sess_destructor — the call is going away. */
		if (m->connh)
			m->connh(raddr1, raddr2, m->arg);
		return;
	}

	/* Remember the pair ICE nominated.  call_apply_sdp() throws it away on
	 * every later SDP exchange — see ice_updateh(). */
	if (raddr1 && sa_isset(raddr1, SA_ALL)) {
		const struct sa *rsig = sdp_media_raddr(m->sdpm);

		m->selected[0] = *raddr1;
		sa_init(&m->selected[1], AF_UNSPEC);
		if (raddr2)
			sa_cpy(&m->selected[1], raddr2);
		sa_init(&m->rsig, AF_UNSPEC);
		if (rsig)
			sa_cpy(&m->rsig, rsig);
		m->sel_set = true;
	}

	/* baresip first: it starts the media encryption, and a re-offer must not
	 * jump ahead of that. */
	if (m->connh)
		m->connh(raddr1, raddr2, m->arg);

	/* SA_ADDR, not SA_ALL: the port belongs to the socket and never moves,
	 * while the session laddr this may be compared against carries a
	 * different port entirely.  The address is the thing the peer filters
	 * on. */
	laddr = sdp_media_laddr(m->sdpm);
	if (!laddr || !sa_isset(&m->signalled, SA_ADDR) ||
	    !sa_isset(laddr, SA_ADDR))
		return;

	if (sa_cmp(laddr, &m->signalled, SA_ADDR))
		return;   /* unchanged — the peer already knows this address */

	/* Once per session.  Each stream would otherwise ask separately, and a
	 * burst of re-INVITEs on one dialog is worse than the problem. */
	if (s->reoffered)
		return;
	s->reoffered = true;

	info("EchoSDK/ice: selected local candidate %J was never signalled "
	     "(offered %J) — re-offering so the peer accepts our media\n",
	     laddr, &m->signalled);

	/* Never while the media encryption is mid-handshake: the re-INVITE
	 * would perturb an association dtls_srtp can only start once, and the
	 * call would come up silent.  See BSDK_ICE_REOFFER_SECURE_DEADLINE_MS. */
	if (sess_awaiting_secure(s)) {
		info("EchoSDK/ice: holding the re-offer until the media "
		     "encryption handshake completes\n");
		s->reoffer_due = tmr_jiffies() +
		                 BSDK_ICE_REOFFER_SECURE_DEADLINE_MS;
		tmr_start(&s->tmr_reoffer, BSDK_ICE_REOFFER_POLL_MS,
		          reoffer_deadline, s);
		return;
	}

	reoffer_release(s);
}

static int ice_mediah(struct mnat_media **mp, struct mnat_sess *sess,
                      struct udp_sock *sock1, struct udp_sock *sock2,
                      struct sdp_media *sdpm,
                      mnat_connected_h *connh, void *arg)
{
	struct bsdk_ice_sess *s = (struct bsdk_ice_sess *)sess;
	struct bsdk_ice_media *m;
	const struct sa *laddr;
	int err;

	if (!s)
		return EINVAL;

	m = mem_zalloc(sizeof(*m), media_destructor);
	if (!m)
		return ENOMEM;

	m->sess  = s;
	m->sdpm  = sdpm;
	m->connh = connh;
	m->arg   = arg;
	/* Referenced, not borrowed: a restart hands the same sockets to the
	 * replacement media, so they have to outlive the ice module's own
	 * reference to them. */
	m->sock1 = mem_ref(sock1);
	m->sock2 = mem_ref(sock2);

	err = real_mediah(&m->inner, s->inner, sock1, sock2, sdpm,
	                  ice_connected, m);
	if (err) {
		mem_deref(m);
		return err;
	}

	/* Record the address this stream is about to be advertised on.
	 *
	 * A stream's own laddr is unset until ICE picks one — sdp_media_add()
	 * only fills in the port, so sdp_media_laddr() reads back as
	 * 0.0.0.0:<port> and the `c=` line the peer receives comes from the
	 * *session*.  Fall back to that, or the baseline is an address that was
	 * never advertised to anyone and every ICE call re-offers itself. */
	laddr = sdp_media_laddr(sdpm);
	if (laddr && sa_isset(laddr, SA_ADDR)) {
		m->signalled = *laddr;
	}
	else if (s->sdp) {
		const struct sa *slad = sdp_session_laddr(s->sdp);
		if (slad)
			m->signalled = *slad;
	}

	list_append(&s->medial, &m->le, m);

	/* Ours, not the ice module's: baresip keeps this as `stream->mns` for
	 * the life of the stream, and a restart replaces what is underneath it.
	 * ice_attrh() unwraps it again. */
	*mp = (struct mnat_media *)m;
	return 0;
}

/**
 * Remote ICE attributes for one stream.
 *
 * Pure unwrapping: baresip calls this with what mediah() returned, which is our
 * wrapper, while the ice module's handler expects its own object.  During the
 * window where a restart has dropped the old media and not yet built the
 * replacement there is nothing to decode into, and the attributes are re-read
 * from the SDP by the ice module's update handler anyway.
 */
static void ice_attrh(struct mnat_media *mm, const char *name,
                      const char *value)
{
	struct bsdk_ice_media *m = (struct bsdk_ice_media *)mm;

	if (!m || !m->inner || !real_attrh)
		return;

	real_attrh(m->inner, name, value);
}

/**
 * Re-assert the remote address ICE selected, after an SDP exchange discarded it.
 *
 * baresip keeps the address it sends media to in `stream->tx.raddr_rtp`, and
 * two different things write it:
 *
 *   - stream_mnat_connected(), from the media-NAT's connected handler, with the
 *     remote candidate ICE nominated;
 *   - stream_remote_set(), from stream_update(), with `sdp_media_raddr()` — the
 *     address in the peer's SDP, verbatim.
 *
 * The second one runs on *every* call to call_update_media(): our re-offer from
 * ice_connected(), ice.c's own re-INVITE, a re-INVITE the peer sends, a session
 * refresh, hold/unhold.  It overwrites the first, and nothing puts it back: the
 * ice module's update handler only refreshes *local* addresses
 * (ice_start() with sess->started already true), and no new connectivity check
 * runs, so no further connected handler fires.
 *
 * When the peer is behind NAT the two addresses are not the same.  A PBX that
 * signals `c=IN IP4 172.16.11.52` and is reached, per ICE, at
 * 82.129.158.253:19914 leaves the stream pointed at 172.16.11.52 from the first
 * re-INVITE onwards — an RFC 1918 address on the far side of the internet.  With
 * DTLS-SRTP the handshake is what dies first: it is still retransmitting when
 * the address changes under it, the retries go into the void, `menc_secure`
 * never gets set, and stream_is_ready() stays false forever.  So audio_update()
 * is never called, the mic is never opened, and the call sits there fully
 * established with tx 0 / rx 0 and no audio levels at all — in both directions,
 * on a call where signalling looks perfect.
 *
 * call_apply_sdp() calls the media-NAT's update handler as its last act, after
 * stream_update() has run for every stream, which makes this the seam that
 * covers all of those paths at once.
 */
static void reassert_selected(struct bsdk_ice_sess *s)
{
	struct le *le;

	LIST_FOREACH(&s->medial, le) {
		struct bsdk_ice_media *m = le->data;
		const struct sa *rsig;

		if (!m->sel_set || !m->connh)
			continue;

		/* Only when the peer is still asking for the same place.  A peer
		 * that signals a *different* address has genuinely moved its
		 * media — a transfer, a re-bridge onto another media server —
		 * and stream_update() is right to follow it.  Pinning the old
		 * pair there would break the very case this is meant to protect.
		 * The nominated pair belongs to an address the peer no longer
		 * uses, so forget it and let ICE nominate again. */
		rsig = sdp_media_raddr(m->sdpm);
		if (!rsig || !sa_cmp(rsig, &m->rsig, SA_ALL)) {
			m->sel_set = false;
			continue;
		}

		if (!m->reasserted) {
			m->reasserted = true;
			info("EchoSDK/ice: re-applying the remote address ICE "
			     "selected (%J) after the SDP exchange reset it to "
			     "the signalled %J\n",
			     &m->selected[0], &m->rsig);
		}

		m->connh(&m->selected[0],
		         sa_isset(&m->selected[1], SA_ALL) ? &m->selected[1]
		                                           : NULL,
		         m->arg);
	}
}


/* ── ICE restart (called from netmon.c on handover) ──────────────────────── */

/**
 * Take one stream's pre-restart ICE state out of the SDP.
 *
 * The re-gather rewrites all of this the moment it reports (set_media_attributes
 * deletes and re-adds the candidate list, gather_handler re-points the media
 * address at the new default candidate), so in the ordinary case this is
 * invisible.  It matters when the gather does *not* report — a STUN server that
 * is unreachable on the new network is exactly the case the deadline exists for.
 * The offer released then would otherwise pair new credentials with the old
 * network's candidates and the old network's media address: the peer would run
 * its checks against addresses that are gone and get nowhere.
 *
 * Cleared instead, the same offer says "no candidates" and carries `c=` on the
 * new interface, which the peer reads as ICE unsupported (ice.c's own
 * verify_peer_ice does the mirror image of this) and answers with plain RTP to
 * an address that works.
 *
 * The port is kept: it belongs to the RTP socket, which is not being re-created,
 * and is what the m= line is built from.  It is read back from the socket rather
 * than from the SDP, because the address that was there may have been a
 * server-reflexive candidate whose port is the NAT's, not ours.
 */
static void media_clear_stale(struct bsdk_ice_media *m, const struct sa *laddr)
{
	struct sa local;

	sdp_media_del_lattr(m->sdpm, ice_attr_cand);
	sdp_media_del_lattr(m->sdpm, ice_attr_remote_cand);

	if (!laddr || !sa_isset(laddr, SA_ADDR) || !m->sock1)
		return;

	if (udp_local_get(m->sock1, &local))
		return;

	{
		struct sa media = *laddr;
		sa_set_port(&media, sa_port(&local));
		sdp_media_set_laddr(m->sdpm, &media);
	}

	if (m->sock2 && !udp_local_get(m->sock2, &local)) {
		struct sa rtcp = *laddr;
		sa_set_port(&rtcp, sa_port(&local));
		sdp_media_set_laddr_rtcp(m->sdpm, &rtcp);
	}
}

/**
 * Restart ICE for one call and re-offer on `laddr`.
 *
 * @param call  baresip call the migration is for (netmon's lc->bc).
 * @param laddr New local address, or NULL to leave the session address alone.
 *
 * @return 0 when a restart is under way — the re-INVITE follows from the estab
 *         handler, within the gathering deadline.
 *         ENOENT when this call has no ICE media-NAT to restart (the caller's
 *         plain re-offer is the right thing then), EALREADY when a restart is
 *         already gathering, or an errorcode when the replacement session could
 *         not be allocated (the old one is left running).
 */
int bsdk_ice_restart(void *call, const struct sa *laddr)
{
	struct bsdk_ice_sess *s = sess_find(call);
	struct mnat_sess *inner;
	struct le *le;
	uint32_t deadline;
	int err;

	if (!s_mnat || !s)
		return ENOENT;

	/* No media means baresip disabled the media-NAT for every stream (BUNDLE
	 * mux does this), so there is no ICE state to restart and no candidates
	 * in the SDP to be stale. */
	if (list_isempty(&s->medial))
		return ENOENT;

	if (s->restarting)
		return EALREADY;

	/* Build the replacement before dismantling anything, so a failure leaves
	 * the call with the ICE state it already had.
	 *
	 * offerer=true: we are the side sending this offer, which per RFC 8445
	 * §7.3.1.1 makes us the controlling agent for the restarted session; the
	 * new tie-breaker sorts it out if the peer disagrees. */
	err = real_sessh(&inner, s->mnat, s->dnsc, s->af,
	                 s->have_srv ? &s->srv : NULL, s->user, s->pass,
	                 s->sdp, true, ice_estab, s);
	if (err) {
		warning("EchoSDK/ice: restart: replacement session failed"
		        " (%m) — keeping the current ICE state\n", err);
		return err;
	}

	/* The session-level address matters for the streams whose media address
	 * the gather has not written yet, and for a peer that ignores ICE. */
	if (laddr && sa_isset(laddr, SA_ADDR))
		sdp_session_set_laddr(s->sdp, laddr);

	/* Drop the old media objects first: each unlinks itself from the old
	 * session's list, so the deref below cannot free them a second time. */
	LIST_FOREACH(&s->medial, le) {
		struct bsdk_ice_media *m = le->data;
		m->inner = mem_deref(m->inner);
		media_clear_stale(m, laddr);
	}

	mem_deref(s->inner);
	s->inner      = inner;
	s->released   = false;
	s->restarting = true;
	s->reoffered  = false;

	LIST_FOREACH(&s->medial, le) {
		struct bsdk_ice_media *m = le->data;
		int e;

		m->sel_set    = false;
		m->reasserted = false;

		e = real_mediah(&m->inner, s->inner, m->sock1, m->sock2,
		                m->sdpm, ice_connected, m);
		if (e) {
			warning("EchoSDK/ice: restart: media '%s' failed (%m)"
			        " — that stream keeps the candidates it had\n",
			        sdp_media_name(m->sdpm), e);
		}
	}

	deadline = g_bsdk.cfg.ice_gathering_timeout_ms
	         ? g_bsdk.cfg.ice_gathering_timeout_ms
	         : BSDK_ICE_RESTART_DEADLINE_MS;
	tmr_start(&s->tmr, deadline, gather_deadline, s);

	info("EchoSDK/ice: restarting ICE on %j — new credentials, re-gathering,"
	     " re-INVITE follows within %u ms\n",
	     laddr, deadline);

	return 0;
}

/**
 * Does this call have a live ICE media-NAT?
 *
 * Answers the question netmon actually has ("are this call's candidates mine to
 * migrate?") rather than the one the config answers ("was ICE asked for?").
 */
bool bsdk_ice_call_active(void *call)
{
	struct bsdk_ice_sess *s = sess_find(call);

	return s && !list_isempty(&s->medial);
}

static int ice_updateh(struct mnat_sess *sess)
{
	struct bsdk_ice_sess *s = (struct bsdk_ice_sess *)sess;
	int err;

	if (!s)
		return EINVAL;

	err = real_updateh(s->inner);

	reassert_selected(s);

	return err;
}

/* ── Install / restore ───────────────────────────────────────────────────── */

/**
 * Interpose the gathering deadline on the "ice" media-NAT.
 *
 * Call after modules_init(), which is what loads the ice module and registers
 * the struct we mutate.  A build without the ice module returns ENOENT and the
 * SDK runs unchanged — there is no media-NAT to defer an INVITE behind.
 */
int bsdk_ice_shim_init(void)
{
	/* mnat_find returns const because callers have no business editing the
	 * registry entry; the object itself is a mutable static owned by the
	 * ice module, and interposing on it is exactly what we are here for. */
	struct mnat *m = (struct mnat *)mnat_find(baresip_mnatl(), "ice");

	if (!m)
		return ENOENT;

	/* echosdk_init after echosdk_shutdown re-runs module_init(), which
	 * re-registers the same static struct.  Installing twice would capture
	 * our own wrappers as the originals and recurse forever. */
	if (m->sessh == ice_sessh)
		return 0;

	real_sessh   = m->sessh;
	real_mediah  = m->mediah;
	real_updateh = m->updateh;
	real_attrh   = m->attrh;

	m->sessh   = ice_sessh;
	m->mediah  = ice_mediah;
	m->updateh = ice_updateh;
	m->attrh   = ice_attrh;
	s_mnat     = m;

	return 0;
}

void bsdk_ice_shim_close(void)
{
	if (!s_mnat)
		return;

	s_mnat->sessh   = real_sessh;
	s_mnat->mediah  = real_mediah;
	s_mnat->updateh = real_updateh;
	s_mnat->attrh   = real_attrh;
	s_mnat = NULL;

	real_sessh   = NULL;
	real_mediah  = NULL;
	real_updateh = NULL;
	real_attrh   = NULL;
}
