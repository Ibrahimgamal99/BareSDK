"""
baresdk — Pythonic wrapper around the baresdk C SDK.

Quick start:

    from baresdk import SDK

    sdk = SDK(log_level=2)
    account = sdk.create_account("alice@pbx.example.com", "secret")
    account.register()

    for ev in account.events():
        if ev.__class__.__name__ == "RegStateEvent" and ev.state == 2:  # REGISTERED
            call = account.call("bob@pbx.example.com")
        elif ev.__class__.__name__ == "CallStateEvent" and ev.state == 4:  # ENDED
            break

    sdk.shutdown()
"""

import queue
import threading
from typing import Iterator, Optional

from ._loader import ffi, lib
from .events import (
    RegStateEvent, IncomingCallEvent, CallStateEvent, CallDtmfEvent,
    SdpNegotiationEvent, SipTraceEvent, MediaStatsEvent, LogEvent,
    RegistrarWarningEvent, TransferRequestEvent, MwiEvent,
    MessageEvent, PresenceStateEvent,
)

# ── C constant mirrors ────────────────────────────────────────────────────────
REG_UNREGISTERED  = 0
REG_REGISTERING   = 1
REG_REGISTERED    = 2
REG_FAILED        = 3
REG_UNREGISTERING = 4

CALL_CALLING     = 0
CALL_RINGING     = 1
CALL_ESTABLISHED = 2
CALL_HELD        = 3
CALL_ENDED       = 4
CALL_CANCELLED   = 5
CALL_FAILED      = 6

TRANSPORT_UDP = 0
TRANSPORT_TCP = 1
TRANSPORT_TLS = 2
TRANSPORT_WS  = 3
TRANSPORT_WSS = 4

MEDIA_ENC_NONE      = 0
MEDIA_ENC_SDES      = 1
MEDIA_ENC_DTLS_SRTP = 2

PRESENCE_UNKNOWN = 0
PRESENCE_OPEN    = 1
PRESENCE_CLOSED  = 2
PRESENCE_BUSY    = 3


def _s(cptr) -> Optional[str]:
    """Decode a C const char * to str (or None if NULL)."""
    if cptr == ffi.NULL:
        return None
    return ffi.string(cptr).decode("utf-8", errors="replace")


def _str_array(arr) -> list:
    """Decode a NULL-terminated const char ** to list[str]."""
    result = []
    i = 0
    while arr[i] != ffi.NULL:
        result.append(_s(arr[i]))
        i += 1
    return result


# ── Global event router ───────────────────────────────────────────────────────

_accounts_lock = threading.Lock()
_accounts: dict = {}          # {c_handle_int: Account}
_sdk_ref   = None             # the single active SDK instance


def _register_account(c_handle, account):
    with _accounts_lock:
        _accounts[int(ffi.cast("uintptr_t", c_handle))] = account


def _unregister_account(c_handle):
    with _accounts_lock:
        _accounts.pop(int(ffi.cast("uintptr_t", c_handle)), None)


def _lookup_account(c_handle):
    return _accounts.get(int(ffi.cast("uintptr_t", c_handle)))


