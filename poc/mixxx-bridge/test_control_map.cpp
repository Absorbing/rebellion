// Unit tests for the Studio-control -> outbound MIDI mapping (control_map.hpp).

#include <cstdio>
#include "control_map.hpp"

using namespace mxb;

static int g_fail = 0;
#define CHECK(c, m) do { if(!(c)){ std::printf("FAIL: %s\n", m); ++g_fail; } } while(0)

int main() {
    // Buttons -> Note on/off ch1, note = buttonid.
    {
        auto on  = mapButton(29, true);   // PLAY pressed
        auto off = mapButton(29, false);  // PLAY released
        CHECK(on.status == 0x90 && on.data1 == 29 && on.data2 == 0x7F, "button press = 90 1D 7F");
        CHECK(off.status == 0x90 && off.data1 == 29 && off.data2 == 0x00, "button release = 90 1D 00");
        auto g = mapButton(16, true);     // GROUP_A
        CHECK(g.data1 == 16, "GROUP_A note = 16");
    }

    // Pads -> Note ch2, note = padid, velocity = pressure (clamped 1..127).
    {
        auto hit  = mapPad(5, true, 100);
        auto soft = mapPad(5, true, 0);     // pressed but 0 pressure -> clamp to 1
        auto rel  = mapPad(5, false, 0);
        auto loud = mapPad(16, true, 999);  // clamp to 127
        CHECK(hit.status == 0x91 && hit.data1 == 5 && hit.data2 == 100, "pad hit = 91 05 64");
        CHECK(soft.data2 == 1, "pad press clamps vel to >=1");
        CHECK(rel.status == 0x91 && rel.data2 == 0, "pad release vel 0");
        CHECK(loud.data1 == 16 && loud.data2 == 127, "pad vel clamps to 127");
    }

    // Knobs -> relative CC ch1, cc = 0x30+(knob-1).
    {
        auto cw  = mapKnobRotate(1, true);
        auto ccw = mapKnobRotate(8, false);
        CHECK(cw.status == 0xB0 && cw.data1 == 0x30 && cw.data2 == 0x41, "knob1 CW = B0 30 41");
        CHECK(ccw.status == 0xB0 && ccw.data1 == 0x37 && ccw.data2 == 0x3F, "knob8 CCW = B0 37 3F");
    }

    if (g_fail == 0) std::printf("ALL CONTROL-MAP TESTS PASSED\n");
    else             std::printf("%d CHECK(S) FAILED\n", g_fail);
    return g_fail ? 1 : 0;
}
