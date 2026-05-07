/**
 * @file dispatch.c  Consumer thread → re_main bridge
 *
 * All state-mutating public API calls go through bare_dispatch() or
 * bare_dispatch_sync(). These use re_thread_async_main() to post work to
 * the re_main event loop, ensuring SIP state is only mutated from re_main.
 *
 * bare_dispatch()      — fire-and-forget; returns immediately.
 * bare_dispatch_sync() — posts and blocks until work completes.
 */

#include "libbare_internal.h"

/* ── Async (fire-and-forget) ─────────────────────────────────────────────── */

typedef struct {
	bare_main_fn fn;
	void        *arg;
} dispatch_ctx_t;

/* re_async_work_h — runs on re_main thread */
static int dispatch_work(void *arg)
{
	dispatch_ctx_t *ctx = arg;
	ctx->fn(ctx->arg);
	mem_deref(ctx);
	return 0;
}

int bare_dispatch(bare_main_fn fn, void *arg)
{
	dispatch_ctx_t *ctx;

	if (!g_bare.initialized)
		return LIBBARE_ERR_STATE;

	ctx = mem_alloc(sizeof(*ctx), NULL);
	if (!ctx)
		return LIBBARE_ERR_NOMEM;

	ctx->fn  = fn;
	ctx->arg = arg;

	/* work runs on re_main; cb=NULL (no post-work callback needed) */
	int err = re_thread_async_main(dispatch_work, NULL, ctx);
	if (err)
		mem_deref(ctx);
	return err;
}

/* ── Synchronous ─────────────────────────────────────────────────────────── */

typedef struct {
	bare_main_fn fn;
	void        *arg;
	mtx_t        lock;
	cnd_t        done;
	bool         finished;
} sync_ctx_t;

static int sync_work(void *arg)
{
	sync_ctx_t *ctx = arg;
	ctx->fn(ctx->arg);
	mtx_lock(&ctx->lock);
	ctx->finished = true;
	cnd_signal(&ctx->done);
	mtx_unlock(&ctx->lock);
	/* do NOT mem_deref — caller owns ctx on its stack */
	return 0;
}

int bare_dispatch_sync(bare_main_fn fn, void *arg)
{
	sync_ctx_t ctx;
	int err;

	if (!g_bare.initialized)
		return LIBBARE_ERR_STATE;

	ctx.fn       = fn;
	ctx.arg      = arg;
	ctx.finished = false;
	mtx_init(&ctx.lock, mtx_plain);
	cnd_init(&ctx.done);

	err = re_thread_async_main(sync_work, NULL, &ctx);
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
