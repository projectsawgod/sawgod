// A synthetic window tile set in video memory for UI tests (game/ui_chrome.h): border index 3 on the
// window's outer edge, fill index 10; palette 15 (Normal) and 7 (Inactive) with distinct colors.
#pragma once
#include "emu/wsc.h"

namespace chrome_fixture {
constexpr uint32_t kBorder = 0xFF0000, kFill = 0x0000FF, kInactiveFill = 0x00FFFF, kText = 0xFFFFFF,
                   kHeal = 0x00FF00, kEmpty = 0x222222, kAccent = 0xFFFF00;
// Host HP bars have their own accessible colors, independent of palette roles.
constexpr uint32_t kHpFull = 0x58883B, kHpEmpty = 0x293326, kHpShadow = 0x1F3015;

inline void put(sawgod::emu::Wsc &w, int tile, int x, int y, int index, bool second = false) {
    const uint16_t at = uint16_t(0x4000 + tile * 32 + y * 4 + x / 2);
    uint8_t b = second ? w.shadow_read8(at) : w.ram[at];
    b = x & 1 ? uint8_t((b & 0xF0) | index) : uint8_t((b & 0x0F) | index << 4);
    if (second)
        w.shadow_write8(at, b);
    else
        w.ram[at] = b;
}

inline void install(sawgod::emu::Wsc &w, int border = 3) {
    w.io[0x60] = 0x80; // color mode
    for (int t = 0; t < 9; ++t)
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                const bool edge = (t < 3 && y == 0) || (t >= 6 && y == 7) || (t % 3 == 0 && x == 0) ||
                                  (t % 3 == 2 && x == 7);
                put(w, 0x1D5 + t, x, y, edge ? border : 10);
            }
    auto color = [&](int pal, int index, uint16_t c) {
        w.ram[0xFE00 + pal * 32 + index * 2] = uint8_t(c);
        w.ram[0xFE00 + pal * 32 + index * 2 + 1] = uint8_t(c >> 8);
    };
    for (int pal : {7, 15}) {
        color(pal, 3, 0xF00);
        color(pal, 10, pal == 15 ? 0x00F : 0x0FF);
        color(pal, 1, 0x000);
        color(pal, 2, 0xFFF);
        color(pal, 6, 0x222);
        color(pal, 7, 0x777);
        color(pal, 12, 0xFF0);
        color(pal, 15, 0x0F0);
    }
}
} // namespace chrome_fixture
