/**
 * @file stats.c  RTCP stats polling timer + MOS calculation
 *
 * A tmr fires on re_main every cfg->stats_interval_ms. For each active call
 * it reads RTCP stats from the audio stream and posts BARESDK_EV_MEDIA_STATS.
 *
 * E-model MOS (ITU-T G.107):
 *   Ta     = RTT/2 + 2·jitter_ms   (one-way delay estimate incl. jitter buffer)
 *   Id     = 0.024·Ta + 0.11·(Ta-177.3)·[Ta>177.3]
 *   Ie-eff = Ie + (95-Ie)·Ppl/(Ppl+Bpl)   per-codec per ITU-T G.113 Appendix I
 *   R      = 93.2 - Id - Ie-eff    (clamped [0,100])
 *   MOS    = 1 + 0.035·R + 7e-6·R·(R-60)·(100-R)
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

/* ITU-T G.113 Appendix I codec parameters: Ie (baseline impairment at 0 loss)
 * and Bpl (packet-loss robustness factor). */
typedef struct { float ie; float bpl; } codec_params_t;

static codec_params_t codec_emodel_params(const char *name, uint32_t bitrate_kbps)
{
	if (!name)                                                return (codec_params_t){ 0.f,  4.3f};
	if (!str_casecmp(name, "PCMU") || !str_casecmp(name, "PCMA"))
	                                                          return (codec_params_t){ 0.f,  4.3f};
	if (!str_casecmp(name, "G722"))                           return (codec_params_t){ 5.f,  7.0f};
	if (!str_casecmp(name, "G729"))                           return (codec_params_t){11.f, 19.0f};
	if (!str_casecmp(name, "G723"))                           return (codec_params_t){15.f, 16.0f};
	if (!str_casecmp(name, "opus")) {
		/* Opus Ie varies by bitrate per 2020 Opus E-model research paper */
		if (bitrate_kbps <= 8)  return (codec_params_t){14.f, 12.0f};
		if (bitrate_kbps <= 12) return (codec_params_t){11.f, 14.0f};
		return                         (codec_params_t){ 7.f, 14.0f};
	}
	return (codec_params_t){0.f, 4.3f};
}

