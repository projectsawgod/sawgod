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
    bool init(SDL_Renderer *, int width = 672, int height = 432, int planes = 4, bool spell_patches = true);
    void shutdown();
    SDL_Texture *texture(int plane) const { return targets_[size_t(plane)]; }
    struct Work {
        uint64_t pixels = 0, clear_pixels = 0;
        unsigned quads = 0, passes = 0, patch_builds = 0, patch_allocations = 0;
    } work;
    struct alignas(16) Parameters {
        std::array<float, 4> position{}, emission{}, extent{}, options{}, grid{224, 144, 0, 0}, camera{},
            scroll{};
    };
    bool begin(int plane);
    bool light(const PointLight &, Parameters, int plane);
    bool shadow(Parameters, const SDL_FRect &native_bounds);
    bool end();

  private:
    bool quad(Parameters &, const SDL_FRect &, SDL_Texture *falloff = nullptr);
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
    struct PatchTexture {
        float radius;
        SDL_Texture *texture;
        bool available;
    };
    std::vector<PatchTexture> patch_textures_;
    SDL_Texture *new_patch_texture(float radius);
    void release_patch_texture(SDL_Texture *);
    struct FalloffPatch;
    FalloffPatch *patch_falloff(const std::array<uint32_t, 2> &, float radius);
    struct FalloffPatch {
        std::array<uint32_t, 2> mask;
        float radius;
        SDL_Texture *texture;
        uint64_t used;
        std::vector<SDL_Vertex> vertices;
    };
    // Position, palette and intensity do not affect falloff; reuse masks across frames and planes.
    std::vector<FalloffPatch> falloff_cache_;
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
