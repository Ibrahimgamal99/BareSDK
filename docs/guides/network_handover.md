# Network Handover — Wi-Fi ↔ 4G/5G

## The problem

A phone that walks out of Wi-Fi range onto cellular gets a new IP address while
the SIP stack is still running. Three things break at once, and none of them
recover on their own:

| What breaks | Symptom without handover |
|---|---|
| SIP transports are bound to an address that no longer exists | Outgoing requests fail; a dead TCP/TLS/WSS socket stalls until Timer B (32 s) rather than failing fast |
| The registrar's binding still points at the old Contact | Inbound calls are routed into the void until the next REGISTER refresh — up to `reg_expires` seconds |
| Active calls advertise the old address in the SDP `c=` line | The peer keeps sending RTP where you can no longer receive it — one-way or dead audio |

baresdk repairs all three. The sequence is:

```
detect → settle → rebind transports → re-REGISTER → re-INVITE → verify media
```

---

## Wiring it up

The SDK cannot see the OS connectivity signal, so tell it:

```c
baresdk_network_changed();
```

Safe to call from any thread, as often as the OS fires. Calls are coalesced —
the handover runs once, after the address set has been stable for
`net_settle_ms`.

### Android

```kotlin
val cm = getSystemService(ConnectivityManager::class.java)
cm.registerDefaultNetworkCallback(object : ConnectivityManager.NetworkCallback() {
    override fun onAvailable(network: Network)  = BareSdk.networkChanged()
    override fun onLost(network: Network)       = BareSdk.networkChanged()
    override fun onCapabilitiesChanged(n: Network, c: NetworkCapabilities) =
        BareSdk.networkChanged()
})
```

Set `net_monitor_interval_s = 0` on mobile — polling `getifaddrs()` on a timer
wastes battery when the OS already has the signal.

### iOS

```swift
let monitor = NWPathMonitor()
monitor.pathUpdateHandler = { _ in baresdk_network_changed() }
monitor.start(queue: DispatchQueue.global(qos: .utility))
```

### Desktop

Leave the built-in poller on (`net_monitor_interval_s`, default 10 s) and you
need no platform code at all. It reconciles the kernel's address list against
the stack's on every tick. If you already subscribe to NetworkManager,
`SCNetworkReachability`, or `NotifyAddrChange`, call `baresdk_network_changed()`
from there and set the interval to 0.

---

## Watching it happen

Every stage arrives as `BARESDK_EV_NETWORK`:

```c
case BARESDK_EV_NETWORK: {
    const baresdk_ev_network_t *n = &ev->u.network;
    switch (n->event) {
    case BARESDK_NET_CHANGE_DETECTED:
        log("network changed — settling");
        break;
    case BARESDK_NET_DOWN:
        log("no usable network — holding");
        break;
    case BARESDK_NET_UP:
        log("network back: %s", n->local_addr);
        break;
    case BARESDK_NET_TRANSPORT_RESET:
        log("SIP transports rebound on %s", n->local_addr);
        break;
    case BARESDK_NET_CALL_MIGRATING:
        log("link settled — rebuilding the media path %u/%u",
            n->attempt, n->max_attempts);
        break;
    case BARESDK_NET_CALL_MIGRATE_ACCEPTED:
        log("peer accepted the new path — waiting for audio to resume");
        break;
    case BARESDK_NET_CALL_MIGRATED:
        log("media recovered after %.1f s", n->elapsed_ms / 1000.0);
        break;
    case BARESDK_NET_CALL_MIGRATION_FAILED:
        log("media did not recover (%s)", baresdk_strerror(n->error));
        break;
    default:
        break;
    }
    break;
}
```

Python:

```python
@sdk.on("network")
def _(ev):
    if ev.stage == "call_migrating":
        print(f"link settled — rebuilding media path {ev.attempt}/{ev.max_attempts}")
    elif ev.stage == "call_migrate_accepted":
        print("peer accepted the new path — waiting for audio to resume")
    elif ev.stage == "call_migrated":
        print(f"media recovered after {ev.elapsed_ms / 1000:.1f}s")
```

### Stages

| Stage | Meaning |
|---|---|
| `CHANGE_DETECTED` | The local address set changed; the debounce timer is running |
| `DOWN` | No routable address. The handover is held, not abandoned — nothing is flushed while there is nowhere to rebind |
| `UP` | A routable address is back |
| `TRANSPORT_RESET` | SIP transports flushed and re-bound; dead TCP/TLS/WSS connections dropped |
| `REREGISTERING` | REGISTER re-sent for one account (`account` field is set) |
| `CALL_MIGRATING` | re-INVITE sent with the new address in the SDP |
| `CALL_MIGRATE_ACCEPTED` | The peer answered the offer. Audio is not confirmed yet |
| `CALL_MIGRATED` | RTP observed on the new path. `elapsed_ms` is the audio gap |
| `CALL_DEFERRED` | Migration is parked and will be retried — either the dialog cannot take a re-INVITE yet, or the new source address is not yet resolvable (route still being installed) |
| `CALL_MIGRATION_FAILED` | Gave up after `max_attempts` |
| `HANDOVER_FAILED` | The transport rebind itself failed; retrying with backoff |

