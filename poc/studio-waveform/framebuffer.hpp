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
};

}  // namespace mxb
