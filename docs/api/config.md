# Configuration reference

Always start with `baresdk_config_init(&cfg)` to zero-fill and set `version`/`struct_size`.

## baresdk_config_t

### Forward-compat guard

| Field | Type | Default | Description |
|---|---|---|---|
| `version` | `uint32_t` | 1 | Set by `baresdk_config_init`. Do not change. |
| `struct_size` | `size_t` | `sizeof(baresdk_config_t)` | Set by `baresdk_config_init`. Do not change. |

### Transport

| Field | Type | Default | Description |
|---|---|---|---|
| `transport` | `baresdk_transport_t` | `UDP` | Default transport: UDP, TCP, TLS, WS, WSS |
| `local_ip` | `const char *` | NULL | Bind IP. NULL = auto-select. |
| `local_port` | `uint16_t` | 0 | Local SIP port. 0 = OS-assigned. |
| `bind_interface` | `const char *` | NULL | Interface name e.g. `"wlan0"`. NULL = any. |
| `prefer_ipv6` | `bool` | false | Prefer IPv6 addresses for ICE candidates. |
| `sip_domain` | `const char *` | NULL | AOR domain e.g. `"pbx.example.com"`. |

### Server address (pick one form)

**Simple form** (UDP/TCP/TLS, default ports):
```
transport   = BARESDK_TRANSPORT_TLS
server_host = "pbx.example.com"
server_port = 0   → uses 5061
```

**URL form** (required for WS/WSS):
```
server_url = "wss://pbx.example.com:8089/ws"
```

| Field | Type | Default | Description |
|---|---|---|---|
| `server_url` | `const char *` | NULL | Full URL. Overrides transport/server_host/server_port. |
| `server_host` | `const char *` | NULL | Hostname or IP (simple form). |
| `server_port` | `uint16_t` | 0 | 0 = transport default (5060/5061). |
| `outbound_proxy` | `const char *` | NULL | Override outbound proxy (NULL = auto from server info). |

### TLS / WSS

| Field | Type | Default | Description |
|---|---|---|---|
| `ca_cert_path` | `const char *` | NULL | CA bundle path. NULL = system store. |
| `client_cert` | `const char *` | NULL | Client certificate PEM path. |
| `client_key` | `const char *` | NULL | Client private key PEM path. |
| `verify_server` | `bool` | true | Verify server certificate. |
| `sni_hostname` | `const char *` | NULL | SNI override (for reverse proxy). NULL = derive from URL. |
| `user_agent` | `const char *` | NULL | `User-Agent` header value. |

### WebSocket-specific

| Field | Type | Default | Description |
|---|---|---|---|
| `ws_origin` | `const char *` | NULL | `Origin` header. NULL = auto from URL. |
| `ws_extra_headers` | `const char **` | NULL | NULL-terminated extra header strings. |

### NAT

| Field | Type | Default | Description |
|---|---|---|---|
| `stun_server` | `const char *` | NULL | e.g. `"stun:stun.l.google.com:19302"` |
| `turn_server` | `const char *` | NULL | e.g. `"turn:turn.example.com:3478"` |
| `turn_user` | `const char *` | NULL | TURN username |
| `turn_pass` | `const char *` | NULL | TURN password |
| `ice_enabled` | `bool` | false | Enable ICE |

### Media

| Field | Type | Default | Description |
|---|---|---|---|
| `media_enc` | `baresdk_media_enc_t` | `NONE` | `NONE`, `SDES`, `DTLS_SRTP` |
| `audio_codecs[8]` | `baresdk_codec_t[]` | `[OPUS]` | Preference-ordered codec list |
| `audio_codec_count` | `int` | 1 | Number of codecs in `audio_codecs` |
| `dscp_sip` | `uint8_t` | 0 | DSCP for SIP (0=OS default, 24=AF31) |
| `dscp_rtp` | `uint8_t` | 0 | DSCP for RTP (0=OS default, 46=EF) |

### Audio processing

| Field | Type | Default | Description |
|---|---|---|---|
| `aec_mode` | `baresdk_aec_mode_t` | `SUPPRESSOR` | `OFF`, `SUPPRESSOR`, `WEBRTC` |
| `aec_suppression_level` | `float` | 1.0 | 0.0–1.0; 1.0 = strongest suppression |
| `ns` | `bool` | false | Noise suppression |
| `agc` | `bool` | false | Automatic gain control |
| `mic_gain_db` | `float` | 0.0 | Microphone gain in dB (−20 to +20) |
| `speaker_gain_db` | `float` | 0.0 | Speaker gain in dB (−20 to +20) |

### Opus encoder

| Field | Type | Default | Description |
|---|---|---|---|
| `opus.bitrate` | `int` | 0 | Target bitrate bps (0 = auto/VBR) |
| `opus.complexity` | `int` | −1 | 0–10 CPU/quality trade-off (−1 = opus default: 9) |
| `opus.cbr` | `bool` | false | Constant bitrate (false = VBR) |
| `opus.dtx` | `bool` | false | Discontinuous transmission (silence suppression) |
| `opus.fec` | `bool` | false | In-band forward error correction |
| `opus.stereo` | `bool` | false | Stereo output (false = mono) |

