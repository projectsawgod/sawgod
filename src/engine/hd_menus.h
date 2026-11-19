// Output-resolution text shadows and bar edges; never changes the native picture or glyph metrics.
#pragma once
#include "engine/ui_draw_cache.h"
#include <SDL3/SDL.h>
#include <array>
#include <map>
#include <span>
#include <vector>

namespace sawgod {
class HdMenus {
  public:
    ~HdMenus() { shutdown(); }
    void shutdown(); // before destroying the renderer
    // Allocates the output-size resources up front; draw() redoes it if the output size changes.
    void prepare(SDL_Renderer *renderer);
    bool draw(SDL_Renderer *renderer, const SDL_FRect &dst, const emu::Wsc::UiText &text,
              const emu::Wsc::UiFill *fill, std::span<const uint32_t> picture, float weight,
              std::span<const uint32_t> blended = {}, const UiDrawCache *prepared = nullptr);
    uint64_t cpu_ns = 0;
    size_t strokes = 0;
    unsigned draws = 0;
    uint64_t rebuilds = 0;

  private:
    SDL_Texture *splat_ = nullptr;
    std::map<uint32_t, SDL_Texture *> masks_;
    // Full-output masks no ink uses right now. Creating one costs milliseconds, so menus reuse them.
    std::vector<SDL_Texture *> spare_masks_;
    SDL_Texture *new_mask(SDL_Renderer *renderer);
    struct BarShadow {
        SDL_Texture *texture = nullptr;
        std::vector<SDL_Vertex> vertices;
    };
    // Output box dimensions and native scale determine the analytic raster. Shared by equal-size bars.
    std::map<std::array<float, 4>, BarShadow> bar_shadows_;
    UiDrawCache cache_;
    size_t cached_strokes_ = 0;
    int width_ = 0, height_ = 0;
    bool software_ = false;
    std::map<uint32_t, std::vector<SDL_Vertex>> splats_;
    std::vector<SDL_Vertex> restore_, glyphs_, corners_;
    std::vector<float> coverage_;
    std::vector<uint32_t> pixels_;
};
} // namespace sawgod
