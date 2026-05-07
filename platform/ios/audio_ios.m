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
 * Called once from bare_platform_audio_init() during libbare_init().
 */

#import <AVFoundation/AVFoundation.h>
#include "../../src/libbare_internal.h"

int bare_platform_audio_init(void)
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
		warning("libbare/ios: AVAudioSession setCategory: %s\n",
		        [[error localizedDescription] UTF8String]);
		return EINVAL;
	}

	/* VoiceChat mode enables the hardware echo-cancellation path and sets
	 * mic gain appropriate for telephone-quality voice calls. */
	[session setMode:AVAudioSessionModeVoiceChat error:&error];
	if (error) {
		warning("libbare/ios: AVAudioSession setMode: %s\n",
		        [[error localizedDescription] UTF8String]);
		return EINVAL;
	}

	[session setActive:YES error:&error];
	if (error) {
		warning("libbare/ios: AVAudioSession setActive: %s\n",
		        [[error localizedDescription] UTF8String]);
		return EINVAL;
	}

	return 0;
}
