// Mixxx Studio Bridge — LED command types.
//
// The bridge no longer paints controls from deck state (that was a placeholder
// and overstepped pads/GROUP buttons the user maps themselves). LEDs stay dark
// until purposeful feedback is wired (hotcue colours on pads, peak meters), at
// which point those features build their LedCmds here. studio_leds.hpp holds the
// index map; main.cpp::sendLed drives a single LED.

#pragma once
#include <cstdint>

#include "studio_leds.hpp"

namespace mxb {

namespace ledcolor {  // niproto colour codes (scripts/niproto.lua)
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

}  // namespace mxb
