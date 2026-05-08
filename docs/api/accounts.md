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
// automatically unregisters first; terminates all active calls on this account
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

## 100rel (RFC 3262 PRACK)

```c
baresdk_account_set_100rel(acct, BARESDK_100REL_ENABLED);   // support if offered
baresdk_account_set_100rel(acct, BARESDK_100REL_REQUIRED);  // require
baresdk_account_set_100rel(acct, BARESDK_100REL_DISABLED);  // never (default)
```

Must be called before the first `baresdk_call_invite` or `baresdk_call_answer`.
