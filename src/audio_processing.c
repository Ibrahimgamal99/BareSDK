/**
 * @file audio_processing.c
 * TX/RX audio filters: mic gain, NS, AGC, AEC half-duplex suppressor,
 * and speaker gain.
 *
 * Filter chain (encode / TX path, in registration order):
 *   bsdk_mic_gain → bsdk_ns → bsdk_agc → bsdk_aec
 * Decode / RX path (baresip walks dech in reverse registration order):
 *   bsdk_spk_gain
 *
 * bsdk_mic_gain  — fixed dB scalar on the TX path (pre-boost before NS/AGC).
 * bsdk_ns        — single-band Wiener noise gate on TX.
 * bsdk_agc       — RMS-based gain normaliser targeting −20 dBFS.
 * bsdk_aec       — half-duplex echo suppressor: attenuates TX when RX is loud.
 *                  This is NOT acoustic echo cancellation.  For true AEC use
 *                  BARESDK_AEC_WEBRTC (desktop, requires libwebrtc-audio-processing-1).
 * bsdk_spk_gain  — fixed dB scalar on the RX path (post-jitter, pre-playback).
 *
 * Thread model
 * ─────────────
 * Gain scalars and the AEC floor are stored as atomic uint32_t (float bits).
 * Setters in audio.c write via bsdk_*_store(); filters read via load_f().
 * No dispatch needed for gain setters — the audio thread gets the new value
 * within one frame (~20 ms) with no locking.
 *
 * aufilt_register/unregister/enable modify the shared aufiltl list.
 * "PROTECTED BY RE_MAIN" is used to mark those call sites — they must
 * always run on the re_main thread via bsdk_dispatch_sync.
 */

#include "baresdk_internal.h"
#include <rem_au.h>
#include <rem_aulevel.h>
#include <rem_auframe.h>
#include <string.h>
#include <math.h>

/* ── Shared atomic gain state ───────────────────────────────────────────── */

static uint32_t g_mic_gain_bits  = 0x3F800000u;  /* float 1.0f — unity */
static uint32_t g_spk_gain_bits  = 0x3F800000u;  /* float 1.0f — unity */
/* aec_suppression_level=1.0 → floor=0.15 (−16.5 dB), the former hardcoded default.
 * aec_suppression_level=0.0 → floor=1.0 (no attenuation).
 * Internal formula: floor = 1.0f - level * 0.85f  (see bsdk_aec_floor_store). */
static uint32_t g_aec_floor_bits = 0x3E19999Au;  /* float 0.15f */

static inline float load_f(uint32_t *p)
{
	uint32_t b = re_atomic_rlx(p);
	float v;
	memcpy(&v, &b, 4);
	return v;
}

static inline void store_f(uint32_t *p, float v)
{
	uint32_t b;
	memcpy(&b, &v, 4);
	re_atomic_rlx_set(p, b);
}

void bsdk_mic_gain_store(float linear) { store_f(&g_mic_gain_bits, linear); }
void bsdk_spk_gain_store(float linear) { store_f(&g_spk_gain_bits, linear); }
void bsdk_aec_floor_store(float floor) { store_f(&g_aec_floor_bits, floor); }

/* ── Helpers shared by mic_gain and spk_gain ────────────────────────────── */

static inline void apply_gain(int16_t *p, size_t n, float g)
{
	for (size_t i = 0; i < n; i++) {
		int32_t s = (int32_t)((float)p[i] * g);
		p[i] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
	}
}

/* ── bsdk_mic_gain (encode-only) ────────────────────────────────────────── */

struct mic_gain_enc_st { struct aufilt_enc_st base; };

static void mic_gain_enc_dtor(void *arg) { (void)arg; }

static int mic_gain_encupd(struct aufilt_enc_st **stp, void **ctx,
                            const struct aufilt *af, struct aufilt_prm *prm,
                            const struct audio *au)
{
	(void)ctx; (void)af; (void)prm; (void)au;
	*stp = mem_deref(*stp);
	struct mic_gain_enc_st *s = mem_zalloc(sizeof(*s), mic_gain_enc_dtor);
	if (!s) return ENOMEM;
	*stp = (struct aufilt_enc_st *)s;
	return 0;
}

static int mic_gain_encode(struct aufilt_enc_st *st, struct auframe *af)
{
	(void)st;
	if (af->fmt != AUFMT_S16LE) return 0;
	float g = load_f(&g_mic_gain_bits);
	if (g == 1.0f) return 0;  /* fast-path bypass — no-op at unity */
	apply_gain((int16_t *)af->sampv, af->sampc, g);
	return 0;
}

/* ── NS ─────────────────────────────────────────────────────────────────── */

struct ns_enc_st {
	struct aufilt_enc_st base;
	float noise_floor_sq;
	float smooth_gain;
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