@ffi.callback("void(const baresdk_event_t *, void *)")
def _global_event_cb(ev_ptr, _userdata):
    """Single C callback; routes decoded events to the right Account queue."""
    ev  = ev_ptr[0]
    typ = ev.type

    try:
        # Figure out the destination account handle
        acct_handle = ffi.NULL
        call_handle = ffi.NULL

        if typ == 0:    # LOG — broadcast to all accounts
            obj = LogEvent(message=_s(ev.u.log.message) or "")
            with _accounts_lock:
                targets = list(_accounts.values())
            for a in targets:
                a._put(obj)
            return

        elif typ == 1:  # REG_STATE
            acct_handle = ev.u.reg.account
            call_handle = ffi.NULL
            obj = RegStateEvent(
                state        = ev.u.reg.state,
                error        = ev.u.reg.error,
                error_str    = _s(ev.u.reg.error_str),
                retry_attempt= ev.u.reg.retry_attempt,
                retry_delay_ms=ev.u.reg.retry_delay_ms,
            )

        elif typ == 2:  # INCOMING_CALL
            acct_handle = ev.u.incoming.account
            call_handle = ev.u.incoming.call
            obj = IncomingCallEvent(
                call=None,  # set after lookup
                from_uri    = _s(ev.u.incoming.from_uri) or "",
                display_name= _s(ev.u.incoming.display_name),
            )

        elif typ == 3:  # CALL_STATE
            acct_handle = ev.u.call_state.account
            call_handle = ev.u.call_state.call
            obj = CallStateEvent(
                call=None,
                state  = ev.u.call_state.state,
                error  = ev.u.call_state.error,
                reason = _s(ev.u.call_state.reason),
            )

        elif typ == 4:  # DTMF
            call_handle = ev.u.dtmf.call
            obj = CallDtmfEvent(call=None, digit=chr(ev.u.dtmf.digit))

        elif typ == 5:  # SDP_NEGOTIATION
            call_handle = ev.u.sdp.call
            obj = SdpNegotiationEvent(
                call=None,
                local_sdp          = _s(ev.u.sdp.local_sdp) or "",
                remote_sdp         = _s(ev.u.sdp.remote_sdp) or "",
                negotiated_codec   = _s(ev.u.sdp.negotiated_codec),
                negotiated_crypto  = _s(ev.u.sdp.negotiated_crypto),
                rejected_codecs    = _str_array(ev.u.sdp.rejected_codecs),
                warnings           = _str_array(ev.u.sdp.warnings),
            )

        elif typ == 6:  # SIP_TRACE
            obj = SipTraceEvent(
                direction  = ev.u.sip_trace.dir,
                transport  = _s(ev.u.sip_trace.transport) or "",
                remote_addr= _s(ev.u.sip_trace.remote_addr) or "",
                raw_message= _s(ev.u.sip_trace.raw_message) or "",
                timestamp_us=ev.u.sip_trace.timestamp_us,
            )
            with _accounts_lock:
                targets = list(_accounts.values())
            for a in targets:
                a._put(obj)
            return

        elif typ == 7:  # MEDIA_STATS
            call_handle = ev.u.stats.call
            s = ev.u.stats
            obj = MediaStatsEvent(
                call=None,
                packets_sent       = s.packets_sent,
                packets_received   = s.packets_received,
                packets_lost       = s.packets_lost,
                packets_lost_rx    = s.packets_lost_rx,
                bytes_sent         = s.bytes_sent,
                bytes_received     = s.bytes_received,
                tx_errors          = s.tx_errors,
                rx_errors          = s.rx_errors,
                loss_pct           = s.loss_pct,
                loss_pct_rx        = s.loss_pct_rx,
                jitter_ms          = s.jitter_ms,
                tx_jitter_ms       = s.tx_jitter_ms,
                rtt_ms             = s.rtt_ms,
                jitter_buffer_ms   = s.jitter_buffer_ms,
                jitter_buffer_load = s.jitter_buffer_load,
                late_packets       = s.late_packets,
                discarded_packets  = s.discarded_packets,
                bandwidth_kbps_tx  = s.bandwidth_kbps_tx,
                bandwidth_kbps_rx  = s.bandwidth_kbps_rx,
                avg_bandwidth_kbps_tx = s.avg_bandwidth_kbps_tx,
                avg_bandwidth_kbps_rx = s.avg_bandwidth_kbps_rx,
                mos_lq             = s.mos_lq,
                mos_cq             = s.mos_cq,
                mos_method         = s.mos_method,
                codec_name         = _s(s.codec_name) or "",
                codec_clock_rate   = s.codec_clock_rate,
                codec_sample_rate  = s.codec_sample_rate,
                codec_channels     = s.codec_channels,
                payload_type       = s.payload_type,
                audio_level_dbov   = s.audio_level_dbov,
                ssrc_tx            = s.ssrc_tx,
                ssrc_rx            = s.ssrc_rx,
                remote_addr        = ffi.string(s.remote_addr).decode("utf-8", errors="replace"),
            )

        elif typ == 8:  # REGISTRAR_WARNING
            obj = RegistrarWarningEvent(message=_s(ev.u.reg_warn.message) or "")

        elif typ == 9:  # TRANSFER_REQUEST
            acct_handle = ev.u.transfer_req.account
            call_handle = ev.u.transfer_req.call
            obj = TransferRequestEvent(
                call=None,
                refer_to_uri = _s(ev.u.transfer_req.refer_to_uri) or "",
                has_replaces = bool(ev.u.transfer_req.has_replaces),
            )

        elif typ == 10: # MWI
            acct_handle = ev.u.mwi.account
            obj = MwiEvent(
                messages_waiting=bool(ev.u.mwi.messages_waiting),
                new_voice  = ev.u.mwi.new_voice,
                old_voice  = ev.u.mwi.old_voice,
                new_urgent = ev.u.mwi.new_urgent,
                old_urgent = ev.u.mwi.old_urgent,
                raw_body   = _s(ev.u.mwi.raw_body),
            )

        elif typ == 11: # MESSAGE
            acct_handle = ev.u.msg.account
            obj = MessageEvent(
                from_uri    = _s(ev.u.msg.from_uri) or "",
                body        = _s(ev.u.msg.body) or "",
                content_type= _s(ev.u.msg.content_type) or "text/plain",
            )

        elif typ == 12: # PRESENCE_STATE
            acct_handle = ev.u.presence.account
            obj = PresenceStateEvent(
                target_uri = _s(ev.u.presence.target_uri) or "",
                status     = ev.u.presence.status,
            )
        else:
            return

        # Attach Call wrapper if needed
        if call_handle != ffi.NULL and hasattr(obj, "call"):
            obj.call = Call(call_handle)

        # Route to owning account
        if acct_handle != ffi.NULL:
            acct = _lookup_account(acct_handle)
            if acct:
                acct._put(obj)
        elif call_handle != ffi.NULL:
            # find account by call owner (best-effort: put on all)
            with _accounts_lock:
                targets = list(_accounts.values())
            for a in targets:
                a._put(obj)

    except Exception:
        pass  # never let Python exceptions escape into C


