/**
 * @file stats.c  RTCP stats polling timer + MOS calculation
 *
 * A tmr fires on re_main every cfg->stats_interval_ms. For each active call
 * it reads RTCP stats from the audio stream and posts BARESDK_EV_MEDIA_STATS.
 *
 * E-model MOS (ITU-T G.107):
 *   R  = 93.2 - Id - Ie
 *   Id = delay impairment: 0.024·RTT + 0.11·(RTT-177.3)·[RTT>177.3]
 *   Ie = equipment impairment: 7 + 30·ln(1 + 15·loss%)
 *   MOS = 1 + 0.035·R + 7e-6·R·(R-60)·(100-R)   (R clamped [0,100])
 *
 * Simplified MOS:
 *   MOS = 4.5 - 0.9·loss_pct/100 - 0.04·jitter_ms/100 - 0.002·RTT
 */

#include <math.h>
#include "baresdk_internal.h"

/* ── MOS calculations ───────────────────────────────────────────────────── */

static float mos_emodel(float loss_pct, float jitter_ms, float rtt_ms)
{
	(void)jitter_ms;
	float Id = 0.024f * rtt_ms
	         + 0.11f * (rtt_ms - 177.3f) * (rtt_ms > 177.3f ? 1.f : 0.f);
	float Ie = 7.0f + 30.0f * logf(1.0f + 15.0f * loss_pct / 100.0f);
	float R  = 93.2f - Id - Ie;
	if (R < 0.f)   R = 0.f;
	if (R > 100.f) R = 100.f;
	return 1.0f + 0.035f * R + 7e-6f * R * (R - 60.f) * (100.f - R);
}

static float mos_simplified(float loss_pct, float jitter_ms, float rtt_ms)
{
	float m = 4.5f
	        - 0.9f * (loss_pct / 100.f)
	        - 0.04f * (jitter_ms / 100.f)
	        - 0.002f * rtt_ms;
	if (m < 1.0f) m = 1.0f;
	if (m > 4.5f) m = 4.5f;
	return m;
}

static float calc_mos(float loss_pct, float jitter_ms, float rtt_ms,
                       baresdk_mos_method_t method)
{
	if (method == BARESDK_MOS_SIMPLIFIED)
		return mos_simplified(loss_pct, jitter_ms, rtt_ms);
	return mos_emodel(loss_pct, jitter_ms, rtt_ms);
}

static float calc_mos_cq(float mos_lq, float rtt_ms)
{
	float penalty = 0.0f;
	if (rtt_ms > 200.f)
		penalty = 0.1f * (rtt_ms - 200.f) / 100.f;
	float cq = mos_lq - penalty;
	if (cq < 1.0f) cq = 1.0f;
	return cq;
}

/* ── Per-call stats collection ───────────────────────────────────────────── */

static void collect_call_stats(struct baresdk_call *lc)
{
	if (!lc->bc)
		return;

	struct audio *au = call_audio(lc->bc);
	if (!au)
		return;

	struct stream *strm = audio_strm(au);
	if (!strm)
		return;

	const struct rtcp_stats *rs = stream_rtcp_stats(strm);
	if (!rs)
		return;

	uint32_t pkts_tx = stream_metric_get_tx_n_packets(strm);
	uint32_t pkts_rx = stream_metric_get_rx_n_packets(strm);
	uint32_t bw_tx   = stream_metric_get_tx_bitrate(strm) / 1000;
	uint32_t bw_rx   = stream_metric_get_rx_bitrate(strm) / 1000;

	float rtt_ms    = (float)rs->rtt / 1000.f;     /* us → ms */
	float jitter_ms = (float)rs->rx.jit / 1000.f;  /* us → ms */

	int total_tx = (int)rs->tx.sent + (rs->tx.lost > 0 ? rs->tx.lost : 0);
	float loss_pct = 0.f;
	if (total_tx > 0)
		loss_pct = (float)rs->tx.lost * 100.f / (float)total_tx;
	if (loss_pct < 0.f) loss_pct = 0.f;

	float mos = calc_mos(loss_pct, jitter_ms, rtt_ms,
	                      g_bsdk.cfg.mos_method);
	float mos_cq_val = calc_mos_cq(mos, rtt_ms);

	/* Get codec name */
	const char *codec_name = NULL;
	const struct aucodec *ac = audio_codec(au, true);
	if (ac)
		codec_name = ac->name;

	struct baresdk_queued_event *qev = mem_alloc(sizeof(*qev), NULL);
	if (!qev)
		return;
	memset(qev, 0, sizeof(*qev));

	baresdk_ev_media_stats_t *s = &qev->ev.u.stats;
	qev->ev.type         = BARESDK_EV_MEDIA_STATS;
	s->call              = lc;
	s->packets_sent      = pkts_tx;
	s->packets_received  = pkts_rx;
	s->packets_lost      = (uint32_t)(rs->tx.lost > 0 ? rs->tx.lost : 0);
	s->loss_pct          = loss_pct;
	s->jitter_ms         = jitter_ms;
	s->rtt_ms            = rtt_ms;
	s->mos_lq            = mos;
	s->mos_cq            = mos_cq_val;
	s->mos_method        = g_bsdk.cfg.mos_method;
	s->bandwidth_kbps_tx = bw_tx;
	s->bandwidth_kbps_rx = bw_rx;

	if (codec_name) {
		str_ncpy(qev->buf, codec_name, sizeof(qev->buf));
		s->codec_name = qev->buf;
		s->codec_clock_rate = ac ? ac->crate : 0;
	}

	mtx_lock(&g_bsdk.ev_lock);
	list_append(&g_bsdk.ev_queue, &qev->le, qev);
	cnd_signal(&g_bsdk.ev_cond);
	mtx_unlock(&g_bsdk.ev_lock);
}

