// Bounded influence quads; scene shaders sample a fixed number of fields regardless of source count.
#pragma once
#include "engine/point_light.h"
#include <SDL3/SDL.h>
#include <array>
#include <vector>
namespace sawgod {
class InfluenceBuffer {
  public:
    ~InfluenceBuffer() { shutdown(); }
    bool init(SDL_Renderer *, int width = 224, int height = 144, int planes = 4);
    void shutdown();
    SDL_Texture *texture(int plane) const { return targets_[size_t(plane)]; }
    struct Work {
        uint64_t pixels = 0, clear_pixels = 0;
        unsigned quads = 0, passes = 0, draws = 0;
    } work;
    struct alignas(16) Parameters {
        std::array<float, 4> position{}, emission{}, extent{}, options{}, grid{224, 144, 0, 0}, camera{},
            scroll{};
        bool operator==(const Parameters &) const = default;
    };
    bool begin(int plane);
    bool light(const PointLight &, Parameters, int plane);
    bool shadow(Parameters, const SDL_FRect &native_bounds);
    bool end();

  private:
    struct Stamp {
        std::array<float, 6> key;
        SDL_Texture *texture;
        uint64_t used;
        bool beam;
        std::vector<SDL_Vertex> vertices;
    };
    Stamp *stamp(const std::array<float, 6> &key);
    bool stamp_quad(Stamp &, const SDL_FRect &, const SDL_FColor &);
    std::vector<Stamp> stamps_;
    uint64_t falloff_clock_ = 0;
    Parameters patch_parameters_{};
    SDL_Renderer *renderer_ = nullptr;
    SDL_Texture *old_ = nullptr, *dummy_ = nullptr;
    SDL_GPUShader *shader_ = nullptr;
    SDL_GPURenderState *state_ = nullptr;
    std::array<SDL_Texture *, 4> targets_{};
    int width_ = 0, height_ = 0;
};
} // namespace sawgod
