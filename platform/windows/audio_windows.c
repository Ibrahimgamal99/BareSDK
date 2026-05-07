/**
 * @file audio_windows.c  Windows platform audio stub
 *
 * baresip's wasapi module handles audio playback/capture directly. This file
 * exists only to satisfy the platform source requirement.
 *
 * Future enhancement: IMMNotificationClient registration here for automatic
 * default-device-change notifications (headphone plug/unplug events).
 */

#include "../../src/libbare_internal.h"

int bare_platform_audio_init(void)
{
	/* baresip's wasapi module (loaded by modules_init) handles Windows audio */
	return 0;
}
