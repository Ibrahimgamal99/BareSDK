use std::{
    ffi::CString,
    sync::{mpsc, Mutex},
};

use baresdk_sys::*;

use crate::{
    account::{Account, AccountConfig},
    error::{check, Error},
    event,
};

/// Global SDK configuration.
#[derive(Debug, Clone)]
pub struct Config {
    pub log_level:          i32,
    pub transport:          baresdk_transport_t,
    pub server_host:        Option<String>,
    pub server_url:         Option<String>,
    pub server_port:        u16,
    pub sip_domain:         Option<String>,
    pub stun_server:        Option<String>,
    pub turn_server:        Option<String>,
    pub turn_user:          Option<String>,
    pub turn_pass:          Option<String>,
    pub ice_enabled:        bool,
    pub ca_cert_path:       Option<String>,
    pub verify_server:      bool,
    pub trace_sip:          bool,
    pub stats_interval_ms:  u32,
    pub pcap_path:          Option<String>,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            log_level:         1,
            transport:         baresdk_transport_t_BARESDK_TRANSPORT_UDP,
            server_host:       None,
            server_url:        None,
            server_port:       0,
            sip_domain:        None,
            stun_server:       None,
            turn_server:       None,
            turn_user:         None,
            turn_pass:         None,
            ice_enabled:       false,
            ca_cert_path:      None,
            verify_server:     true,
            trace_sip:         false,
            stats_interval_ms: 0,
            pcap_path:         None,
        }
    }
}

// ── Global event channel (one per process) ────────────────────────────────

static SENDER: Mutex<Option<mpsc::Sender<crate::event::Event>>> = Mutex::new(None);

unsafe extern "C" fn global_event_cb(ev: *const baresdk_event_t, _ud: *mut std::ffi::c_void) {
    if let Some(decoded) = event::decode(ev) {
        if let Ok(guard) = SENDER.lock() {
            if let Some(tx) = guard.as_ref() {
                let _ = tx.send(decoded);
            }
        }
    }
}

// ── SDK ───────────────────────────────────────────────────────────────────

/// Top-level SDK handle. One per process; drops call `baresdk_shutdown`.
pub struct SDK {
    _cfg_strings: Vec<CString>,   // keep alive for the duration of the SDK
}

impl SDK {
    pub fn new(cfg: Config) -> Result<(Self, mpsc::Receiver<crate::event::Event>), Error> {
        let (tx, rx) = mpsc::channel();
        {
            let mut guard = SENDER.lock().unwrap();
            *guard = Some(tx);
        }

        // Build C config — string lifetimes kept in `strings`
        let mut strings: Vec<CString> = Vec::new();
        let cstr = |s: &Option<String>| -> *const std::os::raw::c_char {
            if let Some(ref v) = s {
                let cs = CString::new(v.as_str()).unwrap();
                let p = cs.as_ptr();
                strings.push(cs);
                p
            } else {
                std::ptr::null()
            }
        };

        let mut c = unsafe {
            let mut c = std::mem::zeroed::<baresdk_config_t>();
            baresdk_config_init(&mut c);
            c
        };

        c.log_level         = cfg.log_level;
        c.transport         = cfg.transport;
        c.server_port       = cfg.server_port;
        c.ice_enabled       = cfg.ice_enabled as i32;
        c.verify_server     = cfg.verify_server as i32;
        c.trace_sip         = cfg.trace_sip as i32;
        c.stats_interval_ms = cfg.stats_interval_ms;
        c.server_host       = cstr(&cfg.server_host);
        c.server_url        = cstr(&cfg.server_url);
        c.sip_domain        = cstr(&cfg.sip_domain);
        c.stun_server       = cstr(&cfg.stun_server);
        c.turn_server       = cstr(&cfg.turn_server);
        c.turn_user         = cstr(&cfg.turn_user);
        c.turn_pass         = cstr(&cfg.turn_pass);
        c.ca_cert_path      = cstr(&cfg.ca_cert_path);
        c.event_cb          = Some(global_event_cb);
        c.event_userdata    = std::ptr::null_mut();

        check(unsafe { baresdk_init(&c) })?;

        Ok((Self { _cfg_strings: strings }, rx))
    }

    pub fn create_account(&self, cfg: AccountConfig)
        -> Result<Account, Error>
    {
        Account::create(cfg)
    }

    pub fn pcap_start(&self, path: &str) -> Result<(), Error> {
        let p = CString::new(path).unwrap();
        check(unsafe { baresdk_pcap_start(p.as_ptr()) })
    }

    pub fn pcap_stop(&self) {
        unsafe { baresdk_pcap_stop(); }
    }

    pub fn version(&self) -> &str {
        unsafe { std::ffi::CStr::from_ptr(baresdk_version()).to_str().unwrap_or("?") }
    }
}

impl Drop for SDK {
    fn drop(&mut self) {
        unsafe { baresdk_shutdown(); }
        let mut guard = SENDER.lock().unwrap();
        *guard = None;
    }
}