### Jitter buffer

| Field | Type | Default | Description |
|---|---|---|---|
| `jitter_buffer_min_ms` | `uint32_t` | 0 | Minimum depth ms (0 = baresip default) |
| `jitter_buffer_max_ms` | `uint32_t` | 0 | Maximum depth ms (0 = baresip default) |
| `jbuf_type` | `baresdk_jbuf_type_t` | `ADAPTIVE` | `ADAPTIVE` or `FIXED` depth |

### Registration

| Field | Type | Default | Description |
|---|---|---|---|
| `reg_expires` | `uint32_t` | 3600 | REGISTER Expires (seconds) |
| `reg_refresh_pct` | `uint32_t` | 75 | Refresh at N% of expires |
| `keepalive_interval` | `uint32_t` | 0 | Keepalive ms (0=transport default) |
| `reg_retry_initial_ms` | `uint32_t` | 2000 | Initial retry delay |
| `reg_retry_max_ms` | `uint32_t` | 300000 | Max retry delay (5 min) |
| `reg_retry_backoff` | `float` | 2.0 | Exponential backoff multiplier |
| `reg_retry_max_attempts` | `uint32_t` | 0 | 0 = retry forever |

### SIP timers (RFC 3261)

| Field | Type | Default |
|---|---|---|
| `sip_t1_ms` | `uint32_t` | 500 |
| `sip_t2_ms` | `uint32_t` | 4000 |
| `sip_timer_b_ms` | `uint32_t` | 32000 |
| `sip_timer_f_ms` | `uint32_t` | 32000 |

### Session timers (RFC 4028)

| Field | Type | Default |
|---|---|---|
| `session_timer_enabled` | `bool` | true |
| `session_expires_s` | `uint32_t` | 1800 |
| `session_min_se_s` | `uint32_t` | 90 |

### Quality & observability

| Field | Type | Default | Description |
|---|---|---|---|
| `stats_interval_ms` | `uint32_t` | 0 | 0=disabled; fires `BARESDK_EV_MEDIA_STATS`. Python: `call.poll_stats(interval=)` overrides this per-call. |
| `mos_method` | `baresdk_mos_method_t` | `EMODEL` | `EMODEL` or `SIMPLIFIED` |
| `trace_sip` | `bool` | false | Emit `BARESDK_EV_SIP_TRACE` per message |
| `trace_sdp_diff` | `bool` | false | Emit `BARESDK_EV_SDP_NEGOTIATION` |
| `pcap_path` | `const char *` | NULL | Path for live pcap capture |

### Network handover (Wi-Fi ↔ 4G/5G)

Full guide: [Network handover](../guides/network_handover.md).

| Field | Type | Default | Description |
|---|---|---|---|
| `net_monitor_interval_s` | `uint32_t` | 10 | Interface poll period in seconds; 0 = off. Set 0 on mobile and call `baresdk_network_changed()` from the OS callback |
| `net_settle_ms` | `uint32_t` | 1500 | Debounce — how long the address set must stay stable before acting |
| `net_reinvite_calls` | `bool` | true | Re-INVITE active calls onto the new local address |
| `net_verify_ms` | `uint32_t` | 4000 | Wait for RTP on the new path before retrying; 0 disables the media check |
| `net_max_attempts` | `uint32_t` | 6 | Retry ceiling for the rebind and for each call migration |
| `net_hangup_on_migration_failure` | `bool` | false | End calls whose media could not be migrated |

### Logging

| Field | Type | Default | Description |
|---|---|---|---|
| `log_level` | `int` | 0 | 0=err, 1=warn, 2=info, 3=debug |
| `event_cb` | `baresdk_event_cb_t` | required | Your event callback |
| `event_userdata` | `void *` | NULL | Passed back to `event_cb` |

---

## baresdk_account_config_t

| Field | Type | Required | Description |
|---|---|---|---|
| `uri` | `const char *` | Yes | `"user@host"` or `"sip:user@host"` |
| `password` | `const char *` | Yes | Digest auth password |
| `transport` | `baresdk_transport_t` | No | Overrides global default |
| `server_host` | `const char *` | No | Overrides host from `uri` |
| `server_port` | `uint16_t` | No | 0 = derive from transport |
| `server_url` | `const char *` | No | Full URL (WS/WSS) |
| `auth_user` | `const char *` | No | NULL = user part of `uri` |
| `display_name` | `const char *` | No | SIP display name |
| `media_enc` | `baresdk_media_enc_t` | No | Per-account media encryption override |
| `ice_enabled` | `bool` | No | Per-account ICE override |
| `stun_server` | `const char *` | No | Per-account STUN override |
| `turn_server` | `const char *` | No | Per-account TURN override |
| `turn_user` / `turn_pass` | `const char *` | No | TURN credentials |
| `outbound_proxy` | `const char *` | No | NULL = auto-derived from server |
| `verify_tls` | `bool` | No | false = skip TLS cert check |
| `dtmf_mode` | `baresdk_dtmf_mode_t` | No | `RFC4733` (default), `SIP_INFO`, `AUTO` |
