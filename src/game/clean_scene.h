// Combined screen state, sampled once before row zero of the native raster.
#pragma once
#include "game/presentation_ownership.h"

namespace sawgod::game {
struct CleanSceneRequests {
    struct Battle {
        bool active = false, enabled = false, ready = false, executing = false, rewards = false;
        int meat = 0;
    } battle;
    bool dialogue = false;
};
struct CleanSceneSettings {
    uint16_t palettes = 0, sprite_palettes = 0, windows = 0, window_tile_min = 0;
    uint8_t sprite_prefix = 0, sprite_min_y = 0;
    // Field dialogue: sprites of the field's NPCs stay even on a hidden palette (palette 15 is also an NPC
    // palette: chests, parked vehicles).
    bool keep_field_npcs = false;
};
inline CleanSceneSettings clean_scene_settings(const CleanSceneRequests &requests) {
    CleanSceneSettings out;
    const auto &b = requests.battle;
    if (b.active) {
        out.windows = kSceneWindowPalettes;
        // Battle UI starts at 0xC0 (meat), then bank34 frames/font at 0x100..0x1FF;
        // preview portraits use the upper bank. bank37 background effects allocate
        // below 0xC0, sharing palettes 8..15. Their palette is not ownership.
        out.window_tile_min = 0xC0;
        if (b.enabled && b.meat) {
            out.palettes = 0xEFF0; // keep the battlefield and native meat (palette 12)
            out.sprite_prefix = 128;
        } else if (b.enabled && (b.ready || b.rewards)) {
            out.palettes = kSceneWindowPalettes;
            out.sprite_palettes = 0x8000;
            out.sprite_prefix = 27;
            out.sprite_min_y = b.executing || b.rewards ? 80 : 0;
        }
    }
    // Dialogue contributes only its own window. Closing it cannot clear battle's request.
    if (requests.dialogue) {
        out.palettes |= 0x8000;
        out.sprite_palettes |= 0x8000;
        out.keep_field_npcs = !b.active;
    }
    return out;
}
} // namespace sawgod::game
