// PoC RGB565 framebuffer + simple drawing for the 480x272 Studio panels.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "font5x7.hpp"

namespace mxb {

constexpr int kW = 480;
constexpr int kH = 272;

// RGB565 helpers.
inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

struct Framebuffer {
    std::vector<uint16_t> px;  // row-major, kW*kH
    Framebuffer() : px(static_cast<size_t>(kW) * kH, 0) {}

    void clear(uint16_t c) { std::fill(px.begin(), px.end(), c); }

    void set(int x, int y, uint16_t c) {
        if (x < 0 || x >= kW || y < 0 || y >= kH) return;
        px[static_cast<size_t>(y) * kW + x] = c;
    }

    void fillRect(int x0, int y0, int w, int h, uint16_t c) {
        for (int y = y0; y < y0 + h; ++y)
            for (int x = x0; x < x0 + w; ++x) set(x, y, c);
    }

    void vline(int x, int y0, int y1, uint16_t c) {
        if (y0 > y1) std::swap(y0, y1);
        for (int y = y0; y <= y1; ++y) set(x, y, c);
    }

    // Draw text with the 5x7 font at scale `s`. Returns the x after the string.
    int text(int x, int y, const std::string& str, uint16_t c, int s = 1) {
        int cx = x;
        for (char ch : str) {
            const uint8_t* g = glyph(ch);
            for (int col = 0; col < 5; ++col) {
                uint8_t bits = g[col];
                for (int row = 0; row < 7; ++row) {
                    if (bits & (1 << row))
                        fillRect(cx + col * s, y + row * s, s, s, c);
                }
            }
            cx += 6 * s;  // 5 cols + 1 spacing
        }
        return cx;
    }

    // Flat RGB565 int array for rebellion.sendDataToDisplay.
    std::vector<int> asIntArray() const {
        return std::vector<int>(px.begin(), px.end());
    }

    // Build the full device display-command byte stream (header + pixel
    // commands + blit + end) for rebellion.sendDisplayCmd, RLE-compressing
    // flat regions. This moves all per-pixel work into compiled C++ and shrinks
    // what crosses the pipe / Lua from ~260 KB to a few KB on these mostly-flat
    // synthetic screens — the old path round-tripped 130k JSON ints per frame.
    //
    // The device consumes pixels in pairs; both commands count pixel-PAIRS:
    //   transmit: 0x00 0x00 <pairs:16> then 2 bytes/pixel  (raw)
    //   repeat  : 0x01 0x00 <pairs:16> <p1:16> <p2:16>     (pair repeated)
    // Mixed commands accumulate into one frame, then blit + end commit it.
    std::vector<int> encodeDisplayCommands(int display) const {
        std::vector<int> d;
        d.reserve(4096);
        auto u8  = [&](int b)       { d.push_back(b & 0xff); };
        auto u16 = [&](int v)       { u8((v >> 8) & 0xff); u8(v & 0xff); };
        auto pix = [&](uint16_t p)  { u8((p >> 8) & 0xff); u8(p & 0xff); };

        // header (x=0,y=0,w=480,h=272 — matches _display_cmd_header)
        u8(0x84); u8(0x00); u8(display); u8(0x60); u8(0x00); u8(0x00); u8(0x00); u8(0x00);
        u8(0); u8(0); u8(0); u8(0);
        u8(0x01); u8(0xe0); u8(0x01); u8(0x10);

        const size_t nPairs = px.size() / 2;   // px.size() == kW*kH (even)
        std::vector<uint16_t> lit;             // pending raw pixels (even length)
        auto flushLit = [&]() {
            if (lit.empty()) return;
            u8(0x00); u8(0x00); u16(static_cast<int>(lit.size() / 2));
            for (uint16_t p : lit) pix(p);
            lit.clear();
        };

        constexpr int REPEAT_MIN = 3;          // pairs; below this, raw is cheaper
        size_t i = 0;
        while (i < nPairs) {
            uint16_t a = px[2 * i], b = px[2 * i + 1];
            size_t run = 1;
            while (i + run < nPairs && run < 0xffff &&
                   px[2 * (i + run)] == a && px[2 * (i + run) + 1] == b)
                ++run;
            if (run >= static_cast<size_t>(REPEAT_MIN)) {
                flushLit();
                u8(0x01); u8(0x00); u16(static_cast<int>(run)); pix(a); pix(b);
            } else {
                for (size_t k = 0; k < run; ++k) { lit.push_back(a); lit.push_back(b); }
            }
            i += run;
        }
        flushLit();

        u8(0x03); u8(0x00); u8(0x00); u8(0x00);          // blit
        u8(0x40); u8(0x00); u8(display); u8(0x00);        // end (display index in byte 3)
        return d;
    }
};

}  // namespace mxb
