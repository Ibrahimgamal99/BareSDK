/**
 * @file audio_android.c  Android platform audio stub
 *
 * baresip's aaudio/opensles modules handle audio directly. No custom driver
 * code is needed.
 *
 * Future enhancement: AudioRecord/AudioTrack JNI calls for low-latency capture
 * or AEC bypass if baresip's aaudio module proves insufficient.
 */

#include "../../src/libbare_internal.h"

int bare_platform_audio_init(void)
{
	/* baresip's aaudio module (loaded by modules_init) handles Android audio.
	 * OpenSL ES fallback is also loaded for devices below API 26. */
	return 0;
}
