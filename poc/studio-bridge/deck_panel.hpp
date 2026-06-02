// Mixxx Studio Bridge — per-deck screen panel.
//
// Renders one DeckState to a 480x272 RGB565 framebuffer: header (deck badge +
// artist/title + BPM), a full-track overview waveform with a playhead, and a
// footer (time + transport flags). Display 0 = Deck A (ch1), display 1 = Deck B.
//
// Pure drawing on the header-only framebuffer, so it compiles + renders off the
// device and is exercised by test_deck_panel.cpp.

#pragma once
#include <cstdio>
#include <string>

#include "framebuffer.hpp"
#include "deck_state.hpp"

namespace mxb {

// Palette (shared look with the rest of the bridge).
namespace dp {
const uint16_t BG    = rgb565(6, 9, 15);
const uint16_t PANEL = rgb565(19, 28, 46);
const uint16_t CYAN  = rgb565(0, 212, 255);
const uint16_t WHITE = 0xFFFF;
const uint16_t DIM   = rgb565(120, 130, 150);
const uint16_t GREEN      = rgb565(0, 210, 160);   // PLAY indicator
const uint16_t WAVE       = rgb565(0, 212, 255);   // average body, ahead of playhead
const uint16_t WAVE_DIM   = rgb565(0, 80, 130);    // peak outline, ahead of playhead
const uint16_t WAVE_PLAYED= rgb565(95, 140, 165);  // average body, already played
const uint16_t PLAYED     = rgb565(45, 55, 78);    // peak outline, already played
const uint16_t AMBER      = rgb565(255, 184, 64);

// Scrolling waveform shows a fixed time span centred on the playhead, so the
// zoom is consistent across tracks (independent of track length).
constexpr double kWindowSeconds = 30.0;
}  // namespace dp

inline int textWidth(const std::string& s, int scale) {
    return static_cast<int>(s.size()) * 6 * scale;
}
inline void rightText(Framebuffer& fb, int xRight, int y, const std::string& s,
                      uint16_t c, int scale) {
    fb.text(xRight - textWidth(s, scale), y, s, c, scale);
}

inline std::string fmtTime(double seconds) {
    if (seconds < 0) seconds = 0;
    int s = static_cast<int>(seconds);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
    return buf;
}

// Render deck `deckNum` (1..4 -> A..D) from `d` into `fb`. `focused` draws a
// border marking the deck the transport buttons currently act on.
inline void renderDeckPanel(Framebuffer& fb, int deckNum, const DeckState& d,
                            bool focused = false) {
    using namespace dp;
    fb.clear(BG);

    // --- header bar ---------------------------------------------------------
    fb.fillRect(0, 0, kW, 22, PANEL);
    // deck badge (A/B/...) in a cyan chip
    std::string badge(1, static_cast<char>('A' + (deckNum - 1)));
    fb.fillRect(4, 3, 18, 16, CYAN);
    fb.text(9, 5, badge, BG, 2);

    if (!d.loaded) {
        fb.text(30, 6, "no track loaded", DIM, 1);
        fb.text(180, 130, "DECK ", DIM, 2);
        fb.text(180 + textWidth("DECK ", 2), 130, badge, DIM, 2);
        return;
    }

    std::string label = d.artist.empty() ? d.title
                                          : (d.artist + " - " + d.title);
    if (label.empty()) label = "track not in library";  // fingerprint had no match
    // truncate to fit before the BPM readout
    const int bpmRoom = 96;
    size_t maxChars = static_cast<size_t>((kW - 30 - bpmRoom) / 6);
    if (label.size() > maxChars && maxChars > 1)
        label = label.substr(0, maxChars - 1) + ">";
    fb.text(30, 6, label, WHITE, 1);

    if (d.bpm > 0) {
        char b[16];
        std::snprintf(b, sizeof(b), "%.1f", d.bpm);
        rightText(fb, kW - 6, 4, b, CYAN, 1);
        rightText(fb, kW - 6, 13, "BPM", DIM, 1);
    }

    // --- waveform overview --------------------------------------------------
    const int wTop = 26, wBot = kH - 18;
    const int mid = (wTop + wBot) / 2;
    const int halfH = (wBot - wTop) / 2 - 1;

    if (d.hasWaveform && !d.waveform.mono.empty()) {
        const auto& mono = d.waveform.mono;
        const size_t total = mono.size();

        // Scrolling window centred on the playhead; clamp to the track at the
        // ends. Reduce each column by AVERAGE (body) with a PEAK outline so the
        // quiet/loud envelope shows instead of saturating to "all highs".
        const double vrate = d.waveform.visual_sample_rate > 0.0
                                 ? d.waveform.visual_sample_rate : 441.0;
        size_t winLen = static_cast<size_t>(kWindowSeconds * vrate);
        if (winLen < 1) winLen = 1;
        if (winLen > total) winLen = total;

        const double posF = d.position * static_cast<double>(total);
        long start = static_cast<long>(posF - winLen / 2.0);
        const long maxStart = static_cast<long>(total) - static_cast<long>(winLen);
        if (start < 0) start = 0;
        if (start > maxStart) start = maxStart;

        const int playX = static_cast<int>(
            (posF - static_cast<double>(start)) / static_cast<double>(winLen) * kW);

        // Colour from frequency bands when present (Mixxx-style: low->R, mid->G,
        // high->B), else fall back to a single cyan hue.
        const bool bands = d.waveform.hasBands();
        const auto& low = d.waveform.low;
        const auto& midB = d.waveform.mid;
        const auto& high = d.waveform.high;

        for (int x = 0; x < kW; ++x) {
            size_t a = static_cast<size_t>(start) + static_cast<size_t>(x)     * winLen / kW;
            size_t b = static_cast<size_t>(start) + static_cast<size_t>(x + 1) * winLen / kW;
            if (b > total) b = total;
            int peak = 0, n = 0, sum = 0, ls = 0, ms = 0, hs = 0, nb = 0;
            for (size_t i = a; i < b && i < total; ++i) {
                int v = mono[i];
                if (v > peak) peak = v;
                sum += v; ++n;
                if (bands && i < low.size() && i < midB.size() && i < high.size()) {
                    ls += low[i]; ms += midB[i]; hs += high[i]; ++nb;
                }
            }
            const int hPeak = peak * halfH / 255;
            const int hAvg  = (n ? sum / n : 0) * halfH / 255;
            const bool played = x < playX;

            uint16_t body, outline;
            bool colored = false;
            if (nb > 0) {
                int la = ls / nb, ma = ms / nb, ha = hs / nb;
                int mx = la; if (ma > mx) mx = ma; if (ha > mx) mx = ha;
                if (mx > 0) {  // real band energy -> colour by dominant band
                    int r = la * 255 / mx, g = ma * 255 / mx, bl = ha * 255 / mx;
                    const int bodyMul = played ? 45 : 100;       // dim played region
                    body    = rgb565(r * bodyMul / 100, g * bodyMul / 100, bl * bodyMul / 100);
                    outline = rgb565(r * 35 / 100,      g * 35 / 100,      bl * 35 / 100);
                    colored = true;
                }
            }
            if (!colored) {  // no/empty band data -> single-hue cyan (never invisible)
                body    = played ? WAVE_PLAYED : WAVE;
                outline = played ? PLAYED      : WAVE_DIM;
            }
            fb.vline(x, mid - hPeak, mid + hPeak, outline);
            fb.vline(x, mid - hAvg,  mid + hAvg,  body);
        }

        // Hotcue markers: a colored line + numbered tag at each cue in the window.
        for (const auto& hc : d.hotcues) {
            double mIdx = hc.fraction * static_cast<double>(total);
            int hx = static_cast<int>((mIdx - static_cast<double>(start)) /
                                      static_cast<double>(winLen) * kW);
            if (hx < 0 || hx >= kW) continue;
            uint16_t c = rgb565(hc.r, hc.g, hc.b);
            int bw = (hx + 3 <= kW) ? 3 : kW - hx;         // 3px-wide bar, clamped
            fb.fillRect(hx, wTop, bw, wBot - wTop + 1, c);
            std::string n = std::to_string(hc.number + 1);
            int tw = textWidth(n, 2) + 4;                  // scale-2 number, readable
            int tx = (hx + tw <= kW) ? hx : kW - tw;       // keep the tag on-screen
            int ty = wBot - 17;                            // label at the bottom
            fb.fillRect(tx, ty, tw, 17, c);
            fb.text(tx + 2, ty + 2, n, BG, 2);
        }

        fb.vline(playX, wTop, wBot, WHITE);
    } else {
        fb.text(150, mid - 4, "waveform not analysed", DIM, 1);
    }

    // --- footer -------------------------------------------------------------
    fb.fillRect(0, kH - 16, kW, 16, PANEL);
    double dur = d.hasWaveform ? d.waveform.durationSeconds() : 0.0;
    std::string t = fmtTime(d.position * dur) + " / " + fmtTime(dur);
    fb.text(6, kH - 13, t, DIM, 1);

    // transport flags, right side
    int fx = kW - 6;
    auto flag = [&](const char* s, bool on, uint16_t onc) {
        if (!on) return;
        fx -= textWidth(s, 1);
        fb.text(fx, kH - 13, s, onc, 1);
        fx -= 6;
    };
    flag("LOOP", d.loop, AMBER);
    flag("SYNC", d.sync, CYAN);
    flag(d.playing ? "PLAY" : "", d.playing, GREEN);

    // Focus border (the deck the transport buttons act on), drawn last.
    if (focused) {
        fb.fillRect(0, 0, kW, 2, CYAN);
        fb.fillRect(0, kH - 2, kW, 2, CYAN);
        fb.fillRect(0, 0, 2, kH, CYAN);
        fb.fillRect(kW - 2, 0, 2, kH, CYAN);
    }
}

}  // namespace mxb
