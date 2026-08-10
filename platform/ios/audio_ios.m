/**
 * @file audio_ios.m  iOS platform audio configuration
 *
 * iOS requires explicit AVAudioSession configuration before VoIP audio works.
 * This must be compiled as Objective-C (.m) and linked against AVFoundation.
 *
 * Category:  PlayAndRecord — simultaneous input + output.
 * Mode:      VoiceChat — activates hardware AEC and appropriate gain for
 *            held-to-ear calls. Also routes to earpiece by default; the app
 *            can override to speaker via AVAudioSession.overrideOutputAudioPort.
 * Options:   AllowBluetooth + AllowBluetoothA2DP — enable BT headset routing.
 *            MixWithOthers is intentionally omitted so our session takes
 *            exclusive control of the hardware AEC path.
 *
 * TODO(ios): not yet wired up — bsdk_platform_audio_init() has no call site
 * in core init, the top-level CMakeLists declares LANGUAGES C only (no OBJC),
 * and -framework AVFoundation is not linked. Complete when iOS builds land.
 */

#import <AVFoundation/AVFoundation.h>
#include <errno.h>
#include "../../src/baresdk_internal.h"

int bsdk_platform_audio_init(void)
{
	AVAudioSession *session = [AVAudioSession sharedInstance];
	NSError *error = nil;

	AVAudioSessionCategoryOptions opts =
		AVAudioSessionCategoryOptionAllowBluetooth |
		AVAudioSessionCategoryOptionAllowBluetoothA2DP;

	[session setCategory:AVAudioSessionCategoryPlayAndRecord
	         withOptions:opts
	               error:&error];
	if (error) {
		warning("baresdk/ios: AVAudioSession setCategory: %s\n",
		        [[error localizedDescription] UTF8String]);
		return EINVAL;
	}

	/* VoiceChat mode enables the hardware echo-cancellation path and sets
	 * mic gain appropriate for telephone-quality voice calls. */
	[session setMode:AVAudioSessionModeVoiceChat error:&error];
	if (error) {
		warning("baresdk/ios: AVAudioSession setMode: %s\n",
		        [[error localizedDescription] UTF8String]);
		return EINVAL;
	}

	[session setActive:YES error:&error];
	if (error) {
		warning("baresdk/ios: AVAudioSession setActive: %s\n",
		        [[error localizedDescription] UTF8String]);
		return EINVAL;
	}

	return 0;
}
