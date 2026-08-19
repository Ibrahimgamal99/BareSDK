/**
 * @file reattach_test.c  Gate test for consumer-reattach + global codec names
 *
 * Covers the two things a host that loses its own runtime while the process
 * (and the SIP stack) stays up depends on — an Android headless Flutter engine
 * destroying the Dart isolate between push wakeups is the motivating case:
 *
 *   1. baresdk_init() on a live stack still returns BARESDK_ERR_ALREADY, and
 *      the recovery path works: baresdk_is_initialized() reports the stack is
 *      up, baresdk_set_event_handler() re-points delivery at the new consumer
 *      (the old callback goes quiet), and account/call enumeration hands the
 *      new consumer back the handles the dead one held.
 *   2. cfg.audio_codec_names[] — the global string codec list — actually
 *      reaches baresip, ordered, instead of being silently dropped.
 *
 * No SIP server needed: registration against a black-hole address produces
 * REG_STATE events, which is all the event traffic the test needs.
 *
 * Build (from repo root, after scripts/build-linux.sh):
 *   gcc -fsanitize=address -g -O1 -std=gnu11 \
 *       -Iinclude -Idist/linux/x86_64/include test/reattach_test.c \
 *       -Wl,--wrap=websock_connect \
 *       -Wl,--whole-archive dist/linux/x86_64/baresdk.a -Wl,--no-whole-archive \
 *       -lssl -lcrypto -lz -lpthread -lm -lresolv -ldl -lstdc++ -lpulse \
 *       -o test/reattach_test && ./test/reattach_test
 *
 * Exit 0 on success.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <unistd.h>
#include "../include/baresdk.h"

/* baresip/re headers — used to read back what the SDK told baresip about
 * codecs. White-box on purpose: the alternative is a live peer to negotiate
 * SDP with. */
#include <re.h>
#include <baresip.h>

#define AOR "sip:alice@192.0.2.1"

static int g_pass = 0;
static int g_fail = 0;

/* stdout, not stderr: bsdk_log_init() takes stderr over for SIP logging. */
#define CHECK(cond, ...) do { \
	if (cond) { g_pass++; } \
	else { g_fail++; printf("FAIL(line %d): ", __LINE__); \
	       printf(__VA_ARGS__); fflush(stdout); } \
} while (0)

/* ── Event handlers ──────────────────────────────────────────────────────── */

/* Handler A stands in for the consumer that installed cfg.event_cb and then
 * died; handler B for the one that comes back after. */
static atomic_int g_a_count = 0;
static atomic_int g_b_count = 0;
static void      *g_b_userdata_seen = NULL;

static void handler_a(const baresdk_event_t *ev, void *ud)
{
	(void)ud;
	atomic_fetch_add(&g_a_count, 1);
	baresdk_event_release(ev);   /* owned mode */
}

static void handler_b(const baresdk_event_t *ev, void *ud)
{
	atomic_fetch_add(&g_b_count, 1);
	g_b_userdata_seen = ud;
	baresdk_event_release(ev);
}

/* Deliberately over the 10 ms callback budget, to be caught mid-delivery. */
static atomic_bool g_slow_in_cb = false;

static void handler_slow(const baresdk_event_t *ev, void *ud)
{
	(void)ud;
	atomic_store(&g_slow_in_cb, true);
	usleep(300000);
	atomic_store(&g_slow_in_cb, false);
	baresdk_event_release(ev);
}

/* ── Enumeration collectors ──────────────────────────────────────────────── */

#define MAX_FOUND 8

typedef struct {
	baresdk_account_handle_t accts[MAX_FOUND];
	int                      count;
} acct_scan_t;

static void collect_acct(baresdk_account_handle_t acct, void *arg)
{
	acct_scan_t *scan = arg;
	if (scan->count < MAX_FOUND)
		scan->accts[scan->count] = acct;
	scan->count++;
}

typedef struct {
	int count;
} call_scan_t;

static void collect_call(baresdk_call_handle_t call, void *arg)
{
	call_scan_t *scan = arg;
	(void)call;
	scan->count++;
}

/* Produce event traffic on demand: an UNREGISTER/REGISTER pair is two
 * reg-state transitions, so it fires REG_STATE events without waiting on a
 * black-hole REGISTER to time out. */
