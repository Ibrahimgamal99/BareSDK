/**
 * quickstart.cpp — register an account and make or receive one call.
 *
 * Build (Linux) — only needs baresdk.hpp + baresdk.so (OpenSSL/zlib baked in):
 *   g++ -std=c++17 quickstart.cpp baresdk.so -I. -o quickstart
 *
 * Usage:
 *   ./quickstart alice@pbx.example.com secret              # receive mode
 *   ./quickstart alice@pbx.example.com secret bob@pbx.example.com  # dial
 */

#include "../baresdk.hpp"
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>

static void print_devices(baresdk::SDK& sdk)
{
    baresdk_audio_device_t devs[32];

    int n = baresdk_audio_list_input_devices(devs, 32);
    if (n > 0) {
        std::cout << "Input devices (" << n << "):\n";
        for (int i = 0; i < n; i++)
            std::cout << "  [" << i << "] " << devs[i].name
                      << (devs[i].is_default ? "  *default*" : "") << "\n";
    }

    n = baresdk_audio_list_output_devices(devs, 32);
    if (n > 0) {
        std::cout << "Output devices (" << n << "):\n";
        for (int i = 0; i < n; i++)
            std::cout << "  [" << i << "] " << devs[i].name
                      << (devs[i].is_default ? "  *default*" : "") << "\n";
    }
}

