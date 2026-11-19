// Conservative native UI/filter coverage, submitted as clipped full-UV draws.
#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sawgod {
// The one rule for where Enhanced shows the ordinary (WSC) picture instead of the Enhanced scene:
// B = 0xFF on owned pixels only, UI (R) and, with enemies, protected enemy pixels (G 0xFE/0xFF).
// Never a halo: a margin shows ordinary scene pixels around every window and speaker tab.
// Every Enhanced path (flat/battle and the field's Mode 7) builds its coverage here.
inline void restoration_coverage(const uint32_t *mask, uint32_t *out, size_t count, bool enemies) {
    for (size_t i = 0; i < count; ++i) {
        out[i] = mask[i];
        if ((mask[i] & 0xFF0000) || (enemies && (mask[i] & 0x00FE00) == 0x00FE00))
            out[i] |= 0xFF;
    }
}
class ProtectedRegions {
  public:
    void update(const uint32_t *mask, bool enemies = false) {
        // Eight-native-pixel cells keep draw count small. A two-pixel halo includes the scaler's
        // one-cell support plus exact-boundary probes, at every fractional output size.
        std::array<bool, 28 * 18> cells{};
        for (int y = 0; y < 144; ++y)
            for (int x = 0; x < 224; ++x) {
                const uint32_t p = mask[y * 224 + x];
                if (!(p & 0xFF0000) && !(enemies && (p & 0x00FE00) == 0x00FE00))
                    continue;
                for (int cy = std::max(0, y - 2) / 8; cy <= std::min(143, y + 2) / 8; ++cy)
                    for (int cx = std::max(0, x - 2) / 8; cx <= std::min(223, x + 2) / 8; ++cx)
                        cells[size_t(cy * 28 + cx)] = true;
            }
        rectangles_.clear();
        for (int y = 0; y < 18; ++y)
            for (int x = 0; x < 28;) {
                if (!cells[size_t(y * 28 + x)]) {
                    ++x;
                    continue;
                }
                const int left = x;
                while (x < 28 && cells[size_t(y * 28 + x)])
                    ++x;
                auto same = std::find_if(rectangles_.begin(), rectangles_.end(), [&](const SDL_Rect &r) {
                    return r.x == left * 8 && r.w == (x - left) * 8 && r.y + r.h == y * 8;
                });
                if (same != rectangles_.end())
                    same->h += 8;
                else
                    rectangles_.push_back({left * 8, y * 8, (x - left) * 8, 8});
            }
    }
    bool empty() const { return rectangles_.empty(); }
    template <class Draw> bool draw(SDL_Renderer *renderer, const SDL_FRect &dst, Draw submit) {
        pixels = 0;
        draws = 0;
        const bool clipped = SDL_RenderClipEnabled(renderer);
        SDL_Rect previous{};
        if (!SDL_GetRenderClipRect(renderer, &previous))
            return false;
        bool ok = true;
        for (const auto &r : rectangles_) {
            // Round shared boundaries once; adjacent regions neither overlap nor leave holes.
            const int left = int(std::floor(dst.x + r.x * dst.w / 224));
            const int top = int(std::floor(dst.y + r.y * dst.h / 144));
            const int right = int(std::floor(dst.x + (r.x + r.w) * dst.w / 224));
            const int bottom = int(std::floor(dst.y + (r.y + r.h) * dst.h / 144));
            SDL_Rect clip{left, top, right - left, bottom - top};
            if (clipped && !SDL_GetRectIntersection(&clip, &previous, &clip))
                continue;
            if (clip.w <= 0 || clip.h <= 0)
                continue;
            ok = SDL_SetRenderClipRect(renderer, &clip) && submit() && ok;
            pixels += uint64_t(clip.w) * uint64_t(clip.h);
            ++draws;
        }
        return SDL_SetRenderClipRect(renderer, clipped ? &previous : nullptr) && ok;
    }
    uint64_t pixels = 0;
    unsigned draws = 0;

  private:
    std::vector<SDL_Rect> rectangles_;
};
} // namespace sawgod
