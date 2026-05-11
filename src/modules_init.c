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
#if defined(BARESDK_AUDIO_AAUDIO)
static const char *PLATFORM_AUDIO[] = { "aaudio", NULL };
#elif defined(BARESDK_AUDIO_AUDIOUNIT)
static const char *PLATFORM_AUDIO[] = { "audiounit", "avcapture", "coreaudio", NULL };
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
	load_list(COMMON_MODULES);

#if defined(BARESDK_PROFILE_DESKTOP)
	load_list(DESKTOP_EXTRA);
#endif

	load_list(PLATFORM_AUDIO);

#if defined(BARESDK_PROFILE_DESKTOP) && defined(BARESDK_HAS_WEBRTC_AEC)
	if (g_bsdk.cfg.aec_mode == BARESDK_AEC_WEBRTC)
		load_list(WEBRTC_AEC_LIST);
#endif

	/* Wire the platform audio module name into baresip config so audio_alloc
	 * can find the right module when opening devices. We use PLATFORM_AUDIO[0]
	 * directly rather than walking the registered list, which could return an
	 * earlier-registered module (e.g. aubridge) instead of the platform one. */
	if (PLATFORM_AUDIO[0]) {
		struct config *cfg = conf_config();
		if (cfg->audio.src_mod[0] == '\0')
			str_ncpy(cfg->audio.src_mod, PLATFORM_AUDIO[0],
			         sizeof(cfg->audio.src_mod));
		if (cfg->audio.play_mod[0] == '\0')
			str_ncpy(cfg->audio.play_mod, PLATFORM_AUDIO[0],
			         sizeof(cfg->audio.play_mod));
	}

	return 0;
}
