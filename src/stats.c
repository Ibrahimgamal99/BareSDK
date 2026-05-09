/**
 * @file stats.c  RTCP stats polling timer + MOS calculation
 *
 * A tmr fires on re_main every cfg->stats_interval_ms. For each active call
 * it reads RTCP stats from the audio stream and posts BARESDK_EV_MEDIA_STATS.
 *
 * E-model MOS (ITU-T G.107):
 *   Ta = RTT/2 + 2·jitter_ms   (one-way delay estimate incl. jitter buffer)
 *   Id = 0.024·Ta + 0.11·(Ta-177.3)·[Ta>177.3]
 *   Ie = 7 + 30·ln(1 + 15·loss%)
 *   R  = 93.2 - Id - Ie         (clamped [0,100])
 *   MOS = 1 + 0.035·R + 7e-6·R·(R-60)·(100-R)
 *
 * Simplified MOS (Telchemy/CISCO standard form):
 *   MOS = 4.5 - 0.09·loss_pct - 0.0009·jitter_ms - 0.0005·RTT
 *         (0.0005·RTT = 0.001 × RTT/2 one-way delay)
 *
 * MOS-CQ (conversational quality, ITU-T G.114):
 *   Additional penalty for RTT > 300 ms (150 ms one-way).
 */

#include <math.h>
#include "baresdk_internal.h"

/* ── MOS calculations ───────────────────────────────────────────────────── */

static float mos_emodel(float loss_pct, float jitter_ms, float rtt_ms)
{
	/* One-way delay estimate: RTT/2 plus jitter buffer (≈2× jitter) */
	float Ta = rtt_ms * 0.5f + 2.0f * jitter_ms;
	float Id = 0.024f * Ta
	         + 0.11f * (Ta - 177.3f) * (Ta > 177.3f ? 1.f : 0.f);
	float Ie = 7.0f + 30.0f * logf(1.0f + 15.0f * loss_pct / 100.0f);
	float R  = 93.2f - Id - Ie;
	if (R < 0.f)   R = 0.f;
	if (R > 100.f) R = 100.f;
	return 1.0f + 0.035f * R + 7e-6f * R * (R - 60.f) * (100.f - R);
}

static float mos_simplified(float loss_pct, float jitter_ms, float rtt_ms)
{
	/* Telchemy/CISCO standard simplified VoIP MOS:
	 *   -0.09  per 1% packet loss
	 *   -0.0009 per ms jitter
	 *   -0.0005 per ms RTT  (= -0.001 per ms one-way delay, RTT/2)  */
	float m = 4.5f
	        - 0.09f   * loss_pct
	        - 0.0009f * jitter_ms
	        - 0.0005f * rtt_ms;
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
	/* ITU-T G.114: 150 ms one-way (300 ms RTT) is the practical limit for
	 * conversational quality. Apply 0.5 MOS penalty per extra 300 ms RTT. */
	float penalty = 0.0f;
	if (rtt_ms > 300.f)
		penalty = 0.5f * (rtt_ms - 300.f) / 300.f;
	float cq = mos_lq - penalty;
	if (cq < 1.0f) cq = 1.0f;
	return cq;
}

/* ── Shared stats population ─────────────────────────────────────────────── */

/* Fill all numeric/address fields of *s from the live audio stream.
 * Does NOT set s->codec_name (pointer lifetime differs per caller).
 * Returns the aucodec so the caller can set codec_name appropriately. */
static const struct aucodec *fill_audio_stats(baresdk_ev_media_stats_t *s,
                                               struct baresdk_call *lc,
                                               struct audio *au,
                                               struct stream *strm)
{
	s->call = lc;

	/* ── Packet counters ─────────────────────────────────────────────── */
	s->packets_sent      = stream_metric_get_tx_n_packets(strm);
	s->packets_received  = stream_metric_get_rx_n_packets(strm);
	s->bytes_sent        = stream_metric_get_tx_n_bytes(strm);
	s->bytes_received    = stream_metric_get_rx_n_bytes(strm);
	s->tx_errors         = stream_metric_get_tx_n_err(strm);
	s->rx_errors         = stream_metric_get_rx_n_err(strm);

