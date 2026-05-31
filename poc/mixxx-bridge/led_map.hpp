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
// Studio (placeholders in mappings.lua) — verify on hardware with MXB_LED_PROBE
// and edit here. Defaults guess index = button id.
struct LedLayout {
    int groupA = 16;   // deck 1 focus indicator (GROUP_A button)
    int groupB = 19;   // deck 2 focus indicator (GROUP_B button)
    int play   = 29;   // PLAY button  (reflects focused deck)
    int cue    = 28;   // RESTART button used as CUE
    int sync   = 27;   // GRID button used as SYNC
};

// Compute LED commands for the deck-focus model:
//   GROUP_A/B : focused deck -> CYAN bright; other deck loaded -> BLUE dim; else off
//   PLAY      : focused deck playing -> GREEN bright; loaded -> GREEN dim; else off
//   CUE       : focused deck loaded -> WHITE dim
//   SYNC      : focused deck sync on -> BLUE bright; loaded -> BLUE dim; else off
inline void computeLeds(const DeckModel& m, int focusedDeck, const LedLayout& L,
                        std::vector<LedCmd>& out) {
    out.clear();
    auto focusInd = [&](int idx, int deck) {
        LedCmd c{idx, ledcolor::OFF, 0};
        if (focusedDeck == deck)      { c.color = ledcolor::CYAN; c.intensity = 3; }
        else if (m.deck(deck).loaded) { c.color = ledcolor::BLUE; c.intensity = 1; }
        out.push_back(c);
    };
    focusInd(L.groupA, 1);
    focusInd(L.groupB, 2);

    const DeckState& f = m.deck(focusedDeck);
    LedCmd play{L.play, ledcolor::OFF, 0};
    if (f.loaded) { play.color = ledcolor::GREEN; play.intensity = f.playing ? 3 : 1; }
    out.push_back(play);

    out.push_back({L.cue, static_cast<uint8_t>(f.loaded ? ledcolor::WHITE : ledcolor::OFF),
                   static_cast<uint8_t>(f.loaded ? 1 : 0)});

    LedCmd sync{L.sync, ledcolor::OFF, 0};
    if (f.sync)        { sync.color = ledcolor::BLUE; sync.intensity = 3; }
    else if (f.loaded) { sync.color = ledcolor::BLUE; sync.intensity = 1; }
    out.push_back(sync);
}

}  // namespace mxb
