# SDK usage guide — C/C++, Python, Flutter

This is the primary reference for using the baresdk. It covers every major
operation — account setup, calls, hold, mute, transfer, stats, and more —
in all three supported languages side by side.

Each section is self-contained. Start at section 1 to initialise the SDK,
then jump to whichever operation you need.

---

## Language coverage at a glance

| Feature | C | C++ | Python | Flutter |
|---|---|---|---|---|
| Full SDK config (transport, TLS, ICE…) | ✓ | ✓ | ✓ | partial¹ |
| Full account config (STUN, TURN, media enc…) | ✓ | ✓ | ✓ | partial¹ |
| Codec selection (string names) | ✓ | ✓ | ✓ | ✓ |
| Events | callback | `on_event` lambda | `@sdk.on()` decorators | `account.events` Stream |
| Outgoing call | ✓ | ✓ | ✓ | ✓ |
| Incoming call | ✓ | ✓ | ✓ | ✓ |
| Hold / resume | ✓ | ✓ | ✓ | ✓ |
| Mute TX / RX | ✓ | ✓ | ✓ | ✓ |
| DTMF | ✓ | ✓ | ✓ | ✓ |
| Blind transfer | ✓ | ✓ | ✓ | ✓ |
| Attended transfer | ✓ | ✓ | — | — |
| Custom SIP headers | ✓ | ✓ | ✓ | ✓ |
| Audio devices | ✓ | ✓ | ✓ | ✓ |
| AEC on/off + mode (SUPPRESSOR/WEBRTC) | ✓ | ✓ | ✓ | ✓ |
| AEC suppression level tuning | ✓ | ✓ | ✓ | — |
| Mic gain (dB) | ✓ | ✓ | ✓ | — |
| Speaker gain (dB) | ✓ | ✓ | ✓ | — |
| NS / AGC (runtime toggle) | ✓ | ✓ | ✓ | ✓ |
| Jitter buffer (runtime) | ✓ | ✓ | ✓ | ✓ |
| Per-call DSCP / QoS | ✓ | ✓ | ✓ | ✓ |
| Stats (on demand) | ✓ | ✓ | ✓ | — |
| pcap capture | ✓ | ✓ | ✓ | — |
| Audio recording (WAV) | ✓ | ✓ | ✓ | ✓ |
| Retry policy control | ✓ | ✓ | ✓ | ✓ |

¹ The Flutter binding exposes `logLevel`, `statsIntervalMs`, `traceSip`,
`transport`, and `audioCodecs`. Deeper options (TLS certs, STUN/TURN) require
a thin native plugin that calls the C API directly.

---

## 1. SDK init — full config

**C**
```c
#include "baresdk.h"

static void on_event(const baresdk_event_t *ev, void *ud);

baresdk_config_t cfg;
baresdk_config_init(&cfg);          /* zero-fills; sets version + struct_size */

/* Transport */
cfg.transport    = BARESDK_TRANSPORT_TLS;   /* UDP / TCP / TLS / WS / WSS */
cfg.server_host  = "pbx.example.com";
cfg.server_port  = 0;                       /* 0 = transport default (5061) */
/* WSS: cfg.server_url = "wss://pbx.example.com:8089/ws"; */

/* TLS */
cfg.ca_cert_path  = "/etc/ssl/certs/ca-bundle.crt";
cfg.verify_server = true;
/* cfg.client_cert = "/etc/baresdk/client.crt";  mutual TLS */
/* cfg.client_key  = "/etc/baresdk/client.key"; */

/* NAT / ICE */
cfg.ice_enabled = true;
cfg.stun_server = "stun:stun.l.google.com:19302";
cfg.turn_server = "turn:turn.example.com:3478";   /* TURN takes priority over STUN */
cfg.turn_user   = "alice";
cfg.turn_pass   = "turn_secret";

/* Media */
cfg.media_enc         = BARESDK_MEDIA_ENC_SDES;   /* NONE / SDES / DTLS_SRTP */
cfg.audio_codecs[0]   = BARESDK_CODEC_OPUS;
cfg.audio_codecs[1]   = BARESDK_CODEC_PCMU;
cfg.audio_codec_count = 2;
cfg.aec_mode              = BARESDK_AEC_SUPPRESSOR; /* half-duplex suppressor (default) */
/* cfg.aec_mode           = BARESDK_AEC_WEBRTC; full-duplex — desktop only, opt-in build */
cfg.aec_suppression_level = 1.0f;  /* 0=off .. 1=max; default 1.0 */
cfg.mic_gain_db           = 0.0f;  /* TX gain dB, [-20,+20]; 0=unity */
cfg.speaker_gain_db       = 0.0f;  /* RX gain dB, [-20,+20]; 0=unity */
cfg.ns  = true;   /* noise suppression  */
cfg.agc = true;   /* auto gain control  */
cfg.jitter_buffer_min_ms = 20;   /* adaptive JB lower bound (0 = fixed 100 ms default) */
cfg.jitter_buffer_max_ms = 300;  /* adaptive JB upper bound */

/* Observability */
cfg.stats_interval_ms = 5000;   /* MEDIA_STATS event every 5 s */
cfg.trace_sip         = true;   /* SIP_TRACE event per message  */
cfg.trace_sdp_diff    = true;   /* SDP_NEGOTIATION event        */
cfg.log_level         = 1;      /* 0=err 1=warn 2=info 3=debug  */

cfg.event_cb       = on_event;
cfg.event_userdata = NULL;

baresdk_init(&cfg);
```

