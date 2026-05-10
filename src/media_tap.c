/**
 * @file media_tap.c  PCM media tap via baresip aufilt
 *
 * Registers a global audio filter. On each audio frame (encode=TX, decode=RX),
 * looks up the call owning the stream and calls the consumer's tap callback
 * with raw int16_t PCM. The tap MUST be non-blocking.
 *
 * Note: fmt conversion — tap delivers S16LE only. If baresip uses a different
 * internal format, the frame is skipped (this is logged as a warning once).
 */

#include "baresdk_internal.h"
#include <rem_au.h>
#include <rem_aulevel.h>
#include <rem_auframe.h>

/* ── Filter state ────────────────────────────────────────────────────────── */

struct tap_enc_st {
	struct aufilt_enc_st base;
	const struct audio  *au;
};

struct tap_dec_st {
	struct aufilt_dec_st base;
	const struct audio  *au;
};

/* ── Filter callbacks ────────────────────────────────────────────────────── */

static int tap_encupd(struct aufilt_enc_st **stp, void **ctx,
                       const struct aufilt *af, struct aufilt_prm *prm,
                       const struct audio *au)
{
	(void)ctx; (void)af; (void)prm;
	struct tap_enc_st *st = mem_alloc(sizeof(*st), NULL);
	if (!st) return ENOMEM;
	st->au = au;
	*stp = (struct aufilt_enc_st *)st;
	return 0;
}

static int tap_decupd(struct aufilt_dec_st **stp, void **ctx,
                       const struct aufilt *af, struct aufilt_prm *prm,
                       const struct audio *au)
{
	(void)ctx; (void)af; (void)prm;
	struct tap_dec_st *st = mem_alloc(sizeof(*st), NULL);
	if (!st) return ENOMEM;
	st->au = au;
	*stp = (struct aufilt_dec_st *)st;
	return 0;
}

struct tap_find_ctx { const struct audio *au; struct baresdk_call *found; };

static void tap_find_fn(struct baresdk_call *lc, void *arg)
{
	struct tap_find_ctx *ctx = arg;
	if (!ctx->found && call_audio(lc->bc) == ctx->au)
		ctx->found = lc;
}

static struct baresdk_call *tap_find_call(const struct audio *au)
{
	struct tap_find_ctx ctx = {.au = au, .found = NULL};
	bsdk_call_foreach(tap_find_fn, &ctx);
	return ctx.found;
}

static int tap_encode(struct aufilt_enc_st *st, struct auframe *af)
{
	struct tap_enc_st *ts = (struct tap_enc_st *)st;
	if (af->fmt != AUFMT_S16LE) return 0;

	struct baresdk_call *lc = tap_find_call(ts->au);
	if (!lc) return 0;

	mtx_lock(&lc->tap_lock);
	baresdk_media_tap_cb_t cb = lc->tap_cb;
	void *ud = lc->tap_userdata;
	mtx_unlock(&lc->tap_lock);

	if (cb) {
		cb(lc, BARESDK_MEDIA_DIR_TX,
		   (const int16_t *)af->sampv,
		   af->sampc, af->srate, af->ch,
		   af->timestamp, ud);
	}
	bsdk_record_write_frame(lc, BARESDK_MEDIA_DIR_TX,
	                        (const int16_t *)af->sampv,
	                        af->sampc, af->srate, af->ch);
	return 0;
}

static int tap_decode(struct aufilt_dec_st *st, struct auframe *af)
{
	struct tap_dec_st *ts = (struct tap_dec_st *)st;
	if (af->fmt != AUFMT_S16LE) return 0;

	struct baresdk_call *lc = tap_find_call(ts->au);
	if (!lc) return 0;

	mtx_lock(&lc->tap_lock);
	baresdk_media_tap_cb_t cb = lc->tap_cb;
	void *ud = lc->tap_userdata;
	mtx_unlock(&lc->tap_lock);

	if (cb) {
		cb(lc, BARESDK_MEDIA_DIR_RX,
		   (const int16_t *)af->sampv,
		   af->sampc, af->srate, af->ch,
		   af->timestamp, ud);
	}
	bsdk_record_write_frame(lc, BARESDK_MEDIA_DIR_RX,
	                        (const int16_t *)af->sampv,
	                        af->sampc, af->srate, af->ch);
	return 0;
}

/* ── Filter registration ─────────────────────────────────────────────────── */

static struct aufilt s_tap_filter = {
	.name     = "baresdk_tap",
	.encupdh  = tap_encupd,
	.ench     = tap_encode,
	.decupdh  = tap_decupd,
	.dech     = tap_decode,
};

static bool s_tap_registered = false;

static void ensure_tap_registered(void)
{
	if (s_tap_registered) return;
	aufilt_register(baresip_aufiltl(), &s_tap_filter);
	s_tap_registered = true;
}

void bsdk_tap_global_reset(void)
{
	if (s_tap_registered) {
		aufilt_unregister(&s_tap_filter);
		s_tap_registered = false;
	}
}

/* ── Public API ──────────────────────────────────────────────────────────── */

typedef struct {
	struct baresdk_call    *lc;
	baresdk_media_tap_cb_t  cb;
	void                   *userdata;
} set_tap_ctx_t;

static void set_tap_fn(void *arg)
{
	set_tap_ctx_t *ctx = arg;
	if (ctx->cb)
		ensure_tap_registered();
	mtx_lock(&ctx->lc->tap_lock);
	ctx->lc->tap_cb       = ctx->cb;
	ctx->lc->tap_userdata = ctx->userdata;
	mtx_unlock(&ctx->lc->tap_lock);
}

int baresdk_call_set_media_tap(baresdk_call_handle_t call,
                                baresdk_media_tap_cb_t cb,
                                void *userdata)
{
	if (!call) return BARESDK_ERR_INVAL;
	set_tap_ctx_t ctx = {.lc = call, .cb = cb, .userdata = userdata};
	return bsdk_dispatch_sync(set_tap_fn, &ctx);
}
