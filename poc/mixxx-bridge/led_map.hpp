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

// Build LED commands from deck state. The bridge only lights what maps cleanly to
// deck state and doesn't reserve those buttons for its own input:
//   pads 1-8/9-16 : deck 1/2 colour (playing green, loaded blue dim)
//   GROUP A/B     : deck 1/2 indicator (playing green, loaded blue dim)
// PLAY/CUE/SYNC are forwarded raw to Mixxx now (user-mapped), so the bridge
// leaves their LEDs alone.
inline void computeLeds(const DeckModel& m, std::vector<LedCmd>& out) {
    out.clear();
    auto rgbUnit = [&](int rI, int gI, int bI, uint8_t r, uint8_t g, uint8_t b) {
        auto chan = [&](int idx, uint8_t inten) {
            if (idx > 0)
                out.push_back({idx,
                               static_cast<uint8_t>(inten ? ledcolor::WHITE : ledcolor::OFF),
                               inten});
        };
        chan(rI, r); chan(gI, g); chan(bI, b);
    };

    for (int deck = 1; deck <= 2; ++deck) {
        uint8_t r, g, b; padColorFor(m.deck(deck), r, g, b);
        const int first = (deck == 1) ? 1 : 9;          // pads
        for (int p = first; p < first + 8; ++p)
            rgbUnit(studioled::padRGB(p, 0), studioled::padRGB(p, 1), studioled::padRGB(p, 2),
                    r, g, b);
        rgbUnit(studioled::groupRGB(deck, 0), studioled::groupRGB(deck, 1),  // GROUP A/B
                studioled::groupRGB(deck, 2), r, g, b);
    }
}

}  // namespace mxb
