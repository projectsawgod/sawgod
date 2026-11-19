// Battle damage/status rules, transformation menus, effects, and formation graphics entry points.
#pragma once
#include "game/machine.h"
namespace sawgod::game {
void load_battle_palette_backup(Machine &m);
void load_battle_palette_display(Machine &m);
void build_name_string(Machine &m);
void draw_battle_marker_variant(Machine &m);
void clear_battle_background(Machine &m);
void clear_battle_left_pane(Machine &m);
void clear_battle_right_pane(Machine &m);
void clear_battle_inner_pane(Machine &m);
void draw_debug_extended_menu(Machine &m);
void draw_debug_settings_menu(Machine &m);
void position_debug_settings_cursor(Machine &m);
void position_debug_list_cursor(Machine &m);
void draw_formation_comparison_portraits(Machine &m);
void restore_formation_portrait_palette(Machine &m);
void apply_formation_portrait_tint1(Machine &m);
void apply_formation_portrait_tint2(Machine &m);
void apply_formation_portrait_tint3(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {
void choose_special_actor_action(Machine &m);

void upload_debug_menu_frame(Machine &m);

void announce_battle_action_once(Machine &m);

void build_action_target_description(Machine &m);

void handle_ally_target_keys(Machine &m);

void backtrack_battle_menu(Machine &m);

void draw_meat_reward(Machine &m);

void draw_debug_encounter_values(Machine &m);

void draw_debug_configuration_values(Machine &m);

// Apply the countering target's effective strength as damage after swapping battle sides, then restore the
// sides.
void resolve_counterattack(Machine &m);

void append_target_group_suffix(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {}

namespace sawgod::game {
void backtrack_battle_menu_core(Machine &m);
void position_ally_target_cursor_core(Machine &m);
void swap_battle_action_sides_core(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {
void show_battle_status_resisted_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
// Roll poison recovery for actor CL; otherwise apply max HP / 16 damage and check for a knockout.
void resolve_battle_status_or_damage_core(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {}

namespace sawgod::game {
void draw_transformation_menu_state_core(Machine &m);
}

namespace sawgod::game {
void draw_transformation_menu_state(Machine &m);
}

namespace sawgod::game {
void run_transformation_menu_core(Machine &m);
}

namespace sawgod::game {
void run_transformation_menu(Machine &m);
}

namespace sawgod::game {
void run_debug_configuration_menu_core(Machine &m);
}

namespace sawgod::game {
void run_debug_configuration_menu(Machine &m);
}

namespace sawgod::game {
void upload_debug_menu_frame_core(Machine &m);
}

namespace sawgod::game {
void run_debug_party_status_menu_core(Machine &m);
}

namespace sawgod::game {
void run_debug_party_status_menu(Machine &m);
}

namespace sawgod::game {
void show_battle_escape_failed_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
// Roll paralysis recovery using item FEh field 4, clear status 40h on success, and redraw the party.
void attempt_battle_escape_core(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {}

namespace sawgod::game {
void show_battle_disengage_failed_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
// Roll sleep recovery using item FDh field 4, clear status 80h on success, and redraw the party.
void attempt_battle_disengage_core(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {}

namespace sawgod::game {
void show_member_item_effect_message_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
// Double pending damage, apply it with actor and target swapped, then restore the sides.
void apply_doubled_battle_damage_core(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {}

namespace sawgod::game {
void show_battle_effect_introduction_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_battle_actor_immunity_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
// Check the acting side's immunity and status threshold, then swap sides to inflict the action status.
void apply_battle_status_effect_core(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {}

namespace sawgod::game {
// Roll follow-up item damage unless resisted, then apply it with actor and target swapped.
void apply_battle_item_damage_core(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {}

namespace sawgod::game {
void show_battle_damage_amount_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_battle_damage_footer_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_battle_empty_item_message_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_member_item_count_message_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_member_status_notice_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_member_status_notice_pages_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_actor_special_status_notice_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_actor_status_notice_pages_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_battle_status_icons_notice_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_battle_status_icons_result_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_battle_status_result_footer_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_transformation_item_prompt_core(Machine &m);
}

namespace sawgod::game {
void show_transformation_item_prompt(Machine &m);
}

namespace sawgod::game {
void show_transformation_result_message_core(Machine &m);
}

namespace sawgod::game {
void show_transformation_result_message(Machine &m);
}

namespace sawgod::game {
void show_battle_escape_success_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_battle_disengage_success_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_battle_actor_failure_core(Machine &m);
}

namespace sawgod::game {
void show_battle_actor_failure(Machine &m);
}

namespace sawgod::game {
void show_battle_no_effect_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_battle_member_count_footer_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_member_count_notice_pages1_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_member_count_notice2_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_member_count_notice_pages3_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_member_count_notice4_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_actor_damage_pages_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_member_count_notice6_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_member_count_notice7_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_member_count_notice8_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_battle_stat_change_notice1_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_battle_stat_change_notice2_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_battle_stat_change_notice3_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_battle_stat_change_notice4_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_battle_stat_change_notice5_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_battle_item_change_notice_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_battle_item_exchange_pages_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_battle_item_stat_increase_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_battle_item_stat_decrease_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_party_status_summary_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_battle_summary_footer_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_selected_member_result_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void show_battle_party_result_core(Machine &m);
}

namespace sawgod::game {}

namespace sawgod::game {
void run_battle_raster_sweep_core(Machine &m);
}

namespace sawgod::game {
void run_battle_raster_sweep(Machine &m);
}

namespace sawgod::game {
void run_battle_raster_shutter_core(Machine &m);
}

namespace sawgod::game {
void run_battle_raster_shutter(Machine &m);
}

namespace sawgod::game {
void draw_formation_comparison_portraits_core(Machine &m);
}

namespace sawgod::game {
void load_battle_palette_backup_core(Machine &m);
}

namespace sawgod::game {
void apply_formation_portrait_tint2_core(Machine &m);
}

namespace sawgod::game {
void apply_formation_portrait_tint1_core(Machine &m);
}

namespace sawgod::game {
void apply_formation_portrait_tint3_core(Machine &m);
}

namespace sawgod::game {
void restore_formation_portrait_palette_core(Machine &m);
}

namespace sawgod::game {
// Shared native bodies used by the call-out cleanup.
void battle_menu_dispatcher_core(Machine &m);
void battle_table_pointer_core(Machine &m);
uint16_t blank_pad_and_extract_digits_core(Machine &m);
void build_battle_member_display_core(Machine &m);
void build_battle_member_name_or_icons_core(Machine &m);
void build_formation_layer_record_core(Machine &m);
void build_icon_list_core(Machine &m);
void build_party_name_core(Machine &m);
void build_status_icons_core(Machine &m);
void check_target_knocked_out_core(Machine &m);
void copy_formation_palette_core(Machine &m);
void dispatch_field_event_core(Machine &m);
void draw_actor_item_list_core(Machine &m);
void draw_battle_member_panels_core(Machine &m);
void draw_battle_skill_list_core(Machine &m);
void draw_numeral_string_core(Machine &m);
void fade_all_palettes_to_black_core(Machine &m);
void fade_in_battle_scene_core(Machine &m);
void force_player_step_core(Machine &m);
void has_table_flag_bit1_core(Machine &m);
void load_battle_actor_graphics_core(Machine &m);
void load_formation_set_core(Machine &m);
void open_party_member_popup_core(Machine &m);
void open_party_menu_core(Machine &m);
void party_slot_for_name_id_core(Machine &m);
void play_battle_effect_6_core(Machine &m);
void play_battle_effect_7_core(Machine &m);
void play_directional_transformation_effect_core(Machine &m);
void play_formation_palette_effect_core(Machine &m);
void play_hit_sprite_effect_core(Machine &m);
void play_impact_sprite_effect_core(Machine &m);
void present_battle_frame_scrolled_core(Machine &m);
void queue_item_count_footer_core(Machine &m);
void remove_defeated_enemy_group_core(Machine &m);
void reset_field_state_core(Machine &m);
void resolve_ai_action_fallback_core(Machine &m);
bool run_battle_core(Machine &m);
void select_next_battle_member_core(Machine &m);
void select_party_menu_category_core(Machine &m);
void set_battle_meter_target_core(Machine &m);
void shake_screen_for_party_hit_core(Machine &m);
void show_actor_item_failed_message_core(Machine &m);
void show_actor_paralyzed_message_core(Machine &m);
void show_actor_asleep_message_core(Machine &m);
void show_actor_poison_ended_message_core(Machine &m);
void show_status_inflicted_message_core(Machine &m);
void show_target_attack_message_core(Machine &m);
void show_target_item_message_core(Machine &m);
void toggle_meat_menu_cursor_core(Machine &m);
void try_open_field_menu_core(Machine &m);
void wait_unusable_item_message_core(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {
// Select the result type from current/meat families and their maximum rank; AL=current type, AH=meat type.
void lookup_meat_transformation_core(Machine &m);
// Replace persistent member AL (low two bits) with monster AH and initialize its HP, stats, and abilities.
void initialize_monster_member_core(Machine &m);
void open_status_popup_core(Machine &m);
void clear_field_overlay_core(Machine &m);
void animate_battle_entry_core(Machine &m);
void draw_meat_reward_core(Machine &m);
void build_name_string_core(Machine &m);
void clear_battle_background_core(Machine &m);
void clear_battle_left_pane_core(Machine &m);
void clear_battle_right_pane_core(Machine &m);
void clear_battle_inner_pane_core(Machine &m);
void draw_debug_extended_menu_core(Machine &m);
void draw_debug_settings_menu_core(Machine &m);
void position_debug_settings_cursor_core(Machine &m);
void position_debug_list_cursor_core(Machine &m);
void draw_debug_encounter_values_core(Machine &m);
void draw_debug_configuration_values_core(Machine &m);
void build_action_target_description_core(Machine &m);
void append_target_group_suffix_core(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {
void select_next_party_member_core(Machine &m);
void select_previous_party_member_core(Machine &m);
void apply_equipment_transfer_choice_core(Machine &m);
void announce_battle_action_once_core(Machine &m);
void choose_special_actor_action_core(Machine &m);
void handle_ally_target_keys_core(Machine &m);
void load_battle_palette_display_core(Machine &m);
// Apply the countering target's effective strength as damage after swapping battle sides, then restore the
// sides.
void resolve_counterattack_core(Machine &m);
} // namespace sawgod::game
