// Mixxx Studio Bridge — v0.0 deliverable
//
// File: v0.0/tools/display-probe/main.cpp
// Purpose: Test 0.0.3 (SPEC §0 Session 3, §1.4) — the load-bearing risk test.
//
// Builds a 480x272 RGB565 framebuffer of alternating black/white vertical
// columns (10px wide each) and sends it to display 0, waits 2s, then sends the
// same pattern to display 1 — using Rebellion's existing display path via the
// JSON-RPC method "rebellion.sendDataToDisplay" (see scripts/rpc.lua:47).
//
// The Studio's serial is learned at runtime from the "device.state" ON event.
// sendDataToDisplay accepts a *flat* pixel array (scripts/rpc.lua: the `else`
// branch sets warr = data), so we send one RGB565 integer per pixel in
// row-major order (width*height = 480*272 = 130560 ints).
//
// REQUIRES the mappings.lua Studio entry to carry display config
// (ledcnt=103, dcnt=2, dheight=272, dwidth=480) — already applied in this repo
// for v0.0. Without it the display command builder has no width/height.
//
// ── Outcomes (record in V00_RESULTS.md) ───────────────────────────────────
//   A: pattern appears correctly on both panels  -> protocol generalises.
//   B: no error logged but panels dark/garbage    -> silent route failure.
//   C: pipe disconnect / NIHIA error / crash      -> hard protocol mismatch.
//
// CAVEAT (verify on hardware): this issues the display RPC from *inside* the
// event callback, which re-enters Rebellion's Lua RPC layer while a dispatch is
// in flight. If that hangs or misbehaves (the SPEC §2 warns about a "sync-push
// hang"), switch SEND_FROM_CALLBACK to 0 to send from main instead, and tune
// the startup delay. This file has NOT been compiled or run here.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <json.hpp>
using json = nlohmann::json;

extern "C" {
#include "rebellion.h"
}

namespace {

constexpr int  WIDTH       = 480;
constexpr int  HEIGHT      = 272;
constexpr int  COL_WIDTH   = 10;       // px per black/white column
constexpr uint16_t WHITE   = 0xFFFF;   // RGB565
constexpr uint16_t BLACK   = 0x0000;
constexpr int  SEND_FROM_CALLBACK = 1; // see CAVEAT above

std::atomic<bool> g_sent{false};
std::string       g_serial;

// Row-major flat RGB565 framebuffer: vertical stripes alternating B/W.
std::vector<int> buildPattern() {
    std::vector<int> fb;
    fb.reserve(static_cast<size_t>(WIDTH) * HEIGHT);
    for (int y = 0; y < HEIGHT; ++y) {
        for (int x = 0; x < WIDTH; ++x) {
            const bool white = ((x / COL_WIDTH) % 2) == 1;
            fb.push_back(white ? WHITE : BLACK);
        }
    }
    return fb;
}

void sendToDisplay(const std::string& serial, int display, const std::vector<int>& fb) {
    json req = {
        {"method", "rebellion.sendDataToDisplay"},
        {"params", json::array({serial, display, fb})},
        {"id", display + 1},
    };
    const std::string s = req.dump();
    auto t0 = std::chrono::steady_clock::now();
    int rc = rebellion_rpc(REBELLION_MF_JSON, REBELLION_MT_REQ,
                           reinterpret_cast<const uint8_t*>(s.c_str()),
                           static_cast<uint32_t>(s.size()));
    auto t1 = std::chrono::steady_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    std::cerr << "display-probe: sent " << fb.size() << " px to display " << display
              << " (rebellion_rpc rc=" << rc << ", call took " << us << " us)\n";
}

void runProbe(const std::string& serial) {
    const std::vector<int> fb = buildPattern();
    std::cerr << "display-probe: serial=" << serial
              << " — sending pattern to display 0\n";
    sendToDisplay(serial, 0, fb);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cerr << "display-probe: sending pattern to display 1\n";
    sendToDisplay(serial, 1, fb);
    std::cerr << "display-probe: done. OBSERVE BOTH PANELS and record outcome "
                 "A/B/C in V00_RESULTS.md.\n";
}

int rpc_callback(rebellion_message_format mf, rebellion_message_type /*mt*/,
                 const uint8_t* udata, uint32_t /*len*/) {
    if (mf != REBELLION_MF_JSON) return 0;
    json j;
    try {
        j = json::parse(reinterpret_cast<const char*>(udata));
    } catch (const std::exception& e) {
        std::cerr << "display-probe: parse error: " << e.what() << '\n';
        return 0;
    }

    // Log every response (NIHIA errors show up here as RPC results with "error").
    if (j.contains("error")) {
        std::cerr << "display-probe: NIHIA/RPC error: " << j["error"].dump() << '\n';
    }

    // Capture serial on device power-on, then fire the probe once.
    if (j.value("event", "") == "device.state") {
        const json d = j.contains("data") ? j["data"] : json::object();
        const std::string state = d.value("state", "");
        if (d.contains("serial") && (state == "ON" || state == "STATE_ON")) {
            g_serial = d["serial"].is_string() ? d["serial"].get<std::string>()
                                               : std::to_string(d["serial"].get<long long>());
            std::cerr << "display-probe: device ON, serial=" << g_serial << '\n';
            bool expected = false;
            if (SEND_FROM_CALLBACK && g_sent.compare_exchange_strong(expected, true)) {
                runProbe(g_serial);
            }
        }
    }
    return 0;
}

} // namespace

int main() {
    std::cerr << "display-probe: registering callback, claiming Studio "
                 "(set config.lua devices to MASCHINE_STUDIO)\n";
    rebellion(rpc_callback);

    if (SEND_FROM_CALLBACK) {
        rebellion_loop(0); // blocks; probe fires from callback on device ON
    } else {
        // Alternative path: pump the loop in slices, send from main once serial known.
        for (int i = 0; i < 600 && g_serial.empty(); ++i) rebellion_loop(50);
        if (!g_serial.empty() && !g_sent.exchange(true)) runProbe(g_serial);
        rebellion_loop(0);
    }
    return 0;
}