	/* ── Bandwidth ───────────────────────────────────────────────────── */
	s->bandwidth_kbps_tx     = stream_metric_get_tx_bitrate(strm) / 1000;
	s->bandwidth_kbps_rx     = stream_metric_get_rx_bitrate(strm) / 1000;
	s->avg_bandwidth_kbps_tx = (uint32_t)(stream_metric_get_tx_avg_bitrate(strm) / 1000.0);
	s->avg_bandwidth_kbps_rx = (uint32_t)(stream_metric_get_rx_avg_bitrate(strm) / 1000.0);

	/* ── RTCP (may be absent early in a call; leave zeros if so) ─────── */
	s->mos_method = g_bsdk.cfg.mos_method;

	const struct rtcp_stats *rs = stream_rtcp_stats(strm);
	if (rs) {
		s->rtt_ms      = (float)rs->rtt    / 1000.f;  /* µs → ms */
		s->jitter_ms   = (float)rs->rx.jit / 1000.f;
		s->tx_jitter_ms = (float)rs->tx.jit / 1000.f;

		/* TX loss: lost ⊂ sent, so denominator = rs->tx.sent */
		s->packets_lost = (uint32_t)(rs->tx.lost > 0 ? rs->tx.lost : 0);
		s->loss_pct = rs->tx.sent > 0
		            ? (float)s->packets_lost * 100.f / (float)rs->tx.sent
		            : 0.f;

		/* RX loss: total expected = received + lost */
		s->packets_lost_rx = (uint32_t)(rs->rx.lost > 0 ? rs->rx.lost : 0);
		uint32_t total_rx  = s->packets_received + s->packets_lost_rx;
		s->loss_pct_rx = total_rx > 0
		               ? (float)s->packets_lost_rx * 100.f / (float)total_rx
		               : 0.f;

		s->mos_lq = calc_mos(s->loss_pct, s->jitter_ms, s->rtt_ms,
		                     g_bsdk.cfg.mos_method);
		s->mos_cq = calc_mos_cq(s->mos_lq, s->rtt_ms);
	}

	/* ── Jitter buffer ───────────────────────────────────────────────── */
	struct jbuf_stat jb;
	if (stream_jbuf_stats(strm, &jb) == 0) {
		s->jitter_buffer_ms   = jb.c_delay;
		s->jitter_buffer_load = jb.c_packets;
		s->late_packets       = jb.n_late;
		s->discarded_packets  = jb.n_overflow + jb.n_flush;
	}

	/* ── Audio level (dBov) ──────────────────────────────────────────── */
	double level = 0.0;
	if (audio_level_get(au, &level) == 0)
		s->audio_level_dbov = (float)level;
	else
		s->audio_level_dbov = (float)NAN;

	/* ── Stream identity ─────────────────────────────────────────────── */
	struct rtp_sock *rsock = stream_rtp_sock(strm);
	if (rsock)
		s->ssrc_tx = rtp_sess_ssrc(rsock);
	stream_ssrc_rx(strm, &s->ssrc_rx);

	s->payload_type = stream_pt_enc(strm);

	const struct sa *raddr = stream_raddr(strm);
	if (raddr)
		re_snprintf(s->remote_addr, sizeof(s->remote_addr), "%J", raddr);

	/* ── Codec ───────────────────────────────────────────────────────── */
	const struct aucodec *ac = audio_codec(au, true);
	if (ac) {
		s->codec_clock_rate  = ac->crate;
		s->codec_sample_rate = ac->srate;
		s->codec_channels    = ac->ch;
	}

	return ac;
}

/* ── Per-call stats collection (timer path) ──────────────────────────────── */

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

	struct baresdk_queued_event *qev = mem_alloc(sizeof(*qev), NULL);
	if (!qev)
		return;
	memset(qev, 0, sizeof(*qev));

	qev->ev.type = BARESDK_EV_MEDIA_STATS;
	baresdk_ev_media_stats_t *s = &qev->ev.u.stats;

	const struct aucodec *ac = fill_audio_stats(s, lc, au, strm);
	if (ac) {
		str_ncpy(qev->buf, ac->name, sizeof(qev->buf));
		s->codec_name = qev->buf;
	}

	bsdk_event_post_qev(qev);
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

	baresdk_ev_media_stats_t *s = ctx->out;
	memset(s, 0, sizeof(*s));

	const struct aucodec *ac = fill_audio_stats(s, lc, au, strm);
	/* codec_name points directly into baresip's stable registered-codec list */
	if (ac)
		s->codec_name = ac->name;

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
