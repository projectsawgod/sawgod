#include "engine/settings.h"

namespace sawgod {

void Settings::register_all(Config &c, bool debug_touch) {
    window_width = &c.add_int("video.window_width", "Video", "", "", 224 * kDefaultWindowScale, 448, 16384);
    window_height = &c.add_int("video.window_height", "Video", "", "", 144 * kDefaultWindowScale, 288, 16384);
    window_width->hidden = window_height->hidden = true;
    // Backend selection is startup-only. Keep these diagnostics out of the options layout.
    renderer = &c.add_enum("video.renderer", "Video", "", "", {"gpu", "legacy"}, {}, 0);
    identity_shader = &c.add_bool("video.identity_shader", "Video", "", "", false);
    renderer->hidden = identity_shader->hidden = true;
    fullscreen =
        &c.add_bool("video.fullscreen", "Video", "Fullscreen", "Fill the display ({Fullscreen})", false);
    borderless = &c.add_bool("video.borderless", "Video", "Borderless", "No title bar or border", false);
    pacing = &c.add_enum("video.pacing", "Video", "Performance", "", {"exact", "smooth", "lospec"},
                         {"Exact", "Smooth", "Lo-Spec"}, int(Pacing::Smooth));
    pacing->choice_order = {int(Pacing::Smooth), int(Pacing::Exact), int(Pacing::LoSpec)};
    pacing->choice_help = {
        "Exact WSC 75.47 Hz; 60 Hz skips 1 in 5",
        "Matches your screen's refresh rate; max FPS",
        "30-60 FPS, for weaker devices",
    };
    // Screen Mode: LCD is the stock screen's look (engine/screen.h), IPS the subpixel grid of an
    // IPS-modded unit.
    screen_mode = &c.add_enum("video.screen_mode", "Video", "Screen Mode", "", {"off", "lcd", "ips"},
                              {"Off", "LCD", "IPS"}, int(ScreenMode::Ips)); // IPS by default
    screen_mode->choice_help = {
        "Regular pixels, no filter",
        "Stock WSC screen: soft, smeary, faded",
        "IPS-modded WSC: strong grid, brighter",
    };
#ifdef SAWGOD_GFX_SWAP
    gfx_mode = &c.add_enum("video.graphics", "Video", "Graphics", "", {"gb", "wsc", "enhanced", "swap"},
                           {"GB", "WSC", "Enhanced", "Swap"}, int(FidelityMode::Swap));
    gfx_mode->choice_help = {
        "Game Boy graphics; locked",
        "WonderSwan Color graphics; locked",
        "Enhanced WSC graphics; locked",
        "{ModeDown}/{ModeUp} move between GB, WSC, Enhanced",
    };
#endif

    enhanced_snap = &c.add_bool("enhanced.snap_to_grid", "Video", "", "", true);
    enhanced_shadows = &c.add_bool("enhanced.contact_shadows", "Video", "", "", true);
    enhanced_grade = &c.add_int("enhanced.warm_grade", "Video", "", "", 0, 0, 300, 5, "%"); // off
    // Effect toggles keep their fixed On strengths for the renderer.
    enhanced_bloom =
        &c.add_int("enhanced.bloom", "Video", "Bloom FX", "Enhanced GFX: Bright-pixel glow", 40, 0, 40);
    enhanced_lights = &c.add_int("enhanced.lights", "Video", "Lighting FX",
                                 "Enhanced GFX: Point-light ambience", 100, 0, 100);
    enhanced_water = &c.add_int("enhanced.water", "Video", "Water FX", "Enhanced GFX: Blue water and shimmer",
                                100, 0, 100);
    for (CVar *v : {enhanced_bloom, enhanced_lights, enhanced_water}) {
        v->values = {v->def, 0};
        v->choice_labels = {"On", "Off"};
        v->nonzero_value = v->def;
    }
    enhanced_beam_style = &c.add_int("enhanced.beam_style", "Video", "", "", 0, 0, 2, 1);
    enhanced_beam_style->hidden = true;
    // The camera itself is fixed (game::Mode7Look: tilt, width, distance and zoom).
    battle_lighting =
        &c.add_bool("enhanced.battle_lighting", "Video", "HD Battles", "Enhanced GFX: Battle lighting", true);
    // Mode 7 and Depth of Field: the world map's tilted camera, and its soft screen edges
    // (Focus and Oval Size are the hidden tuning behind the Tilt-Shift switch).
    enhanced_mode7 = &c.add_bool("enhanced.mode7", "Video", "Mode 7 World Map",
                                 "Enhanced GFX: Mode 7-style world map", true);
    // How far the flat field camera pulls out, in whole output pixels (game::field_zoom_camera).
    enhanced_field_zoom =
        &c.add_int("enhanced.field_zoom", "Video", "Field Zoom",
                   "Enhanced GFX: Town and dungeon camera distance", 125, 100, 150, 25, "%");
    enhanced_field_zoom->values = {100, 125, 150};
    enhanced_field_zoom->choice_labels = {"100%", "125%", "150%"};
    enhanced_dof = &c.add_bool("enhanced.depth_of_field", "Video", "Tilt-Shift",
                               "Enhanced GFX: Softened corners on maps", true);
    enhanced_mode7_focus = &c.add_int("enhanced.mode7_focus", "Video", "", "", 30, 0, 100, 10, "%");
    enhanced_mode7_focus_size = &c.add_int("enhanced.mode7_focus_size", "Video", "", "", 50, 40, 100, 5, "%");
    enhanced_mode7_focus->bar = enhanced_mode7_focus_size->bar = true;
    enhanced_mode7_focus->hidden = enhanced_mode7_focus_size->hidden = true;
    // The hidden minimap state persists the button’s Off -> Small -> Large cycle.
    minimap = &c.add_bool("enhanced.minimap", "Video", "", "", true);
    minimap_size = &c.add_enum("enhanced.minimap_size", "Video", "", "", {"small", "large"}, {}, 0);
    minimap->hidden = minimap_size->hidden = true;
    for (auto *v : {enhanced_snap, enhanced_shadows, enhanced_grade})
        v->hidden = true;

#ifdef SAWGOD_GFX_SWAP
    music_mode = &c.add_enum("audio.music_mode", "Audio", "Music", "", {"gb", "wsc", "enhanced", "swap"},
                             {"GB", "WSC", "Enhanced", "Swap"}, int(FidelityMode::Swap));
    music_mode->choice_help = {
        "Game Boy music and SFX; locked",
        "WonderSwan Color music and SFX; locked",
        "Enhanced music arranged from WSC music; locked",
        "{ModeDown}/{ModeUp} move between GB, WSC, Enhanced",
    };
#endif

    master_volume =
        &c.add_int("audio.master_volume", "Audio", "Master Volume", "Overall volume", 100, 0, 100, 10);
    music_volume = &c.add_int("audio.music_volume", "Audio", "Music Volume", "Music volume", 100, 0, 100, 10);
    sfx_volume =
        &c.add_int("audio.sfx_volume", "Audio", "SFX Volume", "Sound effects volume", 100, 0, 100, 10);
    mute = &c.add_bool("audio.mute", "Audio", "Mute", "Silence all sound", false);

    // Language: English is the default when an English text resource is loaded, else the game is in
    // Japanese. Switches at once (game/text.h).
    language =
        &c.add_enum("gameplay.language", "System", "Language", "", {"en", "ja"}, {"English", "にほんご"}, 0);
    language->choice_help = {
        "English text",
        "にほんごのもじ",
    };

    // Speed: never changes how the game plays, only how fast its frames run
    ff_speed =
        &c.add_enum("gameplay.fast_forward", "Gameplay", "Fast-Forward",
                    "Speed while the Fast-Forward button is held", {"2", "3", "4"}, {"2x", "3x", "4x"}, 2);
    ff_vfx = &c.add_bool("gameplay.fast_forward_vfx", "Gameplay", "Fast-Forward VFX",
                         "Tracking VFX during Fast-Forward", true);
    battle_speed = &c.add_enum("gameplay.battle_speed", "Gameplay", "Battle Speed", "How fast battles are",
                               {"1", "2", "4"}, {"1x", "2x", "4x"}, 0);
    skip_intro =
        &c.add_bool("gameplay.skip_intro", "Gameplay", "Skip Intro", "Skip intro on game boot", false);
    enemy_hp =
        &c.add_bool("gameplay.enemy_hp", "Gameplay", "Enemy HP", "HP displayed over each enemy group", true);
    battle_transparency = &c.add_int("gameplay.battle_transparency", "Gameplay", "", "", 30, 30, 30, 10, "%");
    battle_transparency->hidden = true;

    // The game's own Config tab settings (game/game_config.h writes them into the game's own bytes).
    // Dash is always on.
    message_speed = &c.add_int("gameplay.message_speed", "Gameplay", "Message Speed",
                               "How quickly text appears", 1, 1, 11);
    message_speed->values = {8, 5, 3, 2, 1, 11}; // Instant stores the native sentinel 10 (speed minus one).
    message_speed->bar = true;                   // shown as a Slow ... Fast bar, one cell per step
    message_speed->bar_low = "Slow";
    message_speed->bar_high = "Fast";
    b_button = &c.add_enum("gameplay.b_button", "Gameplay", "Hold Button to", "", {"walk", "run"},
                           {"Walk", "Run"}, 1);
    b_button->choice_help = {
        "Run normally; hold the button to walk",
        "Walk normally; hold the button to run",
    };
    auto_target = &c.add_bool("gameplay.auto_target", "Gameplay", "Auto-Target",
                              "Retarget when the target has disappeared", true);
    meat_preview = &c.add_bool("gameplay.meat_preview", "Gameplay", "Monster Meat Preview",
                               "Show what eating meat will turn you into", true);
    confirm_esper_changes = &c.add_bool("gameplay.confirm_esper_changes", "Gameplay", "Confirm Esper Changes",
                                        "Ask before replacing an Esper ability", true);
    static const char *const kY[4] = {"y1", "y2", "y3", "y4"};
    static const char *const kYLabel[4] = {"Y1 Shortcut", "Y2 Shortcut", "Y3 Shortcut", "Y4 Shortcut"};
    static const char *const kYHelp[4] = {"Y1 (field) opens this menu", "Y2 (field) opens this menu",
                                          "Y3 (field) opens this menu", "Y4 (field) opens this menu"};
    constexpr int kYDefault[4] = {2, 0, 6, 1}; // the game's: Equip, Item, Menu, Ability
    for (int i = 0; i < 4; ++i)
        y_button[i] = &c.add_enum(std::string("gameplay.") + kY[i], "Gameplay", kYLabel[i], kYHelp[i],
                                  {"item", "ability", "equip", "order", "gallery", "save", "menu", "none"},
                                  {"Item", "Ability", "Equip", "Order", "Gallery", "Save", "Menu", "None"},
                                  kYDefault[i]);

    // Cheats page (game/cheats.h): boosts first, the WSC's values by default. Encounters also has its
    // own button (Encounters On/Off, engine/input.h).
    encounters = &c.add_enum("modifiers.encounters", "Cheats", "Encounters", "", {"normal", "half", "off"},
                             {"Normal", "Half", "Off"}, 0);
    encounters->choice_help = {
        "Random battles at the normal rate",
        "Random battles at half the normal rate",
        "No random battles (story battles still occur)",
    };
    gold = &c.add_enum("modifiers.gold", "Cheats", "Gold Boost", "Increase gold gained in battle",
                       {"1", "2", "4"}, {"1x", "2x", "4x"}, 0);
    damage = &c.add_enum("modifiers.damage", "Cheats", "Damage Boost", "Increase damage dealt in battle",
                         {"1", "2", "4"}, {"1x", "2x", "4x"}, 0);
    stat_growth = &c.add_int("modifiers.stat_boost", "Cheats", "Stat Gain Boost",
                             "Increase Human and Esper stat gains", 100, 100, 400);
    stat_growth->values = {100, 200, 400};
    stat_growth->choice_labels = {"1x", "2x", "4x"};

    // Cheats: off by default.
    no_damage = &c.add_bool("modifiers.god_mode", "Cheats", "God Mode",
                            "Party is immune to damage and ailments", false);
    one_hit_kills =
        &c.add_bool("modifiers.one_hit_kills", "Cheats", "One-Hit Kills", "Enemies die in one hit", false);
    unbreakable_gear = &c.add_bool("modifiers.unbreakable_gear", "Cheats", "Unlimited Uses",
                                   "Weapons, items and abilities never run out", false);
    unlimited_hearts = &c.add_bool("modifiers.unlimited_revives", "Cheats", "Unlimited Revives",
                                   "Reviving a party member doesn't cost a heart", false);
    touch_controls =
        &c.add_enum("input.touch_controls", "Gamepad", "Touch Controls", "Show on-screen touch controls",
                    {"auto", "on", "off"}, {"Auto", "On", "Off"}, 0);
#ifndef __ANDROID__
    touch_controls->hidden = !debug_touch;
#else
    (void)debug_touch;
#endif
    swap_ab = &c.add_bool("input.swap_ab", "Gamepad", "Swap A/B",
                          "Exchange gamepad A and B in game and Options", false);
    swap_ab->choice_order = {0, 1};
    swap_ab->choice_labels = {"Off", "On"};
    // Icon Type sits above the bindings (src/engine/overlay.cpp).
    pad_icons =
        &c.add_enum("input.pad_icons", "Gamepad", "Icon Type", "Which controller button icons are shown",
                    {"auto", "xbox", "ps", "switch"}, {"Auto", "Xbox", "PS", "Switch"}, 0);

#ifdef SAWGOD_DEBUG_MENU
    show_fps = &c.add_bool("debug.show_fps", "Debug", "Show FPS",
                           "Game FPS and display refresh rate, top left", false);
#endif
}

bool Settings::fps_shown() const {
#ifdef SAWGOD_DEBUG_MENU
    return show_fps->on();
#else
    return false;
#endif
}

float Settings::bloom_gain() const { return (float(enhanced_bloom->get()) / 20) / 3 * 1.25f; }

float Settings::master_gain() const { return mute->on() ? 0.0f : master_volume->get() / 100.0f; }
float Settings::music_gain() const { return music_volume->get() / 100.0f; }
float Settings::sfx_gain() const { return sfx_volume->get() / 100.0f; }

} // namespace sawgod
