# Curated module set for Linux/macOS/Windows desktop targets.
#
# Audio:   pulse  = PulseAudio / PipeWire (Linux)
#          Replace with audiounit (macOS) or wasapi (Windows) per platform.
# Codecs:  g711 covers both PCMU (G.711 μ-law) and PCMA (G.711 A-law).
# Crypto:  srtp = SDES-SRTP, dtls_srtp = DTLS-SRTP (RFC 5764).
# NAT:     stun/turn/ice = RFC 5389/5766/8445.

set(BARESDK_DESKTOP_MODULES
  # Audio codecs
  "opus"
  "g711"
  "g722"
  "l16"
  "plc"
  # Audio processing
  "aubridge"
  "auconv"
  "auresamp"
  # Audio device (Linux — override per platform)
  "pulse"
  # Crypto
  "srtp"
  "dtls_srtp"
  # NAT traversal
  "stun"
  "turn"
  "ice"
  # SIP features
  "account"
  "contact"
  "menu"
  "mwi"
  "presence"
  "uuid"
  "info"
)
list(JOIN BARESDK_DESKTOP_MODULES ";" BARESDK_DESKTOP_MODULES)
