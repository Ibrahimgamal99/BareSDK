/**
 * @file dispatch.c  Consumer thread → re_main bridge
 *
 * All state-mutating public API calls go through bsdk_dispatch() or
 * bsdk_dispatch_sync(). These use re_thread_async_main() to post work to
 * the re_main event loop, ensuring SIP state is only mutated from re_main.
 *
 * IMPORTANT: re_thread_async_main(work, cb, arg) runs `work` on a libre
 * WORKER-POOL thread and only `cb` on the re_main loop (see re's
 * src/async/async.c: worker_thread vs queueh). The handler therefore goes
 * in the CB SLOT with work=NULL. Passing it as `work` looks fine in
 * Release builds — re's fd_listen() thread guard is compiled out — but
 * mutates the fd table and timer list concurrently with re_main, which
 * corrupts them exactly when fd numbers are recycled (e.g. a WSS connect
 * racing a just-destroyed account's socket teardown).
 *
 * bsdk_dispatch()      — fire-and-forget; returns immediately.
 * bsdk_dispatch_sync() — posts and blocks until work completes; runs the
 *                        function inline when already on the re thread
 *                        (blocking there would deadlock the loop).
 */

#include "baresdk_internal.h"

/* ── Async (fire-and-forget) ─────────────────────────────────────────────── */

typedef struct {
	bsdk_main_fn fn;
	void        *arg;
} dispatch_ctx_t;

/* re_async_h — runs on the re_main thread */
static void dispatch_cb(int err, void *arg)
{
	(void)err;
	dispatch_ctx_t *ctx = arg;
	ctx->fn(ctx->arg);
	mem_deref(ctx);
}

int bsdk_dispatch(bsdk_main_fn fn, void *arg)
{
	dispatch_ctx_t *ctx;

	if (!g_bsdk.initialized)
		return BARESDK_ERR_STATE;

	ctx = mem_alloc(sizeof(*ctx), NULL);
	if (!ctx)
		return BARESDK_ERR_NOMEM;

	ctx->fn  = fn;
	ctx->arg = arg;

	int err = re_thread_async_main(NULL, dispatch_cb, ctx);
	if (err)
		mem_deref(ctx);
	return err;
}

/* ── Synchronous ─────────────────────────────────────────────────────────── */

typedef struct {
	bsdk_main_fn fn;
	void        *arg;
	mtx_t        lock;
	cnd_t        done;
	bool         finished;
} sync_ctx_t;

/* re_async_h — runs on the re_main thread */
static void sync_cb(int err, void *arg)
{
	(void)err;
	sync_ctx_t *ctx = arg;
	ctx->fn(ctx->arg);
	mtx_lock(&ctx->lock);
	ctx->finished = true;
	cnd_signal(&ctx->done);
	mtx_unlock(&ctx->lock);
	/* do NOT mem_deref — caller owns ctx on its stack */
}

int bsdk_dispatch_sync(bsdk_main_fn fn, void *arg)
{
	sync_ctx_t ctx;
	int err;

	if (!g_bsdk.initialized)
		return BARESDK_ERR_STATE;

	/* Already on the re-loop thread (e.g. called from a timer or netmon
	 * handler): run inline — blocking here would deadlock the loop that
	 * must execute the callback.  Compare against our own spawned thread:
	 * re_thread_check() is NOT usable here because re->tid is stamped by
	 * libre_init(), which runs on the app thread in baresdk_init(). */
	if (g_bsdk.re_thread_running &&
	    thrd_equal(g_bsdk.re_thread, thrd_current())) {
		fn(arg);
		return 0;
	}

	ctx.fn       = fn;
	ctx.arg      = arg;
	ctx.finished = false;
	mtx_init(&ctx.lock, mtx_plain);
	cnd_init(&ctx.done);

	err = re_thread_async_main(NULL, sync_cb, &ctx);
	if (err)
		goto out;

	mtx_lock(&ctx.lock);
	while (!ctx.finished)
		cnd_wait(&ctx.done, &ctx.lock);
	mtx_unlock(&ctx.lock);

out:
	cnd_destroy(&ctx.done);
	mtx_destroy(&ctx.lock);
	return err;
}
