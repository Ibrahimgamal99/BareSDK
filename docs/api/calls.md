# Calls

## Outgoing call

```c
baresdk_call_handle_t call;
int rc = baresdk_call_invite(acct, "sip:bob@pbx.example.com", &call);
// fires: CALLING → RINGING → ESTABLISHED  (or FAILED)
```

## Incoming call

```c
// In your event callback:
case BARESDK_EV_INCOMING_CALL:
    printf("Incoming from %s\n", ev->u.incoming.from_uri);
    baresdk_call_answer(ev->u.incoming.call);
    break;
```

To reject without answering, call `baresdk_call_hangup(call)` before answering.

## Hang up / terminate

```c
baresdk_call_hangup(call);   // sends BYE; fires BARESDK_EV_CALL_STATE (ENDED)
```

## Hold and resume

```c
baresdk_call_hold(call);     // re-INVITE with sendonly
baresdk_call_resume(call);   // re-INVITE with sendrecv

// Query current state (reads local hold flag — no network round-trip)
bool held = baresdk_call_is_held(call);
```

## DTMF

```c
baresdk_call_send_dtmf(call, '5');   // digit per account dtmf_mode
// valid: '0'-'9', '*', '#', 'A'-'D'
```

DTMF mode is configured per-account via `baresdk_account_config_t.dtmf_mode`:

| Value | Description |
|---|---|
| `BARESDK_DTMF_RFC4733` | RFC 4733 RTP telephony-event (default) |
| `BARESDK_DTMF_SIP_INFO` | SIP INFO `application/dtmf-relay` — legacy gateways |
| `BARESDK_DTMF_AUTO` | Prefer RFC 4733, fall back to SIP INFO |

## Transfer

### Blind transfer (REFER)
```c
baresdk_call_transfer(call, "sip:carol@pbx.example.com");
```

### Attended transfer
```c
// call_a is the original call (to transfer away)
// call_b is the consultation call already established
baresdk_call_attended_transfer(call_a, call_b);
// embeds Replaces header from call_b's dialog
```

### Incoming transfer request

When the remote side sends a REFER to your UA, the SDK fires `BARESDK_EV_TRANSFER_REQUEST`:

```c
case BARESDK_EV_TRANSFER_REQUEST: {
    const char *uri = ev->u.transfer_req.refer_to_uri;
    bool attended   = ev->u.transfer_req.has_replaces;
    printf("Transfer requested to %s (%s)\n", uri,
           attended ? "attended" : "blind");
    // To follow: hang up current call and dial uri with the same account
    break;
}
```

## Enumerate active calls

```c
void my_iter(baresdk_call_handle_t call, void *arg) {
    printf("active call: %p\n", call);
}
baresdk_call_foreach(my_iter, NULL);
```

## Per-dialog custom headers

Headers attached to the **specific call's** subsequent re-INVITEs, BYE, and REFER:

```c
baresdk_call_add_header(call, "X-Call-Context", "helpdesk-123");
```

## Media stats

Stats are delivered two ways:

- **Timed** — `BARESDK_EV_MEDIA_STATS` fires every `cfg.stats_interval_ms` ms (0 = disabled).
- **On demand** — poll synchronously at any time with `baresdk_call_get_stats`.

### C

```c
// Synchronous poll
baresdk_ev_media_stats_t stats;
baresdk_call_get_stats(call, &stats);
printf("MOS-LQ: %.2f  RTT: %.1f ms  loss: %.1f%%\n",
       stats.mos_lq, stats.rtt_ms, stats.loss_pct);

// Timed delivery via event callback
case BARESDK_EV_MEDIA_STATS: {
    const baresdk_ev_media_stats_t *s = &ev->u.stats;
    printf("tick=%u  MOS-LQ=%.2f\n", s->stats_tick, s->mos_lq);
    break;
}
```

### Python

The Python binding provides three ways to access call stats:

#### Push (event-driven, automatic)

Stats fire via `@sdk.on("media_stats")` whenever the SDK emits them (rate set by `stats_interval_ms` in `sdk.configure()`):

```python
@sdk.on("media_stats")
def _(ev):
    print(f"MOS={ev.mos_lq:.2f}  RTT={ev.rtt_ms:.0f}ms  loss={ev.loss_pct:.1f}%")
    # ev is a MediaStatsEvent with all stats fields
```

#### Custom polling rate — `call.poll_stats()`

```python
@sdk.on("established")
def _(ev):
    # Print stats every 2 s regardless of stats_interval_ms
    ev.call.poll_stats(interval=2.0, on_update=lambda s: s.print())
    # s is a CallStats object updated in-place each tick

# Stop polling manually from any thread:
call.stop_polling()
```

#### One-shot snapshot

```python
# New CallStats object with current values
snap = call.stats()
print(snap.mos_lq, snap.rtt_ms, snap.loss_pct)

# Update an existing CallStats object in-place
live = CallStats()
call.fetch_stats(live)
print(live)
```

#### `CallStats` fields

| Field | Type | Description |
|---|---|---|
| `available` | `bool` | `False` until the first update |
| `mos_lq` / `mos_cq` | `float` | MOS listening / conversational quality (1–5) |
| `mos_lq_rx` / `mos_cq_rx` | `float` | RX-side MOS scores |
| `mos_lq_min` / `mos_lq_avg` | `float` | Session min/avg MOS-LQ |
| `mos_method` | `int` | 0 = E-model, 1 = simplified |
| `rtt_ms` | `float` | Round-trip time ms |
| `jitter_ms` | `float` | RX jitter ms |
| `tx_jitter_ms` | `float` | TX jitter ms |
| `loss_pct` / `loss_pct_rx` | `float` | TX / RX packet loss % |
| `packets_sent` / `packets_received` | `int` | RTP packet counters |
| `packets_lost` / `packets_lost_rx` | `int` | Lost packet counters |
| `bytes_sent` / `bytes_received` | `int` | Byte counters |
| `bandwidth_kbps_tx` / `_rx` | `int` | Current bandwidth kbps |
| `avg_bandwidth_kbps_tx` / `_rx` | `int` | Session-average bandwidth kbps |
| `jitter_buffer_ms` | `int` | Current jitter buffer depth ms |
| `jitter_buffer_load` | `int` | Jitter buffer fill level |
| `jitter_buffer_target_ms` | `int` | Adaptive target depth ms |
| `jitter_buffer_adaptive` | `bool` | Adaptive mode active |
| `late_packets` | `int` | Packets arrived too late |
| `discarded_packets` | `int` | Packets discarded |
| `plc_frames` | `int` | PLC concealment frames |
| `plc_ratio` | `float` | PLC ratio (0–1) |
| `codec_name` | `str` | Negotiated codec e.g. `"opus"` |
| `codec_clock_rate` | `int` | Clock rate Hz e.g. `48000` |
| `codec_sample_rate` | `int` | Sample rate Hz |
| `codec_channels` | `int` | Channel count |
| `payload_type` | `int` | RTP payload type |
| `ssrc_tx` / `ssrc_rx` | `int` | SSRC identifiers |
| `remote_addr` | `str` | Remote RTP address:port |
| `audio_level_dbov` | `float` | Speaker level dBov; `NaN` = never measured, `-127` = measured silence |
| `mic_level_dbov` | `float` | Mic level dBov; `NaN` = never measured, `-127` = measured silence |
| `stats_tick` | `int` | Tick counter (1-based) |
| `call_duration_ms` | `int` | Call duration ms |
| `is_final` | `bool` | `True` on the last stats event after hangup |