---

## Media is verified, not assumed

A re-INVITE that gets a `200 OK` can still leave audio dead — the peer's NAT
binding has not moved, the answer came back with the wrong direction, or the new
path is filtered. So after each re-INVITE the SDK samples the stream's RX packet
counter and checks it has advanced `net_verify_ms` later. If it has not, it
re-offers, and after `net_max_attempts` it reports
`CALL_MIGRATION_FAILED`.

Set `net_verify_ms = 0` to skip the check and treat the re-INVITE as sufficient.

Held calls are exempt: they carry no RTP, so the answered re-INVITE is the only
confirmation available.

---

## Configuration

| Field | Default | Description |
|---|---|---|
| `net_monitor_interval_s` | 10 | Interface poll period in seconds; 0 = off |
| `net_settle_ms` | 1500 | How long the address set must be stable before acting |
| `net_reinvite_calls` | true | Re-INVITE active calls onto the new address |
| `net_verify_ms` | 4000 | Wait for RTP before retrying; 0 disables the media check |
| `net_max_attempts` | 6 | Retry ceiling for both the rebind and each call migration |
| `net_hangup_on_migration_failure` | false | End calls whose media could not be migrated |

Runtime overrides:

```c
baresdk_network_set_monitor_interval(0);              /* mobile */
baresdk_network_set_handover_policy(true, false);     /* reinvite, don't hang up */
```

Queries:

```c
char ip[64];
baresdk_network_local_addr(ip, sizeof(ip));   /* "" when there is no address */
bool up = baresdk_network_is_up();            /* false = no routable address */
```

---

## What is handled

| Case | Behaviour |
|---|---|
| Wi-Fi → cellular, cellular → Wi-Fi | Full sequence; SDP `c=` follows the new source address |
| Both networks briefly down | `DOWN` emitted, handover held with exponential backoff (1→32 s), applied when an address returns. Transports are never flushed into a void |
| Interface still coming up (address burst) | Debounced — the handover runs once, on the final address set |
| Route changed, addresses unchanged | `baresdk_network_changed()` still re-checks each call's source address and re-INVITEs only what actually moved |
| Address changed but a call's path did not | That call is skipped — no pointless re-INVITE |
| IPv4 ↔ IPv6 | Handled; the address family follows the new source address |
| DNS servers changed | Resolvers refreshed, unless you pinned your own nameservers |
| Dead TCP/TLS/WSS sockets | Dropped by the transport flush, so the next request opens a fresh connection instead of stalling to Timer B |
| Account in retry backoff | Backoff cancelled and the attempt counter reset — a new network deserves an immediate try, not the tail of a 5-minute backoff |
| Account created but never registered | Left alone |
| Call in an early dialog (`CALLING`/`RINGING`) | Deferred and migrated once the dialog is established, rather than hung up |
| New default route not installed yet | Common during Wi-Fi→cellular: the address is up before the route is. The call is parked as `CALL_DEFERRED` and address discovery re-runs on each verify tick, up to `net_max_attempts`, then reports `CALL_MIGRATION_FAILED` |
| Peer media address not yet known | Same treatment — parked and retried rather than skipped |
| re-INVITE glare (`491`) | Retried by the SIP layer (3 s / 1 s per RFC 3261) |
| `401`/`407` on the re-INVITE | Re-authenticated and retried automatically |
| `408`/`481` on the re-INVITE | Dialog terminated — the call is genuinely gone |
| Call on hold | Migrated with its direction attribute preserved; media check skipped |
| Link-local-only addresses | Enough to rebind, reported as `DOWN` since no off-link registrar is reachable |

RTP sockets are never re-created. baresip binds them to the wildcard address, so
they follow the new default route automatically — only the address advertised in
the SDP has to change.

---

## Limitation: ICE

**ICE calls get best-effort recovery, not a true ICE restart.**

An RFC 8445 §9 ICE restart requires a new `ice-ufrag` / `ice-pwd` pair in the
offer. baresip generates those once when the media session is created and
exposes no way to regenerate them mid-call, and the ICE agent handle is private
to its module — so the re-INVITE necessarily carries the *same* credentials,
which by definition is not a restart. The peer will not restart either.

Practically:

- **Direct RTP, SDES-SRTP, DTLS-SRTP** migrate correctly. This is the usual
  configuration against a PBX and is what the media verification is tuned for.
- **ICE** calls may recover if the peer latches onto your new source address
  (symmetric RTP / peer-reflexive discovery), and may not. The
  `ev->u.network.ice` field is `true` for these so you can log accordingly and,
  if you want, `net_hangup_on_migration_failure` to end them cleanly.

If your deployment is mobile-first, prefer STUN plus symmetric RTP over ICE and
let the handover do its work.
