# Media & audio

## Codecs

Set in `baresdk_config_t`:

```c
cfg.audio_codecs[0]  = BARESDK_CODEC_OPUS;
cfg.audio_codecs[1]  = BARESDK_CODEC_PCMU;
cfg.audio_codec_count = 2;
```

| Codec | Enum | Notes |
|---|---|---|
| Opus | `BARESDK_CODEC_OPUS` | Wideband / fullband, preferred for WebRTC |
| G.711 µ-law | `BARESDK_CODEC_PCMU` | 8 kHz, universal PSTN compatibility |
| G.711 A-law | `BARESDK_CODEC_PCMA` | 8 kHz, European PSTN |
| G.722 | `BARESDK_CODEC_G722` | Wideband, good for HD voice |
| G.726 32 kbit/s | `BARESDK_CODEC_G726_32` | 8 kHz, legacy ADPCM |

### By name

Codecs can also be listed by name, which reaches codecs with no enum constant
(`"g729"`) and any codec a loaded baresip module registers. Names are matched
case-insensitively; aliases: `opus`, `ulaw`/`g711u`/`pcmu`, `alaw`/`g711a`/`pcma`,
`g722`, `g729`, `g726`/`g726-32`.

```c
strcpy(cfg.audio_codec_names[0], "ulaw");
strcpy(cfg.audio_codec_names[1], "opus");
cfg.audio_codec_name_count = 2;
```

Both lists exist on `baresdk_config_t` (global) and
`baresdk_account_config_t` (per account). Precedence, highest first:

1. account `audio_codec_names`
2. account `audio_codecs`
3. global `audio_codec_names`
4. global `audio_codecs`

In Flutter both levels are `List<String>` and use the name form:
`BareSDKConfig(audioCodecs: ['ulaw', 'opus'])` globally,
`AccountConfig(audioCodecs: [...])` per account.

---

## Opus tuning

Fine-tune the Opus encoder at init time via `cfg.opus` (`baresdk_opus_config_t`):

```c
cfg.opus.bitrate    = 32000;  // 0 = auto/VBR (default)
cfg.opus.complexity = 5;      // 0–10 CPU trade-off; -1 = opus default (9)
cfg.opus.cbr        = false;  // constant bitrate; false = VBR (default)
cfg.opus.dtx        = true;   // discontinuous transmission (silence suppression)
cfg.opus.fec        = true;   // in-band forward error correction
cfg.opus.stereo     = false;  // stereo output; false = mono (default)
```

All fields default to 0 / false / -1 (Opus encoder defaults). Only set what you need.

---

## Audio processing

Enable at init time via `baresdk_config_t`:

```c
cfg.aec_mode              = BARESDK_AEC_SUPPRESSOR;  // built-in half-duplex gate
cfg.aec_suppression_level = 1.0f;                    // 0.0–1.0; 1.0 = maximum
cfg.ns                    = true;   // noise suppression (Wiener gate)
cfg.agc                   = true;   // automatic gain control (normalise to −20 dBFS)
```

AEC mode options:

| Value | `aec_mode` int (Python) | Description |
|---|---|---|
| `BARESDK_AEC_OFF` | `0` | No echo cancellation |
| `BARESDK_AEC_SUPPRESSOR` | `1` | Built-in half-duplex gate (default) |
| `BARESDK_AEC_WEBRTC` | `2` | WebRTC AEC (desktop only; requires `libwebrtc-audio-processing-1`) |

Toggle at runtime without re-dialling:

```c
baresdk_set_aec_mode(BARESDK_AEC_OFF);        // disable AEC
baresdk_set_aec_mode(BARESDK_AEC_SUPPRESSOR); // re-enable
baresdk_set_aec_suppression_level(0.5f);      // softer suppression
baresdk_set_ns(false);
baresdk_set_agc(true);
```

**Python**

