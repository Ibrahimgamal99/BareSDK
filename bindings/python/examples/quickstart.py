"""
quickstart.py — register an account from a JSON config file and
                  make or receive one call.

Prerequisites:
    1. Build the shared library (Linux example):
       ./scripts/build-linux.sh
    2. Install the Python package:
       pip install bindings/python
    3. Set the library path (Linux):
       export LD_LIBRARY_PATH=dist/linux/x86_64:$LD_LIBRARY_PATH

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
  "server_host":  null,           // optional: override SIP domain for routing
  "server_port":  0,              // optional: 0 = use default for transport

  "media_enc":    "dtls_srtp",    // "none" | "sdes" | "dtls_srtp"
  "ice_enabled":  true,
  "stun_server":  "stun:stun.l.google.com:19302",
  "turn_server":  null,           // e.g. "turn:turn.example.com:3478"
  "turn_user":    null,
  "turn_pass":    null,
  "verify_tls":   false,

  "extra_headers": {              // optional SIP headers added to every request
    "X-Tenant-Id": "42"
  },
  "audio_codecs": ["opus"],       // ["opus", "ulaw", "alaw", "g722"] or "opus"
  "rel100":       "enabled"       // "disabled" | "enabled" | "required"
}
"""

import json
import math
import os
import sys
import threading
from typing import Optional, Dict, List, Any

from baresdk import (
    SDK, Account, Call, strerror,
    RegStateEvent, IncomingCallEvent, CallStateEvent, CallDtmfEvent,
    SdpNegotiationEvent, SipTraceEvent, MediaStatsEvent, LogEvent,
    RegistrarWarningEvent, TransferRequestEvent, MwiEvent,
    MessageEvent, PresenceStateEvent, QualityAlertEvent,
    REG_REGISTERED, REG_FAILED,
    CALL_CALLING, CALL_RINGING, CALL_ESTABLISHED, CALL_HELD,
    CALL_ENDED, CALL_CANCELLED, CALL_FAILED,
    TRANSPORT_UDP, TRANSPORT_TCP, TRANSPORT_TLS, TRANSPORT_WS, TRANSPORT_WSS,
    MEDIA_ENC_NONE, MEDIA_ENC_SDES, MEDIA_ENC_DTLS_SRTP,
    CODEC_OPUS, CODEC_PCMU, CODEC_PCMA, CODEC_G722,
)


STATE_NAMES = {
    CALL_CALLING:     "CALLING",
    CALL_RINGING:     "RINGING",
    CALL_ESTABLISHED: "ESTABLISHED",
    CALL_HELD:        "HELD",
    CALL_ENDED:       "ENDED",
    CALL_CANCELLED:   "CANCELLED",
    CALL_FAILED:      "FAILED",
}


