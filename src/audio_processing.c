/**
 * @file audio_processing.c
 * TX audio filters: NS (noise suppression), AGC, and AEC half-duplex suppressor.
 *
 * NS:  single-band Wiener noise gate on the TX (microphone) path.
 * AGC: RMS-based gain normaliser on the TX path; targets −20 dBFS.
 * AEC: half-duplex echo suppressor — attenuates TX when RX energy is high.
 *      This is NOT acoustic echo cancellation.  AEC requires the playback
 *      reference signal before D/A conversion, which is only available to
 *      platform audio drivers.  For true AEC use platform voice-processing
 *      modes: CoreAudio VoiceProcessingIO, AAudio USAGE_VOICE_COMMUNICATION,
 *      or PulseAudio module-echo-cancel.  The suppressor here degrades gracefully
 *      to half-duplex behaviour when both sides speak simultaneously.
 *
 * All three are registered as baresip aufilt instances.  Filters that are
 * disabled at init time are registered but not enabled, so they add zero
 * overhead to the audio processing chain.
 */

#include "baresdk_internal.h"
#include <rem_au.h>
#include <rem_aulevel.h>
#include <rem_auframe.h>
#include <string.h>

/* ── NS ─────────────────────────────────────────────────────────────────── */

struct ns_enc_st {
	struct aufilt_enc_st base;
	float noise_floor_sq; /* running noise power floor (mean square) */
	float smooth_gain;    /* output gain, smoothed to avoid clicks    */
};

static void ns_enc_dtor(void *arg) { (void)arg; }

static int ns_encupd(struct aufilt_enc_st **stp, void **ctx,
                     const struct aufilt *af, struct aufilt_prm *prm,
                     const struct audio *au)
{
	(void)ctx; (void)af; (void)prm; (void)au;
	*stp = mem_deref(*stp);
	struct ns_enc_st *s = mem_zalloc(sizeof(*s), ns_enc_dtor);
	if (!s) return ENOMEM;
	s->smooth_gain = 1.0f;
	*stp = (struct aufilt_enc_st *)s;
	return 0;
}

static int ns_encode(struct aufilt_enc_st *st, struct auframe *af)
{
	if (af->fmt != AUFMT_S16LE) return 0;
	struct ns_enc_st *s = (struct ns_enc_st *)st;
	int16_t *p = (int16_t *)af->sampv;
	size_t   n = af->sampc;
	if (!n) return 0;

	/* Frame power (mean square) */
	float sum_sq = 0.0f;
	for (size_t i = 0; i < n; i++)
		sum_sq += (float)p[i] * (float)p[i];
	float frame_power = sum_sq / (float)n;

	/* Minimum-tracking noise floor: fall fast, rise very slowly */
	if (frame_power < s->noise_floor_sq)
		s->noise_floor_sq = 0.97f  * s->noise_floor_sq + 0.03f  * frame_power;
	else
		s->noise_floor_sq = 0.9998f * s->noise_floor_sq + 0.0002f * frame_power;

	/* Wiener gain in power domain; floor of 0.1 (−20 dB) prevents over-gating */
	float noise      = s->noise_floor_sq + 1.0f; /* +1 avoids division by zero */
	float snr        = frame_power / noise;
	float target_gain;
	if      (snr >= 4.0f) target_gain = 1.0f;
	else if (snr <= 1.0f) target_gain = 0.1f;
	else                  target_gain = 0.1f + 0.9f * (snr - 1.0f) / 3.0f;

	/* Asymmetric smoothing: fast attack avoids chopping speech onsets */
	if (target_gain < s->smooth_gain)
		s->smooth_gain = 0.70f * s->smooth_gain + 0.30f * target_gain;
	else
		s->smooth_gain = 0.95f * s->smooth_gain + 0.05f * target_gain;

	for (size_t i = 0; i < n; i++) {
		float v = (float)p[i] * s->smooth_gain;
		p[i] = (v > 32767.0f) ? 32767 : (v < -32768.0f) ? -32768 : (int16_t)v;
	}
	return 0;
}

