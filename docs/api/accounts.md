# Accounts & registration

## Create an account

```c
baresdk_account_config_t cfg = {
    .uri       = "alice@pbx.example.com",
    .password  = "secret",
    .transport = BARESDK_TRANSPORT_TLS,
};
baresdk_account_handle_t acct;
int rc = baresdk_account_create(&cfg, &acct);
```

`baresdk_account_create` does **not** register. Call `baresdk_account_register` to start.

## Register

```c
baresdk_account_register(acct);
```

Fires `BARESDK_EV_REG_STATE` events: `REGISTERING → REGISTERED` or `FAILED`.

On failure the SDK automatically retries with exponential backoff (configurable via `reg_retry_*` fields in `baresdk_config_t`).

## Unregister

```c
baresdk_account_unregister(acct);
// fires BARESDK_EV_REG_STATE with UNREGISTERING → UNREGISTERED
```

## Destroy

```c
baresdk_account_destroy(acct);
// blocks until unregistered and all calls on this account are terminated
```

## Custom SIP headers

Add headers to **all outgoing requests** from this account:

```c
baresdk_account_add_header(acct, "X-Tenant-Id", "42");
baresdk_account_add_header(acct, "X-App-Version", "2.1.0");
```

## Presence — PUBLISH

Tell the server your status:

```c
baresdk_account_publish_presence(acct, BARESDK_PRESENCE_OPEN);   // available
baresdk_account_publish_presence(acct, BARESDK_PRESENCE_BUSY);   // on a call
baresdk_account_publish_presence(acct, BARESDK_PRESENCE_CLOSED); // DND / offline
```

## Presence — SUBSCRIBE

Watch another extension's presence (BLF):

```c
baresdk_account_subscribe_presence(acct, "bob@pbx.example.com");
// fires BARESDK_EV_PRESENCE_STATE when bob's state changes

baresdk_account_unsubscribe_presence(acct, "bob@pbx.example.com");
```

## Registration retry control

The SDK retries failed registrations automatically with exponential backoff. These functions let you override the policy or control the retry loop at runtime.

### Override retry policy

```c
// Override per-account (takes effect on the next retry)
baresdk_account_set_retry_policy(
    acct,
    2000,    // initial_ms   — first retry delay
    60000,   // max_ms       — delay cap
    1.5f,    // backoff      — multiplier per attempt
    10       // max_attempts — 0 = retry forever
);
```

Per-account policy overrides the global `reg_retry_*` fields in `baresdk_config_t` for that account only.

### Cancel a pending retry

```c
baresdk_account_cancel_retry(acct);
// Stops the backoff timer and resets the attempt counter.
// The account stays in FAILED state — call baresdk_account_register() to restart.
```

### Force immediate retry

```c
baresdk_account_retry_now(acct);
// Cancels the current backoff delay and re-registers immediately.
// Resets the attempt counter. No-op if not in a retry loop.
```

### Retry events

Every scheduled retry fires `BARESDK_EV_REG_STATE` with `state == BARESDK_REG_FAILED`:

```c
case BARESDK_EV_REG_STATE:
    if (ev->u.reg.state == BARESDK_REG_FAILED) {
        printf("retry %u in %u ms\n",
               ev->u.reg.retry_attempt,
               ev->u.reg.retry_delay_ms);
    }
```

---

## 100rel (RFC 3262 PRACK)

```c
baresdk_account_set_100rel(acct, BARESDK_100REL_ENABLED);   // support if offered
baresdk_account_set_100rel(acct, BARESDK_100REL_REQUIRED);  // require
baresdk_account_set_100rel(acct, BARESDK_100REL_DISABLED);  // never (default)
```

Must be called before the first `baresdk_call_invite` or `baresdk_call_answer`.
