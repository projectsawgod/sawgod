// Opt-in presentation observations. Never writes machine state or requests a new raster.
#pragma once
#include "emu/wsc.h"
#include "game/presentation_ownership.h"
#include <cstdio>
#include <span>

namespace sawgod::app {
struct NativePictureFacts {
    int valid_pixels = 0, scr1_pixels = 0, terrain_pixels = 0;
    bool uniform = true, black = false;
    float terrain_fraction = 0;
};
inline NativePictureFacts native_picture_facts(const emu::FrameLayers *layers) {
    NativePictureFacts facts;
    if (!layers)
        return facts;
    uint16_t first = 0;
    for (size_t at = 0; at < emu::FrameLayers::Pixels; ++at) {
        if (!layers->valid[at / 224])
            continue;
        const auto color = layers->color(at);
        if (!facts.valid_pixels)
            first = color;
        ++facts.valid_pixels;
        facts.uniform &= color == first;
        const auto &p = layers->planes[emu::FrameLayers::Screen1][at];
        if (p.flags & emu::RasterPixel::Present) {
            ++facts.scr1_pixels;
            if (!game::UiOwnership{true, false}.owns(p, emu::FrameLayers::Screen1))
                ++facts.terrain_pixels;
        }
    }
    facts.black = facts.valid_pixels && facts.uniform && first == 0;
    if (facts.scr1_pixels)
        facts.terrain_fraction = float(facts.terrain_pixels) / float(facts.scr1_pixels);
    return facts;
}
struct ComposedPictureFacts {
    int pixels = 0;
    bool uniform = false, black = false;
};
inline ComposedPictureFacts composed_picture_facts(std::span<const uint32_t> picture) {
    if (picture.empty())
        return {};
    const auto first = picture.front() & 0xFFFFFF;
    bool uniform = true;
    for (const auto color : picture)
        uniform &= (color & 0xFFFFFF) == first;
    return {int(picture.size()), uniform, uniform && first == 0};
}
struct FrameAuditSample {
    int64_t fixture_view;
    uint64_t movie_frame, generation;
    int scene, map;
    float mode7;
    bool perspective, dialogue, battle, full_ui, windows;
    int owner;
    bool holding, field_cleared, effects;
    const char *field_step, *world_step;
    float settled_zoom;
    bool wipe;
    int graphics;
    uint64_t battle_starts, battle_ends;
    bool descriptor_matches, field_published;
    int previous_scene = 0, transition = 0;
    float effect_weight = 0;
    bool field_fade = false, effect_fading = false;
};
class FrameAuditLog {
  public:
    explicit FrameAuditLog(const char *path) {
        if (path && *path)
            file_ = std::fopen(path, "w");
        if (file_)
            std::fputs("fixture_view,movie_frame,game_frame,generation,scene,map,mode7,perspective,dialogue,"
                       "battle_ui,full_ui,windows_over_scene,picture_owner,holding,field_cleared,effects,"
                       "field_step,world_step,field_settled_zoom,battle_entry_wipe,graphics,battle_starts,"
                       "battle_ends,descriptor_matches,field_published,native_valid_pixels,native_uniform,"
                       "native_black,scr1_pixels,scr1_terrain_fraction,loaded_map,scroll_x,scroll_y,"
                       "line_vector,hblank_vector,irq_enable,shown_native_pixels,shown_native_uniform,shown_"
                       "native_black,previous_scene,transition,effect_weight,field_fade,effect_fading\n",
                       file_);
    }
    ~FrameAuditLog() {
        if (file_)
            std::fclose(file_);
    }
    FrameAuditLog(const FrameAuditLog &) = delete;
    FrameAuditLog &operator=(const FrameAuditLog &) = delete;
    bool enabled() const { return file_ != nullptr; }
    void write(const emu::Wsc &w, const FrameAuditSample &s, std::span<const uint32_t> picture = {}) {
        if (!file_)
            return;
        const auto facts = native_picture_facts(w.frame_layers());
        const auto shown = composed_picture_facts(picture);
        const auto word = [&](unsigned at) { return unsigned(w.ram[at] | w.ram[at + 1] << 8); };
        std::fprintf(
            file_,
            "%lld,%llu,%llu,%llu,%d,%d,%.6f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s,%s,%.6f,"
            "%d,%d,%llu,%llu,%d,%d,%d,%d,%d,%d,%.6f,%d,%d,%d,%08X,%08X,%d,%d,%d,%d,%d,%d,%.6f,%d,%d\n",
            (long long)s.fixture_view, (unsigned long long)s.movie_frame, (unsigned long long)w.frame_count,
            (unsigned long long)s.generation, s.scene, s.map, double(s.mode7), s.perspective, s.dialogue,
            s.battle, s.full_ui, s.windows, s.owner, s.holding, s.field_cleared, s.effects, s.field_step,
            s.world_step, double(s.settled_zoom), s.wipe, s.graphics, (unsigned long long)s.battle_starts,
            (unsigned long long)s.battle_ends, s.descriptor_matches, s.field_published, facts.valid_pixels,
            facts.uniform, facts.black, facts.scr1_pixels, double(facts.terrain_fraction), w.ram[0xD0DC],
            int(int16_t(word(0xD59D))), int(int16_t(word(0xD59F))), word(0x92) << 16 | word(0x90),
            word(0x9E) << 16 | word(0x9C), w.io[0xB2], shown.pixels, shown.uniform, shown.black,
            s.previous_scene, s.transition, double(s.effect_weight), s.field_fade, s.effect_fading);
    }

  private:
    FILE *file_ = nullptr;
};
} // namespace sawgod::app