# ── Call ──────────────────────────────────────────────────────────────────────

class Call:
    """Wrapper around a baresdk_call_handle_t."""

    def __init__(self, handle):
        self._h = handle

    def answer(self):
        _check(lib.baresdk_call_answer(self._h), "answer")

    def hangup(self):
        lib.baresdk_call_hangup(self._h)

    def hold(self):
        _check(lib.baresdk_call_hold(self._h), "hold")

    def resume(self):
        _check(lib.baresdk_call_resume(self._h), "resume")

    def send_dtmf(self, digit: str):
        lib.baresdk_call_send_dtmf(self._h, ord(digit))

    def transfer(self, uri: str):
        _check(lib.baresdk_call_transfer(self._h, uri.encode()), "transfer")

    def mute(self, muted: bool = True):
        lib.baresdk_audio_mute(self._h, muted)

    def mute_rx(self, muted: bool = True):
        lib.baresdk_audio_mute_rx(self._h, muted)

    def set_dscp_rtp(self, dscp: int):
        _check(lib.baresdk_call_set_dscp_rtp(self._h, dscp), "set_dscp_rtp")

    def stats(self):
        s = ffi.new("baresdk_ev_media_stats_t *")
        lib.baresdk_call_get_stats(self._h, s)
        return s[0]

    def record_start(self, path: str):
        """Record mixed call audio (RX+TX) to a single WAV file."""
        _check(lib.baresdk_call_record_start(self._h, path.encode()), "record_start")

    def record_stop(self):
        """Stop recording and finalize WAV headers."""
        lib.baresdk_call_record_stop(self._h)

    @property
    def handle(self):
        return self._h


