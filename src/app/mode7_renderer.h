// GPU tile-map inputs and cached cards supplied by the presentation-only world cache.
#pragma once
#include "engine/influence_buffer.h"
#include "engine/protected_regions.h"
#include "game/mode7.h"
#include <SDL3/SDL.h>
#include <map>
namespace sawgod {
class Mode7Renderer {
  public:
    ~Mode7Renderer() { shutdown(); }
    bool init(SDL_Renderer *);
    bool draw(const game::Mode7World &, const game::Mode7Camera &, bool snap, const uint32_t *ui,
              bool ips = false, bool shadows = false, float ips_amount = 1, float shadow_strength = 1,
              float output_scale = 1);
    bool draw_ui_grid(const SDL_FRect &destination);
    // The flat field's pixel border lines, multiplied over the finished picture (after its effects).
    bool draw_field_grid(const SDL_FRect &destination);
    // The world-map minimap over the finished picture, after every scene effect (app/minimap.h): the 64x64
    // world from the 160x160 color mip (less its 8-tile border) at 2x2 nearest texels per tile, `tile` window
    // pixels each (less `rows` top and bottom and `columns` each side), then the party's dot centered on
    // world tile `party`: one `block`-pixel square, no outline, at `marker` gray. Draws nothing before a
    // world atlas.
    bool draw_minimap(const SDL_FRect &area, int tile, int rows, int columns, int block,
                      std::array<int, 2> party, float opacity, float brightness, float marker,
                      float marker_opacity);
    bool has_atlas() const { return ready() && map_ >= 0; }
    void shutdown();
    bool resize(int width, int height);
    // Run full-size atlas and camera passes before audio starts; no game state is read or changed.
    bool warmup();
    bool ready() const { return shader_ != nullptr; }
    bool last_upload_full = false; // diagnostics: fresh world inputs rather than incremental uploads
    SDL_Texture *picture() const { return targets_[0]; }
    // Ownership bits occupy the scene alpha byte (UI, foreground/card, filter margin, actor).
    SDL_Texture *ownership() const { return targets_[0]; }
    SDL_Texture *water() const { return targets_[1]; }
    bool timing_enabled = false;
    uint64_t atlas_ns = 0;
    std::array<uint64_t, 3> shaded_pixels{};
    uint64_t atlas_pixels = 0, scanline_pixels = 0;
    ProtectedRegions ui_regions;
    InfluenceBuffer::Work shadow_work{};
    static constexpr unsigned ground_shadow_samples = 1;
    std::array<uint64_t, 3> pass_ns{};

  private:
    SDL_Renderer *renderer_ = nullptr;
    InfluenceBuffer shadow_;
    bool shadow_active_ = false;
    bool dry_ = false, water_cleared_ = false; // map without water tags: its water pass is all zero
    bool bind_scene();
    bool draw_mips(const SDL_Rect *visible = nullptr);
    void cache_palette_usage(const game::Mode7Surface &);
    bool draw_palette_mips(const std::array<uint16_t, 16> &changed);
    // Color-index masks per palette in each 16x16 cell, aligned with all five mip levels.
    std::vector<std::array<uint16_t, 16>> palette_usage_;
    SDL_Rect atlas_valid_{};
    bool mips_valid_ = false;
    SDL_GPUShader *shader_ = nullptr;
    SDL_GPUSampler *sampler_ = nullptr, *linear_ = nullptr;
    SDL_GPURenderState *state_ = nullptr;
    std::array<SDL_Texture *, 4> world_{}; // words, indices, palettes, water tags
    std::array<SDL_Texture *, 2> targets_{};
    std::array<SDL_Texture *, 5> mips_{}; // GPU-only world color pyramid
    std::array<SDL_GPURenderState *, 5> mip_states_{};
    int width_ = 224, height_ = 144;
    std::vector<uint32_t> cached_ui_, cached_indices_, cached_palettes_;
    SDL_Texture *ui_ = nullptr, *ui_grid_ = nullptr, *scanlines_ = nullptr;
    SDL_GPURenderState *scanline_state_ = nullptr;
    bool ips_ = false;
    float ips_amount_ = 1;
    std::map<uint64_t, SDL_Texture *> cards_;
    int map_ = -1;
    uint64_t revision_ = 0, generation_ = 0, tile_revision_ = 0, palette_revision_ = 0;
    struct alignas(16) Uniforms {
        std::array<float, 4> control{}, camera{}, scroll{}, card{}, shape{}, rectangle{}, screen{};
        std::array<float, 4> raster{1, 0, 0, 0}; // output pixels per scene pixel
        std::array<std::array<float, 4>, 20> actors{};
    } uniforms_;
};
} // namespace sawgod
