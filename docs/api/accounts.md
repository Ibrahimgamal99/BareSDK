# Accounts & registration

## Create an account

```c
voxsdk_account_config_t cfg = {
    .uri       = "alice@pbx.example.com",
    .password  = "secret",
    .transport = VOXSDK_TRANSPORT_TLS,
};
voxsdk_account_handle_t acct;
int rc = voxsdk_account_create(&cfg, &acct);
```

`voxsdk_account_create` does **not** register. Call `voxsdk_account_register` to start.

## Register

```c
voxsdk_account_register(acct);
```

Fires `VOXSDK_EV_REG_STATE` events: `REGISTERING → REGISTERED` or `FAILED`.

On failure the SDK automatically retries with exponential backoff (configurable via `reg_retry_*` fields in `voxsdk_config_t`).

## Unregister

```c
voxsdk_account_unregister(acct);
// fires VOXSDK_EV_REG_STATE with UNREGISTERING → UNREGISTERED
```

## Destroy

```c
voxsdk_account_destroy(acct);
// blocks until unregistered and all calls on this account are terminated
```

## Push notifications

The SDK supports two modes for delivering a push token to the SIP server so it can wake the device when an INVITE arrives.

### Mode 1 — RFC 8599 Contact URI params (self-hosted servers)

Set the token at account creation time via `voxsdk_account_config_t`:

```c
voxsdk_account_config_t cfg = {
    .uri            = "alice@pbx.example.com",
    .password       = "secret",
    .transport      = VOXSDK_TRANSPORT_TLS,
    .push_provider  = VOXSDK_PUSH_PROVIDER_APNS,  // or APNS_SANDBOX / FCM
    .push_token     = "<device-token-hex>",
    .push_param     = "com.example.MyApp",          // bundle ID (APNs) or package name (FCM)
};
```

The SDK embeds the params **inside** the Contact angle brackets on every REGISTER:

```
Contact: <sip:alice@10.0.5.2;pn-provider=apns;pn-prid=TOKEN;pn-param=com.example.MyApp>
```

The server (Kamailio `push_notification` module, drachtio, etc.) reads these params from its registrar table and uses them to wake the device via APNs or FCM.

#### Update the token at runtime

PushKit tokens rotate. Update without re-creating the account:

```c
voxsdk_account_set_push_token(acct, new_token);
```

The SDK re-registers immediately (unless a transaction is in flight or a retry backoff is pending — in those cases the new token is applied on the next natural re-registration). Pass `NULL` to clear push params.

> **APNs environment note:** use `VOXSDK_PUSH_PROVIDER_APNS_SANDBOX` for debug/TestFlight builds (PushKit sandbox APNs endpoint) and `VOXSDK_PUSH_PROVIDER_APNS` for App Store / production builds. Mismatching the environment causes silent delivery failures at the APNs level — pushes appear to succeed but never arrive.

### Mode 2 — REGISTER-only custom headers (hosted / vendor servers)

For servers you do not control (Twilio, Plivo, hosted PBXes) where push dispatch is server-managed via non-standard headers:

```c
voxsdk_account_add_register_header(acct, "X-Push-Token", "<device-token>");
voxsdk_account_add_register_header(acct, "X-Apple-Push-Bundle", "com.example.MyApp");
```

These headers appear **only on REGISTER** requests — not on INVITE, BYE, REFER, or any other dialog request. This prevents leaking the push token to call peers.

For comparison, `voxsdk_account_add_header()` sends the header on **every** outgoing request from this account. Use that for tenant IDs, app version strings, or other metadata that belongs on all requests.

---

## Custom SIP headers

Add headers to **all outgoing requests** from this account:

```c
voxsdk_account_add_header(acct, "X-Tenant-Id", "42");
voxsdk_account_add_header(acct, "X-App-Version", "2.1.0");
```

## Messaging — SIP MESSAGE

Send an out-of-dialog instant message. No call is needed, and the account does
not have to be in a call for either direction.

**C**
```c
voxsdk_message_send(acct, "bob@pbx.example.com", "on my way", "text/plain");
```

**C++**
```cpp
acct.send_message("bob@pbx.example.com", "on my way");   // content_type defaults to text/plain
```

**Python**
```python
acc.send_message("bob@pbx.example.com", "on my way")
```

**Dart**
```dart
account.sendMessage('bob@pbx.example.com', 'on my way');
```

