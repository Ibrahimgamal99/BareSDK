# Changelog

All notable changes to baresdk are documented here.

---

## [Unreleased]

### Added

- **Degraded-link handling** — the SDK already recovered from a *changed*
  address (network handover); it had nothing for a link that keeps its address
  and stops working. That is the ordinary mobile failure: one bar of signal, a
  saturated uplink, a cell that stops forwarding packets without dropping the
  PDP context, a carrier NAT that quietly expires a UDP binding. In every case
  the local IP is unchanged so handover sees nothing, the dialog is healthy so
  the stack reports the call as up, and the user hears silence. New guide:
  [Degraded links](guides/degraded_links.md).

  - `cfg.media_stall_ms` (default 4000) fires `BARESDK_QUALITY_MEDIA_STALL`
    when inbound RTP stops advancing and again with `recovering` when it
    resumes. Non-fatal, and the only way this condition becomes observable at
    all — when RTP stops, every other metric simply stops changing.
    Suppressed on held calls and during a handover migration, where
    `BARESDK_EV_NETWORK` already narrates the outage in more detail.
  - `cfg.rtp_timeout_s` (default 0 = off) wires baresip's `avt.rtp_timeout`,
    which was never set: the fatal counterpart that ends such a call. Left off
    because ending a call is destructive and some deployments run legitimate
    one-way media. Per-call override: `baresdk_call_set_rtp_timeout()`.
  - `cfg.adaptive_bitrate` steps the Opus encoder down under the loss the
    *peer* reports over RTCP and back up on recovery — halve down, +25% up,
    with a dead band between `adapt_loss_down_pct` and `adapt_loss_up_pct` so a
    link hovering near a threshold does not oscillate, and
    `adapt_recover_ticks` clean ticks required before any increase. Applied
    through the codec's encoder-update path, so there is no re-INVITE, no
    renegotiation and no gap in the audio. Runtime control:
    `baresdk_set_adaptive_bitrate()`, `baresdk_call_set_bitrate()`.
  - `cfg.opus_expected_loss_pct` supplies the `opus_packet_loss` the opus
    module needs. `cfg.opus.fec` alone only *permitted* in-band FEC: the
    encoder sizes its redundant LBRR frame from this percentage and baresip's
    decoder gates FEC reconstruction on it being non-zero, so `opus.fec` on its
    own concealed nothing.

- **Keepalive is a real mechanism instead of a dead config field** —
  `cfg.keepalive_interval` had a documented default of 30000 and was read by no
  code at all. It now drives a SIP OPTIONS probe on that period, which does two
  jobs: it refreshes the UDP NAT binding — carrier NAT drops idle mappings
  after 30–180 s, while the default `reg_expires` is 3600 — and its answer, or
  absence, tests reachability. Any response counts, including 405: a proxy that
  refuses OPTIONS still received it. On failure the account reports
  `BARESDK_ERR_TIMEOUT` and, with the new `cfg.keepalive_reregister` (default
  true), re-REGISTERs immediately rather than leaving an unreachable
  registration nominally healthy for up to an hour. Suppressed while a call is
  up on the account: RTP already holds the binding open, and an extra request
  competing with media for a congested uplink is exactly wrong.
  `baresdk_account_keepalive_now()` runs the same probe on demand, for a
  foreground or push-wake handler.

- **`cfg.sip_timer_b_ms` / `cfg.sip_timer_f_ms` now do something** — both were
  dead fields. A request onto a black-holed link gets no response at all, and
  RFC 3261 bounds that at 64·T1 = 32 s, which in libre is a compile-time
  constant with no runtime knob — so an app that would rather fail in eight
  seconds and offer to retry had nowhere to say so. `sip_timer_b_ms` now arms
  an SDK-side watchdog that cancels an outgoing call still in `CALLING` with
  408, surfacing as `BARESDK_CALL_FAILED` / `BARESDK_ERR_TIMEOUT`; only
  `CALLING` is watched, because a call that reached `RINGING` has proven the
  path works and how long to let it ring is a product decision.
  `sip_timer_f_ms` now bounds the registration watchdog, which was hardcoded to
  35 s. `sip_t1_ms` / `sip_t2_ms` remain compile-time in libre and are
  documented as informational rather than left to look configurable.