**C++**
```cpp
#include "baresdk.hpp"

baresdk::SDK sdk;

/* Access the full baresdk_config_t via sdk.config() */
auto& cfg = sdk.config();
cfg.transport    = BARESDK_TRANSPORT_TLS;
cfg.server_host  = "pbx.example.com";
cfg.ca_cert_path = "/etc/ssl/certs/ca-bundle.crt";
cfg.verify_server = true;
cfg.ice_enabled  = true;
cfg.stun_server  = "stun:stun.l.google.com:19302";
cfg.turn_server  = "turn:turn.example.com:3478";
cfg.turn_user    = "alice";
cfg.turn_pass    = "turn_secret";
cfg.media_enc         = BARESDK_MEDIA_ENC_SDES;
cfg.audio_codecs[0]   = BARESDK_CODEC_OPUS;
cfg.audio_codec_count = 1;
cfg.aec_mode              = BARESDK_AEC_SUPPRESSOR;
cfg.aec_suppression_level = 1.0f;
cfg.ns = cfg.agc = true;
cfg.jitter_buffer_min_ms = 20;
cfg.jitter_buffer_max_ms = 300;
cfg.stats_interval_ms = 5000;
cfg.log_level         = 1;

sdk.on_event([](const baresdk_event_t& ev) {
    /* handle events — see section 3 */
});

/* create_account() calls sdk.init() automatically on first use */
```

**Python**
```python
import baresdk as sdk

# sdk.configure() accepts every field from baresdk_config_t as a keyword argument.
# Must be called before the first sdk.create_account().
sdk.configure(
    log_level             = 1,
    stats_interval_ms     = 5000,
    verify_server         = True,
    aec_mode              = 1,    # 1=SUPPRESSOR (default); 2=WEBRTC (desktop opt-in build)
    aec_suppression_level = 1.0,  # 0=off .. 1=max
    mic_gain_db           = 0.0,  # 0=unity; +6 boosts a quiet USB mic
    speaker_gain_db       = 0.0,
    ns                    = True,
    agc                   = True,
    jitter_buffer_min_ms  = 20,
    jitter_buffer_max_ms  = 300,
)
```

**Flutter**
```dart
import 'package:baresdk/baresdk.dart';

final sdk = BareSDK(
  logLevel:        1,
  statsIntervalMs: 5000,
  traceSip:        false,
);
// The Flutter constructor exposes logLevel, statsIntervalMs, and traceSip.
// TLS certs, STUN/TURN, and codec lists require a native plugin for access.
```

---

## 2. Account creation — full config

**C**
```c
baresdk_account_config_t acfg = { 0 };

/* Identity */
acfg.uri          = "alice@pbx.example.com";   /* or "alice@host:port" */
acfg.password     = "secret";
acfg.display_name = "Alice";
acfg.auth_user    = NULL;   /* NULL = user part of uri */

/* Transport — pick one of: */
acfg.transport    = BARESDK_TRANSPORT_TLS;
/* acfg.server_host = "sip-edge.example.com";   server ≠ SIP domain */
/* acfg.server_port = 5061; */
/* acfg.server_url  = "wss://pbx.example.com/ws";   WebSocket */

/* Media encryption */
acfg.media_enc = BARESDK_MEDIA_ENC_SDES;   /* NONE / SDES / DTLS_SRTP */

/* NAT / ICE */
acfg.ice_enabled  = true;
acfg.stun_server  = "stun:stun.l.google.com:19302";
acfg.turn_server  = "turn:turn.example.com:3478";
acfg.turn_user    = "alice";
acfg.turn_pass    = "turn_secret";

acfg.verify_tls   = true;   /* false = skip cert check (testing only) */

/* Codec selection — string names, aliases accepted.
 * Omit to use the global cfg.audio_codecs list. */
strcpy(acfg.audio_codec_names[0], "ulaw");   /* G.711 µ-law  */
strcpy(acfg.audio_codec_names[1], "alaw");   /* G.711 A-law  */
strcpy(acfg.audio_codec_names[2], "opus");   /* Opus         */
acfg.audio_codec_name_count = 3;

baresdk_account_handle_t acct;
baresdk_account_create(&acfg, &acct);

baresdk_account_add_header(acct, "X-Tenant-Id", "42");    /* optional */
baresdk_account_set_100rel(acct, BARESDK_100REL_ENABLED); /* optional */

baresdk_account_register(acct);
```

Accepted codec name aliases:

| Name | Codec |
|---|---|
| `"opus"` | Opus 48 kHz stereo |
| `"ulaw"` / `"pcmu"` / `"g711u"` | G.711 µ-law |
| `"alaw"` / `"pcma"` / `"g711a"` | G.711 A-law |
| anything else | passed as-is to baresip |

Opus and G.711 are the only codecs compiled into the library.

Priority: per-account string names → per-account enum (`audio_codecs[]`) → global `cfg.audio_codecs`.
Setting none of them offers `opus, PCMU, PCMA` — the cross-platform default.

**C++**
```cpp
/* Simple form — codec list via initializer_list */
auto acct = sdk.create_account("alice@pbx.example.com", "secret",
                               BARESDK_TRANSPORT_TLS,
                               {BARESDK_CODEC_PCMU, BARESDK_CODEC_PCMA,
                                BARESDK_CODEC_OPUS});

/* Full form — use string names via baresdk_account_config_t */
baresdk_account_config_t acfg{};
acfg.uri          = "alice@pbx.example.com";
acfg.password     = "secret";
acfg.display_name = "Alice";
acfg.transport    = BARESDK_TRANSPORT_TLS;
acfg.media_enc    = BARESDK_MEDIA_ENC_SDES;
acfg.ice_enabled  = true;
acfg.stun_server  = "stun:stun.l.google.com:19302";
acfg.turn_server  = "turn:turn.example.com:3478";
acfg.turn_user    = "alice";
acfg.turn_pass    = "turn_secret";
acfg.verify_tls   = true;
std::strcpy(acfg.audio_codec_names[0], "ulaw");
std::strcpy(acfg.audio_codec_names[1], "alaw");
std::strcpy(acfg.audio_codec_names[2], "opus");
acfg.audio_codec_name_count = 3;

auto acct = sdk.create_account(acfg);
acct.add_header("X-Tenant-Id", "42");
acct.set_100rel(BARESDK_100REL_ENABLED);
acct.register_account();
```