	float sum_sq = 0.0f;
	for (size_t i = 0; i < n; i++)
		sum_sq += (float)p[i] * (float)p[i];
	float frame_power = sum_sq / (float)n;

	if (frame_power < s->noise_floor_sq)
		s->noise_floor_sq = 0.97f  * s->noise_floor_sq + 0.03f  * frame_power;
	else
		s->noise_floor_sq = 0.9998f * s->noise_floor_sq + 0.0002f * frame_power;

	float noise      = s->noise_floor_sq + 1.0f;
	float snr        = frame_power / noise;
	float target_gain;
	if      (snr >= 4.0f) target_gain = 1.0f;
	else if (snr <= 1.0f) target_gain = 0.1f;
	else                  target_gain = 0.1f + 0.9f * (snr - 1.0f) / 3.0f;

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
	float frame_power = sum_sq / (float)n;

	const float target_sq = 1.073e7f;
	float output_power    = frame_power * (s->gain * s->gain);

	if (frame_power > 1.07e4f) {
		if (output_power > target_sq * 1.5f)
			s->gain *= 0.85f;
		else if (output_power < target_sq * 0.67f)
			s->gain *= 1.02f;
	}

	if (s->gain < 0.1f)  s->gain = 0.1f;
	if (s->gain > 10.0f) s->gain = 10.0f;

	for (size_t i = 0; i < n; i++) {
		float v = (float)p[i] * s->gain;
		p[i] = (v > 32767.0f) ? 32767 : (v < -32768.0f) ? -32768 : (int16_t)v;
	}
	return 0;
}

/* ── AEC (half-duplex echo suppressor) ──────────────────────────────────── */

/* RX power shared between aec_decode (writer) and aec_encode (reader).
 * Stored as uint32_t because GCC/Clang atomic intrinsics don't accept float *. */
static uint32_t g_aec_rx_power_bits = 0u;

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

	/* aec_suppression_level=0 → floor=1.0 (no TX attenuation)
	 * aec_suppression_level=1 → floor=0.15 (−16.5 dB, current default)
	 * The mapping is inverted because g_aec_floor_bits is a minimum gain,
	 * not a suppression amount.  See bsdk_aec_floor_store() in audio.c. */
	const float rx_low  = 4.0e4f;
	const float rx_high = 9.0e6f;
	float floor_gain    = load_f(&g_aec_floor_bits);
	float target_gain;
	if (rx_power <= rx_low)
		target_gain = 1.0f;
	else if (rx_power >= rx_high)
		target_gain = floor_gain;
	else
		target_gain = 1.0f - (1.0f - floor_gain) * (rx_power - rx_low) / (rx_high - rx_low);

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
	           ? 0.80f * prev + 0.20f * frame_power
	           : 0.95f * prev + 0.05f * frame_power;
	aec_rx_store(next);
	return 0;
}

/* ── bsdk_spk_gain (decode-only) ────────────────────────────────────────── */

struct spk_gain_dec_st { struct aufilt_dec_st base; };

static void spk_gain_dec_dtor(void *arg) { (void)arg; }

static int spk_gain_decupd(struct aufilt_dec_st **stp, void **ctx,
                            const struct aufilt *af, struct aufilt_prm *prm,
                            const struct audio *au)
{
	(void)ctx; (void)af; (void)prm; (void)au;
	*stp = mem_deref(*stp);
	struct spk_gain_dec_st *s = mem_zalloc(sizeof(*s), (mem_destroy_h *)spk_gain_dec_dtor);
	if (!s) return ENOMEM;
	*stp = (struct aufilt_dec_st *)s;
	return 0;
}

static int spk_gain_decode(struct aufilt_dec_st *st, struct auframe *af)
{
	(void)st;
	if (af->fmt != AUFMT_S16LE) return 0;
	float g = load_f(&g_spk_gain_bits);
	if (g == 1.0f) return 0;  /* fast-path bypass — no-op at unity */
	apply_gain((int16_t *)af->sampv, af->sampc, g);
	return 0;
}

/* ── Registration ───────────────────────────────────────────────────────── */

static struct aufilt g_mic_gain_filter = {
	.name    = "bsdk_mic_gain",
	.encupdh = mic_gain_encupd,
	.ench    = mic_gain_encode,
};

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

static struct aufilt g_spk_gain_filter = {
	.name    = "bsdk_spk_gain",
	.decupdh = spk_gain_decupd,
	.dech    = spk_gain_decode,
};

