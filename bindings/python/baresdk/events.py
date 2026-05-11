"""
events.py — Python dataclasses mirroring baresdk event payloads.

The raw C event pointer is decoded into one of these objects and put on
the per-account queue.  All string fields are decoded from bytes to str.
State fields use human-readable strings instead of integer constants.
"""

from dataclasses import dataclass, field
from typing import Optional, List


@dataclass
class RegStateEvent:
    type: str = field(init=False, default="reg_state")
    state: str           # "unregistered" | "registering" | "registered" | "failed" | "unregistering"
    error: int           # BARESDK_ERR_* or BARESDK_OK
    error_str: Optional[str]
    retry_attempt: int
    retry_delay_ms: int


@dataclass
class IncomingCallEvent:
    type: str = field(init=False, default="incoming_call")
    call: object         # baresdk.Call (set by Account)
    from_uri: str
    display_name: Optional[str]


@dataclass
class CallStateEvent:
    type: str = field(init=False, default="call_state")
    call: object         # baresdk.Call
    state: str           # "calling" | "ringing" | "established" | "held" | "ended" | "cancelled" | "failed"
    error: int
    reason: Optional[str]


@dataclass
class CallDtmfEvent:
    type: str = field(init=False, default="dtmf")
    call: object
    digit: str           # single character


@dataclass
class SdpNegotiationEvent:
    type: str = field(init=False, default="sdp_negotiation")
    call: object
    local_sdp: str
    remote_sdp: str
    negotiated_codec: Optional[str]
    negotiated_crypto: Optional[str]
    rejected_codecs: List[str]
    warnings: List[str]


@dataclass
class SipTraceEvent:
    type: str = field(init=False, default="sip_trace")
    direction: str       # "tx" | "rx"
    transport: str
    remote_addr: str
    raw_message: str
    timestamp_us: int


@dataclass
class MediaStatsEvent:
    type: str = field(init=False, default="media_stats")
    call: object
    # Packet counters
    packets_sent: int
    packets_received: int
    packets_lost: int        # TX-side
    packets_lost_rx: int     # RX-side
    bytes_sent: int
    bytes_received: int
    tx_errors: int
    rx_errors: int
    # Loss
    loss_pct: float          # TX-side %
    loss_pct_rx: float       # RX-side %
    # Delay / jitter
    jitter_ms: float
    tx_jitter_ms: float
    rtt_ms: float
    # Jitter buffer
    jitter_buffer_ms: int
    jitter_buffer_load: int
    late_packets: int
    discarded_packets: int
    jitter_buffer_target_ms: int
    jitter_buffer_adaptive: bool
    # PLC
    plc_frames: int
    plc_ratio: float
    # Bandwidth
    bandwidth_kbps_tx: int
    bandwidth_kbps_rx: int
    avg_bandwidth_kbps_tx: int
    avg_bandwidth_kbps_rx: int
    # MOS
    mos_lq: float
    mos_cq: float
    mos_lq_rx: float
    mos_cq_rx: float
    mos_method: int
    # Codec
    codec_name: str
    codec_clock_rate: int
    codec_sample_rate: int
    codec_channels: int
    payload_type: int
    # Audio level
    audio_level_dbov: float  # speaker (RX) dBov; NaN when unavailable
    mic_level_dbov: float    # microphone (TX) dBov; NaN when unavailable
    # Stream identity
    ssrc_tx: int
    ssrc_rx: int
    remote_addr: str
    # Session history
    mos_lq_min: float
    mos_lq_avg: float
    stats_tick: int
    call_duration_ms: int
    is_final: bool


@dataclass
class LogEvent:
    type: str = field(init=False, default="log")
    message: str


@dataclass
class RegistrarWarningEvent:
    type: str = field(init=False, default="registrar_warning")
    message: str


@dataclass
class TransferRequestEvent:
    type: str = field(init=False, default="transfer_request")
    call: object
    refer_to_uri: str
    has_replaces: bool


@dataclass
class MwiEvent:
    type: str = field(init=False, default="mwi")
    messages_waiting: bool
    new_voice: int
    old_voice: int
    new_urgent: int
    old_urgent: int
    raw_body: Optional[str]


@dataclass
class MessageEvent:
    type: str = field(init=False, default="message")
    from_uri: str
    body: str
    content_type: str


@dataclass
class PresenceStateEvent:
    type: str = field(init=False, default="presence_state")
    target_uri: str
    status: str          # "unknown" | "open" | "closed" | "busy"


@dataclass
class QualityAlertEvent:
    type: str = field(init=False, default="quality_alert")
    call: object
    issue: str           # "mos" | "loss" | "jitter" | "rtt"
    value: float
    threshold: float
    recovering: bool
