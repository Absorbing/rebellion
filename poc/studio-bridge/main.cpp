// Mixxx Studio Bridge — Stage 3 integration.
//
//   Mixxx state ──loopMIDI──▶ MidiDecoder ──▶ DeckModel ──▶ DeckState[1..4]
//                                                               │
//   display 0 = Deck A (ch1)   ◀── renderDeckPanel ────────────┤
//   display 1 = Deck B (ch2)   ◀── renderDeckPanel ────────────┘
//
// Reuses the proven studio_waveform device/loop pattern (wait for ON, warm up,
// pump rebellion_loop, push full frames via rebellion.sendDisplayCmd). The only
// new live seams are the loopMIDI listener and the SQLite resolver — everything
// else is the same path that already drove the screens.
//
//   studio_bridge "C:\Users\<you>\AppData\Local\Mixxx" ["Mixxx-State"]
//
// Single-threaded: rebellion_loop / rebellion_rpc only from main(). RtMidi
// delivers MIDI on its own thread; it only mutates DeckModel via a mutex-guarded
// queue drained here, so the device path stays single-threaded.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#include <json.hpp>
using json = nlohmann::json;

extern "C" {
#include "rebellion.h"
}

#include "framebuffer.hpp"
#include "deck_panel.hpp"
#include "deck_state.hpp"
#include "mixxx_midi.hpp"
#include "mixxx_listener.hpp"
#include "track_resolver.hpp"

namespace {

constexpr int SERIAL_WAIT_MS     = 30000;
constexpr int INSTANCE_WARMUP_MS = 4000;
constexpr int SLICE_MS           = 30;
constexpr int INPUT_POLL_MS      = 5;
constexpr int REDRAW_MIN_MS      = 33;   // ~30fps cap; overview frames are heavy

std::string g_serial;
std::string g_mixxxDir;

// Which Mixxx deck each display shows. Display 0 = Deck A, display 1 = Deck B.
constexpr int kDeckForDisplay[2] = {1, 2};

// --- event plumbing ---------------------------------------------------------
// RtMidi callback thread pushes BridgeEvents here; main thread drains them.
std::mutex            g_evMutex;
std::vector<mxb::BridgeEvent> g_evQueue;

void enqueueEvent(const mxb::BridgeEvent& e) {
    std::lock_guard<std::mutex> lk(g_evMutex);
    g_evQueue.push_back(e);
}

// --- device send (identical framing to studio_waveform) ---------------------
void sendFB(int display, const mxb::Framebuffer& fb) {
    json req = {{"method", "rebellion.sendDisplayCmd"},
                {"params", json::array({g_serial, display, fb.encodeDisplayCommands(display)})},
                {"id", display + 1}};
    const std::string s = req.dump();
    rebellion_rpc(REBELLION_MF_JSON, REBELLION_MT_REQ,
                  reinterpret_cast<const uint8_t*>(s.c_str()),
                  static_cast<uint32_t>(s.size()));
}

void pump(int ms) { for (int e = 0; e < ms; e += SLICE_MS) rebellion_loop(SLICE_MS); }

int msSince(std::chrono::steady_clock::time_point t) {
    return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t).count());
}

int rpc_callback(rebellion_message_format mf, rebellion_message_type,
                 const uint8_t* udata, uint32_t) {
    if (mf != REBELLION_MF_JSON) return 0;
    json j;
    try { j = json::parse(reinterpret_cast<const char*>(udata)); } catch (...) { return 0; }
    const std::string ev = j.value("event", "");
    json d;
    try { d = j.contains("data") ? j["data"] : json::object(); } catch (...) { d = json::object(); }
    json fields = d;
    try { if (d.is_object() && d.contains("data") && d["data"].is_object()) fields = d["data"]; }
    catch (...) {}

    if (ev == "device.state") {
        std::string st;
        try { st = fields.value("state", ""); } catch (...) {}
        if (g_serial.empty() && fields.contains("serial") && (st == "ON" || st == "STATE_ON")) {
            try {
                g_serial = fields["serial"].is_string()
                               ? fields["serial"].get<std::string>()
                               : std::to_string(fields["serial"].get<long long>());
            } catch (...) {}
            std::fprintf(stderr, "device ON, serial=%s\n", g_serial.c_str());
        }
    }
    return 0;
}

std::string defaultMixxxDir() {
#ifdef _WIN32
    if (const char* la = std::getenv("LOCALAPPDATA")) return std::string(la) + "\\Mixxx";
    if (const char* ra = std::getenv("APPDATA"))      return std::string(ra) + "\\Mixxx";
#endif
    return ".";
}

