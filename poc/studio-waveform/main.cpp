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

std::string g_serial;

// ---- app state -------------------------------------------------------------
std::vector<mxb::TrackRow> g_tracks;
std::string g_mixxxDir;
int    g_selected = 0;            // index into g_tracks (Knob 2)
mxb::Waveform g_wf;               // currently loaded waveform
double g_scroll = 0.0;            // view start as fraction 0..1 (Knob 1)
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
    json req = {{"method", "rebellion.sendDataToDisplay"},
                {"params", json::array({g_serial, display, fb.asIntArray()})},
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
// Apply a +1/-1 step to whichever control the knob drives.
void scrub(int dir) {
    g_scroll += dir * 0.04;
    if (g_scroll < 0) g_scroll = 0;
    if (g_scroll > 1) g_scroll = 1;
    g_dirty1 = true;
    std::fprintf(stderr, "  -> scrub dir=%d scroll=%.2f\n", dir, g_scroll);
}

void selectTrack(int dir) {
    int n = static_cast<int>(g_tracks.size());
    if (n <= 0) return;
    g_selected = (g_selected + dir + n) % n;
    std::fprintf(stderr, "  -> select track #%d\n", g_selected);
    loadSelected();
}

// Knob index comes as "KNOB1".."KNOB8"; direction string gives the sign.
void onKnob(const std::string& knob, int dir) {
    if (knob == "KNOB1")      scrub(dir);
    else if (knob == "KNOB2") selectTrack(dir);
}

int rpc_callback(rebellion_message_format mf, rebellion_message_type,
                 const uint8_t* udata, uint32_t) {
    if (mf != REBELLION_MF_JSON) return 0;
    json j;
    try { j = json::parse(reinterpret_cast<const char*>(udata)); }
    catch (...) { return 0; }

    const std::string ev = j.value("event", "");
    json d;
    try { d = j.contains("data") ? j["data"] : json::object(); }
    catch (...) { d = json::object(); }

    if (ev == "device.state") {
        std::string st;
        try { st = d.value("state", ""); } catch (...) {}
        if (g_serial.empty() && d.contains("serial") &&
            (st == "ON" || st == "STATE_ON")) {
            try {
                g_serial = d["serial"].is_string()
                               ? d["serial"].get<std::string>()
                               : std::to_string(d["serial"].get<long long>());
            } catch (...) {}
            std::fprintf(stderr, "device ON, serial=%s\n", g_serial.c_str());
        }
    } else if (ev == "KNOB_ROTATE") {
        // Use the direction STRING (robust; avoids parsing the numeric field).
        std::string knob, direction;
        try { knob = d.value("knob", ""); direction = d.value("direction", ""); }
        catch (...) {}
        int dir = (direction == "CLOCKWISE") ? 1 : -1;
        std::fprintf(stderr, "[event] KNOB_ROTATE knob=%s dir=%s\n",
                     knob.c_str(), direction.c_str());
        if (!knob.empty()) onKnob(knob, dir);
    } else if (!ev.empty() && ev != "PAD_DATA") {
        // Surface anything else (BTN_DATA, the 4-D jog, etc.) so we can see it.
        std::fprintf(stderr, "[event] %s %s\n", ev.c_str(), d.dump().c_str());
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

    // Main loop: pump events; knobs set dirty flags; redraw changed screens.
    for (;;) {
        rebellion_loop(SLICE_MS);
        if (g_dirty0 || g_dirty1) redraw();
    }
    return 0;  // unreachable; Ctrl+C exits
}
