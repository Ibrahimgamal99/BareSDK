# baresdk — Python SIP/VoIP SDK

Python binding for **baresdk** — a thread-safe SIP/RTP library for desktop softphones,
VoIP clients, and call-center apps. Built on [baresip](https://github.com/baresip/baresip)
and [libre](https://github.com/baresip/re), delivered as a self-contained shared library.

```bash
pip install baresdk
```

---

## Features

| Category | What's included |
|---|---|
| **Transport** | UDP · TCP · TLS · WS · WSS |
| **Crypto** | SRTP-SDES · DTLS-SRTP |
| **NAT** | ICE · STUN · TURN |
| **Calls** | Invite · Answer · Hangup · Hold/Resume · DTMF |
| **Transfer** | Blind (REFER) · Attended (REFER w/ Replaces) |
| **Messaging** | SIP MESSAGE send/receive |
| **Presence** | PUBLISH · SUBSCRIBE/NOTIFY · BLF · MWI |
| **Reliability** | PRACK / 100rel (RFC 3262) |
| **Audio** | Mic/speaker gain · mute · device hot-switch · AEC · NS · AGC · PCM tap · WAV recording |
| **Observability** | SIP trace · SDP diff · pcap · RTCP/MOS stats · jitter buffer · bandwidth |
| **Multi-account** | Yes — any number of accounts per stack |
| **Thread safety** | Full — call any API from any thread |

---

## Platform requirements

### Linux

One library is not pre-installed on all distros:

```bash
# Ubuntu / Debian
sudo apt install libwebrtc-audio-processing-1

# Fedora / RHEL / CentOS
sudo dnf install webrtc-audio-processing

# Arch
sudo pacman -S webrtc-audio-processing
```

Everything else (`libssl`, `libz`, `libpulse`, …) is already on every desktop Linux.

> If the library is missing, `import baresdk` raises `ImportError` with the exact
> install command for your distro — you don't need to remember the package name.

### Windows

No extra installs needed. OpenSSL, zlib, and opus are all statically embedded in
`baresdk.dll`. The only requirement is the **Visual C++ Redistributable (2015–2022)**.
---

## Quick start

### Register and receive calls

```python
from baresdk import SDK, create_account, register, answer, hangup

with SDK(log_level=1) as sdk:
    account = create_account(sdk, "alice@pbx.example.com", "secret")
    register(account)

    for ev in account.events(timeout=60):
        if ev.type == "reg_state" and ev.state == "registered":
            print("Registered!")

        elif ev.type == "incoming_call":
            print(f"Incoming call from {ev.from_uri}")
            answer(ev.call)

        elif ev.type == "call_state" and ev.state in ("ended", "failed", "cancelled"):
            print("Call ended.")
            break
```

### Make an outgoing call

```python
from baresdk import SDK, create_account, register, dial

with SDK(log_level=1) as sdk:
    account = create_account(sdk, "alice@pbx.example.com", "secret",
                             transport="tls")
    register(account)

    for ev in account.events(timeout=30):
        if ev.type == "reg_state" and ev.state == "registered":
            call = dial(account, "bob@pbx.example.com")

        elif ev.type == "media_stats":
            print(f"MOS: {ev.mos_lq:.2f}  RTT: {ev.rtt_ms:.0f} ms")

        elif ev.type == "call_state" and ev.state == "ended":
            break
```

### WSS + DTLS-SRTP + ICE (WebRTC-compatible PBX)

```python
from baresdk import SDK, create_account, register

with SDK(log_level=1) as sdk:
    account = create_account(sdk,
        "alice@pbx.example.com", "secret",
        transport   = "wss",
        server_url  = "wss://pbx.example.com/ws",
        media_enc   = "dtls_srtp",
        ice_enabled = True,
        stun_server = "stun:stun.l.google.com:19302",
        verify_tls  = True,
    )
    register(account)
```

---

## Audio controls

```python
# Echo cancellation, noise suppression, AGC
sdk.set_aec(True)
sdk.set_ns(True)
sdk.set_agc(True)

# Mic and speaker gain (dB, range −20 to +20; 0.0 = bypass)
sdk.set_mic_gain(6.0)      # boost a quiet mic
sdk.set_speaker_gain(-3.0) # attenuate speaker

# Jitter buffer
sdk.set_jitter_buffer(20, 200)
```

### Echo cancellation modes

| Mode | Duplex | CPU | Best for |
|---|---|---|---|
| `AEC_SUPPRESSOR` (default) | Half-duplex | Negligible | Mobile / simple calls |
| `AEC_WEBRTC` | Full-duplex | Moderate | Desktop softphone / call center |

```python
from baresdk import AEC_WEBRTC

sdk = SDK(aec_mode=AEC_WEBRTC)   # full-duplex — requires libwebrtc-audio-processing-1
```

---

## `create_account` options

| Parameter | Type | Values |
|---|---|---|
| `transport` | str | `"udp"` `"tcp"` `"tls"` `"ws"` `"wss"` |
| `media_enc` | str | `"none"` `"sdes"` `"dtls_srtp"` |
| `ice_enabled` | bool | |
| `stun_server` | str | e.g. `"stun:stun.l.google.com:19302"` |
| `turn_server` | str | e.g. `"turn:turn.example.com:3478"` |
| `server_url` | str | WebSocket URL — overrides transport/host/port |
| `verify_tls` | bool | |
| `audio_codecs` | list[str] | `["opus", "ulaw", "alaw"]` |
| `extra_headers` | dict[str,str] | Added to every outbound REGISTER/INVITE |

---

## Events reference

| `ev.type` | Fired when |
|---|---|
| `"reg_state"` | Registration state changed |
| `"incoming_call"` | Incoming INVITE received |
| `"call_state"` | Call state changed |
| `"dtmf"` | DTMF digit received |
| `"media_stats"` | Periodic RTCP/MOS stats |
| `"sip_trace"` | Raw SIP message (`trace_sip=True`) |
| `"transfer_request"` | Incoming REFER |
| `"mwi"` | Voicemail notification |
| `"message"` | SIP MESSAGE received |
| `"presence_state"` | Buddy presence changed |
| `"quality_alert"` | MOS/loss/jitter/RTT threshold crossed |

**`reg_state` values:** `"unregistered"` `"registering"` `"registered"` `"failed"` `"unregistering"`

**`call_state` values:** `"calling"` `"ringing"` `"established"` `"held"` `"ended"` `"cancelled"` `"failed"`