**Python**
```python
# transport and media_enc accept strings — no constants needed.
account = sdk.create_account(
    "alice@pbx.example.com", "secret",
    transport    = "tls",
    display_name = "Alice",
    media_enc    = "sdes",
    ice_enabled  = True,
    stun_server  = "stun:stun.l.google.com:19302",
    turn_server  = "turn:turn.example.com:3478",
    turn_user    = "alice",
    turn_pass    = "turn_secret",
    audio_codecs = ["ulaw", "alaw", "opus"],
    extra_headers = {"X-Tenant-Id": "42"},
)
account.register()
```

**Flutter**
```dart
// audioCodecs accepts a list of codec name strings.
// STUN/TURN/ICE/TLS options require a native plugin.
final account = sdk.createAccount(
  'alice@pbx.example.com',
  'secret',
  transport:   baresdk_transport_t.BARESDK_TRANSPORT_TLS,
  audioCodecs: ['ulaw', 'alaw', 'opus'],
);
account.addHeader('X-Tenant-Id', '42');
account.register();
```

---

## 3. Handling events

**C**
```c
static baresdk_call_handle_t g_call = NULL;

static void on_event(const baresdk_event_t *ev, void *ud)
{
    switch (ev->type) {

    case BARESDK_EV_REG_STATE:
        if (ev->u.reg.state == BARESDK_REG_REGISTERED)
            printf("Registered\n");
        else if (ev->u.reg.state == BARESDK_REG_FAILED)
            printf("Reg failed: %s (retry in %u ms)\n",
                   ev->u.reg.error_str ? ev->u.reg.error_str : "?",
                   ev->u.reg.retry_delay_ms);
        break;

    case BARESDK_EV_INCOMING_CALL:
        printf("Incoming from %s\n", ev->u.incoming.from_uri);
        g_call = ev->u.incoming.call;
        baresdk_call_answer(g_call);   /* or store and answer later */
        break;

    case BARESDK_EV_CALL_STATE:
        printf("Call state %d  reason: %s\n",
               ev->u.call_state.state,
               ev->u.call_state.reason ? ev->u.call_state.reason : "");
        if (ev->u.call_state.state == BARESDK_CALL_ENDED   ||
            ev->u.call_state.state == BARESDK_CALL_FAILED  ||
            ev->u.call_state.state == BARESDK_CALL_CANCELLED)
            g_call = NULL;
        break;

    case BARESDK_EV_CALL_DTMF:
        printf("DTMF: %c\n", ev->u.dtmf.digit);
        break;

    case BARESDK_EV_SDP_NEGOTIATION:
        printf("Codec: %s  Crypto: %s\n",
               ev->u.sdp.negotiated_codec, ev->u.sdp.negotiated_crypto);
        break;

    case BARESDK_EV_MEDIA_STATS:
        printf("MOS-LQ %.2f  RTT %.0f ms  loss TX %.1f%%  RX %.1f%%\n",
               ev->u.stats.mos_lq, ev->u.stats.rtt_ms,
               ev->u.stats.loss_pct, ev->u.stats.loss_pct_rx);
        break;

    case BARESDK_EV_TRANSFER_REQUEST:
        printf("Transfer to %s (attended=%d)\n",
               ev->u.transfer_req.refer_to_uri,
               ev->u.transfer_req.has_replaces);
        break;

    case BARESDK_EV_MESSAGE:
        printf("MESSAGE from %s: %s\n", ev->u.msg.from_uri, ev->u.msg.body);
        break;

    case BARESDK_EV_MWI:
        printf("Voicemail: %u new, %u old\n",
               ev->u.mwi.new_voice, ev->u.mwi.old_voice);
        break;

    case BARESDK_EV_PRESENCE_STATE:
        printf("Presence %s → %d\n",
               ev->u.presence.target_uri, ev->u.presence.status);
        break;

    case BARESDK_EV_SIP_TRACE:
        printf("%s\n%s\n---\n",
               ev->u.sip_trace.dir == BARESDK_MEDIA_DIR_TX ? ">>>" : "<<<",
               ev->u.sip_trace.raw_message);
        break;

    default: break;
    }
}
```

**C++**
```cpp
sdk.on_event([&](const baresdk_event_t& ev) {
    switch (ev.type) {

    case BARESDK_EV_REG_STATE:
        if (ev.u.reg.state == BARESDK_REG_REGISTERED)
            std::cout << "Registered\n";
        else if (ev.u.reg.state == BARESDK_REG_FAILED)
            std::cout << "Reg failed: "
                      << (ev.u.reg.error_str ? ev.u.reg.error_str : "?") << "\n";
        break;

    case BARESDK_EV_INCOMING_CALL:
        std::cout << "Incoming from " << ev.u.incoming.from_uri << "\n";
        active_call = baresdk::Call(ev.u.incoming.call);
        active_call.answer();
        break;

    case BARESDK_EV_CALL_STATE:
        if (ev.u.call_state.state == BARESDK_CALL_ENDED   ||
            ev.u.call_state.state == BARESDK_CALL_FAILED  ||
            ev.u.call_state.state == BARESDK_CALL_CANCELLED)
            active_call = {};
        break;

    case BARESDK_EV_MEDIA_STATS:
        std::cout << "MOS-LQ " << ev.u.stats.mos_lq
                  << "  RTT "  << ev.u.stats.rtt_ms << " ms\n";
        break;

    default: break;
    }
});
```

