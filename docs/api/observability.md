# Observability

baresdk provides built-in tools for monitoring call quality, tracing SIP traffic, and capturing packets for offline analysis.

---

## Media statistics (RTCP)

Periodic stats are on by default at `cfg.stats_interval_ms = 2000`:

```c
cfg.stats_interval_ms = 2000;  // default; 0 disables
```

You will receive `BARESDK_EV_MEDIA_STATS` events at that interval during every active call.

!!! warning "0 disables more than the events"
    `stats_interval_ms` is also the master switch for RTCP accounting inside
    baresip (`avt.rtp_stats`). With it at 0 the loss, jitter, RTT and MOS fields
    read back as zero from `baresdk_call_get_stats()` too, and every feature
    derived from them — quality alerts, media-stall detection and adaptive
    bitrate — is inert.

### Key metrics

**Quality scores**

| Metric | Field | Unit | Good | Acceptable | Poor |
|---|---|---|---|---|---|
| TX packet loss | `loss_pct` | % | < 1% | < 5% | ≥ 5% |
| RX packet loss | `loss_pct_rx` | % | < 1% | < 5% | ≥ 5% |
| RX jitter | `jitter_ms` | ms | < 10 | < 30 | ≥ 30 |
| Round-trip time | `rtt_ms` | ms | < 150 | < 300 | ≥ 300 |
| MOS listening quality | `mos_lq` | 1.0–4.5 | ≥ 4.0 | ≥ 3.6 | < 3.6 |
| MOS conversational quality | `mos_cq` | 1.0–4.5 | ≥ 4.0 | ≥ 3.6 | < 3.6 |

**Bandwidth**

| Metric | Field | Unit | Notes |
|---|---|---|---|
| Current TX bitrate | `bandwidth_kbps_tx` | kbps | Instantaneous |
| Current RX bitrate | `bandwidth_kbps_rx` | kbps | Instantaneous |
| Average TX bitrate | `avg_bandwidth_kbps_tx` | kbps | Session average |
| Average RX bitrate | `avg_bandwidth_kbps_rx` | kbps | Session average |

**Jitter buffer**

| Metric | Field | Notes |
|---|---|---|
| Buffer depth | `jitter_buffer_ms` | Current adaptive delay introduced |
| Buffer load | `jitter_buffer_load` | Packets currently buffered |
| Late arrivals | `late_packets` | Arrived after playout deadline |
| Discarded | `discarded_packets` | Overflow or flush |

**Audio level**

| Metric | Field | Notes |
|---|---|---|
| Received level | `audio_level_dbov` | dBov: 0 = max, –127 = silent; `NaN` = unavailable |

**Stream identity** — useful for correlating stats with network captures

| Field | Notes |
|---|---|
| `ssrc_tx` | Our SSRC as seen in Wireshark |
| `ssrc_rx` | Remote SSRC (0 until first RTP received) |
| `remote_addr` | Remote RTP endpoint `"ip:port"` |
| `payload_type` | RTP payload type number |

### MOS methods

Set `cfg.mos_method` to choose the scoring algorithm:

| Method | Enum | Formula | Best for |
|---|---|---|---|
| E-Model (ITU-T G.107) | `BARESDK_MOS_EMODEL` | Full impairment model: loss + jitter + one-way delay | Accurate VoIP quality assessment |
| Simplified | `BARESDK_MOS_SIMPLIFIED` | Telchemy/CISCO: 4.5 − 0.09·loss − 0.0009·jitter − 0.0005·RTT | Quick dashboard metric |

Both produce MOS in the 1.0–4.5 range. `mos_cq` adds an additional penalty for RTT > 300 ms (ITU-T G.114 conversational limit).

### Synchronous query

`baresdk_call_get_stats()` returns the current stats without waiting for the next timer tick. Packet counters and bandwidth are always populated; RTCP fields are zero until the first RTCP exchange.

```c
baresdk_ev_media_stats_t stats;
int rc = baresdk_call_get_stats(call, &stats);
if (rc == BARESDK_OK) {
    printf("MOS-LQ=%.2f  MOS-CQ=%.2f  RTT=%.0f ms  loss=%.1f%%\n",
           stats.mos_lq, stats.mos_cq, stats.rtt_ms, stats.loss_pct);
    printf("TX %u kbps  RX %u kbps  jitter %.1f ms\n",
           stats.bandwidth_kbps_tx, stats.bandwidth_kbps_rx, stats.jitter_ms);
    printf("codec %s  remote %s  SSRC rx=%u\n",
           stats.codec_name, stats.remote_addr, stats.ssrc_rx);
}
```

---

## Quality alerts

Rather than re-deriving thresholds from every stats tick, set them once and get
an edge-triggered event when one is crossed — and another when it is crossed
back. A call that stays bad produces one alert, not one per tick.