class AccountConfig:
    """Account configuration that owns all string data."""

    def __init__(self):
        self.enabled: bool = True
        self.uri: str = ""
        self.password: str = ""
        self.display_name: str = ""
        self.auth_user: str = ""

        self.transport: int = TRANSPORT_WSS
        self.server_url: str = ""
        self.server_host: str = ""
        self.server_port: int = 0

        self.media_enc: int = MEDIA_ENC_DTLS_SRTP
        self.ice_enabled: bool = True
        self.stun_server: str = ""
        self.turn_server: str = ""
        self.turn_user: str = ""
        self.turn_pass: str = ""
        self.verify_tls: bool = False

        self.extra_headers: Dict[str, str] = {}
        self.audio_codecs: List[str] = []
        self.rel100: int = 0  # 0=disabled, 1=enabled, 2=required

    def from_cli(self, sip_uri: str, password: str):
        """Legacy mode: construct from CLI args."""
        self.uri = sip_uri
        self.password = password

        # derive server_url from URI host
        u = sip_uri
        if u.startswith("sip:"):
            u = u[4:]

        at_pos = u.find('@')
        if at_pos != -1:
            host = u[at_pos + 1:]
            self.display_name = u[:at_pos]
        else:
            host = u
            self.display_name = u

        colon_pos = host.find(':')
        if colon_pos != -1:
            host = host[:colon_pos]

        self.server_url = f"wss://{host}:443/"
        self.transport = TRANSPORT_WSS
        self.media_enc = MEDIA_ENC_DTLS_SRTP
        self.ice_enabled = True
        self.stun_server = "stun:stun.l.google.com:19302"
        self.verify_tls = False

    def from_json(self, j: Dict[str, Any]):
        """Construct from JSON dict."""
        self.enabled = j.get("enabled", True)
        self.uri = j.get("uri", "")
        self.password = j.get("password", "")
        self.display_name = j.get("display_name", "")
        self.auth_user = j.get("auth_user", "")

        # transport
        t = j.get("transport", "wss")
        if t == "udp":
            self.transport = TRANSPORT_UDP
        elif t == "tcp":
            self.transport = TRANSPORT_TCP
        elif t == "tls":
            self.transport = TRANSPORT_TLS
        elif t == "ws":
            self.transport = TRANSPORT_WS
        elif t == "wss":
            self.transport = TRANSPORT_WSS
        else:
            raise ValueError(f"unknown transport: {t}")

        self.server_url = j.get("server_url", "")
        self.server_host = j.get("server_host", "")
        self.server_port = j.get("server_port", 0)

        # media_enc
        m = j.get("media_enc", "dtls_srtp")
        if m == "none":
            self.media_enc = MEDIA_ENC_NONE
        elif m == "sdes":
            self.media_enc = MEDIA_ENC_SDES
        elif m == "dtls_srtp":
            self.media_enc = MEDIA_ENC_DTLS_SRTP
        else:
            raise ValueError(f"unknown media_enc: {m}")

        self.ice_enabled = j.get("ice_enabled", True)
        self.stun_server = j.get("stun_server", "")
        self.turn_server = j.get("turn_server", "")
        self.turn_user = j.get("turn_user", "")
        self.turn_pass = j.get("turn_pass", "")
        self.verify_tls = j.get("verify_tls", False)

        # rel100
        r = j.get("rel100", "disabled")
        if r == "disabled":
            self.rel100 = 0
        elif r == "enabled":
            self.rel100 = 1
        elif r == "required":
            self.rel100 = 2
        else:
            raise ValueError(f"unknown rel100 value: {r}")

        # extra_headers
        self.extra_headers = j.get("extra_headers", {})

        # audio_codecs
        ac = j.get("audio_codecs")
        if isinstance(ac, list):
            self.audio_codecs = [str(c) for c in ac if c]
        elif isinstance(ac, str):
            self.audio_codecs = [ac] if ac else []

    def dump(self):
        """Print configuration to stdout."""
        transport_names = {
            TRANSPORT_UDP: "udp",
            TRANSPORT_TCP: "tcp",
            TRANSPORT_TLS: "tls",
            TRANSPORT_WS:  "ws",
            TRANSPORT_WSS: "wss",
        }
        enc_names = {
            MEDIA_ENC_NONE:      "none",
            MEDIA_ENC_SDES:      "sdes",
            MEDIA_ENC_DTLS_SRTP: "dtls_srtp",
        }

        print("Account config:")
        print(f"  uri          : {self.uri}")
        print(f"  display_name : {self.display_name}")
        print(f"  auth_user    : {self.auth_user or '(from uri)'}")
        print(f"  transport    : {transport_names.get(self.transport, '?')}")
        print(f"  server_url   : {self.server_url or '(none)'}")
        print(f"  server_host  : {self.server_host or '(none)'}")
        print(f"  server_port  : {self.server_port}")
        print(f"  media_enc    : {enc_names.get(self.media_enc, '?')}")
        print(f"  ice_enabled  : {self.ice_enabled}")
        print(f"  stun_server  : {self.stun_server or '(none)'}")
        print(f"  turn_server  : {self.turn_server or '(none)'}")
        print(f"  verify_tls   : {self.verify_tls}")

        if self.audio_codecs:
            print(f"  audio_codecs : {', '.join(self.audio_codecs)}")
        else:
            print("  audio_codecs : (global SDK list)")

        if self.extra_headers:
            print("  extra_headers:")
            for k, v in self.extra_headers.items():
                print(f"    {k}: {v}")


def print_devices(sdk: SDK):
    """Print available audio devices."""
    inputs = sdk.list_input_devices()
    outputs = sdk.list_output_devices()

    if inputs:
        print(f"Input devices ({len(inputs)}):")
        for i, d in enumerate(inputs):
            dflt = "  *default*" if d["is_default"] else ""
            print(f"  [{i}] {d['name']}{dflt}")

    if outputs:
        print(f"Output devices ({len(outputs)}):")
        for i, d in enumerate(outputs):
            dflt = "  *default*" if d["is_default"] else ""
            print(f"  [{i}] {d['name']}{dflt}")


def print_stats(s: MediaStatsEvent):
    """Print media statistics in a formatted box."""
    method = "E-model" if s.mos_method == 0 else "simplified"
    level = f"{s.audio_level_dbov:.1f} dBov" if not math.isnan(s.audio_level_dbov) else "n/a"

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
        f"│  Level     : {level}\n"
        f"└───────────────────────────────────────────────"
    )


def looks_like_json_path(arg: str) -> bool:
    """Detect if argument is a JSON file path or raw JSON string."""
    if len(arg) > 5 and arg.endswith(".json"):
        return True
    if arg.startswith("{"):
        return True
    return False


