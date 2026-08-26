# Architecture overview

## Thread model

EchoSDK uses three threads internally:

```
Your code (any thread)
      │
      │  public API calls
      ▼
  dispatch bridge  ──────────────────►  re_main thread
  (bsdk_dispatch)                       (SIP state machine, I/O)
                                                │
                                                │  events enqueued
                                                ▼
                                        event queue (max 4096)
                                                │
                                                │  dequeued + decoded
                                                ▼
                                        event dispatch thread
                                                │
                                                │  your event_cb called
                                                ▼
                                        your event callback
```

**Key rules:**
- All public API functions are thread-safe — call from any thread.
- Event callbacks are called from the **event dispatch thread**, not `re_main`. You may call EchoSDK APIs from inside the callback.
- Keep the callback fast (< 10 ms). Heavy work (recording, transcription) should be dispatched to your own thread.

---

## Lifecycle

```
echosdk_config_init()        ← fill config struct with defaults
echosdk_init()               ← start re_main + event threads
  echosdk_account_create()   ← create account (does not register)
  echosdk_account_register() ← send REGISTER
    ...events...
  echosdk_account_destroy()  ← unregister + free
echosdk_shutdown()           ← stop all threads, free everything
```

`echosdk_init()` is not idempotent: on a stack that is already up it returns
`ECHOSDK_ERR_ALREADY` and changes nothing. Config applies at init only.

---

## Reattaching to a live stack

The stack belongs to the **process**, not to the runtime that started it. A host
that can lose and rebuild its own runtime while the process survives comes back
to a stack that is still up and still registered — the motivating case is an
Android headless Flutter engine destroying the Dart isolate between push
wakeups, and any re-loaded plugin or scripting VM behaves the same way.

Re-initializing is not the recovery; re-pointing is:

```c
if (echosdk_is_initialized()) {
        /* Take over event delivery from the consumer that is gone. */
        echosdk_set_event_handler(my_event_cb, my_userdata, /*owned=*/true);

        /* Re-derive the handles it held instead of creating duplicates. */
        echosdk_account_foreach(adopt_account, NULL);   /* + echosdk_account_get_aor() */
        echosdk_call_foreach(adopt_call, NULL);         /* + echosdk_call_get_state() */
}
else {
        echosdk_init(&cfg);
}
```

Events that fired while nobody was listening are **dropped, not buffered** — so
recover state, not history. An INVITE that arrived during the gap is not
replayed as an event, but the call is still live: `echosdk_call_foreach()` finds
it and `echosdk_call_get_state()` reports it still `RINGING`. Registration state
comes back the same way via `echosdk_account_get_reg_state()`.

Going the other way, a consumer that knows it is about to disappear should call
`echosdk_set_event_handler(NULL, NULL, false)` so nothing is delivered into a
runtime being torn down. The stack stays up, registered and push-reachable.

Flutter does all of this for you: `EchoSDK.start()` reattaches when the stack is
already running and sets [`reattached`](../quickstart/flutter.md), adopting the
live accounts and calls; `detach()` is the park-and-leave-running counterpart to
`shutdown()`.

---

## Config deep-copy

All strings passed to `echosdk_init()` and `echosdk_account_create()` are **deep-copied** internally. You may free your config structs immediately after the call returns.

---

## ABI stability

- Fields are only **appended** to structs — never reordered or removed.
- `echosdk_config_t` carries `version` and `struct_size` fields. Always call `echosdk_config_init()` to zero-fill and set these correctly before populating.
- Opaque handle types (`echosdk_account_handle_t`, `echosdk_call_handle_t`) are stable across minor versions.
