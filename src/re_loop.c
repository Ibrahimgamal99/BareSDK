/**
 * @file re_loop.c  re_main thread lifecycle
 *
 * Runs the re event loop on a dedicated thread. After libre_init() the global
 * re context (re_global) is set. The re_main thread uses it via the automatic
 * fallback in re_get() — no explicit re_thread_attach() needed.
 *
 * re_cancel() from any thread sets polling=false on re_global, which causes
 * re_main() to exit. That is our shutdown signal.
 */

#include "libbare_internal.h"

static int re_main_thread_fn(void *arg)
{
	(void)arg;
	re_main(NULL);  /* blocks until re_cancel() */
	return 0;
}

int bare_re_loop_start(void)
{
	int rc = thrd_create(&g_bare.re_thread, re_main_thread_fn, NULL);
	if (rc != thrd_success)
		return ENOMEM;
	g_bare.re_thread_running = true;
	return 0;
}

void bare_re_loop_stop(void)
{
	if (!g_bare.re_thread_running)
		return;
	re_cancel();
	thrd_join(g_bare.re_thread, NULL);
	g_bare.re_thread_running = false;
}
