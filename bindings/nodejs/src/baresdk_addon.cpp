/**
 * baresdk_addon.cpp — Node.js N-API native addon for baresdk.
 *
 * Exposes SDK, Account, and Call as JavaScript classes.
 * Events are delivered via Node.js thread-safe function (napi_threadsafe_function)
 * which posts decoded event objects to the JS event emitter on the main thread.
 */

#include <napi.h>
#include <string>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "../../../include/baresdk.h"

/* ── helpers ──────────────────────────────────────────────────────────────── */

static std::string cstr_or_empty(const char* s) {
    return s ? std::string(s) : std::string();
}

/* ── Thread-safe event bridge ─────────────────────────────────────────────── */

struct JsEventContext {
    Napi::ThreadSafeFunction tsfn;
};

static JsEventContext* g_ev_ctx = nullptr;
static std::mutex       g_tsfn_mu;

struct EventData {
    std::string json; // JSON-serialised event
};

/* Serialise a C event to JSON (minimal, no dependencies) */
static std::string event_to_json(const baresdk_event_t* ev) {
    std::string j = "{";
    auto qs = [](const char* s) -> std::string {
        if (!s) return "null";
        std::string r = "\"";
        for (const char* p = s; *p; ++p) {
            if (*p == '"' || *p == '\\') r += '\\';
            r += *p;
        }
        r += '"';
        return r;
    };

    switch (ev->type) {
    case BARESDK_EV_LOG:
        j += "\"type\":\"log\",\"message\":" + qs(ev->u.log.message);
        break;
    case BARESDK_EV_REG_STATE:
        j += "\"type\":\"reg_state\",\"state\":" + std::to_string(ev->u.reg.state)
           + ",\"error\":" + std::to_string(ev->u.reg.error)
           + ",\"errorStr\":" + qs(ev->u.reg.error_str)
           + ",\"retryAttempt\":" + std::to_string(ev->u.reg.retry_attempt);
        break;
    case BARESDK_EV_INCOMING_CALL:
        j += "\"type\":\"incoming_call\""
             ",\"callHandle\":" + std::to_string((uintptr_t)ev->u.incoming.call)
           + ",\"fromUri\":"    + qs(ev->u.incoming.from_uri)
           + ",\"displayName\":" + qs(ev->u.incoming.display_name);
        break;
    case BARESDK_EV_CALL_STATE:
        j += "\"type\":\"call_state\""
             ",\"callHandle\":" + std::to_string((uintptr_t)ev->u.call_state.call)
           + ",\"state\":"  + std::to_string(ev->u.call_state.state)
           + ",\"reason\":" + qs(ev->u.call_state.reason);
        break;
    case BARESDK_EV_CALL_DTMF:
        j += "\"type\":\"dtmf\""
             ",\"callHandle\":" + std::to_string((uintptr_t)ev->u.dtmf.call)
           + ",\"digit\":\"" + std::string(1, ev->u.dtmf.digit) + "\"";
        break;
    case BARESDK_EV_MEDIA_STATS: {
        const auto& s = ev->u.stats;
        char buf[512];
        snprintf(buf, sizeof(buf),
            "\"type\":\"media_stats\""
            ",\"callHandle\":%zu"
            ",\"packetsLost\":%u,\"lossPct\":%.2f"
            ",\"jitterMs\":%.2f,\"rttMs\":%.2f"
            ",\"mosLq\":%.2f,\"mosCq\":%.2f"
            ",\"bandwidthTx\":%u,\"bandwidthRx\":%u",
            (size_t)s.call,
            s.packets_lost, s.loss_pct,
            s.jitter_ms, s.rtt_ms,
            s.mos_lq, s.mos_cq,
            s.bandwidth_kbps_tx, s.bandwidth_kbps_rx);
        j += buf;
        j += ",\"codec\":" + qs(s.codec_name);
        break;
    }
    case BARESDK_EV_SIP_TRACE:
        j += "\"type\":\"sip_trace\""
             ",\"dir\":" + std::to_string(ev->u.sip_trace.dir)
           + ",\"transport\":" + qs(ev->u.sip_trace.transport)
           + ",\"remoteAddr\":" + qs(ev->u.sip_trace.remote_addr)
           + ",\"rawMessage\":" + qs(ev->u.sip_trace.raw_message);
        break;
    case BARESDK_EV_MESSAGE:
        j += "\"type\":\"message\""
             ",\"fromUri\":" + qs(ev->u.msg.from_uri)
           + ",\"body\":"    + qs(ev->u.msg.body)
           + ",\"contentType\":" + qs(ev->u.msg.content_type);
        break;
    default:
        j += "\"type\":\"unknown\",\"typeCode\":" + std::to_string(ev->type);
        break;
    }
    j += "}";
    return j;
}