def _check(rc, what):
    if rc != 0:
        raise RuntimeError(f"baresdk.{what} failed (code {rc})")


# ── Account ───────────────────────────────────────────────────────────────────

class Account:
    """A SIP account. Receives events via the events() generator."""

    def __init__(self, handle):
        self._h = handle
        self._q: queue.SimpleQueue = queue.SimpleQueue()
        _register_account(handle, self)

    def _put(self, obj):
        self._q.put(obj)

    def register(self):
        _check(lib.baresdk_account_register(self._h), "register")

    def unregister(self):
        lib.baresdk_account_unregister(self._h)

    def set_retry_policy(self, initial_ms: int, max_ms: int,
                         backoff: float, max_attempts: int = 0):
        """Override the retry policy for this account."""
        _check(lib.baresdk_account_set_retry_policy(
            self._h, initial_ms, max_ms, backoff, max_attempts),
            "set_retry_policy")

    def cancel_retry(self):
        """Cancel a pending retry timer. Account stays FAILED."""
        lib.baresdk_account_cancel_retry(self._h)

    def retry_now(self):
        """Skip the current backoff delay and re-register immediately."""
        lib.baresdk_account_retry_now(self._h)

    def call(self, uri: str) -> Call:
        ch = ffi.new("baresdk_call_handle_t *")
        _check(lib.baresdk_call_invite(self._h, uri.encode(), ch), "call_invite")
        return Call(ch[0])

    def send_message(self, to: str, body: str, content_type: str = "text/plain"):
        _check(
            lib.baresdk_message_send(self._h, to.encode(), body.encode(),
                                     content_type.encode()),
            "message_send")

    def publish_presence(self, status: int):
        lib.baresdk_account_publish_presence(self._h, status)

    def subscribe_presence(self, target_uri: str):
        _check(lib.baresdk_account_subscribe_presence(self._h, target_uri.encode()),
               "subscribe_presence")

    def add_header(self, name: str, value: str):
        _check(lib.baresdk_account_add_header(self._h, name.encode(), value.encode()),
               "add_header")

    def events(self, timeout: Optional[float] = None) -> Iterator:
        """
        Blocking generator that yields event objects one at a time.
        Stops when a sentinel None is put on the queue.

            for ev in account.events():
                if isinstance(ev, RegStateEvent) and ev.state == REG_REGISTERED:
                    ...
        """
        while True:
            try:
                ev = self._q.get(timeout=timeout)
                if ev is None:
                    return
                yield ev
            except queue.Empty:
                return

    def destroy(self):
        _unregister_account(self._h)
        lib.baresdk_account_destroy(self._h)
        self._h = None
        self._q.put(None)  # unblock any waiting events() caller

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.destroy()


def _list_audio_devices(fn) -> list:
    buf = ffi.new("baresdk_audio_device_t[32]")
    n = fn(buf, 32)
    out = []
    for i in range(max(n, 0)):
        out.append({
            "name":        ffi.string(buf[i].name).decode("utf-8", errors="replace"),
            "description": ffi.string(buf[i].description).decode("utf-8", errors="replace"),
            "is_default":  bool(buf[i].is_default),
        })
    return out


# ── SDK ───────────────────────────────────────────────────────────────────────