/* ── Timer handler (re_main thread) ─────────────────────────────────────── */

static void stats_visit(struct baresdk_call *lc, void *arg)
{
	(void)arg;
	if (lc->state == BARESDK_CALL_ESTABLISHED)
		collect_call_stats(lc);
}

static void stats_timer_handler(void *arg)
{
	(void)arg;
	bsdk_call_foreach(stats_visit, NULL);
	tmr_start(&g_bsdk.stats_tmr, g_bsdk.cfg.stats_interval_ms,
	          stats_timer_handler, NULL);
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

int bsdk_stats_init(void)
{
	if (!g_bsdk.cfg.stats_interval_ms)
		return 0;

	tmr_init(&g_bsdk.stats_tmr);
	tmr_start(&g_bsdk.stats_tmr, g_bsdk.cfg.stats_interval_ms,
	          stats_timer_handler, NULL);
	return 0;
}

void bsdk_stats_close(void)
{
	tmr_cancel(&g_bsdk.stats_tmr);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

typedef struct {
	struct baresdk_call      *lc;
	baresdk_ev_media_stats_t *out;
	int                       result;
} getstats_ctx_t;

static void getstats_fn(void *arg)
{
	getstats_ctx_t *ctx = arg;
	struct baresdk_call *lc = ctx->lc;
	if (!lc->bc) { ctx->result = ENOENT; return; }

	struct audio *au = call_audio(lc->bc);
	if (!au) { ctx->result = ENOENT; return; }

	struct stream *strm = audio_strm(au);
	if (!strm) { ctx->result = ENOENT; return; }

	const struct rtcp_stats *rs = stream_rtcp_stats(strm);
	if (!rs) { ctx->result = ENOENT; return; }

	baresdk_ev_media_stats_t *s = ctx->out;
	memset(s, 0, sizeof(*s));

	s->call             = lc;
	s->packets_sent     = stream_metric_get_tx_n_packets(strm);
	s->packets_received = stream_metric_get_rx_n_packets(strm);
	s->packets_lost     = (uint32_t)(rs->tx.lost > 0 ? rs->tx.lost : 0);
	s->rtt_ms           = (float)rs->rtt / 1000.f;
	s->jitter_ms        = (float)rs->rx.jit / 1000.f;

	int total_tx = (int)rs->tx.sent + (int)s->packets_lost;
	s->loss_pct  = total_tx > 0
	             ? (float)s->packets_lost * 100.f / (float)total_tx
	             : 0.f;

	s->mos_lq     = calc_mos(s->loss_pct, s->jitter_ms, s->rtt_ms,
	                         g_bsdk.cfg.mos_method);
	s->mos_cq     = calc_mos_cq(s->mos_lq, s->rtt_ms);
	s->mos_method = g_bsdk.cfg.mos_method;

	s->bandwidth_kbps_tx = stream_metric_get_tx_bitrate(strm) / 1000;
	s->bandwidth_kbps_rx = stream_metric_get_rx_bitrate(strm) / 1000;

	const struct aucodec *ac = audio_codec(au, true);
	if (ac) {
		s->codec_name       = ac->name;
		s->codec_clock_rate = ac->crate;
	}

	ctx->result = 0;
}

int baresdk_call_get_stats(baresdk_call_handle_t call,
                            baresdk_ev_media_stats_t *out)
{
	if (!call || !out) return BARESDK_ERR_INVAL;
	getstats_ctx_t ctx = {.lc = call, .out = out, .result = 0};
	int err = bsdk_dispatch_sync(getstats_fn, &ctx);
	return err ? err : ctx.result;
}