static void event_cb(const baresdk_event_t* ev, void* /*ud*/) {
    std::lock_guard<std::mutex> lk(g_tsfn_mu);
    if (!g_ev_ctx) return;

    auto* data = new EventData{ event_to_json(ev) };
    g_ev_ctx->tsfn.NonBlockingCall(data, [](Napi::Env env, Napi::Function jsCallback, EventData* d) {
        auto json_str = Napi::String::New(env, d->json);
        delete d;
        jsCallback.Call({ json_str });
    });
}

/* ── Call class ───────────────────────────────────────────────────────────── */

class JsCall : public Napi::ObjectWrap<JsCall> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        auto func = DefineClass(env, "Call", {
            InstanceMethod("answer",   &JsCall::Answer),
            InstanceMethod("hangup",   &JsCall::Hangup),
            InstanceMethod("hold",     &JsCall::Hold),
            InstanceMethod("resume",   &JsCall::Resume),
            InstanceMethod("sendDtmf", &JsCall::SendDtmf),
            InstanceMethod("transfer", &JsCall::Transfer),
            InstanceMethod("mute",     &JsCall::Mute),
        });
        constructor() = Napi::Persistent(func);
        constructor().SuppressDestruct();
        exports.Set("Call", func);
        return exports;
    }

    explicit JsCall(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<JsCall>(info) {
        if (info.Length() >= 1 && info[0].IsNumber()) {
            uintptr_t ptr = (uintptr_t)info[0].As<Napi::Number>().Int64Value();
            handle_ = (baresdk_call_handle_t)ptr;
        }
    }

    static Napi::FunctionReference& constructor() {
        static Napi::FunctionReference inst;
        return inst;
    }

    static Napi::Object New(Napi::Env env, baresdk_call_handle_t h) {
        return constructor().New({ Napi::Number::New(env, (double)(uintptr_t)h) });
    }

    baresdk_call_handle_t handle() const { return handle_; }

private:
    Napi::Value Answer(const Napi::CallbackInfo& i)   { baresdk_call_answer(handle_);  return i.Env().Undefined(); }
    Napi::Value Hangup(const Napi::CallbackInfo& i)   { baresdk_call_hangup(handle_);  return i.Env().Undefined(); }
    Napi::Value Hold(const Napi::CallbackInfo& i)     { baresdk_call_hold(handle_);    return i.Env().Undefined(); }
    Napi::Value Resume(const Napi::CallbackInfo& i)   { baresdk_call_resume(handle_);  return i.Env().Undefined(); }
    Napi::Value Mute(const Napi::CallbackInfo& i) {
        bool m = i.Length() >= 1 ? i[0].As<Napi::Boolean>().Value() : true;
        baresdk_audio_mute(handle_, m);
        return i.Env().Undefined();
    }
    Napi::Value SendDtmf(const Napi::CallbackInfo& i) {
        if (i.Length() >= 1) {
            std::string d = i[0].As<Napi::String>().Utf8Value();
            if (!d.empty()) baresdk_call_send_dtmf(handle_, d[0]);
        }
        return i.Env().Undefined();
    }
    Napi::Value Transfer(const Napi::CallbackInfo& i) {
        if (i.Length() >= 1) {
            std::string uri = i[0].As<Napi::String>().Utf8Value();
            baresdk_call_transfer(handle_, uri.c_str());
        }
        return i.Env().Undefined();
    }

    baresdk_call_handle_t handle_ = nullptr;
};