**Python**
```python
# No constants needed — events use string names and string state values.
import baresdk as sdk

active_call = None

@sdk.on("registered")
def _(ev):
    print("Registered")

@sdk.on("reg_failed")
def _(ev):
    print(f"Reg failed: {ev.error_str}  retry in {ev.retry_delay_ms} ms")

@sdk.on("incoming_call")
def _(ev):
    global active_call
    print(f"Incoming from {ev.from_uri}")
    active_call = ev.call
    active_call.answer()

@sdk.on("call_state")
def _(ev):
    global active_call
    print(f"Call state: {ev.state}  reason: {ev.reason}")
    if ev.state in ("ended", "failed", "cancelled"):
        active_call = None
        sdk.stop()

@sdk.on("dtmf")
def _(ev):
    print(f"DTMF: {ev.digit}")

@sdk.on("sdp_negotiation")
def _(ev):
    print(f"Codec: {ev.negotiated_codec}  Crypto: {ev.negotiated_crypto}")

@sdk.on("media_stats")
def _(ev):
    print(f"MOS-LQ {ev.mos_lq:.2f}  RTT {ev.rtt_ms:.0f} ms  "
          f"loss TX {ev.loss_pct:.1f}%  RX {ev.loss_pct_rx:.1f}%")

@sdk.on("transfer_request")
def _(ev):
    print(f"Transfer to {ev.refer_to_uri}  attended={ev.has_replaces}")

@sdk.on("message")
def _(ev):
    print(f"MESSAGE from {ev.from_uri}: {ev.body}")

@sdk.on("mwi")
def _(ev):
    print(f"Voicemail: {ev.new_voice} new, {ev.old_voice} old")

@sdk.on("presence_state")
def _(ev):
    print(f"Presence {ev.target_uri} → {ev.status}")

@sdk.on("sip_trace")
def _(ev):
    arrow = ">>>" if ev.direction == "tx" else "<<<"
    print(f"{arrow}\n{ev.raw_message}\n---")
```

**Flutter**
```dart
account.events.listen((ev) {

  if (ev is RegStateEvent) {
    if (ev.state == baresdk_reg_state_t.BARESDK_REG_REGISTERED) {
      print('Registered');
    } else if (ev.state == baresdk_reg_state_t.BARESDK_REG_FAILED) {
      print('Reg failed: ${ev.errorStr}');
    }

  } else if (ev is IncomingCallEvent) {
    print('Incoming from ${ev.fromUri}');
    activeCall = ev.call;
    activeCall!.answer();

  } else if (ev is CallStateEvent) {
    final done = ev.state == baresdk_call_state_t.BARESDK_CALL_ENDED   ||
                 ev.state == baresdk_call_state_t.BARESDK_CALL_FAILED  ||
                 ev.state == baresdk_call_state_t.BARESDK_CALL_CANCELLED;
    if (done) { activeCall = null; }

  } else if (ev is CallDtmfEvent) {
    print('DTMF: ${ev.digit}');

  } else if (ev is MediaStatsEvent) {
    print('MOS-LQ ${ev.mosLq.toStringAsFixed(2)}  '
          'RTT ${ev.rttMs.toStringAsFixed(0)} ms  '
          'loss TX ${ev.lossPct.toStringAsFixed(1)}%  '
          'RX ${ev.lossPctRx.toStringAsFixed(1)}%');

  } else if (ev is MessageEvent) {
    print('MESSAGE from ${ev.fromUri}: ${ev.body}');

  } else if (ev is PresenceStateEvent) {
    print('Presence ${ev.targetUri} → ${ev.status}');

  } else if (ev is SipTraceEvent) {
    print(ev.direction == 1 ? '>>>' : '<<<');
    print(ev.rawMessage);
  }
});
```

---

## 4. Outgoing call

**C**
```c
baresdk_call_handle_t call;
baresdk_call_invite(acct, "sip:bob@pbx.example.com", &call);
/* → CALL_STATE events: CALLING → RINGING → ESTABLISHED (or FAILED) */
```

**C++**
```cpp
auto call = acct.call("sip:bob@pbx.example.com");
```

**Python**
```python
call = sdk.call("bob@pbx.example.com")   # sip: prefix and @domain added automatically
# or with explicit account when multiple accounts exist:
call = sdk.call("bob@pbx.example.com", account=account)
# or via the account directly:
call = account.call("sip:bob@pbx.example.com")
```

**Flutter**
```dart
final call = account.call('sip:bob@pbx.example.com');
```

---

## 5. Hold and resume

> Hold sends a re-INVITE (sendonly) and notifies the remote party.
> Mute silences audio locally without a re-INVITE — see section 6.

**C**
```c
baresdk_call_hold(call);    /* → CALL_STATE: HELD */
baresdk_call_resume(call);  /* → CALL_STATE: ESTABLISHED */
```

**C++**
```cpp
call.hold();
call.resume();
```

**Python**
```python
call.hold()
call.resume()
```

**Flutter**
```dart
call.hold();
call.resume();
```

---

## 6. Mute and unmute

**C**
```c
baresdk_audio_mute(call, true);     /* mic off  — remote can't hear you */
baresdk_audio_mute(call, false);    /* mic on */

baresdk_audio_mute_rx(call, true);  /* speaker off — you can't hear remote */
baresdk_audio_mute_rx(call, false); /* speaker on */
```

**C++**
```cpp
call.mute(true);     /* mic off  */
call.mute(false);    /* mic on   */
/* Note: C++ wrapper exposes mute() but not mute_rx() directly.
   Use the C function for RX mute: baresdk_audio_mute_rx(call.handle(), true); */
```

