/**
 * @file audio_linux.c  Linux platform audio stub
 *
 * baresip's pulse/alsa module handles audio playback/capture directly. No
 * custom driver code is needed.
 *
 * Future enhancement: native PipeWire client here if baresip's pulse module
 * proves insufficient (e.g. for low-latency audio processing or AEC bypass).
 */

#include "../../src/libbare_internal.h"

int bare_platform_audio_init(void)
{
	/* baresip's pulse module (loaded by modules_init) handles Linux audio */
	return 0;
}
