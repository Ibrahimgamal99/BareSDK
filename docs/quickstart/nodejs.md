# Quick start — Node.js

## Prerequisites

```bash
# Ubuntu/Debian
sudo apt install nodejs npm libssl3 zlib1g

# Fedora/RHEL
sudo dnf install nodejs npm openssl-libs zlib
```

Node.js 18 or newer is required.

---

## One-command setup

```bash
bash bindings/nodejs/build.sh
```

This builds the SDK if needed, installs Node.js dependencies, and compiles the native addon. The shared library path and rpath are resolved automatically — no `LD_LIBRARY_PATH` required at runtime.

---

## Manual setup (alternative)

```bash
# 1. Build the SDK
bash scripts/build-linux.sh          # Linux  → dist/linux/x86_64/baresdk.so
bash scripts/build-macos.sh          # macOS  → dist/macos/universal/baresdk.dylib
.\scripts\build-windows.ps1          # Windows → dist\windows\x64\baresdk.dll

# 2. Build the addon
cd bindings/nodejs
npm install
```

To point at a custom SDK location:
```bash
BARESDK_DIST_DIR=/path/to/sdk npm install
```

---

## Register and handle events

```js
const { SDK, Transport } = require('./bindings/nodejs');

const sdk = new SDK({ logLevel: 1, statsInterval: 5000 });
console.log('version:', sdk.version);

const account = sdk.createAccount({
    uri:      'alice@pbx.example.com',
    password: 'secret',
    transport: Transport.TLS,
});

account.register();

sdk.on('registered', () => {
    console.log('Registered!');
    const call = account.call('bob@pbx.example.com');
});

sdk.on('incomingCall', (ev) => {
    console.log('Incoming from', ev.fromUri);
});

sdk.on('callState', (ev) => {
    if (ev.state === 4 /* ENDED */) {
        console.log('Call ended');
        sdk.shutdown();
    }
});

sdk.on('mediaStats', (ev) => {
    console.log(`MOS: ${ev.mosLq.toFixed(2)}  RTT: ${ev.rttMs.toFixed(0)} ms`);
});

process.on('SIGINT', () => { sdk.shutdown(); process.exit(); });
```

---

## TypeScript

```ts
import { SDK, Account, Call, Transport, SDKOptions } from 'baresdk';

const sdk = new SDK({ logLevel: 1 } as SDKOptions);
const account: Account = sdk.createAccount({
    uri:      'alice@pbx.example.com',
    password: 'secret',
});

sdk.on('registered', () => {
    const call: Call = account.call('bob@pbx.example.com');
    setTimeout(() => call.hangup(), 10_000);
});
```

---

## See also
- Full example: [bindings/nodejs/examples/quickstart.js](../../bindings/nodejs/examples/quickstart.js)
- TypeScript declarations: [bindings/nodejs/lib/index.d.ts](../../bindings/nodejs/lib/index.d.ts)
