/**
 * @file audio.c  Audio device control and mute
 */

#include "libbare_internal.h"

/* ── libbare_audio_mute ──────────────────────────────────────────────────── */

typedef struct { struct libbare_call *lc; bool mute; int result; } mute_ctx_t;

static void mute_fn(void *arg)
{
	mute_ctx_t *ctx = arg;
	struct libbare_call *lc = ctx->lc;
	if (!lc->bc) { ctx->result = ENOENT; return; }

	struct audio *au = call_audio(lc->bc);
	if (!au) { ctx->result = ENOENT; return; }

	audio_mute(au, ctx->mute);
	ctx->result = 0;
}

int libbare_audio_mute(libbare_call_handle_t call, bool mute)
{
	if (!call) return LIBBARE_ERR_INVAL;
	mute_ctx_t ctx = {.lc = call, .mute = mute, .result = 0};
	int err = bare_dispatch_sync(mute_fn, &ctx);
	return err ? err : ctx.result;
}

/* ── libbare_audio_set_input_device ──────────────────────────────────────── */

typedef struct { const char *name; int result; } device_ctx_t;

static void set_input_fn(void *arg)
{
	device_ctx_t *ctx = arg;
	struct config *bc = conf_config();
	if (ctx->name)
		str_ncpy(bc->audio.src_dev, ctx->name,
		         sizeof(bc->audio.src_dev));
	else
		bc->audio.src_dev[0] = '\0';
	ctx->result = 0;
}

int libbare_audio_set_input_device(const char *name)
{
	device_ctx_t ctx = {.name = name, .result = 0};
	int err = bare_dispatch_sync(set_input_fn, &ctx);
	return err ? err : ctx.result;
}

static void set_output_fn(void *arg)
{
	device_ctx_t *ctx = arg;
	struct config *bc = conf_config();
	if (ctx->name)
		str_ncpy(bc->audio.play_dev, ctx->name,
		         sizeof(bc->audio.play_dev));
	else
		bc->audio.play_dev[0] = '\0';
	ctx->result = 0;
}

int libbare_audio_set_output_device(const char *name)
{
	device_ctx_t ctx = {.name = name, .result = 0};
	int err = bare_dispatch_sync(set_output_fn, &ctx);
	return err ? err : ctx.result;
}