```python
import baresdk as sdk

# ── Configure before the first create_account() ──────────────────────────────
sdk.configure(
    aec_mode              = 1,    # 1 = suppressor (default); 2 = WebRTC full-duplex
    aec_suppression_level = 1.0,  # 0.0 = no suppression, 1.0 = maximum (default)
    ns                    = True, # noise suppression
    agc                   = True, # automatic gain control
)

# ── Toggle at runtime (any time, from any thread) ────────────────────────────
sdk.set_aec(True)                      # enable (uses mode set at configure())
sdk.set_aec(False)                     # disable

sdk.set_aec_mode(0)                    # 0 = off
sdk.set_aec_mode(1)                    # 1 = suppressor (restore default)
sdk.set_aec_mode(2)                    # 2 = WebRTC full-duplex (desktop only, opt-in build)

sdk.set_aec_suppression_level(0.6)     # tune aggressiveness (suppressor only)
                                        # 0.0 = TX passes through freely
                                        # 1.0 = maximum ducking (default)

sdk.set_ns(True)                       # noise suppression on/off
sdk.set_agc(True)                      # auto gain control on/off

sdk.set_mic_gain(6.0)                  # TX gain dB [-20, +20]; 0 = unity (bypass)
sdk.set_speaker_gain(-3.0)             # RX gain dB [-20, +20]; 0 = unity (bypass)
```

**AEC mode comparison**

| | Suppressor (mode=1, default) | WebRTC (mode=2, opt-in) |
|--|--|--|
| Duplex | Half — ducks mic when far-end is loud | Full — both sides speak simultaneously |
| Double-talk | One side goes quiet | Both parties heard |
| CPU | Negligible | Moderate |
| Platform | All | Desktop only |
| Build | None | `cmake -DBARESDK_WITH_WEBRTC_AEC=ON` + `libwebrtc-audio-processing-1-dev` |

> **WebRTC AEC** must be selected at init time via `sdk.configure(aec_mode=2)`. Only `off ↔ init_mode` transitions are valid at runtime — switching between SUPPRESSOR and WEBRTC returns an error.
>
> **Mobile (Android / iOS):** full-duplex AEC is handled automatically by the OS audio driver. No SDK flag needed.

---

## Jitter buffer

Configure at init time:

```c
cfg.jitter_buffer_min_ms = 20;
cfg.jitter_buffer_max_ms = 150;
cfg.jbuf_type = BARESDK_JBUF_ADAPTIVE;   // default
// cfg.jbuf_type = BARESDK_JBUF_FIXED;   // constant depth at min_ms
```

Adjust at runtime (takes effect on new calls):

```c
baresdk_set_jitter_buffer(20, 200);                    // resize adaptive buffer
baresdk_set_jitter_buffer_type(BARESDK_JBUF_FIXED);    // switch to fixed depth
baresdk_set_jitter_buffer_type(BARESDK_JBUF_ADAPTIVE); // back to adaptive
```

Both bounds default to baresip's built-in values when left at zero.

---

## Per-call DSCP / QoS

Override the RTP DSCP marking for a specific established call:

```c
// EF (46) — Expedited Forwarding, lowest latency
baresdk_call_set_dscp_rtp(call, 46);
```

The global RTP and SIP DSCP values are set at init time via `cfg.dscp_rtp` and `cfg.dscp_sip`.

---

## Mute / unmute

```c
baresdk_audio_mute(call, true);     // mute microphone (TX)
baresdk_audio_mute(call, false);    // unmute microphone

baresdk_audio_mute_rx(call, true);  // silence speaker (RX)
baresdk_audio_mute_rx(call, false); // unmute speaker

// Query current TX mute state (no network round-trip)
bool muted = baresdk_audio_is_muted(call);
```

`baresdk_audio_mute` stops encoding and sending microphone audio.
`baresdk_audio_mute_rx` disables the RTP receiver — the remote continues to send but audio is not decoded or played.

---

## Audio device enumeration