```c
cfg.mos_alert_threshold    = 3.5;    /* defaults */
cfg.loss_alert_threshold   = 5.0;
cfg.jitter_alert_threshold = 40.0;
cfg.media_stall_ms         = 4000;
```

| Issue | Fires when | `value` |
|---|---|---|
| `BARESDK_QUALITY_MOS` | `mos_lq` drops below `mos_alert_threshold` | the MOS |
| `BARESDK_QUALITY_LOSS` | `loss_pct` exceeds `loss_alert_threshold` | loss % |
| `BARESDK_QUALITY_JITTER` | `jitter_ms` exceeds `jitter_alert_threshold` | jitter ms |
| `BARESDK_QUALITY_MEDIA_STALL` | no inbound RTP for `media_stall_ms` | stall ms |

`MEDIA_STALL` is not a metric threshold — it is the *absence* of inbound RTP
while the call is neither held nor mid-handover. It is the one condition that no
amount of reading the other metrics will reveal, because when RTP stops the
metrics simply stop changing. Suppressed on held calls and during a handover
migration, where `BARESDK_EV_NETWORK` narrates the same outage in more detail.

```c
case BARESDK_EV_QUALITY_ALERT: {
    const baresdk_ev_quality_alert_t *a = &ev->u.quality_alert;
    printf("%s %s: %.1f (threshold %.1f)\n",
           a->recovering ? "recovered" : "degraded",
           issue_name(a->issue), a->value, a->threshold);
    break;
}
```

Set any threshold to 0 to disable that alert. See
[Degraded links](../guides/degraded_links.md) for what to do about each one.

---

## SIP trace

Set `cfg.trace_sip = true` to receive `BARESDK_EV_SIP_TRACE` for every SIP message sent or received:

```c
cfg.trace_sip = true;
```

Each event contains:

| Field | Description |
|---|---|
| `dir` | `BARESDK_MEDIA_DIR_TX` (sent) or `BARESDK_MEDIA_DIR_RX` (received) |
| `transport` | `"UDP"`, `"TCP"`, `"TLS"`, `"WS"`, `"WSS"` |
| `remote_addr` | `"ip:port"` of the remote endpoint |
| `raw_message` | Complete SIP message text |
| `timestamp_us` | Microseconds since boot |

Example handler:

```c
case BARESDK_EV_SIP_TRACE: {
    const char *arrow = (ev->u.sip_trace.dir == BARESDK_MEDIA_DIR_TX) ? ">>>" : "<<<";
    printf("[%s %s %s]\n%s\n---\n",
           arrow, ev->u.sip_trace.transport, ev->u.sip_trace.remote_addr,
           ev->u.sip_trace.raw_message);
    break;
}
```

**Warning:** SIP trace generates high event volume. Enable only for debugging; disable in production to reduce CPU and memory overhead.

---

## SDP negotiation trace

Set `cfg.trace_sdp_diff = true` to receive `BARESDK_EV_SDP_NEGOTIATION` after each SDP offer/answer exchange:

```c
cfg.trace_sdp_diff = true;
```

The event payload includes the negotiated codec, media encryption, rejected codecs, and any warnings.

---

## Pcap capture

Write SIP and RTP packets to a Wireshark-compatible pcap file:

```c
// Start capture
baresdk_pcap_start("/tmp/capture.pcap");

// ... make calls ...

// Stop capture and flush
baresdk_pcap_stop();
```

The pcap file contains synthetic Ethernet/IP/UDP headers around each packet so Wireshark can decode the SIP and RTP layers without a network interface capture.

**Workflow:**

1. Start pcap before initiating calls.
2. Reproduce the issue.
3. Stop pcap.
4. Open the file in Wireshark: `wireshark /tmp/capture.pcap`
5. Filter SIP with `sip`, RTP with `rtp`, or RTCP with `rtcp`.

---

## Log levels

Set `cfg.log_level` to control verbosity:

| Level | Value | What you see |
|---|---|---|
| Error | 0 | Failures and crashes only |
| Warning | 1 | Configuration issues, retries |
| Info | 2 | Registration state, call lifecycle |
| Debug | 3 | Internal state machines, packet details |

Log messages are delivered via `BARESDK_EV_LOG` events. To suppress events entirely, set `log_level = -1` (no events emitted — only stats/trace events).

---

## Putting it all together

A typical monitoring setup:

```c
baresdk_config_t cfg;
baresdk_config_init(&cfg);

cfg.log_level         = 1;     // warnings
cfg.stats_interval_ms = 5000;  // RTCP stats every 5 s
cfg.trace_sip         = false; // enable on demand
cfg.trace_sdp_diff    = true;  // always track codec negotiation
cfg.mos_method        = BARESDK_MOS_EMODEL;

// In production: no pcap. On bug report: enable pcap + SIP trace.
```
