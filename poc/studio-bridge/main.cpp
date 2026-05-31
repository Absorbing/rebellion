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
#include "control_map.hpp"
#include "midi_out.hpp"
#include "led_map.hpp"

namespace {

constexpr int SERIAL_WAIT_MS     = 30000;
constexpr int INSTANCE_WARMUP_MS = 4000;
constexpr int SLICE_MS           = 30;
constexpr int INPUT_POLL_MS      = 5;
constexpr int REDRAW_MIN_MS      = 33;   // ~30fps cap; overview frames are heavy

std::string g_serial;
std::string g_mixxxDir;

// Outbound: Studio buttons/pads/knobs -> MIDI -> Mixxx (2nd loopMIDI port).
// Driven from rpc_callback, which runs on the main thread during rebellion_loop,
// so direct sends are safe (no cross-thread concern like the inbound listener).
mxb::MidiOut g_out;

// Deck-focus model: which deck the single transport buttons (PLAY/CUE/SYNC) act
// on. GROUP_A/B set it. Set in rpc_callback (main thread), read in the loop.
int g_focusedDeck = 1;

int knobNameToIndex(const std::string& name) {  // "KNOB1".."KNOB8" -> 1..8, else 0
    if (name.size() == 5 && name.compare(0, 4, "KNOB") == 0 &&
        name[4] >= '1' && name[4] <= '8')
        return name[4] - '0';
    return 0;
}

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

// Set one button/pad LED (rebellion.sendLedData: serial, index, color, intensity).
void sendLed(int index, uint8_t color, uint8_t intensity) {
    if (g_serial.empty() || index <= 0) return;
    json req = {{"method", "rebellion.sendLedData"},
                {"params", json::array({g_serial, index, color, intensity})},
                {"id", 1000 + index}};
    const std::string s = req.dump();
    rebellion_rpc(REBELLION_MF_JSON, REBELLION_MT_REQ,
                  reinterpret_cast<const uint8_t*>(s.c_str()),
                  static_cast<uint32_t>(s.size()));
}

// Push deck-state pad LEDs, sending only the ones whose colour/intensity changed.
void updateLeds(mxb::DeckModel& model) {
    static std::map<int, mxb::LedCmd> last;
    std::vector<mxb::LedCmd> want;
    mxb::computeLeds(model, want);
    for (const auto& c : want) {
        if (c.index <= 0) continue;
        auto it = last.find(c.index);
        if (it == last.end() || !(it->second == c)) {
            sendLed(c.index, c.color, c.intensity);
            last[c.index] = c;
        }
    }
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
        return 0;
    }

