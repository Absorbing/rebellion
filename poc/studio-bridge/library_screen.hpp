// Mixxx Studio Bridge — library list screen.
//
// Renders the LibraryModel as a scrolling track list with the cursor highlighted
// and a position scrollbar. Pure framebuffer drawing (compile/render-tested).

#pragma once
#include <string>

#include "framebuffer.hpp"
#include "deck_panel.hpp"     // dp palette + textWidth/rightText
#include "library_model.hpp"

namespace mxb {

inline std::string truncTo(const std::string& s, int maxChars) {
    if (static_cast<int>(s.size()) <= maxChars || maxChars < 2) return s;
    return s.substr(0, maxChars - 1) + ">";
}

// Render the browse list. `focusedDeck` (1/2) is the load target shown in header.
inline void renderLibrary(Framebuffer& fb, const LibraryModel& lib, int focusedDeck) {
    using namespace dp;
    fb.clear(BG);

    // header
    fb.fillRect(0, 0, kW, 20, PANEL);
    fb.text(6, 4, "LIBRARY", CYAN, 2);
    char deck = static_cast<char>('A' + (focusedDeck - 1));
    std::string hdr = std::to_string(lib.empty() ? 0 : lib.cursor() + 1) + "/" +
                      std::to_string(lib.size()) + "  ENTER>" + deck;
    rightText(fb, kW - 6, 6, hdr, DIM, 1);

    if (lib.empty()) {
        fb.text(150, 130, "no tracks", DIM, 2);
        return;
    }

    const int rows = 11, rowH = 22, top = 24;
    const int start = lib.windowStart(rows);
    const int maxChars = (kW - 14) / (6 * 2);   // scale-2 glyphs are 12px wide
    for (int i = 0; i < rows && start + i < lib.size(); ++i) {
        const int idx = start + i;
        const int y = top + i * rowH;
        const LibRow& r = lib.at(idx);
        const bool sel = (idx == lib.cursor());
        if (sel) fb.fillRect(0, y - 2, kW - 6, rowH, CYAN);
        std::string line = r.artist.empty() ? r.title
                                            : (r.artist + " - " + r.title);
        if (line.empty()) line = "(untitled)";
        fb.text(6, y, truncTo(line, maxChars), sel ? BG : WHITE, 2);
    }

    // scrollbar (right edge): thumb height/position from cursor fraction
    const int trackH = rows * rowH;
    int thumb = trackH * rows / lib.size();
    if (thumb < 6) thumb = 6;
    int ty = top + (trackH - thumb) * lib.cursor() / (lib.size() > 1 ? lib.size() - 1 : 1);
    fb.fillRect(kW - 4, top, 4, trackH, PANEL);
    fb.fillRect(kW - 4, ty, 4, thumb, CYAN);
}

}  // namespace mxb