static void poke_events(baresdk_account_handle_t acct)
{
	baresdk_account_unregister(acct);
	usleep(300000);
	baresdk_account_register(acct);
	usleep(700000);
}

/* ── Codec read-back ─────────────────────────────────────────────────────── */

/* Join the aucodec names baresip resolved for AOR into buf, e.g.
 * "PCMU,opus". Empty when the account or its codec list is missing. */
static void configured_codecs(char *buf, size_t sz)
{
	buf[0] = '\0';

	struct ua *ua = uag_find_aor(AOR);
	if (!ua) return;
	struct account *acc = ua_account(ua);
	if (!acc) return;
	struct list *l = account_aucodecl(acc);
	if (!l) return;

	struct le *le;
	for (le = list_head(l); le; le = le->next) {
		const struct aucodec *ac = le->data;
		if (!ac || !ac->name) continue;
		if (buf[0]) strncat(buf, ",", sz - strlen(buf) - 1);
		strncat(buf, ac->name, sz - strlen(buf) - 1);
	}
}

/* ── Test ────────────────────────────────────────────────────────────────── */

int main(void)
{
	baresdk_config_t cfg;
	baresdk_config_init(&cfg);

	cfg.transport            = BARESDK_TRANSPORT_UDP;
	cfg.log_level            = 0;
	cfg.event_cb             = handler_a;
	cfg.deliver_owned_events = true;
	/* Short retry so a failing REGISTER keeps producing events. */
	cfg.reg_retry_initial_ms = 500;
	cfg.reg_retry_max_ms     = 500;
	cfg.reg_retry_backoff    = 1.0f;

	/* The global string codec list — ordered, µ-law first. Both names
	 * resolve in the desktop module profile, so a correct marshal is
	 * visible as exactly this order. */
	strcpy(cfg.audio_codec_names[0], "ulaw");
	strcpy(cfg.audio_codec_names[1], "opus");
	cfg.audio_codec_name_count = 2;

	CHECK(!baresdk_is_initialized(),
	      "is_initialized() true before init\n");
	CHECK(baresdk_set_event_handler(handler_b, NULL, true) ==
	              BARESDK_ERR_STATE,
	      "set_event_handler() before init did not report ERR_STATE\n");

	int err = baresdk_init(&cfg);
	if (err) {
		fprintf(stderr, "baresdk_init: %d\n", err);
		return 1;
	}

	CHECK(baresdk_is_initialized(),
	      "is_initialized() false on a live stack\n");

	/* A second init must still be refused — reattach is opt-in, never an
	 * implicit re-configure of a running stack. */
	CHECK(baresdk_init(&cfg) == BARESDK_ERR_ALREADY,
	      "second baresdk_init() did not return ERR_ALREADY\n");

	baresdk_account_handle_t acct = NULL;
	baresdk_account_config_t acfg;
	memset(&acfg, 0, sizeof(acfg));
	acfg.uri       = "alice@192.0.2.1";
	acfg.password  = "secret";
	acfg.transport = BARESDK_TRANSPORT_UDP;

	err = baresdk_account_create(&acfg, &acct);
	if (err) {
		fprintf(stderr, "account_create: %d\n", err);
		return 1;
	}
	baresdk_account_register(acct);

	/* ── 2. Global codec names reached baresip, in order ─────────────── */
	char codecs[256];
	configured_codecs(codecs, sizeof(codecs));
	CHECK(strncmp(codecs, "PCMU,opus", 9) == 0,
	      "global audio_codec_names not applied: got \"%s\", "
	      "expected to start \"PCMU,opus\"\n", codecs);

	/* Let handler A see some registration traffic. */
	sleep(2);
	int a_before = atomic_load(&g_a_count);
	CHECK(a_before > 0, "handler A received no events\n");

	/* ── 1. Reattach: the isolate died, a new one takes over ─────────── */
	int dummy_ud = 42;
	CHECK(baresdk_set_event_handler(handler_b, &dummy_ud, true) ==
	              BARESDK_OK,
	      "set_event_handler() on a live stack failed\n");

	/* Enumeration hands the new consumer the handles the dead one had. */
	acct_scan_t ascan = { .count = 0 };
	baresdk_account_foreach(collect_acct, &ascan);
	CHECK(ascan.count == 1, "account_foreach found %d accounts, want 1\n",
	      ascan.count);
	CHECK(ascan.count == 1 && ascan.accts[0] == acct,
	      "account_foreach returned a handle that is not the live account\n");

	char aor[256] = "";
	CHECK(baresdk_account_get_aor(acct, aor, sizeof(aor)) == BARESDK_OK,
	      "account_get_aor failed\n");
	CHECK(strcmp(aor, AOR) == 0,
	      "account_get_aor = \"%s\", want \"%s\"\n", aor, AOR);

	char tiny[4] = "xx";
	CHECK(baresdk_account_get_aor(acct, tiny, sizeof(tiny)) ==
	              BARESDK_ERR_NOMEM,
	      "account_get_aor into a too-small buffer did not report NOMEM\n");
	CHECK(tiny[0] == '\0',
	      "account_get_aor left a truncated string in a too-small buffer\n");
	CHECK(baresdk_account_get_aor(NULL, aor, sizeof(aor)) ==
	              BARESDK_ERR_INVAL,
	      "account_get_aor(NULL) did not report ERR_INVAL\n");

	baresdk_reg_state_t rs = baresdk_account_get_reg_state(acct);
	CHECK(rs == BARESDK_REG_REGISTERING || rs == BARESDK_REG_FAILED ||
	      rs == BARESDK_REG_RECONNECTING,
	      "reg_state = %d, want REGISTERING, RECONNECTING or FAILED for a "
	      "black-hole server\n", (int)rs);

	/* No calls, but the call side of the reattach path must still answer
	 * sanely rather than trip over an empty list. */
	call_scan_t cscan = { .count = 0 };
	baresdk_call_foreach(collect_call, &cscan);
	CHECK(cscan.count == 0, "call_foreach found %d calls, want 0\n",
	      cscan.count);
	CHECK(baresdk_call_get_account(NULL) == NULL,
	      "call_get_account(NULL) not NULL\n");
	CHECK(baresdk_call_get_state(NULL) == BARESDK_CALL_ENDED,
	      "call_get_state(NULL) not ENDED\n");

	/* B now gets the traffic; A must be quiet from here on.  A black-hole
	 * REGISTER goes silent for a full timer B, so poke the stack for
	 * events instead of waiting on retries. */
	atomic_store(&g_a_count, 0);
	poke_events(acct);
	CHECK(atomic_load(&g_b_count) > 0,
	      "handler B received no events after reattach\n");
	CHECK(atomic_load(&g_a_count) == 0,
	      "handler A still received %d events after reattach\n",
	      atomic_load(&g_a_count));
	CHECK(g_b_userdata_seen == &dummy_ud,
	      "handler B got the wrong userdata\n");

	/* ── The swap waits out a delivery already inside the old handler ─
	 * Consumers free the old callback as soon as this returns, so a swap
	 * that races an in-flight delivery is a use-after-free. */
	CHECK(baresdk_set_event_handler(handler_slow, NULL, true) == BARESDK_OK,
	      "set_event_handler(handler_slow) failed\n");
	baresdk_account_unregister(acct);
	baresdk_account_register(acct);
	for (int i = 0; i < 3000 && !atomic_load(&g_slow_in_cb); i++)
		usleep(1000);
	CHECK(atomic_load(&g_slow_in_cb),
	      "never caught the event thread inside the handler\n");

	CHECK(baresdk_set_event_handler(handler_b, &dummy_ud, true) ==
	              BARESDK_OK,
	      "set_event_handler() during a delivery failed\n");
	CHECK(!atomic_load(&g_slow_in_cb),
	      "set_event_handler() returned while the old handler was still "
	      "running — freeing it here would be a use-after-free\n");

	/* ── Parked delivery: NULL handler drops events, leaks nothing ───── */
	CHECK(baresdk_set_event_handler(NULL, NULL, false) == BARESDK_OK,
	      "set_event_handler(NULL) failed\n");
	atomic_store(&g_b_count, 0);
	poke_events(acct);
	CHECK(atomic_load(&g_b_count) == 0,
	      "handler B still received events after being parked\n");

	baresdk_account_destroy(acct);
	baresdk_shutdown();

	CHECK(!baresdk_is_initialized(),
	      "is_initialized() true after shutdown\n");
	CHECK(baresdk_set_event_handler(handler_a, NULL, true) ==
	              BARESDK_ERR_STATE,
	      "set_event_handler() after shutdown did not report ERR_STATE\n");

	printf("\n%d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