```c
baresdk_audio_device_t devs[32];

int n = baresdk_audio_list_input_devices(devs, 32);
for (int i = 0; i < n; i++)
    printf("[%d] %s%s\n", i, devs[i].name,
           devs[i].is_default ? "  *default*" : "");

n = baresdk_audio_list_output_devices(devs, 32);
for (int i = 0; i < n; i++)
    printf("[%d] %s%s\n", i, devs[i].name,
           devs[i].is_default ? "  *default*" : "");
```

`baresdk_audio_device_t` fields:

| Field | Type | Description |
|---|---|---|
| `name` | `char[128]` | Device identifier — pass to `set_input/output_device` |
| `description` | `char[256]` | Human-readable label (may be empty) |
| `is_default` | `bool` | Platform default device |

Returns the number of entries written (≥ 0). Returns 0 if the audio module has not finished enumeration yet — call again after a short delay or after receiving the first `BARESDK_EV_LOG` message.

---

## Audio device selection

```c
baresdk_audio_set_input_device("Plantronics Headset");    // microphone
baresdk_audio_set_output_device("Plantronics Headset");   // speaker
baresdk_audio_set_input_device(NULL);                     // platform default
```

Device names come from `baresdk_audio_list_input/output_devices()`. The change takes effect immediately on all active calls — no re-dial required.

---

## PCM media tap

Install a callback that receives every decoded audio frame:

```c
void my_tap(baresdk_call_handle_t call,
            baresdk_media_dir_t   dir,   // RX or TX
            const int16_t        *pcm,
            size_t                samples,
            uint32_t              sample_rate,
            uint8_t               channels,
            uint64_t              timestamp_us,
            void                 *userdata)
{
    // copy PCM data if needed; this runs on the audio thread
    memcpy(my_buf, pcm, samples * channels * sizeof(int16_t));
    my_buf_ready = true;
}

baresdk_call_set_media_tap(call, my_tap, my_userdata);
baresdk_call_set_media_tap(call, NULL, NULL);   // remove tap
```

**Rules:**
- Called from the **audio thread** — must return immediately.
- Copy PCM data to your own buffer; don't hold a reference to `pcm` past the callback.
- `dir == BARESDK_MEDIA_DIR_RX` = decoded received audio (what you hear).
- `dir == BARESDK_MEDIA_DIR_TX` = microphone audio before encoding (what you send).

---

## Audio recording

Record a call's audio to a single WAV file (PCM S16LE). Both the received (RX) and sent (TX) audio are clip-summed into one stream — you hear both sides of the conversation.

```c
baresdk_call_record_start(call, "/tmp/call.wav");

// Stop and finalize the WAV header
baresdk_call_record_stop(call);
```

**Output format:** PCM S16LE WAV. Sample rate and channel count are taken from the first audio frame (e.g. 48 kHz/2ch for Opus, 8 kHz/1ch for G.711).

**Typical usage — start on answer, stop on hangup:**

```c
case BARESDK_EV_CALL_STATE:
    if (ev->u.call_state.state == BARESDK_CALL_ESTABLISHED)
        baresdk_call_record_start(ev->u.call_state.call, "/tmp/call.wav");
    if (ev->u.call_state.state == BARESDK_CALL_CLOSED)
        baresdk_call_record_stop(ev->u.call_state.call);
```

**Notes:**
- Returns `EALREADY` if recording is already active.
- Always call `record_stop` before hangup to get a correctly finalized WAV header. If the call is destroyed first, the file is closed but sizes in the header will be wrong.
- Recording is independent of the PCM media tap — both can be active simultaneously.

---

## Media encryption

| Enum | Description |
|---|---|
| `BARESDK_MEDIA_ENC_NONE` | No encryption (RTP) |
| `BARESDK_MEDIA_ENC_SDES` | SRTP with SDP crypto attributes (RFC 4568) |
| `BARESDK_MEDIA_ENC_DTLS_SRTP` | DTLS-SRTP (RFC 5764) — required for WebRTC |

Set globally in `baresdk_config_t.media_enc` or per-account in `baresdk_account_config_t.media_enc`.
