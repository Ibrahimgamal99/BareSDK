/**
 * @file modules_init.c  Static baresip module loading
 *
 * With STATIC=ON baresip embeds modules but still requires explicit loading.
 * module_load(".", name) looks up the name in the generated mod_table[] and
 * calls me->init() — no config file or filesystem access needed.
 *
 * The module list must match what was compiled into the baresip archive
 * (controlled by cmake/modules-desktop.cmake or modules-mobile.cmake).
 */

#include "baresdk_internal.h"

/* Common modules present in both desktop and mobile profiles.
 *
 * Deliberately excluded:
 *   "account" — reads ~/.baresip/accounts from disk; SDK creates all
 *               accounts programmatically via baresdk_account_create()
 *   "contact" — reads ~/.baresip/contacts from disk (template contains
 *               sip:user@domain;presence=p2p which triggers spurious
 *               SUBSCRIBE dialogs); contacts are added via the API
 *   "menu"    — interactive CLI menu; writes to stderr and has no role
 *               in a library SDK
 */
static const char *COMMON_MODULES[] = {
	"opus",
	"g711",
	"g722",
	"srtp",
	"dtls_srtp",
	"stun",
	"turn",
	"ice",
	"mwi",
	"presence",
	"uuid",
	NULL
};

/* Desktop-only modules */
static const char *DESKTOP_EXTRA[] = {
	"l16",
	"aubridge",
	"auconv",
	"auresamp",
	NULL
};

#if defined(BARESDK_PROFILE_DESKTOP) && defined(BARESDK_HAS_WEBRTC_AEC)
/* Loaded only when aec_mode == WEBRTC at init time.
 * module_load is one-way — we don't unload at runtime. */
static const char *WEBRTC_AEC_LIST[] = { "webrtc_aec", NULL };
#endif

/* Platform audio module selected at compile time */
#if defined(BARESDK_AUDIO_OPENSLES)
/* Android: OpenSLES works on every supported API level (minSdk 24).
 * The preferred driver is baresdk's own "sles_vc" (voice-communication
 * preset → platform AEC/NS); modules_init() falls back to loading the
 * stock "opensles" module when sles_vc fails to initialize.
 * aaudio needs API >= 26 and is not compiled in — see CMakeLists.txt. */
static const char *PLATFORM_AUDIO[] = { "opensles", NULL };
#elif defined(BARESDK_AUDIO_AAUDIO)
static const char *PLATFORM_AUDIO[] = { "aaudio", NULL };
#elif defined(BARESDK_AUDIO_AUDIOUNIT)
/* iOS: audiounit covers capture + playback (VoiceProcessingIO = HW AEC).
 * avcapture (video) is deliberately not compiled — see CMakeLists.txt. */
static const char *PLATFORM_AUDIO[] = { "audiounit", NULL };
#elif defined(BARESDK_AUDIO_COREAUDIO)
static const char *PLATFORM_AUDIO[] = { "coreaudio", NULL };
#elif defined(BARESDK_AUDIO_WASAPI)
static const char *PLATFORM_AUDIO[] = { "wasapi", NULL };
#elif defined(BARESDK_AUDIO_PULSE)
static const char *PLATFORM_AUDIO[] = { "pulse", NULL };
#else
static const char *PLATFORM_AUDIO[] = { NULL };
#endif

static void load_list(const char **list)
{
	if (!list)
		return;
	for (int i = 0; list[i]; i++) {
		int err = module_load(".", list[i]);
		if (err && err != EALREADY)
			warning("baresdk: module '%s': %m\n", list[i], err);
	}
}

int modules_init(void)
{
	const char *audio_mod = PLATFORM_AUDIO[0];

	load_list(COMMON_MODULES);

#if defined(BARESDK_PROFILE_DESKTOP)
	load_list(DESKTOP_EXTRA);
#endif

#if defined(BARESDK_AUDIO_OPENSLES)
	/* Prefer baresdk's voice-communication OpenSLES driver (platform
	 * AEC/NS); fall back to the stock opensles module on failure. */
	if (bsdk_sles_vc_init() == 0)
		audio_mod = "sles_vc";
	else
		load_list(PLATFORM_AUDIO);
#else
	load_list(PLATFORM_AUDIO);
#endif

#if defined(BARESDK_PROFILE_DESKTOP) && defined(BARESDK_HAS_WEBRTC_AEC)
	if (g_bsdk.cfg.aec_mode == BARESDK_AEC_WEBRTC)
		load_list(WEBRTC_AEC_LIST);
#endif

	/* Wire the platform audio module name into baresip config so audio_alloc
	 * picks the real device module.  Must overwrite unconditionally:
	 * aubridge loads earlier (DESKTOP_EXTRA) and ausrc_register/auplay_register
	 * set cfg->audio.{src,play}_mod to the first ausrc/auplay registered.
	 * If we only filled when empty, the platform module would never win. */
	if (audio_mod) {
		struct config *cfg = conf_config();
		str_ncpy(cfg->audio.src_mod, audio_mod,
		         sizeof(cfg->audio.src_mod));
		str_ncpy(cfg->audio.play_mod, audio_mod,
		         sizeof(cfg->audio.play_mod));
#if defined(BARESDK_AUDIO_WASAPI)
		/* wasapi src/play accept either a real endpoint ID or the literal
		 * string "default" — an empty device name makes IMMDeviceEnumerator_
		 * GetDevice("") fail with E_INVALIDARG and the audio thread bails
		 * out before producing any frames. */
		if (cfg->audio.src_dev[0] == '\0')
			str_ncpy(cfg->audio.src_dev, "default",
			         sizeof(cfg->audio.src_dev));
		if (cfg->audio.play_dev[0] == '\0')
			str_ncpy(cfg->audio.play_dev, "default",
			         sizeof(cfg->audio.play_dev));
#endif
	}

	return 0;
}