    // --- Studio controls -> outbound MIDI (forward to Mixxx) -----------------
    if (ev == "BTN_DATA") {
        int id = fields.value("buttonid", -1);
        std::string state = fields.value("state", "");
        if (id < 0 || (state != "PRESSED" && state != "RELEASED")) return 0;
        const bool pressed = (state == "PRESSED");
        // Deck-focus model: GROUP_A/B pick the active deck; the single transport
        // buttons act on it. Everything else is forwarded raw for Mixxx mapping.
        switch (id) {
            case 16: if (pressed) g_focusedDeck = 1; break;   // GROUP_A -> focus deck 1
            case 19: if (pressed) g_focusedDeck = 2; break;   // GROUP_B -> focus deck 2
            case 29: g_out.send(mxb::mapTransport(mxb::Transport::Play, g_focusedDeck, pressed)); break;  // PLAY
            case 28: g_out.send(mxb::mapTransport(mxb::Transport::Cue,  g_focusedDeck, pressed)); break;  // RESTART -> CUE
            case 27: g_out.send(mxb::mapTransport(mxb::Transport::Sync, g_focusedDeck, pressed)); break;  // GRID -> SYNC
            default: g_out.send(mxb::mapButton(id, pressed)); break;
        }
    } else if (ev == "PAD_DATA") {
        int pad = fields.value("padid", -1);
        std::string state = fields.value("state", "");
        int cp = fields.value("cpressure", 0);
        if (pad >= 1 && (state == "PRESSED" || state == "RELEASED"))
            g_out.send(mxb::mapPad(pad, state == "PRESSED", cp));
    } else if (ev == "KNOB_ROTATE") {
        int k = knobNameToIndex(fields.value("knob", ""));
        std::string dir = fields.value("direction", "");
        if (k >= 1 && !dir.empty())
            g_out.send(mxb::mapKnobRotate(k, dir == "CLOCKWISE"));
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
    const std::string outPort  = (argc > 3) ? argv[3] : "Studio-Control";
    std::fprintf(stderr, "studio_bridge: Mixxx dir = %s, in = \"%s\", out = \"%s\"\n",
                 g_mixxxDir.c_str(), midiPort.c_str(), outPort.c_str());

    // Deck model: on a track-identity event, match the fingerprint to a library
    // row and decode its waveform from mixxxdb.sqlite + analysis/.
    mxb::DeckModel model([&](const mxb::TrackFingerprint& fp, std::string& artist,
                             std::string& title, double& bpm, mxb::Waveform& wf) {
        mxb::ResolvedTrack rt;
        std::string err;
        if (!mxb::resolveTrackByFingerprint(g_mixxxDir, fp, rt, err)) {
            std::fprintf(stderr, "resolve failed: %s\n", err.c_str());
            return false;
        }
        artist = rt.artist; title = rt.title; wf = std::move(rt.waveform);
        if (rt.bpm > 0) bpm = rt.bpm;
        // Band diagnostic: -1 = band vector empty (no signal_filtered decoded);
        // 0 = present but all-zero (decode bug); >0 = real band energy.
        auto avg = [](const std::vector<uint8_t>& v) -> int {
            if (v.empty()) return -1;
            long s = 0; for (uint8_t x : v) s += x;
            return static_cast<int>(s / static_cast<long>(v.size()));
        };
        std::fprintf(stderr, "loaded: %s - %s [%s] (%zu frames; bands l/m/h avg=%d/%d/%d)\n",
                     artist.c_str(), title.c_str(), rt.location.c_str(), wf.mono.size(),
                     avg(wf.low), avg(wf.mid), avg(wf.high));
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

    // Outbound port (Studio controls -> Mixxx). Optional: bridge still drives
    // the screens if it's missing, just won't forward buttons.
    std::string oerr;
    if (!g_out.open(outPort, oerr)) {
        std::fprintf(stderr, "MIDI out: %s\n", oerr.c_str());
        std::fprintf(stderr, "available output ports:\n");
        for (const auto& p : mxb::MidiOut::listOutputPorts())
            std::fprintf(stderr, "  - %s\n", p.c_str());
        std::fprintf(stderr, "(continuing; Studio buttons won't reach Mixxx)\n");
    } else {
        std::fprintf(stderr, "forwarding Studio controls -> \"%s\"\n", outPort.c_str());
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

    // LED probe (MXB_LED_PROBE=1): cycle every LED index, showing the number on
    // the screen, so the real button->index map can be read off the hardware.
    // The Studio LED indices in mappings.lua are placeholders; this confirms them.
    if (std::getenv("MXB_LED_PROBE")) {
        constexpr int kLedCnt = 103;   // Studio ledcnt (mappings.lua)
        std::fprintf(stderr, "LED PROBE: cycling indices 1..%d (~500ms each). "
                             "Note which button/pad lights at each number.\n", kLedCnt);
        int idx = 1, prev = 0;
        auto last = std::chrono::steady_clock::now();
        for (;;) {
            rebellion_loop(INPUT_POLL_MS);
            if (msSince(last) >= 500) {
                if (prev > 0) sendLed(prev, mxb::ledcolor::OFF, 0);
                sendLed(idx, mxb::ledcolor::WHITE, 3);
                for (int disp = 0; disp < 2; ++disp) {
                    mxb::Framebuffer fb; fb.clear(mxb::dp::BG);
                    std::string t = "LED " + std::to_string(idx);
                    fb.text((mxb::kW - mxb::textWidth(t, 4)) / 2, 110, t, mxb::dp::CYAN, 4);
                    sendFB(disp, fb);
                }
                prev = idx;
                if (++idx > kLedCnt) idx = 1;
                last = std::chrono::steady_clock::now();
            }
        }
    }

    std::fprintf(stderr, "ready: display 0 = Deck A, display 1 = Deck B. "
                         "Load tracks in Mixxx. Ctrl+C to exit.\n");

    auto lastDraw = std::chrono::steady_clock::now();
    int lastFocus = 0;
    for (;;) {
        rebellion_loop(INPUT_POLL_MS);

        // Drain MIDI events captured on the RtMidi thread.
        std::vector<mxb::BridgeEvent> batch;
        {
            std::lock_guard<std::mutex> lk(g_evMutex);
            batch.swap(g_evQueue);
        }
        for (const auto& e : batch) model.apply(e);

        // Focus changed (GROUP_A/B) -> repaint both deck borders.
        if (g_focusedDeck != lastFocus) {
            model.deck(1).dirty = true;
            model.deck(2).dirty = true;
            lastFocus = g_focusedDeck;
        }

        updateLeds(model);  // deck state on the RGB pads (deck1=1-8, deck2=9-16)

        // Redraw any deck whose state changed, throttled. (Full-frame overview
        // pushes are heavy; SPEC §4.4 diff-regions is the later optimisation.)
        if (msSince(lastDraw) >= REDRAW_MIN_MS) {
            for (int disp = 0; disp < 2; ++disp) {
                int deckNum = kDeckForDisplay[disp];
                mxb::DeckState& d = model.deck(deckNum);
                if (d.dirty) {
                    mxb::Framebuffer fb;
                    mxb::renderDeckPanel(fb, deckNum, d, deckNum == g_focusedDeck);
                    sendFB(disp, fb);
                    d.dirty = false;
                }
            }
            lastDraw = std::chrono::steady_clock::now();
        }
    }
    return 0;  // unreachable
}
