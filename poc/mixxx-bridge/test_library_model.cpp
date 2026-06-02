// Unit tests for the library browse model (cursor + visible-window math).

#include <cstdio>
#include "library_model.hpp"

using namespace mxb;

static int g_fail = 0;
#define CHECK(c, m) do { if(!(c)){ std::printf("FAIL: %s\n", m); ++g_fail; } } while(0)

int main() {
    LibraryModel lib;

    // Empty model: moves are no-ops, no crash.
    CHECK(lib.empty() && lib.size() == 0, "empty initially");
    CHECK(lib.move(5) == 0, "move on empty returns 0");

    std::vector<LibRow> rows;
    for (int i = 0; i < 100; ++i) rows.push_back({i + 1, "Artist" + std::to_string(i), "T"});
    lib.setTracks(rows);
    CHECK(lib.size() == 100 && lib.cursor() == 0, "100 tracks, cursor at 0");

    // Move + clamp + actual-delta return (for lock-step).
    CHECK(lib.move(3) == 3 && lib.cursor() == 3, "move +3");
    CHECK(lib.move(-10) == -3 && lib.cursor() == 0, "clamp at top, returns actual -3");
    CHECK(lib.move(1000) == 99 && lib.cursor() == 99, "clamp at bottom, returns actual +99");
    CHECK(lib.selected().id == 100, "selected = last track id");

    // Visible window keeps the cursor on screen.
    lib.toTop();
    CHECK(lib.windowStart(10) == 0, "top -> window 0");
    lib.move(50);
    int ws = lib.windowStart(10);
    CHECK(ws <= 50 && 50 < ws + 10, "cursor 50 within 10-row window");
    lib.move(1000);  // bottom
    CHECK(lib.windowStart(10) == 90, "bottom -> window shows last 10");

    // Fewer tracks than rows -> window always 0.
    LibraryModel small;
    small.setTracks({{1, "A", "x"}, {2, "B", "y"}});
    CHECK(small.windowStart(10) == 0, "small list -> window 0");

    if (g_fail == 0) std::printf("ALL LIBRARY-MODEL TESTS PASSED\n");
    else             std::printf("%d CHECK(S) FAILED\n", g_fail);
    return g_fail ? 1 : 0;
}
