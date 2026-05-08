use std::ffi::CString;

use baresdk_sys::*;

use crate::{call::Call, error::{check, Error}};

/// Per-account configuration.
#[derive(Debug, Clone, Default)]
pub struct AccountConfig {
    pub uri:          String,
    pub password:     String,
    pub transport:    Option<baresdk_transport_t>,
    pub server_host:  Option<String>,
    pub server_port:  u16,
    pub server_url:   Option<String>,
    pub auth_user:    Option<String>,
    pub display_name: Option<String>,
    pub media_enc:    Option<baresdk_media_enc_t>,
    pub ice_enabled:  bool,
    pub stun_server:  Option<String>,
    pub turn_server:  Option<String>,
    pub turn_user:    Option<String>,
    pub turn_pass:    Option<String>,
    pub verify_tls:   bool,
}

/// A SIP account handle.
pub struct Account {
    handle: baresdk_account_handle_t,
    // CStrings must outlive the C layer
    _strings: Vec<CString>,
}

impl Account {
    pub(crate) fn create(cfg: AccountConfig) -> Result<Self, Error> {
        let mut strings: Vec<CString> = Vec::new();

        let cstr_opt = |s: &Option<String>| -> *const std::os::raw::c_char {
            if let Some(ref v) = s {
                let cs = CString::new(v.as_str()).unwrap();
                let p = cs.as_ptr();
                strings.push(cs);
                p
            } else {
                std::ptr::null()
            }
        };

        let uri_cs  = CString::new(cfg.uri.as_str()).unwrap();
        let pass_cs = CString::new(cfg.password.as_str()).unwrap();

        let mut c = unsafe { std::mem::zeroed::<baresdk_account_config_t>() };
        c.uri       = uri_cs.as_ptr();
        c.password  = pass_cs.as_ptr();
        c.transport = cfg.transport
            .unwrap_or(baresdk_transport_t_BARESDK_TRANSPORT_UDP);
        c.server_port = cfg.server_port;
        c.ice_enabled = cfg.ice_enabled as i32;
        c.verify_tls  = cfg.verify_tls as i32;
        c.server_host = cstr_opt(&cfg.server_host);
        c.server_url  = cstr_opt(&cfg.server_url);
        c.auth_user   = cstr_opt(&cfg.auth_user);
        c.display_name = cstr_opt(&cfg.display_name);
        c.stun_server  = cstr_opt(&cfg.stun_server);
        c.turn_server  = cstr_opt(&cfg.turn_server);
        c.turn_user    = cstr_opt(&cfg.turn_user);
        c.turn_pass    = cstr_opt(&cfg.turn_pass);

        if let Some(enc) = cfg.media_enc { c.media_enc = enc; }

        strings.push(uri_cs);
        strings.push(pass_cs);

        let mut handle: baresdk_account_handle_t = std::ptr::null_mut();
        check(unsafe { baresdk_account_create(&c, &mut handle) })?;

        Ok(Account { handle, _strings: strings })
    }

    pub fn register(&self) -> Result<(), Error> {
        check(unsafe { baresdk_account_register(self.handle) })
    }

    pub fn unregister(&self) -> Result<(), Error> {
        check(unsafe { baresdk_account_unregister(self.handle) })
    }

    pub fn call(&self, uri: &str) -> Result<Call, Error> {
        let uri_cs = CString::new(uri).unwrap();
        let mut call_handle: baresdk_call_handle_t = std::ptr::null_mut();
        check(unsafe { baresdk_call_invite(self.handle, uri_cs.as_ptr(), &mut call_handle) })?;
        Ok(Call::from_handle(call_handle))
    }

    pub fn send_message(&self, to: &str, body: &str, content_type: &str) -> Result<(), Error> {
        let to_cs   = CString::new(to).unwrap();
        let body_cs = CString::new(body).unwrap();
        let ct_cs   = CString::new(content_type).unwrap();
        check(unsafe {
            baresdk_message_send(self.handle, to_cs.as_ptr(), body_cs.as_ptr(), ct_cs.as_ptr())
        })
    }

    pub fn publish_presence(&self, status: baresdk_presence_status_t) -> Result<(), Error> {
        check(unsafe { baresdk_account_publish_presence(self.handle, status) })
    }

    pub fn subscribe_presence(&self, target_uri: &str) -> Result<(), Error> {
        let cs = CString::new(target_uri).unwrap();
        check(unsafe { baresdk_account_subscribe_presence(self.handle, cs.as_ptr()) })
    }

    pub fn add_header(&self, name: &str, value: &str) -> Result<(), Error> {
        let n = CString::new(name).unwrap();
        let v = CString::new(value).unwrap();
        check(unsafe { baresdk_account_add_header(self.handle, n.as_ptr(), v.as_ptr()) })
    }

    pub fn handle(&self) -> baresdk_account_handle_t { self.handle }
}

impl Drop for Account {
    fn drop(&mut self) {
        unsafe { baresdk_account_destroy(self.handle); }
    }
}
