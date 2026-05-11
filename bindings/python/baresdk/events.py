"""
events.py — Python dataclasses mirroring baresdk event payloads.

The raw C event pointer is decoded into one of these objects and put on
the per-account queue.  All string fields are decoded from bytes to str.
"""

from dataclasses import dataclass, field
from typing import Optional, List


@dataclass
class RegStateEvent:
    state: int           # BARESDK_REG_* constant
    error: int           # BARESDK_ERR_* or BARESDK_OK
    error_str: Optional[str]
    retry_attempt: int
    retry_delay_ms: int


@dataclass
class IncomingCallEvent:
    call: object         # baresdk.Call (set by Account)
    from_uri: str
    display_name: Optional[str]


@dataclass
class CallStateEvent:
    call: object         # baresdk.Call
    state: int           # BARESDK_CALL_* constant
    error: int
    reason: Optional[str]


@dataclass
class CallDtmfEvent:
    call: object
    digit: str           # single character


@dataclass
class SdpNegotiationEvent:
    call: object
    local_sdp: str
    remote_sdp: str
    negotiated_codec: Optional[str]
    negotiated_crypto: Optional[str]
    rejected_codecs: List[str]
    warnings: List[str]


@dataclass
class SipTraceEvent:
    direction: int       # BARESDK_MEDIA_DIR_TX / RX
    transport: str
    remote_addr: str
    raw_message: str
    timestamp_us: int


@dataclass
class MediaStatsEvent:
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
    audio_level_dbov: float  # dBov; NaN when unavailable
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
    message: str


@dataclass
class RegistrarWarningEvent:
    message: str


@dataclass
class TransferRequestEvent:
    call: object
    refer_to_uri: str
    has_replaces: bool


@dataclass
class MwiEvent:
    messages_waiting: bool
    new_voice: int
    old_voice: int
    new_urgent: int
    old_urgent: int
    raw_body: Optional[str]


@dataclass
class MessageEvent:
    from_uri: str
    body: str
    content_type: str


@dataclass
class PresenceStateEvent:
    target_uri: str
    status: int          # BARESDK_PRESENCE_* constant


@dataclass
class QualityAlertEvent:
    call: object
    issue: int           # BARESDK_QUALITY_* constant
    value: float         # current metric value
    threshold: float     # threshold that was crossed
    recovering: bool     # True = value returned above threshold
