// Registry of decompiled bank 0x36 battle/UI helpers.
#include "game/bank36_battle.h"
#include "game/machine.h"

namespace sawgod::game {
namespace {
// Arithmetic flags are dead at the call sites (including through intervening MOV/POP/RET).
// The byte swap and joined popup announcements are also checked with every arithmetic flag intact.
const Routine kBank36[] = {
    {"clear_battle_ai_flags", 0x360A9D, clear_battle_ai_flags, true, true},
    {"mark_battle_end_flags", 0x360ABC, mark_battle_end_flags, true, true},
    {"tally_enemy_type_counts", 0x360D6D, tally_enemy_type_counts, true, true},
    {"swap_battle_order_entries", 0x3611B2, swap_battle_order_entries, false, true},
    {"battle_order_entry_pointer", 0x3611F1, battle_order_entry_pointer, true, true},
    {"battle_actor_below_requirement", 0x362936, battle_actor_below_requirement, true, true},
    {"draw_battle_member_panels", 0x362A39, draw_battle_member_panels, true, true},
    {"draw_battle_status_sprites", 0x3630D1, draw_battle_status_sprites, true, true},
    {"hide_battle_status_sprites", 0x36326D, hide_battle_status_sprites, true, true},
    {"blink_battle_command_cursor", 0x3632C5, blink_battle_command_cursor, true, true},
    {"draw_battle_command_cursor", 0x3634F1, draw_battle_command_cursor, true, true},
    {"build_formation_layer_record", 0x3635E1, build_formation_layer_record, true, true},
    {"build_formation_layer_records", 0x36368F, build_formation_layer_records, true, false},
    {"draw_class_icon_or_label", 0x3637E0, draw_class_icon_or_label, true, true},
    {"draw_text_alt", 0x363860, draw_text_alt, true, false},
    {"reverse_bits_al", 0x36402B, reverse_bits_al, true, true},
    // battle_priority_less_than (0x36403B): the only caller's `jb` reads CF straight after the call, so
    // FLAGS must be exact -- checked without the arithmetic-flags mask.
    {"battle_priority_less_than", 0x36403B, battle_priority_less_than, false, true},
    {"party_slot_for_name_id", 0x3640E6, party_slot_for_name_id, true, false},
    {"draw_battle_skill_list", 0x36271F, draw_battle_skill_list, true, true},
    {"draw_battle_digit_row", 0x36561D, draw_battle_digit_row, true, false},
    {"draw_battle_digit_tile", 0x365638, draw_battle_digit_tile, true, false},
    {"count_actor_available_items", 0x364D87, count_actor_available_items, true, true},
    // has_table_flag_bit1 (0x365010): callers branch on both CF and ZF (jae/je/jb), so FLAGS must be
    // exact -- checked without the arithmetic-flags mask.
    {"has_table_flag_bit1", 0x365010, has_table_flag_bit1, false, true},
    {"battle_actor_pointer", 0x365042, battle_actor_pointer, true, true},
    {"battle_actor_stat_byte9_halved", 0x36502E, battle_actor_stat_byte9_halved, true, true},
    {"advance_record_pointer", 0x365056, advance_record_pointer, true, false},
    {"battle_actor_status_byte", 0x365067, battle_actor_status_byte, true, false},
    {"selected_battle_equipment", 0x36507D, selected_battle_equipment, true, false},
    {"format_number", 0x3650C3, format_number, true, false},
    {"build_party_name", 0x3650FB, build_party_name, true, false},
    {"build_status_icons", 0x3651C2, build_status_icons, true, false},
    {"build_icon_list", 0x365212, build_icon_list, true, false},
    {"lookup_table_byte", 0x36526E, lookup_table_byte, true, false},
    {"draw_battle_marker_sprite", 0x3652DA, draw_battle_marker_sprite, true, false},
    {"draw_battle_marker_sprite_alt", 0x36530D, draw_battle_marker_sprite_alt, true, false},
    {"draw_battle_marker_sprite_alt2", 0x365340, draw_battle_marker_sprite_alt2, true, false},
    {"draw_battle_status_icon_tiles", 0x365373, draw_battle_status_icon_tiles, true, false},
    {"hide_battle_menu_sprites", 0x3653A8, hide_battle_menu_sprites, true, false},
    {"draw_battle_cursor_tiles", 0x3653BF, draw_battle_cursor_tiles, true, false},
    {"draw_battle_cursor_tiles_alt", 0x3653EF, draw_battle_cursor_tiles_alt, true, false},
    {"hide_and_redraw_battle_cursors", 0x365422, hide_and_redraw_battle_cursors, true, false},
    {"clear_battle_message_box", 0x365450, clear_battle_message_box, true, false},
    {"clear_battle_field_tiles", 0x3655A0, clear_battle_field_tiles, true, false},
    {"draw_numeral_string", 0x3655CD, draw_numeral_string, true, false},
    {"draw_glyph_tile", 0x3655EB, draw_glyph_tile, true, false},
    {"draw_glyph_string", 0x365686, draw_glyph_string, true, false},
    {"compact_battle_digits", 0x3656A6, compact_battle_digits, true, true},
    {"blank_pad_and_extract_digits", 0x3656E8, blank_pad_and_extract_digits, true, true},
    {"draw_tile_box_border", 0x365763, draw_tile_box_border, true, false},
    {"actor_has_equipped_item", 0x3666CE, actor_has_equipped_item, true, true},
    {"build_battle_member_display", 0x369D8E, build_battle_member_display, true, true},
    {"build_battle_member_name_or_icons", 0x369E1B, build_battle_member_name_or_icons, true, true},
    {"restore_saved_registers", 0x36C4D7, restore_saved_registers, true, true},

    // bank 0x36 battle routines, third pass (strategy/equipment/target handlers' surviving
    // dependencies plus assorted small helpers)
    {"log_battle_menu_state", 0x361976, log_battle_menu_state, true, true},
    {"draw_actor_item_list", 0x362EAC, draw_actor_item_list, true, true},
    {"selected_equipment_name_or_placeholder", 0x36306E, selected_equipment_name_or_placeholder, true, true},
    {"update_battle_marker_highlight", 0x363579, update_battle_marker_highlight, true, true},
    {"copy_bytes_ds_si_to_ss_di", 0x364083, copy_bytes_ds_si_to_ss_di, true, true},
    {"battle_actor_stat_byte_b", 0x364FCC, battle_actor_stat_byte_b, true, true},
    {"battle_actor_stat_byte7_halved", 0x364FDA, battle_actor_stat_byte7_halved, true, true},
    {"battle_actor_stat_byte8_halved", 0x364FF5, battle_actor_stat_byte8_halved, true, true},
    {"draw_tile_box_border_alt", 0x3657E1, draw_tile_box_border_alt, true, false},

    // The battle loop / enemy AI / battle input family that earlier notes here deferred (0x360615 and its
    // helpers, 0x360B2E, 0x3604DA/0x3605CF/0x360DE5, the callers) is registered at the end of this table;
    // including battle_input_step (0x360186) and run_battle (0x360062).

    // bank 0x36 routines that wait for frames or call original code (yields)
    {"flush_battle_frame", 0x360142, flush_battle_frame, true, false, true},
    {"battle_meter_step", 0x364F43, battle_meter_step, true, true, true},
    {"set_battle_meter_target", 0x364FB1, set_battle_meter_target, true, true, true},
    {"queue_item_count_footer", 0x36A3DE, queue_item_count_footer, true, true, true},
    {"open_party_member_popup", 0x36A557, open_party_member_popup, true, true, true},
    {"copy_formation_palette", 0x36CF27, copy_formation_palette, true},
    {"copy_formation_palette_next", 0x36D079, copy_formation_palette_next, true},
    {"load_formation_set", 0x362E00, load_formation_set, true},
    {"draw_party_status_row", 0x363714, draw_party_status_row, true, true},

    // battle-menu dispatcher and its input handlers (yields: call-outs to not-yet-decompiled
    // routines and, on one path, to the original wait_vblank/dma_sprite_table)
    {"battle_menu_dispatcher", 0x361D60, battle_menu_dispatcher, true, true, true},
    {"reset_and_reopen_battle_menu", 0x36198E, reset_and_reopen_battle_menu, true, true, true},
    {"battle_command_menu_input", 0x3611FF, battle_command_menu_input, true, true, true},
    {"battle_target_menu_input", 0x36147B, battle_target_menu_input, true, true, true},
    {"battle_strategy_menu_input", 0x3616CD, battle_strategy_menu_input, true, true, true},

    // AI-dispatcher-family leaves
    {"copy_byte_advance_stride", 0x363FE1, copy_byte_advance_stride, true, true},
    {"stride_copy_row", 0x363FEC, stride_copy_row, true, true},
    {"clear_or_flag_actor_records", 0x360CB7, clear_or_flag_actor_records, true, true, true},
    {"priority_bubble_pass", 0x36115C, priority_bubble_pass, true, true},
    {"battle_order_shuffle", 0x36110F, battle_order_shuffle, true, true},
    {"count_action_order_alive", 0x361049, count_action_order_alive, true, true},
    {"fade_all_palettes_to_black", 0x3619B6, fade_all_palettes_to_black, true, true, true},
    {"resolve_ai_action_fallback", 0x360F17, resolve_ai_action_fallback, true, true},

    // the battle loop, enemy AI and battle input dispatchers
    // resolve_action_target: the only caller does `jb` straight after -- exact FLAGS.
    {"resolve_action_target", 0x360B2E, resolve_action_target, false, true},
    {"select_next_battle_member", 0x364D3E, select_next_battle_member, true, true, true},
    {"position_enemy_target_cursor", 0x3632EC, position_enemy_target_cursor, true, true},
    {"shake_screen_for_party_hit", 0x362B62, shake_screen_for_party_hit, true, true, true},
    {"execute_battle_action", 0x360C1C, execute_battle_action, true, true, true},
    {"draw_battle_member_boxes", 0x362987, draw_battle_member_boxes, true, true},
    {"draw_enemy_group_portrait", 0x36C8FE, draw_enemy_group_portrait, true},
    {"draw_enemy_group_portraits", 0x36C877, draw_enemy_group_portraits, true},
    {"choose_enemy_actions", 0x360DE5, choose_enemy_actions, true, true, true},
    {"execute_battle_round", 0x3605CF, execute_battle_round, true, true, true},
    {"run_battle_round", 0x3604DA, run_battle_round, true, true, true},
    // check_target_knocked_out: callers branch on CF (jae) -- exact FLAGS.
    {"check_target_knocked_out", 0x364DCE, check_target_knocked_out, false, true, true},
    {"announce_battle_action", 0x369E80, announce_battle_action, false, true, true},
    {"resolve_physical_attack", 0x366E36, resolve_physical_attack, true, true, true},
    {"execute_order_entry", 0x360615, execute_order_entry, true, true, true},
    // Time passes inside read_keypad as well as wait_vblank in frame-spanning tests (0x360186 polls it for
    // A after a victory without waiting for VBlank).
    {"battle_input_step", 0x360186, battle_input_step, true, true, true},
    // run_battle: one call is a whole battle. Verified on tests/movies/battle_playthrough.txt (a native-mode
    // recording) with no step limit (the private verification suite); in the standard movies its calls don't
    // finish in time.
    {"run_battle", 0x360062, run_battle, true, false, true},

    // battle message popups (yields: open_status_popup is still original code). The yields fuzz
    // limit is 60M steps (at most 20 fuzz passes per invocation, IVT guard off for the write below
    // 0x400 that opening the popup leaves), which clears all of these.
    {"show_gold_reward_message", 0x36BBBD, show_gold_reward_message, true, true, true},
    {"show_party_victory_message", 0x36AADC, show_party_victory_message, true, true, true},
    {"show_target_knocked_out_message", 0x36A870, show_target_knocked_out_message, true, true, true},
    {"apply_status_10_effect", 0x360CE2, apply_status_10_effect, true, true, true},
    {"try_cure_status_40", 0x3669E6, try_cure_status_40, true, true, true},
    {"try_cure_status_80", 0x366A40, try_cure_status_80, true, true, true},
    {"resolve_drain_hit", 0x366F2A, resolve_drain_hit, true, true, true},
    {"resolve_status_hit", 0x366FFD, resolve_status_hit, true, true, true},
    {"resolve_fixed_damage_hit", 0x367129, resolve_fixed_damage_hit, true, true, true},
    {"show_actor_item_failed_message", 0x36A053, show_actor_item_failed_message, false, true, true},
    {"show_damage_message", 0x36A0E9, show_damage_message, true, true, true},
    {"show_enemies_eliminated_message", 0x36A15F, show_enemies_eliminated_message, true, true, true},
    {"show_attack_missed_message", 0x36A36D, show_attack_missed_message, true, true, true},
    {"show_no_damage_message", 0x36A399, show_no_damage_message, true, true, true},
    {"show_target_died_message", 0x36A8B2, show_target_died_message, true, true, true},
    {"show_target_description_message", 0x36A907, show_target_description_message, false, true, true},
    {"show_target_attack_message", 0x36A969, show_target_attack_message, false, true, true},
    {"show_actor_confused_message", 0x36A9F1, show_actor_confused_message, true, true, true},
    {"show_actor_recovered_message", 0x36AA33, show_actor_recovered_message, false, true, true},
    {"show_party_wiped_out_message", 0x36AA95, show_party_wiped_out_message, true, true, true},
    {"show_party_escaped_message", 0x36AB1B, show_party_escaped_message, true, true, true},
    {"show_cannot_escape_message", 0x36AB9C, show_cannot_escape_message, true, true, true},
    {"show_actor_paralysis_ended_message", 0x36AC7D, show_actor_paralysis_ended_message, true, true, true},
    {"show_target_item_message", 0x36AFF9, show_target_item_message, true, true, true},
    {"show_actor_awakened_message", 0x36B073, show_actor_awakened_message, true, true, true},
    {"show_actor_asleep_message", 0x36B0B5, show_actor_asleep_message, true, true, true},
    {"show_actor_paralyzed_message", 0x36B0F7, show_actor_paralyzed_message, true, true, true},
    {"show_no_effect_message", 0x36B1BA, show_no_effect_message, true, true, true},
    {"show_actor_poison_ended_message", 0x36B337, show_actor_poison_ended_message, true, true, true},
    {"show_status_bit0_message", 0x36B48B, show_status_bit0_message, true, true, true},
    {"show_status_bit1_message", 0x36B4D0, show_status_bit1_message, false, true, true},
    {"show_status_bit2_message", 0x36B522, show_status_bit2_message, true, true, true},
    {"show_status_bit3_message", 0x36B567, show_status_bit3_message, false, true, true},
    {"show_status_bit4_message", 0x36B5B9, show_status_bit4_message, true, true, true},
    {"show_actor_amount_message", 0x36B5FE, show_actor_amount_message, false, true, true},
    {"show_status_bit5_message", 0x36B681, show_status_bit5_message, true, true, true},
    {"show_status_bit6_message", 0x36B6C6, show_status_bit6_message, true, true, true},
    {"show_status_bit7_message", 0x36B70B, show_status_bit7_message, true, true, true},
    {"show_status_inflicted_message", 0x36BC84, show_status_inflicted_message, true, true, true},
    {"show_max_hp_growth_message", 0x36BD14, show_max_hp_growth_message, true, true, true},
    {"show_stat_b_growth_message", 0x36BDB4, show_stat_b_growth_message, true, true, true},
    {"show_stat_e_growth_message", 0x36BE54, show_stat_e_growth_message, true, true, true},
    {"show_stat_d_growth_message", 0x36BEF4, show_stat_d_growth_message, true, true, true},
    {"show_stat_c_growth_message", 0x36BF94, show_stat_c_growth_message, true, true, true},
    {"show_ability_gained_message", 0x36C034, show_ability_gained_message, true, true, true},
    {"show_ability_replaced_message", 0x36C0CF, show_ability_replaced_message, false, true, true},
    {"show_ability_uses_up_message", 0x36C1AD, show_ability_uses_up_message, true, true, true},
    {"show_ability_uses_down_message", 0x36C24C, show_ability_uses_down_message, true, true, true},
    {"show_party_surprise_message", 0x36C2EB, show_party_surprise_message, true, true, true},
    {"show_enemy_surprise_message", 0x36C333, show_enemy_surprise_message, true, true, true},
    {"show_target_group_defeated_message", 0x36C3A9, show_target_group_defeated_message, true, true, true},
    {"show_enemy_defeat_count_message", 0x36C3EB, show_enemy_defeat_count_message, true, true, true},
    {"add_battle_reward_to_money", 0x3638F5, add_battle_reward_to_money, true, true},
    {"add_battle_reward_to_total", 0x36392C, add_battle_reward_to_total, true, true},
    {"format_battle_reward_total", 0x3638A6, format_battle_reward_total, true, true},
    {"battle_table_pointer", 0x364091, battle_table_pointer, true, true},
    {"present_battle_frame_scrolled", 0x3664EF, present_battle_frame_scrolled, true, false, true},
    {"count_enemy_with_equipped_item", 0x36668A, count_enemy_with_equipped_item, true, true},
    {"uncount_party_with_equipped_item", 0x3666AC, uncount_party_with_equipped_item, true, true},
    {"fade_in_battle_scene", 0x3619E1, fade_in_battle_scene, true, true, true},
    {"load_battle_actor_graphics", 0x361C98, load_battle_actor_graphics, true, true},
    {"award_battle_gold", 0x363963, award_battle_gold, true, true, true},
    {"sort_party_after_battle", 0x363F20, sort_party_after_battle, true, true, true},
    {"dissolve_enemy_group", 0x36CD6C, dissolve_enemy_group, true, false, true},
    {"remove_defeated_enemy_group", 0x364E40, remove_defeated_enemy_group, true, true, true},
    {"init_enemy_group_actors", 0x3625A9, init_enemy_group_actors, true, true},
    {"init_battle_actors", 0x3621EF, init_battle_actors, true, true},
    {"setup_battle", 0x361A21, setup_battle, true, true, true},
    {"battle_opening", 0x366507, battle_opening, true, true, true},
    {"place_equipment_column_cursor", 0x363434, place_equipment_column_cursor, true, true},
    {"random_ability_index", 0x3639D2, random_ability_index, true, true},
    {"grow_mutants_after_battle", 0x363B61, grow_mutants_after_battle, true, true, true},
    {"swap_actor_and_target", 0x3640A7, swap_actor_and_target, true, true},
    {"toggle_meat_menu_cursor", 0x364A06, toggle_meat_menu_cursor, true, true},
};
} // namespace
const Routine *routines_bank36(size_t *count) {
    *count = sizeof kBank36 / sizeof kBank36[0];
    return kBank36;
}
} // namespace sawgod::game