void renderSplash(mxb::Framebuffer& fb, int deckNum) {
    using namespace mxb::dp;
    fb.clear(BG);
    fb.fillRect(0, 0, mxb::kW, 3, CYAN);
    fb.fillRect(0, mxb::kH - 3, mxb::kW, 3, CYAN);
    const std::string t = "MIXXX STUDIO BRIDGE";
    fb.text((mxb::kW - mxb::textWidth(t, 3)) / 2, 96, t, CYAN, 3);
    std::string sub = "waiting for Mixxx on deck ";
    sub += static_cast<char>('A' + (deckNum - 1));
    fb.text((mxb::kW - mxb::textWidth(sub, 1)) / 2, 140, sub, DIM, 1);
}

}  // namespace

int main(int argc, char** argv) {
    g_mixxxDir = (argc > 1) ? argv[1] : defaultMixxxDir();
    const std::string midiPort = (argc > 2) ? argv[2] : "Mixxx-State";
    std::fprintf(stderr, "studio_bridge: Mixxx dir = %s, MIDI port = \"%s\"\n",
                 g_mixxxDir.c_str(), midiPort.c_str());

    // Deck model: on a track-path event, resolve+decode against mixxxdb.sqlite.
    mxb::DeckModel model([&](const std::string& path, std::string& artist,
                             std::string& title, mxb::Waveform& wf) {
        mxb::ResolvedTrack rt;
        std::string err;
        if (!mxb::resolveTrackByPath(g_mixxxDir, path, rt, err)) {
            std::fprintf(stderr, "resolve failed for %s: %s\n", path.c_str(), err.c_str());
            return false;
        }
        artist = rt.artist; title = rt.title; wf = std::move(rt.waveform);
        std::fprintf(stderr, "loaded: %s - %s (%zu frames)\n",
                     artist.c_str(), title.c_str(), wf.mono.size());
        return true;
    });

    // MIDI decode -> queue (RtMidi thread) ; drained on the main thread. The
    // listener builds its own decoder from this sink.
    mxb::MixxxListener listener([&](const mxb::BridgeEvent& e) { enqueueEvent(e); });

    std::string lerr;
    if (!listener.open(midiPort, lerr)) {
        std::fprintf(stderr, "MIDI listener: %s\n", lerr.c_str());
        std::fprintf(stderr, "available input ports:\n");
        for (const auto& p : mxb::MixxxListener::listInputPorts())
            std::fprintf(stderr, "  - %s\n", p.c_str());
        std::fprintf(stderr, "(continuing; screens will show 'waiting for Mixxx')\n");
    }

    rebellion(rpc_callback);
    for (int w = 0; w < SERIAL_WAIT_MS && g_serial.empty(); w += SLICE_MS)
        rebellion_loop(SLICE_MS);
    if (g_serial.empty()) {
        std::fprintf(stderr, "no Studio (USB+PSU, NIHardwareService running, "
                             "other NI apps closed?)\n");
        rebellion(nullptr);
        return 1;
    }
    std::fprintf(stderr, "warming up instance...\n");
    pump(INSTANCE_WARMUP_MS);

    // Initial paint: splash per deck until Mixxx sends state.
    for (int disp = 0; disp < 2; ++disp) {
        mxb::Framebuffer fb; renderSplash(fb, kDeckForDisplay[disp]); sendFB(disp, fb);
    }

    std::fprintf(stderr, "ready: display 0 = Deck A, display 1 = Deck B. "
                         "Load tracks in Mixxx. Ctrl+C to exit.\n");

    auto lastDraw = std::chrono::steady_clock::now();
    for (;;) {
        rebellion_loop(INPUT_POLL_MS);

        // Drain MIDI events captured on the RtMidi thread.
        std::vector<mxb::BridgeEvent> batch;
        {
            std::lock_guard<std::mutex> lk(g_evMutex);
            batch.swap(g_evQueue);
        }
        for (const auto& e : batch) model.apply(e);

        // Redraw any deck whose state changed, throttled. (Full-frame overview
        // pushes are heavy; SPEC §4.4 diff-regions is the later optimisation.)
        if (msSince(lastDraw) >= REDRAW_MIN_MS) {
            for (int disp = 0; disp < 2; ++disp) {
                int deckNum = kDeckForDisplay[disp];
                mxb::DeckState& d = model.deck(deckNum);
                if (d.dirty) {
                    mxb::Framebuffer fb;
                    mxb::renderDeckPanel(fb, deckNum, d);
                    sendFB(disp, fb);
                    d.dirty = false;
                }
            }
            lastDraw = std::chrono::steady_clock::now();
        }
    }
    return 0;  // unreachable
}