static void print_stats(const baresdk_ev_media_stats_t& s)
{
    const char* method = (s.mos_method == BARESDK_MOS_EMODEL) ? "E-model" : "simplified";

    std::cout
        << "┌─ Media Stats ─────────────────────────────────\n"
        << "│  Codec     : " << (s.codec_name ? s.codec_name : "?")
        <<   "  " << s.codec_clock_rate / 1000 << " kHz"
        <<   "  ch=" << (int)s.codec_channels
        <<   "  PT=" << s.payload_type << "\n"
        << "│  Remote    : " << s.remote_addr
        <<   "  SSRC rx=" << s.ssrc_rx << "  tx=" << s.ssrc_tx << "\n"
        << "│  Packets   : tx=" << s.packets_sent
        <<   "  rx=" << s.packets_received
        <<   "  lost_tx=" << s.packets_lost << " (" << s.loss_pct << "%)"
        <<   "  lost_rx=" << s.packets_lost_rx << " (" << s.loss_pct_rx << "%)\n"
        << "│  Bandwidth : tx=" << s.bandwidth_kbps_tx << " kbps"
        <<   "  rx=" << s.bandwidth_kbps_rx << " kbps"
        <<   "  (avg tx=" << s.avg_bandwidth_kbps_tx
        <<   "  rx=" << s.avg_bandwidth_kbps_rx << ")\n"
        << "│  Delay     : RTT=" << s.rtt_ms << " ms"
        <<   "  jitter=" << s.jitter_ms << " ms"
        <<   "  tx_jitter=" << s.tx_jitter_ms << " ms\n"
        << "│  Jitter buf: depth=" << s.jitter_buffer_ms << " ms"
        <<   "  load=" << s.jitter_buffer_load
        <<   "  late=" << s.late_packets
        <<   "  discarded=" << s.discarded_packets << "\n"
        << "│  MOS (" << method << "): LQ=" << s.mos_lq << "  CQ=" << s.mos_cq << "\n";

    if (!std::isnan(s.audio_level_dbov))
        std::cout << "│  Level     : " << s.audio_level_dbov << " dBov\n";

    std::cout << "└───────────────────────────────────────────────\n";
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "usage: quickstart <sip-uri> <password> [<callee-uri>]\n";
        return 1;
    }

    const char* sip_uri   = argv[1];
    const char* password  = argv[2];
    const char* callee    = (argc >= 4) ? argv[3] : nullptr;

    /* ── state shared between main and event callback ──────────────────── */
    std::mutex              mtx;
    std::condition_variable cv;
    bool                    registered       = false;
    bool                    call_established = false;
    bool                    call_done        = false;
    bool                    incoming_call    = false;
    baresdk::Call           active_call;

    /* ── SDK setup ──────────────────────────────────────────────────────── */
    baresdk::SDK sdk;
    sdk.config().log_level         = 2;   /* 0=err 1=warn 2=info 3=debug */
    sdk.config().stats_interval_ms = 5000;
    sdk.config().trace_sip         = true;

    sdk.on_event([&](const baresdk_event_t& ev) {
        switch (ev.type) {

        case BARESDK_EV_REG_STATE:
            if (ev.u.reg.state == BARESDK_REG_REGISTERED) {
                std::lock_guard<std::mutex> lk(mtx);
                registered = true;
                cv.notify_one();
            } else if (ev.u.reg.state == BARESDK_REG_FAILED) {
                std::cerr << "Registration failed: "
                          << (ev.u.reg.error_str ? ev.u.reg.error_str : "?") << "\n";
                std::lock_guard<std::mutex> lk(mtx);
                registered = true; /* unblock main */
                cv.notify_one();
            }
            break;

        case BARESDK_EV_INCOMING_CALL:
            std::cout << "\n=== Incoming call from "
                      << (ev.u.incoming.from_uri ? ev.u.incoming.from_uri : "unknown")
                      << " ===\n"
                      << "Press 'a' + Enter to answer, 'r' + Enter to reject\n";
            {
                std::lock_guard<std::mutex> lk(mtx);
                active_call = baresdk::Call(ev.u.incoming.call);
                incoming_call = true;
                cv.notify_one();
            }
            break;

        case BARESDK_EV_CALL_STATE: {
            static const char* kStateNames[] = {
                "CALLING","RINGING","ESTABLISHED","HELD","ENDED","CANCELLED","FAILED"
            };
            int si = ev.u.call_state.state;
            const char* sname = (si >= 0 && si <= 6) ? kStateNames[si] : "?";
            std::cout << "Call state: " << sname << " (" << si << ")";
            if (ev.u.call_state.reason && ev.u.call_state.reason[0])
                std::cout << "  reason=\"" << ev.u.call_state.reason << "\"";
            if (ev.u.call_state.error != BARESDK_OK)
                std::cout << "  error=" << ev.u.call_state.error;
            std::cout << "\n";
            if (ev.u.call_state.state == BARESDK_CALL_ESTABLISHED) {
                std::lock_guard<std::mutex> lk(mtx);
                call_established = true;
                cv.notify_one();
            } else if (ev.u.call_state.state == BARESDK_CALL_ENDED  ||
                       ev.u.call_state.state == BARESDK_CALL_FAILED ||
                       ev.u.call_state.state == BARESDK_CALL_CANCELLED) {
                std::lock_guard<std::mutex> lk(mtx);
                call_done = true;
                cv.notify_one();
            }
            break;
        }

        case BARESDK_EV_MEDIA_STATS:
            print_stats(ev.u.stats);
            break;

        case BARESDK_EV_LOG:
            if (ev.u.log.message)
                std::cerr << "[sdk] " << ev.u.log.message;
            break;

        case BARESDK_EV_SIP_TRACE:
            std::cout << (ev.u.sip_trace.dir == BARESDK_MEDIA_DIR_TX ? ">>>\n" : "<<<\n")
                      << ev.u.sip_trace.raw_message << "\n---\n";
            break;

        default: break;
        }
    });

    /* ── Register ───────────────────────────────────────────────────────── */
    auto account = sdk.create_account(sip_uri, password, BARESDK_TRANSPORT_UDP);
    account.register_account();

    {
        std::unique_lock<std::mutex> lk(mtx);
        cv.wait_for(lk, std::chrono::seconds(15), [&]{ return registered; });
    }

    print_devices(sdk);

    /* ── Dial or wait ───────────────────────────────────────────────────── */
    if (callee) {
        std::string callee_uri = callee;
        if (callee_uri.rfind("sip:", 0) != 0)
            callee_uri = "sip:" + callee_uri;
        std::cout << "Dialling " << callee_uri << " ...\n";
        active_call = account.call(callee_uri.c_str());

        std::unique_lock<std::mutex> lk(mtx);
        cv.wait_for(lk, std::chrono::seconds(30),
                    [&]{ return call_established || call_done; });
        if (!call_established) return 0;

        /* Hold the call for 10 s then hang up */
        lk.unlock();
        std::this_thread::sleep_for(std::chrono::seconds(10));
        active_call.hangup();

        std::unique_lock<std::mutex> lk2(mtx);
        cv.wait_for(lk2, std::chrono::seconds(5), [&]{ return call_done; });
    } else {
        std::cout << "Waiting for incoming call (30 s)...\n";
        std::unique_lock<std::mutex> lk(mtx);
        cv.wait_for(lk, std::chrono::seconds(30), [&]{ return incoming_call || call_done; });

        if (incoming_call) {
            lk.unlock();
            char choice = '\0';
            while (std::cin.get(choice)) {
                std::cin.ignore();
                if (choice == 'a' || choice == 'A') {
                    try { active_call.answer(); }
                    catch (const std::exception& e) {
                        std::cerr << "answer failed: " << e.what() << "\n";
                    }
                    break;
                } else if (choice == 'r' || choice == 'R') {
                    active_call.hangup();
                    break;
                }
            }
            lk.lock();
            cv.wait_for(lk, std::chrono::seconds(30), [&]{ return call_done; });
        }
    }

    return 0;
}
