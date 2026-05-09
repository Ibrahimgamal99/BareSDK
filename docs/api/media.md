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

```c
cfg.aec = true;   // acoustic echo cancellation (prevents echo on speakerphone)
cfg.ns  = true;   // noise suppression
cfg.agc = true;   // automatic gain control (normalises mic volume)
```

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

## Media encryption

| Enum | Description |
|---|---|
| `BARESDK_MEDIA_ENC_NONE` | No encryption (RTP) |
| `BARESDK_MEDIA_ENC_SDES` | SRTP with SDP crypto attributes (RFC 4568) |
| `BARESDK_MEDIA_ENC_DTLS_SRTP` | DTLS-SRTP (RFC 5764) — required for WebRTC |

Set globally in `baresdk_config_t.media_enc` or per-account in `baresdk_account_config_t.media_enc`.
