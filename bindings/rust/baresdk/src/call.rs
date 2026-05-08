use std::ffi::CString;
use baresdk_sys::*;
use crate::error::{check, Error};

/// An active SIP call handle.
pub struct Call {
    handle: baresdk_call_handle_t,
}

impl Call {
    pub(crate) fn from_handle(h: baresdk_call_handle_t) -> Self {
        Self { handle: h }
    }

    /// Reconstruct from a raw pointer value (received in event payloads).
    pub fn from_ptr(ptr: usize) -> Self {
        Self { handle: ptr as baresdk_call_handle_t }
    }

    pub fn answer(&self) -> Result<(), Error> {
        check(unsafe { baresdk_call_answer(self.handle) })
    }

    pub fn hangup(&self) -> Result<(), Error> {
        check(unsafe { baresdk_call_hangup(self.handle) })
    }

    pub fn hold(&self) -> Result<(), Error> {
        check(unsafe { baresdk_call_hold(self.handle) })
    }

    pub fn resume(&self) -> Result<(), Error> {
        check(unsafe { baresdk_call_resume(self.handle) })
    }

    pub fn send_dtmf(&self, digit: char) -> Result<(), Error> {
        check(unsafe { baresdk_call_send_dtmf(self.handle, digit as std::os::raw::c_char) })
    }

    pub fn transfer(&self, uri: &str) -> Result<(), Error> {
        let cs = CString::new(uri).unwrap();
        check(unsafe { baresdk_call_transfer(self.handle, cs.as_ptr()) })
    }

    pub fn attended_transfer(&self, consult: &Call) -> Result<(), Error> {
        check(unsafe { baresdk_call_attended_transfer(self.handle, consult.handle) })
    }

    pub fn mute(&self, muted: bool) -> Result<(), Error> {
        check(unsafe { baresdk_audio_mute(self.handle, muted as i32) })
    }

    pub fn add_header(&self, name: &str, value: &str) -> Result<(), Error> {
        let n = CString::new(name).unwrap();
        let v = CString::new(value).unwrap();
        check(unsafe { baresdk_call_add_header(self.handle, n.as_ptr(), v.as_ptr()) })
    }

    pub fn stats(&self) -> Result<baresdk_ev_media_stats_t, Error> {
        let mut s = unsafe { std::mem::zeroed::<baresdk_ev_media_stats_t>() };
        check(unsafe { baresdk_call_get_stats(self.handle, &mut s) })?;
        Ok(s)
    }

    pub fn handle_ptr(&self) -> usize { self.handle as usize }
}
