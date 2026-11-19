// The front end's settings, registered in a Config (engine/config.h) with their defaults, menu text and
// categories. Host presentation and the gameplay options live here; new options register here
// and the options overlay lists them automatically.
// US spelling in everything the player sees.
#pragma once

#include <cstdint>

#include "engine/config.h"

namespace sawgod {

// No pacing mode may change the game's speed: logic always runs at the WSC's 75.47 Hz.
enum class Pacing { Exact = 0, Smooth = 1 };
enum class ScreenMode { Off = 0, Lcd = 1, Ips = 2 };
// Presentation fidelity, ordered from GB to Enhanced; Swap uses the session step buttons.
enum class FidelityMode { Gb = 0, Wsc = 1, Enhanced = 2, Swap = 3 };
using GfxMode = FidelityMode;

// Initial window: 5x the WSC screen.
inline constexpr int kDefaultWindowScale = 5;

struct Settings {
    // Video
    CVar *window_width = nullptr, *window_height = nullptr; // video.window_*: last windowed size (hidden)
    CVar *fullscreen = nullptr;                             // video.fullscreen
    CVar *borderless = nullptr;                             // video.borderless
    CVar *pacing = nullptr;                                 // video.pacing: Pacing
    CVar *screen_mode = nullptr;                            // video.screen_mode: ScreenMode
    CVar *renderer = nullptr;        // video.renderer: gpu (with fallback), legacy; restart required
    CVar *identity_shader = nullptr; // video.identity_shader: diagnostic pass, off; restart required
    CVar *gfx_mode = nullptr, *music_mode = nullptr; // video.graphics / audio.music_mode
    // Enhanced tuning (mostly hidden until Pre-release Options review), WSC scene only.
    CVar *enhanced_snap = nullptr, *enhanced_shadows = nullptr;
    CVar *enhanced_beam_style = nullptr;
    CVar *enhanced_field_zoom = nullptr; // percent pull-out, whole output pixels
    CVar *battle_lighting = nullptr;     // battle stage, pools, corner focus and target light
    CVar *enhanced_mode7 = nullptr, *enhanced_dof = nullptr; // world map: tilted camera, soft edges
    CVar *enhanced_mode7_focus = nullptr, *enhanced_mode7_focus_size = nullptr;
    // World-map minimap (app/minimap.h): Options' On/Off, the size it shows at, and the temporary darkening
    CVar *minimap = nullptr, *minimap_size = nullptr;
    CVar *enhanced_grade = nullptr, *enhanced_lights = nullptr,
         *enhanced_water = nullptr; // percent of the default look
    CVar *enhanced_bloom = nullptr; // 0-100 percent; 60 = the default look
    // Audio
    CVar *master_volume = nullptr, *music_volume = nullptr, *sfx_volume = nullptr; // 0-100, steps of 10
    CVar *mute = nullptr;
    CVar *language = nullptr; // gameplay.language: 0 = English (the --english pack), 1 = Japanese
    CVar *ff_vfx = nullptr;   // gameplay.fast_forward_vfx: host tracking/LCD persistence, default on
    CVar *ff_speed = nullptr; // gameplay.fast_forward: the Fast-Forward button's speed (0: 2x, 1: 3x, 2: 4x)
    CVar *battle_speed = nullptr; // gameplay.battle_speed: battles run at 0: 1x, 1: 2x, 2: 4x
    CVar *skip_intro = nullptr;   // gameplay.skip_intro: straight to the title menu
    CVar *swap_ab = nullptr;      // input.swap_ab: exchange the effective gamepad A/B actions
    CVar *pad_icons = nullptr;    // input.pad_icons: button icon family, 0 Auto, 1 Xbox, 2 PS, 3 Switch
    CVar *enemy_hp = nullptr;     // gameplay.enemy_hp: HP boxes over the enemies in battle (game/info.h)
    CVar *battle_transparency = nullptr; // 0 solid .. 100 transparent; text remains opaque
    // The game's own Config tab, moved here (game/game_config.h)
    CVar *message_speed = nullptr; // speeds 8, 5, 3, 2, 1 (default), 11 (Instant)
    CVar *b_button = nullptr; // gameplay.b_button: Hold Button to: 0 Walk, 1 Run (default); the button is the
                              // game's B, remappable
    CVar *auto_target = nullptr;           // gameplay.auto_target
    CVar *meat_preview = nullptr;          // gameplay.meat_preview
    CVar *confirm_esper_changes = nullptr; // host decision before replacing an existing Esper ability
    CVar *y_button[4] = {};                // gameplay.y1..y4: what Y1-Y4 open (kYMenuValues)
    // Cheats (game/cheats.h). Cheats, all off by default: God Mode, One-Hit Kills, Unbreakable Gear,
    // Unlimited Revives
    CVar *no_damage = nullptr, *one_hit_kills = nullptr, *unbreakable_gear = nullptr,
         *unlimited_hearts = nullptr;
    // ... boosts, the WSC's by default
    CVar *encounters = nullptr;  // modifiers.encounters: 0 normal, 1 half, 2 off
    CVar *gold = nullptr;        // modifiers.gold: 1x (default), 2x, 4x
    CVar *damage = nullptr;      // modifiers.damage: 1x (default), 2x, 4x
    CVar *stat_growth = nullptr; // modifiers.stat_boost: percent, 100 (default), 200, 400
#ifdef SAWGOD_DEBUG_MENU
    // Debug (only in builds with the CMake option SAWGOD_DEBUG_MENU)
    CVar *show_fps = nullptr;
#endif

    // Registers everything in `config` (call once, before Config::load).
    void register_all(Config &config);

    Pacing pacing_mode() const { return Pacing(pacing->get()); }
    // gameplay.y1..y4 choices as the game's values (1-9 less 6, the Config tab)
    static constexpr uint8_t kYMenuValues[8] = {1, 2, 3, 4, 5, 7, 8, 9};
    static constexpr int save_slot_count() { return 9; } // and the AUTO slot (no longer options)
    int gold_percent() const {
        static constexpr int kPercent[] = {100, 200, 400};
        return kPercent[gold->get()];
    }
    int damage_factor() const { return damage->get() == 2 ? 4 : damage->get() + 1; }
    bool english() const { return language->get() == 0; }
    int fast_forward_speed() const { return ff_speed->get() + 2; }
    int battle_speed_factor() const { return battle_speed->get() == 2 ? 4 : battle_speed->get() + 1; }
    ScreenMode screen() const { return ScreenMode(screen_mode->get()); }
    GfxMode gfx() const { return gfx_mode ? GfxMode(gfx_mode->get()) : GfxMode::Wsc; }
    FidelityMode music() const { return music_mode ? FidelityMode(music_mode->get()) : FidelityMode::Wsc; }
    bool fps_shown() const;
    // Effective 0..1 gains (the master gain has the mute applied).
    float bloom_gain() const; // percent mapped to the former levels, preserving their exact render strengths
    float master_gain() const;
    float music_gain() const;
    float sfx_gain() const;
};

} // namespace sawgod