/* Whether the half-duplex suppressor should run at all.
 *
 * It exists for platforms that hand us the raw microphone.  Both mobile
 * drivers capture through the OS voice path instead — Android's sles_vc via
 * the VOICE_COMMUNICATION recording preset, iOS's audiounit via
 * VoiceProcessingIO — so the device has already cancelled the echo in
 * hardware before the first sample reaches us.  Ducking TX by another 16.5 dB
 * every time the far end has audio removes no echo that is still there; it
 * just half-duplexes a call the hardware had already made full-duplex, and
 * takes ~0.85 s to let the mic back up after the far end stops.  There is no
 * case where that is the right thing, so it is not an app-overridable choice.
 *
 * The veto only holds while that driver is the one in the path.  Once the app
 * takes the device over (baresdk_audio_use_external), our voice-path driver is
 * displaced and its canceller goes with it — so the suppressor becomes
 * available again.  It still does not switch itself on: an app that owns the
 * device is expected to capture through the platform voice path itself, and
 * has to ask for the fallback explicitly. */
bool bsdk_aec_suppressor_wanted(baresdk_aec_mode_t mode)
{
	if (mode != BARESDK_AEC_SUPPRESSOR)
		return false;

	if (bsdk_platform_has_aec() && !bsdk_audio_external_selected()) {
		info("baresdk: platform audio driver cancels echo in hardware; "
		     "software suppressor stays off\n");
		return false;
	}

	return true;
}

void bsdk_audio_processing_init(bool ns, bool agc,
                                baresdk_aec_mode_t aec_mode,
                                float aec_suppression_level,
                                float mic_db, float spk_db)
{
	struct list *fl = baresip_aufiltl();

	/* Seed atomic state from config before registering filters. */
	if (mic_db != 0.0f)
		store_f(&g_mic_gain_bits, powf(10.0f, mic_db / 20.0f));
	if (spk_db != 0.0f)
		store_f(&g_spk_gain_bits, powf(10.0f, spk_db / 20.0f));
	/* floor = 1.0 - level * 0.85; level=1.0 → 0.15 (default) */
	store_f(&g_aec_floor_bits, 1.0f - aec_suppression_level * 0.85f);

	/* PROTECTED BY RE_MAIN — aufilt_register modifies the aufiltl list. */
	aufilt_register(fl, &g_mic_gain_filter);  /* TX first: raw pre-boost */
	aufilt_register(fl, &g_ns_filter);
	aufilt_register(fl, &g_agc_filter);
	aufilt_register(fl, &g_aec_filter);
	aufilt_register(fl, &g_spk_gain_filter);  /* RX only */

	/* Every enable below must pass the flag, never `if (flag) enable(true)`:
	 * aufilt_register() enables what it registers, so a one-way enable is
	 * not "leave it alone", it is "leave it ON".  These are TX filters and
	 * that is not a subtle difference — bsdk_ns gates to −20 dB, bsdk_agc
	 * pulls toward −20 dBFS with a 0.1 gain floor, and bsdk_aec ducks
	 * 16.5 dB whenever the far end has audio.  Stacked on a microphone the
	 * app asked us not to touch (ns and agc default to false, and AEC_OFF
	 * means off), that is a caller nobody can hear.
	 *
	 * The two gain filters are the exception, and only because they read as
	 * enabled either way: they self-bypass at unity. */
	/* PROTECTED BY RE_MAIN */
	aufilt_enable(fl, "bsdk_mic_gain", true);
	aufilt_enable(fl, "bsdk_spk_gain", true);

	bool aec_on = bsdk_aec_suppressor_wanted(aec_mode);

	/* PROTECTED BY RE_MAIN */
	aufilt_enable(fl, "bsdk_ns",  ns);
	aufilt_enable(fl, "bsdk_agc", agc);
	/* bsdk_aec only for SUPPRESSOR, and only where nothing below us has
	 * already cancelled the echo; WEBRTC uses the webrtc_aec module. */
	aufilt_enable(fl, "bsdk_aec", aec_on);

	/* Say what the microphone actually goes through.  Every one of these is
	 * capable of taking the TX path down by 20 dB, so "what is touching the
	 * mic" is the first question worth answering when a caller reports that
	 * the far end cannot hear them — on a device, from a log, without a
	 * debugger. */
	info("baresdk: TX chain: mic_gain=%s ns=%s agc=%s aec=%s%s\n",
	     mic_db != 0.0f ? "on" : "unity/bypass",
	     ns ? "on" : "off",
	     agc ? "on" : "off",
	     aec_on ? "suppressor" : "off",
	     (!aec_on && aec_mode == BARESDK_AEC_SUPPRESSOR)
	         ? " (platform canceller)" : "");
}

void bsdk_audio_processing_close(void)
{
	/* PROTECTED BY RE_MAIN */
	aufilt_unregister(&g_mic_gain_filter);
	aufilt_unregister(&g_ns_filter);
	aufilt_unregister(&g_agc_filter);
	aufilt_unregister(&g_aec_filter);
	aufilt_unregister(&g_spk_gain_filter);
}
