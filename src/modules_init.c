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

#include "libbare_internal.h"

/* Common modules present in both desktop and mobile profiles */
static const char *COMMON_MODULES[] = {
	"opus",
	"g711",
	"g722",
	"srtp",
	"dtls_srtp",
	"stun",
	"turn",
	"ice",
	"account",
	"contact",
	"menu",
	"mwi",
	"presence",
	"uuid",
	"info",    /* SIP INFO — DTMF RFC 2976, application/dtmf-relay */
	NULL
};

/* Desktop-only modules */
static const char *DESKTOP_EXTRA[] = {
	"l16",
	"plc",
	"aubridge",
	"auconv",
	"auresamp",
	NULL
};

/* Platform audio module selected at compile time */
#if defined(LIBBARE_AUDIO_AAUDIO)
static const char *PLATFORM_AUDIO[] = { "aaudio", NULL };
#elif defined(LIBBARE_AUDIO_AUDIOUNIT)
static const char *PLATFORM_AUDIO[] = { "audiounit", "avcapture", "coreaudio", NULL };
#elif defined(LIBBARE_AUDIO_COREAUDIO)
static const char *PLATFORM_AUDIO[] = { "coreaudio", NULL };
#elif defined(LIBBARE_AUDIO_WASAPI)
static const char *PLATFORM_AUDIO[] = { "wasapi", NULL };
#elif defined(LIBBARE_AUDIO_PULSE)
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
			warning("libbare: module '%s': %m\n", list[i], err);
	}
}

int modules_init(void)
{
	load_list(COMMON_MODULES);

#if defined(LIBBARE_PROFILE_DESKTOP)
	load_list(DESKTOP_EXTRA);
#endif

	load_list(PLATFORM_AUDIO);
	return 0;
}
