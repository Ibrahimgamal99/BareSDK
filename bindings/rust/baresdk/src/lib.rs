/*!
baresdk — safe Rust wrapper for the baresdk C SDK.

# Quick start

```no_run
use baresdk::{SDK, Config, AccountConfig, Transport, event::Event};

let sdk = SDK::new(Config { log_level: 1, ..Config::default() })?;

let (account, events) = sdk.create_account(AccountConfig {
    uri:      "alice@pbx.example.com".into(),
    password: "secret".into(),
    ..AccountConfig::default()
})?;

account.register()?;

for ev in events {
    match ev {
        Event::Registered => {
            let call = account.call("bob@pbx.example.com")?;
        }
        Event::CallState { state, .. } => println!("call state: {:?}", state),
        _ => {}
    }
}
# Ok::<(), baresdk::Error>(())
```
*/

pub mod error;
pub mod event;
pub mod sdk;
pub mod account;
pub mod call;

pub use error::Error;
pub use sdk::{SDK, Config};
pub use account::{Account, AccountConfig};
pub use call::Call;

pub use baresdk_sys::{
    baresdk_transport_t as Transport,
    baresdk_media_enc_t as MediaEnc,
    baresdk_codec_t     as Codec,
    baresdk_call_state_t as CallState,
    baresdk_reg_state_t  as RegState,
    baresdk_presence_status_t as PresenceStatus,
};
