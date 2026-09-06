/**
 * @file re_loop.c  re_main thread lifecycle
 *
 * Runs the re event loop on a dedicated thread. After libre_init() the global
 * re context (re_global) is set. The re_main thread uses it via the automatic
 * fallback in re_get() — no explicit re_thread_attach() needed.
 *
 * Stopping: re_cancel() alone cannot interrupt a blocking select() call.
 * We use an mqueue (whose pipe is registered with fd_listen) to wake the
 * select, then call re_cancel() so the loop exits on the next iteration.
 */

#include "voxsdk_internal.h"

static void wakeup_handler(int id, void *data, void *arg)
{
	(void)id; (void)data; (void)arg;
	re_cancel();
}

static int re_main_thread_fn(void *arg)
{
	(void)arg;
	re_main(NULL);  /* blocks until re_cancel() */
	return 0;
}

int vox_re_loop_start(void)
{
	/* Allocate the wakeup mqueue BEFORE starting the thread.
	 * mqueue_alloc registers a pipe with fd_listen on re_global so that
	 * mqueue_push() can interrupt a blocking select() call. */
	int err = mqueue_alloc(&g_vox.re_wakeup_mq, wakeup_handler, NULL);
	if (err)
		return err;

	int rc = thrd_create(&g_vox.re_thread, re_main_thread_fn, NULL);
	if (rc != thrd_success) {
		g_vox.re_wakeup_mq = mem_deref(g_vox.re_wakeup_mq);
		return ENOMEM;
	}
	g_vox.re_thread_running = true;
	return 0;
}

void vox_re_loop_stop(void)
{
	if (!g_vox.re_thread_running)
		return;

	/* Push to the wakeup mqueue — this writes to the pipe, which wakes up
	 * the blocking select() in the re_main loop.  The wakeup_handler then
	 * calls re_cancel() from within the re thread so polling=false is seen
	 * on the very next iteration. */
	mqueue_push(g_vox.re_wakeup_mq, 0, NULL);

	thrd_join(g_vox.re_thread, NULL);
	g_vox.re_wakeup_mq = mem_deref(g_vox.re_wakeup_mq);
	g_vox.re_thread_running = false;
}