Incoming messages arrive as `VOXSDK_EV_MESSAGE` (`message` in Python,
`MessageEvent` in Dart) with `from_uri`, `body` and `content_type` — see
[Events reference](events.md#voxsdk_ev_message).

## Presence — PUBLISH

Tell the server your status:

```c
voxsdk_account_publish_presence(acct, VOXSDK_PRESENCE_OPEN);   // available
voxsdk_account_publish_presence(acct, VOXSDK_PRESENCE_BUSY);   // on a call
voxsdk_account_publish_presence(acct, VOXSDK_PRESENCE_CLOSED); // DND / offline
```

## Presence — SUBSCRIBE

Watch another extension's presence (BLF):

```c
voxsdk_account_subscribe_presence(acct, "bob@pbx.example.com");
// fires VOXSDK_EV_PRESENCE_STATE when bob's state changes

voxsdk_account_unsubscribe_presence(acct, "bob@pbx.example.com");
```

## Registration retry control

The SDK retries failed registrations automatically with exponential backoff. These functions let you override the policy or control the retry loop at runtime.

### Override retry policy

```c
// Override per-account (takes effect on the next retry)
voxsdk_account_set_retry_policy(
    acct,
    2000,    // initial_ms   — first retry delay
    60000,   // max_ms       — delay cap
    1.5f,    // backoff      — multiplier per attempt
    10       // max_attempts — 0 = retry forever
);
```

Per-account policy overrides the global `reg_retry_*` fields in `voxsdk_config_t` for that account only.

### Cancel a pending retry

```c
voxsdk_account_cancel_retry(acct);
// Stops the backoff timer and resets the attempt counter.
// An account that was RECONNECTING reports FAILED — the SDK is no longer
// recovering it — and stays there until voxsdk_account_register().
```

### Force immediate retry

```c
voxsdk_account_retry_now(acct);
// Cancels the current backoff delay and re-registers immediately.
// Resets the attempt counter. No-op if not in a retry loop.
```

### Retry events

A registration the SDK will retry is reported as `VOXSDK_REG_RECONNECTING`,
not `VOXSDK_REG_FAILED` — the failure itself, and then each scheduled retry
with its countdown:

```c
case VOXSDK_EV_REG_STATE:
    if (ev->u.reg.state == VOXSDK_REG_RECONNECTING) {
        if (ev->u.reg.retry_attempt)
            printf("reconnecting: attempt %u in %u ms\n",
                   ev->u.reg.retry_attempt,
                   ev->u.reg.retry_delay_ms);
        else
            printf("reconnecting: %s\n",
                   ev->u.reg.error_str ? ev->u.reg.error_str : "");
    }
    else if (ev->u.reg.state == VOXSDK_REG_FAILED) {
        /* The SDK has given up: auth, an exhausted retry budget, or a retry
         * this app cancelled.  This is the one worth showing the user. */
    }
```

`RECONNECTING` also covers the losses that are not a REGISTER failure at all —
a dead keepalive path, and a network handover — so a status indicator bound to
the registration state tracks them without also subscribing to
`VOXSDK_EV_NETWORK`.  See [`VOXSDK_EV_REG_STATE`](events.md#voxsdk_ev_reg_state).

---

## Audio codec selection

By default each account uses the global `cfg.audio_codecs` list set at SDK init. Set the per-account codec list to override it for a specific account.

### C

```c
voxsdk_account_config_t cfg = {
    .uri      = "alice@pbx.example.com",
    .password = "secret",
};

// String names — highest priority, most flexible
strcpy(cfg.audio_codec_names[0], "ulaw");   // G.711 µ-law
strcpy(cfg.audio_codec_names[1], "alaw");   // G.711 A-law
strcpy(cfg.audio_codec_names[2], "opus");
cfg.audio_codec_name_count = 3;
```

Accepted name aliases:

| You write | Baresip codec |
|---|---|
| `"opus"` | Opus 48 kHz stereo |
| `"ulaw"` / `"pcmu"` / `"g711u"` | G.711 µ-law (PCMU/8000) |
| `"alaw"` / `"pcma"` / `"g711a"` | G.711 A-law (PCMA/8000) |
| anything else | passed as-is to baresip |

Opus and G.711 are the only codecs the library compiles in. A name that no
loaded module registers is offered to nobody and logs a warning.

**Priority:** per-account string names → per-account enum list → global `cfg.audio_codecs`.
When none are set, the default offer is `opus, PCMU, PCMA`.

### Python

```python
import vox_sdk as sdk

account = sdk.create_account(
    "alice@pbx.example.com", "secret",
    audio_codecs=["ulaw", "alaw", "opus"],
)
account.register()
```

### Flutter

```dart
final account = sdk.createAccount(
  "alice@pbx.example.com", "secret",
  audioCodecs: ["ulaw", "alaw", "opus"],
);
```

### C++

```cpp
auto acct = sdk.create_account(
    "alice@pbx.example.com", "secret",
    VOXSDK_TRANSPORT_UDP,
    {VOXSDK_CODEC_PCMU, VOXSDK_CODEC_PCMA, VOXSDK_CODEC_OPUS}
);
```

Or with the full account config struct for string names:

```cpp
voxsdk_account_config_t acfg{};
acfg.uri      = "alice@pbx.example.com";
acfg.password = "secret";
std::strcpy(acfg.audio_codec_names[0], "ulaw");
std::strcpy(acfg.audio_codec_names[1], "opus");
acfg.audio_codec_name_count = 2;
auto acct = sdk.create_account(acfg);
```

---

## 100rel (RFC 3262 PRACK)

```c
voxsdk_account_set_100rel(acct, VOXSDK_100REL_ENABLED);   // support if offered
voxsdk_account_set_100rel(acct, VOXSDK_100REL_REQUIRED);  // require
voxsdk_account_set_100rel(acct, VOXSDK_100REL_DISABLED);  // never (default)
```

Must be called before the first `voxsdk_call_invite` or `voxsdk_call_answer`.
