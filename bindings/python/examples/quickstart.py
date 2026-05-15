"""
quickstart.py — register an account and make or receive one call.

Usage:
    python quickstart.py account.json                          # receive mode
    python quickstart.py account.json bob@pbx.example.com      # dial
    python quickstart.py alice@pbx.example.com secret          # legacy CLI mode (receive)
    python quickstart.py alice@pbx.example.com secret bob@...  # legacy CLI mode (dial)

Debug:
    BARESDK_DEBUG_INIT=1 python quickstart.py account.json     # verbose init/shutdown trace

Minimal JSON account config (account.json):
{
  "enabled":      true,
  "uri":          "120@pbx.example.com",
  "password":     "secret",
  "display_name": "Extension 120",
  "transport":    "wss",
  "media_enc":    "dtls_srtp",
  "ice_enabled":  true,
  "rtcp_mux":     true,
  "stun_server":  "stun:stun.l.google.com:19302",
  "verify_tls":   false,
  "audio_codec":  "opus"
}

Optional overrides (add when needed):
  "server_url":     "wss://pbx.example.com:443/ws"
  "outbound_proxy": "sip:proxy.example.com:5060;transport=udp"
  "auth_user":      "alice"
  "extra_headers":  {"X-Tenant-Id": "42"}
  "audio_codecs":   ["opus", "pcmu"]
  "rel100":         "enabled"

Media stats:
  Stats are printed every STATS_INTERVAL_S seconds while a call is active.
  Change STATS_INTERVAL_S below to any value (e.g. 1, 2, 5).
  Press 's' during a call for an immediate refresh.
"""

import json
import sys
import threading
from typing import Optional

import baresdk as sdk

STATS_INTERVAL_S = 5.0


def _config_from_json(path_or_str: str):
    j = json.loads(path_or_str) if path_or_str.startswith("{") else json.load(open(path_or_str))

    uri      = j.pop("uri", "")
    password = j.pop("password", "")
    enabled  = j.pop("enabled", True)

    # audio_codec (singular) alias
    if "audio_codec" in j and "audio_codecs" not in j:
        v = j.pop("audio_codec")
        if v:
            j["audio_codecs"] = [v] if isinstance(v, str) else v
    elif "audio_codec" in j:
        j.pop("audio_codec")

    if isinstance(j.get("audio_codecs"), str):
        j["audio_codecs"] = [j["audio_codecs"]] if j["audio_codecs"] else []

    # verify_tls → global SDK setting
    verify_tls = j.pop("verify_tls", True)

    sdk_kwargs = {}
    if not verify_tls:
        sdk_kwargs["verify_server"] = False

    account_kwargs = {k: v for k, v in j.items() if v is not None}
    return uri, password, enabled, account_kwargs, sdk_kwargs


def _config_from_cli(sip_uri: str, password: str):
    u    = sip_uri[4:] if sip_uri.startswith("sip:") else sip_uri
    at   = u.find("@")
    host = u[at + 1:] if at != -1 else u
    host = host[:host.find(":")] if ":" in host else host
    account_kwargs = {
        "transport":   "wss",
        "server_url":  f"wss://{host}:443/",
        "media_enc":   "dtls_srtp",
        "ice_enabled": True,
        "stun_server": "stun:stun.l.google.com:19302",
    }
    sdk_kwargs = {"verify_server": False}
    return sip_uri, password, True, account_kwargs, sdk_kwargs


def print_devices():
    for label, fn in (("Input", sdk.list_input_devices), ("Output", sdk.list_output_devices)):
        devices = fn()
        if devices:
            print(f"{label} devices ({len(devices)}):")
            for i, d in enumerate(devices):
                print(f"  [{i}] {d['name']}{'  *default*' if d['is_default'] else ''}")


