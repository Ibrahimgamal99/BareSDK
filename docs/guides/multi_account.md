# Multiple Accounts

VoxSDK supports creating multiple accounts in a single process. Each account has independent registration, calls, and event routing.

---

## Basic multi-account setup

```c
voxsdk_config_t cfg;
voxsdk_config_init(&cfg);
cfg.event_cb = on_event;
cfg.log_level = 1;
voxsdk_init(&cfg);

// Account 1 — TLS
voxsdk_account_config_t cfg1 = {
    .uri       = "alice@pbx.example.com",
    .password  = "secret1",
    .transport = VOXSDK_TRANSPORT_TLS,
    .server_host = "pbx.example.com",
};
voxsdk_account_handle_t acct1;
voxsdk_account_create(&cfg1, &acct1);
voxsdk_account_register(acct1);

// Account 2 — WSS
voxsdk_account_config_t cfg2 = {
    .uri        = "bob@pbx2.example.com",
    .password   = "secret2",
    .server_url = "wss://pbx2.example.com/ws",
};
voxsdk_account_handle_t acct2;
voxsdk_account_create(&cfg2, &acct2);
voxsdk_account_register(acct2);
```

---

## Event routing

All events include an `account` handle. Route events to the correct logic:

```c
void on_event(const voxsdk_event_t *ev, void *userdata) {
    voxsdk_account_handle_t src = NULL;

    switch (ev->type) {
    case VOXSDK_EV_REG_STATE:
        src = ev->u.reg.account;
        break;
    case VOXSDK_EV_INCOMING_CALL:
        src = ev->u.incoming.account;
        break;
    case VOXSDK_EV_CALL_STATE:
        src = ev->u.call_state.account;
        break;
    default:
        break;
    }

    if (src == acct1) {
        handle_acct1_event(ev);
    } else if (src == acct2) {
        handle_acct2_event(ev);
    }
}
```

---

## Per-account config overrides

Each `voxsdk_account_config_t` can override global settings:

```c
// Account with different media encryption
voxsdk_account_config_t dtls_cfg = {
    .uri         = "alice@webrtc.example.com",
    .password    = "secret",
    .server_url  = "wss://webrtc.example.com/ws",
    .media_enc   = VOXSDK_MEDIA_ENC_DTLS_SRTP,
    .ice_enabled = true,
    .stun_server = "stun:stun.l.google.com:19302",
};

// Account with plain RTP on local network
voxsdk_account_config_t lan_cfg = {
    .uri         = "100@192.168.1.10",
    .password    = "secret",
    .transport   = VOXSDK_TRANSPORT_UDP,
};
```

### Overridable per-account fields

| Field | Description |
|---|---|
| `transport` | SIP transport protocol |
| `server_host` / `server_port` | SIP server address |
| `server_url` | Full URL (for WS/WSS) |
| `media_enc` | Media encryption (NONE / SDES / DTLS-SRTP) |
| `ice_enabled` | ICE on/off |
| `stun_server` | STUN server address |
| `turn_server` / `turn_user` / `turn_pass` | TURN relay |
| `outbound` | Outbound proxy |
| `verify_tls` | TLS certificate verification |

---

## Use cases

### Registrar farm (load balancing)

Register the same AOR on multiple servers for redundancy:

```c
voxsdk_account_config_t primary = {
    .uri = "alice@pbx.example.com",
    .password = "secret",
    .server_host = "pbx1.example.com",
};
voxsdk_account_config_t backup = {
    .uri = "alice@pbx.example.com",
    .password = "secret",
    .server_host = "pbx2.example.com",
};
```

### Multi-tenant PBX

Different tenants on different domains:

```c
voxsdk_account_config_t tenant_a = {
    .uri = "100@tenant-a.example.com",
    .password = "secret_a",
};
voxsdk_account_config_t tenant_b = {
    .uri = "200@tenant-b.example.com",
    .password = "secret_b",
};
```

### PBX gateway + WebRTC

One account for PSTN gateway (SDES), another for WebRTC clients (DTLS-SRTP):

```c
voxsdk_account_config_t pstn = {
    .uri = "gateway@carrier.com",
    .password = "secret",
    .media_enc = VOXSDK_MEDIA_ENC_SDES,
};
voxsdk_account_config_t webrtc = {
    .uri = "webrtc@pbx.example.com",
    .password = "secret",
    .server_url = "wss://pbx.example.com/ws",
    .media_enc = VOXSDK_MEDIA_ENC_DTLS_SRTP,
    .ice_enabled = true,
};
```

---

## Thread safety

All account-level APIs (`voxsdk_account_register`, `voxsdk_call_invite`, etc.) are thread-safe. You may call them from any thread. Events are delivered from a single dispatch thread, so your callback does not need to be re-entrant.

---

## Cleanup

Destroy each account before shutdown:

```c
voxsdk_account_destroy(acct1);
voxsdk_account_destroy(acct2);
voxsdk_shutdown();
```

`voxsdk_shutdown()` also forcibly terminates any remaining accounts, but explicit cleanup is recommended.
