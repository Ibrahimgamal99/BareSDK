# Events reference

All events are delivered via the `event_cb` you set in `baresdk_config_t`. The callback is called from the **event dispatch thread** (not `re_main`) so you may call baresdk APIs from inside it.

Event payloads are in the `ev->u` union — access the member matching `ev->type`.

---

## BARESDK_EV_LOG
**`ev->u.log`** — `baresdk_ev_log_t`

Emitted for every internal log message at or below `cfg.log_level`.

| Field | Type | Description |
|---|---|---|
| `message` | `const char *` | Log text |

---

## BARESDK_EV_REG_STATE
**`ev->u.reg`** — `baresdk_ev_reg_state_t`

Fires on every registration state change.

| Field | Type | Description |
|---|---|---|
| `account` | handle | The account that changed state |
| `state` | `baresdk_reg_state_t` | `UNREGISTERED`, `REGISTERING`, `REGISTERED`, `FAILED`, `UNREGISTERING` |
| `error` | `baresdk_error_t` | `BARESDK_OK` when `REGISTERED` |
| `error_str` | `const char *` | Human-readable error; NULL on OK |
| `retry_attempt` | `uint32_t` | How many retries so far |
| `retry_delay_ms` | `uint32_t` | Next retry in this many ms |

---

## BARESDK_EV_INCOMING_CALL
**`ev->u.incoming`** — `baresdk_ev_incoming_call_t`

Fires when a new INVITE is received. Call `baresdk_call_answer()` or `baresdk_call_hangup()`.

| Field | Type | Description |
|---|---|---|
| `account` | handle | Receiving account |
| `call` | handle | The incoming call |
| `from_uri` | `const char *` | Caller SIP URI |
| `display_name` | `const char *` | Caller display name (may be NULL) |

---

## BARESDK_EV_CALL_STATE
**`ev->u.call_state`** — `baresdk_ev_call_state_t`

Fires on every call state transition.

| State | Meaning |
|---|---|
| `CALLING` | INVITE sent, waiting for response |
| `RINGING` | 180 Ringing received |
| `ESTABLISHED` | 200 OK / ACK exchanged; media flowing |
| `HELD` | Call on hold (re-INVITE sendonly) |
| `ENDED` | BYE sent or received |
| `CANCELLED` | CANCEL sent before answer |
| `FAILED` | Error response (4xx, 5xx, 6xx) |

| Field | Type | Description |
|---|---|---|
| `account` | handle | Owning account |
| `call` | handle | The call |
| `state` | `baresdk_call_state_t` | New state |
| `error` | `baresdk_error_t` | Error code when FAILED |
| `reason` | `const char *` | SIP reason phrase (may be NULL) |

---

## BARESDK_EV_CALL_DTMF
**`ev->u.dtmf`** — `baresdk_ev_call_dtmf_t`

DTMF digit received via RFC 4733.

| Field | Type | Description |
|---|---|---|
| `call` | handle | Call that received the digit |
| `digit` | `char` | `'0'`–`'9'`, `'*'`, `'#'`, `'A'`–`'D'` |

---

## BARESDK_EV_SDP_NEGOTIATION
**`ev->u.sdp`** — `baresdk_ev_sdp_negotiation_t`

Emitted after SDP offer/answer exchange (when `cfg.trace_sdp_diff = true`).

| Field | Type | Description |
|---|---|---|
| `call` | handle | |
| `local_sdp` | `const char *` | Full local SDP |
| `remote_sdp` | `const char *` | Full remote SDP |
| `negotiated_codec` | `const char *` | e.g. `"opus/48000/2"` |
| `negotiated_crypto` | `const char *` | `"NONE"`, `"SDES"`, `"DTLS-SRTP"` |
| `rejected_codecs` | `const char * const *` | NULL-terminated array |
| `warnings` | `const char * const *` | NULL-terminated array |

---

## BARESDK_EV_SIP_TRACE
**`ev->u.sip_trace`** — `baresdk_ev_sip_trace_t`

Raw SIP message (when `cfg.trace_sip = true`).

| Field | Type | Description |
|---|---|---|
| `dir` | `baresdk_media_dir_t` | `TX` or `RX` |
| `transport` | `const char *` | `"UDP"`, `"TCP"`, `"TLS"`, `"WS"`, `"WSS"` |
| `remote_addr` | `const char *` | `"1.2.3.4:5060"` |
| `raw_message` | `const char *` | Full SIP message text |
| `timestamp_us` | `uint64_t` | Microseconds since boot |

---

## BARESDK_EV_MEDIA_STATS
**`ev->u.stats`** — `baresdk_ev_media_stats_t`

Periodic RTCP stats (rate set by `cfg.stats_interval_ms`). Also returned synchronously by `baresdk_call_get_stats()`. All RTCP-dependent fields are zero until the first RTCP exchange — packet counters and bandwidth are available immediately.

**Packet counters**

