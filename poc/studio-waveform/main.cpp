// Mixxx Studio Bridge — PoC Stage 2: waveform on the Studio screens.
//
//   display 0 = scrollable track list   (Knob 2 changes selection -> loads it)
//   display 1 = selected track waveform  (Knob 1 scrubs the view window)
//
// Links librebellion (drives panels + reads knobs) and mxb_decode (reads
// mixxxdb.sqlite + decodes the analysis blob). Reuses the v0.0 display-probe
// timing pattern: wait for device ON, warm up the instance, then drive.
//
//   studio_waveform "C:\Users\<you>\AppData\Local\Mixxx"
//
// Single-threaded: rebellion_loop / rebellion_rpc are only called from main().

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <json.hpp>
using json = nlohmann::json;

extern "C" {
#include "rebellion.h"
}

#include "framebuffer.hpp"
#include "mixxxdb.hpp"
#include "waveform.hpp"

namespace {

constexpr int SERIAL_WAIT_MS     = 30000;
constexpr int INSTANCE_WARMUP_MS = 4000;
constexpr int SLICE_MS           = 30;

// The under-display knobs are high-resolution endless encoders: one physical
// notch emits a burst of +/-1 tick events. Tune how those ticks map to actions,
// and cap how often we push a (slow, full-frame) redraw so a burst collapses to
// a couple of frames instead of a multi-second backlog of 261KB pushes.
constexpr double SCRUB_PER_TICK      = 0.005; // view shift per knob tick (before per-frame cap)
constexpr double SCRUB_MAX_PER_FRAME = 0.025; // clamp scrub/frame so a tick burst can't jump the view
constexpr int    SELECT_COOLDOWN_MS  = 130;   // min gap between track steps (one step per notch)
constexpr int    INPUT_POLL_MS       = 5;     // drain device input this often (stay reactive)
constexpr int    REDRAW_MIN_MS       = 20;    // cap display pushes to ~50 fps

std::string g_serial;

// ---- app state -------------------------------------------------------------
std::vector<mxb::TrackRow> g_tracks;
std::string g_mixxxDir;
int    g_selected = 0;            // index into g_tracks (Knob 2)
mxb::Waveform g_wf;               // currently loaded waveform
double g_scroll = 0.0;            // view start as fraction 0..1 (Knob 1)
int    g_scrubTicks = 0;         // net Knob 1 ticks awaiting apply (tallied in callback)
int    g_selTicks   = 0;         // net Knob 2 ticks awaiting apply
std::chrono::steady_clock::time_point g_lastSelect{};  // last Knob 2 track step
bool   g_knobTouched[9] = {false};  // 1..8: under-display knob touch (BTN_DATA) state
bool   g_dirty0 = true, g_dirty1 = true;  // which screen needs a redraw

// Colors
const uint16_t BG    = mxb::rgb565(6, 9, 15);
const uint16_t PANEL = mxb::rgb565(19, 28, 46);
const uint16_t CYAN  = mxb::rgb565(0, 212, 255);
const uint16_t WHITE = 0xFFFF;
const uint16_t DIM   = mxb::rgb565(120, 130, 150);
const uint16_t LOWC  = mxb::rgb565(0, 120, 220);   // low band
const uint16_t MIDC  = mxb::rgb565(0, 210, 160);   // body
const uint16_t HIC   = mxb::rgb565(255, 90, 108);  // peaks

// ---- device send -----------------------------------------------------------
void sendFB(int display, const mxb::Framebuffer& fb) {
    // Send the RLE-compressed device command stream (built in C++) rather than
    // 130k raw RGB565 ints: the old path JSON-encoded/decoded and rebuilt a
    // full-frame Lua table every frame, which dominated latency.
    json req = {{"method", "rebellion.sendDisplayCmd"},
                {"params", json::array({g_serial, display, fb.encodeDisplayCommands(display)})},
                {"id", display + 1}};
    const std::string s = req.dump();
    rebellion_rpc(REBELLION_MF_JSON, REBELLION_MT_REQ,
                  reinterpret_cast<const uint8_t*>(s.c_str()),
                  static_cast<uint32_t>(s.size()));
}

void pump(int ms) {
    for (int e = 0; e < ms; e += SLICE_MS) rebellion_loop(SLICE_MS);
}

// ---- track loading ---------------------------------------------------------
void loadSelected() {
    if (g_tracks.empty()) return;
    const mxb::TrackRow& t = g_tracks[g_selected];
    std::string blob = g_mixxxDir;
    if (!blob.empty() && blob.back() != '/' && blob.back() != '\\') blob += '\\';
    blob += "analysis\\" + std::to_string(t.analysis_id);
    std::string err;
    mxb::Waveform wf;
    if (mxb::decodeWaveformFile(blob, wf, err)) {
        g_wf = std::move(wf);
        g_scroll = 0.0;
        std::fprintf(stderr, "loaded: %s - %s (%zu frames, %.0fs)\n",
                     t.artist.c_str(), t.title.c_str(), g_wf.mono.size(),
                     g_wf.durationSeconds());
    } else {
        std::fprintf(stderr, "decode failed for id=%d: %s\n",
                     t.analysis_id, err.c_str());
        g_wf = mxb::Waveform{};
    }
    g_dirty0 = g_dirty1 = true;
}

// ---- rendering -------------------------------------------------------------
void renderList(mxb::Framebuffer& fb) {
    fb.clear(BG);
    fb.fillRect(0, 0, mxb::kW, 18, PANEL);
    fb.text(8, 5, "MIXXX STUDIO BRIDGE  -  TRACKS", CYAN, 1);

    const int rowH = 26;
    const int top = 26;
    for (size_t i = 0; i < g_tracks.size(); ++i) {
        int y = top + static_cast<int>(i) * rowH;
        if (y > mxb::kH - rowH) break;
        bool sel = (static_cast<int>(i) == g_selected);
        if (sel) {
            fb.fillRect(4, y - 3, mxb::kW - 8, rowH - 2, mxb::rgb565(0, 60, 90));
            fb.fillRect(4, y - 3, 3, rowH - 2, CYAN);
        }
        const mxb::TrackRow& t = g_tracks[i];
        std::string label = t.artist.empty() ? t.title
                                             : (t.artist + " - " + t.title);
        if (label.size() > 44) label = label.substr(0, 43) + ">";
        fb.text(14, y, label, sel ? WHITE : DIM, 1);
    }
}

void renderWaveform(mxb::Framebuffer& fb) {
    fb.clear(BG);
    fb.fillRect(0, 0, mxb::kW, 18, PANEL);
    const mxb::TrackRow& t = g_tracks.empty() ? mxb::TrackRow{}
                                              : g_tracks[g_selected];
    std::string title = t.title.empty() ? "(no track)" : t.title;
    if (title.size() > 40) title = title.substr(0, 39) + ">";
    fb.text(8, 5, title, CYAN, 1);

    if (g_wf.mono.empty()) {
        fb.text(160, 130, "no waveform", DIM, 1);
        return;
    }

    // View window: show ~20% of the track, starting at g_scroll.
    const double windowFrac = 0.20;
    size_t total = g_wf.mono.size();
    size_t winLen = static_cast<size_t>(total * windowFrac);
    if (winLen < 2) winLen = total;
    size_t start = static_cast<size_t>(g_scroll * (total - winLen));
    if (start + winLen > total) start = total - winLen;

    const int wTop = 24, wBot = mxb::kH - 18;
    const int mid = (wTop + wBot) / 2;
    const int halfH = (wBot - wTop) / 2 - 1;

    // One column per x: peak amplitude over that slice of the window.
    for (int x = 0; x < mxb::kW; ++x) {
        size_t a = start + static_cast<size_t>(x) * winLen / mxb::kW;
        size_t b = start + static_cast<size_t>(x + 1) * winLen / mxb::kW;
        if (b > total) b = total;
        int peak = 0;
        for (size_t i = a; i < b && i < total; ++i)
            if (g_wf.mono[i] > peak) peak = g_wf.mono[i];
        int h = peak * halfH / 255;
        uint16_t c = peak > 200 ? HIC : (peak > 110 ? MIDC : LOWC);
        fb.vline(x, mid - h, mid + h, c);
    }

    // Playhead-ish center marker + scroll bar.
    fb.vline(mxb::kW / 2, wTop, wBot, mxb::rgb565(255, 255, 255));
    int barW = static_cast<int>(windowFrac * mxb::kW);
    int barX = static_cast<int>(g_scroll * (mxb::kW - barW));
    fb.fillRect(0, mxb::kH - 6, mxb::kW, 6, PANEL);
    fb.fillRect(barX, mxb::kH - 6, barW, 6, CYAN);
}

void redraw() {
    if (g_dirty0) { mxb::Framebuffer fb; renderList(fb);     sendFB(0, fb); g_dirty0 = false; }
    if (g_dirty1) { mxb::Framebuffer fb; renderWaveform(fb); sendFB(1, fb); g_dirty1 = false; }
}

// ---- input -----------------------------------------------------------------
// The under-display knobs are smooth (detent-less) endless encoders: a turn
// emits a burst of +/-1 ticks. The callback only TALLIES net ticks (instant,
// never blocks on I/O); applyInput() folds them into motion once per frame.
// Because it acts on ticks-per-frame, behaviour tracks how fast you're turning
// rather than how the pipe batched events, and the per-frame clamp means no
// burst — however it arrives — can fling the view.
int msSince(std::chrono::steady_clock::time_point t) {
    return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t).count());
}