**Python**
```python
call.mute(muted=True)      # mic off
call.mute(muted=False)     # mic on

call.mute_rx(muted=True)   # speaker off
call.mute_rx(muted=False)  # speaker on
```

**Flutter**
```dart
call.mute(on: true);     // mic off
call.mute(on: false);    // mic on

call.muteRx(on: true);   // speaker off
call.muteRx(on: false);  // speaker on
```

---

## 7. DTMF

Digits: `0`–`9`, `*`, `#`, `A`–`D` (RFC 4733 RTP events).

**C**
```c
baresdk_call_send_dtmf(call, '5');
baresdk_call_send_dtmf(call, '#');
```

**C++**
```cpp
call.send_dtmf('5');
call.send_dtmf('#');
```

**Python**
```python
call.send_dtmf('5')
call.send_dtmf('#')
```

**Flutter**
```dart
call.sendDtmf('5');
call.sendDtmf('#');
```

---

## 8. Blind transfer (REFER)

The remote party (bob) receives a REFER and re-invites the target. Your leg ends.

**C**
```c
baresdk_call_transfer(call, "sip:carol@pbx.example.com");
/* → CALL_STATE: ENDED for this call on success */
```

**C++**
```cpp
call.transfer("sip:carol@pbx.example.com");
```

**Python**
```python
call.transfer("sip:carol@pbx.example.com")
```

**Flutter**
```dart
call.transfer('sip:carol@pbx.example.com');
```

---

## 9. Attended transfer (C / C++ only)

Warm handoff: hold the first call, dial a consultation, then bridge the two.

**C**
```c
/* Step 1 — hold the first call */
baresdk_call_hold(call_a);

/* Step 2 — dial consultation */
baresdk_call_handle_t call_b;
baresdk_call_invite(acct, "sip:carol@pbx.example.com", &call_b);
/* wait for call_b → ESTABLISHED */

/* Step 3 — send REFER w/ Replaces: bridges call_a to call_b */
baresdk_call_attended_transfer(call_a, call_b);
/* call_a → ENDED; call_b → ENDED */
```

**C++**
```cpp
call_a.hold();
auto call_b = acct.call("sip:carol@pbx.example.com");
/* wait for call_b ESTABLISHED */
call_a.attended_transfer(call_b);
```

> Python and Flutter do not currently expose `attended_transfer`.
> Call `baresdk_call_attended_transfer` via cffi / native plugin if needed.

---

## 10. Custom SIP headers

Account-level headers are sent on every request (REGISTER, INVITE, BYE…).
Call-level headers are sent on re-INVITEs, BYE, and REFER within that dialog.

**C**
```c
baresdk_account_add_header(acct, "X-Tenant-Id",  "42");   /* account-level */
baresdk_call_add_header   (call, "X-Call-Track",  "abc");  /* call-level */
```

**C++**
```cpp
acct.add_header("X-Tenant-Id",  "42");
call.add_header("X-Call-Track", "abc");
```

**Python**
```python
account.add_header("X-Tenant-Id",  "42")
# call-level not yet exposed in Python; use cffi directly if needed
```

**Flutter**
```dart
account.addHeader('X-Tenant-Id', '42');
// call-level not yet exposed in Flutter binding
```

---

## 11. Audio devices

**C**
```c
baresdk_audio_device_t devs[32];
int n;

n = baresdk_audio_list_input_devices(devs, 32);
for (int i = 0; i < n; i++)
    printf("in  [%d] %s%s\n", i, devs[i].name,
           devs[i].is_default ? " *" : "");

n = baresdk_audio_list_output_devices(devs, 32);
for (int i = 0; i < n; i++)
    printf("out [%d] %s%s\n", i, devs[i].name,
           devs[i].is_default ? " *" : "");

baresdk_audio_set_input_device("HDA Intel PCH: ALC3204 Analog (hw:0,0)");
baresdk_audio_set_output_device(NULL);   /* NULL = platform default */
```

**C++**
```cpp
/* C++ uses the C functions directly — no wrapper needed */
baresdk_audio_device_t devs[32];
int n = baresdk_audio_list_input_devices(devs, 32);
for (int i = 0; i < n; i++)
    std::cout << "in [" << i << "] " << devs[i].name << "\n";

baresdk_audio_set_input_device("HDA Intel PCH: ALC3204 Analog (hw:0,0)");
baresdk_audio_set_output_device(nullptr);
```

**Python**
```python
for d in sdk.list_input_devices():
    print(f"in  {d['name']}{'  *' if d['is_default'] else ''}")

for d in sdk.list_output_devices():
    print(f"out {d['name']}{'  *' if d['is_default'] else ''}")

sdk.set_input_device("HDA Intel PCH: ALC3204 Analog (hw:0,0)")
sdk.set_output_device("")   # empty string = platform default
```

**Flutter**
```dart
for (final d in sdk.listInputDevices()) {
  print('in  ${d.name}${d.isDefault ? "  *" : ""}');
}
for (final d in sdk.listOutputDevices()) {
  print('out ${d.name}${d.isDefault ? "  *" : ""}');
}
// setInputDevice / setOutputDevice not yet in Flutter binding;
// call baresdk_audio_set_input_device via a native plugin if needed.
```

---

## 12. Media stats

Stats fire automatically via `MediaStatsEvent` when `statsIntervalMs > 0`.
C and C++ can also query them on demand.

