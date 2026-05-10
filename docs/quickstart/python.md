# Quick start — Python

## Prerequisites

```bash
# Ubuntu/Debian
sudo apt install libssl3 zlib1g

# Fedora/RHEL
sudo dnf install openssl-libs zlib
```

---

## One-command setup

```bash
bash bindings/python/build.sh
```

This builds the SDK if needed, then installs the Python package. The library is found automatically — no environment variables or manual file copying required.

---

## Manual setup (alternative)

Build the shared library for your platform:

| Platform | Command | Output |
|---|---|---|
| Linux | `bash scripts/build-linux.sh` | `dist/linux/x86_64/baresdk.so` |
| macOS | `bash scripts/build-macos.sh` | `dist/macos/universal/baresdk.dylib` |
| Windows | `.\scripts\build-windows.ps1` | `dist\windows\x64\baresdk.dll` |
| Android | `bash scripts/build-android.sh` | `dist/android/<ABI>/baresdk.so` |

Then install the package:
```bash
pip install bindings/python
```

The loader automatically finds the library in `dist/` — no `LD_LIBRARY_PATH` or manual copy needed. To override the path explicitly:
```bash
export BARESDK_LIB=/abs/path/to/baresdk.so
```

---

## Register and wait for a call

```python
from baresdk import (SDK, RegStateEvent, IncomingCallEvent, CallStateEvent,
                     REG_REGISTERED, CALL_ENDED, CALL_FAILED, CALL_CANCELLED)

with SDK(log_level=1) as sdk:
    account = sdk.create_account("alice@pbx.example.com", "secret")
    account.register()

    for ev in account.events(timeout=60):
        if isinstance(ev, RegStateEvent) and ev.state == REG_REGISTERED:
            print("Registered!")

        elif isinstance(ev, IncomingCallEvent):
            print(f"Incoming call from {ev.from_uri}")
            ev.call.answer()

        elif isinstance(ev, CallStateEvent) and ev.state in (CALL_ENDED, CALL_FAILED, CALL_CANCELLED):
            print("Call ended.")
            break

    account.destroy()
```

---

## Make an outgoing call

```python
from baresdk import SDK, RegStateEvent, CallStateEvent, MediaStatsEvent
from baresdk import REG_REGISTERED, CALL_ENDED, TRANSPORT_TLS

with SDK(log_level=1, stats_interval_ms=5000) as sdk:
    account = sdk.create_account(
        "alice@pbx.example.com", "secret",
        transport=TRANSPORT_TLS
    )
    account.register()

    call = None
    for ev in account.events(timeout=30):
        if isinstance(ev, RegStateEvent) and ev.state == REG_REGISTERED:
            call = account.call("bob@pbx.example.com")

        elif isinstance(ev, MediaStatsEvent):
            print(f"MOS: {ev.mos_lq:.2f}  RTT: {ev.rtt_ms:.0f} ms")

        elif isinstance(ev, CallStateEvent) and ev.state == CALL_ENDED:
            break

    account.destroy()
```

---

## Custom TLS + ICE

```python
sdk = SDK(
    log_level      = 2,
    transport      = 4,                       # TRANSPORT_WSS
    server_url     = "wss://pbx.example.com/ws",
    ca_cert_path   = "/etc/ssl/certs/ca-bundle.crt",
    verify_server  = 1,
)
account = sdk.create_account(
    "alice@pbx.example.com", "secret",
    ice_enabled  = 1,
    stun_server  = "stun:stun.l.google.com:19302",
)
```

---

## Runtime audio quality controls

```python
# At init time (before baresdk_init):
sdk = SDK(log_level=1, aec=1, ns=1, agc=1,
          jitter_buffer_min_ms=20, jitter_buffer_max_ms=150)

# Toggle filters on the fly at any time:
sdk.set_aec(True)
sdk.set_ns(False)
sdk.set_agc(True)

# Change jitter buffer bounds (takes effect on new calls):
sdk.set_jitter_buffer(20, 200)   # widen on a poor network

# Set per-call RTP DSCP on an established call:
call.set_dscp_rtp(46)  # EF — Expedited Forwarding
```

---

## See also
- Full example: [bindings/python/examples/quickstart.py](../../bindings/python/examples/quickstart.py)
- Events: [events reference](../api/events.md)
- Media & audio API: [api/media.md](../api/media.md)
