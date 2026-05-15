/**
 * @file audio_macos.c  macOS platform audio stub
 *
 * baresip's coreaudio module handles audio directly. No custom driver code
 * is needed.
 *
 * Future enhancement: AVCaptureDevice notification observer here for
 * automatic device-change events (e.g., AirPods connect/disconnect).
 */

#include "../../src/baresdk_internal.h"

int bsdk_platform_audio_init(void)
{
	/* baresip's coreaudio module (loaded by modules_init) handles macOS audio */
	return 0;
}