**C / C++ — on demand**
```c
baresdk_ev_media_stats_t s;
if (baresdk_call_get_stats(call, &s) == BARESDK_OK) {
    printf("MOS-LQ %.2f  MOS-CQ %.2f\n",    s.mos_lq, s.mos_cq);
    printf("RTT %.0f ms  jitter %.1f ms\n",  s.rtt_ms, s.jitter_ms);
    printf("loss TX %.1f%% RX %.1f%%\n",     s.loss_pct, s.loss_pct_rx);
    printf("bw TX %u kbps  RX %u kbps\n",   s.bandwidth_kbps_tx, s.bandwidth_kbps_rx);
    printf("jbuf %u ms  late %u  disc %u\n", s.jitter_buffer_ms,
                                              s.late_packets, s.discarded_packets);
    printf("codec %s  %u kHz  PT %d\n",      s.codec_name,
                                              s.codec_clock_rate / 1000, s.payload_type);
}
```

**C++**
```cpp
auto s = call.stats();   /* returns baresdk_ev_media_stats_t by value */
std::cout << "MOS-LQ " << s.mos_lq << "  RTT " << s.rtt_ms << " ms\n";
```

**Python — event-driven (automatic)**
```python
@sdk.on("media_stats")
def _(ev):
    print(f"MOS-LQ {ev.mos_lq:.2f}  MOS-CQ {ev.mos_cq:.2f}")
    print(f"RTT {ev.rtt_ms:.0f} ms  jitter {ev.jitter_ms:.1f} ms")
    print(f"loss TX {ev.loss_pct:.1f}%  RX {ev.loss_pct_rx:.1f}%")
    print(f"bw TX {ev.bandwidth_kbps_tx} kbps  RX {ev.bandwidth_kbps_rx} kbps")
    print(f"codec {ev.codec_name}  {ev.codec_clock_rate // 1000} kHz  PT {ev.payload_type}")
```

**Python — custom polling rate**
```python
@sdk.on("established")
def _(ev):
    # Poll every 2 s regardless of stats_interval_ms
    ev.call.poll_stats(interval=2.0, on_update=lambda s: s.print())
```

**Python — one-shot snapshot**
```python
snap = call.stats()               # new CallStats object
call.fetch_stats(existing_stats)  # update existing CallStats in-place
```

**Flutter — from MediaStatsEvent**
```dart
} else if (ev is MediaStatsEvent) {
  print('MOS-LQ ${ev.mosLq.toStringAsFixed(2)}  '
        'MOS-CQ ${ev.mosCq.toStringAsFixed(2)}');
  print('RTT ${ev.rttMs.toStringAsFixed(0)} ms  '
        'jitter ${ev.jitterMs.toStringAsFixed(1)} ms');
  print('loss TX ${ev.lossPct.toStringAsFixed(1)}%  '
        'RX ${ev.lossPctRx.toStringAsFixed(1)}%');
  print('bw TX ${ev.bandwidthTx} kbps  RX ${ev.bandwidthRx} kbps');
  print('codec ${ev.codec}  '
        '${(ev.codecClockRate / 1000).round()} kHz  PT ${ev.payloadType}');
}
```

### Stats field reference

| C / C++ field | Python field | Flutter field | Description |
|---|---|---|---|
| `mos_lq` / `mos_cq` | `mos_lq` / `mos_cq` | `mosLq` / `mosCq` | MOS quality (1.0–4.5) |
| `rtt_ms` | `rtt_ms` | `rttMs` | Round-trip time (ms) |
| `jitter_ms` | `jitter_ms` | `jitterMs` | RX interarrival jitter |
| `tx_jitter_ms` | `tx_jitter_ms` | `txJitterMs` | TX jitter (RTCP from remote) |
| `loss_pct` / `packets_lost` | same | `lossPct` / `packetsLost` | TX-side loss |
| `loss_pct_rx` / `packets_lost_rx` | same | `lossPctRx` / `packetsLostRx` | RX-side loss |
| `bandwidth_kbps_tx/rx` | same | `bandwidthTx/Rx` | Current bitrate |
| `avg_bandwidth_kbps_tx/rx` | same | `avgBandwidthTx/Rx` | Session-average bitrate |
| `jitter_buffer_ms` | same | `jitterBufferMs` | Current buffer depth (ms); 100 ms when FIXED |
| `jitter_buffer_load` | same | `jitterBufferLoad` | Packets currently held in buffer |
| `jitter_buffer_target_ms` | same | `jitterBufferTargetMs` | Jitter estimate used to set depth (adaptive only) |
| `jitter_buffer_adaptive` | same | `jitterBufferAdaptive` | `true` when adaptive mode is active |
| `late_packets` | same | `latePackets` | Packets arrived too late |
| `discarded_packets` | same | `discardedPackets` | Packets dropped (overflow or flush) |
| `codec_name` | same | `codec` | e.g. `"opus"`, `"PCMU"` |
| `payload_type` | same | `payloadType` | RTP PT number |
| `ssrc_tx` / `ssrc_rx` | same | `ssrcTx` / `ssrcRx` | RTP SSRCs |
| `remote_addr` | same | `remoteAddr` | Remote RTP `"ip:port"` |
| `audio_level_dbov` | same | `audioLevelDbov` | Received audio level in dBov (0 = max, −127 = silence); computed from RX PCM — always available |

---

## 13. pcap capture (C / C++ / Python)

```c
baresdk_pcap_start("/tmp/call.pcap");
/* ... calls happen here ... */
baresdk_pcap_stop();
```

```cpp
sdk.pcap_start("/tmp/call.pcap");
sdk.pcap_stop();
```

```python
sdk.pcap_start("/tmp/call.pcap")
# ... calls happen here ...
sdk.pcap_stop()
```

Flutter does not expose pcap. Use a native plugin to call `baresdk_pcap_start`.

---

## 14. Audio recording (C / C++ / Python / Flutter)

Record call audio to a single mixed WAV file. Both sides of the conversation (RX + TX) are clip-summed into one stream.

**C**
```c
baresdk_call_record_start(call, "/tmp/call.wav");
baresdk_call_record_stop(call);   // call before hangup for a clean WAV header
```

