// Mixxx Studio Bridge — v0.0 deliverable
//
// File: v0.0/tools/display-probe/main.cpp
// Purpose: Test 0.0.3 (SPEC §0 Session 3, §1.4) — the load-bearing risk test.
//
// Builds a 480x272 RGB565 framebuffer of alternating black/white vertical
// columns (10px wide each) and sends it to display 0, then display 1, using
// Rebellion's existing display path via the JSON-RPC method
// "rebellion.sendDataToDisplay" (see scripts/rpc.lua:47).
//
// TIMING (learned on hardware): the device emits "device.state ON" during the
// PID-connect handshake, but the per-serial *instance* (which sendDataToDisplay
// needs) isn't created until the later serial-connect handshake completes. So
// we must NOT send from the device.state callback — we capture the serial, then
// pump the event loop for a few seconds to let the instance come up, and only
// then send. We also pump (not sleep) between/after sends so the framebuffer is
// actually flushed over the pipe.
//
// REQUIRES the mappings.lua Studio entry to carry display config
// (ledcnt=103, dcnt=2, dheight=272, dwidth=480) — already applied in this repo.
//
// ── Outcomes (record in V00_RESULTS.md) ───────────────────────────────────
//   A: pattern appears correctly on both panels  -> protocol generalises.
//   B: no error logged but panels dark/garbage    -> silent route failure.
//   C: pipe disconnect / NIHIA error / crash      -> hard protocol mismatch.
//
// If you see "no instance found" in the log, the send fired too early — raise
// INSTANCE_WARMUP_MS below. This file is single-threaded: rebellion_loop and
// rebellion_rpc are only ever called from main(), never concurrently.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include <json.hpp>
using json = nlohmann::json;

extern "C" {
#include "rebellion.h"
}

namespace {

constexpr int  WIDTH    = 480;
constexpr int  HEIGHT   = 272;
constexpr int  COL_WIDTH = 10;     // px per black/white column
constexpr uint16_t WHITE = 0xFFFF; // RGB565
constexpr uint16_t BLACK = 0x0000;

constexpr int SERIAL_WAIT_MS     = 30000; // max wait for device power-on
constexpr int INSTANCE_WARMUP_MS = 4000;  // let serial-connect create the instance
constexpr int OBSERVE_MS         = 2500;  // pump/observe time after each send
constexpr int SLICE_MS           = 50;    // loop slice

std::string g_serial;
std::atomic<bool> g_rpc_error{false};

// Row-major flat RGB565 framebuffer: vertical stripes alternating B/W.
std::vector<int> buildPattern() {
    std::vector<int> fb;
    fb.reserve(static_cast<size_t>(WIDTH) * HEIGHT);
    for (int y = 0; y < HEIGHT; ++y)
        for (int x = 0; x < WIDTH; ++x)
            fb.push_back(((x / COL_WIDTH) % 2) ? WHITE : BLACK);
    return fb;
}

// Pump the event loop for roughly ms milliseconds in SLICE_MS slices.
void pump(int ms) {
    for (int elapsed = 0; elapsed < ms; elapsed += SLICE_MS)
        rebellion_loop(SLICE_MS);
}

void sendToDisplay(const std::string& serial, int display, const std::vector<int>& fb) {
    json req = {
        {"method", "rebellion.sendDataToDisplay"},
        {"params", json::array({serial, display, fb})},
        {"id", display + 1},
    };
    const std::string s = req.dump();
    g_rpc_error = false;
    auto t0 = std::chrono::steady_clock::now();
    int rc = rebellion_rpc(REBELLION_MF_JSON, REBELLION_MT_REQ,
                           reinterpret_cast<const uint8_t*>(s.c_str()),
                           static_cast<uint32_t>(s.size()));
    auto t1 = std::chrono::steady_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    std::cerr << "display-probe: sent " << fb.size() << " px to display " << display
              << " (rc=" << rc << ", " << us << " us)"
              << (g_rpc_error ? "  <-- RPC ERROR, see log above" : "") << '\n';
}

int rpc_callback(rebellion_message_format mf, rebellion_message_type /*mt*/,
                 const uint8_t* udata, uint32_t /*len*/) {
    if (mf != REBELLION_MF_JSON) return 0;
    json j;
    try {
        j = json::parse(reinterpret_cast<const char*>(udata));
    } catch (const std::exception&) {
        return 0;
    }
    if (j.contains("error")) {
        g_rpc_error = true;
        std::cerr << "display-probe: NIHIA/RPC error: " << j["error"].dump() << '\n';
    }
    // Capture serial on power-on; do NOT send here (instance not ready yet).
    if (j.value("event", "") == "device.state") {
        const json d = j.contains("data") ? j["data"] : json::object();
        const std::string state = d.value("state", "");
        if (g_serial.empty() && d.contains("serial") &&
            (state == "ON" || state == "STATE_ON")) {
            g_serial = d["serial"].is_string()
                           ? d["serial"].get<std::string>()
                           : std::to_string(d["serial"].get<long long>());
            std::cerr << "display-probe: device ON, serial=" << g_serial
                      << " (waiting for instance to come up...)\n";
        }
    }
    return 0;
}

} // namespace

int main() {
    std::cerr << "display-probe: registering callback, claiming Studio "
                 "(config.lua devices = MASCHINE_STUDIO)\n";
    rebellion(rpc_callback);

    // 1. Wait for the device to power on and report its serial.
    for (int waited = 0; waited < SERIAL_WAIT_MS && g_serial.empty(); waited += SLICE_MS)
        rebellion_loop(SLICE_MS);
    if (g_serial.empty()) {
        std::cerr << "display-probe: no device serial seen — is the Studio on "
                     "(USB+PSU) and free of other NI apps?\n";
        return 1;
    }

    // 2. Let the serial-connect handshake create the per-serial instance.
    std::cerr << "display-probe: warming up instance (" << INSTANCE_WARMUP_MS << " ms)\n";
    pump(INSTANCE_WARMUP_MS);

    // 3. Send the pattern to display 0, observe, then display 1, observe.
    const std::vector<int> fb = buildPattern();
    std::cerr << "display-probe: sending pattern to display 0\n";
    sendToDisplay(g_serial, 0, fb);
    pump(OBSERVE_MS);
    std::cerr << "display-probe: sending pattern to display 1\n";
    sendToDisplay(g_serial, 1, fb);
    pump(OBSERVE_MS);

    std::cerr << "display-probe: done. OBSERVE BOTH PANELS and record A/B/C in "
                 "V00_RESULTS.md. (Ctrl+C to exit.)\n";
    rebellion_loop(0);
    return 0;
}
