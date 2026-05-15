# Debugging SIP

## Init/shutdown trace

`baresdk_init()` and `baresdk_shutdown()` walk through ~14 stages. To trace which step a hang or crash occurs at, set the `BARESDK_DEBUG_INIT` env var before launching:

```bash
# Linux / macOS
BARESDK_DEBUG_INIT=1 ./your_app
```

```powershell
# Windows
$env:BARESDK_DEBUG_INIT=1
.\your_app.exe
```

Output looks like:

```
[bsdk] step 1: deep_copy
[bsdk] step 2: log_init
[bsdk] step 3: libre_init
...
[bsdk] step 14: done
```

Leave the variable unset for normal (silent) operation.

---

## Log levels

Start with `log_level = 2` (info) for call flow visibility:

```c
cfg.log_level = 2;  // 0=err, 1=warn, 2=info, 3=debug
```

Use `log_level = 3` (debug) for maximum detail — internal state machines, timer events, packet parsing. Expect high output volume at this level.

---

## SIP trace

Enable per-message SIP tracing:

```c
cfg.trace_sip = true;
```

Each `BARESDK_EV_SIP_TRACE` event contains the full SIP message. Log it:

```c
case BARESDK_EV_SIP_TRACE: {
    const char *dir = (ev->u.sip_trace.dir == BARESDK_MEDIA_DIR_TX)
                      ? "SEND" : "RECV";
    printf("[%s %s %s]\n%s\n",
           dir, ev->u.sip_trace.transport,
           ev->u.sip_trace.remote_addr,
           ev->u.sip_trace.raw_message);
    break;
}
```

### What to look for

| Check | Where |
|---|---|
| Registration sent? | Look for `REGISTER` in TX direction |
| Auth challenge? | Look for `401 Unauthorized` in RX; then `REGISTER` with `Authorization` header in TX |
| INVITE sent? | TX `INVITE` with correct `Contact` and SDP |
| SDP codecs match? | Compare `m=audio` line in INVITE and 200 OK |
| BYE received? | RX `BYE` — who terminated and why? |
| Error responses? | `4xx`, `5xx`, `6xx` — check `Reason` header |

---

## Pcap capture

Write packets to a file for Wireshark analysis:

```c
baresdk_pcap_start("/tmp/debug.pcap");
// ... reproduce issue ...
baresdk_pcap_stop();
```

Open in Wireshark:

```bash
wireshark /tmp/debug.pcap
```

### Useful Wireshark filters

| Filter | Purpose |
|---|---|
| `sip` | All SIP messages |
| `sip.Method == "REGISTER"` | Registration flow |
| `sip.Method == "INVITE"` | Call setup |
| `sip.Status-Code == 401` | Auth challenges |
| `rtp` | Media packets |
| `rtcp` | Quality reports |
| `sip || rtp || rtcp` | Full call flow |

### Wireshark tips

1. Right-click a SIP message → **Follow SIP Call** to see the complete dialog.
2. **Statistics → RTP → Stream Analysis** for jitter/loss graphs.
3. **Telephony → VoIP Calls** for a call ladder diagram.

---

## SDP negotiation

Enable SDP diff to see codec and encryption negotiation:

```c
cfg.trace_sdp_diff = true;
```

The `BARESDK_EV_SDP_NEGOTIATION` event shows:

```c
case BARESDK_EV_SDP_NEGOTIATION: {
    printf("Codec: %s  Crypto: %s\n",
           ev->u.sdp.negotiated_codec,
           ev->u.sdp.negotiated_crypto);
    // Check rejected_codecs and warnings arrays
    break;
}
```

Common issues:
- **No matching codec** → `negotiated_codec` is NULL; check `audio_codecs` config.
- **Crypto mismatch** → `negotiated_crypto` is "NONE" when you expected "DTLS-SRTP"; check `media_enc` setting.

---

## Media stats

Periodic stats reveal audio quality problems:

```c
cfg.stats_interval_ms = 3000;
```

```c
case BARESDK_EV_MEDIA_STATS: {
    printf("MOS-LQ=%.2f loss=%.1f%% jitter=%.1fms RTT=%.1fms\n",
           ev->u.stats.mos_lq, ev->u.stats.loss_pct,
           ev->u.stats.jitter_ms, ev->u.stats.rtt_ms);
    break;
}
```

### Interpreting stats

| Metric | Good | Warning | Bad |
|---|---|---|---|
| MOS-LQ | ≥ 4.0 | 3.6–4.0 | < 3.6 |
| Loss | < 1% | 1–3% | > 3% |
| Jitter | < 20 ms | 20–50 ms | > 50 ms |
| RTT | < 100 ms | 100–200 ms | > 200 ms |

---

## Registration debugging

| Problem | Check |
|---|---|
| No REGISTER sent | `baresdk_account_register()` called? Network reachable? |
| 401 Unauthorized | Wrong `uri` or `password`; check `auth_user` override |
| 403 Forbidden | Account not provisioned on server; check `uri` domain |
| 408 Timeout | Wrong `server_host`/`server_port`; firewall blocks SIP port |
| Registration drops | `reg_expires` too short; keepalive not working; NAT timeout |

### Retry policy

The SDK retries automatically with exponential backoff:

```
attempt 1: wait 2s    (reg_retry_initial_ms)
attempt 2: wait 4s    (× backoff = 2.0)
attempt 3: wait 8s
attempt 4: wait 16s
...
max:      wait 5 min  (reg_retry_max_ms)
```

Monitor retries via `retry_attempt` and `retry_delay_ms` in `BARESDK_EV_REG_STATE`.

---

## Common error codes

| Code | Meaning | Action |
|---|---|---|
| `BARESDK_ERR_DNS` | Cannot resolve server hostname | Check DNS / network |
| `BARESDK_ERR_TRANSPORT` | TCP/TLS/WebSocket connection failed | Check firewall, TLS cert |
| `BARESDK_ERR_AUTH` | SIP authentication failed | Verify credentials |
| `BARESDK_ERR_TIMEOUT` | No response (timer B/F expired) | Check server reachability |
| `BARESDK_ERR_SERVER_5XX` | Server error | Check server logs |
| `BARESDK_ERR_WS_PROTOCOL_REJECTED` | WebSocket upgrade rejected | Check `ws_origin`, server config |
| `BARESDK_ERR_STATE` | API called in wrong lifecycle state | Check call/account state before calling |

---

## Debug workflow

1. Set `log_level = 2`, `trace_sip = true`, `trace_sdp_diff = true`.
2. Start pcap capture: `baresdk_pcap_start("debug.pcap")`.
3. Reproduce the issue.
4. Stop pcap: `baresdk_pcap_stop()`.
5. Review SIP trace output for message flow.
6. Open pcap in Wireshark for detailed analysis.
7. Set `log_level = 3` if more detail is needed.
