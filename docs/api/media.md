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

---

## Audio processing

Enable at init time via `baresdk_config_t`:

```c
cfg.aec = true;   // half-duplex echo suppressor (attenuates TX when RX is loud)
cfg.ns  = true;   // noise suppression (Wiener gate on microphone path)
cfg.agc = true;   // automatic gain control (normalises mic volume to −20 dBFS)
```

Toggle at runtime on any active call without re-dialling:

```c
baresdk_set_aec(true);
baresdk_set_ns(false);
baresdk_set_agc(true);
```

> **Note on AEC:** the built-in suppressor is a half-duplex gate, not full acoustic echo cancellation. For true AEC use platform voice modes: CoreAudio `VoiceProcessingIO`, AAudio `USAGE_VOICE_COMMUNICATION`, or PulseAudio `module-echo-cancel`.

---

## Jitter buffer

Configure adaptive jitter buffering at init time:

```c
cfg.jitter_buffer_min_ms = 20;
cfg.jitter_buffer_max_ms = 150;
```

Adjust at runtime (takes effect on new calls):

```c
baresdk_set_jitter_buffer(20, 200);  // widen buffer on a poor network
baresdk_set_jitter_buffer(10,  80);  // tighten for a low-latency LAN
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
