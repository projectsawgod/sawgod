// Optional, scanline-resolved presentation data. No game-specific ownership lives here.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace sawgod::emu {
struct RasterPixel {
    uint16_t color = 0, tile = 0, cell = 0;
    uint8_t palette = 0, index = 0, sprite = 255, flags = 0;
    // Present is independent of palette index zero (which may be opaque).
    int16_t x = 0, y = 0; // latched sprite tile origin, in signed screen pixels
    enum : uint8_t { Present = 1, HostText = 2, HFlip = 4, VFlip = 8, Window = 16 };
};
struct FrameLayers {
    static constexpr int Width = 224, Height = 144, Pixels = Width * Height;
    enum Layer { Backdrop, Screen1, SpriteBehind, Screen2, SpriteFront, Host, Count };
    std::array<std::array<RasterPixel, Pixels>, Count> planes{};
    std::array<std::array<uint8_t, 24>, Height> registers{};
    std::array<uint8_t, Height> valid{};
    const RasterPixel &top(size_t at) const {
        for (int i = Count - 1; i > 0; --i)
            if (planes[size_t(i)][at].flags & RasterPixel::Present)
                return planes[size_t(i)][at];
        return planes[Backdrop][at];
    }
    uint16_t color(size_t at) const { return top(at).color; }
};
} // namespace sawgod::emu
