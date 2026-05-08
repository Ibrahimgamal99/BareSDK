'use strict';
/**
 * quickstart.js — register an account and make or receive one call.
 *
 * Prerequisites:
 *   cd bindings/nodejs && npm install && npm run build
 *
 * Usage:
 *   node quickstart.js alice@pbx.example.com secret               # receive
 *   node quickstart.js alice@pbx.example.com secret bob@pbx.example.com  # dial
 */

const readline = require('readline');
const { SDK, Transport } = require('..');

const [,, sipUri, password, callee] = process.argv;
if (!sipUri || !password) {
    console.error('usage: quickstart.js <sip-uri> <password> [<callee>]');
    process.exit(1);
}

let calleeUri = callee;
if (calleeUri && !calleeUri.startsWith('sip:')) {
    calleeUri = 'sip:' + calleeUri;
}

const sdk = new SDK({ logLevel: 1, statsInterval: 5000 });
console.log('baresdk version:', sdk.version);

const account = sdk.createAccount({
    uri:      sipUri,
    password: password,
    transport: Transport.UDP,
});

account.register();
let activeCall = null;

sdk.on('registered', () => {
    console.log('Registered!');
    if (calleeUri) {
        console.log('Dialling', calleeUri, '...');
        activeCall = account.call(calleeUri);
    } else {
        console.log('Waiting for incoming call ...');
    }
});

sdk.on('registrationFailed', (ev) => {
    console.error('Registration failed:', ev.errorStr);
    sdk.shutdown();
});

sdk.on('incomingCall', (ev) => {
    console.log(`\n=== Incoming call from ${ev.fromUri || 'unknown'} ===`);
    console.log("Press 'a' + Enter to answer, 'r' + Enter to reject");
    activeCall = ev.callHandle;

    const rl = readline.createInterface({ input: process.stdin });
    rl.on('line', (line) => {
        const choice = line.trim().toLowerCase();
        if (choice === 'a') {
            if (activeCall) activeCall.answer();
            rl.close();
        } else if (choice === 'r') {
            if (activeCall) activeCall.hangup();
            rl.close();
        }
    });
});

sdk.on('callState', (ev) => {
    console.log('Call state:', ev.state);
    if (ev.state === 2 /* ESTABLISHED */) {
        setTimeout(() => {
            if (activeCall) activeCall.hangup();
        }, 10000);
    }
    if (ev.state === 4 /* ENDED */ || ev.state === 6 /* FAILED */) {
        console.log('Call done.');
        sdk.shutdown();
    }
});

sdk.on('mediaStats', (ev) => {
    console.log(`Stats — MOS-LQ: ${ev.mosLq.toFixed(2)}  RTT: ${ev.rttMs.toFixed(0)} ms  loss: ${ev.lossPct.toFixed(1)}%`);
});

// Graceful exit on Ctrl+C
process.on('SIGINT', () => { sdk.shutdown(); process.exit(0); });