/* ── Account class ────────────────────────────────────────────────────────── */

class JsAccount : public Napi::ObjectWrap<JsAccount> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        auto func = DefineClass(env, "Account", {
            InstanceMethod("register",   &JsAccount::Register),
            InstanceMethod("unregister", &JsAccount::Unregister),
            InstanceMethod("call",       &JsAccount::Call_),
            InstanceMethod("sendMessage",&JsAccount::SendMessage),
        });
        constructor() = Napi::Persistent(func);
        constructor().SuppressDestruct();
        exports.Set("Account", func);
        return exports;
    }

    explicit JsAccount(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<JsAccount>(info), handle_(nullptr) {}

    static Napi::FunctionReference& constructor() {
        static Napi::FunctionReference inst;
        return inst;
    }

    void SetHandle(baresdk_account_handle_t h) { handle_ = h; }

    ~JsAccount() { if (handle_) baresdk_account_destroy(handle_); }

private:
    Napi::Value Register(const Napi::CallbackInfo& i) {
        int rc = baresdk_account_register(handle_);
        return Napi::Number::New(i.Env(), rc);
    }
    Napi::Value Unregister(const Napi::CallbackInfo& i) {
        int rc = baresdk_account_unregister(handle_);
        return Napi::Number::New(i.Env(), rc);
    }
    Napi::Value Call_(const Napi::CallbackInfo& i) {
        if (i.Length() < 1) return i.Env().Null();
        std::string uri = i[0].As<Napi::String>().Utf8Value();
        baresdk_call_handle_t ch = nullptr;
        int rc = baresdk_call_invite(handle_, uri.c_str(), &ch);
        if (rc != BARESDK_OK) Napi::Error::New(i.Env(), "call_invite failed").ThrowAsJavaScriptException();
        return JsCall::New(i.Env(), ch);
    }
    Napi::Value SendMessage(const Napi::CallbackInfo& i) {
        if (i.Length() < 2) return i.Env().Undefined();
        std::string to   = i[0].As<Napi::String>().Utf8Value();
        std::string body = i[1].As<Napi::String>().Utf8Value();
        std::string ct   = (i.Length() >= 3) ? i[2].As<Napi::String>().Utf8Value() : "text/plain";
        baresdk_message_send(handle_, to.c_str(), body.c_str(), ct.c_str());
        return i.Env().Undefined();
    }

    baresdk_account_handle_t handle_;
};

/* ── SDK class ────────────────────────────────────────────────────────────── */