def main():
    if len(sys.argv) < 2:
        print("Usage:\n"
              "  quickstart.py account.json [callee-uri]\n"
              "  quickstart.py <sip-uri> <password> [<callee-uri>]")
        return 1

    arg1   = sys.argv[1]
    callee: Optional[str] = None

    if arg1.endswith(".json") or arg1.startswith("{"):
        try:
            uri, password, enabled, account_kwargs, sdk_kwargs = _config_from_json(arg1)
        except Exception as e:
            print(f"Failed to load config: {e}")
            return 1
        if len(sys.argv) >= 3:
            callee = sys.argv[2]
    else:
        if len(sys.argv) < 3:
            print(f"usage: {sys.argv[0]} <sip-uri> <password> [<callee-uri>]")
            return 1
        uri, password, enabled, account_kwargs, sdk_kwargs = _config_from_cli(arg1, sys.argv[2])
        if len(sys.argv) >= 4:
            callee = sys.argv[3]

    if not enabled:
        print("Account is disabled in config.")
        return 0
    if not uri:
        print("No URI in config.")
        return 1

    # Global SDK config
    sdk.configure(log_level=0, stats_interval_ms=5000, **sdk_kwargs)
    sdk.set_aec(True)
    sdk.set_ns(True)
    sdk.set_agc(True)

    acc = sdk.create_account(uri, password, **account_kwargs)
    acc.register()

    active_call: Optional[sdk.Call] = None
    call_lock = threading.Lock()

    # ── Event handlers ────────────────────────────────────────────────────────

    @sdk.on("registered")
    def _(ev):
        nonlocal active_call
        print("Registered OK.")
        print_devices()
        if callee:
            print(f"Dialling {callee} ...")
            with call_lock:
                active_call = sdk.call(callee)
        else:
            print("Waiting for incoming call...")

    @sdk.on("reg_failed")
    def _(ev):
        detail = ev.error_str or sdk.strerror(ev.error)
        print(f"Registration failed: {detail}")
        sdk.stop()

    @sdk.on("incoming_call")
    def _(ev):
        nonlocal active_call
        print(f"\n=== Incoming call from {ev.from_uri} ===")
        print("Press 'a' + Enter to answer, 'r' + Enter to reject")
        with call_lock:
            active_call = ev.call

        def _answer_or_reject():
            for line in sys.stdin:
                ch = line.strip().lower()
                if ch == "a":
                    with call_lock:
                        if active_call:
                            try:
                                active_call.answer()
                            except Exception as e:
                                print(f"answer failed: {e}")
                    break
                elif ch == "r":
                    with call_lock:
                        if active_call:
                            active_call.hangup()
                    break

        threading.Thread(target=_answer_or_reject, daemon=True).start()

    @sdk.on("call_state")
    def _(ev):
        nonlocal active_call
        msg = f"Call state: {ev.state}"
        if ev.reason:
            msg += f"  reason={ev.reason!r}"
        if ev.error:
            msg += f"  error={ev.error}"
        print(msg)

        if ev.state == "established":
            print(f"Call active (stats every {STATS_INTERVAL_S}s). "
                  f"Keys: h=hangup  o=hold  r=resume  m=mute  u=unmute  t=transfer  s=stats-now")
            with call_lock:
                c = active_call
            if c:
                c.poll_stats(interval=STATS_INTERVAL_S, on_update=lambda s: s.print())

            def _interactive():
                for line in sys.stdin:
                    ch = line.strip().lower()
                    with call_lock:
                        c = active_call
                    if not c:
                        break
                    if ch == "h":
                        c.hangup(); break
                    elif ch == "o":
                        try: c.hold();         print("On hold.")
                        except Exception as e: print(f"hold: {e}")
                    elif ch == "r":
                        try: c.resume();       print("Resumed.")
                        except Exception as e: print(f"resume: {e}")
                    elif ch == "m":
                        try: c.mute(True);     print("Muted.")
                        except Exception as e: print(f"mute: {e}")
                    elif ch == "u":
                        try: c.mute(False);    print("Unmuted.")
                        except Exception as e: print(f"unmute: {e}")
                    elif ch == "s":
                        c.stats().print()
                    elif ch == "t":
                        dest = input("Transfer to URI: ").strip()
                        if dest:
                            try: c.transfer(dest); print("Transfer sent.")
                            except Exception as e: print(f"transfer: {e}")

            threading.Thread(target=_interactive, daemon=True).start()

        if ev.state in ("ended", "failed", "cancelled"):
            with call_lock:
                active_call = None
            sdk.stop()

    @sdk.on("transfer_request")
    def _(ev):
        kind = "attended" if ev.has_replaces else "blind"
        print(f"=== Transfer request ({kind}): REFER to {ev.refer_to_uri}")
        print("    (to follow: hang up current call and dial the refer_to_uri)")

    @sdk.on("sip_trace")
    def _(ev):
        print(f"{'>>>' if ev.direction == 'tx' else '<<<'}\n{ev.raw_message}\n---")

    @sdk.on("log")
    def _(ev):
        print(f"[sdk] {ev.message}")

    # ── Run ───────────────────────────────────────────────────────────────────

    sdk.run()
    print("Done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
