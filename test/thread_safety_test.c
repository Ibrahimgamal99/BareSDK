/**
 * @file thread_safety_test.c  N-thread concurrent API hammer test
 *
 * Spawns NTHREADS threads. Each thread repeatedly calls:
 *   - voxsdk_call_invite() + voxsdk_call_hangup()
 *   - voxsdk_audio_mute() toggle
 *   - voxsdk_call_get_stats()
 *
 * Build with: -fsanitize=thread
 * Pass: no crash, no TSan output, exit 0.
 * The test is intentionally short (1 second) so it fits in CI.
 *
 * Note: calls will fail (no server) but the point is thread-safety,
 * not call establishment.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <threads.h>
#include <time.h>
#include "../include/voxsdk.h"

#define NTHREADS  8
#define RUN_MS    1000  /* 1 second per thread */

static voxsdk_account_handle_t g_acct = NULL;
static atomic_int g_stop = 0;

static void event_handler(const voxsdk_event_t *ev, void *ud)
{
	(void)ev; (void)ud;
}

static int worker(void *arg)
{
	(void)arg;

	struct timespec ts_end;
	timespec_get(&ts_end, TIME_UTC);
	ts_end.tv_sec += RUN_MS / 1000;

	while (!atomic_load(&g_stop)) {
		struct timespec now;
		timespec_get(&now, TIME_UTC);
		if (now.tv_sec > ts_end.tv_sec ||
		    (now.tv_sec == ts_end.tv_sec &&
		     now.tv_nsec >= ts_end.tv_nsec))
			break;

		/* Invite (will fail — server not present, that's fine) */
		voxsdk_call_handle_t call = NULL;
		voxsdk_call_invite(g_acct, "sip:test@127.0.0.1", &call);

		if (call) {
			/* Toggle mute */
			voxsdk_audio_mute(call, true);
			voxsdk_audio_mute(call, false);

			/* Get stats (will return error — call not established) */
			voxsdk_ev_media_stats_t stats;
			voxsdk_call_get_stats(call, &stats);

			voxsdk_call_hangup(call);
		}

		/* Tiny sleep to avoid spinning */
		struct timespec sl = {0, 1000000}; /* 1 ms */
		thrd_sleep(&sl, NULL);
	}

	return 0;
}

int main(void)
{
	voxsdk_config_t cfg;
	voxsdk_config_init(&cfg);
	cfg.event_cb  = event_handler;
	cfg.log_level = -1; /* suppress all */

	int err = voxsdk_init(&cfg);
	if (err) {
		fprintf(stderr, "voxsdk_init failed: %d\n", err);
		return 1;
	}

	voxsdk_account_config_t acfg = {
		.aor       = "sip:test@127.0.0.1",
		.auth_pass = "test",
	};
	err = voxsdk_account_create(&acfg, &g_acct);
	if (err) {
		fprintf(stderr, "account_create failed: %d\n", err);
		voxsdk_shutdown();
		return 1;
	}

	/* Spawn threads */
	thrd_t threads[NTHREADS];
	for (int i = 0; i < NTHREADS; i++)
		thrd_create(&threads[i], worker, NULL);

	/* Wait */
	for (int i = 0; i < NTHREADS; i++)
		thrd_join(threads[i], NULL);

	voxsdk_shutdown();
	printf("Thread safety test passed\n");
	return 0;
}
