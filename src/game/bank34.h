// Decompiled routines from bank 0x34 (main engine).
#pragma once

#include "game/machine.h"

namespace sawgod::game {

void wait_frames(Machine &m);

void wait_vblank(Machine &m);           // 0x340009 (far, yields)
void wait_visible_scanline(Machine &m); // 0x340051 (far, yields)
void read_keypad(Machine &m);           // 0x34005C
// Advance the table RNG; return AL = table[(calls + frames)&255], AH = 0.
void random_byte(Machine &m); // 0x3401BD
// Return an inclusive AL..AH roll with rounded byte scaling; reversed bounds wrap.
void random_range(Machine &m);           // 0x3401D7
void copy_tiles(Machine &m);             // 0x34028E
void clear_tiles(Machine &m);            // 0x3402A3
void fill_screen_rect(Machine &m);       // 0x3402BC
void draw_tilemap(Machine &m);           // 0x34032D
void draw_tilemap_offset(Machine &m);    // 0x340369
void draw_window_alt(Machine &m);        // 0x34047F
void draw_window(Machine &m);            // 0x340503
void copy_palettes(Machine &m);          // 0x340582
void fill_palette(Machine &m);           // 0x34059F
void brighten_palette_step(Machine &m);  // 0x3405C5
void darken_palette_step(Machine &m);    // 0x340607
void palette_step_up_toward(Machine &m); // 0x34063D
void palette_step_toward(Machine &m);    // 0x3406EF
void dma_sprite_table(Machine &m);       // 0x34075B
void dma_palettes(Machine &m);           // 0x34078A
void set_sprite_16x16(Machine &m);       // 0x340839
void draw_text(Machine &m);              // 0x34091D
void number_to_digits(Machine &m);       // 0x340B73
// Reset persistent game state, starting map, empty party/inventories and member histories.
void clear_game_state(Machine &m);       // 0x34106D
void char_type_to_gfx_index(Machine &m); // 0x34129A
void lookup_name_record(Machine &m);     // 0x3412F6

// bank34_menus.cpp (rom >= 0x34A000)
void append_word_queue(Machine &m); // 0x34CA20
void draw_status_icons(Machine &m); // 0x34CB44

// bank34_engine.cpp (near routines scattered through 0x341000-0x34C000)
void draw_race_select_cursor(Machine &m); // 0x341A80 (near)
void draw_play_time(Machine &m);          // 0x341F5A (near)
void copy_bytes_terminated(Machine &m);   // 0x347F06 (near)
void draw_text_n_white(Machine &m);       // 0x347D06 (near)
void format_stat_2digit(Machine &m);      // 0x347FEE (near)
void format_seconds_2digit(Machine &m);   // 0x348025 (near)
// Set DS = SS and clear play-time scratch, then perform the same hours:minutes conversion.
void play_time_to_minutes_seconds(Machine &m); // 0x3480CB (near)
// Split floor(DX:AX/4500) into hours:minutes in AL:AH, capped at 99:59 (historical name).
void frames_to_minutes_seconds(Machine &m);     // 0x3480FC (near)
void draw_class_select_cursor(Machine &m);      // 0x3461C1 (near)
void draw_gender_select_cursor(Machine &m);     // 0x3461E0 (near)
void draw_race_icon_cursor(Machine &m);         // 0x34AEB6 (near)
void draw_class_gender_icon_cursor(Machine &m); // 0x34B566 (near)

// bank34_engine.cpp continued (character-creation cursors, field menu, BCD/race formatters, save
// slot verification, event flags, sound trigger)
void draw_creation_step_cursor(Machine &m);   // 0x34453B (near)
void draw_class_menu_cursor(Machine &m);      // 0x344F15 (near)
void draw_class_or_gender_cursor(Machine &m); // 0x3428E4 (near)
void draw_field_menu_cursor(Machine &m);      // 0x347204 (near)
void draw_field_menu_label(Machine &m);       // 0x347261 (near)
void draw_class_review_cursor(Machine &m);    // 0x3454F0 (near)
void format_stat_1digit(Machine &m);          // 0x347F84 (near)
void write_race_abbreviation(Machine &m);     // 0x347F4D (near)
void decimal_to_bcd_clamped(Machine &m);      // 0x347EBE (near)
void draw_bcd_number(Machine &m);             // 0x347D78 (near)
void copy_and_verify_save_block(Machine &m);  // 0x3469E1 (near)
void load_save_slot(Machine &m);              // 0x3469A4 (near)
void test_event_flag(Machine &m);             // 0x345F29 (near)
void draw_flag_gated_icon(Machine &m);        // 0x345DB0 (near)
void trigger_sound_effect(Machine &m);        // 0x348518 (near)

// Internal, not registered: shared cores reused by other near routines that reach these via a real
// `call`/`lcall` from another near routine and so can't unwind a synthetic return address (same reasoning
// as next_random_byte's comment in bank34.cpp).
void draw_tilemap_offset_core(Machine &m);
void draw_tilemap_core(Machine &m);
void draw_text_core(Machine &m);
void set_sprite_16x16_core(Machine &m);
uint16_t number_to_digits_core(Machine &m, uint16_t number, uint16_t ds, uint16_t si);
uint16_t screen_map_offset(uint8_t row, uint16_t bx);

// Shared bodies; callers reproduce the original register effects without a machine return.
void random_range_core(Machine &m);
void copy_tiles_core(Machine &m);
void copy_palettes_core(Machine &m);
void fill_screen_rect_core(Machine &m);
void draw_window_core(Machine &m);
void char_type_to_gfx_index_core(Machine &m);
void lookup_name_record_core(Machine &m);
void clear_game_state_core(Machine &m);
void append_word_queue_core(Machine &m);
void draw_status_icons_core(Machine &m);
void copy_bytes_terminated_core(Machine &m);
void draw_text_n_white_core(Machine &m);
void format_stat_2digit_core(Machine &m);
void format_seconds_2digit_core(Machine &m);
void format_stat_1digit_core(Machine &m);
void write_race_abbreviation_core(Machine &m);
void decimal_to_bcd_clamped_core(Machine &m);
void draw_bcd_number_core(Machine &m);
void load_save_slot_core(Machine &m);
void draw_flag_gated_icon_core(Machine &m);
void play_time_to_minutes_seconds_core(Machine &m);
void draw_play_time_core(Machine &m);

} // namespace sawgod::game