- **RFC 3263 SRV failover** — `src/dns.c` implemented the whole NAPTR→SRV chain
  and `bsdk_dns_resolve()` had no callers, so every retry re-sent to the host
  that had just timed out and a down primary proxy was never failed over to the
  secondary the records exist to name. Registration now resolves the target
  list once per account and advances one target per failed attempt, in
  (priority, weight) order, wrapping at the end. `cfg.dns_srv_failover`
  (default true) gates it; it is skipped when there is no ordered list to walk
  or when an operator has already chosen — a pinned `outbound_proxy`, an IP
  literal, an explicit port, or a WS/WSS URL. Results are now sorted, which
  they were not: the handlers appended them in DNS-answer order, so the "first"
  target was an accident of packet timing.

- **`cfg.reg_retry_jitter`** (default 0.2) — the retry backoff was documented as
  "exponential backoff with jitter" and had none. Every device that lost the
  same network ran the same schedule from the same instant, so the fleet arrived
  at the registrar as one burst, and because the schedules never diverged the
  herd re-formed on every subsequent attempt.

- **`cfg.net_ice_handover`** and `BARESDK_NET_CALL_ICE_STALE` — an ICE call
  cannot re-gather candidates mid-call (baresip fixes the local ufrag/pwd when
  the media session is allocated and its mnat update handler re-runs
  `icem_update()` rather than re-gathering), so the handover re-INVITE
  necessarily carries stale candidates. That cannot be fixed from outside the
  library, so instead it is made visible and bounded: the event is emitted once
  per call per handover *before* the offer goes out, and
  `BARESDK_ICE_HANDOVER_FAIL_FAST` reports failure after one attempt rather
  than spending `net_verify_ms` × `net_max_attempts` — 24 s of silence at the
  defaults — on an offer that cannot succeed. `max_attempts` in the event now
  reflects the budget the call is actually held to.

- `test/unit/test_fmtp_bitrate.c` — 45 assertions over the fmtp rewriting that
  makes adaptive bitrate safe, including that feeding its own output back in
  reaches a fixed point rather than growing the string on every adaptation step.

### Changed

- **Media statistics are on by default** (`cfg.stats_interval_ms` 0 → 2000) and
  the three quality-alert thresholds are set to the values their own field docs
  recommended (`mos_alert_threshold` 3.5, `loss_alert_threshold` 5.0,
  `jitter_alert_threshold` 40.0). `stats_interval_ms` is the master switch for
  RTCP accounting in baresip, so at 0 the loss/jitter/RTT/MOS fields read back
  as zero from `baresdk_call_get_stats()` too — an app that never set it got no
  quality signal anywhere and nothing said so. Everything added above reads
  from this tick. Set it to 0 to opt out.

- A `408` on a call now maps to `BARESDK_ERR_TIMEOUT` rather than
  `BARESDK_ERR_INVAL`. It is what the new setup watchdog cancels with, and what
  a proxy sends when its own transaction timed out; neither is the
  malformed-request sense of `INVAL`.

### Fixed

- **Python: every config field after `cfg.aec_mode` was written to the wrong
  offset.** `baresdk_aec_mode_t` is a packed 1-byte enum, and the cffi header
  generator strips `__attribute__((packed))` along with every other attribute,
  so cffi widened the field to 4 bytes and shifted the 29 members that follow —
  `stats_interval_ms`, the whole `net_*` group, the retry policy, the session
  timers. `sizeof` still matched by padding coincidence, which is why the
  `struct_size` check in `baresdk_init()` never caught it.
  `bindings/python/build.sh` now passes `-DBARESDK_NO_PACKED_ENUM=1`, selecting
  the `uint8_t` typedef that preserves the layout, the same way
  `bindings/flutter/ffigen.yaml` already did. Verified by comparing every field
  offset and every struct size against the C compiler: 0 mismatches.

- **Python: `configure(transport="udp")` raised `TypeError`.** The string form is
  what `configure()` documents and what `create_account()` already accepted, but
  the global path assigned it straight to an integer field, so the documented
  call failed from inside `_ensure_init()`. `transport` and `media_enc` are now
  translated there too.

- `bindings/flutter`: `BareSDKConfig.statsIntervalMs` and the three alert
  thresholds defaulted to 0 and were written unconditionally, which would have
  overwritten the new native defaults with "disabled". They now mirror the
  native defaults.

- `test/unit/Makefile`: `test_ws_pin` did not link. Its own comment says to keep
  `--wrap` and the flag was absent, so `make test` failed out of the box on the
  first target that needs the sysroot.

