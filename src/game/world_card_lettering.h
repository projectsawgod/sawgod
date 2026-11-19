// English world names are baked pictures. Native hooks own every scene boundary and dissolve row.
#pragma once
#include "engine/indexed_png.h"
#include "game/hooks.h"
#include <array>
namespace sawgod::emu {
class Wsc;
}
namespace sawgod::game {
class WorldCardLettering {
  public:
    struct Rect {
        int x, y, w, h;
    };
    struct Part {
        RgbaImage image; // Trimmed transparent PNG; x/y retain its position on the full canvas.
        int x = 0, y = 0;
        std::array<int, 4> ink{};
    };
    void attach(emu::Wsc &w);
    bool active(emu::Wsc &w) const;
    float opacity(emu::Wsc &w) const;
    int card() const { return card_; }
    static const RgbaImage &image(int card, bool enhanced = false);
    static const Part &part(int card, bool enhanced, int index);
    // Visible rectangles in canvas pixels, mapped from the native kanji's 32 raster edges.
    std::vector<Rect> slices(bool enhanced, int index) const;
    void composite(emu::Wsc &w, std::vector<uint32_t> &rgb, bool enhanced = false) const;

  private:
    hooks::Subscription subscription_;
    uint64_t generation_ = 0;
    int card_ = 0, fade_ = 0;
    std::array<int, 32> edges_{};
};
} // namespace sawgod::game
