"""
quickstart.py — register an account and make or receive one call.

Prerequisites:
    1. Build the shared library (Linux example):
       ./scripts/build-linux.sh
    2. Install the Python package:
       pip install bindings/python
    3. Set the library path (Linux):
       export LD_LIBRARY_PATH=dist/linux/x86_64:$LD_LIBRARY_PATH

Usage:
    python quickstart.py alice@pbx.example.com secret              # receive mode
    python quickstart.py alice@pbx.example.com secret bob@pbx.example.com  # dial
"""

import math
import sys
import time
import threading

from baresdk import (
    SDK,
    RegStateEvent, IncomingCallEvent, CallStateEvent, MediaStatsEvent,
    REG_REGISTERED, CALL_ESTABLISHED, CALL_ENDED, CALL_FAILED, CALL_CANCELLED,
    TRANSPORT_UDP,
)

if len(sys.argv) < 3:
    print("usage: quickstart.py <sip-uri> <password> [<callee-uri>]")
    sys.exit(1)

sip_uri  = sys.argv[1]
password = sys.argv[2]
callee   = sys.argv[3] if len(sys.argv) >= 4 else None

if callee and not callee.startswith("sip:"):
    callee = "sip:" + callee


def print_devices(sdk):
    inputs  = sdk.list_input_devices()
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
    method = "E-model" if s.mos_method == 0 else "simplified"
    level  = f"{s.audio_level_dbov:.1f} dBov" if not math.isnan(s.audio_level_dbov) else "n/a"
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


# ── Start SDK ──────────────────────────────────────────────────────────────────
incoming_call = threading.Event()
call_done     = threading.Event()
active_call   = None
call_lock     = threading.Lock()

with SDK(log_level=1, stats_interval_ms=5000) as sdk:
    print(f"baresdk version: {sdk.version}")

    account = sdk.create_account(sip_uri, password, TRANSPORT_UDP)
    account.register()

    for ev in account.events(timeout=60):

        if isinstance(ev, RegStateEvent):
            if ev.state == REG_REGISTERED:
                print("Registered!")
                print_devices(sdk)
                if callee:
                    print(f"Dialling {callee} ...")
                    with call_lock:
                        active_call = account.call(callee)
                else:
                    print("Waiting for incoming call ...")
            elif ev.error_str:
                print(f"Registration failed: {ev.error_str}")
                break

        elif isinstance(ev, IncomingCallEvent):
            print(f"\n=== Incoming call from {ev.from_uri} ===")
            print("Press 'a' + Enter to answer, 'r' + Enter to reject")
            with call_lock:
                active_call = ev.call
            incoming_call.set()

        elif isinstance(ev, CallStateEvent):
            print(f"Call state: {ev.state}"
                  + (f"  reason={ev.reason!r}" if ev.reason else ""))
            if ev.state in (CALL_ENDED, CALL_FAILED, CALL_CANCELLED):
                print("Call done.")
                call_done.set()
                break
            if ev.state == CALL_ESTABLISHED:
                time.sleep(10)
                with call_lock:
                    if active_call:
                        active_call.hangup()

        elif isinstance(ev, MediaStatsEvent):
            print_stats(ev)

        if incoming_call.is_set() and not call_done.is_set():
            choice = input().strip().lower()
            if choice == 'a':
                with call_lock:
                    if active_call:
                        try:
                            active_call.answer()
                        except Exception as e:
                            print(f"answer failed: {e}")
            elif choice == 'r':
                with call_lock:
                    if active_call:
                        active_call.hangup()
            incoming_call.clear()

    account.destroy()

print("Done.")
