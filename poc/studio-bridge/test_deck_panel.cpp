// Render-smoke test for the deck panel: draws representative DeckStates and
// confirms each encodes through the device RLE command path. No hardware.

#include <cstdio>
#include <cmath>

#include "deck_panel.hpp"

using namespace mxb;

static int g_fail = 0;
#define CHECK(c, m) do { if(!(c)){ std::printf("FAIL: %s\n", m); ++g_fail; } } while(0)

static DeckState makeLoaded() {
    DeckState d;
    d.loaded = true; d.hasWaveform = true;
    d.artist = "Some Artist"; d.title = "A Long Track Title That Might Overflow The Header Area";
    d.bpm = 128.0; d.position = 0.37; d.playing = true; d.sync = true;
    d.hotcues = {{0, 0.10, 255, 0, 0}, {1, 0.35, 0, 200, 255}, {2, 0.80, 0, 255, 80}};
    d.waveform.visual_sample_rate = 441.0;
    d.waveform.mono.resize(40000);
    d.waveform.low.resize(40000);
    d.waveform.mid.resize(40000);
    d.waveform.high.resize(40000);
    for (size_t i = 0; i < d.waveform.mono.size(); ++i) {
        d.waveform.mono[i] = static_cast<uint8_t>(
            120 + 120 * std::sin(i * 0.001) * std::sin(i * 0.013));
        // synthetic bands that shift dominance across the track (R->G->B)
        d.waveform.low[i]  = static_cast<uint8_t>(128 + 127 * std::sin(i * 0.0007));
        d.waveform.mid[i]  = static_cast<uint8_t>(128 + 127 * std::sin(i * 0.0011 + 2.0));
        d.waveform.high[i] = static_cast<uint8_t>(128 + 127 * std::sin(i * 0.0017 + 4.0));
    }
    return d;
}

int main() {
    struct Case { const char* name; DeckState d; int deck; };
    std::vector<Case> cases;
    cases.push_back({"loaded+playing", makeLoaded(), 1});
    { DeckState e; cases.push_back({"empty", e, 2}); }
    { DeckState n; n.loaded = true; n.title = "Unanalysed"; cases.push_back({"loaded-no-wf", n, 2}); }

    for (auto& c : cases) {
        Framebuffer fb;
        renderDeckPanel(fb, c.deck, c.d);
        CHECK(fb.px.size() == static_cast<size_t>(kW) * kH, c.name);
        auto cmd = fb.encodeDisplayCommands(c.deck - 1);
        CHECK(cmd.size() > 16, c.name);  // header + something
        std::printf("  %-16s deck %d -> %zu cmd bytes (raw %zu)\n",
                    c.name, c.deck, cmd.size(), fb.px.size() * 2);
    }

    if (g_fail == 0) std::printf("ALL DECK-PANEL RENDER TESTS PASSED\n");
    else             std::printf("%d CHECK(S) FAILED\n", g_fail);
    return g_fail ? 1 : 0;
}
