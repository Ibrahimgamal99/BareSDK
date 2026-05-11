# Quick start — Python

## Prerequisites

```bash
# Ubuntu/Debian
sudo apt install libssl3 zlib1g

# Fedora/RHEL
sudo dnf install openssl-libs zlib
```

---

## One-command setup

```bash
bash bindings/python/build.sh
```

This does everything in one step:
1. Builds the SDK if the `.so` is missing
2. Regenerates `_baresdk_clean.h` from `include/baresdk.h` so the binding stays in sync
3. Copies the `.so` into the package directory — no `LD_LIBRARY_PATH` needed
4. Installs the Python package (`pip install -e`)

Re-run `build.sh` whenever `include/baresdk.h` or the C source changes.

---

## Manual setup (alternative)

Build the shared library for your platform:

| Platform | Command | Output |
|---|---|---|
| Linux | `bash scripts/build-linux.sh` | `dist/linux/x86_64/baresdk.so` |
| macOS | `bash scripts/build-macos.sh` | `dist/macos/universal/baresdk.dylib` |
| Windows | `.\scripts\build-windows.ps1` | `dist\windows\x64\baresdk.dll` |
| Android | `bash scripts/build-android.sh` | `dist/android/<ABI>/baresdk.so` |

Then install the package:
```bash
pip install bindings/python
```

The loader searches for the library in this order:

| Priority | Source |
|---|---|
| 1 | `BARESDK_LIB` env var — absolute path to the `.so`/`.dylib`/`.dll` |
| 2 | `LD_LIBRARY_PATH` / `DYLD_LIBRARY_PATH` directories |
| 3 | Package directory — the `.so` copied there by `build.sh` |
| 4 | Repo `dist/` directory — works from a source checkout |
| 5 | System library paths |

To pin a specific build:
```bash
export BARESDK_LIB=/abs/path/to/baresdk.so
# or
export LD_LIBRARY_PATH=/abs/path/to/dir:$LD_LIBRARY_PATH
```

---

## Register and wait for a call

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

    account.destroy()
```

---

## Make an outgoing call

```python
from baresdk import SDK, create_account, register, dial

with SDK(log_level=1, stats_interval_ms=5000) as sdk:
    account = create_account(sdk, "alice@pbx.example.com", "secret",
                             transport="tls")
    register(account)

    call = None
    for ev in account.events(timeout=30):
        if ev.type == "reg_state" and ev.state == "registered":
            call = dial(account, "bob@pbx.example.com")

        elif ev.type == "media_stats":
            print(f"MOS: {ev.mos_lq:.2f}  RTT: {ev.rtt_ms:.0f} ms")

        elif ev.type == "call_state" and ev.state == "ended":
            break

    account.destroy()
```

---

## Custom TLS + ICE

```python
from baresdk import SDK, create_account, register

with SDK(log_level=2, verify_server=True) as sdk:
    account = create_account(sdk,
        "alice@pbx.example.com", "secret",
        transport    = "wss",
        server_url   = "wss://pbx.example.com/ws",
        ice_enabled  = True,
        stun_server  = "stun:stun.l.google.com:19302",
        verify_tls   = True,
    )
    register(account)
```

---

## Runtime audio quality controls

```python
# Toggle filters on the fly at any time:
sdk.set_aec(True)
sdk.set_ns(False)
sdk.set_agc(True)

# Change jitter buffer bounds (takes effect on new calls):
sdk.set_jitter_buffer(20, 200)   # widen on a poor network

# Set per-call RTP DSCP on an established call:
call.set_dscp_rtp(46)  # EF — Expedited Forwarding
```

---

## Microphone and speaker gain

Manual dB gain control on the TX (mic) and RX (speaker) paths, independent of AGC.
Range: −20 to +20 dB. `0.0` = unity — fast-path bypass with no per-sample work.
Takes effect within one audio frame (~20 ms), safe to call from any thread.

```python
from baresdk import SDK

sdk = SDK(...)

# Boost a quiet USB mic by 6 dB (≈ 2× amplitude):
sdk.set_mic_gain(6.0)

# Attenuate the speaker by 3 dB:
sdk.set_speaker_gain(-3.0)

# Back to unity (bypass):
sdk.set_mic_gain(0.0)
sdk.set_speaker_gain(0.0)
```

> **Interaction with AGC**: mic gain is applied *before* NS/AGC/AEC on the TX chain.
> AGC then normalises the boosted signal to −20 dBFS. If AGC is off, the raw dB boost
> reaches the encoder as-is.

---

## Echo cancellation

### Simple on/off (all platforms)

```python
sdk.set_aec(True)   # enable — uses the mode configured at init
sdk.set_aec(False)  # disable
```

The default mode is `AEC_SUPPRESSOR`: a built-in half-duplex TX suppressor that
attenuates the microphone when the far end is loud. It works on all platforms
with zero external dependencies.

### Tuning the suppressor

```python
from baresdk import AEC_OFF, AEC_SUPPRESSOR, AEC_WEBRTC