/* ── AGC ────────────────────────────────────────────────────────────────── */

struct agc_enc_st {
	struct aufilt_enc_st base;
	float gain;
};

static void agc_enc_dtor(void *arg) { (void)arg; }

static int agc_encupd(struct aufilt_enc_st **stp, void **ctx,
                      const struct aufilt *af, struct aufilt_prm *prm,
                      const struct audio *au)
{
	(void)ctx; (void)af; (void)prm; (void)au;
	*stp = mem_deref(*stp);
	struct agc_enc_st *s = mem_zalloc(sizeof(*s), agc_enc_dtor);
	if (!s) return ENOMEM;
	s->gain = 1.0f;
	*stp = (struct aufilt_enc_st *)s;
	return 0;
}

static int agc_encode(struct aufilt_enc_st *st, struct auframe *af)
{
	if (af->fmt != AUFMT_S16LE) return 0;
	struct agc_enc_st *s = (struct agc_enc_st *)st;
	int16_t *p = (int16_t *)af->sampv;
	size_t   n = af->sampc;
	if (!n) return 0;

	float sum_sq = 0.0f;
	for (size_t i = 0; i < n; i++)
		sum_sq += (float)p[i] * (float)p[i];
	float frame_power = sum_sq / (float)n; /* mean square */

	/* Target −20 dBFS: RMS = 0.1 × 32768 = 3276.8; mean-square = 1.073e7 */
	const float target_sq = 1.073e7f;
	float output_power    = frame_power * (s->gain * s->gain);

	/* Only act above −50 dBFS (mean-square ≈ 1.07e4) to avoid boosting silence */
	if (frame_power > 1.07e4f) {
		if (output_power > target_sq * 1.5f)
			s->gain *= 0.85f; /* fast attack */
		else if (output_power < target_sq * 0.67f)
			s->gain *= 1.02f; /* slow release */
	}

	/* Clamp: 0.1 (−20 dB) to 10.0 (+20 dB) */
	if (s->gain < 0.1f)  s->gain = 0.1f;
	if (s->gain > 10.0f) s->gain = 10.0f;

	for (size_t i = 0; i < n; i++) {
		float v = (float)p[i] * s->gain;
		p[i] = (v > 32767.0f) ? 32767 : (v < -32768.0f) ? -32768 : (int16_t)v;
	}
	return 0;
}

/* ── AEC (half-duplex echo suppressor) ──────────────────────────────────── */

/* Shared across all concurrent calls.  A stale read from the TX thread is
 * harmless — the suppressor is best-effort and converges within a few frames.
 * Stored as uint32_t because GCC/Clang atomic intrinsics don't accept float *. */
static uint32_t g_aec_rx_power_bits = 0u; /* bit-pattern of float 0.0f */

static inline float aec_rx_load(void)
{
	uint32_t bits = re_atomic_rlx(&g_aec_rx_power_bits);
	float v; memcpy(&v, &bits, 4); return v;
}
static inline void aec_rx_store(float f)
{
	uint32_t bits; memcpy(&bits, &f, 4);
	re_atomic_rlx_set(&g_aec_rx_power_bits, bits);
}

struct aec_enc_st {
	struct aufilt_enc_st base;
	float smooth_gain;
};
struct aec_dec_st { struct aufilt_dec_st base; };

static void aec_enc_dtor(void *arg) { (void)arg; }
static void aec_dec_dtor(void *arg) { (void)arg; }

static int aec_encupd(struct aufilt_enc_st **stp, void **ctx,
                      const struct aufilt *af, struct aufilt_prm *prm,
                      const struct audio *au)
{
	(void)ctx; (void)af; (void)prm; (void)au;
	*stp = mem_deref(*stp);
	struct aec_enc_st *s = mem_zalloc(sizeof(*s), aec_enc_dtor);
	if (!s) return ENOMEM;
	s->smooth_gain = 1.0f;
	*stp = (struct aufilt_enc_st *)s;
	return 0;
}

