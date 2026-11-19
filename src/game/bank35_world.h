// Field movement, map animation, and event-script routine entry points.
#pragma once
#include "game/machine.h"
namespace sawgod::game {
void prepare_player_scroll_core(Machine &m);
void prepare_player_scroll(Machine &m);
// Roll the travel-adjusted terrain encounter and select its weighted battle/variant.
void roll_field_encounter(Machine &m);
void bitstream_read_byte(Machine &m);
void draw_map_rectangle(Machine &m);
void scroll_to_map_offset(Machine &m);
void draw_map_top_edge(Machine &m);
void draw_map_left_edge(Machine &m);
void draw_map_right_edge(Machine &m);
void clear_field_npcs(Machine &m);
void find_npc_at_cell(Machine &m);
void read_packed_progress(Machine &m);
void reset_field_state(Machine &m);
void clear_field_overlay(Machine &m);
void initialize_player_sprite(Machine &m);
void clear_field_word_buffer(Machine &m);
void backup_field_display(Machine &m);
void decompress_field_map(Machine &m);
void draw_map_bottom_edge(Machine &m);
void draw_map_viewport(Machine &m);
void backup_field_npcs(Machine &m);
void restore_field_npcs(Machine &m);
void spawn_parked_vehicle(Machine &m);
void initialize_map_npcs(Machine &m);
void clear_field_interaction(Machine &m);
void clear_field_event(Machine &m);
void build_field_distortion(Machine &m);
void write_packed_progress(Machine &m);
void read_bestiary_bit(Machine &m);
void scan_item_records(Machine &m);
void find_party_item(Machine &m);
void npc_record_pointer(Machine &m);
void face_interacting_npc(Machine &m);
void test_adjacent_npc_cell(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {
void update_field_frame(Machine &m);
void update_map_frame_effect(Machine &m);
void update_map_display_effect(Machine &m);
void upload_field_display(Machine &m);
void process_field_direction(Machine &m);
void animate_player_scroll(Machine &m);
void finish_player_move(Machine &m);
void process_field_action(Machine &m);
void dispatch_field_event(Machine &m);
void fade_field_to_target(Machine &m);
void fade_palettes_16(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {
void attempt_player_step(Machine &m);
void process_player_cell(Machine &m);
void initialize_field_display(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {
void load_field_map(Machine &m);

void initialize_field_controls(Machine &m);
void restore_field_after_menu(Machine &m);
void try_open_field_menu(Machine &m);
void enter_field_battle(Machine &m);
void animate_battle_entry(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {
void fade_field_actors_to_target(Machine &m);

void queue_matched_npc_interaction(Machine &m);

void force_player_step(Machine &m);

void interact_with_facing_cell(Machine &m);

void choose_tower_destination(Machine &m);

void use_first_tower_key(Machine &m);

void use_second_tower_key(Machine &m);

void use_third_tower_key(Machine &m);

void install_tower_key(Machine &m);

void combine_progression_items(Machine &m);

void use_first_progression_item(Machine &m);

void use_second_progression_item(Machine &m);

void use_special_progression_item(Machine &m);

void animate_destination_warp(Machine &m);

void use_destination_travel_item(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {
void animate_destination_warp_core(Machine &m);
void animate_player_scroll_core(Machine &m);
void attempt_player_step_core(Machine &m);
void backup_field_display_core(Machine &m);
void build_field_distortion_core(Machine &m);
void choose_tower_destination_core(Machine &m);
void clear_field_event_core(Machine &m);
void clear_field_interaction_core(Machine &m);

void clear_field_word_buffer_core(Machine &m);
void draw_map_top_edge_core(Machine &m);
void face_interacting_npc_core(Machine &m);
void fade_field_to_target_core(Machine &m);
void fade_palettes_16_core(Machine &m);

void finish_player_move_core(Machine &m);
void initialize_field_controls_core(Machine &m);
void initialize_field_display_core(Machine &m);
void interact_with_facing_cell_core(Machine &m);

void queue_matched_npc_interaction_core(Machine &m);
void read_packed_progress_core(Machine &m);
// Shared encounter rule; table RNG includes the upper endpoint.
void roll_field_encounter_core(Machine &m);
void test_adjacent_npc_cell_core(Machine &m);
void update_field_frame_core(Machine &m);
void update_map_display_effect_core(Machine &m);
void update_map_frame_effect_core(Machine &m);
void use_destination_travel_item_core(Machine &m);
void write_packed_progress_core(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {
void process_field_action_core(Machine &m);
}

namespace sawgod::game {
void dispatch_field_event_core(Machine &m);
}

namespace sawgod::game {
void restore_field_after_menu_core(Machine &m);
}

namespace sawgod::game {
void try_open_field_menu_core(Machine &m);
}

namespace sawgod::game {
void npc_record_pointer_core(Machine &m);
}

namespace sawgod::game {
void draw_map_right_edge_core(Machine &m);
}

namespace sawgod::game {
void initialize_player_sprite_core(Machine &m);
}

namespace sawgod::game {
void draw_map_viewport_core(Machine &m);
}

namespace sawgod::game {
void backup_field_npcs_core(Machine &m);
}

namespace sawgod::game {
void initialize_map_npcs_core(Machine &m);
}

namespace sawgod::game {
void restore_field_npcs_core(Machine &m);
}

namespace sawgod::game {
bool enter_field_battle_core(Machine &m);
}

namespace sawgod::game {
void fade_field_actors_to_target_core(Machine &m);
}

namespace sawgod::game {
void force_player_step_core(Machine &m);
}

namespace sawgod::game {
void install_tower_key_core(Machine &m);
}

namespace sawgod::game {
void use_first_tower_key_core(Machine &m);
}

namespace sawgod::game {
void use_second_tower_key_core(Machine &m);
}

namespace sawgod::game {
void use_third_tower_key_core(Machine &m);
}

namespace sawgod::game {
void combine_progression_items_core(Machine &m);
}

namespace sawgod::game {
void use_first_progression_item_core(Machine &m);
}

namespace sawgod::game {
void use_second_progression_item_core(Machine &m);
}

namespace sawgod::game {
void use_special_progression_item_core(Machine &m);
}

namespace sawgod::game {
void game_main(Machine &m);
void animate_map_palette_cycle(Machine &m);
void animate_map_palette_cycle_core(Machine &m);
void animate_map_tile_strip(Machine &m);
void animate_map_tile_strip_core(Machine &m);
void travel_area_exit(Machine &m);
void travel_area_exit_core(Machine &m);
void process_field_direction_entry_core(Machine &m);
void process_player_cell_entry_core(Machine &m);
void decompress_field_map_core(Machine &m); // the field maps' LZ (game/map_lz.h is the same, host-side)
// Spend one life on a dead member; the original writes only the low HP byte.
void use_field_revive_item_core(Machine &m); // 0x35BD2F (tests: the hearts cheat)
// Grow max HP by base plus roll below the item threshold, otherwise one; cap at 999.
void add_field_maximum_hp_core(Machine &m); // 0x35C035 (tests: Stat Growth)
} // namespace sawgod::game