void applyInput() {
    // Knob 1: scrub, proportional to ticks this frame but clamped.
    int s = g_scrubTicks;
    g_scrubTicks = 0;
    if (s != 0) {
        double delta = s * SCRUB_PER_TICK;
        if (delta >  SCRUB_MAX_PER_FRAME) delta =  SCRUB_MAX_PER_FRAME;
        if (delta < -SCRUB_MAX_PER_FRAME) delta = -SCRUB_MAX_PER_FRAME;
        g_scroll += delta;
        if (g_scroll < 0) g_scroll = 0;
        if (g_scroll > 1) g_scroll = 1;
        g_dirty1 = true;
    }

    // Knob 2: one track step per notch — act on the first tick, then swallow the
    // rest of the burst for a cooldown (a held turn steps at a steady rate).
    if (g_selTicks != 0 && msSince(g_lastSelect) >= SELECT_COOLDOWN_MS) {
        int dir = (g_selTicks > 0) ? 1 : -1;
        g_selTicks = 0;
        g_lastSelect = std::chrono::steady_clock::now();
        int n = static_cast<int>(g_tracks.size());
        if (n > 0) {
            g_selected = ((g_selected + dir) % n + n) % n;
            std::fprintf(stderr, "  -> select track #%d\n", g_selected);
            loadSelected();
        }
    }
}

