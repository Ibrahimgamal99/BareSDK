use baresdk_sys::*;

/// Decoded SDK event delivered to callers via `std::sync::mpsc::Receiver<Event>`.
#[derive(Debug, Clone)]
pub enum Event {
    Log { message: String },

    Registered,
    RegistrationFailed { error: i32, error_str: Option<String> },
    Registering,
    Unregistered,

    IncomingCall { call_handle: usize, from_uri: String, display_name: Option<String> },

    CallState     { call_handle: usize, state: baresdk_call_state_t, reason: Option<String> },
    CallDtmf      { call_handle: usize, digit: char },

    SdpNegotiation {
        call_handle:       usize,
        local_sdp:         String,
        remote_sdp:        String,
        negotiated_codec:  Option<String>,
        negotiated_crypto: Option<String>,
    },

    SipTrace {
        direction:   baresdk_media_dir_t,
        transport:   String,
        remote_addr: String,
        raw_message: String,
        timestamp_us: u64,
    },

    MediaStats {
        call_handle:      usize,
        packets_sent:     u32,
        packets_received: u32,
        packets_lost:     u32,
        loss_pct:         f32,
        jitter_ms:        f32,
        rtt_ms:           f32,
        mos_lq:           f32,
        mos_cq:           f32,
        codec_name:       String,
        bandwidth_kbps_tx: u32,
        bandwidth_kbps_rx: u32,
    },

    RegistrarWarning { message: String },

    TransferRequest  { call_handle: usize, refer_to_uri: String, has_replaces: bool },

    Mwi {
        messages_waiting: bool,
        new_voice: u32, old_voice: u32,
        new_urgent: u32, old_urgent: u32,
    },

    Message      { from_uri: String, body: String, content_type: String },
    PresenceState{ target_uri: String, status: baresdk_presence_status_t },
}

/// Decode a raw C event pointer into a safe Rust `Event`.
pub(crate) unsafe fn decode(ev: *const baresdk_event_t) -> Option<Event> {
    let ev = &*ev;

    fn cstr(p: *const std::os::raw::c_char) -> Option<String> {
        if p.is_null() { return None; }
        unsafe { Some(std::ffi::CStr::from_ptr(p).to_string_lossy().into_owned()) }
    }
    fn cstr_req(p: *const std::os::raw::c_char) -> String {
        cstr(p).unwrap_or_default()
    }
    fn ptr_as_usize<T>(p: *mut T) -> usize { p as usize }

    Some(match ev.type_ {
        baresdk_event_type_t_BARESDK_EV_LOG => Event::Log {
            message: cstr_req(ev.u.log.message),
        },

        baresdk_event_type_t_BARESDK_EV_REG_STATE => {
            match ev.u.reg.state {
                baresdk_reg_state_t_BARESDK_REG_REGISTERED   => Event::Registered,
                baresdk_reg_state_t_BARESDK_REG_FAILED        => Event::RegistrationFailed {
                    error:     ev.u.reg.error as i32,
                    error_str: cstr(ev.u.reg.error_str),
                },
                baresdk_reg_state_t_BARESDK_REG_REGISTERING  => Event::Registering,
                _ => Event::Unregistered,
            }
        }

        baresdk_event_type_t_BARESDK_EV_INCOMING_CALL => Event::IncomingCall {
            call_handle:  ptr_as_usize(ev.u.incoming.call),
            from_uri:     cstr_req(ev.u.incoming.from_uri),
            display_name: cstr(ev.u.incoming.display_name),
        },

        baresdk_event_type_t_BARESDK_EV_CALL_STATE => Event::CallState {
            call_handle: ptr_as_usize(ev.u.call_state.call),
            state:       ev.u.call_state.state,
            reason:      cstr(ev.u.call_state.reason),
        },

        baresdk_event_type_t_BARESDK_EV_CALL_DTMF => Event::CallDtmf {
            call_handle: ptr_as_usize(ev.u.dtmf.call),
            digit:       ev.u.dtmf.digit as u8 as char,
        },

        baresdk_event_type_t_BARESDK_EV_SDP_NEGOTIATION => Event::SdpNegotiation {
            call_handle:       ptr_as_usize(ev.u.sdp.call),
            local_sdp:         cstr_req(ev.u.sdp.local_sdp),
            remote_sdp:        cstr_req(ev.u.sdp.remote_sdp),
            negotiated_codec:  cstr(ev.u.sdp.negotiated_codec),
            negotiated_crypto: cstr(ev.u.sdp.negotiated_crypto),
        },

        baresdk_event_type_t_BARESDK_EV_SIP_TRACE => Event::SipTrace {
            direction:    ev.u.sip_trace.dir,
            transport:    cstr_req(ev.u.sip_trace.transport),
            remote_addr:  cstr_req(ev.u.sip_trace.remote_addr),
            raw_message:  cstr_req(ev.u.sip_trace.raw_message),
            timestamp_us: ev.u.sip_trace.timestamp_us,
        },

        baresdk_event_type_t_BARESDK_EV_MEDIA_STATS => {
            let s = &ev.u.stats;
            Event::MediaStats {
                call_handle:      ptr_as_usize(s.call),
                packets_sent:     s.packets_sent,
                packets_received: s.packets_received,
                packets_lost:     s.packets_lost,
                loss_pct:         s.loss_pct,
                jitter_ms:        s.jitter_ms,
                rtt_ms:           s.rtt_ms,
                mos_lq:           s.mos_lq,
                mos_cq:           s.mos_cq,
                codec_name:       cstr_req(s.codec_name),
                bandwidth_kbps_tx: s.bandwidth_kbps_tx,
                bandwidth_kbps_rx: s.bandwidth_kbps_rx,
            }
        }

        baresdk_event_type_t_BARESDK_EV_REGISTRAR_WARNING => Event::RegistrarWarning {
            message: cstr_req(ev.u.reg_warn.message),
        },

        baresdk_event_type_t_BARESDK_EV_TRANSFER_REQUEST => Event::TransferRequest {
            call_handle:  ptr_as_usize(ev.u.transfer_req.call),
            refer_to_uri: cstr_req(ev.u.transfer_req.refer_to_uri),
            has_replaces: ev.u.transfer_req.has_replaces != 0,
        },

        baresdk_event_type_t_BARESDK_EV_MWI => Event::Mwi {
            messages_waiting: ev.u.mwi.messages_waiting != 0,
            new_voice:  ev.u.mwi.new_voice,
            old_voice:  ev.u.mwi.old_voice,
            new_urgent: ev.u.mwi.new_urgent,
            old_urgent: ev.u.mwi.old_urgent,
        },

        baresdk_event_type_t_BARESDK_EV_MESSAGE => Event::Message {
            from_uri:     cstr_req(ev.u.msg.from_uri),
            body:         cstr_req(ev.u.msg.body),
            content_type: cstr_req(ev.u.msg.content_type),
        },

        baresdk_event_type_t_BARESDK_EV_PRESENCE_STATE => Event::PresenceState {
            target_uri: cstr_req(ev.u.presence.target_uri),
            status:     ev.u.presence.status,
        },

        _ => return None,
    })
}
