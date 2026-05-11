/**
 * @file audio.c  Audio device control, mute, and runtime quality settings
 */

#include "baresdk_internal.h"
#include <re_udp.h>


/* ── baresdk_audio_mute ──────────────────────────────────────────────────── */

typedef struct { struct baresdk_call *lc; bool mute; int result; } mute_ctx_t;

static void mute_fn(void *arg)
{
	mute_ctx_t *ctx = arg;
	struct baresdk_call *lc = ctx->lc;
	if (!lc->bc) { ctx->result = ENOENT; return; }

	struct audio *au = call_audio(lc->bc);
	if (!au) { ctx->result = ENOENT; return; }

	audio_mute(au, ctx->mute);
	ctx->result = 0;
}

int baresdk_audio_mute(baresdk_call_handle_t call, bool mute)
{
	if (!call) return BARESDK_ERR_INVAL;
	mute_ctx_t ctx = {.lc = call, .mute = mute, .result = 0};
	int err = bsdk_dispatch_sync(mute_fn, &ctx);
	return err ? err : ctx.result;
}

/* ── baresdk_audio_mute_rx ───────────────────────────────────────────────── */

static void mute_rx_fn(void *arg)
{
	mute_ctx_t *ctx = arg;
	struct baresdk_call *lc = ctx->lc;
	if (!lc->bc) { ctx->result = ENOENT; return; }

	struct audio *au = call_audio(lc->bc);
	if (!au) { ctx->result = ENOENT; return; }

	struct stream *strm = audio_strm(au);
	if (!strm) { ctx->result = ENOENT; return; }

	ctx->result = stream_enable_rx(strm, !ctx->mute);
}

int baresdk_audio_mute_rx(baresdk_call_handle_t call, bool mute)
{
	if (!call) return BARESDK_ERR_INVAL;
	mute_ctx_t ctx = {.lc = call, .mute = mute, .result = 0};
	int err = bsdk_dispatch_sync(mute_rx_fn, &ctx);
	return err ? err : ctx.result;
}

/* ── baresdk_audio_set_input_device ──────────────────────────────────────── */

typedef struct { const char *name; int result; } device_ctx_t;

typedef struct {
	struct baresdk_call *lc;
	const char *mod;
	const char *dev;
	bool        is_input;
} update_call_dev_ctx_t;

static void update_call_device(struct baresdk_call *lc, void *arg)
{
	update_call_dev_ctx_t *ctx = arg;
	if (lc->state != BARESDK_CALL_ESTABLISHED || !lc->bc)
		return;
	struct audio *au = call_audio(lc->bc);
	if (!au)
		return;
	if (ctx->is_input)
		audio_set_source(au, ctx->mod, ctx->dev);
	else
		audio_set_player(au, ctx->mod, ctx->dev);
}

static void set_input_fn(void *arg)
{
	device_ctx_t *ctx = arg;
	struct config *bc = conf_config();
	if (ctx->name)
		str_ncpy(bc->audio.src_dev, ctx->name,
		         sizeof(bc->audio.src_dev));
	else
		bc->audio.src_dev[0] = '\0';

	/* Also switch any established calls to the new device */
	update_call_dev_ctx_t uctx = {
		.mod      = bc->audio.src_mod,
		.dev      = bc->audio.src_dev,
		.is_input = true,
	};
	bsdk_call_foreach(update_call_device, &uctx);
	ctx->result = 0;
}

int baresdk_audio_set_input_device(const char *name)
{
	device_ctx_t ctx = {.name = name, .result = 0};
	int err = bsdk_dispatch_sync(set_input_fn, &ctx);
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

	/* Also switch any established calls to the new device */
	update_call_dev_ctx_t uctx = {
		.mod      = bc->audio.play_mod,
		.dev      = bc->audio.play_dev,
		.is_input = false,
	};
	bsdk_call_foreach(update_call_device, &uctx);
	ctx->result = 0;
}

int baresdk_audio_set_output_device(const char *name)
{
	device_ctx_t ctx = {.name = name, .result = 0};
	int err = bsdk_dispatch_sync(set_output_fn, &ctx);
	return err ? err : ctx.result;
}

/* ── Audio device enumeration ────────────────────────────────────────────── */

/* Both struct ausrc and struct auplay start with: le, name, dev_list.
 * Use this layout to read dev_list without casting to the full type. */
struct audio_mod_hdr {
	struct le    le;
	const char  *name;
	struct list  dev_list;
};

typedef struct {
	baresdk_audio_device_t *devices;
	int                     max_count;
	int                     count;
	bool                    is_input;
} list_dev_ctx_t;

/* Called per mediadev entry within a module */
static bool fill_mediadev(struct le *le, void *arg)
{
	list_dev_ctx_t *ctx = arg;
	if (ctx->count >= ctx->max_count)
		return true;  /* stop */
	struct mediadev *md = le->data;
	baresdk_audio_device_t *d = &ctx->devices[ctx->count++];
	str_ncpy(d->name, md->name, sizeof(d->name));
	d->description[0] = '\0';
	d->is_default = ctx->is_input ? md->src.is_default : md->play.is_default;
	return false;  /* continue */
}