# 0.0 = no suppression (TX passes through freely)
# 1.0 = maximum suppression (default — −16.5 dB floor when RX is active)
sdk.set_aec_suppression_level(0.6)   # medium: less ducking on double-talk
sdk.set_aec_suppression_level(1.0)   # restore default
```

### Full-duplex WebRTC AEC (desktop, opt-in)

For true acoustic echo cancellation where both parties can speak simultaneously:

```python
# At init time — choose the backend:
config.aec_mode = AEC_WEBRTC   # requires opt-in build (see below)

# At runtime — only AEC_OFF ↔ AEC_WEBRTC transitions are valid:
sdk.set_aec(False)   # pause
sdk.set_aec(True)    # resume WebRTC AEC
```

**Requires:**
1. Build with: `cmake -DBARESDK_WITH_WEBRTC_AEC=ON ...`
2. System library: `libwebrtc-audio-processing-1-dev` (Debian/Ubuntu) or equivalent
3. Desktop platform only — `AEC_WEBRTC` returns `ENOTSUP` on Android/iOS

> **Mobile**: on Android and iOS, full-duplex AEC is handled by the OS audio driver
> (AAudio `VOICE_COMMUNICATION` / `AVAudioSession .voiceChat`).
> No SDK configuration needed — the platform does it automatically.

### AEC mode comparison

| | `AEC_SUPPRESSOR` (default) | `AEC_WEBRTC` (advanced) |
|--|--|--|
| **Duplex** | Half-duplex (ducks TX when RX is loud) | Full-duplex (cancels echo while both speak) |
| **Double-talk** | One side goes quiet | Both parties heard simultaneously |
| **CPU** | Negligible | Moderate |
| **Platform** | All | Desktop only |
| **Build flag** | None | `BARESDK_WITH_WEBRTC_AEC=ON` |
| **Best for** | Mobile / low-power / simple calls | Desktop softphone / professional voice |

---

## Top-level functions

All common operations are importable directly from `baresdk` — no constants needed for the common case:

| Function | Description |
|---|---|
| `create_account(sdk, uri, password, **kwargs)` | Create an account. Accepts string values for `transport`, `media_enc`, `rel100`. |
| `register(account)` | Register and return the account (chainable). |
| `dial(account, uri)` | Place an outbound call. Auto-prefixes `sip:` if missing. |
| `hangup(call)` | Hang up or reject a call. |
| `answer(call)` | Answer an incoming call. |

`create_account` accepts these string values:

| Kwarg | String values |
|---|---|
| `transport` | `"udp"` `"tcp"` `"tls"` `"ws"` `"wss"` |
| `media_enc` | `"none"` `"sdes"` `"dtls_srtp"` |
| `rel100` | `"disabled"` `"enabled"` `"required"` |
| `extra_headers` | `dict[str, str]` — calls `account.add_header()` for each pair |

---

## Event types and states

Events have a `.type` string — no imports needed for dispatch:

| `ev.type` | When fired |
|---|---|
| `"reg_state"` | Registration state changed |
| `"incoming_call"` | Incoming INVITE received |
| `"call_state"` | Call state changed |
| `"dtmf"` | DTMF digit received |
| `"sdp_negotiation"` | SDP offer/answer complete |
| `"media_stats"` | Periodic RTP stats |
| `"sip_trace"` | Raw SIP message (when `trace_sip=True`) |
| `"log"` | SDK log message |
| `"transfer_request"` | Incoming REFER |
| `"mwi"` | Voicemail notification |
| `"message"` | SIP MESSAGE received |
| `"presence_state"` | Buddy presence changed |
| `"quality_alert"` | Quality threshold crossed |
| `"registrar_warning"` | Non-fatal registrar warning |

`RegStateEvent.state` string values: `"unregistered"` `"registering"` `"registered"` `"failed"` `"unregistering"`

`CallStateEvent.state` string values: `"calling"` `"ringing"` `"established"` `"held"` `"ended"` `"cancelled"` `"failed"`

`SipTraceEvent.direction`: `"tx"` or `"rx"`

`PresenceStateEvent.status`: `"unknown"` `"open"` `"closed"` `"busy"`

`QualityAlertEvent.issue`: `"mos"` `"loss"` `"jitter"` `"rtt"`

---

## See also
- Full example: [bindings/python/examples/quickstart.py](../../bindings/python/examples/quickstart.py)
- Events: [events reference](../api/events.md)
- Media & audio API: [api/media.md](../api/media.md)
