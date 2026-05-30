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
const uint16_t LOWC  = rgb565(0, 120, 220);
const uint16_t MIDC  = rgb565(0, 210, 160);
const uint16_t HIC   = rgb565(255, 90, 108);
const uint16_t PLAYED= rgb565(60, 70, 95);   // waveform behind the playhead
const uint16_t AMBER = rgb565(255, 184, 64);
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

// Render deck `deckNum` (1..4 -> A..D) from `d` into `fb`.
inline void renderDeckPanel(Framebuffer& fb, int deckNum, const DeckState& d) {
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
        const int playX = static_cast<int>(d.position * kW);
        for (int x = 0; x < kW; ++x) {
            size_t a = static_cast<size_t>(x)     * total / kW;
            size_t b = static_cast<size_t>(x + 1) * total / kW;
            if (b > total) b = total;
            int peak = 0;
            for (size_t i = a; i < b; ++i) if (mono[i] > peak) peak = mono[i];
            int h = peak * halfH / 255;
            uint16_t c;
            if (x <= playX) {
                c = PLAYED;                                   // already played: dim
            } else {
                c = peak > 200 ? HIC : (peak > 110 ? MIDC : LOWC);
            }
            fb.vline(x, mid - h, mid + h, c);
        }
        // playhead
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
    flag(d.playing ? "PLAY" : "", d.playing, MIDC);
}

}  // namespace mxb
