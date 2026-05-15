# Curated module set for mobile / embedded targets.
# Platform-specific audio I/O modules are NOT included here; they are
# appended by CMakeLists.txt based on the detected platform:
#   Android → opensles;aaudio
#   Apple   → audiounit;avcapture;coreaudio
#
# Explicitly excluded (too large or desktop-only):
#   ffmpeg, sdl, sdl2, x11, gst, gstVideo, gstRtp, pulse, alsa, pipewire,
#   mosquitto, v4l2, jack, evdev, mqtt, spandsp, aptx, avformat, avcodec

set(BARESDK_MOBILE_MODULES_BASE
  "opus"
  "g711"
  "g722"
  "srtp"
  "dtls_srtp"
  "turn"
  "ice"
  "uuid"
  "account"
  "contact"
  "menu"
  "mwi"
  "presence"
)
list(JOIN BARESDK_MOBILE_MODULES_BASE ";" BARESDK_MOBILE_MODULES_BASE)
