package dev.echosdk.flutter

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.net.ConnectivityManager
import android.net.Network
import android.os.Build
import android.os.Handler
import android.os.Looper
import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel

/**
 * Platform shim for the EchoSDK FFI plugin.
 *
 * The SIP/media work happens entirely in native code driven over Dart FFI;
 * this class only covers what FFI cannot reach:
 *  - the app cache dir (EchoSDK's required tmp_dir on Android),
 *  - voice-call audio focus + MODE_IN_COMMUNICATION,
 *  - speakerphone routing,
 *  - ConnectivityManager default-network callbacks, forwarded to Dart so
 *    the SDK can re-register / migrate calls on network handover.
 */
class EchoSDKPlugin : FlutterPlugin, MethodChannel.MethodCallHandler {
    private lateinit var channel: MethodChannel
    private lateinit var context: Context
    private val mainHandler = Handler(Looper.getMainLooper())

    private var audioManager: AudioManager? = null
    private var focusRequest: AudioFocusRequest? = null
    private var connectivityManager: ConnectivityManager? = null
    private var networkCallback: ConnectivityManager.NetworkCallback? = null
    private var audioRouter: AudioRouter? = null
    private var appAudio: AppOwnedAudioEngine? = null

    override fun onAttachedToEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        context = binding.applicationContext
        channel = MethodChannel(binding.binaryMessenger, "echo_sdk")
        channel.setMethodCallHandler(this)
        val am = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
        audioManager = am
        appAudio = AppOwnedAudioEngine(am) { code, message ->
            mainHandler.post {
                channel.invokeMethod(
                    "onAppOwnedAudioError",
                    mapOf("code" to code, "message" to message))
            }
        }
        audioRouter = AudioRouter(
            context,
            onRoutesChanged = {
                // Already posted to the main thread by AudioRouter.
                channel.invokeMethod("onAudioRoutesChanged", null)
            },
            // Pre-31 only: SCO has just connected or dropped, so any stream
            // created before it is stuck on the old device for its lifetime.
            // If the app owns the streams we can rebuild them; if the SDK owns
            // them there is nothing to do and this is a no-op.
            onRouteSettled = { appAudio?.restartStreams() },
        ).also { it.start() }
        registerNetworkCallback()
    }

    override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        unregisterNetworkCallback()
        audioRouter?.stop()
        audioRouter = null
        // Streams die before the mode reverts to MODE_NORMAL — the reverse of
        // the startup order, for the same routing reason.
        appAudio?.disarm()
        appAudio = null
        abandonAudioFocus()
        channel.setMethodCallHandler(null)
    }

    override fun onMethodCall(call: MethodCall, result: MethodChannel.Result) {
        when (call.method) {
            "getCacheDir" -> result.success(context.cacheDir.absolutePath)
            "configureAudioSession" -> {
                val active = call.argument<Boolean>("active") ?: false
                if (active) requestAudioFocus() else abandonAudioFocus()
                result.success(null)
            }
            "setSpeakerphone" -> {
                val on = call.argument<Boolean>("on") ?: false
                val router = audioRouter
                if (router != null) {
                    // Route-aware: OFF returns to the best non-speaker route
                    // (BT > wired > earpiece), not blindly to the earpiece.
                    router.setSpeakerphone(on)
                } else {
                    audioManager?.isSpeakerphoneOn = on
                }
                result.success(null)
            }
            "listAudioRoutes" ->
                result.success(audioRouter?.listRoutes() ?: emptyList<Map<String, Any>>())
            "setAudioRoute" -> {
                val id = call.argument<String>("id")
                if (id == null) {
                    result.error("bad-args", "setAudioRoute needs an id", null)
                } else {
                    result.success(audioRouter?.selectRoute(id)
                        ?: emptyList<Map<String, Any>>())
                }
            }
            "setExternalRouting" -> {
                audioRouter?.externalRouting = call.argument<Boolean>("on") ?: false
                result.success(null)
            }
            "startAppOwnedAudio" -> {
                appAudio?.arm()
                result.success(null)
            }
            "stopAppOwnedAudio" -> {
                appAudio?.disarm()
                result.success(null)
            }
            "appOwnedAudioStatus" ->
                result.success(appAudio?.status() ?: emptyMap<String, Any?>())
            else -> result.notImplemented()
        }
    }

    // ── Audio focus ─────────────────────────────────────────────────────
    //
    // Both of these must finish their work before the method-channel result is
    // sent: the Dart side awaits `configureAudioSession(true)` before it starts
    // a call, precisely so MODE_IN_COMMUNICATION is in force by the time the
    // native core opens the OpenSL streams. Android fixes a stream's routing
    // when it is created, so a mode set that lands late does not move the
    // audio — it produces a call the user cannot hear. Keep this synchronous.

    // When the SDK owns the device, a transient focus loss is survivable —
    // OpenSL keeps running and the far end hears the interruption at worst.
    // When the app owns it, nothing else would tell our capture and playback
    // threads to stand down, so the listener is not optional.
    private val focusListener = AudioManager.OnAudioFocusChangeListener { change ->
        when (change) {
            AudioManager.AUDIOFOCUS_LOSS,
            AudioManager.AUDIOFOCUS_LOSS_TRANSIENT,
            AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK -> appAudio?.pause()
            AudioManager.AUDIOFOCUS_GAIN -> appAudio?.resume()
        }
    }

    private fun requestAudioFocus() {
        val am = audioManager ?: return
        // Mode first, then focus: the mode is what routes the voice streams,
        // and it must be set even if the focus request is refused.
        am.mode = AudioManager.MODE_IN_COMMUNICATION
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            if (focusRequest == null) {
                focusRequest = AudioFocusRequest.Builder(
                    AudioManager.AUDIOFOCUS_GAIN_TRANSIENT
                )
                    .setAudioAttributes(
                        AudioAttributes.Builder()
                            .setUsage(AudioAttributes.USAGE_VOICE_COMMUNICATION)
                            .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
                            .build()
                    )
                    .setOnAudioFocusChangeListener(focusListener, mainHandler)
                    .build()
            }
            focusRequest?.let { am.requestAudioFocus(it) }
        } else {
            @Suppress("DEPRECATION")
            am.requestAudioFocus(
                focusListener,
                AudioManager.STREAM_VOICE_CALL,
                AudioManager.AUDIOFOCUS_GAIN_TRANSIENT
            )
        }
    }

    private fun abandonAudioFocus() {
        val am = audioManager ?: return
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            focusRequest?.let { am.abandonAudioFocusRequest(it) }
        } else {
            @Suppress("DEPRECATION")
            am.abandonAudioFocus(focusListener)
        }
        am.isSpeakerphoneOn = false
        am.mode = AudioManager.MODE_NORMAL
    }

    // ── Network change → Dart → echosdk_network_changed() ──────────────

    private fun registerNetworkCallback() {
        val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE)
                as ConnectivityManager
        connectivityManager = cm
        val cb = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) = notifyDart()
            override fun onLost(network: Network) = notifyDart()
        }
        networkCallback = cb
        try {
            cm.registerDefaultNetworkCallback(cb)
        } catch (_: Exception) {
            // Missing ACCESS_NETWORK_STATE or too many callbacks — the SDK
            // still works, handover just relies on its own detection.
            networkCallback = null
        }
    }

    private fun unregisterNetworkCallback() {
        val cb = networkCallback ?: return
        try {
            connectivityManager?.unregisterNetworkCallback(cb)
        } catch (_: Exception) {
        }
        networkCallback = null
    }

    private fun notifyDart() {
        // MethodChannel calls must run on the platform (main) thread.
        mainHandler.post {
            channel.invokeMethod("onNetworkChanged", null)
        }
    }
}
