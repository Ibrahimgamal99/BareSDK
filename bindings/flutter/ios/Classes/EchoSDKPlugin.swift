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
///  - audio-session activation around calls,
///  - audio-route enumeration/selection (earpiece/speaker/Bluetooth/wired),
///  - NWPathMonitor network-change callbacks, forwarded to Dart so the SDK
///    can re-register / migrate calls on network handover.
public class BaresdkPlugin: NSObject, FlutterPlugin {
  private var channel: FlutterMethodChannel?
  private var pathMonitor: NWPathMonitor?
  private var lastPathStatus: NWPath.Status?
  private var routeObserver: NSObjectProtocol?

  /// Report-only mode: a CallKit-integrated host app may own routing itself
  /// (CallKit also resets the output override on activation) — enumeration
  /// and change events keep flowing, selection becomes a no-op, so the two
  /// route owners never fight.
  private var externalRouting = false

  /// Reference implementation of the app-owned audio device (VoiceProcessingIO).
  /// Idle until Dart turns it on; the SDK owns the device by default.
  private lazy var appAudio: BaresdkAudioEngine = {
    let engine = BaresdkAudioEngine()
    engine.onError = { [weak self] code, message in
      self?.channel?.invokeMethod("onAppOwnedAudioError",
                                  arguments: ["code": code, "message": message])
    }
    return engine
  }()

  public static func register(with registrar: FlutterPluginRegistrar) {
    let channel = FlutterMethodChannel(name: "baresdk",
                                       binaryMessenger: registrar.messenger())
    let instance = BaresdkPlugin()
    instance.channel = channel
    registrar.addMethodCallDelegate(instance, channel: channel)
    instance.startPathMonitor()
    instance.observeRouteChanges()
  }