- **App-owned audio device** — `baresdk_audio_use_external(true)` stops the SDK opening any capture or playback device of its own (no OpenSL ES, no AudioUnit) and hands the microphone and speaker to the host app, which moves PCM across `baresdk_audio_external_push()` / `baresdk_audio_external_pull()` from whatever the platform gives it — `AudioRecord`/`AudioTrack`, `AVAudioEngine`, a WebRTC `AudioDeviceModule`, or a file for testing. `baresdk_audio_external_format()` reports the rate/channels/ptime the call negotiated. Switching is live, including mid-call, and `false` restores the platform device. Note it replaces the *device*: the platform echo cancellers the SDK relies on belong to the drivers being displaced, so an app that takes this over owns AEC too.

- **App-owned audio reaches every binding** — Flutter: `BareSDK.useAppOwnedAudio()`, `appOwnedAudioFormat` (an `ExternalAudioFormat` with `samplesPerFrame`), `appOwnedAudioActive`. C++: `SDK::use_external_audio()`, `audio_push()`, `audio_pull()`, `audio_format()`, `audio_external_active()`. Python: `use_external_audio()`, `external_audio_push()` (any S16LE buffer — `bytes`, `array('h')`, numpy), `external_audio_pull()`, `external_audio_format()`, `external_audio_active()`. The Dart facade deliberately omits push/pull: the realtime loop belongs in the plugin's native layer, because a GC pause on the capture path is a dropped frame.

- **The software echo suppressor is available again while the app owns the device** — it was vetoed wherever the platform driver cancels echo, but that driver is exactly what the app-owned device displaces, so the veto left mobile with no canceller anywhere in the chain and no way to ask for one. It is now offered as a fallback, still **off** by default (an app that takes the device is expected to capture through the platform voice path itself, and ducking its TX silently would be the SDK fighting the platform), and forced off again when the platform device returns so the two never stack.

- `bindings/flutter/example/integration_test/app_owned_audio_test.dart` — on-device gate test for the app-owned device: registers, calls an echo service, and asserts on frame counters and capture level from the native engine rather than on call state, because "connected but silent" is the failure this feature exists to prevent and call state cannot see it. The engine reports `pushFrames`, `pullFrames`, `pushErrors`, `capturePeak` and `nonSilentFrames` through `appOwnedAudioStatus()` for exactly this purpose.

- `bindings/python/examples/external_audio.py` — pushes a synthesised tone as the microphone and writes the far end to a WAV, so the app-owned device can be verified end to end on a desktop with no audio hardware.
- `test/unit/test_audio_external.c` and `test/audio_external_test.c` — unit and gate tests for the device, covering each of the regressions below.

### Fixed

- **After a reconnect, a WebSocket call could never be ended by the far end** — handover skips the re-INVITE when the local address has not changed ("same path"), which is correct for address-routed transports. A WebSocket client has no listening port, though: its Contact is the RFC 7118 placeholder `sip:user@<ip>:9;transport=wss` and the server reaches it by remembering which WebSocket the dialog's requests arrived on. A transport reset always builds a *new* WebSocket, so that association went stale on every reconnect even when the IP never moved — media kept flowing and the app's own BYE still got out (it is routed, not received), but an inbound BYE had nowhere to be delivered and the call hung in `ESTABLISHED` for the rest of the session. WS/WSS calls now re-INVITE on a transport reset regardless of the address, which re-binds the dialog to the live connection. Address-routed transports keep the shortcut and gain no extra signalling. Verified on device: reconnect mid-call, then the far end hangs up → `CALL_MIGRATING` → `CALL_MIGRATED` → inbound BYE delivered → `ENDED`.

- **Every remote hangup was reported as a call failure** — libre signals a peer-initiated termination by passing `ECONNRESET` to the session close handler: its BYE handler answers 200 OK and then calls `sipsess_terminate(sess, ECONNRESET, NULL)` (`re/src/sipsess/listen.c`), and the peer-CANCEL path does the same. baresip's close handler tests `err` before `msg`, so a perfectly normal hangup arrived as `"Connection reset by peer [104]"` and was classified `BARESDK_CALL_FAILED` with `BARESDK_ERR_TRANSPORT`. Apps that branch on `ENDED` left the call on screen, and every hangup looked like a network fault. A call that was ESTABLISHED and closes with a transport error is now reported as `BARESDK_CALL_ENDED` with the reason `"Remote hangup"`; SIP failures (486, 603, 4xx/5xx/6xx) and pre-answer errors are untouched. Verified on device against a live PBX: remote BYE → `ended`, 486 → `failed`, 603 → `failed`, local hangup → `ended`.

