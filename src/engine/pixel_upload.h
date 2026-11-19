// Exact pixel uploads: one bounding rectangle, or no driver work for an unchanged image.
#pragma once
#include "engine/presentation_diagnostics.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace sawgod {
inline bool upload_pixels(SDL_Texture *texture, const uint32_t *pixels, int width, int height,
                          std::vector<uint32_t> &previous) {
    const size_t size = size_t(width) * size_t(height);
    SDL_Rect rect{0, 0, width, height};
    if (!presentation_diagnostics::fresh() && previous.size() == size) {
        if (std::memcmp(previous.data(), pixels, size * sizeof(uint32_t)) == 0)
            return true;
        int left = width, right = -1, top = height, bottom = -1;
        for (int y = 0; y < height; ++y) {
            const size_t row = size_t(y * width);
            if (std::memcmp(previous.data() + row, pixels + row, size_t(width) * sizeof(uint32_t)) == 0)
                continue;
            top = std::min(top, y);
            bottom = y;
            for (int x = 0; x < width; ++x)
                if (previous[row + size_t(x)] != pixels[row + size_t(x)]) {
                    left = std::min(left, x);
                    right = std::max(right, x);
                }
        }
        rect = {left, top, right - left + 1, bottom - top + 1};
    }
    if (!presentation_diagnostics::cpu_only() &&
        !SDL_UpdateTexture(texture, &rect, pixels + rect.y * width + rect.x, width * 4))
        return false;
    previous.assign(pixels, pixels + size);
    return true;
}
} // namespace sawgod