**C++**
```cpp
call.record_start("/tmp/call.wav");
call.record_stop();
```

**Python**
```python
call.record_start("/tmp/call.wav")
call.record_stop()
```

**Flutter**
```dart
call.recordStart("/tmp/call.wav");
call.recordStop();
```

Typical pattern — tie recording to call state:

**C**
```c
case BARESDK_EV_CALL_STATE:
    if (ev->u.call_state.state == BARESDK_CALL_ESTABLISHED)
        baresdk_call_record_start(ev->u.call_state.call, "/tmp/call.wav");
    if (ev->u.call_state.state == BARESDK_CALL_CLOSED)
        baresdk_call_record_stop(ev->u.call_state.call);
```

Output format: PCM S16LE WAV, 48 kHz/2ch (Opus) or 8 kHz/1ch (G.711). Recording is independent of the media tap — both can be active at the same time.

---

## 15. Runtime audio quality controls

Gain, echo cancellation, noise suppression, AGC, jitter buffer, and DSCP are all
adjustable live — no re-dial needed.

### Microphone and speaker gain

**C**
```c
/* dB, clamped to [-20, +20]. 0.0 = unity (fast-path bypass, zero overhead). */
baresdk_set_mic_gain_db(6.0f);      /* boost quiet USB mic +6 dB */
baresdk_set_speaker_gain_db(-3.0f); /* reduce playback -3 dB     */
baresdk_set_mic_gain_db(0.0f);      /* back to unity              */
```

**C++**
```cpp
sdk.set_mic_gain(6.0f);
sdk.set_speaker_gain(-3.0f);
```

**Python**
```python
sdk.set_mic_gain(6.0)
sdk.set_speaker_gain(-3.0)
sdk.set_mic_gain(0.0)   # bypass
```

> Mic gain applies *before* NS/AGC/AEC on the TX chain — it's a raw pre-boost.
> AGC then normalises the boosted signal. Safe to call from any thread.

### Echo cancellation

**C**
```c
/* Simple on/off — re-enables whichever backend was set at init */
baresdk_set_aec(true);
baresdk_set_aec(false);

/* Fine-grained: switch mode (only AEC_OFF ↔ init_mode allowed at runtime) */
baresdk_set_aec_mode(BARESDK_AEC_OFF);
baresdk_set_aec_mode(BARESDK_AEC_SUPPRESSOR);  /* restore default */

/* Tune suppressor aggressiveness (SUPPRESSOR mode only)
 * 0.0 = no TX suppression; 1.0 = max (−16.5 dB floor, default) */
baresdk_set_aec_suppression_level(0.6f);  /* less ducking on double-talk */
baresdk_set_aec_suppression_level(1.0f);  /* restore default             */
```

**C++**
```cpp
sdk.set_aec(true);
sdk.set_aec_mode(BARESDK_AEC_OFF);
sdk.set_aec_suppression_level(0.6f);
```

**Python**
```python
sdk.set_aec(True)                     # enable
sdk.set_aec(False)                    # disable
sdk.set_aec_mode(0)                   # 0=off
sdk.set_aec_mode(1)                   # 1=suppressor (restore default)
sdk.set_aec_mode(2)                   # 2=webrtc (desktop only, opt-in build)
sdk.set_aec_suppression_level(0.6)    # tune aggressiveness (suppressor only)
```

**AEC mode comparison**

| | `AEC_SUPPRESSOR` (default) | `AEC_WEBRTC` (opt-in build) |
|--|--|--|
| Duplex | Half-duplex (ducks TX when RX loud) | Full-duplex (subtracts echo) |
| Double-talk | One side goes quiet | Both parties heard |
| Platform | All | Desktop only |
| Build | None | `cmake -DBARESDK_WITH_WEBRTC_AEC=ON` + `libwebrtc-audio-processing-1-dev` |

> `AEC_WEBRTC` must be configured at init — switching between SUPPRESSOR and WEBRTC
> at runtime returns `EINVAL`. Only `AEC_OFF ↔ init_mode` transitions are valid.
>
> On **Android** and **iOS**, full-duplex AEC is handled automatically by the OS
> audio driver — no SDK flag needed.

### Noise suppression and AGC

**C**
```c
baresdk_set_ns(true);    /* noise suppression  */
baresdk_set_agc(true);   /* auto gain control  */
```

**C++**
```cpp
sdk.set_ns(true);
sdk.set_agc(true);
```

**Python**
```python
sdk.set_ns(True)
sdk.set_agc(True)
```

**Flutter**
```dart
sdk.setNs(true);
sdk.setAgc(true);
```

### Jitter buffer and DSCP

**C**
```c
baresdk_set_jitter_buffer(20, 200);   /* widen on poor network — new calls only */
baresdk_call_set_dscp_rtp(call, 46);  /* EF — takes effect next outgoing packet */
```

**C++**
```cpp
sdk.set_jitter_buffer(20, 200);
call.set_dscp_rtp(46);
```

**Python**
```python
sdk.set_jitter_buffer(20, 200)
call.set_dscp_rtp(46)
```

**Flutter**
```dart
sdk.setJitterBuffer(20, 200);
call.setDscpRtp(46);
```

> **Jitter buffer:** when both bounds are 0 (the default), the buffer runs in **fixed**
> mode at 100 ms. Setting `min_ms` / `max_ms` switches to **adaptive** mode. Runtime
> changes take effect on new calls only — active calls are unaffected.

---

## 16. Registration retry control

The SDK retries failed registrations automatically. These functions let you adjust the policy at runtime or take manual control.