- **WebSocket calls opened a second connection for every dialog** — RFC 7118 gives a WS client one connection and routes everything over it, but libre routes by address: for an in-dialog request it resolves the dialog's Route/Contact and looks the connection up by peer address, so a target that is not the registration peer gets a whole new WebSocket. Behind a reverse proxy that target is the server's own loopback address (Asterisk advertises `127.0.0.1:8088` in Record-Route), so every call opened a second socket that existed only for the life of the dialog. A loopback WS destination is now rewritten to the address the registration is already connected to, so libre finds and reuses the existing connection. Linux/Android only — the fix rides the same `--wrap` mechanism as the WebSocket path workaround, which Windows and Apple's linkers do not have.

- **Android app-owned audio captured near-silence on every stereo call** — the reference engine asked `AudioRecord` for the channel count the codec negotiated, and Opus routinely negotiates 48 kHz **stereo**. Phones have no stereo voice-communication capture path: `CHANNEL_IN_STEREO` yields an `AudioRecord` that initialises, reads without error and returns audio roughly 36x too quiet — a call that looks perfectly healthy by frame count and that the far end cannot hear. Capture is now always mono and up-mixed to the negotiated layout before `push()`. Found on a Galaxy A54 (Android 16) against a live Asterisk echo test; peak capture level went from 0.0017 to 0.062 of full scale on the same call.

- **App-owned audio: `push()` never returned when the call negotiated a ptime under 20 ms** — the capture buffer's pre-fill threshold was a fixed 20 ms while the drain loop released one *ptime* at a time. Below the threshold `aubuf_read_auframe()` returns silence without draining, so the loop's condition stayed true forever: the app's realtime capture thread spun holding the device lock, and the SIP thread then deadlocked behind it on teardown. A peer offering `a=ptime:10` was enough. The threshold is now one frame, and the loop also breaks if a read fails to drain — a spin there is too expensive to leave to one invariant.

- **App-owned audio: microphone audio was discarded at the start of every call** — the capture FIFO was left in `aubuf`'s live mode, whose first read drops the whole backlog down to one frame to cut latency. That is right for a playback jitter buffer and wrong for a capture path, where the backlog is microphone audio the app has already handed over: the first ~4 frames of every call went missing. Live mode is now off for capture.

- **App-owned audio: ending the second of two calls took the microphone for the rest of the session** — the selected device was a bare pointer, cleared only when the closing device was the selected one. Closing the newer of two open devices therefore left the older one alive but unreachable and `push()` returned `ENODEV` forever, so call-waiting and hold/resume both silenced the surviving call. Devices are tracked in a list now: the newest still wins, and closing it falls back to whichever is still open.

- **App-owned audio did not survive a stack restart** — nothing called `bsdk_audio_external_close()` at shutdown, while `baresip_init()` re-initialises the device lists on the way back up. The driver believed it was still registered, skipped re-registering, and `baresdk_audio_use_external(true)` went on **returning 0** while the module name resolved to nothing — every call after a restart came up with no audio at all, with no error anywhere. Shutdown now closes the driver, and the lock is created once per process and never destroyed, so an app's realtime thread racing shutdown cannot land on a destroyed mutex.

- **The microphone was processed by filters the app had switched off** — `aufilt_register()` enables what it registers, and the SDK's TX filters were then enabled with `if (flag) aufilt_enable(name, true)`. A one-way enable never disables, so `bsdk_ns`, `bsdk_agc` and `bsdk_aec` all ran regardless of config: noise suppression and AGC ran with `ns`/`agc` at their default `false`, and the echo suppressor ran under `BARESDK_AEC_OFF`. Stacked on the TX path that is a −20 dB noise gate, a normaliser with a 0.1 gain floor, and a 16.5 dB duck whenever the far end has audio — worst case a caller the other end cannot hear. All five filters now take the flag directly.

- **Mobile: the software echo suppressor fought the hardware one** — Android captures through the `VOICE_COMMUNICATION` recording preset and iOS through `VoiceProcessingIO`, so the device cancels the echo before the SDK sees a sample. `aec_mode` still defaulted to `SUPPRESSOR` there (in C and in Flutter), which ducked TX by 16.5 dB every time the far end had any audio above roughly −44 dBFS and took ~0.85 s to release — removing no echo that was still present, and half-duplexing a call the hardware had already made full-duplex. The suppressor is now skipped wherever the platform driver cancels echo, including the runtime `baresdk_set_aec_mode()` path; the stock `opensles` fallback (generic preset, no platform AEC) still gets it.

