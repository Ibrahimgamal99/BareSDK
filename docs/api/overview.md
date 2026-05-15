# Architecture overview

## Thread model

baresdk uses three threads internally:

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
- Event callbacks are called from the **event dispatch thread**, not `re_main`. You may call baresdk APIs from inside the callback.
- Keep the callback fast (< 10 ms). Heavy work (recording, transcription) should be dispatched to your own thread.

---

## Lifecycle

```
baresdk_config_init()        ← fill config struct with defaults
baresdk_init()               ← start re_main + event threads
  baresdk_account_create()   ← create account (does not register)
  baresdk_account_register() ← send REGISTER
    ...events...
  baresdk_account_destroy()  ← unregister + free
baresdk_shutdown()           ← stop all threads, free everything
```

---

## Config deep-copy

All strings passed to `baresdk_init()` and `baresdk_account_create()` are **deep-copied** internally. You may free your config structs immediately after the call returns.

---

## ABI stability

- Fields are only **appended** to structs — never reordered or removed.
- `baresdk_config_t` carries `version` and `struct_size` fields. Always call `baresdk_config_init()` to zero-fill and set these correctly before populating.
- Opaque handle types (`baresdk_account_handle_t`, `baresdk_call_handle_t`) are stable across minor versions.
