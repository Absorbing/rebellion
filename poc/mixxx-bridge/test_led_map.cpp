// Tests for the probed Studio LED index map (studio_leds.hpp).

#include <cstdio>
#include "led_map.hpp"   // pulls studio_leds.hpp + LedCmd

using namespace mxb;

static int g_fail = 0;
#define CHECK(c, m) do { if(!(c)){ std::printf("FAIL: %s\n", m); ++g_fail; } } while(0)

int main() {
    using namespace mxb::studioled;

    // Pad RGB triples and whites at the probed addresses.
    CHECK(padRGB(1, 0) == 1  && padRGB(8, 2) == 24, "pads 1-8 RGB at 1..24");
    CHECK(padRGB(9, 0) == 63 && padRGB(16, 2) == 86, "pads 9-16 RGB at 63..86");
    CHECK(padWhite(1) == 25 && padWhite(16) == 94, "pad whites 25 / 94");
    CHECK(padRGB(0, 0) == 0 && padRGB(17, 0) == 0, "pad out of range -> 0");

    // GROUP A-H RGB + whites.
    CHECK(groupRGB(1, 0) == 107 && groupRGB(8, 2) == 130, "groups A-H RGB 107..130");
    CHECK(groupWhite(1) == 131 && groupWhite(8) == 138, "group whites 131..138");

    // Named single LEDs + meters + jog ring.
    CHECK(PLAY == 147 && REC == 148 && BROWSE == 45, "named transport/section LEDs");
    CHECK(peakLeft(0) == 159 && peakLeft(15) == 174, "peak L 159..174");
    CHECK(peakRight(0) == 175 && peakRight(15) == 190, "peak R 175..190");
    CHECK(jogRing(0) == 199 && jogRing(14) == 213, "jog ring 199..213");
    CHECK(LEDCNT == 213, "ledcnt 213");

    // Button ids.
    CHECK(btn::PLAY == 29 && btn::BROWSE == 5 && btn::JOG_CLICK == 51, "key button ids");
    CHECK(btn::GROUP_A == 16 && btn::ENTER == 52 && btn::BACK == 55, "nav button ids");

    // LedCmd equality (used by future feedback).
    CHECK((LedCmd{1, ledcolor::GREEN, 3} == LedCmd{1, ledcolor::GREEN, 3}), "LedCmd ==");

    if (g_fail == 0) std::printf("ALL STUDIO-LED MAP TESTS PASSED\n");
    else             std::printf("%d CHECK(S) FAILED\n", g_fail);
    return g_fail ? 1 : 0;
}
