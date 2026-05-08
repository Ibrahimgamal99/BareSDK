//! quickstart.rs — register + make/receive a call with baresdk.
//!
//! Build:
//!   BARESDK_LIB_DIR=../../dist/linux/x86_64 cargo run --example quickstart -- \
//!       alice@pbx.example.com secret [bob@pbx.example.com]

use std::io::{self, BufRead};
use std::{env, time::Duration};

use baresdk::{
    account::AccountConfig,
    call::Call,
    event::Event,
    sdk::{Config, SDK},
    Transport,
};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<String> = env::args().collect();
    if args.len() < 3 {
        eprintln!("usage: quickstart <sip-uri> <password> [<callee-uri>]");
        std::process::exit(1);
    }

    let sip_uri  = &args[1];
    let password = &args[2];
    let callee   = args.get(3).map(|s| {
        if s.starts_with("sip:") || s.starts_with("sips:") {
            s.clone()
        } else {
            format!("sip:{}", s)
        }
    });

    // ── Init SDK ──────────────────────────────────────────────────────────
    let (sdk, events) = SDK::new(Config {
        log_level:         1,
        stats_interval_ms: 5000,
        ..Config::default()
    })?;

    println!("baresdk version: {}", sdk.version());

    // ── Create + register account ─────────────────────────────────────────
    let account = sdk.create_account(AccountConfig {
        uri:       sip_uri.clone(),
        password:  password.clone(),
        transport: Some(Transport::BARESDK_TRANSPORT_UDP),
        ..AccountConfig::default()
    })?;

    account.register()?;

    // ── Event loop ────────────────────────────────────────────────────────
    let mut active_call: Option<Call> = None;
    let mut incoming_pending = false;

    for ev in events.iter() {
        match ev {
            Event::Registered => {
                println!("Registered!");
                if let Some(ref target) = callee {
                    println!("Dialling {target} ...");
                    active_call = Some(account.call(target)?);
                } else {
                    println!("Waiting for incoming call ...");
                }
            }

            Event::RegistrationFailed { error_str, .. } => {
                eprintln!("Registration failed: {:?}", error_str);
                break;
            }

            Event::IncomingCall { call_handle, from_uri, .. } => {
                println!("\n=== Incoming call from {from_uri} ===");
                println!("Press 'a' + Enter to answer, 'r' + Enter to reject");
                let call = Call::from_ptr(*call_handle);
                active_call = Some(call);
                incoming_pending = true;
            }

            Event::CallState { state, .. } => {
                use baresdk_sys::baresdk_call_state_t_BARESDK_CALL_ESTABLISHED as EST;
                use baresdk_sys::baresdk_call_state_t_BARESDK_CALL_ENDED as END;
                use baresdk_sys::baresdk_call_state_t_BARESDK_CALL_FAILED as FAIL;

                println!("Call state: {state:?}");

                if *state == EST {
                    std::thread::sleep(Duration::from_secs(10));
                    if let Some(ref c) = active_call { c.hangup().ok(); }
                }
                if *state == END || *state == FAIL {
                    println!("Call ended.");
                    break;
                }
            }

            Event::MediaStats { mos_lq, rtt_ms, loss_pct, .. } => {
                println!("Stats — MOS-LQ: {mos_lq:.2}  RTT: {rtt_ms:.0} ms  loss: {loss_pct:.1}%");
            }

            _ => {}
        }

        if incoming_pending {
            let stdin = io::stdin();
            for line in stdin.lock().lines() {
                let line = line.unwrap_or_default();
                let choice = line.trim().to_lowercase();
                if choice == "a" {
                    if let Some(ref c) = active_call {
                        if let Err(e) = c.answer() {
                            eprintln!("answer failed: {e}");
                        }
                    }
                    break;
                } else if choice == "r" {
                    if let Some(ref c) = active_call {
                        c.hangup().ok();
                    }
                    break;
                }
            }
            incoming_pending = false;
        }
    }

    Ok(())
}
