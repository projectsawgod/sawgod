// Owns SDL presentation resources, without touching picture composition or game state.
#pragma once

#include <SDL3/SDL.h>

namespace sawgod {

class PresentationRenderer {
  public:
    ~PresentationRenderer();
    PresentationRenderer() = default;
    PresentationRenderer(const PresentationRenderer &) = delete;
    PresentationRenderer &operator=(const PresentationRenderer &) = delete;

    // GPU first, with the old SDL renderer selection on device/shader/capability failure.
    // legacy=true explicitly skips the GPU renderer, even during automatic fallback.
    bool init(SDL_Window *window, bool legacy, bool identity);
    SDL_Renderer *get() const { return renderer_; }
    bool is_gpu() const;
    bool identity_active() const { return state_ != nullptr; }
    // Scope this to the game's XRGB draws; clear before IPS masks, overlays and badges.
    bool begin_game();
    bool end_game();
    void shutdown(); // call after destroying textures/ImGui, before SDL_Quit

  private:
    bool init_identity();
    bool probe();
    SDL_Renderer *renderer_ = nullptr;
    SDL_GPUShader *shader_ = nullptr;
    SDL_GPURenderState *state_ = nullptr;
};

} // namespace sawgod