/* Called per audio module (ausrc or auplay) */
static bool fill_module_devices(struct le *le, void *arg)
{
	list_dev_ctx_t *ctx = arg;
	struct audio_mod_hdr *mod = le->data;
	if (list_isempty(&mod->dev_list)) {
		/* Module registered but no enumerated devices yet — expose the
		 * module name itself as a usable device identifier. */
		if (ctx->count < ctx->max_count) {
			baresdk_audio_device_t *d = &ctx->devices[ctx->count++];
			str_ncpy(d->name, mod->name, sizeof(d->name));
			d->description[0] = '\0';
			d->is_default = true;  /* single entry = default */
		}
	} else {
		list_apply(&mod->dev_list, false, fill_mediadev, ctx);
	}
	return ctx->count >= ctx->max_count;  /* stop if full */
}

typedef struct {
	baresdk_audio_device_t *devices;
	int                     max_count;
	int                     result;
	bool                    is_input;
} enum_dev_ctx_t;

static void enum_input_fn(void *arg)
{
	enum_dev_ctx_t *ctx = arg;
	list_dev_ctx_t lctx = {
		.devices   = ctx->devices,
		.max_count = ctx->max_count,
		.count     = 0,
		.is_input  = true,
	};
	list_apply(baresip_ausrcl(), false, fill_module_devices, &lctx);
	ctx->result = lctx.count;
}

static void enum_output_fn(void *arg)
{
	enum_dev_ctx_t *ctx = arg;
	list_dev_ctx_t lctx = {
		.devices   = ctx->devices,
		.max_count = ctx->max_count,
		.count     = 0,
		.is_input  = false,
	};
	list_apply(baresip_auplayl(), false, fill_module_devices, &lctx);
	ctx->result = lctx.count;
}

int baresdk_audio_list_input_devices(baresdk_audio_device_t *devices, int max_count)
{
	if (!devices || max_count <= 0) return BARESDK_ERR_INVAL;
	enum_dev_ctx_t ctx = {
		.devices   = devices,
		.max_count = max_count,
		.result    = 0,
		.is_input  = true,
	};
	int err = bsdk_dispatch_sync(enum_input_fn, &ctx);
	return err ? err : ctx.result;
}

int baresdk_audio_list_output_devices(baresdk_audio_device_t *devices, int max_count)
{
	if (!devices || max_count <= 0) return BARESDK_ERR_INVAL;
	enum_dev_ctx_t ctx = {
		.devices   = devices,
		.max_count = max_count,
		.result    = 0,
		.is_input  = false,
	};
	int err = bsdk_dispatch_sync(enum_output_fn, &ctx);
	return err ? err : ctx.result;
}

/* ── Runtime audio processing toggles ───────────────────────────────────── */

static void set_filter_fn(void *arg)
{
	struct { const char *name; bool enable; } *ctx = arg;
	aufilt_enable(baresip_aufiltl(), ctx->name, ctx->enable);
}

void baresdk_set_aec(bool enable)
{
	struct { const char *name; bool enable; } ctx = { "bsdk_aec", enable };
	bsdk_dispatch_sync(set_filter_fn, &ctx);
	g_bsdk.cfg.aec = enable;
}

void baresdk_set_ns(bool enable)
{
	struct { const char *name; bool enable; } ctx = { "bsdk_ns", enable };
	bsdk_dispatch_sync(set_filter_fn, &ctx);
	g_bsdk.cfg.ns = enable;
}

void baresdk_set_agc(bool enable)
{
	struct { const char *name; bool enable; } ctx = { "bsdk_agc", enable };
	bsdk_dispatch_sync(set_filter_fn, &ctx);
	g_bsdk.cfg.agc = enable;
}

/* ── Per-call DSCP ───────────────────────────────────────────────────────── */

typedef struct {
	struct baresdk_call *lc;
	uint8_t              dscp;
	int                  result;
} dscp_ctx_t;

static void set_dscp_rtp_fn(void *arg)
{
	dscp_ctx_t *ctx = arg;
	if (!ctx->lc->bc) { ctx->result = ENOENT; return; }

	struct audio *au = call_audio(ctx->lc->bc);
	if (!au)          { ctx->result = ENOENT; return; }

	struct stream *strm = audio_strm(au);
	if (!strm)        { ctx->result = ENOENT; return; }

	struct rtp_sock *rs = stream_rtp_sock(strm);
	if (!rs)          { ctx->result = ENOENT; return; }

	/* rtp_sock() returns the underlying struct udp_sock* as void* */
	struct udp_sock *us = rtp_sock(rs);
	if (!us)          { ctx->result = ENOENT; return; }

	ctx->result = udp_settos(us, ctx->dscp);
}

int baresdk_call_set_dscp_rtp(baresdk_call_handle_t call, uint8_t dscp)
{
	if (!call) return BARESDK_ERR_INVAL;
	dscp_ctx_t ctx = { .lc = call, .dscp = dscp, .result = 0 };
	int err = bsdk_dispatch_sync(set_dscp_rtp_fn, &ctx);
	return err ? err : ctx.result;
}

/* ── Global jitter buffer bounds ─────────────────────────────────────────── */

static void set_jbuf_fn(void *arg)
{
	uint32_t *bounds = arg; /* [0]=min, [1]=max */
	struct config *c = conf_config();
	c->avt.audio.jbtype       = JBUF_ADAPTIVE;
	c->avt.audio.jbuf_del.min = bounds[0];
	c->avt.audio.jbuf_del.max = bounds[1] ? bounds[1] : 150u;
}

void baresdk_set_jitter_buffer(uint32_t min_ms, uint32_t max_ms)
{
	uint32_t bounds[2] = { min_ms, max_ms };
	bsdk_dispatch_sync(set_jbuf_fn, bounds);
	g_bsdk.cfg.jitter_buffer_min_ms = min_ms;
	g_bsdk.cfg.jitter_buffer_max_ms = max_ms;
}
