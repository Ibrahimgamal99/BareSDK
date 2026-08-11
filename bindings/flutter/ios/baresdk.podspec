#
# baresdk Flutter plugin — iOS.
#
# Vendors the prebuilt dynamic baresdk.xcframework (built on macOS/CI by
# scripts/build-ios.sh, copied here by scripts/sync-flutter-xcframework.sh)
# and compiles the small Swift shim (audio session / speaker routing /
# NWPathMonitor network callbacks).
#
Pod::Spec.new do |s|
  s.name             = 'baresdk'
  s.version          = '1.0.0'
  s.summary          = 'SIP softphone SDK (baresip-based) for Flutter.'
  s.description      = <<-DESC
UDP/TCP/TLS/WS/WSS SIP registration, calls, codec control, reconnection and
network handover, media statistics and quality alerts — native core driven
over dart:ffi.
                       DESC
  s.homepage         = 'https://github.com/Ibrahimgamal99/BareSDK'
  s.license          = { :type => 'BSD' }
  s.author           = { 'BareSDK' => 'noreply@example.com' }
  s.source           = { :path => '.' }

  s.platform         = :ios, '13.0'
  s.swift_version    = '5.0'
  s.source_files     = 'Classes/**/*'
  s.dependency 'Flutter'

  # Prebuilt native core. Missing? Run scripts/build-ios.sh on macOS (or
  # download the CI artifact) — see docs/quickstart/flutter.md.
  s.vendored_frameworks = 'Frameworks/baresdk.xcframework'
  s.prepare_command = <<-CMD
    if [ ! -d Frameworks/baresdk.xcframework ]; then
      echo "error: baresdk.xcframework is missing." >&2
      echo "Build it with scripts/build-ios.sh (macOS) or download the" >&2
      echo "'ios-xcframework' artifact from the build-mobile CI workflow" >&2
      echo "into bindings/flutter/ios/Frameworks/baresdk.xcframework." >&2
      exit 1
    fi
  CMD

  s.frameworks = 'AVFoundation', 'AudioToolbox', 'CoreAudio', 'Network'

  s.pod_target_xcconfig = {
    'DEFINES_MODULE' => 'YES',
    # Simulator x86_64 slice exists, so no arch exclusions needed.
    'IPHONEOS_DEPLOYMENT_TARGET' => '13.0',
  }
end
