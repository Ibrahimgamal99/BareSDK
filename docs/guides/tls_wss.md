# TLS and WSS Setup

## Overview

baresdk supports encrypted SIP signaling via TLS (TCP) and WSS (WebSocket over TLS). This guide covers certificate configuration, common server setups, and troubleshooting.

---

## Transport options

| Transport | Enum | Default port | Encryption |
|---|---|---|---|
| UDP | `BARESDK_TRANSPORT_UDP` | 5060 | None |
| TCP | `BARESDK_TRANSPORT_TCP` | 5060 | None |
| TLS | `BARESDK_TRANSPORT_TLS` | 5061 | TLS over TCP |
| WS | `BARESDK_TRANSPORT_WS` | 80/8088 | None |
| WSS | `BARESDK_TRANSPORT_WSS` | 443 | TLS over WebSocket |

---

## Basic TLS setup

```c
baresdk_account_config_t cfg = {
    .uri         = "alice@pbx.example.com",
    .password    = "secret",
    .transport   = BARESDK_TRANSPORT_TLS,
    .server_host = "pbx.example.com",
    .server_port = 5061,
    .verify_tls  = true,
};
```

> **Note:** `verify_tls` is reserved for future per-account control. Currently the effective TLS verification setting is the global `verify_server` field in `baresdk_config_t` (defaults to `true`). Set it there to control verification for all accounts.

### Global TLS config (baresdk_config_t)

```c
baresdk_config_t cfg;
baresdk_config_init(&cfg);

cfg.ca_cert_path  = "/etc/ssl/certs/ca-bundle.crt";  // NULL = system store
cfg.verify_server = true;
cfg.sni_hostname  = NULL;  // auto-derived from server_host
```

---

## WebSocket (WS / WSS)

WebSocket transport is required when connecting to SIP servers behind reverse proxies (Asterisk HTTP, Kamailio with XHTTP, OpenSIPS).

```c
baresdk_account_config_t cfg = {
    .uri         = "alice@pbx.example.com",
    .password    = "secret",
    .server_url  = "wss://pbx.example.com:443/ws",
};
```

When `server_url` is set, `transport` is derived from the URL scheme (`ws://` → WS, `wss://` → WSS).

### Custom Origin header

Some servers validate the WebSocket `Origin` header:

```c
baresdk_config_t cfg;
baresdk_config_init(&cfg);
cfg.ws_origin = "https://app.example.com";
```

### Extra WebSocket headers

```c
const char *extra[] = {
    "X-API-Key: abc123",
    "X-App-Version: 2.0",
    NULL,
};
cfg.ws_extra_headers = extra;
```

---

## Server-specific guides

### Asterisk (WS / WSS)

`http.conf`:
```ini
[general]
enabled=yes
bindaddr=0.0.0.0
bindport=8088
; For WSS:
; tlsenable=yes
; tlscertfile=/etc/asterisk/keys/asterisk.pem
; tlsprivatekey=/etc/asterisk/keys/asterisk.key
```

`sip.conf`:
```ini
[alice]
type=friend
host=dynamic
transport=ws,wss
dtlsenable=yes
dtlsautoarrange=yes
```

baresdk config:
```c
.server_url = "ws://asterisk.local:8088/ws",
// or WSS:
.server_url = "wss://asterisk.example.com:8089/ws",
```

### Kamailio with TLS

`kamailio.cfg`:
```
listen=tls:0.0.0.0:5061
modparam("tls", "certificate", "/etc/kamailio/tls/kamailio.pem")
modparam("tls", "private_key", "/etc/kamailio/tls/kamailio.key")
```

### OpenSIPS with WebSocket

```
listen=tls:0.0.0.0:443
loadmodule "proto_wss.so"
```

---

## SNI hostname override

When a reverse proxy terminates TLS with a certificate for a different hostname than the SIP domain, set `sni_hostname`:

```c
cfg.sni_hostname = "proxy.example.com";  // matches cert CN/SAN
// SIP domain remains "pbx.internal"
```

---

## Self-signed certificates (development)

```c
baresdk_account_config_t cfg = {
    .uri         = "alice@192.168.1.10",
    .password    = "secret",
    .transport   = BARESDK_TRANSPORT_TLS,
    .server_host = "192.168.1.10",
    .server_port = 5061,
    .verify_tls  = false,   // accept self-signed
};
```

Or load a specific CA:

```c
baresdk_config_t gcfg;
baresdk_config_init(&gcfg);
gcfg.ca_cert_path  = "/path/to/self-signed-ca.pem";
gcfg.verify_server = true;
```

---

## Client certificate (mTLS)

Some PBX deployments require mutual TLS:

```c
cfg.client_cert = "/path/to/client.crt";
cfg.client_key  = "/path/to/client.key";
```

---

## Troubleshooting TLS

| Error | Cause | Fix |
|---|---|---|
| `BARESDK_ERR_TRANSPORT` | TLS handshake failed | Check server cert, set `verify_tls = false` to test |
| `BARESDK_ERR_AUTH` after TLS connects | Wrong SIP credentials | Verify `uri` + `password` |
| Certificate verification fails | Self-signed or expired | Set `ca_cert_path` or `verify_tls = false` |
| SNI mismatch | Proxy cert doesn't match | Set `sni_hostname` explicitly |
| WSS 403 / rejected | Origin check fails | Set `ws_origin` to match server's allowed origins |

Enable SIP trace to inspect the TLS negotiation:

```c
cfg.trace_sip = true;
```
