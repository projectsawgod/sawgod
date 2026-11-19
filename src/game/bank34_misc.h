// Shared menu, character-creation, and drawing routine entry points.
#pragma once
#include "game/machine.h"
namespace sawgod::game {
void format_stat_3digit(Machine &m);
void format_hours_2digit(Machine &m);
void number32_to_digits(Machine &m);
void number32_to_digits_core(Machine &m);
void format_gold(Machine &m);
void clear_progress_history(Machine &m);
void clear_progress_histories(Machine &m);
void clear_event_flags(Machine &m);
void reset_dialogue_party_order(Machine &m);
void copy_save_payload(Machine &m);
void draw_creation_icon(Machine &m);
void draw_bcd_left(Machine &m);
void draw_bcd_zero_padded(Machine &m);
void draw_item_name(Machine &m);
void test_menu_categories(Machine &m);
void draw_race_stat(Machine &m);
void draw_creation_race_label(Machine &m);
void format_stat_3digit_core(Machine &m);
void format_hours_2digit_core(Machine &m);
void format_gold_core(Machine &m);
void draw_bcd_left_core(Machine &m);
void draw_bcd_zero_padded_core(Machine &m);
void draw_item_name_core(Machine &m);
void draw_race_stat_core(Machine &m);
void draw_creation_race_label_core(Machine &m);
void draw_item_icon(Machine &m);
void draw_item_count(Machine &m);
void draw_item_entry(Machine &m);
void draw_item_without_count(Machine &m);
void draw_item_entry_rows(Machine &m);
void load_save_leader_sprite(Machine &m);
void load_party_menu_sprites(Machine &m);
void load_party_menu_sprites_core(Machine &m);
void load_creation_race_sprite(Machine &m);
void draw_creation_race_row(Machine &m);
void draw_save_party_preview(Machine &m);
void draw_selected_save_party(Machine &m);
void draw_name_entry_caret(Machine &m);
void prepare_party_menu(Machine &m);
void draw_save_slot_summary(Machine &m);
void draw_save_slot_summaries(Machine &m);
void draw_save_menu(Machine &m);
void draw_party_overview(Machine &m);
void draw_member_summary(Machine &m);
void draw_option_label(Machine &m);
void read_last_save_slot(Machine &m);
void draw_creation_name(Machine &m);
void draw_name_entry_alphabet(Machine &m);
void draw_name_entry_screen(Machine &m);
// Build the creation member at DI from AL's monster template (HP, race flags, stats and item uses).
void initialize_creation_template(Machine &m);
void draw_creation_type_preview(Machine &m);
void draw_creation_type_names(Machine &m);
void draw_creation_type_screen(Machine &m);
void restore_field_menu_display(Machine &m);
void prepare_field_menu_display(Machine &m);
void load_menu(Machine &m);
void write_save_slot(Machine &m);
void draw_save_confirmation(Machine &m);
void redraw_save_panels(Machine &m);
void set_event_flag(Machine &m);
void append_progress_history(Machine &m);
void clear_hardware_palettes(Machine &m);
void set_color_hardware_mode(Machine &m);
void classify_item_entry(Machine &m);
void draw_option_values(Machine &m);
void draw_options_screen(Machine &m);
void draw_creation_races(Machine &m);
void draw_race_selection_screen(Machine &m);
void draw_review_members(Machine &m);
void draw_review_screen(Machine &m);
void draw_key_items(Machine &m);
void draw_inventory_items(Machine &m);
void draw_inventory_screen(Machine &m);
void copy_direct_palettes(Machine &m);
void copy_mono_tiles(Machine &m);
void brighten_palette_step(Machine &m);
void lookup_wave_sample(Machine &m);
void lookup_wave_sample_core(Machine &m);
// Resolve ordered family pair and maximum rank for AL/AH; FF means no transformation.
void lookup_meat_transformation(Machine &m);
// Replace member AL&3 from monster AH; original zero-count ability-copy overrun is retained.
void initialize_monster_member(Machine &m);
void lookup_character_descriptor(Machine &m);
void restore_menu_sprites(Machine &m);
void restore_menu_palettes(Machine &m);
void restore_menu_screen_one(Machine &m);
void restore_menu_screen_two(Machine &m);
void draw_direction_choice_cursor(Machine &m);
void draw_direction_choices(Machine &m);
void draw_direction_choice_icon(Machine &m);
// Refill a valid inventory target and spend one refill-item use; CF reports rejection.
void refill_inventory_item(Machine &m);
void draw_tower_destination(Machine &m);
void draw_tower_destination_number(Machine &m);
void draw_item_error_tiles(Machine &m);
void swap_item_records(Machine &m);
void erase_item_record(Machine &m);
void draw_refill_target_cursor(Machine &m);
void draw_item_target_cursor(Machine &m);
void draw_item_targets(Machine &m);
void draw_item_target_summary(Machine &m);
void count_party_members(Machine &m);
void find_last_party_member(Machine &m);
void draw_equipment_refill_cursor(Machine &m);
void draw_equipment_action_cursor(Machine &m);
void draw_equipment_review_screen(Machine &m);
void draw_equipment_review_member(Machine &m);
void draw_equipment_left_marker(Machine &m);
void draw_equipment_right_marker(Machine &m);
void draw_refill_inventory(Machine &m);
void draw_refill_instructions(Machine &m);
void draw_tower_instructions(Machine &m);
void select_next_equipment_member(Machine &m);
void select_previous_equipment_member(Machine &m);
// Validate cross-list equipment replacement; preserve source/destination pointers and return CF.
void prepare_equipment_transfer(Machine &m);
void validate_key_item_transfer(Machine &m);
void swap_equipment_records(Machine &m);
// Reject duplicate categories, otherwise remove/add saturated byte stat bonuses; CF reports rejection.
void replace_equipment_stats(Machine &m);
void draw_equipment_selection_cursor(Machine &m);
// Return the selected member's permitted equipment-slot bounds and adjust restricted selections.
void equipment_slot_bounds(Machine &m);
void draw_equipment_edit_screen(Machine &m);
void select_next_party_member(Machine &m);
void select_previous_party_member(Machine &m);
void draw_equipment_edit_member(Machine &m);
void draw_equipment_inventory(Machine &m);
void draw_equipment_inventory_rows(Machine &m);
void draw_member_equipment_rows(Machine &m);
void clear_equipment_left_marker(Machine &m);
void clear_equipment_right_marker(Machine &m);
void draw_equipment_inventory_arrow(Machine &m);
void clear_equipment_inventory_arrow(Machine &m);
void validate_party_reorder(Machine &m);
void swap_party_records(Machine &m);
void update_party_swap_tiles(Machine &m);
void draw_party_swap_label(Machine &m);
void append_party_swap_label_sprites(Machine &m);
void select_next_creation_type(Machine &m);
void select_previous_creation_type(Machine &m);
void draw_creation_grid_cursor(Machine &m);
void draw_creation_equipment_cursor(Machine &m);
void draw_creation_type_detail_screen(Machine &m);
void draw_creation_item_description(Machine &m);
void draw_creation_unlock_preview(Machine &m);
void draw_creation_selected_preview(Machine &m);
void draw_creation_type_alternate_icon(Machine &m);
void draw_creation_type_walking_icon(Machine &m);
void draw_creation_large_graphic(Machine &m);
void creation_icon_position(Machine &m);
void creation_palette_source(Machine &m);
void creation_type_index(Machine &m);
void draw_unlocked_creation_icon(Machine &m);
void swap_member_progress(Machine &m);
void draw_masked_character_tilemap(Machine &m);
void draw_save_member_sprite(Machine &m);
void load_save_member_graphics(Machine &m);
void write_last_save_slot(Machine &m);
void draw_options_primary_cursor(Machine &m);
void draw_options_secondary_cursor(Machine &m);
void draw_abbreviated_option_panel(Machine &m);
void draw_full_option_panel(Machine &m);
void draw_centered_option_panel(Machine &m);
void draw_dialogue_speed_panel(Machine &m);
void draw_dialogue_party_order_panel(Machine &m);
void draw_dialogue_member_name(Machine &m);
void draw_dialogue_member_list(Machine &m);
void draw_load_menu_cursor(Machine &m);
void draw_load_menu_screen(Machine &m);
void draw_text_n_dark(Machine &m);
void show_secondary_menu_cursor(Machine &m);
void show_refill_menu_cursor(Machine &m);
void hide_secondary_menu_cursor(Machine &m);
void hide_primary_menu_cursor(Machine &m);
void draw_menu_selection_description(Machine &m);
void read_menu_progress_nibble(Machine &m);
void draw_member_race_abbreviation(Machine &m);
void draw_history_member_cursor(Machine &m);
void draw_history_row_cursor(Machine &m);
void draw_monster_history_screen(Machine &m);
void draw_history_members(Machine &m);
void draw_history_member(Machine &m);
void draw_selected_monster_history(Machine &m);
void draw_monster_history_page(Machine &m);
void draw_monster_history_row(Machine &m);
void advance_monster_history_index(Machine &m);
void select_next_history_member(Machine &m);
void select_previous_history_member(Machine &m);
void draw_inn_confirmation_cursor(Machine &m);
// Store the missing-HP price for living members and draw the inn confirmation.
void draw_inn_screen(Machine &m);
void draw_inn_party(Machine &m);
void restore_saved_menu_buffers(Machine &m);
// Format the shared 32-bit service price (also used by shop and revive screens).
void format_inn_cost(Machine &m);
void draw_compact_member_icon(Machine &m);
void draw_inn_member_summary(Machine &m);
void load_compact_party_graphics(Machine &m);
void clear_inn_message(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {
void append_progress_history_core(Machine &m);
void clear_equipment_inventory_arrow_core(Machine &m);
void clear_equipment_left_marker_core(Machine &m);
void clear_equipment_right_marker_core(Machine &m);
void clear_inn_message_core(Machine &m);
void clear_progress_history_core(Machine &m);
void count_party_members_core(Machine &m);
void draw_creation_item_description_core(Machine &m);
void draw_creation_type_detail_screen_core(Machine &m);
void draw_creation_type_preview_core(Machine &m);
void draw_creation_type_screen_core(Machine &m);
void draw_dialogue_member_list_core(Machine &m);
void draw_direction_choices_core(Machine &m);
void draw_equipment_action_cursor_core(Machine &m);
void draw_equipment_edit_screen_core(Machine &m);
void draw_equipment_inventory_arrow_core(Machine &m);
void draw_equipment_left_marker_core(Machine &m);
void draw_equipment_review_screen_core(Machine &m);
void draw_equipment_right_marker_core(Machine &m);
void draw_equipment_selection_cursor_core(Machine &m);
void draw_inventory_items_core(Machine &m);

void draw_item_error_tiles_core(Machine &m);
void draw_item_targets_core(Machine &m);
void draw_menu_selection_description_core(Machine &m);
void draw_monster_history_page_core(Machine &m);
void draw_name_entry_alphabet_core(Machine &m);
void draw_name_entry_caret_core(Machine &m);
void draw_options_secondary_cursor_core(Machine &m);
void draw_party_overview_core(Machine &m);
void draw_party_swap_label_core(Machine &m);
void draw_refill_instructions_core(Machine &m);
void draw_refill_inventory_core(Machine &m);
void draw_save_confirmation_core(Machine &m);
void draw_selected_save_party_core(Machine &m);
void draw_tower_destination_entry_core(Machine &m);
void draw_tower_destination_number_core(Machine &m);
void draw_tower_instructions_core(Machine &m);
// Derive slot restrictions from the selected member's race flags.
void equipment_slot_bounds_core(Machine &m);
void erase_item_record_core(Machine &m);
void find_last_party_member_core(Machine &m);
void hide_primary_menu_cursor_core(Machine &m);
void hide_secondary_menu_cursor_core(Machine &m);
void load_menu_core(Machine &m);
void prepare_equipment_transfer_core(Machine &m);
void prepare_field_menu_display_core(Machine &m);

void read_last_save_slot_core(Machine &m);
void read_menu_progress_nibble_core(Machine &m);
void redraw_save_panels_core(Machine &m);
// Apply the same category validation and saturated stat adjustment without a machine return.
void replace_equipment_stats_core(Machine &m);
void restore_field_menu_display_core(Machine &m);
void select_next_equipment_member_core(Machine &m);
void select_previous_equipment_member_core(Machine &m);
void show_refill_menu_cursor_core(Machine &m);
void show_secondary_menu_cursor_core(Machine &m);
void swap_equipment_records_core(Machine &m);
void swap_item_records_core(Machine &m);
void swap_party_records_core(Machine &m);
void test_menu_categories_core(Machine &m);
void update_party_swap_tiles_core(Machine &m);
void validate_key_item_transfer_core(Machine &m);
void validate_party_reorder_core(Machine &m);
void write_last_save_slot_core(Machine &m);
void write_save_slot_core(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {
void draw_direction_choice_cursor_core(Machine &m);
}

namespace sawgod::game {
void draw_equipment_refill_cursor_core(Machine &m);
}

namespace sawgod::game {
void draw_refill_target_cursor_core(Machine &m);
}

namespace sawgod::game {
void draw_item_target_cursor_core(Machine &m);
}

namespace sawgod::game {
void draw_name_entry_screen_core(Machine &m);
}

namespace sawgod::game {
void refill_inventory_item_core(Machine &m);
}

namespace sawgod::game {
void select_previous_creation_type_core(Machine &m);
}

namespace sawgod::game {
void select_next_creation_type_core(Machine &m);
}

namespace sawgod::game {
void prepare_party_menu_entry_core(Machine &m);
}

namespace sawgod::game {
void draw_item_entry_rows_entry_core(Machine &m);
void swap_member_progress_core(Machine &m);
void draw_inventory_screen_core(Machine &m);
void prepare_party_menu_entry_core(Machine &m);
void draw_review_screen_core(Machine &m);
void draw_race_selection_screen_core(Machine &m);
void draw_creation_races_entry_core(Machine &m);
void draw_save_menu_core(Machine &m);
void draw_options_screen_core(Machine &m);
void run_inventory_tab(Machine &m);
void run_inventory_tab_core(Machine &m);
void run_equipment_tab(Machine &m);
void run_equipment_tab_core(Machine &m);
void run_transfer_tab(Machine &m);
void run_transfer_tab_core(Machine &m);
void run_formation_tab(Machine &m);
void run_formation_tab_core(Machine &m);
void run_progress_tab(Machine &m);
void run_progress_tab_core(Machine &m);
void run_save_tab(Machine &m);
void run_save_tab_core(Machine &m);
void run_options_tab(Machine &m);
void run_options_tab_core(Machine &m);
} // namespace sawgod::game