def main():
    if len(sys.argv) < 2:
        print(
            "Usage:\n"
            "  quickstart.py account.json [callee-uri]\n"
            "  quickstart.py <sip-uri> <password> [<callee-uri>]"
        )
        return 1

    cfg = AccountConfig()
    callee = None

    if looks_like_json_path(sys.argv[1]):
        # JSON mode
        try:
            arg1 = sys.argv[1]
            if arg1.startswith("{"):
                j = json.loads(arg1)
            else:
                with open(arg1) as f:
                    j = json.load(f)
            cfg.from_json(j)
        except Exception as e:
            print(f"Failed to load account config: {e}")
            return 1

        if len(sys.argv) >= 3:
            callee = sys.argv[2]
    else:
        # Legacy CLI mode
        if len(sys.argv) < 3:
            print(f"usage: {sys.argv[0]} <sip-uri> <password> [<callee-uri>]")
            return 1

        cfg.from_cli(sys.argv[1], sys.argv[2])

        if len(sys.argv) >= 4:
            callee = sys.argv[3]

    if not cfg.enabled:
        print("Account is disabled in config. Exiting.")
        return 0

    if not cfg.uri:
        print("No URI in config.")
        return 1

    cfg.dump()

    # Shared state
    call_done = threading.Event()
    active_call: Optional[Call] = None
    call_lock = threading.Lock()

    def stdin_watch(prompt_char: str, action):
        """Background thread: read lines from stdin, call action() on prompt_char."""
        for line in sys.stdin:
            if line.strip().lower() == prompt_char:
                action()
                break

    # SDK setup
    with SDK(
        log_level=0,
        stats_interval_ms=5000,
        trace_sip=False,
        prefer_ipv6=False,
        verify_server=False
    ) as sdk:
        # Set global codec list
        sdk.set_aec(True)
        sdk.set_ns(True)
        sdk.set_agc(True)

        # Create account with configuration
        kwargs = {}
        if cfg.display_name:
            kwargs["display_name"] = cfg.display_name
        if cfg.auth_user:
            kwargs["auth_user"] = cfg.auth_user
        if cfg.server_url:
            kwargs["server_url"] = cfg.server_url
        if cfg.server_host:
            kwargs["server_host"] = cfg.server_host
        if cfg.server_port > 0:
            kwargs["server_port"] = cfg.server_port
        if cfg.media_enc != MEDIA_ENC_DTLS_SRTP:
            kwargs["media_enc"] = cfg.media_enc
        if not cfg.ice_enabled:
            kwargs["ice_enabled"] = False
        if cfg.stun_server:
            kwargs["stun_server"] = cfg.stun_server
        if cfg.turn_server:
            kwargs["turn_server"] = cfg.turn_server
        if cfg.turn_user:
            kwargs["turn_user"] = cfg.turn_user
        if cfg.turn_pass:
            kwargs["turn_pass"] = cfg.turn_pass
        if cfg.verify_tls:
            kwargs["verify_tls"] = True
        if cfg.audio_codecs:
            kwargs["audio_codecs"] = cfg.audio_codecs

        account = sdk.create_account(
            cfg.uri,
            cfg.password,
            transport=cfg.transport,
            **kwargs
        )

        # Add extra headers
        for k, v in cfg.extra_headers.items():
            account.add_header(k, v)

        account.register()

        registered = False

        for ev in account.events():
            if isinstance(ev, RegStateEvent):
                if ev.state == REG_REGISTERED:
                    print("Registered OK.")
                    registered = True
                    print_devices(sdk)

                    if callee:
                        # Dial mode
                        callee_uri = callee
                        if not callee_uri.startswith("sip:"):
                            callee_uri = "sip:" + callee_uri

                        if "@" not in callee_uri:
                            domain = cfg.uri
                            if domain.startswith("sip:"):
                                domain = domain[4:]
                            at_pos = domain.find("@")
                            if at_pos != -1:
                                domain = domain[at_pos + 1:]
                            callee_uri = callee_uri + "@" + domain

                        print(f"Dialling {callee_uri} ...")
                        with call_lock:
                            active_call = account.call(callee_uri)
                    else:
                        # Receive mode
                        print("Waiting for incoming call (30 s)...")

                elif ev.state == REG_FAILED:
                    print(f"Registration failed: {ev.error_str or '?'}")
                    break

            elif isinstance(ev, IncomingCallEvent):
                print(f"\n=== Incoming call from {ev.from_uri} ===")
                print("Press 'a' + Enter to answer, 'r' + Enter to reject")

                with call_lock:
                    active_call = ev.call

                def answer_or_reject():
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

                threading.Thread(target=answer_or_reject, daemon=True).start()

            elif isinstance(ev, CallStateEvent):
                sname = STATE_NAMES.get(ev.state, str(ev.state))
                msg = f"Call state: {sname} ({ev.state})"
                if ev.reason:
                    msg += f"  reason={ev.reason!r}"
                if ev.error != 0:
                    msg += f"  error={ev.error}"
                print(msg)

                if ev.state == CALL_ESTABLISHED and callee:
                    print("Call active. Press 'h' + Enter to hang up.")

                    def hangup():
                        with call_lock:
                            if active_call:
                                active_call.hangup()

                    threading.Thread(
                        target=stdin_watch, args=("h", hangup), daemon=True
                    ).start()

                if ev.state in (CALL_ENDED, CALL_FAILED, CALL_CANCELLED):
                    call_done.set()
                    break

            elif isinstance(ev, MediaStatsEvent):
                print_stats(ev)

            elif isinstance(ev, SipTraceEvent):
                dir_str = ">>>" if ev.direction == 1 else "<<<"
                print(f"{dir_str}\n{ev.raw_message}\n---")

            elif isinstance(ev, LogEvent):
                print(f"[sdk] {ev.message}")

        account.destroy()

    print("Done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
