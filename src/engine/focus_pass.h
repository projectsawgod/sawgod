// Mode 7 focus: draws the finished world picture with a gentle blur in a band along the screen edges, the
// same at the same distance in from any edge (assets/shaders/focus.frag.hlsl). The UI is restored on top
// afterward, so it never blurs.
#pragma once
#include <SDL3/SDL.h>
#include <array>

namespace sawgod {
class FocusPass {
  public:
    ~FocusPass() { shutdown(); }
    bool init(SDL_Renderer *renderer);
    void shutdown();
    bool ready() const { return state_ != nullptr; }
    // strength 0..1 (0 = plain draw); size 0.4..1: the larger, the thinner the soft band along the edges
    // (none at 1); dst is where the picture goes on screen.
    bool draw(SDL_Texture *picture, const SDL_FRect &dst, float strength, float size);

  private:
    SDL_Renderer *renderer_ = nullptr;
    SDL_GPUShader *shader_ = nullptr;
    SDL_GPURenderState *state_ = nullptr;
    struct alignas(16) Uniforms {
        std::array<float, 4> band{}, control{};
    } uniforms_;
};
} // namespace sawgod