class JsSdk : public Napi::ObjectWrap<JsSdk> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        auto func = DefineClass(env, "SDK", {
            InstanceMethod("createAccount", &JsSdk::CreateAccount),
            InstanceMethod("shutdown",      &JsSdk::Shutdown),
            InstanceMethod("version",       &JsSdk::Version),
        });
        constructor() = Napi::Persistent(func);
        constructor().SuppressDestruct();
        exports.Set("SDK", func);
        return exports;
    }

    explicit JsSdk(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<JsSdk>(info) {
        Napi::Env env = info.Env();

        if (info.Length() < 1 || !info[0].IsFunction()) {
            Napi::Error::New(env, "SDK(eventCallback) requires a function argument")
                .ThrowAsJavaScriptException();
            return;
        }

        auto cb_fn = info[0].As<Napi::Function>();

        // Set up thread-safe function so C callback can post to JS
        auto ctx = std::make_unique<JsEventContext>();
        ctx->tsfn = Napi::ThreadSafeFunction::New(
            env, cb_fn, "baresdk_event", 0, 1, [](Napi::Env) {});
        {
            std::lock_guard<std::mutex> lk(g_tsfn_mu);
            delete g_ev_ctx;
            g_ev_ctx = ctx.release();
        }

        // Init SDK
        baresdk_config_t cfg;
        baresdk_config_init(&cfg);

        if (info.Length() >= 2 && info[1].IsObject()) {
            auto opts = info[1].As<Napi::Object>();
            if (opts.Has("logLevel"))       cfg.log_level         = opts.Get("logLevel").As<Napi::Number>().Int32Value();
            if (opts.Has("statsInterval"))  cfg.stats_interval_ms = opts.Get("statsInterval").As<Napi::Number>().Uint32Value();
            if (opts.Has("traceSip"))       cfg.trace_sip         = opts.Get("traceSip").As<Napi::Boolean>().Value();
        }

        cfg.event_cb       = event_cb;
        cfg.event_userdata = nullptr;

        int rc = baresdk_init(&cfg);
        if (rc != BARESDK_OK) {
            Napi::Error::New(env, "baresdk_init failed: " + std::to_string(rc))
                .ThrowAsJavaScriptException();
        }
    }

    ~JsSdk() {
        baresdk_shutdown();
        std::lock_guard<std::mutex> lk(g_tsfn_mu);
        if (g_ev_ctx) {
            g_ev_ctx->tsfn.Release();
            delete g_ev_ctx;
            g_ev_ctx = nullptr;
        }
    }

    static Napi::FunctionReference& constructor() {
        static Napi::FunctionReference inst;
        return inst;
    }

private:
    Napi::Value CreateAccount(const Napi::CallbackInfo& i) {
        Napi::Env env = i.Env();
        if (i.Length() < 1 || !i[0].IsObject())
            Napi::TypeError::New(env, "createAccount(config) expected object").ThrowAsJavaScriptException();

        auto opts = i[0].As<Napi::Object>();
        std::string uri  = opts.Get("uri").As<Napi::String>().Utf8Value();
        std::string pass = opts.Get("password").As<Napi::String>().Utf8Value();

        baresdk_account_config_t acfg{};
        acfg.uri      = uri.c_str();
        acfg.password = pass.c_str();
        acfg.transport = BARESDK_TRANSPORT_UDP;

        if (opts.Has("transport")) acfg.transport = (baresdk_transport_t)opts.Get("transport").As<Napi::Number>().Int32Value();

        std::string server_url, server_host;
        if (opts.Has("serverUrl"))  { server_url  = opts.Get("serverUrl").As<Napi::String>().Utf8Value();  acfg.server_url  = server_url.c_str(); }
        if (opts.Has("serverHost")) { server_host = opts.Get("serverHost").As<Napi::String>().Utf8Value(); acfg.server_host = server_host.c_str(); }

        baresdk_account_handle_t h = nullptr;
        int rc = baresdk_account_create(&acfg, &h);
        if (rc != BARESDK_OK) {
            Napi::Error::New(env, "account_create failed: " + std::to_string(rc)).ThrowAsJavaScriptException();
            return env.Undefined();
        }

        auto js_acct = JsAccount::constructor().New({});
        JsAccount::Unwrap(js_acct)->SetHandle(h);
        return js_acct;
    }

    Napi::Value Shutdown(const Napi::CallbackInfo& i) {
        baresdk_shutdown();
        return i.Env().Undefined();
    }

    Napi::Value Version(const Napi::CallbackInfo& i) {
        return Napi::String::New(i.Env(), baresdk_version());
    }
};

/* ── Module init ──────────────────────────────────────────────────────────── */

Napi::Object InitModule(Napi::Env env, Napi::Object exports) {
    JsCall::Init(env, exports);
    JsAccount::Init(env, exports);
    JsSdk::Init(env, exports);

    // Export transport constants
    auto t = Napi::Object::New(env);
    t.Set("UDP", Napi::Number::New(env, BARESDK_TRANSPORT_UDP));
    t.Set("TCP", Napi::Number::New(env, BARESDK_TRANSPORT_TCP));
    t.Set("TLS", Napi::Number::New(env, BARESDK_TRANSPORT_TLS));
    t.Set("WS",  Napi::Number::New(env, BARESDK_TRANSPORT_WS));
    t.Set("WSS", Napi::Number::New(env, BARESDK_TRANSPORT_WSS));
    exports.Set("Transport", t);

    return exports;
}

NODE_API_MODULE(baresdk, InitModule)
