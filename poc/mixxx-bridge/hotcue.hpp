// Mixxx Studio Bridge — a hotcue marker for the waveform view.

#pragma once
#include <cstdint>

namespace mxb {

struct Hotcue {
    int     number = 0;      // 0-based hotcue index (display as number+1)
    double  fraction = 0.0;  // position as a fraction of the track (0..1)
    uint8_t r = 0, g = 0, b = 0;
};

}  // namespace mxb