namespace sawgod::game {
void darken_palette_step_core(Machine &m);
void palette_step_toward_core(Machine &m);
void dma_palettes_core(Machine &m);
void dma_sprite_table_core(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {
void status_popup_wait(Machine &m);
void open_status_popup(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {
void fill_palette_core(Machine &m);

void fade_menu_palettes(Machine &m);
void select_party_menu_category(Machine &m);
void accept_direction_choice(Machine &m);
void select_direction_destination(Machine &m);
void select_class_action(Machine &m);
void wait_save_confirmation(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {
void brighten_palette_step_core(Machine &m);
void palette_step_up_toward_core(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {
void open_party_menu(Machine &m);

void confirm_save_slot(Machine &m);

void edit_creation_name(Machine &m);

void new_game_setup(Machine &m);

// Swap/discard inventory records or dispatch the selected item.
void apply_inventory_slot_choice(Machine &m);

// Dispatch an inventory item's field action or reject an unusable choice.
void dispatch_inventory_item_action(Machine &m);

void select_inventory_refill(Machine &m);

void select_inventory_destination(Machine &m);

void use_inventory_destination(Machine &m);

void select_inventory_item_target(Machine &m);

void wait_unusable_item_message(Machine &m);

void select_equipment_refill(Machine &m);

void select_equipment_destination(Machine &m);

void use_equipment_destination(Machine &m);

void select_equipment_item_target(Machine &m);

void wait_equipment_transfer_error(Machine &m);

// Swap the selected party records and repair the dialogue leader/order references.
void swap_selected_party_members(Machine &m);

// Apply a field inventory item and spend a finite use.
void use_inventory_item_on_actor(Machine &m);

// Apply an equipped field item and spend a finite use.
void use_equipment_item_on_actor(Machine &m);

// Dispatch a usable item when source and destination coincide.
void accept_equipment_item_action(Machine &m);

// Dispatch the equipped item's field behavior from its ID.
void dispatch_equipment_item_action(Machine &m);

// Refill a different eligible item to its table maximum and spend one refill-item use.
void refill_selected_equipment_item(Machine &m);

// Validate and apply a selected equipment transfer.
void apply_equipment_transfer_choice(Machine &m);

// Move an item into the first compatible free slot.
void auto_transfer_equipment_item(Machine &m);

void select_equipment_item_action(Machine &m);

void run_equipment_item_menu(Machine &m);

void run_equipment_transfer_menu(Machine &m);

void select_equipment_transfer_destination(Machine &m);

void select_party_swap_destination(Machine &m);

void update_party_swap_frame(Machine &m);

void wait_party_swap_frames(Machine &m);

void animate_party_member_swap(Machine &m);

void draw_growth_review_cursor(Machine &m);

void draw_bestiary_category_cursor(Machine &m);

void draw_bestiary_list_cursor(Machine &m);

void apply_selected_item_effect(Machine &m);

void wait_menu_error_message(Machine &m);

void select_field_menu_assignment(Machine &m);

void run_growth_review_menu(Machine &m);

void browse_bestiary_list(Machine &m);

void draw_load_slot_prompt(Machine &m);

void confirm_load_slot(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {
void accept_direction_choice_core(Machine &m);
void accept_equipment_item_action_core(Machine &m);
void apply_inventory_slot_choice_core(Machine &m);
void auto_transfer_equipment_item_core(Machine &m);

void dispatch_inventory_item_action_core(Machine &m);
void draw_class_gender_icon_cursor_core(Machine &m);
void draw_class_review_cursor_core(Machine &m);
void draw_field_menu_label_core(Machine &m);
void draw_gender_select_cursor_core(Machine &m);

void draw_race_icon_cursor_core(Machine &m);
void draw_race_select_cursor_core(Machine &m);

void draw_window_alt_core(Machine &m);
void edit_creation_name_core(Machine &m);

void new_game_setup_core(Machine &m);
void number_to_digits_entry_core(Machine &m);
void status_popup_wait_core(Machine &m, uint16_t timing_words = 0);
void swap_selected_party_members_core(Machine &m);
void trigger_sound_effect_core(Machine &m);
void wait_frames_core(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {
void draw_class_or_gender_cursor_core(Machine &m);
}

namespace sawgod::game {
void draw_class_menu_cursor_core(Machine &m);
}

namespace sawgod::game {
void refill_selected_equipment_item_core(Machine &m);
}

namespace sawgod::game {
void use_inventory_destination_core(Machine &m);
}

namespace sawgod::game {
void use_equipment_destination_core(Machine &m);
}

namespace sawgod::game {
void use_inventory_item_on_actor_core(Machine &m);
}

namespace sawgod::game {
void use_equipment_item_on_actor_core(Machine &m);
}

namespace sawgod::game {
void apply_selected_item_effect_core(Machine &m);
}

namespace sawgod::game {
void open_party_menu_core(Machine &m);
}

namespace sawgod::game {
void select_party_menu_category_core(Machine &m);
}

namespace sawgod::game {
void confirm_save_slot_core(Machine &m);
}

namespace sawgod::game {
void wait_save_confirmation_core(Machine &m);
}

namespace sawgod::game {
void select_direction_destination_core(Machine &m);
}

namespace sawgod::game {
void select_class_action_core(Machine &m);
}

namespace sawgod::game {
void select_inventory_refill_core(Machine &m);
}

namespace sawgod::game {
void select_inventory_destination_core(Machine &m);
}

namespace sawgod::game {
void select_inventory_item_target_core(Machine &m);
}

namespace sawgod::game {
void wait_unusable_item_message_core(Machine &m);
void wait_orb_pair_missing_message_core(Machine &m);
} // namespace sawgod::game

namespace sawgod::game {
void run_equipment_item_menu_core(Machine &m);
}

namespace sawgod::game {
void select_equipment_item_action_core(Machine &m);
}

namespace sawgod::game {
void dispatch_equipment_item_action_core(Machine &m);
}

namespace sawgod::game {
void select_equipment_refill_core(Machine &m);
}

namespace sawgod::game {
void select_equipment_destination_core(Machine &m);
}

namespace sawgod::game {
void select_equipment_item_target_core(Machine &m);
}

namespace sawgod::game {
void run_equipment_transfer_menu_core(Machine &m);
}

namespace sawgod::game {
void select_equipment_transfer_destination_core(Machine &m);
}

namespace sawgod::game {
void wait_equipment_transfer_error_core(Machine &m);
}

namespace sawgod::game {
void select_party_swap_destination_core(Machine &m);
}

namespace sawgod::game {
void update_party_swap_frame_core(Machine &m);
}

namespace sawgod::game {
void wait_party_swap_frames_core(Machine &m);
}

namespace sawgod::game {
void animate_party_member_swap_core(Machine &m);
}

namespace sawgod::game {
void run_growth_review_menu_core(Machine &m);
}

namespace sawgod::game {
void draw_growth_review_cursor_core(Machine &m);
}

namespace sawgod::game {
void select_field_menu_assignment_core(Machine &m);
}

namespace sawgod::game {
void draw_load_slot_prompt_core(Machine &m);
}

namespace sawgod::game {
void confirm_load_slot_core(Machine &m);
}

namespace sawgod::game {
void draw_bestiary_list_cursor_core(Machine &m);
}

namespace sawgod::game {
void browse_bestiary_list_core(Machine &m);
}

namespace sawgod::game {
void draw_bestiary_category_cursor_core(Machine &m);
}

namespace sawgod::game {
void wait_menu_error_message_core(Machine &m);
void open_status_popup_core(Machine &m);
// A nonzero count changes only the word waits, leaving the displayed queue intact.
void open_status_popup_core(Machine &m, uint16_t timing_words);
void draw_creation_step_cursor_core(Machine &m);
void draw_class_select_cursor_core(Machine &m);
void draw_field_menu_cursor_core(Machine &m);
} // namespace sawgod::game
