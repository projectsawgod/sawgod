// The world-map minimap: the rules, kept apart from drawing so the tests check the
// app's own mapping. Only on the four world maps, while the party walks the Enhanced Mode 7 world view; the
// whole 64x64-tile world at 2x2 averaged colors per tile (Mode7Renderer::draw_minimap), a breathing one-game-
// pixel dot on the party's tile, bottom right of the game picture. Settings: enhanced.minimap (Options >
// Enhanced > Minimap) and enhanced.minimap_size; the Minimap button (M / L2) cycles Off -> Small -> Large ->
// Off.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace sawgod::app {

enum class MinimapSize { Off, Small, Large };

// The two settings as one size: Options' On/Off and the last size shown (Small or Large).
inline MinimapSize minimap_size(bool on, bool large) {
    return !on ? MinimapSize::Off : large ? MinimapSize::Large : MinimapSize::Small;
}
// The Minimap button: Off -> Small -> Large -> Off. Off keeps Large as the size Options' On restores.
inline void minimap_cycle(bool &on, bool &large) {
    if (!on) {
        on = true;
        large = false;
    } else if (!large)
        large = true;
    else
        on = false;
}

// Where the party walks the world: the button is read, and the minimap may show, only here.
struct MinimapScene {
    bool world_map = false; // GameConfig::world_map
    bool field = false;     // the field loop owns the picture (no battle, no full-screen menu)
    bool mode7 = false;     // the Enhanced world camera drew this picture (never GB or WSC graphics)
    bool menu = false;      // Options or a native menu window
    bool dialogue = false;  // a message box
    bool battle = false;
    bool transition = false; // a script or field fade runs (map changes, warps, fades)
    bool fast_forward = false;
};
inline bool minimap_context(const MinimapScene &s) {
    return s.world_map && s.field && s.mode7 && !s.menu && !s.dialogue && !s.battle && !s.transition;
}
inline bool minimap_visible(const MinimapScene &s, MinimapSize size) {
    return size != MinimapSize::Off && minimap_context(s) && !s.fast_forward;
}

// Window pixels. Every world tile is `tile` x `tile` window pixels; the map is 64 tiles square. The world
// atlas is 80 tiles square: the 64x64 world inside an 8-tile authored border (filler beyond the edges,
// plus a garbage top row), which the minimap leaves out.
struct MinimapLayout {
    int x = 0, y = 0, tile = 0, side = 0, height = 0; // side = width
};
inline constexpr int kMinimapTiles = 64, kMinimapBorder = 8;
// Edge tiles that read as a border: rows off the top and bottom, columns off each side.
// Ocean (102) has one odd row at each end; Sky (122) two rows and one column.
struct MinimapTrim {
    int rows = 0, columns = 0;
};
inline MinimapTrim minimap_trim(int map) {
    return map == 102 ? MinimapTrim{1, 0} : map == 122 ? MinimapTrim{2, 1} : MinimapTrim{};
}

// The game picture (window pixels, `scale` window pixels per native pixel), rounded to whole pixels per tile,
// inset 4 native pixels from its bottom-right corner, less the trimmed edge tiles.
inline MinimapLayout minimap_layout(float gx, float gy, float gw, float gh, float scale, MinimapSize size,
                                    MinimapTrim trim = {}) {
    if (size == MinimapSize::Off)
        return {};
    // Small is about a quarter of the picture's height; Large is 1.5x Small, at least a
    // pixel per tile bigger in small windows.
    MinimapLayout out;
    const int small = std::max(1, int(std::lround(0.25f * gh / kMinimapTiles)));
    out.tile =
        size == MinimapSize::Small ? small : std::max(small + 1, int(std::lround(1.5f * float(small))));
    out.side = out.tile * (kMinimapTiles - 2 * trim.columns);
    out.height = out.tile * (kMinimapTiles - 2 * trim.rows);
    const int inset = int(std::lround(4 * scale));
    out.x = int(std::lround(gx + gw)) - inset - out.side;
    out.y = int(std::lround(gy + gh)) - inset - out.height;
    return out;
}

// The party's world tile from the field scroll (native pixels): the party's ground point is the screen's
// (112,79). The game keeps scroll = ((player_x - 7) * 16 + 8, (player_y - 4) * 16), so this is the player's
// cell (0xD0DD/E) between steps and changes halfway through one.
inline std::array<int, 2> minimap_party_tile(float scroll_x, float scroll_y) {
    const auto tile = [](float p) { return std::clamp(int(std::floor(p / 16)), 0, kMinimapTiles - 1); };
    return {tile(scroll_x + 112), tile(scroll_y + 79)};
}

// The party dot rests at a muted 80% white; every 2.5 s it dips smoothly to 30% and back within 0.7 s, like a
// breath.
inline constexpr float kMarkerLow = 0.3f, kMarkerPeak = 0.8f;
inline constexpr uint64_t kMarkerPeriodNs = 2'500'000'000, kMarkerDipNs = 700'000'000;
inline float minimap_marker_level(uint64_t now_ns) {
    const uint64_t t = now_ns % kMarkerPeriodNs;
    if (t >= kMarkerDipNs)
        return kMarkerPeak;
    const float phase = float(t) / float(kMarkerDipNs);
    return kMarkerPeak - (kMarkerPeak - kMarkerLow) * 0.5f * (1 - std::cos(phase * 6.2831853f));
}

// The map's colors at 80% brightness, drawn 90% opaque over the scene.
struct MinimapLook {
    float brightness = 0.8f, opacity = 0.9f;
};
inline MinimapLook minimap_look() { return {}; }

// Shows and hides over 0.2 s instead of popping.
class MinimapFade {
  public:
    static constexpr uint64_t kFadeNs = 200'000'000;
    float update(bool shown, uint64_t now) {
        if (started_) {
            const float step = float(now > last_ ? now - last_ : 0) / float(kFadeNs);
            amount_ = shown ? std::min(1.f, amount_ + step) : std::max(0.f, amount_ - step);
        } else
            amount_ = 0;
        started_ = true;
        last_ = now;
        return amount_;
    }
    void settle(bool shown, uint64_t now) {
        amount_ = shown ? 1.f : 0.f;
        started_ = true;
        last_ = now;
    }
    float amount() const { return amount_; }

  private:
    float amount_ = 0;
    uint64_t last_ = 0;
    bool started_ = false;
};

} // namespace sawgod::app
