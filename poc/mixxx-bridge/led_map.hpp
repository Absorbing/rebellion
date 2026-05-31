// Mixxx Studio Bridge — deck state -> button LED feedback.
//
// Drives the Studio's button LEDs from the DeckState we already track, via
// rebellion's sendLedData (host passes a raw LED array index 1..ledcnt). The
// Studio LED indices in rebellion's mappings.lua are placeholders (a v0.0 trial
// config), so the index layout below is a best guess — confirm with the LED
// probe (run studio_bridge with MXB_LED_PROBE=1) and adjust `LedLayout`.

#pragma once
#include <cstdint>
#include <vector>

#include "deck_state.hpp"

namespace mxb {

// niproto colour codes (scripts/niproto.lua). intensity is 0..3 (off..bright).
namespace ledcolor {
constexpr uint8_t OFF = 0, RED = 1, ORANGE = 2, YELLOW = 5, GREEN = 7,
                  CYAN = 9, BLUE = 11, PURPLE = 14, WHITE = 17;
}

struct LedCmd {
    int     index = 0;       // LED array index (1-based)
    uint8_t color = 0;       // 0..17
    uint8_t intensity = 0;   // 0..3
    bool operator==(const LedCmd& o) const {
        return index == o.index && color == o.color && intensity == o.intensity;
    }
};

// Which LED index belongs to which control. Indices are unconfirmed for the
// Studio (placeholders in mappings.lua) — verify on hardware and edit here.
struct LedLayout {
    // Deck play buttons: GROUP_A = deck 1, GROUP_B = deck 2 (button ids 16/19;
    // LED index guessed = button id until the probe confirms).
    int deckPlay[3] = {0, 16, 19};
    // Deck cue buttons: GROUP_C / GROUP_D (button ids 20/23).
    int deckCue[3]  = {0, 20, 23};
};

// Compute the desired LED commands for decks 1..2 from the model.
// play button: playing -> GREEN bright, loaded/stopped -> GREEN dim, empty -> off.
// cue button:  loaded -> WHITE dim, else off.
inline void computeDeckLeds(const DeckModel& m, const LedLayout& L,
                            std::vector<LedCmd>& out) {
    out.clear();
    for (int n = 1; n <= 2; ++n) {
        const DeckState& d = m.deck(n);
        LedCmd play{L.deckPlay[n], ledcolor::OFF, 0};
        if (d.loaded) { play.color = ledcolor::GREEN; play.intensity = d.playing ? 3 : 1; }
        out.push_back(play);

        LedCmd cue{L.deckCue[n], ledcolor::OFF, 0};
        if (d.loaded) { cue.color = ledcolor::WHITE; cue.intensity = 1; }
        out.push_back(cue);
    }
}

}  // namespace mxb