static float mos_emodel(float loss_pct, float jitter_ms, float rtt_ms,
                         float ie_codec, float bpl)
{
	float Ta    = rtt_ms * 0.5f + 2.0f * jitter_ms;
	float Id    = 0.024f * Ta
	            + 0.11f * (Ta - 177.3f) * (Ta > 177.3f ? 1.f : 0.f);
	/* ITU-T G.107: Ie-eff = Ie + (95-Ie) * Ppl / (Ppl + Bpl) */
	float Ie_eff = ie_codec + (95.f - ie_codec) * loss_pct / (loss_pct + bpl);
	float R = 93.2f - Id - Ie_eff;
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
                       baresdk_mos_method_t method, codec_params_t cp)
{
	if (method == BARESDK_MOS_SIMPLIFIED)
		return mos_simplified(loss_pct, jitter_ms, rtt_ms);
	return mos_emodel(loss_pct, jitter_ms, rtt_ms, cp.ie, cp.bpl);
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

	/* ── Codec (needed before RTCP for clock-rate-based jitter conversion) */
	const struct aucodec *ac = audio_codec(au, true);
	if (ac) {
		s->codec_clock_rate  = ac->crate;
		s->codec_sample_rate = ac->srate;
		s->codec_channels    = ac->ch;
	}

	/* ── RTCP (may be absent early in a call; leave zeros if so) ─────── */
	s->mos_method = g_bsdk.cfg.mos_method;

	const struct rtcp_stats *rs = stream_rtcp_stats(strm);
	if (rs) {
		/* rtt is in µs — confirmed by libre/src/rtp/rtcp.h */
		s->rtt_ms = (float)rs->rtt / 1000.f;

		/* jit is in RTP timestamp units (RFC 3550 §6.4.1).
		 * Convert: jit_ms = jit_ts * 1000 / clock_rate
		 * Source: baresip/src/rtpstat.c */
		uint32_t clock = s->codec_clock_rate > 0 ? s->codec_clock_rate : 8000u;
		s->jitter_ms    = (float)rs->rx.jit * 1000.f / (float)clock;
		s->tx_jitter_ms = (float)rs->tx.jit * 1000.f / (float)clock;

		/* TX loss: lost ⊂ sent, so denominator = rs->tx.sent.
		 * rs->tx.lost is signed — can go negative on seq reorder. */
		int32_t raw_tx_lost = (int32_t)rs->tx.lost;
		s->packets_lost = (raw_tx_lost > 0) ? (uint32_t)raw_tx_lost : 0u;
		s->loss_pct = rs->tx.sent > 0
		            ? (float)s->packets_lost * 100.f / (float)rs->tx.sent
		            : 0.f;

		/* RX loss: rs->rx.lost (RTCP) only valid after ~50 packets.
		 * Gate to avoid early-call denominator mismatch. */
		bool rtcp_stable = (s->packets_received > 50) && (rs->rx.lost >= 0);
		int32_t raw_rx_lost = (int32_t)rs->rx.lost;
		s->packets_lost_rx = (rtcp_stable && raw_rx_lost > 0)
		                   ? (uint32_t)raw_rx_lost : 0u;
		uint32_t total_rx  = s->packets_received + s->packets_lost_rx;
		s->loss_pct_rx = (rtcp_stable && total_rx > 0)
		               ? (float)s->packets_lost_rx * 100.f / (float)total_rx
		               : 0.f;

		codec_params_t cp = codec_emodel_params(ac ? ac->name : NULL,
		                                        s->avg_bandwidth_kbps_tx);
		s->mos_lq    = calc_mos(s->loss_pct,    s->jitter_ms, s->rtt_ms,
		                        g_bsdk.cfg.mos_method, cp);
		s->mos_cq    = calc_mos_cq(s->mos_lq,    s->rtt_ms);
		s->mos_lq_rx = calc_mos(s->loss_pct_rx, s->jitter_ms, s->rtt_ms,
		                        g_bsdk.cfg.mos_method, cp);
		s->mos_cq_rx = calc_mos_cq(s->mos_lq_rx, s->rtt_ms);
	}

	/* ── Jitter buffer ───────────────────────────────────────────────── */
	struct jbuf_stat jb;
	if (stream_jbuf_stats(strm, &jb) == 0) {
		s->jitter_buffer_ms        = jb.c_delay;
		s->jitter_buffer_load      = jb.c_packets;
		s->late_packets            = jb.n_late;
		s->discarded_packets       = jb.n_overflow + jb.n_flush;
		s->jitter_buffer_target_ms = jb.c_jitter;
		s->jitter_buffer_adaptive  =
		    (conf_config()->avt.audio.jbtype == JBUF_ADAPTIVE);
		s->plc_frames = jb.n_lost;
		s->plc_ratio  = jb.n_get > 0
		              ? (float)jb.n_lost / (float)jb.n_get : 0.f;
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

	return ac;
}

/* ── Quality alert helper ────────────────────────────────────────────────── */

static void post_quality_alert(struct baresdk_call *lc,
                                baresdk_quality_issue_t issue,
                                float value, float threshold, bool recovering)
{
	struct baresdk_queued_event *qev = mem_alloc(sizeof(*qev), NULL);
	if (!qev) return;
	memset(qev, 0, sizeof(*qev));
	qev->ev.type = BARESDK_EV_QUALITY_ALERT;
	baresdk_ev_quality_alert_t *a = &qev->ev.u.quality_alert;
	a->call       = lc;
	a->issue      = issue;
	a->value      = value;
	a->threshold  = threshold;
	a->recovering = recovering;
	bsdk_event_post_qev(qev);
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

	/* Session history */
	lc->stats_tick++;
	if (s->mos_lq > 0.f) {
		if (lc->stats_mos_min == 0.f || s->mos_lq < lc->stats_mos_min)
			lc->stats_mos_min = s->mos_lq;
		lc->stats_mos_sum += s->mos_lq;
	}
	s->mos_lq_min      = lc->stats_mos_min;
	s->mos_lq_avg      = lc->stats_tick > 0
	                   ? lc->stats_mos_sum / (float)lc->stats_tick : 0.f;
	s->stats_tick      = lc->stats_tick;
	s->call_duration_ms = lc->stats_call_start
	                    ? (tmr_jiffies() - lc->stats_call_start) : 0u;
	s->is_final        = false;

	/* Quality alert threshold crossing detection */
	const baresdk_config_t *cfg = &g_bsdk.cfg;
	if (cfg->mos_alert_threshold > 0.f && s->mos_lq > 0.f) {
		bool was_bad = lc->last_mos_lq > 0.f
		            && lc->last_mos_lq < cfg->mos_alert_threshold;
		bool is_bad  = s->mos_lq < cfg->mos_alert_threshold;
		if (is_bad != was_bad)
			post_quality_alert(lc, BARESDK_QUALITY_MOS, s->mos_lq,
			                   cfg->mos_alert_threshold, was_bad);
	}
	if (cfg->loss_alert_threshold > 0.f) {
		bool was_bad = lc->last_loss_pct > cfg->loss_alert_threshold;
		bool is_bad  = s->loss_pct      > cfg->loss_alert_threshold;
		if (is_bad != was_bad)
			post_quality_alert(lc, BARESDK_QUALITY_LOSS, s->loss_pct,
			                   cfg->loss_alert_threshold, was_bad);
	}
	if (cfg->jitter_alert_threshold > 0.f) {
		bool was_bad = lc->last_jitter_ms > cfg->jitter_alert_threshold;
		bool is_bad  = s->jitter_ms       > cfg->jitter_alert_threshold;
		if (is_bad != was_bad)
			post_quality_alert(lc, BARESDK_QUALITY_JITTER, s->jitter_ms,
			                   cfg->jitter_alert_threshold, was_bad);
	}
	lc->last_mos_lq    = s->mos_lq;
	lc->last_loss_pct  = s->loss_pct;
	lc->last_jitter_ms = s->jitter_ms;

	bsdk_event_post_qev(qev);
}

/* ── Final stats snapshot on call teardown ───────────────────────────────── */

void bsdk_stats_collect_final(struct baresdk_call *lc)
{
	if (!lc || !lc->bc)
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

	s->is_final        = true;
	s->mos_lq_min      = lc->stats_mos_min;
	s->mos_lq_avg      = lc->stats_tick > 0
	                   ? lc->stats_mos_sum / (float)lc->stats_tick : 0.f;
	s->stats_tick      = lc->stats_tick;
	s->call_duration_ms = lc->stats_call_start
	                    ? (tmr_jiffies() - lc->stats_call_start) : 0u;

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