static int aec_decupd(struct aufilt_dec_st **stp, void **ctx,
                      const struct aufilt *af, struct aufilt_prm *prm,
                      const struct audio *au)
{
	(void)ctx; (void)af; (void)prm; (void)au;
	*stp = mem_deref(*stp);
	struct aec_dec_st *s = mem_zalloc(sizeof(*s), aec_dec_dtor);
	if (!s) return ENOMEM;
	*stp = (struct aufilt_dec_st *)s;
	return 0;
}

static int aec_encode(struct aufilt_enc_st *st, struct auframe *af)
{
	if (af->fmt != AUFMT_S16LE) return 0;
	struct aec_enc_st *s = (struct aec_enc_st *)st;
	int16_t *p = (int16_t *)af->sampv;
	size_t   n = af->sampc;
	if (!n) return 0;

	float rx_power = aec_rx_load();

	/* Suppress TX linearly between rx_low (quiet) and rx_high (normal speech).
	 * Below rx_low: no suppression.  Above rx_high: suppress to 0.15 (−16.5 dB). */
	const float rx_low  = 4.0e4f; /* ≈ −30 dBFS mean-square */
	const float rx_high = 9.0e6f; /* ≈ −21 dBFS mean-square */
	float target_gain;
	if (rx_power <= rx_low)
		target_gain = 1.0f;
	else if (rx_power >= rx_high)
		target_gain = 0.15f;
	else
		target_gain = 1.0f - 0.85f * (rx_power - rx_low) / (rx_high - rx_low);

	/* Fast attack prevents echo reaching the far end; slow release avoids clipping */
	if (target_gain < s->smooth_gain)
		s->smooth_gain = 0.60f * s->smooth_gain + 0.40f * target_gain;
	else
		s->smooth_gain = 0.95f * s->smooth_gain + 0.05f * target_gain;

	for (size_t i = 0; i < n; i++) {
		float v = (float)p[i] * s->smooth_gain;
		p[i] = (v > 32767.0f) ? 32767 : (v < -32768.0f) ? -32768 : (int16_t)v;
	}
	return 0;
}

static int aec_decode(struct aufilt_dec_st *st, struct auframe *af)
{
	(void)st;
	if (af->fmt != AUFMT_S16LE) return 0;
	const int16_t *p = (const int16_t *)af->sampv;
	size_t n = af->sampc;
	if (!n) return 0;

	float sum_sq = 0.0f;
	for (size_t i = 0; i < n; i++)
		sum_sq += (float)p[i] * (float)p[i];
	float frame_power = sum_sq / (float)n;

	float prev = aec_rx_load();
	float next = (frame_power > prev)
	           ? 0.80f * prev + 0.20f * frame_power  /* fast attack  */
	           : 0.95f * prev + 0.05f * frame_power; /* slow release */
	aec_rx_store(next);
	return 0;
}

/* ── Registration ───────────────────────────────────────────────────────── */

static struct aufilt g_ns_filter = {
	.name    = "bsdk_ns",
	.encupdh = ns_encupd,
	.ench    = ns_encode,
};

static struct aufilt g_agc_filter = {
	.name    = "bsdk_agc",
	.encupdh = agc_encupd,
	.ench    = agc_encode,
};

static struct aufilt g_aec_filter = {
	.name    = "bsdk_aec",
	.encupdh = aec_encupd,
	.ench    = aec_encode,
	.decupdh = aec_decupd,
	.dech    = aec_decode,
};

void bsdk_audio_processing_init(bool ns, bool agc, bool aec)
{
	struct list *fl = baresip_aufiltl();
	aufilt_register(fl, &g_ns_filter);
	aufilt_register(fl, &g_agc_filter);
	aufilt_register(fl, &g_aec_filter);
	if (ns)  aufilt_enable(fl, "bsdk_ns",  true);
	if (agc) aufilt_enable(fl, "bsdk_agc", true);
	if (aec) aufilt_enable(fl, "bsdk_aec", true);
}

void bsdk_audio_processing_close(void)
{
	aufilt_unregister(&g_ns_filter);
	aufilt_unregister(&g_agc_filter);
	aufilt_unregister(&g_aec_filter);
}
