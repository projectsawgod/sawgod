// Half-native bloom and native influence stamps; scene edges/UI stay at output resolution.
#pragma once
#include "engine/battle_lighting.h"
#include "engine/influence_buffer.h"
#include "engine/point_light.h"
#include "engine/protected_regions.h"
#include <SDL3/SDL.h>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace sawgod {
struct EnhancedParameters {
    bool snap = true, shadows = true;
    float grade = 1, bloom = 1, gb_weight = 0, effect_weight = 1, light_strength = 1, water_strength = 1;
    BattleLighting battle_lighting;
    int beam_style = 0; // cone, parallel shaft, cone with drifting dust
    uint64_t frame = 0;
    std::array<float, 2> effect_grid{224, 144}; // projected WSC cells across the scene
    std::array<float, 2> effect_origin{};       // a zoomed flat field: its first cell in the game's own view
    std::array<float, 2> palette_range{0, 1};
    std::vector<uint32_t> water;
    std::vector<std::array<float, 4>> actors;
    std::vector<uint32_t> background;
    std::vector<PointLight> lights;
};
class EnhancedGraphics {
  public:
    ~EnhancedGraphics() { shutdown(); }
    bool init(SDL_Renderer *renderer, SDL_Texture *scene = nullptr, SDL_Texture *ownership = nullptr,
              SDL_Texture *water = nullptr, const EnhancedGraphics *shared = nullptr);
    bool upload(const std::vector<uint32_t> &wsc, const std::vector<uint32_t> &gb, const uint32_t *mask,
                const EnhancedParameters &parameters);
    SDL_Texture *render(bool force_redraw = false); // restores caller's target and clears custom shader state
    bool restore_ui(SDL_Texture *ordinary, const SDL_FRect &destination);
    // Flat final draw combines scene and exact UI/enemy restoration before the ordinary IPS masks.
    // restore_only draws host UI after a capped scene (including its lit enemies) has been enlarged.
    bool present(SDL_Texture *scaled_scene, SDL_Texture *ordinary, const SDL_FRect &destination,
                 bool restore_only = false);
    void shutdown();
    bool ready() const { return shader_ != nullptr; }
    bool last_upload_full = false; // diagnostics: all flat inputs started without resident bytes
    // Actual submitted target area, for a deterministic performance budget (not hardware timing).
    struct Work {
        uint64_t shaded_pixels = 0, bloom_pixels = 0;
        unsigned passes = 0;
    };
    Work last_work{}, final_work{};
    bool timing_enabled = false;
    std::array<uint64_t, 4> pass_ns{};
    uint64_t ui_ns = 0;
    ProtectedRegions ui_regions;
    InfluenceBuffer::Work influence_work{};
    static constexpr unsigned composite_influence_samples = 4, restore_influence_samples = 2;

  private:
    ProtectedRegions sharp_ui_regions_;
    SDL_Renderer *renderer_ = nullptr;
    bool external_ = false, dirty_ = true;
    InfluenceBuffer influence_;
    EnhancedParameters influence_parameters_;
    bool build_influences();
    std::array<bool, 4> influence_active_{};
    // Flat and world passes use identical shader bytes. Sharing the handle also reuses SDL pipelines,
    // whose keys include the shader pointer; independent bindings/targets/uniforms stay per instance.
    std::shared_ptr<SDL_GPUShader> shader_;
    SDL_GPUSampler *nearest_ = nullptr, *linear_ = nullptr;
    SDL_Texture *water_pattern_ = nullptr;
    SDL_Texture *source_ = nullptr, *original_ = nullptr, *gb_ = nullptr, *mask_ = nullptr, *water_ = nullptr;
    std::array<SDL_Texture *, 6> targets_{};
    std::array<SDL_GPURenderState *, 8> states_{};
    SDL_Texture *finish_ordinary_ = nullptr;
    int finish_width_ = 0, finish_height_ = 0;
    struct alignas(16) Uniforms {
        std::array<float, 4> control{}, scene{};
        std::array<std::array<float, 4>, 20> actors{};
        std::array<float, 4> lighting{}; // count, strength, reserved
        std::array<float, 4> water{};    // strength, bounded frame phase, reserved
        std::array<float, 4> metadata{}, dark_color{}, effect_grid{};
        std::array<float, 4> battle{}; // weight, then each group's target selection (its outline)
        std::array<std::array<float, 4>, 3> battle_groups{};
        std::array<float, 4> palette_fade{}; // native lower/upper channel limits
    } uniforms_;
    // Exact byte cache keys for all 256 supported sources, including the bit patterns of native spell masks.
    std::array<std::array<float, 4>, 256> light_positions_{}, light_colors_{}, light_beams_{};
    static_assert(sizeof(Uniforms) <= 4096);
    std::array<uint32_t, 224 * 144> upload_mask_{};
    std::array<uint32_t, 224 * 144> dry_{};
    std::vector<uint32_t> cached_source_, cached_original_, cached_gb_, cached_water_, cached_mask_;
    Uniforms cached_uniforms_{};
    std::array<std::array<float, 4>, 256> cached_positions_{}, cached_colors_{}, cached_beams_{};
};
} // namespace sawgod