class SDK:
    """
    Top-level SDK object. One per process.

        sdk = SDK(log_level=2, transport=TRANSPORT_TLS, server_host="pbx.example.com")
        account = sdk.create_account("alice@pbx.example.com", "secret")
        account.register()
    """

    def __init__(self, **kwargs):
        self._cfg = ffi.new("baresdk_config_t *")
        lib.baresdk_config_init(self._cfg)

        for key, val in kwargs.items():
            if isinstance(val, str):
                # keep reference so GC doesn't collect the cdata
                setattr(self, f"_str_{key}", ffi.new("char[]", val.encode()))
                setattr(self._cfg, key, getattr(self, f"_str_{key}"))
            else:
                setattr(self._cfg, key, val)

        self._cfg.event_cb       = _global_event_cb
        self._cfg.event_userdata = ffi.NULL
        _check(lib.baresdk_init(self._cfg), "init")

    def create_account(self, uri: str, password: str,
                       transport: int = TRANSPORT_UDP,
                       **kwargs) -> Account:
        cfg = ffi.new("baresdk_account_config_t *")
        # keep cdata alive
        self._acfg_uri  = ffi.new("char[]", uri.encode())
        self._acfg_pass = ffi.new("char[]", password.encode())
        cfg.uri       = self._acfg_uri
        cfg.password  = self._acfg_pass
        cfg.transport = transport
        for key, val in kwargs.items():
            if isinstance(val, str):
                ref = ffi.new("char[]", val.encode())
                setattr(self, f"_acfg_{key}", ref)
                setattr(cfg, key, ref)
            else:
                setattr(cfg, key, val)

        h = ffi.new("baresdk_account_handle_t *")
        _check(lib.baresdk_account_create(cfg, h), "account_create")
        return Account(h[0])

    def list_input_devices(self):
        """Return a list of dicts with keys: name, description, is_default."""
        return _list_audio_devices(lib.baresdk_audio_list_input_devices)

    def list_output_devices(self):
        """Return a list of dicts with keys: name, description, is_default."""
        return _list_audio_devices(lib.baresdk_audio_list_output_devices)

    def set_input_device(self, name: str):
        lib.baresdk_audio_set_input_device(name.encode())

    def set_output_device(self, name: str):
        lib.baresdk_audio_set_output_device(name.encode())

    def set_aec(self, enable: bool):
        lib.baresdk_set_aec(int(enable))

    def set_ns(self, enable: bool):
        lib.baresdk_set_ns(int(enable))

    def set_agc(self, enable: bool):
        lib.baresdk_set_agc(int(enable))

    def set_jitter_buffer(self, min_ms: int, max_ms: int):
        lib.baresdk_set_jitter_buffer(min_ms, max_ms)

    def pcap_start(self, path: str):
        _check(lib.baresdk_pcap_start(path.encode()), "pcap_start")

    def pcap_stop(self):
        lib.baresdk_pcap_stop()

    @property
    def version(self) -> str:
        return ffi.string(lib.baresdk_version()).decode()

    def shutdown(self):
        lib.baresdk_shutdown()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.shutdown()


__all__ = [
    "SDK", "Account", "Call",
    "RegStateEvent", "IncomingCallEvent", "CallStateEvent", "CallDtmfEvent",
    "SdpNegotiationEvent", "SipTraceEvent", "MediaStatsEvent", "LogEvent",
    "RegistrarWarningEvent", "TransferRequestEvent", "MwiEvent",
    "MessageEvent", "PresenceStateEvent",
    "REG_UNREGISTERED", "REG_REGISTERING", "REG_REGISTERED",
    "REG_FAILED", "REG_UNREGISTERING",
    "CALL_CALLING", "CALL_RINGING", "CALL_ESTABLISHED", "CALL_HELD",
    "CALL_ENDED", "CALL_CANCELLED", "CALL_FAILED",
    "TRANSPORT_UDP", "TRANSPORT_TCP", "TRANSPORT_TLS", "TRANSPORT_WS", "TRANSPORT_WSS",
    "MEDIA_ENC_NONE", "MEDIA_ENC_SDES", "MEDIA_ENC_DTLS_SRTP",
    "PRESENCE_UNKNOWN", "PRESENCE_OPEN", "PRESENCE_CLOSED", "PRESENCE_BUSY",
]