- **Android: calls came up with audio in one direction only** — the platform was put into `MODE_IN_COMMUNICATION` from the call-established event, over an async method-channel hop, while the native core opened its OpenSL streams the moment media started. Android fixes a stream's routing when it is *created*, so whenever the mode landed second the playback stream stayed on the voice-call domain with nothing routing it: RTP flowed both ways, the far end heard everything, and the local user heard silence. `Account.call()` and `Call.answer()` now claim the audio session before starting media (`answer()` returns a `Future`, awaiting it is optional), and the event path is only a backstop.

- **WebSocket calls dropped after ~32 seconds once a second account had existed** — connection pinning (which keeps in-dialog requests on the WebSocket flow instead of dialling the dialog's Record-Route, an unroutable `127.0.0.1:8088` behind a reverse proxy) switched off when two accounts named different servers, and stayed off for the life of the process. One stale account — the mistyped login attempt an app forgot to destroy — was enough to lose the ACK for every later call and have the server tear each dialog down on its ~32 s timer with media still flowing. Servers are refcounted now: pinning returns as soon as one is left, and while they are ambiguous a target that cannot work (a loopback address belonging to no account) is redirected rather than dialled. Covered by `test/unit/test_ws_pin.c`.

- **Android: gaps in the audio the far end heard** — the OpenSL capture queue was primed with one buffer instead of both, so the recorder had nowhere to write between a completion callback and the next `Enqueue` and AudioFlinger dropped that slice of input, ~10 ms at a time, for the whole call.

- **Flutter example re-registered by leaking accounts** — tapping Register built a new account and dropped the old one on the floor, still retrying its own registration forever and still counting against WebSocket pinning. It destroys the previous account first.

- **iOS: init seized the audio session from CallKit** — `baresdk_init()` called `[AVAudioSession setActive:YES]` unconditionally, so merely starting the SDK (at app launch, or on a PushKit wake while CallKit was still reporting the call) took the exclusive PlayAndRecord route. Apple requires `CXProvider` to be the only activator, in `provider(_:didActivateAudioSession:)`. Activation is now controlled by `platform_audio_activate`; CallKit apps set it to `false` and get category + mode without activation.

- **Global codec list was never marshalled from Flutter** — `BareSDKConfig.audioCodecs` existed in Dart and stopped there: nothing wrote it into the native config, so a global codec preference was silently ignored (per-account `AccountConfig.audioCodecs` did work). The global list is now a first-class native field and reaches baresip in order.
- **Re-entry on a live stack was unusable** — a host that loses its own runtime while the process survives (Android headless Flutter engine destroying the Dart isolate between push wakeups) came back to a stack that was still up, and `baresdk_init()` could only answer `BARESDK_ERR_ALREADY`: the event sink still pointed at the dead runtime and the handles it held were gone. There is now a reattach path — see "Reattaching to a live stack" in `docs/api/overview.md`.

### Added

- `platform_audio_activate` in `baresdk_config_t` (default `true`) — whether `baresdk_init()` activates the platform audio session it configures. iOS only; other platforms ignore it. Flutter: `BareSDKConfig.platformAudioActivate`, normally paired with `BareSDK.start(manageAudioSession: false)`.
- `audio_codec_names[8][32]` + `audio_codec_name_count` in `baresdk_config_t` — the global counterpart to the per-account name list, so codecs with no `baresdk_codec_t` constant (`"g729"`) and any codec a loaded module registers can be selected globally. Precedence: account names → account enums → global names → global enums.
- `baresdk_is_initialized()` — is the stack up in this process.
- `baresdk_set_event_handler(cb, userdata, deliver_owned_events)` — re-point event delivery at a new consumer, or park it with `NULL`, without tearing the stack down.
- `baresdk_account_foreach()`, `baresdk_account_get_aor()`, `baresdk_account_get_reg_state()` — re-derive account handles and their state instead of creating duplicates.
- `baresdk_call_get_account()`, `baresdk_call_get_state()` — pair with the existing `baresdk_call_foreach()` to recover a call that arrived while nobody was listening (events are dropped across the gap; the call itself is not).
- Flutter: `BareSDK.start()` reattaches to a running stack by default (`reattachIfRunning: false` to opt out), `BareSDK.reattached`, `BareSDK.accounts` / `BareSDK.calls`, `BareSDK.detach()`, `Account.aor`, `Account.regState`.
- `test/reattach_test.c` — gate test for both fixes.

---

## [1.4.1] — 2026-05-14

### Fixed

- **Windows build — MSBuild tlog locking** — sub-project builds (`libre`, `baresip`, `opus`) could fail with "The requested operation cannot be performed on a file with a user-mapped section open" when Windows Defender (or any AV) held `.tlog` dependency-tracking files mapped. Fixed by passing `CMAKE_VS_GLOBALS=TrackFileAccess=false` to every `ExternalProject_Add` so MSBuild skips the tlog write step entirely.
- **Windows DLL — missing exports** — `baresdk_strerror` and `baresdk_version` were absent from `baresdk.def` because the DEF-generation regex required whitespace before the function name, which doesn't match pointer-returning signatures (`const char *fn(`). Fixed regex to accept either whitespace or `*` as the separator.

### Changed

- **Windows build consolidation** — `generate-def.ps1` and `relink-dll.ps1` are removed; their logic is now inlined into `scripts/build-windows.ps1`. One script does everything: configure → build → install → DEF → link DLL.
- **Windows build script** — auto-detects vcpkg at common locations (`C:\vcpkg`, `D:\vcpkg`, `%USERPROFILE%\vcpkg`) when `VCPKG_ROOT` is not set.

---

## [1.4.0] — 2026-05-11

### Fixed

- **RTCP ICE crash on incoming calls** — when ICE is enabled, baresip opens a second ICE session for RTCP. A failure in that session caused a crash on incoming calls. Fixed by enabling RTCP multiplexing (RFC 5761) by default: RTCP shares the RTP port, so no separate RTCP ICE session is created.

### Added

- `rtcp_mux` field in `baresdk_config_t` (default `true`). Set to `false` to opt out of RTCP-mux and revert to separate RTCP ports (not recommended when ICE is enabled).

---

## [1.3.0] — 2026-05-10

### Added

#### Push notifications (RFC 8599 + REGISTER-only headers)

- `baresdk_push_provider_t` enum — `BARESDK_PUSH_PROVIDER_NONE` / `APNS` / `APNS_SANDBOX` / `FCM`.
- Three new fields in `baresdk_account_config_t`: `push_provider`, `push_token`, `push_param`. When set, the SDK encodes RFC 8599 `pn-provider` / `pn-prid` / `pn-param` URI parameters **inside** the Contact angle brackets on every REGISTER. Self-hosted servers (Kamailio, drachtio, FreeSWITCH) read these from the registrar and use them to wake the device via APNs or FCM.
- `baresdk_account_set_push_token(acct, token)` — update the push token at runtime without re-creating the account. Re-registers immediately when safe (defers if a REGISTER/UNREGISTER transaction is in flight or a retry backoff is pending). Pass `NULL` to clear all push params.
- `baresdk_account_add_register_header(acct, name, value)` — attach a custom SIP header to REGISTER requests **only**. Does not appear on INVITE, BYE, REFER, or any other request. Use this for hosted / vendor servers (Twilio, Plivo, Asterisk PJSIP) that dispatch push via non-standard headers rather than RFC 8599.
- Flutter binding: `createAccount()` `pushProvider` / `pushToken` / `pushParam` named params; `Account.setPushToken()`; `Account.addRegisterHeader()`.
- Python binding: `PUSH_PROVIDER_NONE/APNS/APNS_SANDBOX/FCM` constants; `SDK.create_account()` `push_provider` / `push_token` / `push_param` params; `Account.set_push_token()`; `Account.add_register_header()`.
- C++ binding: same enum, new config fields, `Account::set_push_token()`, `Account::add_register_header()`, `SDK::create_account()` overload.

---

## [1.2.0] — 2026-05-10

### Added

#### Audio recording
- `baresdk_call_record_start(call, path)` — record call audio to a single mixed WAV file (PCM S16LE). Both the received (RX) and sent (TX) audio are clip-summed into one stream.
- `baresdk_call_record_stop(call)` — stop recording and finalize the WAV header with correct sizes. The file is also closed automatically if the call is destroyed.
- Recording runs independently of the PCM media tap — both can be active simultaneously on the same call.

#### Registration retry control
- `baresdk_account_set_retry_policy(acct, initial_ms, max_ms, backoff, max_attempts)` — override the retry policy for a specific account at runtime without recreating it. Overrides the global `reg_retry_*` fields in `baresdk_config_t` for that account only.
- `baresdk_account_cancel_retry(acct)` — cancel a pending retry timer and reset the attempt counter. The account stays in `FAILED` state; call `baresdk_account_register()` to restart manually.
- `baresdk_account_retry_now(acct)` — skip the current backoff delay and re-register immediately. Resets the attempt counter.

---

## [1.1.0] — 2026-05-08

### Fixed — Critical call flow bugs

#### Incoming calls
- **AOR construction** now includes port and IPv6 brackets. Previously, registrations against a non-default SIP port received `404 Not Found` on every inbound call because Asterisk/FreeSWITCH matched AOR by exact string.
- **Silent INCOMING_CALL drop under load** — the call wrapper is now registered only after a queue slot is confirmed. Previously under memory pressure the wrapper was registered but the event dropped, leaving baresip in EARLY state with the app unaware.
- **Race on fast cancel** — if the caller cancels before the event thread delivers INCOMING_CALL, the CLOSED event no longer nulls the call handle first. `baresdk_call_answer()` no longer spuriously returns `ENOENT`.
- **Incoming call destructor** — tap lock and custom header list now have a proper destructor; previously leaked on every received call.

#### Outgoing calls
- **Orphaned baresip call on alloc failure** — if wrapper `mem_alloc` fails after `ua_connect` succeeds, the SIP INVITE is now cancelled with 500. Previously the call rang on the wire while the app received `ENOMEM` with no handle.

#### Registration
- **SIP error code parsing** — `strstr(reason, "5")` replaced with proper 3-digit extraction; 415, 451, 486 are no longer misclassified as 5xx. 407 Proxy Auth now correctly maps to `BARESDK_ERR_AUTH`.

#### NAT / ICE
- **STUN + TURN co-existence** — when both `stun_server` and `turn_server` are configured, TURN takes precedence as the active ICE server. Previously configuring TURN always silently overwrote the STUN setting.

#### Memory
- **Call wrapper leak** — wrappers are freed after `BARESDK_EV_CALL_CLOSED` is delivered. Previously every completed call leaked one wrapper.
- **Stats queue bypass** — stats events now respect `ev_queue_max` and update `ev_queue_len`; previously they bypassed the limit and could grow the queue without bound on long calls.

#### Mutex safety
- **Init-time mutex lock** (UB on Windows/RTOS) — `mtx_lock` before `mtx_init` replaced with `bsdk_call_global_init()` called from `baresdk_init`.
- **Shutdown mutex order** — `mtx_destroy` now called after `mtx_unlock` in `bsdk_call_global_reset`.

#### IPv6
- **AOR formatting** — AOR now correctly produces `sip:user@[2001:db8::1]:5060;transport=tls`.
- **URI parsing** — `parse_account_uri` now skips `[…]` brackets before scanning for `:port`, preventing the host from splitting at the first colon inside an IPv6 literal.

---

### Changed — Build system
- Platform build scripts (`build-linux.sh`, `build-android.sh`, `build-macos.sh`, `build-windows.ps1`) now produce **both** the static archive and the shared library in one run. No separate shared-lib scripts required.
- Shared libraries are now **fully self-contained** — no extra packages needed at runtime:
  - **Linux** — OpenSSL, zlib, pthreads, libm, libresolv baked in as `.a`; only `libdl` / `libc` (glibc) remain dynamic.
  - **macOS** — Homebrew OpenSSL `.a` embedded; only Apple system frameworks remain.
  - **Windows** — vcpkg `libssl.lib`, `libcrypto.lib`, `zlib.lib` linked directly into the DLL.
  - **Android / iOS** — mbedTLS was already merged in; no change.
- Removed: `scripts/build_shared_linux.sh`, `scripts/build_shared_macos.sh`, `scripts/build_shared_android.sh`, `scripts/build_shared_windows.sh`.

---

### Changed — Bindings
- C / C++ compile commands simplified — no extra `-l` flags needed when linking against the self-contained shared lib.
- **One-command setup scripts** added for every language binding — each `build.sh` builds the SDK if needed and then installs/compiles the binding in one step:
  - `bash bindings/cpp/build.sh`
  - `bash bindings/python/build.sh`
  - `bash bindings/nodejs/build.sh`
  - `bash bindings/rust/build.sh`
- **Python** — `_loader.py` now auto-discovers `baresdk.so` in `dist/<platform>/<arch>/` when running from a source checkout. Manual `LD_LIBRARY_PATH` or file copy no longer required.
- **Rust** — `build.rs` auto-selects the correct `dist/` sub-directory (`linux/x86_64`, `linux/arm64`, `macos/universal`, `windows/x64`) based on the Cargo target. `BARESDK_LIB_DIR` export no longer required for native builds.
- **Node.js** — `binding.gyp` replaced fragile relative `-L` paths with absolute paths resolved at build time via `node -p`. Sets `-Wl,-rpath` so the addon finds `baresdk.so` at runtime without `LD_LIBRARY_PATH`. Supports `BARESDK_DIST_DIR` env var to override.
- **C++** — `CMakeLists.txt` auto-detects platform and architecture (`linux/x86_64`, `linux/arm64`, `macos/universal`, `windows/x64`) instead of hardcoding `linux/x86_64`.
- Quickstart docs updated for all languages to reflect one-command setup.

---

## [1.0.0] — 2025-05-08

### Added

#### Core SDK
- SIP UA with full INVITE/BYE/REGISTER flow
- Transports: UDP, TCP, TLS, WebSocket (WS), secure WebSocket (WSS)
- Media encryption: none, SDES (RFC 4568), DTLS-SRTP (RFC 5764)
- ICE / STUN / TURN NAT traversal
- Audio codecs: Opus, G.711 (PCMU/PCMA), G.722
- Audio processing: AEC, noise suppression, AGC
- DTMF via RFC 4733 RTP events
- Blind and attended call transfer (REFER)
- SIP MESSAGE (in/out of dialog)
- Presence: PUBLISH and SUBSCRIBE/NOTIFY
- MWI (message-waiting indication)
- 100rel / PRACK support (RFC 3262)
- Session timers (RFC 4028)
- Multi-account support with per-account config overrides
- Custom SIP headers (per-account and per-call)
- Audio device selection (input/output by name)
- PCM media tap (per-call RX/TX audio frame callback)

#### Observability
- RTCP media stats: loss, jitter, RTT, bandwidth
- MOS scoring: E-Model (ITU-T G.107) and simplified
- SIP trace (per-message TX/RX capture)
- SDP negotiation trace (codec + crypto result)
- Pcap capture (Wireshark-compatible output)
- Configurable log levels (error/warn/info/debug)
- Registrar warning events

#### Platforms
- Linux x86_64 (OpenSSL)
- macOS universal (x86_64 + arm64, OpenSSL)
- Windows x64 (OpenSSL via vcpkg)
- Android (arm64-v8a, armeabi-v7a, x86_64, mbedTLS)
- iOS device + simulator (mbedTLS)

#### Build system
- CMake with ExternalProject (re + baresip + optional mbedTLS)
- Static archive merge (`baresdk.a`) via libtool / lib.exe / ar MRI
- `BARESDK_SHARED` option for shared library output (`.so` / `.dylib` / `.dll`)
- Platform build scripts: `build-linux.sh`, `build-macos.sh`, `build-ios.sh`, `build-android.sh`, `build-windows.ps1`

#### C++ binding
- Header-only RAII wrapper (`bindings/cpp/baresdk.hpp`)
- `SDK`, `Account`, `Call` classes with automatic resource cleanup

#### Python binding
- cffi-based wrapper (`bindings/python/`)
- `SDK`, `Account`, `Call` Pythonic classes
- Event delivery via `queue.SimpleQueue` generator pattern
- Clean header preprocessing via `generate_clean_header.sh`

#### Rust binding
- `baresdk-sys` crate (bindgen auto-generated raw FFI)
- `baresdk` crate (safe wrapper with `Result` error handling)
- Event delivery via `std::sync::mpsc` channel

#### Node.js binding
- N-API C++ addon (`bindings/nodejs/`)
- `SDK`, `Account`, `Call` JavaScript classes
- TypeScript declarations (`.d.ts`)
- `node-gyp` build configuration

#### Flutter / Dart binding
- dart:ffi + ffigen wrapper (`bindings/flutter/`)
- `BareSDK`, `Account`, `Call` Dart classes
- Event delivery via `Stream<BareSDKEvent>` with `StreamController`
- Multi-platform library loading (Android, iOS, macOS, Windows, Linux)

#### Documentation
- API reference: overview, config, accounts, calls, media, events, observability
- Quickstart guides: C/C++, Python, Rust, Node.js, Flutter
- How-to guides: NAT traversal, TLS/WSS, multi-account, WebRTC browser interop, debugging
- Configuration examples (`accounts_example.json`)
