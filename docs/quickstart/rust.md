# Quick start — Rust

## Prerequisites

```bash
# Ubuntu/Debian
sudo apt install libssl-dev zlib1g-dev clang

# Fedora/RHEL
sudo dnf install openssl-devel zlib-devel clang
```

`clang` is required by `bindgen` to generate the FFI bindings.

---

## One-command setup

```bash
bash bindings/rust/build.sh
```

This builds the SDK if needed, then runs `cargo build --release`. The correct `dist/` path is detected automatically for your platform and architecture — no `BARESDK_LIB_DIR` export required.

---

## Manual setup (alternative)

```bash
# 1. Build the SDK
bash scripts/build-linux.sh          # Linux
bash scripts/build-macos.sh          # macOS
.\scripts\build-windows.ps1          # Windows

# 2. Build the Rust crate
cd bindings/rust
cargo build --release --examples
```

To point at a custom SDK location:
```bash
BARESDK_LIB_DIR=/path/to/sdk cargo build --release
```

---

## Add to your project

```toml
# Cargo.toml
[dependencies]
baresdk = { path = "bindings/rust/baresdk" }
```

---

## Register and handle events

```rust
use baresdk::{
    SDK, Config, AccountConfig, Transport,
    event::Event,
};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let (sdk, events) = SDK::new(Config {
        log_level:         1,
        stats_interval_ms: 5000,
        ..Config::default()
    })?;

    let account = sdk.create_account(AccountConfig {
        uri:       "alice@pbx.example.com".into(),
        password:  "secret".into(),
        transport: Some(Transport::BARESDK_TRANSPORT_TLS),
        ..AccountConfig::default()
    })?;

    account.register()?;

    for ev in events {
        match ev {
            Event::Registered => {
                println!("Registered!");
                let _call = account.call("bob@pbx.example.com")?;
            }
            Event::IncomingCall { call_handle, from_uri, .. } => {
                println!("Incoming from {from_uri}");
                baresdk::call::Call::from_ptr(call_handle).answer()?;
            }
            Event::CallState { state, .. } => {
                println!("Call state: {state:?}");
            }
            Event::MediaStats { mos_lq, rtt_ms, .. } => {
                println!("MOS: {mos_lq:.2}  RTT: {rtt_ms:.0} ms");
            }
            _ => {}
        }
    }
    Ok(())
}
```

---

## Run the example

```bash
cargo run --manifest-path bindings/rust/Cargo.toml \
          --example quickstart -- alice@pbx.example.com secret

# Dial outbound
cargo run --manifest-path bindings/rust/Cargo.toml \
          --example quickstart -- alice@pbx.example.com secret bob@pbx.example.com
```

---

## See also
- Full example: [bindings/rust/baresdk/examples/quickstart.rs](../../bindings/rust/baresdk/examples/quickstart.rs)
- `baresdk-sys`: raw bindgen bindings in [bindings/rust/baresdk-sys/](../../bindings/rust/baresdk-sys/)
