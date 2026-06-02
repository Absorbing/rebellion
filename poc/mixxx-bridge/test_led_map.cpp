// Unit tests for deck-state -> pad LED feedback (led_map.hpp). No hardware.

#include <cstdio>
#include "led_map.hpp"

using namespace mxb;

static int g_fail = 0;
#define CHECK(c, m) do { if(!(c)){ std::printf("FAIL: %s\n", m); ++g_fail; } } while(0)

int main() {
    DeckModel model([](const TrackFingerprint&, std::string&, std::string&,
                       double&, Waveform&) { return false; });  // loader unused

    std::vector<LedCmd> cmds;
    auto find = [&](int idx) -> LedCmd {
        for (auto& c : cmds) if (c.index == idx) return c;
        return LedCmd{-1, 99, 99};
    };

    // Empty decks, focus deck 1: pads off; GROUP A cyan (focused); PLAY off.
    computeLeds(model, 1, cmds);
    CHECK(find(studioled::padRGB(1, 1)).intensity == 0, "empty pad off");
    CHECK(find(studioled::groupRGB(1, 1)).intensity == 3 &&
          find(studioled::groupRGB(1, 2)).intensity == 3, "GROUP A focused -> cyan (G+B)");
    CHECK(find(studioled::PLAY).intensity == 0, "PLAY off when focused deck empty");

    // Deck 1 playing (pads 1-8 green), deck 2 loaded+stopped (pads 9-16 blue dim).
    model.deck(1).loaded = true; model.deck(1).playing = true;
    model.deck(2).loaded = true; model.deck(2).playing = false;
    computeLeds(model, 1, cmds);

    // pad 1 (deck1): R off, G bright, B off.
    CHECK(find(studioled::padRGB(1, 0)).intensity == 0, "pad1 R off");
    CHECK(find(studioled::padRGB(1, 1)).intensity == 3 &&
          find(studioled::padRGB(1, 1)).color == ledcolor::WHITE, "pad1 G bright (playing=green)");
    CHECK(find(studioled::padRGB(1, 2)).intensity == 0, "pad1 B off");
    // pad 8 still deck 1.
    CHECK(find(studioled::padRGB(8, 1)).intensity == 3, "pad8 G bright (deck1)");

    // pad 9 (deck2): blue dim.
    CHECK(find(studioled::padRGB(9, 1)).intensity == 0, "pad9 G off");
    CHECK(find(studioled::padRGB(9, 2)).intensity == 1, "pad9 B dim (loaded,stopped=blue)");
    CHECK(find(studioled::padRGB(16, 2)).intensity == 1, "pad16 B dim (deck2)");

    // Focused deck 1 transport LEDs (PLAY bright, SYNC dim since not synced).
    CHECK(find(studioled::PLAY).intensity == 3, "PLAY bright (focused deck playing)");
    CHECK(find(studioled::RESTART).intensity == 1, "CUE(RESTART) dim (focused loaded)");
    // GROUP B unfocused+loaded -> blue dim.
    CHECK(find(studioled::groupRGB(2, 2)).intensity == 1, "GROUP B unfocused loaded -> blue dim");

    // Switch focus to deck 2 -> GROUP B cyan, PLAY reflects deck 2 (loaded, stopped).
    computeLeds(model, 2, cmds);
    CHECK(find(studioled::groupRGB(2, 1)).intensity == 3, "GROUP B focused -> cyan");
    CHECK(find(studioled::PLAY).intensity == 1, "PLAY dim (focused deck2 loaded, stopped)");

    // Index sanity from the probed map.
    CHECK(studioled::padRGB(1, 0) == 1 && studioled::padRGB(8, 2) == 24, "pads 1-8 at 1..24");
    CHECK(studioled::padRGB(9, 0) == 63 && studioled::padRGB(16, 2) == 86, "pads 9-16 at 63..86");
    CHECK(studioled::groupRGB(1, 0) == 107 && studioled::groupRGB(8, 2) == 130, "groups A-H at 107..130");
    CHECK(studioled::PLAY == 147 && studioled::peakLeft(15) == 174 && studioled::peakRight(0) == 175,
          "named/peak indices");

    if (g_fail == 0) std::printf("ALL LED-MAP TESTS PASSED\n");
    else             std::printf("%d CHECK(S) FAILED\n", g_fail);
    return g_fail ? 1 : 0;
}
