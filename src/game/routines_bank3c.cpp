#include "game/computed_handlers.h"
// Registry of decompiled bank 0x3C title/intro helpers.
#include "game/bank3c_title.h"
#include "game/machine.h"

namespace sawgod::game {
namespace {
// Arithmetic flags are overwritten before use at every call site.
const Routine kBank3C[] = {
    {"dispatch_intro_command", 0x3C0000, dispatch_intro_command, true, false, true},
    {"play_intro_sound_if_enabled", 0x3C00EA, play_intro_sound_if_enabled, true, true, false},
    {"request_intro_music_if_enabled", 0x3C00FA, request_intro_music_if_enabled, true, true, false},
    {"turn_intro_player_from_facing_table", 0x3C0109, turn_intro_player_from_facing_table, true, true, false},
    {"move_intro_player_vertical_first", 0x3C012D, move_intro_player_vertical_first, true, false, true},
    {"move_intro_player_horizontal_first", 0x3C01B0, move_intro_player_horizontal_first, true, false, true},
    {"shake_intro_field", 0x3C0233, shake_intro_field, true, false, true},
    {"append_intro_sprite_grid", 0x3C028C, append_intro_sprite_grid, true, false, false},
    {"append_intro_sprite_grid_offset", 0x3C02A3, append_intro_sprite_grid_offset, true, false, false},
    {"animate_intro_falling_particles", 0x3C02BA, animate_intro_falling_particles, true, true, true},
    {"animate_intro_stationary_particles", 0x3C02C0, animate_intro_stationary_particles, true, true, true},
    {"animate_intro_return_particles", 0x3C02C6, animate_intro_return_particles, true, true, true},
    {"animate_intro_particle_sequence", 0x3C02CC, animate_intro_particle_sequence, true, true, true},
    {"animate_intro_projectile_burst", 0x3C0382, animate_intro_projectile_burst, true, true, true},
    {"darken_intro_effect_palette", 0x3C0555, darken_intro_effect_palette, true, true, true},
    {"initialize_intro_falling_particles", 0x3C0580, initialize_intro_falling_particles, true, true, false},
    {"update_intro_falling_particles", 0x3C061F, update_intro_falling_particles, true, true, false},
    {"draw_intro_moving_effect_sprite", 0x3C06BE, draw_intro_moving_effect_sprite, true, true, false},
    {"animate_intro_horizontal_projectile", 0x3C06CE, animate_intro_horizontal_projectile, true, true, true},
    {"clear_intro_primary_callback", 0x3C0760, clear_intro_primary_callback, true, true, false},
    {"scroll_intro_field_up_and_back", 0x3C0767, scroll_intro_field_up_and_back, true, true, true},
    {"animate_intro_orbiting_sprite_scene", 0x3C07D6, animate_intro_orbiting_sprite_scene, true, true, true},
    {"draw_intro_secondary_orbit_sprites", 0x3C0A66, draw_intro_secondary_orbit_sprites, true, true, false},
    {"update_intro_orbiting_sprites", 0x3C0A8D, update_intro_orbiting_sprites, true, true, false},
    {"draw_intro_primary_orbit_sprites", 0x3C0B04, draw_intro_primary_orbit_sprites, true, true, false},
    {"animate_intro_window_split_transition", 0x3C0B3D, animate_intro_window_split_transition, true, true,
     true},
    {"animate_intro_split_fade_transition", 0x3C0C85, animate_intro_split_fade_transition, true, true, true},
    {"run_intro_white_flash_battle", 0x3C0D0F, run_intro_white_flash_battle, true, true, true},
    {"animate_intro_upper_sprite_strip", 0x3C0DBD, animate_intro_upper_sprite_strip, true, true, true},
    {"animate_intro_lower_sprite_strip", 0x3C0ECC, animate_intro_lower_sprite_strip, true, true, true},
    {"start_intro_periodic_sound_fade", 0x3C0F98, start_intro_periodic_sound_fade, true, true, true},
    {"update_intro_periodic_sound", 0x3C0FE3, update_intro_periodic_sound, true, true, false},
    {"start_intro_secondary_moving_sprite", 0x3C0FF9, start_intro_secondary_moving_sprite, true, true, false},
    {"finish_intro_secondary_moving_sprite", 0x3C1042, finish_intro_secondary_moving_sprite, true, true,
     true},
    {"draw_intro_secondary_moving_sprite", 0x3C1087, draw_intro_secondary_moving_sprite, true, true, false},
    {"animate_intro_map_progress_flash", 0x3C10C7, animate_intro_map_progress_flash, true, true, true},
    {"start_intro_random_sprite_animation", 0x3C1137, start_intro_random_sprite_animation, true, true, false},
    {"update_intro_random_sprite_animation", 0x3C113F, update_intro_random_sprite_animation, true, true,
     false},
    {"stop_intro_random_sprite_animation", 0x3C1174, stop_intro_random_sprite_animation, true, true, false},
    {"move_intro_first_npc_up", 0x3C11A4, move_intro_first_npc_up, true, true, true},
    {"move_intro_player_to_center", 0x3C11CE, move_intro_player_to_center, true, true, true},
    {"move_intro_three_npcs_away", 0x3C11E3, move_intro_three_npcs_away, true, true, true},
    {"animate_intro_npc_departure_scene", 0x3C126A, animate_intro_npc_departure_scene, true, true, true},
    {"animate_intro_player_departure_strip", 0x3C1644, animate_intro_player_departure_strip, true, true,
     true},
    {"animate_intro_strip_at_right", 0x3C172E, animate_intro_strip_at_right, true, true, true},
    {"animate_intro_strip_at_left", 0x3C185C, animate_intro_strip_at_left, true, true, true},
    {"animate_intro_wave_particles_and_warp", 0x3C198A, animate_intro_wave_particles_and_warp, true, true,
     true},
    {"draw_intro_wave_particle_second_frame", 0x3C1ADE, draw_intro_wave_particle_second_frame, true, true,
     false},
    {"draw_intro_wave_particle_first_frame", 0x3C1AE7, draw_intro_wave_particle_first_frame, true, true,
     false},
    {"run_intro_battle_with_npc_restore", 0x3C1AF0, run_intro_battle_with_npc_restore, true, true, true},
    {"animate_intro_large_sprite_overlay", 0x3C1B60, animate_intro_large_sprite_overlay, true, true, true},
    {"board_intro_vehicle_and_warp", 0x3C1CC6, board_intro_vehicle_and_warp, true, true, true},
    {"start_intro_scrolling_sprite_grid", 0x3C1D25, start_intro_scrolling_sprite_grid, true, true, true},
    {"update_intro_scrolling_sprite_grid", 0x3C1D7D, update_intro_scrolling_sprite_grid, true, true, false},
    {"start_intro_static_sprite_grid", 0x3C1DFA, start_intro_static_sprite_grid, true, true, false},
    {"draw_intro_static_sprite_grid", 0x3C1E2A, draw_intro_static_sprite_grid, true, true, false},
    {"run_intro_battle_and_resume_music", 0x3C1E73, run_intro_battle_and_resume_music, true, true, true},
    {"animate_intro_progress_sprite_grid", 0x3C1EC3, animate_intro_progress_sprite_grid, true, true, true},
    {"animate_intro_alternate_sprite_grid", 0x3C1FAA, animate_intro_alternate_sprite_grid, true, true, true},
    {"animate_intro_particles_with_dual_overlays", 0x3C2086, animate_intro_particles_with_dual_overlays, true,
     true, true},
    {"draw_intro_first_overlay_grid", 0x3C2147, draw_intro_first_overlay_grid, true, true, false},
    {"draw_intro_second_overlay_grid", 0x3C2187, draw_intro_second_overlay_grid, true, true, false},
    {"finish_intro_dual_overlay_scene", 0x3C21CC, finish_intro_dual_overlay_scene, true, true, true},
    {"start_intro_oscillating_grid", 0x3C2229, start_intro_oscillating_grid, true, true, false},
    {"update_intro_oscillating_grid", 0x3C2268, update_intro_oscillating_grid, true, true, false},
    {"start_intro_multi_grid_sequence", 0x3C22C0, start_intro_multi_grid_sequence, true, true, true},
    {"update_intro_multi_grid_sequence", 0x3C2326, update_intro_multi_grid_sequence, true, true, false},
    {"animate_intro_npc_grid_flash", 0x3C2475, animate_intro_npc_grid_flash, true, true, true},
    {"animate_intro_screen_overlay_progress", 0x3C2552, animate_intro_screen_overlay_progress, true, true,
     true},
    {"start_intro_fixed_encounter", 0x3C2695, start_intro_fixed_encounter, true, true, true},
    {"scroll_intro_field_with_sound_fade", 0x3C26B2, scroll_intro_field_with_sound_fade, true, true, true},
    {"animate_intro_two_stage_sprite_effect", 0x3C2701, animate_intro_two_stage_sprite_effect, true, true,
     true},
    {"run_intro_battle_and_random_particles", 0x3C27E0, run_intro_battle_and_random_particles, true, true,
     true},
    {"animate_intro_random_particles_with_progress", 0x3C29A4, animate_intro_random_particles_with_progress,
     true, true, true},
    {"animate_intro_random_particles_with_sound", 0x3C2B13, animate_intro_random_particles_with_sound, true,
     true, true},
    {"start_intro_upper_left_overlay", 0x3C2C81, start_intro_upper_left_overlay, true, true, false},
    {"draw_intro_upper_left_overlay", 0x3C2CB1, draw_intro_upper_left_overlay, true, true, false},
    {"clear_intro_frame_callbacks", 0x3C2CF1, clear_intro_frame_callbacks, true, true, false},
    {"play_intro_vignette_first_variant", 0x3C2D00, play_intro_vignette_first_variant, true, true, true},
    {"play_intro_vignette_second_variant", 0x3C2D07, play_intro_vignette_second_variant, true, true, true},
    {"play_intro_vignette_third_variant", 0x3C2D0E, play_intro_vignette_third_variant, true, true, true},
    {"play_intro_vignette_fourth_variant", 0x3C2D15, play_intro_vignette_fourth_variant, true, true, true},
    {"play_intro_vignette_fifth_variant", 0x3C2D1C, play_intro_vignette_fifth_variant, true, true, true},
    {"play_intro_vignette_scene", 0x3C2D23, play_intro_vignette_scene, true, true, true},
    {"draw_intro_vignette_large_grid", 0x3C316A, draw_intro_vignette_large_grid, true, true, false},
    {"draw_intro_vignette_middle_grid", 0x3C31A4, draw_intro_vignette_middle_grid, true, true, false},
    {"draw_intro_vignette_small_grid", 0x3C31DD, draw_intro_vignette_small_grid, true, true, false},
    {"draw_intro_vignette_marker_sprite", 0x3C3216, draw_intro_vignette_marker_sprite, true, true, false},
    {"update_intro_vignette_grid_offsets", 0x3C3236, update_intro_vignette_grid_offsets, true, true, false},
    {"mark_intro_npc_script_controlled", 0x3C3296, mark_intro_npc_script_controlled, true, true, false},
    {"mark_intro_first_npc_script_controlled", 0x3C32A6, mark_intro_first_npc_script_controlled, true, true,
     false},
    {"mark_intro_other_npcs_script_controlled", 0x3C32AD, mark_intro_other_npcs_script_controlled, true, true,
     false},
    {"move_intro_player_to_fixed_column", 0x3C32C0, move_intro_player_to_fixed_column, true, true, true},
    {"darken_all_intro_palettes", 0x3C32CA, darken_all_intro_palettes, true, true, true},
    {"fade_intro_sound_and_palettes", 0x3C32F0, fade_intro_sound_and_palettes, true, true, true},
    {"animate_intro_player_rising_scene", 0x3C3318, animate_intro_player_rising_scene, true, true, true},
    {"update_intro_player_four_frame_animation", 0x3C33CE, update_intro_player_four_frame_animation, true,
     true, false},
    {"run_intro_final_battle_and_music", 0x3C33E0, run_intro_final_battle_and_music, true, true, true},
    {"fade_intro_sound_with_field_frames", 0x3C3430, fade_intro_sound_with_field_frames, true, true, true},
    {"prepare_intro_scene_sequence", 0x3C3457, prepare_intro_scene_sequence, true, true, false},
    {"play_intro_scene_sequence", 0x3C347F, play_intro_scene_sequence, true, true, true},
    {"show_intro_completion_panel", 0x3C3854, show_intro_completion_panel, true, true, true},
    {"run_ending_menu_sequence", 0x3C38C0, run_ending_menu_sequence, true, true, true, false, true},
    {"run_intro_fourth_field_scene", 0x3C4279, run_intro_fourth_field_scene, true, true, true},

    {"run_intro_fifth_caption_scene", 0x3C4331, run_intro_fifth_caption_scene, true, true, true},

    {"run_intro_third_field_scene", 0x3C418F, run_intro_third_field_scene, true, true, true},

    {"reveal_intro_scene_strip_slow", 0x3C461C, reveal_intro_scene_strip_slow, true, true, true},

    {"run_intro_scroll_panels", 0x3C474A, run_intro_scroll_panels, true, true, true},

    {"run_intro_second_field_scene", 0x3C3F63, run_intro_second_field_scene, true, true, true},

    {"show_intro_closing_scene", 0x3C43AE, show_intro_closing_scene, true, true, true},

    {"brighten_scrolling_intro_scene", 0x3C4A37, brighten_scrolling_intro_scene, true, true, true},

    {"brighten_intro_scene", 0x3C4A10, brighten_intro_scene, true, true, true},

    {"darken_intro_scene", 0x3C49E9, darken_intro_scene, true, true, true},

    {"fade_scrolling_intro_in", 0x3C499B, fade_scrolling_intro_in, true, true, true},

    {"wait_intro_field_frames", 0x3C494A, wait_intro_field_frames, true, true, true},

    {"present_intro_field_frame", 0x3C4923, present_intro_field_frame, true, true, true},

    {"brighten_intro_field", 0x3C48FC, brighten_intro_field, true, true, true},

    {"fade_intro_field_to_target", 0x3C48C4, fade_intro_field_to_target, true, true, true},

    {"wait_intro_frames", 0x3C48AF, wait_intro_frames, true, true, true},

    {"run_intro_field_scene", 0x3C3E6E, run_intro_field_scene, true, true, true},

    {"reveal_intro_scene_strip", 0x3C4578, reveal_intro_scene_strip, true, true, true},

    {"fade_scrolling_intro_to_target", 0x3C49C2, fade_scrolling_intro_to_target, true, true, true},

    {"wait_scrolling_intro_frames", 0x3C48B8, wait_scrolling_intro_frames, true, true, true},

    {"wait_intro_frame", 0x3C489A, wait_intro_frame, true, true, true},

    {"show_achievement_vignette_four", 0x3C3A7F, show_achievement_vignette_four, true, false, true},

    {"show_achievement_vignette_three", 0x3C3A6E, show_achievement_vignette_three, true, false, true},

    {"show_achievement_vignette_two", 0x3C3A5D, show_achievement_vignette_two, true, false, true},

    {"title_screen", 0x3C3CC0, title_screen, true, false, true},

    {"show_achievement_vignette", 0x3C3A90, show_achievement_vignette, true, false, true},
    {"prologue", 0x3C440E, prologue, true, false, true},
    {"title_menu", 0x3C4A60, title_menu, true, false, true},

    {"show_achievement_vignette_one", 0x3C3A4C, show_achievement_vignette_one, true, false, true},
    {"opening", 0x3C3D92, opening, true, false, true},
    {"run_field_pre_sprite_callback", 0x3C00AE, run_field_pre_sprite_callback, true, false, true},
    {"run_field_post_sprite_callback", 0x3C00C2, run_field_post_sprite_callback, true, false, true},
    {"run_field_npc_callback", 0x3C00D6, run_field_npc_callback, true, false, true},
    {"load_intro_background", 0x3C4700, load_intro_background, true, true},
    {"advance_intro_background_scroll", 0x3C473B, advance_intro_background_scroll, true, true},
    {"draw_intro_scene_strip", 0x3C47C2, draw_intro_scene_strip, true, true},
    {"draw_intro_scene_tilemap", 0x3C480B, draw_intro_scene_tilemap, true, true},
    {"load_intro_scene_tiles", 0x3C483E, load_intro_scene_tiles, true, true},
    {"advance_title_palette_pulse", 0x3C4ECA, advance_title_palette_pulse, true, true},
};
} // namespace
const Routine *routines_bank3c(size_t *count) {
    *count = sizeof kBank3C / sizeof kBank3C[0];
    return kBank3C;
}
} // namespace sawgod::game
