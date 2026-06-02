// Mixxx Studio Bridge — deck state -> pad LED feedback.
//
// Drives the Studio's 16 RGB pads from DeckState (deck 1 = pads 1-8, deck 2 =
// pads 9-16). The transport/GROUP button LEDs aren't addressable within the
// device's ledcnt (see studio_leds.hpp), so deck feedback lives on the pads —
// which is also where hotcue/sampler colouring will go. Focus is shown on-screen
// (the cyan deck border), not on LEDs.
//
// Each pad colour is three single-channel writes (R/G/B indices). A channel is
// lit with color=WHITE + intensity, off with color=OFF.

#pragma once
#include <cstdint>
#include <vector>

#include "deck_state.hpp"
#include "studio_leds.hpp"

namespace mxb {

namespace ledcolor {  // niproto colour codes (only WHITE/OFF needed for channels)
constexpr uint8_t OFF = 0, WHITE = 17;
}

struct LedCmd {
    int     index = 0;       // LED array index (1-based)
    uint8_t color = 0;       // 0..17
    uint8_t intensity = 0;   // 0..3
    bool operator==(const LedCmd& o) const {
        return index == o.index && color == o.color && intensity == o.intensity;
    }
};

// RGB intensities (0..3) for one deck's pad colour:
//   playing -> green ; loaded+stopped -> blue (dim) ; empty -> off.
inline void padColorFor(const DeckState& d, uint8_t& r, uint8_t& g, uint8_t& b) {
    r = g = b = 0;
    if (!d.loaded) return;
    if (d.playing) g = 3;        // green, bright
    else           b = 1;        // blue, dim (loaded, stopped)
}

// Build LED commands from deck state. Now that the upper LED bank is mapped:
//   pads 1-8/9-16 : deck 1/2 colour (playing green, loaded blue dim)
//   GROUP A/B     : focused deck -> cyan ; loaded+unfocused -> blue dim ; off
//   PLAY          : focused deck playing -> bright, loaded -> dim, else off
//   RESTART (cue) : focused deck loaded -> dim
//   GRID (sync)   : focused deck sync on -> bright, loaded -> dim
inline void computeLeds(const DeckModel& m, int focusedDeck, std::vector<LedCmd>& out) {
    out.clear();
    auto chan = [&](int idx, uint8_t inten) {
        if (idx > 0)
            out.push_back({idx, static_cast<uint8_t>(inten ? ledcolor::WHITE : ledcolor::OFF),
                           inten});
    };
    auto rgbUnit = [&](int rI, int gI, int bI, uint8_t r, uint8_t g, uint8_t b) {
        chan(rI, r); chan(gI, g); chan(bI, b);
    };

    // Pads: deck 1 = 1-8, deck 2 = 9-16.
    for (int deck = 1; deck <= 2; ++deck) {
        uint8_t r, g, b; padColorFor(m.deck(deck), r, g, b);
        int first = (deck == 1) ? 1 : 9;
        for (int p = first; p < first + 8; ++p)
            rgbUnit(studioled::padRGB(p, 0), studioled::padRGB(p, 1), studioled::padRGB(p, 2),
                    r, g, b);
    }

    // GROUP A/B = deck 1/2 focus + load indicator.
    for (int deck = 1; deck <= 2; ++deck) {
        uint8_t r = 0, g = 0, b = 0;
        if (focusedDeck == deck)         { g = 3; b = 3; }   // cyan = focused
        else if (m.deck(deck).loaded)    { b = 1; }          // blue dim = loaded, unfocused
        rgbUnit(studioled::groupRGB(deck, 0), studioled::groupRGB(deck, 1),
                studioled::groupRGB(deck, 2), r, g, b);
    }

    // Transport reflects the focused deck (matches the input routing).
    const DeckState& f = m.deck(focusedDeck);
    chan(studioled::PLAY,    f.loaded ? (f.playing ? 3 : 1) : 0);
    chan(studioled::RESTART, f.loaded ? 1 : 0);              // CUE
    chan(studioled::GRID,    f.sync ? 3 : (f.loaded ? 1 : 0)); // SYNC
}

}  // namespace mxb