**C**
```c
// Tighten retries for a specific account (e.g. aggressive mobile reconnect)
baresdk_account_set_retry_policy(acct,
    1000,   // initial_ms   — first retry after 1 s
    30000,  // max_ms       — cap at 30 s
    1.5f,   // backoff      — 1 s → 1.5 s → 2.25 s → … → 30 s
    0       // max_attempts — 0 = retry forever
);

// User taps "Cancel reconnect"
baresdk_account_cancel_retry(acct);

// User taps "Retry now" — skip the current backoff delay
baresdk_account_retry_now(acct);
```

**C++**
```cpp
acct.set_retry_policy(1000, 30000, 1.5f, 0);
acct.cancel_retry();
acct.retry_now();
```

**Python**
```python
account.set_retry_policy(initial_ms=1000, max_ms=30000,
                         backoff=1.5, max_attempts=0)
account.cancel_retry()
account.retry_now()
```

**Flutter**
```dart
// Flutter binding exposes the same three calls
internal.nativeBindings.baresdk_account_set_retry_policy(
    account.handle, 1000, 30000, 1.5, 0);
internal.nativeBindings.baresdk_account_cancel_retry(account.handle);
internal.nativeBindings.baresdk_account_retry_now(account.handle);
```

Each scheduled retry fires `BARESDK_EV_REG_STATE` with `state == BARESDK_REG_FAILED`:

**C**
```c
case BARESDK_EV_REG_STATE:
    if (ev->u.reg.state == BARESDK_REG_FAILED && ev->u.reg.retry_attempt > 0)
        printf("retry %u in %u ms\n",
               ev->u.reg.retry_attempt,
               ev->u.reg.retry_delay_ms);
```

**Python**
```python
@sdk.on("reg_failed")
def _(ev):
    if ev.retry_attempt > 0:
        print(f"retry {ev.retry_attempt} in {ev.retry_delay_ms} ms")
```

**Flutter**
```dart
} else if (ev is RegStateEvent &&
           ev.state == baresdk_reg_state_t.BARESDK_REG_FAILED) {
  // retry_attempt and retry_delay_ms are in the raw C struct;
  // read via the event pointer if needed, or handle reconnect logic here
}
```

---

## 17. Teardown

**C**
```c
baresdk_call_hangup(call);           /* BYE */
baresdk_account_unregister(acct);    /* REGISTER Expires: 0 */
baresdk_account_destroy(acct);       /* blocks until complete */
baresdk_shutdown();
```

**C++**
```cpp
call.hangup();
acct.unregister();
/* acct destructor calls baresdk_account_destroy automatically */
/* sdk destructor calls baresdk_shutdown automatically */
```

**Python**
```python
call.hangup()
account.unregister()
# Teardown is automatic when sdk.run() exits (Ctrl-C or sdk.stop()).
# To tear down manually without run():
account.destroy()
```

**Flutter**
```dart
call.hangup();
account.unregister();
account.destroy();
sdk.shutdown();
```

---

## Transport / config quick-reference

| Goal | C/C++ / Python field | Flutter |
|---|---|---|
| Plain UDP | `transport=UDP` | `transport: BARESDK_TRANSPORT_UDP` |
| TLS (port 5061) | `transport=TLS`, `ca_cert_path`, `verify_server=true` | `transport: BARESDK_TRANSPORT_TLS` |
| WebSocket | `server_url="ws://host/ws"` | — native plugin |
| Secure WebSocket | `server_url="wss://host/ws"` | — native plugin |
| STUN only | `ice_enabled=true`, `stun_server="stun:host:3478"` | — native plugin |
| TURN only | `ice_enabled=true`, `turn_server=…`, `turn_user`, `turn_pass` | — native plugin |
| STUN + TURN | both — TURN takes priority | — native plugin |
| WebRTC interop | `media_enc=DTLS_SRTP`, `ice_enabled=true`, STUN+TURN | — native plugin |
| Voice quality (init) | `aec_mode=SUPPRESSOR`, `ns=true`, `agc=true` | — native plugin |
| Echo on/off (runtime) | `baresdk_set_aec(bool)` / `sdk.set_aec(bool)` | `sdk.setAec(bool)` |
| Echo mode (runtime) | `baresdk_set_aec_mode(mode)` / `sdk.set_aec_mode(mode)` | — native plugin |
| Echo suppressor strength | `baresdk_set_aec_suppression_level(f)` / `sdk.set_aec_suppression_level(f)` | — |
| NS / AGC (runtime) | `baresdk_set_ns/agc()` / `sdk.set_ns/agc()` | `sdk.setNs/Agc()` |
| Mic gain (runtime) | `baresdk_set_mic_gain_db(db)` / `sdk.set_mic_gain(db)` | — native plugin |
| Speaker gain (runtime) | `baresdk_set_speaker_gain_db(db)` / `sdk.set_speaker_gain(db)` | — native plugin |
| Jitter buffer (init, adaptive) | `jitter_buffer_min_ms` / `jitter_buffer_max_ms` in `SDKConfig` | — native plugin |
| Jitter buffer (runtime) | `baresdk_set_jitter_buffer()` / `sdk.set_jitter_buffer()` | `sdk.setJitterBuffer()` |
| Per-call DSCP | `baresdk_call_set_dscp_rtp()` / `call.set_dscp_rtp()` | `call.setDscpRtp()` |
| Codec selection (string names) | `audio_codec_names[]`, `audio_codec_name_count` | `audioCodecs: [...]` |
| Codec selection (enum) | `audio_codecs[]`, `audio_codec_count` | — (use string names) |

---

## See also

- [NAT traversal (ICE / STUN / TURN)](nat_traversal.md)
- [TLS and WSS setup](tls_wss.md)
- [Multiple accounts](multi_account.md)
- [Observability](../api/observability.md)
- [Events reference](../api/events.md)