| Field | Type | Description |
|---|---|---|
| `call` | handle | |
| `packets_sent` | `uint32_t` | RTP packets sent |
| `packets_received` | `uint32_t` | RTP packets received |
| `packets_lost` | `uint32_t` | TX-side lost (remote reported we dropped) |
| `packets_lost_rx` | `uint32_t` | RX-side lost (we didn't receive) |
| `bytes_sent` | `uint32_t` | Total RTP bytes sent |
| `bytes_received` | `uint32_t` | Total RTP bytes received |
| `tx_errors` | `uint32_t` | RTP transmit errors |
| `rx_errors` | `uint32_t` | RTP receive errors |

**Loss**

| Field | Type | Description |
|---|---|---|
| `loss_pct` | `float` | TX-side packet loss % |
| `loss_pct_rx` | `float` | RX-side packet loss % |

**Delay and jitter**

| Field | Type | Description |
|---|---|---|
| `rtt_ms` | `float` | Round-trip time (ms) — zero until first RTCP SR/RR |
| `jitter_ms` | `float` | RX interarrival jitter we observe (ms) |
| `tx_jitter_ms` | `float` | TX jitter remote reports back (ms) |

**Jitter buffer**

| Field | Type | Description |
|---|---|---|
| `jitter_buffer_ms` | `uint32_t` | Adaptive buffer depth (ms) |
| `jitter_buffer_load` | `uint32_t` | Packets currently held in buffer |
| `late_packets` | `uint32_t` | Packets that arrived too late |
| `discarded_packets` | `uint32_t` | Packets discarded (overflow or flush) |

**Bandwidth**

| Field | Type | Description |
|---|---|---|
| `bandwidth_kbps_tx` | `uint32_t` | Current TX bitrate (kbps) |
| `bandwidth_kbps_rx` | `uint32_t` | Current RX bitrate (kbps) |
| `avg_bandwidth_kbps_tx` | `uint32_t` | Session-average TX bitrate (kbps) |
| `avg_bandwidth_kbps_rx` | `uint32_t` | Session-average RX bitrate (kbps) |

**MOS scores** — zero until RTCP available

| Field | Type | Description |
|---|---|---|
| `mos_lq` | `float` | MOS Listening Quality (1.0–4.5) |
| `mos_cq` | `float` | MOS Conversational Quality (1.0–4.5) |
| `mos_method` | `baresdk_mos_method_t` | `EMODEL` or `SIMPLIFIED` |

**Codec**

| Field | Type | Description |
|---|---|---|
| `codec_name` | `const char *` | e.g. `"opus"`, `"PCMU"` |
| `codec_clock_rate` | `uint32_t` | RTP clock rate Hz (e.g. 48000) |
| `codec_sample_rate` | `uint32_t` | Audio sample rate Hz |
| `codec_channels` | `uint8_t` | 1 = mono, 2 = stereo |
| `payload_type` | `int` | RTP payload type (0–127) |

**Audio level**

| Field | Type | Description |
|---|---|---|
| `audio_level_dbov` | `float` | Received level in dBov (0 = max, –127 = silent); `NaN` if unavailable |

**Stream identity**

| Field | Type | Description |
|---|---|---|
| `ssrc_tx` | `uint32_t` | Our SSRC |
| `ssrc_rx` | `uint32_t` | Remote SSRC (0 if not yet received) |
| `remote_addr` | `char[64]` | Remote RTP address `"ip:port"` |

---

## BARESDK_EV_REGISTRAR_WARNING
**`ev->u.reg_warn`** — `baresdk_ev_registrar_warning_t`

Non-fatal warning from the registrar (e.g. `Warning:` header).

| Field | Type |
|---|---|
| `message` | `const char *` |

---

## BARESDK_EV_TRANSFER_REQUEST
**`ev->u.transfer_req`** — `baresdk_ev_transfer_req_t`

Incoming REFER request.

| Field | Type | Description |
|---|---|---|
| `account` | handle | |
| `call` | handle | Call that received the REFER |
| `refer_to_uri` | `const char *` | Transfer target URI |
| `has_replaces` | `bool` | true = attended transfer |

---

## BARESDK_EV_MWI
**`ev->u.mwi`** — `baresdk_ev_mwi_t`

Voicemail notification (NOTIFY from MWI subscription).

| Field | Type |
|---|---|
| `account` | handle |
| `messages_waiting` | `bool` |
| `new_voice` | `uint32_t` |
| `old_voice` | `uint32_t` |
| `new_urgent` | `uint32_t` |
| `old_urgent` | `uint32_t` |
| `raw_body` | `const char *` |

---

## BARESDK_EV_MESSAGE
**`ev->u.msg`** — `baresdk_ev_message_t`

Incoming SIP MESSAGE (out-of-dialog instant message).

| Field | Type |
|---|---|
| `account` | handle |
| `from_uri` | `const char *` |
| `body` | `const char *` |
| `content_type` | `const char *` |

---

## BARESDK_EV_PRESENCE_STATE
**`ev->u.presence`** — `baresdk_ev_presence_state_t`

Buddy presence changed (received via SUBSCRIBE/NOTIFY).

| Field | Type |
|---|---|
| `account` | handle |
| `target_uri` | `const char *` |
| `status` | `baresdk_presence_status_t`: `UNKNOWN`, `OPEN`, `CLOSED`, `BUSY` |
