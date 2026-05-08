# NAT Traversal — ICE / STUN / TURN

## The problem

When your SIP device is behind a NAT router, the IP address and port in the SDP offer are private (e.g. `192.168.1.50`). The remote side cannot reach that address. You need NAT traversal to discover your public address and ensure media flows in both directions.

---

## Quick diagnosis

| Symptom | Likely cause |
|---|---|
| One-way audio (you hear them, they don't hear you) | SDP contains private IP; RTP flows outbound but replies can't return |
| No audio either direction | Firewall blocks RTP ports entirely |
| Registration works but calls fail | SIP signaling passes (UDP hole kept alive) but RTP ports are blocked |
| Works from some networks, not others | Symmetric NAT (STUN alone won't work — need TURN) |

---

## STUN (simple NAT)

STUN discovers your public IP:port mapping. Works when the NAT is **not symmetric** (i.e., the same mapping is used for all destinations).

```c
baresdk_account_config_t cfg = {
    .uri          = "alice@pbx.example.com",
    .password     = "secret",
    .ice_enabled  = true,
    .stun_server  = "stun:stun.l.google.com:19302",
};
```

STUN alone is sufficient for most home/office routers with full-cone or address-restricted NAT.

---

## ICE (Interactive Connectivity Establishment)

ICE tries multiple candidate addresses (host, server-reflexive via STUN, relay via TURN) and picks the best working pair.

```c
baresdk_account_config_t cfg = {
    .uri          = "alice@pbx.example.com",
    .password     = "secret",
    .ice_enabled  = true,
    .stun_server  = "stun:stun.l.google.com:19302",
};
```

ICE is **required** when using DTLS-SRTP (WebRTC interop). Enable it for any scenario where NAT type is unknown.

---

## TURN (guaranteed relay)

TURN relays media through a server when direct connectivity fails (symmetric NAT, corporate firewalls). It always works but adds latency and bandwidth cost.

When both `stun_server` and `turn_server` are set, TURN takes priority as the active ICE server (TURN includes STUN-equivalent discovery). Set only `stun_server` when you don't have a TURN server.

```c
baresdk_account_config_t cfg = {
    .uri          = "alice@pbx.example.com",
    .password     = "secret",
    .ice_enabled  = true,
    .stun_server  = "stun:stun.l.google.com:19302",
    .turn_server  = "turn:turn.example.com:3478",
    .turn_user    = "alice",
    .turn_pass    = "turn_secret",
};
```

### TURN server options

| Server | Transport | Port | Notes |
|---|---|---|---|
| coturn | UDP/TCP | 3478 | Most widely used; open source |
| Twilio TURN | UDP/TLS | 3478/443 | Managed service |
| Metered TURN | UDP/TCP/TLS | various | Managed service |

### coturn quick setup

```
# /etc/turnserver.conf
listening-port=3478
realm=example.com
user=alice:turn_secret
lt-cred-mech
fingerprint
```

---

## Configuration patterns

### Pattern 1: Basic (STUN only, home/office)

```c
.ice_enabled  = true,
.stun_server  = "stun:stun.l.google.com:19302",
```

### Pattern 2: Robust (STUN + TURN fallback)

```c
.ice_enabled  = true,
.stun_server  = "stun:stun.l.google.com:19302",
.turn_server  = "turn:turn.example.com:3478",
.turn_user    = "alice",
.turn_pass    = "turn_secret",
```

### Pattern 3: Enterprise firewall (TURN only, TLS)

```c
.ice_enabled  = true,
.turn_server  = "turn:turn.example.com:443?transport=tcp",
.turn_user    = "alice",
.turn_pass    = "turn_secret",
```

### Pattern 4: WebRTC browser interop (ICE + DTLS-SRTP + TURN)

```c
.media_enc    = BARESDK_MEDIA_ENC_DTLS_SRTP,
.ice_enabled  = true,
.stun_server  = "stun:stun.l.google.com:19302",
.turn_server  = "turn:turn.example.com:3478",
.turn_user    = "alice",
.turn_pass    = "turn_secret",
```

---

## ICE candidate types

| Type | Source | Reliability |
|---|---|---|
| Host | Local interface IP | Works on same LAN |
| Server-reflexive (srflx) | STUN response | Works on most NATs |
| Relay | TURN allocation | Always works |

ICE automatically prioritises host > srflx > relay and selects the first working pair.

---

## Troubleshooting

1. **Enable SIP trace** to check the SDP for candidate lines:
   ```
   a=candidate:... srflx ...
   a=candidate:... relay ...
   ```

2. **Check stats** — if `rtt_ms` is very high and `loss_pct` is zero, you're probably on TURN relay.

3. **Firewall rules** — ensure outbound UDP to STUN (port 3478) and TURN (port 3478/443) is allowed.

4. **NAT type test** — use `stun-client` or the STUN binding request/response to determine if your NAT is symmetric.
