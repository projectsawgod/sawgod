// Host presentation only: no machine clock, input, or RNG dependency.
#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <cstdint>
#include <vector>

namespace sawgod {
class FastForwardEnvelope {
  public:
    static constexpr uint64_t kEntryNs = 100'000'000, kExitNs = 120'000'000;
    float update(bool enabled, bool held, uint64_t now);

  private:
    float level(uint64_t now) const;
    bool held_ = false;
    uint64_t changed_ = 0, last_ = 0;
    float from_ = 0;
};
// Same look for the software renderer. Pixels and history are clean composed output;
// UI ownership is in the native 224x144 grid, even with zoom or perspective below it.
void fast_forward_pixels(std::vector<uint32_t> &pixels, const std::vector<uint32_t> &previous, int width,
                         int height, SDL_FRect game, const uint32_t *ui, float amount, float gb,
                         double seconds);
// Two clean output targets alternate, retaining one prior presented picture (never feedback).
class FastForwardVfx {
  public:
    ~FastForwardVfx() { shutdown(); }
    bool begin(SDL_Renderer *renderer, int width, int height);
    bool end(SDL_FRect game, const uint32_t *ui, float amount, float gb, double seconds);
    void reset_history() {
        history_ = false;
        previous_.clear();
    }
    void shutdown();

  private:
    bool resources(SDL_Renderer *renderer, int width, int height);
    SDL_Renderer *renderer_ = nullptr;
    SDL_Texture *caller_ = nullptr, *mask_ = nullptr, *software_ = nullptr;
    std::array<SDL_Texture *, 2> clean_{};
    SDL_GPUShader *shader_ = nullptr;
    SDL_GPUSampler *sampler_ = nullptr;
    std::array<SDL_GPURenderState *, 2> states_{};
    int width_ = 0, height_ = 0, current_ = 0;
    bool history_ = false;
    std::vector<uint32_t> previous_, previous_ui_, protected_ui_;
    struct alignas(16) Uniforms {
        std::array<float, 4> game{}, control{}, output{};
    };
};
// Drawn as geometry so the FF indicator cannot acquire digits or a font fallback.
void draw_fast_forward_indicator(SDL_Renderer *renderer, SDL_FRect box, float scale, bool gb);
} // namespace sawgod
