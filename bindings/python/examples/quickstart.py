"""
quickstart.py — register an account and make or receive one call.

Usage:
    python quickstart.py account.json                          # receive mode
    python quickstart.py account.json bob@pbx.example.com      # dial
    python quickstart.py alice@pbx.example.com secret          # legacy CLI mode (receive)
    python quickstart.py alice@pbx.example.com secret bob@...  # legacy CLI mode (dial)

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

server_url, outbound_proxy, server_host, server_port, auth_user are all
auto-derived from uri + transport. Port defaults: udp/tcp=5060, tls=5061,
ws=8088, wss=8089. Include a port in the uri to override: "120@host:443".

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
import queue
import sys
import threading
from typing import Optional

from baresdk import SDK, Call, CallStats, create_account, register, dial, hangup, answer, strerror

# How often to refresh media stats during a call.
# Change to any float in seconds — independent of the SDK stats_interval_ms.
STATS_INTERVAL_S = 5.0


def _config_from_json(path_or_str: str):
    """Parse JSON config. Returns (uri, password, enabled, kwargs)."""
    j = json.loads(path_or_str) if path_or_str.startswith("{") else json.load(open(path_or_str))

    uri      = j.pop("uri", "")
    password = j.pop("password", "")
    enabled  = j.pop("enabled", True)

    # audio_codec (singular) alias — use when audio_codecs is absent
    if "audio_codec" in j and "audio_codecs" not in j:
        v = j.pop("audio_codec")
        if v:
            j["audio_codecs"] = [v] if isinstance(v, str) else v
    elif "audio_codec" in j:
        j.pop("audio_codec")

    # audio_codecs: normalize string → list
    ac = j.get("audio_codecs")
    if isinstance(ac, str):
        j["audio_codecs"] = [ac] if ac else []

    # drop null values — let SDK use its defaults
    kwargs = {k: v for k, v in j.items() if v is not None}
    return uri, password, enabled, kwargs


def _config_from_cli(sip_uri: str, password: str):
    """Derive minimal config from bare CLI args."""
    u    = sip_uri[4:] if sip_uri.startswith("sip:") else sip_uri
    at   = u.find("@")
    host = u[at + 1:] if at != -1 else u
    host = host[:host.find(":")] if ":" in host else host
    return sip_uri, password, True, {
        "transport":   "wss",
        "server_url":  f"wss://{host}:443/",
        "media_enc":   "dtls_srtp",
        "ice_enabled": True,
        "stun_server": "stun:stun.l.google.com:19302",
        "verify_tls":  False,
    }


def print_devices(sdk: SDK):
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
            uri, password, enabled, kwargs = _config_from_json(arg1)
        except Exception as e:
            print(f"Failed to load config: {e}")
            return 1
        if len(sys.argv) >= 3:
            callee = sys.argv[2]
    else:
        if len(sys.argv) < 3:
            print(f"usage: {sys.argv[0]} <sip-uri> <password> [<callee-uri>]")
            return 1
        uri, password, enabled, kwargs = _config_from_cli(arg1, sys.argv[2])
        if len(sys.argv) >= 4:
            callee = sys.argv[3]

    if not enabled:
        print("Account is disabled in config.")
        return 0
    if not uri:
        print("No URI in config.")
        return 1

    active_call: Optional[Call] = None
    call_lock = threading.Lock()

    def stdin_watch(char: str, action):
        for line in sys.stdin:
            if line.strip().lower() == char:
                action()
                break

    verify_tls = kwargs.pop("verify_tls", True)
    sdk_kwargs = {"log_level": 0, "stats_interval_ms": 5000}
    if not verify_tls:
        sdk_kwargs["verify_server"] = False

    with SDK(**sdk_kwargs) as sdk:
        sdk.set_aec(True)
        sdk.set_ns(True)
        sdk.set_agc(True)

        account = create_account(sdk, uri, password, **kwargs)
        register(account)

        for ev in account.events():
            if ev.type == "reg_state":
                if ev.state == "registered":
                    print("Registered OK.")
                    print_devices(sdk)
                    if callee:
                        print(f"Dialling {callee} ...")
                        with call_lock:
                            active_call = dial(account, callee)
                    else:
                        print("Waiting for incoming call...")
                elif ev.state == "failed":
                    detail = ev.error_str or strerror(ev.error)
                    print(f"Registration failed: {detail}")
                    break

            elif ev.type == "incoming_call":
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
                                        answer(active_call)
                                    except Exception as e:
                                        print(f"answer failed: {e}")
                            break
                        elif ch == "r":
                            with call_lock:
                                if active_call:
                                    hangup(active_call)
                            break

                threading.Thread(target=_answer_or_reject, daemon=True).start()

            elif ev.type == "call_state":
                msg = f"Call state: {ev.state}"
                if ev.reason:
                    msg += f"  reason={ev.reason!r}"
                if ev.error:
                    msg += f"  error={ev.error}"
                print(msg)

                if ev.state == "established":
                    print(f"Call active (stats every {STATS_INTERVAL_S}s). "
                          f"Keys: h=hangup  o=hold  r=resume  m=mute  u=unmute  t=transfer  s=stats-now")

                    stats_trigger = queue.Queue()

                    def _stats_printer(call=active_call):
                        for stats in account.stats_stream(
                                call=call,
                                interval=STATS_INTERVAL_S,
                                trigger=stats_trigger):
                            stats.print()
                            if stats.is_final:
                                break

                    threading.Thread(target=_stats_printer, daemon=True).start()

                    def _interactive():
                        for line in sys.stdin:
                            ch = line.strip().lower()
                            with call_lock:
                                c = active_call
                            if not c:
                                break
                            if ch == "h":
                                hangup(c); break
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
                                stats_trigger.put(1)  # immediate refresh
                            elif ch == "t":
                                dest = input("Transfer to URI: ").strip()
                                if dest:
                                    try: c.transfer(dest); print("Transfer sent.")
                                    except Exception as e: print(f"transfer: {e}")

                    threading.Thread(target=_interactive, daemon=True).start()

                if ev.state in ("ended", "failed", "cancelled"):
                    break

            elif ev.type == "transfer_request":
                kind = "attended" if ev.has_replaces else "blind"
                print(f"=== Transfer request ({kind}): REFER to {ev.refer_to_uri}")
                print("    (to follow: hang up current call and dial the refer_to_uri)")

            elif ev.type == "sip_trace":
                print(f"{'>>>' if ev.direction == 'tx' else '<<<'}\n{ev.raw_message}\n---")

            elif ev.type == "log":
                print(f"[sdk] {ev.message}")

        account.destroy()

    print("Done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
