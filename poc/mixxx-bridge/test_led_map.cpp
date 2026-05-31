// Unit tests for deck-state -> LED feedback (led_map.hpp). No hardware.

#include <cstdio>
#include "led_map.hpp"

using namespace mxb;

static int g_fail = 0;
#define CHECK(c, m) do { if(!(c)){ std::printf("FAIL: %s\n", m); ++g_fail; } } while(0)

int main() {
    LedLayout L;  // groupA=16 groupB=19 play=29 cue=28 sync=27
    DeckModel model([](const TrackFingerprint&, std::string&, std::string&,
                       double&, Waveform&) { return false; });  // loader unused

    std::vector<LedCmd> cmds;
    auto find = [&](int idx) -> LedCmd {
        for (auto& c : cmds) if (c.index == idx) return c;
        return LedCmd{-1, 99, 99};
    };

    // Empty decks, focus on deck 1 -> GROUP_A focus (cyan), everything else off.
    computeLeds(model, 1, L, cmds);
    CHECK(cmds.size() == 5, "5 led commands (groupA/B, play, cue, sync)");
    CHECK(find(16).color == ledcolor::CYAN && find(16).intensity == 3, "deck1 focused -> cyan");
    CHECK(find(19).color == ledcolor::OFF, "deck2 unfocused+empty -> off");
    CHECK(find(29).color == ledcolor::OFF, "play off when focused deck empty");

    // Deck 1 loaded + playing + sync; deck 2 loaded. Focus deck 1.
    model.deck(1).loaded = true; model.deck(1).playing = true; model.deck(1).sync = true;
    model.deck(2).loaded = true;
    computeLeds(model, 1, L, cmds);
    CHECK(find(16).color == ledcolor::CYAN, "deck1 still focused -> cyan");
    CHECK(find(19).color == ledcolor::BLUE && find(19).intensity == 1, "deck2 loaded+unfocused -> blue dim");
    CHECK(find(29).color == ledcolor::GREEN && find(29).intensity == 3, "PLAY -> green bright (focused playing)");
    CHECK(find(28).color == ledcolor::WHITE, "CUE -> white (focused loaded)");
    CHECK(find(27).color == ledcolor::BLUE && find(27).intensity == 3, "SYNC -> blue bright (focused sync)");

    // Switch focus to deck 2 (loaded, stopped) -> PLAY dim green, reflects deck 2.
    computeLeds(model, 2, L, cmds);
    CHECK(find(19).color == ledcolor::CYAN, "deck2 now focused -> cyan");
    CHECK(find(29).color == ledcolor::GREEN && find(29).intensity == 1, "PLAY dim (focused deck2 loaded, stopped)");

    if (g_fail == 0) std::printf("ALL LED-MAP TESTS PASSED\n");
    else             std::printf("%d CHECK(S) FAILED\n", g_fail);
    return g_fail ? 1 : 0;
}
