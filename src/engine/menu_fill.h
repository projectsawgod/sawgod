// Output-resolution host-window gradients; consumes presentation metadata only.
#pragma once
#include "engine/ui_draw_cache.h"
#include <SDL3/SDL.h>
#include <span>
#include <vector>

namespace sawgod {
class MenuFill {
  public:
    bool draw(SDL_Renderer *renderer, const SDL_FRect &dst, const emu::Wsc::UiFill &fill,
              std::span<const uint32_t> picture, float weight, const emu::Wsc::UiText *text = nullptr,
              const UiDrawCache *prepared = nullptr);
    uint64_t cpu_ns = 0; // geometry construction and submission; GPU work is in the frame timestamps
    size_t quads = 0;
    uint64_t rebuilds = 0;

  private:
    std::vector<SDL_Vertex> vertices_;
    UiDrawCache cache_;
};
} // namespace sawgod
