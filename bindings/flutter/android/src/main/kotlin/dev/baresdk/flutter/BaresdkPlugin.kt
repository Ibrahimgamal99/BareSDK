package dev.baresdk.flutter

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
 * Platform shim for the baresdk FFI plugin.
 *
 * The SIP/media work happens entirely in native code driven over Dart FFI;
 * this class only covers what FFI cannot reach:
 *  - the app cache dir (baresdk's required tmp_dir on Android),
 *  - voice-call audio focus + MODE_IN_COMMUNICATION,
 *  - speakerphone routing,
 *  - ConnectivityManager default-network callbacks, forwarded to Dart so
 *    the SDK can re-register / migrate calls on network handover.
 */
class BaresdkPlugin : FlutterPlugin, MethodChannel.MethodCallHandler {
    private lateinit var channel: MethodChannel
    private lateinit var context: Context
    private val mainHandler = Handler(Looper.getMainLooper())

    private var audioManager: AudioManager? = null
    private var focusRequest: AudioFocusRequest? = null
    private var connectivityManager: ConnectivityManager? = null
    private var networkCallback: ConnectivityManager.NetworkCallback? = null

    override fun onAttachedToEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        context = binding.applicationContext
        channel = MethodChannel(binding.binaryMessenger, "baresdk")
        channel.setMethodCallHandler(this)
        audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
        registerNetworkCallback()
    }

    override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        unregisterNetworkCallback()
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
                audioManager?.isSpeakerphoneOn = on
                result.success(null)
            }
            else -> result.notImplemented()
        }
    }

    // ── Audio focus ─────────────────────────────────────────────────────

    private fun requestAudioFocus() {
        val am = audioManager ?: return
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
                    .build()
            }
            focusRequest?.let { am.requestAudioFocus(it) }
        } else {
            @Suppress("DEPRECATION")
            am.requestAudioFocus(
                null,
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
            am.abandonAudioFocus(null)
        }
        am.isSpeakerphoneOn = false
        am.mode = AudioManager.MODE_NORMAL
    }

    // ── Network change → Dart → baresdk_network_changed() ──────────────

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