int rpc_callback(rebellion_message_format mf, rebellion_message_type,
                 const uint8_t* udata, uint32_t) {
    if (mf != REBELLION_MF_JSON) return 0;
    json j;
    try { j = json::parse(reinterpret_cast<const char*>(udata)); }
    catch (...) { return 0; }

    const std::string ev = j.value("event", "");

    // start.lua forwards {event=<name>, data=<event object>}. For device-level
    // events (device.state) the payload fields sit directly under "data". For
    // instance events (KNOB_ROTATE, BTN_DATA, PAD_DATA, ...) the dispatcher
    // wraps the parsed fields one level deeper: the real fields live under
    // data.data, alongside name/id/device/serial/self. Read the inner object
    // when present so knob/button fields are actually visible.
    json d;
    try { d = j.contains("data") ? j["data"] : json::object(); }
    catch (...) { d = json::object(); }
    json fields = d;
    try {
        if (d.is_object() && d.contains("data") && d["data"].is_object())
            fields = d["data"];
    } catch (...) {}

    if (ev == "device.state") {
        std::string st;
        try { st = fields.value("state", ""); } catch (...) {}
        if (g_serial.empty() && fields.contains("serial") &&
            (st == "ON" || st == "STATE_ON")) {
            try {
                g_serial = fields["serial"].is_string()
                               ? fields["serial"].get<std::string>()
                               : std::to_string(fields["serial"].get<long long>());
            } catch (...) {}
            std::fprintf(stderr, "device ON, serial=%s\n", g_serial.c_str());
        }
    } else if (ev == "KNOB_ROTATE") {
        // Just tally net ticks (direction string is robust); applyInput() acts.
        std::string knob, direction;
        try { knob = fields.value("knob", ""); direction = fields.value("direction", ""); }
        catch (...) {}
        int dir = (direction == "CLOCKWISE") ? 1 : -1;
        if (knob == "KNOB1")      g_scrubTicks += dir;
        else if (knob == "KNOB2") g_selTicks   += dir;
    } else if (ev == "BTN_DATA") {
        // The under-display knobs are touch-sensitive: a touch arrives as
        // KNOB1..8 PRESSED, lift as RELEASED. Use a fresh touch to clear any
        // stale ticks so motion always starts from a clean slate.
        std::string btn, state;
        try { btn = fields.value("button", ""); state = fields.value("state", ""); }
        catch (...) {}
        if (btn.size() == 5 && btn.compare(0, 4, "KNOB") == 0) {
            int k = btn[4] - '0';
            if (k >= 1 && k <= 8) {
                bool pressed = (state == "PRESSED");
                g_knobTouched[k] = pressed;
                if (pressed) {
                    if (k == 1) g_scrubTicks = 0;
                    if (k == 2) { g_selTicks = 0; g_lastSelect = {}; }
                }
            }
        }
    } else if (!ev.empty() && ev != "PAD_DATA") {
        // Surface anything else (the 4-D jog, etc.) so we can see it.
        std::fprintf(stderr, "[event] %s %s\n", ev.c_str(), fields.dump().c_str());
    }
    return 0;
}

