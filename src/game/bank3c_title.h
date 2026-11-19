// Title, intro-scene, and ending routine entry points.
#pragma once
#include "game/machine.h"
#include "game/ui_contract.h"
namespace sawgod::game {
void load_intro_background(Machine &m);
void advance_intro_background_scroll(Machine &m);
void advance_title_palette_pulse(Machine &m);
void load_intro_scene_tiles(Machine &m);
void draw_intro_scene_tilemap(Machine &m);
void draw_intro_scene_strip(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {
void run_field_pre_sprite_callback(Machine &m);
void run_field_post_sprite_callback(Machine &m);
void run_field_npc_callback(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {
void wait_intro_frame(Machine &m);
void wait_scrolling_intro_frames(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {
void show_achievement_vignette_one(Machine &m);
void fade_scrolling_intro_to_target(Machine &m);
void opening(Machine &m);
void title_screen(Machine &m);
void reveal_intro_scene_strip(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {
void show_achievement_vignette(Machine &m);

void run_intro_field_scene(Machine &m);

void prologue(Machine &m);

// The app hides the original-mode row through port_policy.h. ROM fallback still offers it.
inline constexpr UiElement kTitleElements[] = {
    {"title.new", "activate"},
    {"title.continue", "activate empty"},
    {"title.original", "activate hidden"},
    {"title.cursor", "wrap cancel"},
    {"title.timeout", "activate"},
    {"title.picture", "render"},
};
void title_menu(Machine &m);

void show_achievement_vignette_two(Machine &m);

void show_achievement_vignette_three(Machine &m);

void show_achievement_vignette_four(Machine &m);

void wait_intro_frames(Machine &m);

void fade_intro_field_to_target(Machine &m);

void brighten_intro_field(Machine &m);

void present_intro_field_frame(Machine &m);

void wait_intro_field_frames(Machine &m);

void fade_scrolling_intro_in(Machine &m);

void darken_intro_scene(Machine &m);

void brighten_intro_scene(Machine &m);

void brighten_scrolling_intro_scene(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {
void show_intro_closing_scene(Machine &m);
}

namespace sawgod::game {
void advance_intro_background_scroll_core(Machine &m);
void advance_title_palette_pulse_core(Machine &m);
void fade_scrolling_intro_to_target_core(Machine &m);
void load_intro_background_core(Machine &m);
void reveal_intro_scene_strip_core(Machine &m);
void run_field_npc_callback_core(Machine &m);
void run_field_post_sprite_callback_core(Machine &m);
void run_field_pre_sprite_callback_core(Machine &m);
void show_achievement_vignette_core(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {
void wait_scrolling_intro_frames_core(Machine &m);
}

namespace sawgod::game {
void load_intro_scene_tiles_core(Machine &m);
}

namespace sawgod::game {
void draw_intro_scene_tilemap_core(Machine &m);
}

namespace sawgod::game {
void draw_intro_scene_strip_core(Machine &m);
}

namespace sawgod::game {
void run_intro_second_field_scene(Machine &m);
void run_intro_second_field_scene_core(Machine &m);
void run_intro_scroll_panels(Machine &m);
void reveal_intro_scene_strip_slow(Machine &m);
void run_intro_third_field_scene(Machine &m);
void run_intro_fifth_caption_scene(Machine &m);
void run_intro_fourth_field_scene(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {
void brighten_scrolling_intro_scene_core(Machine &m);
}

namespace sawgod::game {
void run_intro_field_scene_core(Machine &m);
}

namespace sawgod::game {
void fade_scrolling_intro_in_core(Machine &m);
}

namespace sawgod::game {
void fade_intro_field_to_target_core(Machine &m);
}

namespace sawgod::game {
void run_intro_scroll_panels_core(Machine &m);
}

namespace sawgod::game {
void darken_intro_scene_core(Machine &m);
}

namespace sawgod::game {
void reveal_intro_scene_strip_slow_core(Machine &m);
}

namespace sawgod::game {
void wait_intro_frames_core(Machine &m);
}

namespace sawgod::game {
void show_intro_closing_scene_core(Machine &m);
}

namespace sawgod::game {
void run_intro_third_field_scene_core(Machine &m);
}

namespace sawgod::game {
void run_intro_fifth_caption_scene_core(Machine &m);
}

namespace sawgod::game {
void brighten_intro_field_core(Machine &m);
}

namespace sawgod::game {
void run_intro_fourth_field_scene_core(Machine &m);
}

namespace sawgod::game {
void show_achievement_vignette_two_core(Machine &m);
}

namespace sawgod::game {
void show_achievement_vignette_three_core(Machine &m);
}

namespace sawgod::game {
void show_achievement_vignette_four_core(Machine &m);
}

namespace sawgod::game {
void present_intro_field_frame_core(Machine &m);
}

namespace sawgod::game {
void wait_intro_field_frames_core(Machine &m);
}

namespace sawgod::game {
void brighten_intro_scene_core(Machine &m);
void show_achievement_vignette_one_core(Machine &m);
} // namespace sawgod::game
