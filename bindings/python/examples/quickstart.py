"""
quickstart.py — register an account and make or receive one call.

Usage:
    python quickstart.py account.json                          # receive mode
    python quickstart.py account.json bob@pbx.example.com      # dial
    python quickstart.py alice@pbx.example.com secret          # legacy CLI mode (receive)
    python quickstart.py alice@pbx.example.com secret bob@...  # legacy CLI mode (dial)

JSON account config example (account.json):
{
  "enabled":      true,
  "uri":          "120@pbx.example.com",
  "password":     "secret",
  "display_name": "Alice",
  "auth_user":    null,

  "transport":    "wss",          // "udp" | "tcp" | "tls" | "ws" | "wss"
  "server_url":   "wss://pbx.example.com:443/",
  "server_host":  null,
  "server_port":  0,

  "media_enc":    "dtls_srtp",    // "none" | "sdes" | "dtls_srtp"
  "ice_enabled":  true,
  "stun_server":  "stun:stun.l.google.com:19302",
  "turn_server":  null,
  "turn_user":    null,
  "turn_pass":    null,
  "verify_tls":   false,

  "extra_headers": {"X-Tenant-Id": "42"},
  "audio_codecs": ["opus"],
  "rel100":       "enabled"       // "disabled" | "enabled" | "required"
}
"""

import json
import math
import sys
import threading
from typing import Optional

from baresdk import SDK, Call, create_account, register, dial, hangup, answer


def _config_from_json(path_or_str: str):
    """Parse JSON config. Returns (uri, password, enabled, kwargs)."""
    j = json.loads(path_or_str) if path_or_str.startswith("{") else json.load(open(path_or_str))

    uri      = j.pop("uri", "")
    password = j.pop("password", "")
    enabled  = j.pop("enabled", True)

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


def print_stats(s):
    method  = "E-model" if s.mos_method == 0 else "simplified"
    spk     = f"{s.audio_level_dbov:.4f} dBov" if not math.isnan(s.audio_level_dbov) else "n/a"
    mic     = f"{s.mic_level_dbov:.4f} dBov"   if not math.isnan(s.mic_level_dbov)   else "n/a"
    print(
        f"┌─ Media Stats ─────────────────────────────────\n"
        f"│  Codec     : {s.codec_name}  {s.codec_clock_rate // 1000} kHz"
        f"  ch={s.codec_channels}  PT={s.payload_type}\n"
        f"│  Remote    : {s.remote_addr}"
        f"  SSRC rx={s.ssrc_rx}  tx={s.ssrc_tx}\n"
        f"│  Packets   : tx={s.packets_sent}  rx={s.packets_received}"
        f"  lost_tx={s.packets_lost} ({s.loss_pct:.1f}%)"
        f"  lost_rx={s.packets_lost_rx} ({s.loss_pct_rx:.1f}%)\n"
        f"│  Bandwidth : tx={s.bandwidth_kbps_tx} kbps  rx={s.bandwidth_kbps_rx} kbps"
        f"  (avg tx={s.avg_bandwidth_kbps_tx}  rx={s.avg_bandwidth_kbps_rx})\n"
        f"│  Delay     : RTT={s.rtt_ms:.1f} ms"
        f"  jitter={s.jitter_ms:.1f} ms"
        f"  tx_jitter={s.tx_jitter_ms:.1f} ms\n"
        f"│  Jitter buf: depth={s.jitter_buffer_ms} ms"
        f"  load={s.jitter_buffer_load}"
        f"  late={s.late_packets}"
        f"  discarded={s.discarded_packets}\n"
        f"│  MOS ({method}): LQ={s.mos_lq:.3f}  CQ={s.mos_cq:.3f}\n"
        f"│  Speaker   : {spk}\n"
        f"│  Mic       : {mic}\n"
        f"└───────────────────────────────────────────────"
    )


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

    with SDK(log_level=0, stats_interval_ms=5000) as sdk:
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
                    print(f"Registration failed: {ev.error_str or '?'}")
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

                if ev.state == "established" and callee:
                    print("Call active. Press 'h' + Enter to hang up.")

                    def _do_hangup():
                        with call_lock:
                            if active_call:
                                hangup(active_call)

                    threading.Thread(
                        target=stdin_watch, args=("h", _do_hangup), daemon=True
                    ).start()

                if ev.state in ("ended", "failed", "cancelled"):
                    break

            elif ev.type == "media_stats":
                print_stats(ev)

            elif ev.type == "sip_trace":
                print(f"{'>>>' if ev.direction == 'tx' else '<<<'}\n{ev.raw_message}\n---")

            elif ev.type == "log":
                print(f"[sdk] {ev.message}")

        account.destroy()

    print("Done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