std::string defaultMixxxDir() {
#ifdef _WIN32
    if (const char* la = std::getenv("LOCALAPPDATA")) return std::string(la) + "\\Mixxx";
    if (const char* ra = std::getenv("APPDATA")) return std::string(ra) + "\\Mixxx";
#endif
    return ".";
}

}  // namespace

int main(int argc, char** argv) {
    g_mixxxDir = (argc > 1) ? argv[1] : defaultMixxxDir();
    std::fprintf(stderr, "studio_waveform: Mixxx dir = %s\n", g_mixxxDir.c_str());

    std::string err;
    if (!mxb::queryAnalyzedTracks(g_mixxxDir, 8, g_tracks, err) || g_tracks.empty()) {
        std::fprintf(stderr, "no analyzed tracks (%s). Analyze one in Mixxx.\n",
                     err.empty() ? "none found" : err.c_str());
        return 1;
    }
    std::fprintf(stderr, "loaded %zu tracks from DB\n", g_tracks.size());

    rebellion(rpc_callback);

    // Wait for device + instance (proven v0.0 timing).
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

    loadSelected();   // decode the first track
    redraw();         // initial paint of both screens

    std::fprintf(stderr, "ready: Knob 1 = scrub waveform, Knob 2 = change track. "
                         "Ctrl+C to exit.\n");

    // Main loop: poll device input often (so ticks are captured promptly, not
    // batched behind a slow op), fold accumulated ticks into state every pass,
    // and push a frame at most ~50 fps. Input handling stays decoupled from the
    // (now cheap, RLE) display push, so the UI tracks the knobs in real time.
    auto lastDraw = std::chrono::steady_clock::now();
    for (;;) {
        rebellion_loop(INPUT_POLL_MS);
        applyInput();
        if ((g_dirty0 || g_dirty1) && msSince(lastDraw) >= REDRAW_MIN_MS) {
            redraw();
            lastDraw = std::chrono::steady_clock::now();
        }
    }
    return 0;  // unreachable; Ctrl+C exits
}
