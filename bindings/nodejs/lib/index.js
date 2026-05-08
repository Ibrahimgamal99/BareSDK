'use strict';
/**
 * baresdk — Node.js binding.
 *
 * Wraps the native N-API addon and adds an EventEmitter-style API.
 *
 *   const { SDK, Transport } = require('baresdk');
 *
 *   const sdk = new SDK({ logLevel: 1, statsInterval: 5000 });
 *   const account = sdk.createAccount({ uri: 'alice@pbx.example.com', password: 'secret' });
 *   account.register();
 *
 *   sdk.on('registered',   ()   => console.log('Registered!'));
 *   sdk.on('incomingCall', (ev) => ev.call.answer());
 *   sdk.on('callState',    (ev) => { if (ev.state === 4) console.log('call ended'); });
 *   sdk.on('mediaStats',   (ev) => console.log('MOS', ev.mosLq));
 */

const EventEmitter = require('events');
const path         = require('path');

// Load native addon
const addon = (() => {
    try {
        return require('../build/Release/baresdk');
    } catch (_) {
        return require('../build/Debug/baresdk');
    }
})();

const { Transport } = addon;

/* ── Call wrapper ─────────────────────────────────────────────────────────── */
class Call {
    constructor(nativeCall) {
        this._native = nativeCall;
    }
    answer()              { this._native.answer();         }
    hangup()              { this._native.hangup();         }
    hold()                { this._native.hold();           }
    resume()              { this._native.resume();         }
    sendDtmf(digit)       { this._native.sendDtmf(digit);  }
    transfer(uri)         { this._native.transfer(uri);    }
    mute(on = true)       { this._native.mute(on);         }
}

/* ── Account wrapper ──────────────────────────────────────────────────────── */
class Account {
    constructor(nativeAcct) {
        this._native = nativeAcct;
    }
    register()                          { return this._native.register();             }
    unregister()                        { return this._native.unregister();           }
    call(uri)                           { return new Call(this._native.call(uri));    }
    sendMessage(to, body, ct)           { this._native.sendMessage(to, body, ct);     }
}

/* ── SDK wrapper ──────────────────────────────────────────────────────────── */
class SDK extends EventEmitter {
    /**
     * @param {object} [opts]
     * @param {number} [opts.logLevel=1]          0=err 1=warn 2=info 3=debug
     * @param {number} [opts.statsInterval=0]     ms; 0=disabled
     * @param {boolean}[opts.traceSip=false]
     */
    constructor(opts = {}) {
        super();
        this._native = new addon.SDK((jsonStr) => this._onEvent(jsonStr), opts);
    }

    /** Create and return an Account (does not register automatically). */
    createAccount(config) {
        return new Account(this._native.createAccount(config));
    }

    /** SDK version string. */
    get version() { return this._native.version(); }

    /** Graceful shutdown. */
    shutdown() { this._native.shutdown(); }

    // ── internal event router ──────────────────────────────────────────────
    _onEvent(jsonStr) {
        let ev;
        try { ev = JSON.parse(jsonStr); } catch (_) { return; }

        switch (ev.type) {
        case 'log':           this.emit('log',           ev); break;
        case 'reg_state':     this._onRegState(ev);           break;
        case 'incoming_call': this._onIncomingCall(ev);       break;
        case 'call_state':    this.emit('callState',     ev); break;
        case 'dtmf':          this.emit('dtmf',          ev); break;
        case 'media_stats':   this.emit('mediaStats',    ev); break;
        case 'sip_trace':     this.emit('sipTrace',      ev); break;
        case 'message':       this.emit('message',       ev); break;
        default:              this.emit('event',         ev); break;
        }
    }

    _onRegState(ev) {
        // REGISTERED=2, FAILED=3
        if      (ev.state === 2) this.emit('registered',         ev);
        else if (ev.state === 3) this.emit('registrationFailed', ev);
        else                     this.emit('regState',           ev);
    }

    _onIncomingCall(ev) {
        // We can't wrap the native call here because we don't have a native
        // Call handle object — consumers use callHandle (uintptr_t) to match
        // their Call objects. For incoming calls a helper is provided.
        this.emit('incomingCall', ev);
    }
}

module.exports = { SDK, Account, Call, Transport };