  public func detachFromEngine(for registrar: FlutterPluginRegistrar) {
    appAudio.disarm()
    pathMonitor?.cancel()
    pathMonitor = nil
    if let observer = routeObserver {
      NotificationCenter.default.removeObserver(observer)
      routeObserver = nil
    }
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
      if !externalRouting {
        let session = AVAudioSession.sharedInstance()
        do {
          try session.overrideOutputAudioPort(on ? .speaker : .none)
        } catch {
          NSLog("baresdk: overrideOutputAudioPort failed: %@",
                error.localizedDescription)
        }
      }
      result(nil)

    case "listAudioRoutes":
      result(listAudioRoutes())

    case "setAudioRoute":
      let args = call.arguments as? [String: Any]
      guard let id = args?["id"] as? String else {
        result(FlutterError(code: "bad-args",
                            message: "setAudioRoute needs an id",
                            details: nil))
        return
      }
      if !externalRouting { selectRoute(id: id) }
      result(listAudioRoutes())

    case "setExternalRouting":
      let args = call.arguments as? [String: Any]
      externalRouting = args?["on"] as? Bool ?? false
      result(nil)

    case "startAppOwnedAudio":
      appAudio.arm()
      result(nil)

    case "stopAppOwnedAudio":
      appAudio.disarm()
      result(nil)

    case "appOwnedAudioStatus":
      result(appAudio.status())

    // CallKit hosts own session activation (they pass
    // platformAudioActivate: false and manageAudioSession: false), so the
    // engine cannot learn it any other way. Forward CXProvider's
    // didActivate/didDeactivate here.
    case "notifyCallKitAudioActive":
      let args = call.arguments as? [String: Any]
      if args?["active"] as? Bool ?? false {
        appAudio.sessionActivated()
      } else {
        appAudio.sessionDeactivated()
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
        // Tell the engine only once activation actually succeeded — starting
        // the AudioUnit against an inactive session is the ordering bug that
        // owning the device is supposed to remove.
        appAudio.sessionActivated()
      } else {
        appAudio.sessionDeactivated()
        try session.setActive(false,
                              options: .notifyOthersOnDeactivation)
      }
    } catch {
      NSLog("baresdk: audio session setActive(%d) failed: %@",
            active ? 1 : 0, error.localizedDescription)
    }
  }

  // ── Audio routes ──────────────────────────────────────────────────────
  //
  // Under PlayAndRecord + VoiceChat the OUTPUT follows the selected INPUT, so
  // the route list is built from `availableInputs` plus one synthesized
  // loudspeaker entry (the speaker is an output *override*, never an input).
  // `isActive` derives from the actual current output port — the
  // authoritative answer, unlike input-side guessing. A Bluetooth device's
  // OUTPUT port UID differs from its INPUT UID, so matching is always on the
  // ids issued here, never on raw output UIDs.

  private func routeKind(_ port: AVAudioSessionPortDescription) -> String {
    switch port.portType {
    case .builtInMic: return "earpiece"
    case .bluetoothHFP, .bluetoothLE: return "bluetooth"
    default: return "wired-headset" // headsetMic, usbAudio, carAudio, …
    }
  }

  private func activeKind() -> String {
    let out = AVAudioSession.sharedInstance().currentRoute.outputs.first
    switch out?.portType {
    case .some(.builtInSpeaker): return "speaker"
    case .some(.builtInReceiver): return "earpiece"
    case .some(.bluetoothHFP), .some(.bluetoothA2DP), .some(.bluetoothLE):
      return "bluetooth"
    case .none: return "earpiece"
    default: return "wired-headset"
    }
  }

  /// Stable route id for [input]. Semantic for the singletons
  /// ('earpiece' | 'wired-headset' | first 'bluetooth'); port-UID-suffixed
  /// only for additional BT devices.
  private func routeId(_ input: AVAudioSessionPortDescription,
                       sawBluetooth: inout Bool) -> String {
    switch routeKind(input) {
    case "earpiece": return "earpiece"
    case "bluetooth":
      if sawBluetooth { return "bt:\(input.uid)" }
      sawBluetooth = true
      return "bluetooth"
    default: return "wired-headset"
    }
  }

  private func listAudioRoutes() -> [[String: Any]] {
    let session = AVAudioSession.sharedInstance()
    let active = activeKind()
    var routes: [[String: Any]] = []
    var sawBt = false
    for input in session.availableInputs ?? [] {
      let kind = routeKind(input)
      let id = routeId(input, sawBluetooth: &sawBt)
      if routes.contains(where: { ($0["id"] as? String) == id }) { continue }
      routes.append([
        "id": id,
        // Built-ins get '' so the host app can localize ("Earpiece").
        "name": kind == "earpiece" ? "" : input.portName,
        "kind": kind,
        "isActive": kind == active,
      ])
    }
    routes.append([
      "id": "speaker", "name": "", "kind": "speaker",
      "isActive": active == "speaker",
    ])
    return routes
  }

  private func selectRoute(id: String) {
    let session = AVAudioSession.sharedInstance()
    do {
      if id == "speaker" {
        try session.overrideOutputAudioPort(.speaker)
        return
      }
      // Clear the speaker override FIRST — while it is in force,
      // setPreferredInput is ignored.
      try session.overrideOutputAudioPort(.none)
      var sawBt = false
      for input in session.availableInputs ?? [] {
        if routeId(input, sawBluetooth: &sawBt) == id {
          try session.setPreferredInput(input)
          return
        }
      }
      // Unknown id → system default input.
      try session.setPreferredInput(nil)
    } catch {
      NSLog("baresdk: selectRoute(%@) failed: %@", id, error.localizedDescription)
    }
  }

  /// Push route changes to Dart so pickers stay honest without polling —
  /// headset plugged/unplugged, BT connected, CallKit override reset, a
  /// system-initiated move.
  private func observeRouteChanges() {
    routeObserver = NotificationCenter.default.addObserver(
      forName: AVAudioSession.routeChangeNotification,
      object: nil, queue: .main
    ) { [weak self] note in
      guard let self = self,
            let raw = note.userInfo?[AVAudioSessionRouteChangeReasonKey] as? UInt,
            let reason = AVAudioSession.RouteChangeReason(rawValue: raw) else { return }
      switch reason {
      case .newDeviceAvailable, .oldDeviceUnavailable,
           .routeConfigurationChange, .override, .categoryChange:
        self.channel?.invokeMethod("onAudioRoutesChanged", arguments: nil)
      default:
        break
      }
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
