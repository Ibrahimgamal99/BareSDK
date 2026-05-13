# baresdk

Python bindings for a thread-safe SIP/RTP library. Built on [baresip](https://github.com/baresip/baresip) + [libre](https://github.com/baresip/re).

```bash
pip install baresdk
```
---

## Features

| Category | |
|---|---|
| Transport | UDP · TCP · TLS · WS · WSS |
| Crypto | SRTP-SDES · DTLS-SRTP |
| NAT | ICE · STUN · TURN |
| Calls | Invite · Answer · Hangup · Hold/Resume · DTMF · Blind/Attended transfer |
| Messaging | SIP MESSAGE · Presence · BLF · MWI |
| Audio | AEC · NS · AGC · gain · mute · device hot-switch · PCM tap · WAV recording |
| Stats | RTCP/MOS · jitter · bandwidth · SIP trace · pcap |
| Multi-account | ✓ — unlimited accounts per stack |
| Thread-safe | ✓ — call any API from any thread |

---

## Requirements

**Linux** — two runtime libraries required:

```bash
sudo apt install libssl3 libpulse0                    # Ubuntu/Debian
sudo dnf install openssl-libs pulseaudio-libs         # Fedora/RHEL
sudo pacman -S openssl libpulse                       # Arch
```

> `import baresdk` prints the exact install command if either is missing.  
> `libwebrtc-audio-processing-1` is optional — enables `AEC_WEBRTC` full-duplex mode; the SDK works without it.

**Windows** — only the [Visual C++ Redistributable 2015–2022](https://aka.ms/vs/17/release/vc_redist.x64.exe) required. OpenSSL, zlib, and opus are statically embedded.
---

## Quick start

```python
from baresdk import SDK, create_account, register, answer, dial

# Receive calls
with SDK() as sdk:
    acc = create_account(sdk, "alice@pbx.example.com", "secret")
    register(acc)
    for ev in acc.events(timeout=60):
        if ev.type == "incoming_call":
            answer(ev.call)
        elif ev.type == "call_state" and ev.state in ("ended", "failed"):
            break

# Make a call
with SDK() as sdk:
    acc = create_account(sdk, "alice@pbx.example.com", "secret")
    register(acc)
    for ev in acc.events(timeout=30):
        if ev.type == "reg_state" and ev.state == "registered":
            call = dial(acc, "bob@pbx.example.com")
        elif ev.type == "call_state" and ev.state == "ended":
            break
```

**WSS + DTLS-SRTP + ICE:**
```python
acc = create_account(sdk, "alice@pbx.example.com", "secret",
    transport="wss", server_url="wss://pbx.example.com/ws",
    media_enc="dtls_srtp", ice_enabled=True,
    stun_server="stun:stun.l.google.com:19302")
```

---

## Audio

```python
sdk.set_aec(True); sdk.set_ns(True); sdk.set_agc(True)
sdk.set_mic_gain(6.0)       # dB, range −20 to +20
sdk.set_speaker_gain(-3.0)
sdk.set_jitter_buffer(20, 200)
```

| AEC mode | Quality | Requires |
|---|---|---|
| `AEC_SUPPRESSOR` *(default)* | Half-duplex | nothing |
| `AEC_WEBRTC` | Full-duplex | `libwebrtc-audio-processing-1` |

```python
from baresdk import AEC_WEBRTC
sdk = SDK(aec_mode=AEC_WEBRTC)
```

---

## `create_account` parameters

| Parameter | Type | Example |
|---|---|---|
| `transport` | str | `"udp"` `"tcp"` `"tls"` `"ws"` `"wss"` |
| `media_enc` | str | `"none"` `"sdes"` `"dtls_srtp"` |
| `ice_enabled` | bool | |
| `stun_server` | str | `"stun:stun.l.google.com:19302"` |
| `turn_server` | str | `"turn:turn.example.com:3478"` |
| `server_url` | str | WebSocket URL — overrides host/port |
| `verify_tls` | bool | |
| `audio_codecs` | list[str] | `["opus", "ulaw", "alaw"]` |
| `extra_headers` | dict | Added to every REGISTER/INVITE |

---

## Events

| `ev.type` | When |
|---|---|
| `reg_state` | Registration changed — `unregistered` · `registering` · `registered` · `failed` |
| `incoming_call` | INVITE received |
| `call_state` | Call changed — `calling` · `ringing` · `established` · `held` · `ended` · `failed` |
| `dtmf` | Digit received |
| `media_stats` | Periodic RTCP/MOS report |
| `transfer_request` | REFER received |
| `message` | SIP MESSAGE received |
| `presence_state` | Buddy presence changed |
| `mwi` | Voicemail notification |
| `quality_alert` | MOS/loss/jitter/RTT threshold crossed |
| `sip_trace` | Raw SIP message *(enable with `trace_sip=True`)* |

---