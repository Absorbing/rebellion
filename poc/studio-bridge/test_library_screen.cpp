// Render-smoke test for the library list screen. No hardware.

#include <cstdio>
#include "library_screen.hpp"

using namespace mxb;

static int g_fail = 0;
#define CHECK(c, m) do { if(!(c)){ std::printf("FAIL: %s\n", m); ++g_fail; } } while(0)

int main() {
    LibraryModel lib;

    // Empty list renders (the "no tracks" path) and encodes.
    { Framebuffer fb; renderLibrary(fb, lib, 1);
      CHECK(fb.encodeDisplayCommands(0).size() > 16, "empty library renders"); }

    std::vector<LibRow> rows;
    for (int i = 0; i < 500; ++i)
        rows.push_back({i + 1, "Artist " + std::to_string(i),
                        "A Track Title That Is Quite Long " + std::to_string(i)});
    lib.setTracks(rows);
    lib.move(250);  // middle

    for (int deck = 1; deck <= 2; ++deck) {
        Framebuffer fb;
        renderLibrary(fb, lib, deck);
        auto cmd = fb.encodeDisplayCommands(deck - 1);
        CHECK(fb.px.size() == static_cast<size_t>(kW) * kH, "framebuffer sized");
        CHECK(cmd.size() > 16, "library renders + encodes");
        std::printf("  library deck %d (cursor 250/500) -> %zu cmd bytes\n", deck, cmd.size());
    }

    // Top and bottom edges render without going out of range.
    lib.toTop();      { Framebuffer fb; renderLibrary(fb, lib, 1); (void)fb; }
    lib.move(10000);  { Framebuffer fb; renderLibrary(fb, lib, 1); (void)fb; }

    if (g_fail == 0) std::printf("ALL LIBRARY-SCREEN TESTS PASSED\n");
    else             std::printf("%d CHECK(S) FAILED\n", g_fail);
    return g_fail ? 1 : 0;
}
