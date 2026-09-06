package dev.voxsdk

import android.util.Log

/**
 * Direct access to VoxSDK's app-owned audio device.
 *
 * These are the only SDK calls made from a realtime audio thread, which is why
 * they bypass Dart entirely: the loop has one frame-time (10-20 ms) to capture,
 * hand over, take the far end's audio back and write it out, and a Dart isolate
 * hop plus a GC pause does not fit in that budget.
 *
 * The entry points live in libvoxsdk.so itself (platform/android/
 * audio_external_jni.c), so nothing here needs an NDK build. Deliberately in
 * `dev.voxsdk` rather than `dev.voxsdk.flutter`: it is a plain Android
 * binding and any Android consumer can use it, Flutter or not.
 *
 * PCM is S16LE interleaved at the rate [format] reports. Buffers must be
 * **direct** ByteBuffers — a heap buffer has no stable address to hand C, and
 * is rejected with [EINVAL].
 */
internal object ExternalAudio {

    private const val TAG = "VoxSDKExternalAudio"

    /** No call is capturing or playing. Normal between calls, not an error. */
    const val ENODEV = 19
    const val EINVAL = 22

    /**
     * False when libvoxsdk.so could not be loaded, in which case every call
     * here throws. A host that never turns the feature on is unaffected, so
     * this is reported rather than fatal.
     */
    val available: Boolean = try {
        // The shipped artifact is libvoxsdk.so (all three ABIs), and
        // loadLibrary prepends "lib"/appends ".so" — so the argument is the
        // BARE, lower-case stem. "VoxSDK" here looked for libVoxSDK.so and
        // threw UnsatisfiedLinkError on every (case-sensitive) Android
        // filesystem, which the catch below turned into a permanent
        // available=false: app-owned audio could never arm. Keep this in step
        // with DynamicLibrary.open('libvoxsdk.so') in lib/src/sdk.dart.
        System.loadLibrary("voxsdk")
        true
    } catch (t: Throwable) {
        Log.e(TAG, "libvoxsdk.so not loadable; app-owned audio unavailable", t)
        false
    }

    external fun nativeUseExternal(enable: Boolean): Int
    external fun nativePush(buf: java.nio.ByteBuffer, nsamp: Int): Int
    external fun nativePull(buf: java.nio.ByteBuffer, nsamp: Int): Int
    external fun nativeFormat(out: IntArray): Int
    external fun nativeIsActive(): Boolean
}
