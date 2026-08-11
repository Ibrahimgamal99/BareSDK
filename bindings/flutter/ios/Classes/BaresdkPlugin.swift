import AVFoundation
import Flutter
import Network
import UIKit

/// Platform shim for the baresdk FFI plugin (iOS).
///
/// The SIP/media work happens in the native core driven over dart:ffi; this
/// class covers only what FFI cannot reach:
///  - a writable directory for the SDK's tmp state (parity with Android —
///    the core also auto-detects $TMPDIR on iOS),
///  - audio-session activation around calls and speakerphone routing,
///  - NWPathMonitor network-change callbacks, forwarded to Dart so the SDK
///    can re-register / migrate calls on network handover.
public class BaresdkPlugin: NSObject, FlutterPlugin {
  private var channel: FlutterMethodChannel?
  private var pathMonitor: NWPathMonitor?
  private var lastPathStatus: NWPath.Status?

  public static func register(with registrar: FlutterPluginRegistrar) {
    let channel = FlutterMethodChannel(name: "baresdk",
                                       binaryMessenger: registrar.messenger())
    let instance = BaresdkPlugin()
    instance.channel = channel
    registrar.addMethodCallDelegate(instance, channel: channel)
    instance.startPathMonitor()
  }

  public func detachFromEngine(for registrar: FlutterPluginRegistrar) {
    pathMonitor?.cancel()
    pathMonitor = nil
  }

  public func handle(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
    switch call.method {
    case "getCacheDir":
      result(NSTemporaryDirectory())

    case "configureAudioSession":
      let args = call.arguments as? [String: Any]
      let active = args?["active"] as? Bool ?? false
      configureAudioSession(active: active)
      result(nil)

    case "setSpeakerphone":
      let args = call.arguments as? [String: Any]
      let on = args?["on"] as? Bool ?? false
      let session = AVAudioSession.sharedInstance()
      do {
        try session.overrideOutputAudioPort(on ? .speaker : .none)
      } catch {
        NSLog("baresdk: overrideOutputAudioPort failed: %@",
              error.localizedDescription)
      }
      result(nil)

    default:
      result(FlutterMethodNotImplemented)
    }
  }

  /// The native core sets the category (PlayAndRecord + VoiceChat) at init;
  /// this only toggles activation around calls. Deactivation is best-effort
  /// and notifies other apps their audio may resume.
  private func configureAudioSession(active: Bool) {
    let session = AVAudioSession.sharedInstance()
    do {
      if active {
        try session.setActive(true)
      } else {
        try session.setActive(false,
                              options: .notifyOthersOnDeactivation)
      }
    } catch {
      NSLog("baresdk: audio session setActive(%d) failed: %@",
            active ? 1 : 0, error.localizedDescription)
    }
  }

  /// Forward network-path changes to Dart (drives baresdk_network_changed()).
  private func startPathMonitor() {
    let monitor = NWPathMonitor()
    pathMonitor = monitor
    monitor.pathUpdateHandler = { [weak self] path in
      guard let self = self else { return }
      // NWPathMonitor fires on any property change; only status flips and
      // interface changes matter for SIP handover. Debounce identical states.
      if self.lastPathStatus == path.status,
         path.status != .satisfied {
        return
      }
      self.lastPathStatus = path.status
      DispatchQueue.main.async {
        self.channel?.invokeMethod("onNetworkChanged", arguments: nil)
      }
    }
    monitor.start(queue: DispatchQueue.global(qos: .utility))
  }
}
