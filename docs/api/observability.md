# Observability

baresdk provides built-in tools for monitoring call quality, tracing SIP traffic, and capturing packets for offline analysis.

---

## Media statistics (RTCP)

Enable periodic stats with `cfg.stats_interval_ms`:

```c
cfg.stats_interval_ms = 5000;  // every 5 seconds
```

You will receive `BARESDK_EV_MEDIA_STATS` events at that interval during every active call.

### Key metrics

| Metric | Field | Unit | Typical range |
|---|---|---|---|
| Packet loss | `loss_pct` | % | 0–5% is acceptable |
| Jitter | `jitter_ms` | ms | < 30 ms is good |
| Round-trip time | `rtt_ms` | ms | < 150 ms is good |
| MOS listening quality | `mos_lq` | 1–5 | ≥ 3.6 is acceptable |
| MOS conversational quality | `mos_cq` | 1–5 | ≥ 3.6 is acceptable |
| TX bandwidth | `bandwidth_kbps_tx` | kbps | codec-dependent |
| RX bandwidth | `bandwidth_kbps_rx` | kbps | codec-dependent |

### MOS methods

Set `cfg.mos_method` to choose the scoring algorithm:

| Method | Enum | Description |
|---|---|---|
| E-Model (ITU-T G.107) | `BARESDK_MOS_EMODEL` | Network-impairment model; uses loss, jitter, RTT |
| Simplified | `BARESDK_MOS_SIMPLIFIED` | Loss-only approximation; lower overhead |

### Synchronous query

At any time you can query the current stats for a call:

```c
baresdk_ev_media_stats_t stats;
int rc = baresdk_call_get_stats(call, &stats);
if (rc == BARESDK_OK) {
    printf("MOS-LQ=%.2f loss=%.1f%%\n", stats.mos_lq, stats.loss_pct);
}
```

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
