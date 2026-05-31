// Unit tests for deck-state -> LED feedback (led_map.hpp). No hardware.

#include <cstdio>
#include "led_map.hpp"

using namespace mxb;

static int g_fail = 0;
#define CHECK(c, m) do { if(!(c)){ std::printf("FAIL: %s\n", m); ++g_fail; } } while(0)

int main() {
    LedLayout L;  // defaults: deck1 play=16 cue=20, deck2 play=19 cue=23
    DeckModel model([](const TrackFingerprint&, std::string&, std::string&,
                       double&, Waveform&) { return false; });  // loader unused

    std::vector<LedCmd> cmds;

    // Empty decks -> all off.
    computeDeckLeds(model, L, cmds);
    CHECK(cmds.size() == 4, "4 led commands (play+cue x2 decks)");
    for (auto& c : cmds) CHECK(c.color == ledcolor::OFF && c.intensity == 0, "empty deck led off");

    // Deck 1 loaded + playing; deck 2 loaded + stopped.
    model.deck(1).loaded = true; model.deck(1).playing = true;
    model.deck(2).loaded = true; model.deck(2).playing = false;
    computeDeckLeds(model, L, cmds);

    auto find = [&](int idx) -> LedCmd {
        for (auto& c : cmds) if (c.index == idx) return c;
        return LedCmd{-1, 99, 99};
    };
    CHECK(find(16).color == ledcolor::GREEN && find(16).intensity == 3, "deck1 play -> green bright");
    CHECK(find(19).color == ledcolor::GREEN && find(19).intensity == 1, "deck2 loaded -> green dim");
    CHECK(find(20).color == ledcolor::WHITE && find(20).intensity == 1, "deck1 cue -> white dim");
    CHECK(find(23).color == ledcolor::WHITE && find(23).intensity == 1, "deck2 cue -> white dim");

    if (g_fail == 0) std::printf("ALL LED-MAP TESTS PASSED\n");
    else             std::printf("%d CHECK(S) FAILED\n", g_fail);
    return g_fail ? 1 : 0;
}
