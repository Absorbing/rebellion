// Mixxx Studio Bridge — v0.0 deliverable
//
// File: v0.0/tools/nihia-monitor/main.cpp
// Purpose: Test 0.0.2 / 0.0.2b (SPEC §1.5.1).
//
// Standalone CLI that links Rebellion's NIHIA core (librebellion), claims the
// Studio, and prints every NIHIA event to stdout in a human-readable line.
//
// Rebellion surfaces decoded events to a host callback as JSON of the shape:
//     { "event": "<NAME>", "data": { ... } }
// (see scripts/start.lua dispatcher '*' handler). The known shapes are:
//     PAD_DATA     -> { padid, pad, state, nstate, pressure, cpressure }
//     BTN_DATA     -> { button, buttonid, state, stategroups }
//     KNOB_ROTATE  -> { knob, direction, rotation, crotation }
//     device.state -> { serial, state }   (ON / OFF)
//     (unknown)    -> raw fields; we dump the whole object.
//
// Flags:
//   --device <NAME>        informational only; device selection is via config.lua
//   --filter A,B,C         only print these event names
//   --raw                  also dump the full JSON object alongside the parsed line
//   --csv                  emit CSV instead of aligned text
//
// NOTE: This file was authored against the Rebellion source in this repo but
// has NOT been compiled or run here (no Windows / NIHIA / hardware in the build
// environment). Build and run it on the Windows rig per v0.0/README.md.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

#include <json.hpp>
using json = nlohmann::json;

extern "C" {
#include "rebellion.h"
}

namespace {

bool        g_csv = false;
bool        g_raw = false;
std::set<std::string> g_filter; // empty => all events

std::atomic<bool> g_running{true};

std::string timestamp() {
    using namespace std::chrono;
    auto now  = system_clock::now();
    auto t    = system_clock::to_time_t(now);
    auto ms   = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", tmv.tm_hour, tmv.tm_min,
                  tmv.tm_sec, static_cast<int>(ms.count()));
    return buf;
}

// Best-effort field extraction; missing fields render as "-".
std::string field(const json& d, const char* key) {
    if (!d.contains(key) || d[key].is_null()) return "-";
    if (d[key].is_string()) return d[key].get<std::string>();
    std::ostringstream os;
    os << d[key];
    return os.str();
}

void printParsed(const std::string& ts, const std::string& ev, const json& d) {
    if (g_csv) {
        // ts,event,a,b,c,d  (generic columns; meaning depends on event)
        std::cout << ts << ',' << ev << ',';
        if (ev == "PAD_DATA")
            std::cout << field(d, "padid") << ',' << field(d, "state") << ','
                      << field(d, "pressure") << ',' << field(d, "cpressure");
        else if (ev == "BTN_DATA")
            std::cout << field(d, "buttonid") << ',' << field(d, "state") << ",,";
        else if (ev == "KNOB_ROTATE")
            std::cout << field(d, "knob") << ',' << field(d, "direction") << ','
                      << field(d, "rotation") << ',';
        else if (ev == "device.state")
            std::cout << field(d, "serial") << ',' << field(d, "state") << ",,";
        else
            std::cout << ",,," ;
        std::cout << '\n';
        return;
    }

    std::ostringstream line;
    line << '[' << ts << "] ";
    if (ev == "PAD_DATA") {
        line << "PAD_DATA    pad=" << field(d, "padid")
             << "  state=" << field(d, "state")
             << "  pressure=" << field(d, "pressure")
             << "  cpressure=" << field(d, "cpressure");
    } else if (ev == "BTN_DATA") {
        line << "BTN_DATA    btnid=" << field(d, "buttonid")
             << "  state=" << field(d, "state");
    } else if (ev == "KNOB_ROTATE") {
        line << "KNOB_ROTATE knob=" << field(d, "knob")
             << "  dir=" << field(d, "direction")
             << "  rot=" << field(d, "rotation");
    } else if (ev == "device.state") {
        line << "DEVICE      serial=" << field(d, "serial")
             << "  state=" << field(d, "state");
    } else {
        line << ev << "   raw=" << d.dump();
    }
    std::cout << line.str();
    // Always surface the raw data object too: the friendly fields above are
    // best-effort and the exact key names vary per event, but this guarantees
    // the actual values (ids, velocity, pressure) are never hidden.
    if (!d.empty()) std::cout << "    data=" << d.dump();
    std::cout << '\n';
}

int rpc_callback(rebellion_message_format mf, rebellion_message_type /*mt*/,
                 const uint8_t* udata, uint32_t /*len*/) {
    if (mf != REBELLION_MF_JSON) return 0;
    const char* data = reinterpret_cast<const char*>(udata);

    json j;
    try {
        j = json::parse(data);
    } catch (const std::exception& e) {
        std::cerr << "[parse error] " << e.what() << " :: " << data << '\n';
        return 0;
    }

    if (!j.contains("event")) return 0; // ignore RPC results, only events here
    const std::string ev = j["event"].is_string() ? j["event"].get<std::string>() : "";
    if (ev.empty()) return 0;

    if (!g_filter.empty() && g_filter.find(ev) == g_filter.end()) return 0;

    const json d = j.contains("data") ? j["data"] : json::object();
    printParsed(timestamp(), ev, d);
    std::cout.flush();
    return 0;
}

void onSignal(int) { g_running = false; }

void parseArgs(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--raw") {
            g_raw = true;
        } else if (a == "--csv") {
            g_csv = true;
        } else if (a == "--filter" && i + 1 < argc) {
            std::string list = argv[++i];
            std::stringstream ss(list);
            std::string item;
            while (std::getline(ss, item, ',')) {
                if (!item.empty()) g_filter.insert(item);
            }
        } else if (a == "--device" && i + 1 < argc) {
            ++i; // informational; selection is driven by config.lua
        } else if (a == "-h" || a == "--help") {
            std::cout << "nihia-monitor [--device NAME] [--filter A,B,C] [--raw] [--csv]\n";
            std::exit(0);
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    parseArgs(argc, argv);
    std::signal(SIGINT, onSignal);
#ifdef SIGTERM
    std::signal(SIGTERM, onSignal);
#endif

    if (g_csv) std::cout << "ts,event,c1,c2,c3,c4\n";
    std::cerr << "nihia-monitor: registering callback, claiming device "
                 "(per config.lua devices list)\n";

    rebellion(rpc_callback);

    // rebellion_loop(0) blocks and pumps NIHIA events into rpc_callback.
    // It runs until the process is signalled. We poll a short loop instead so
    // Ctrl+C exits cleanly; if your librebellion build blocks indefinitely in
    // rebellion_loop, just rely on Ctrl+C terminating the process.
    while (g_running) {
        rebellion_loop(100); // 100ms slices if supported; 0 = run-forever fallback
        if (!g_running) break;
    }

    std::cerr << "\nnihia-monitor: exiting\n";
    return 0;
}
