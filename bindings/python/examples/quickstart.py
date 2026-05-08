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

# ── Start SDK ──────────────────────────────────────────────────────────────
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
            print(f"Call state: {ev.state}")
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
            print(f"MOS-LQ: {ev.mos_lq:.2f}  RTT: {ev.rtt_ms:.0f} ms  loss: {ev.loss_pct:.1f}%")

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
