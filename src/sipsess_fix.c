/**
 * @file sipsess_fix.c  Hanging up an answered incoming call must send the BYE
 *
 * Symptom: the app hangs up an incoming call, the local side tears down and
 * reports ENDED — and nothing goes on the wire.  The caller stays on a live
 * call.  When they eventually hang up themselves, their BYE arrives at a
 * dialog we have already dismantled and we answer 481 Call Does Not Exist.
 * Outgoing calls are unaffected.
 *
 * ── Why ─────────────────────────────────────────────────────────────────────
 *
 * libre only sends the BYE from the session destructor, and only if it gets
 * that far (re/src/sipsess/sess.c):
 *
 *     switch (sess->terminated) {
 *     case 0:
 *             if (termwait(sess))     // pending transactions? defer.
 *                     return;
 *     case 1:
 *             if (terminate(sess))    // <- the BYE lives in here
 *                     return;
 *     }
 *
 * and termwait() defers while `sess->replyl` is not empty.
 *
 * A UAS that rings before answering puts *two* entries on that list: both
 * sipsess_reply_1xx() (our 180) and sipsess_reply_2xx() (our 200) do
 * list_append(&sess->replyl, ...) stamped with the same INVITE CSeq.  The ACK
 * is supposed to clear them, but sipsess_reply_ack() matches with
 * list_apply(), which stops at the first hit, and frees exactly one:
 *
 *     reply = list_ledata(list_apply(&sess->replyl, false, cmp_handler, msg));
 *     if (!reply) return ENOENT;
 *     mem_deref(reply);
 *
 * cmp_handler compares only `msg->cseq.num == reply->seq`, so both entries
 * match and the second one is left behind for the life of the call.  At
 * hangup termwait() sees it, takes a reference, and returns — the destructor
 * never reaches terminate(), and no BYE is sent.  It is not lost forever: the
 * leftover reply's 64*T1 timer fires 32 s later and the destructor runs again,
 * so the BYE is merely 32 seconds late, which for the caller is the same as
 * never.
 *
 * A UAC sends no replies at all, so its replyl is empty and terminate() runs
 * immediately — which is exactly why outgoing hangups always worked.
 *
 * ── The fix ─────────────────────────────────────────────────────────────────
 *
 * Drain every reply the ACK matches instead of just the first.  Removing the
 * 200's entry on ACK is the intended semantic anyway: that entry owns the
 * retransmission timer for the 200, which must stop once the ACK is in.
 *
 * Interposed with --wrap rather than patched into libre, for the same reason
 * ws_path.c does it: third_party/ is fetched at a pinned revision and carries
 * no patches.  See the note there about platforms with no --wrap.
 */

#include "baresdk_internal.h"

/* --wrap is a GNU ld feature.  Compiling this anywhere else leaves an
 * unresolved __real_sipsess_reply_ack in the archive, and every consumer that
 * links it fails — which is exactly what happened to the Linux build when the
 * flag was added to CMakeLists.txt but not to scripts/build-linux.sh.  Guard
 * the translation unit rather than trusting three separate link sites to stay
 * in step; on a platform without --wrap this file is empty and the BYE bug
 * described above is still present there.
 *
 * The flag has to be passed wherever the archive is linked:
 *   - CMakeLists.txt        (shared library, used by the Android build)
 *   - scripts/build-linux.sh (desktop Linux .so) */
#if defined(__linux__) || defined(__ANDROID__)

/* Private to libre (src/sipsess/sipsess.h), so mirror the declaration — the
 * same approach baresdk_internal.h already takes for baresip internals. */
struct sipsess;

int __real_sipsess_reply_ack(struct sipsess *sess, const struct sip_msg *msg);
int __wrap_sipsess_reply_ack(struct sipsess *sess, const struct sip_msg *msg);

int __wrap_sipsess_reply_ack(struct sipsess *sess, const struct sip_msg *msg)
{
	int err;

	err = __real_sipsess_reply_ack(sess, msg);
	if (err)
		return err;   /* no match — a retransmitted ACK, nothing to do */

	/* Drain the rest.  Each successful call unlinks one entry, and the list
	 * is short (one provisional plus the final), but bound the loop anyway:
	 * a silent infinite loop here would hang the re thread, and every SIP
	 * message would stop with it. */
	int extra = 0;
	for (int i = 0; i < 16; i++) {
		if (__real_sipsess_reply_ack(sess, msg))
			break;
		++extra;
	}

	debug("baresdk/sipsess: ACK cleared %d reply record(s)\n", extra + 1);
	return 0;
}

/* ── Why the BYE did not go out ──────────────────────────────────────────────
 *
 * Everything above is only half the story: the session destructor reaches
 * sipsess_bye() only if termwait() found nothing pending, and if it does get
 * there, terminate() throws the return value away:
 *
 *     if (sipsess_bye(sess, true))
 *             return false;      // no BYE, no log, no event
 *
 * So a BYE can fail to leave for three different reasons and every one of them
 * is silent — which is why diagnosing this needed a packet capture from the
 * far end rather than anything the SDK reported.  Say something instead.
 *
 * This is a diagnostic, not a fix: it does not make the BYE go out, it makes
 * the failure attributable.  A call that ends with no "sipsess_bye" line at all
 * never reached terminate(), which means termwait() is still holding the
 * session for a pending transaction. */
int __real_sipsess_bye(struct sipsess *sess, bool reset_ls);
int __wrap_sipsess_bye(struct sipsess *sess, bool reset_ls);

int __wrap_sipsess_bye(struct sipsess *sess, bool reset_ls)
{
	int err = __real_sipsess_bye(sess, reset_ls);

	if (err) {
		warning("baresdk/sipsess: BYE could not be sent (%m) — the peer "
		        "will stay on the call until it gives up on its own\n",
		        err);
	}
	else {
		debug("baresdk/sipsess: BYE queued\n");
	}

	return err;
}

#endif /* __linux__ || __ANDROID__ */
