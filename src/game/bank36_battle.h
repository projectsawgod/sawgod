// Decompiled battle/UI helpers from bank 0x36 (segment 6000h).
#pragma once

#include "game/machine.h"

namespace sawgod::game {
void swap_battle_order_entries(Machine &m);
void battle_order_entry_pointer(Machine &m);
void draw_battle_member_panels(Machine &m);
void draw_battle_status_sprites(Machine &m);
void hide_battle_status_sprites(Machine &m);
void blink_battle_command_cursor(Machine &m);
void draw_battle_command_cursor(Machine &m);
void draw_text_alt(Machine &m);
void battle_actor_pointer(Machine &m);
void advance_record_pointer(Machine &m);
void selected_battle_equipment(Machine &m);
void lookup_table_byte(Machine &m);
void draw_battle_marker_sprite(Machine &m);
void draw_battle_cursor_tiles(Machine &m);
void draw_numeral_string(Machine &m);
void draw_glyph_tile(Machine &m);
void compact_battle_digits(Machine &m);

// bank 0x36 battle routines
void battle_actor_status_byte(Machine &m);       // 0x365067
void draw_battle_cursor_tiles_alt(Machine &m);   // 0x3653EF
void clear_battle_ai_flags(Machine &m);          // 0x360A9D (near)
void blank_pad_and_extract_digits(Machine &m);   // 0x3656E8 (near)
void draw_glyph_string(Machine &m);              // 0x365686
void draw_battle_marker_sprite_alt2(Machine &m); // 0x365340
void draw_battle_marker_sprite_alt(Machine &m);  // 0x36530D
// Return whether battle strength, defense, or agility is below the member's persistent stat.
void battle_actor_below_requirement(Machine &m); // 0x362936 (near)
// Test whether actor AL carries item DL in any of its eight battle equipment slots.
void actor_has_equipped_item(Machine &m);  // 0x3666CE (near)
void build_party_name(Machine &m);         // 0x3650FB
void draw_tile_box_border(Machine &m);     // 0x365763
void draw_class_icon_or_label(Machine &m); // 0x3637E0 (near)
void has_table_flag_bit1(Machine &m);      // 0x365010 (near)
void hide_battle_menu_sprites(Machine &m); // 0x3653A8
// Count equipped actions with uses remaining and item flag bit 0 set.
void count_actor_available_items(Machine &m);    // 0x364D87 (near)
void reverse_bits_al(Machine &m);                // 0x36402B (near)
void clear_battle_field_tiles(Machine &m);       // 0x3655A0
void clear_battle_message_box(Machine &m);       // 0x365450
void hide_and_redraw_battle_cursors(Machine &m); // 0x365422
void build_battle_member_display(Machine &m);    // 0x369D8E (near)
void build_battle_member_display_core(Machine &m);

// bank 0x36 battle routines, second pass
// Return actor AL's agility, halved unless status bit 3 is set; SI follows the result.
void battle_actor_stat_byte9_halved(Machine &m); // 0x36502E (near)
// Mark no active party members (04h) or no active enemies with HP remaining (02h).
void mark_battle_end_flags(Machine &m); // 0x360ABC (near)
// Compare action priorities using the original reversed-bit ordering and carry chain.
void battle_priority_less_than(Machine &m);     // 0x36403B (near)
void build_formation_layer_records(Machine &m); // 0x36368F
void build_formation_layer_record(Machine &m);  // 0x3635E1 (near)
void restore_saved_registers(Machine &m);       // 0x36C4D7 (near)
// Recount active enemies with HP remaining into the three type/count group records.
void tally_enemy_type_counts(Machine &m);           // 0x360D6D (near)
void build_battle_member_name_or_icons(Machine &m); // 0x369E1B (near)
void draw_battle_status_icon_tiles(Machine &m);     // 0x365373
void build_icon_list(Machine &m);                   // 0x365212
void format_number(Machine &m);                     // 0x3650C3
void format_number_core(Machine &m);
void build_status_icons(Machine &m);     // 0x3651C2
void party_slot_for_name_id(Machine &m); // 0x3640E6

// bank 0x36 battle-skill-list renderer and its dependencies
void draw_battle_digit_tile(Machine &m); // 0x365638
void draw_battle_digit_row(Machine &m);  // 0x36561D
void draw_battle_skill_list(Machine &m); // 0x36271F (near)

// bank 0x36 battle routines, third pass
// Return target defense, halved when status bit 2 is set.
void battle_actor_stat_byte8_halved(Machine &m); // 0x364FF5 (near)
// Return acting-actor strength, halved when status bit 2 is set.
void battle_actor_stat_byte7_halved(Machine &m); // 0x364FDA (near)
// Return the target's resistance/immunity flags from battle record field 0Bh.
void battle_actor_stat_byte_b(Machine &m);               // 0x364FCC (near)
void log_battle_menu_state(Machine &m);                  // 0x361976 (near)
void copy_bytes_ds_si_to_ss_di(Machine &m);              // 0x364083 (near)
void update_battle_marker_highlight(Machine &m);         // 0x363579 (near)
void draw_tile_box_border_alt(Machine &m);               // 0x3657E1
void selected_equipment_name_or_placeholder(Machine &m); // 0x36306E (near)
void draw_actor_item_list(Machine &m);                   // 0x362EAC (near)

// bank 0x36 routines that wait for frames or call original code (yields)
void flush_battle_frame(Machine &m); // 0x360142
// Apply signed damage to target HP, clamp to zero/max HP, and redraw the HP display.
void battle_meter_step(Machine &m); // 0x364F43 (near)
// Filter and store signed damage (negative heals), then apply it to the selected target's HP.
void set_battle_meter_target(Machine &m);     // 0x364FB1 (near)
void queue_item_count_footer(Machine &m);     // 0x36A3DE (near)
void open_party_member_popup(Machine &m);     // 0x36A557 (near)
void copy_formation_palette(Machine &m);      // 0x36CF27
void copy_formation_palette_next(Machine &m); // 0x36D079
void load_formation_set(Machine &m);          // 0x362E00
void draw_party_status_row(Machine &m);       // 0x363714 (near)

// battle-menu dispatcher and its input handlers
void battle_menu_dispatcher(Machine &m);       // 0x361D60 (near)
void reset_and_reopen_battle_menu(Machine &m); // 0x36198E (near)
void battle_command_menu_input(Machine &m);    // 0x3611FF (near)
void battle_target_menu_input(Machine &m);     // 0x36147B (near)
void battle_strategy_menu_input(Machine &m);   // 0x3616CD (near)

// AI-dispatcher-family leaves
void copy_byte_advance_stride(Machine &m); // 0x363FE1 (near)
void stride_copy_row(Machine &m);          // 0x363FEC (near)
// Zero dead actors' HP and apply poison recovery/damage to actors that are not stoned.
void clear_or_flag_actor_records(Machine &m); // 0x360CB7 (near)
// Compare adjacent active actors' agility and swap their action entries when the next is at least as fast.
void priority_bubble_pass(Machine &m); // 0x36115C (near)
// Randomly swap action-order entries before the agility ordering passes.
void battle_order_shuffle(Machine &m); // 0x36110F (near)
// Count usable equipped actions for actor AL, preserving the original use-count sentinel rules.
void count_action_order_alive(Machine &m);   // 0x361049 (near)
void fade_all_palettes_to_black(Machine &m); // 0x3619B6 (near)
// Choose a target from the action's target class and the actor's confusion flag.
void resolve_ai_action_fallback(Machine &m); // 0x360F17 (near)

// the battle loop, enemy AI and battle input dispatchers
// Resolve a party/group target to a live actor, optionally cycling to another enemy group.
void resolve_action_target(Machine &m); // 0x360B2E (near)
// Advance to the next party member able to choose a command, or close command selection.
void select_next_battle_member(Machine &m);    // 0x364D3E (near)
void position_enemy_target_cursor(Machine &m); // 0x3632EC (near)
void shake_screen_for_party_hit(Machine &m);   // 0x362B62 (near)
// Resolve the target's selected counteraction, then dispatch the acting item's effect handler.
void execute_battle_action(Machine &m);     // 0x360C1C (near)
void draw_battle_member_boxes(Machine &m);  // 0x362987 (near)
void draw_enemy_group_portrait(Machine &m); // 0x36C8FE
void draw_enemy_group_portrait_core(Machine &m);
void draw_enemy_group_portraits(Machine &m); // 0x36C877
// Choose usable enemy skills, spend finite uses, and select targets; honor sleep/paralysis and surprise.
void choose_enemy_actions(Machine &m); // 0x360DE5 (near)
// Execute ordered actions until battle ends, then apply poison and check for a defeated side.
void execute_battle_round(Machine &m); // 0x3605CF (near)
// Prepare enemy actions and party targets, shuffle/sort the order, then execute one round.
void run_battle_round(Machine &m); // 0x3604DA (near)
// Clamp nonpositive target HP to zero, mark death, and count/report the knockout (CF set).
void check_target_knocked_out(Machine &m); // 0x364DCE (near)
void announce_battle_action(Machine &m);   // 0x369E80 (near)
// Roll agility-based evasion, then apply weapon/strength damage minus defense and follow-up effects.
void resolve_physical_attack(Machine &m); // 0x366E36 (near)
// Execute one actor's order entry, handling disabling statuses, item uses, and target scope.
void execute_order_entry(Machine &m); // 0x360615 (near)
// Process battle commands and escape attempts; a failed escape forfeits the party's actions.
void battle_input_step(Machine &m); // 0x360186 (near)
void run_battle(Machine &m);        // 0x360062

// battle message popups and the remaining battle-scene routines
void show_cannot_escape_message(Machine &m);         // 0x36AB9C (near)
void show_enemies_eliminated_message(Machine &m);    // 0x36A15F (near)
void show_attack_missed_message(Machine &m);         // 0x36A36D (near)
void show_enemy_defeat_count_message(Machine &m);    // 0x36C3EB (near)
void show_gold_reward_message(Machine &m);           // 0x36BBBD (near)
void show_party_victory_message(Machine &m);         // 0x36AADC (near)
void show_party_escaped_message(Machine &m);         // 0x36AB1B (near)
void show_party_wiped_out_message(Machine &m);       // 0x36AA95 (near)
void show_target_knocked_out_message(Machine &m);    // 0x36A870 (near)
void show_target_died_message(Machine &m);           // 0x36A8B2 (near)
void show_target_group_defeated_message(Machine &m); // 0x36C3A9 (near)
void show_no_damage_message(Machine &m);             // 0x36A399 (near)
void show_damage_message(Machine &m);                // 0x36A0E9 (near)
void show_actor_confused_message(Machine &m);        // 0x36A9F1 (near)
void show_actor_recovered_message(Machine &m);       // 0x36AA33 (near)
void show_target_description_message(Machine &m);    // 0x36A907 (near)
// Add the 32-bit reward to party gold, capped at 999999.
void add_battle_reward_to_money(Machine &m); // 0x3638F5 (near)
// Accumulate the 32-bit reward total, capped at 999999.
void add_battle_reward_to_total(Machine &m);       // 0x36392C (near)
void format_battle_reward_total(Machine &m);       // 0x3638A6 (near)
void battle_table_pointer(Machine &m);             // 0x364091 (near)
void present_battle_frame_scrolled(Machine &m);    // 0x3664EF
void count_enemy_with_equipped_item(Machine &m);   // 0x36668A (near)
void uncount_party_with_equipped_item(Machine &m); // 0x3666AC (near)
void fade_in_battle_scene(Machine &m);             // 0x3619E1 (near)
void load_battle_actor_graphics(Machine &m);       // 0x361C98 (near)
// Sum each present enemy's gold reward, credit party gold, and display the capped total.
void award_battle_gold(Machine &m); // 0x363963 (near)
// Reorder persistent party records by status while preserving the dialogue leader.
void sort_party_after_battle(Machine &m); // 0x363F20 (near)
void sort_party_after_battle_core(Machine &m);
void dissolve_enemy_group(Machine &m); // 0x36CD6C
// Recount the target's live group and dissolve its portrait when no active members remain.
void remove_defeated_enemy_group(Machine &m); // 0x364E40 (near)
// Expand an enemy type/count pair into battle actors, separating passive flags from usable skills.
void init_enemy_group_actors(Machine &m); // 0x3625A9 (near)
// Build party stats/skills and randomly sized enemy groups from the selected formation.
void init_battle_actors(Machine &m); // 0x3621EF (near)
void setup_battle(Machine &m);       // 0x361A21 (near)
// Use F9h/FAh/FBh equipment and a random roll to choose normal, party-surprise, or enemy-surprise turns.
void battle_opening(Machine &m);                     // 0x366507 (near)
void place_equipment_column_cursor(Machine &m);      // 0x363434 (near)
void show_actor_poison_ended_message(Machine &m);    // 0x36B337 (near)
void show_actor_paralysis_ended_message(Machine &m); // 0x36AC7D (near)
void show_actor_paralyzed_message(Machine &m);       // 0x36B0F7 (near)
void show_actor_awakened_message(Machine &m);        // 0x36B073 (near)
void show_actor_asleep_message(Machine &m);          // 0x36B0B5 (near)
void show_actor_amount_message(Machine &m);          // 0x36B5FE (near)
// Cure poison on a magic-based roll; otherwise inflict maximum HP / 16 damage.
void apply_status_10_effect(Machine &m); // 0x360CE2 (near)
// Roll paralysis recovery using item FEh's field 4.
void try_cure_status_40(Machine &m); // 0x3669E6 (near)
// Roll sleep recovery using item FDh's field 4.
void try_cure_status_80(Machine &m); // 0x366A40 (near)
// Draw an ability index from 0..31; rolls 80h..90h map to index zero, favoring the first ability.
void random_ability_index(Machine &m); // 0x3639D2 (near)
// Roll one growth outcome per living mutant: ability, max HP, stat, ability uses, or no change.
void grow_mutants_after_battle(Machine &m);      // 0x363B61 (near)
void show_max_hp_growth_message(Machine &m);     // 0x36BD14 (near)
void show_stat_b_growth_message(Machine &m);     // 0x36BDB4 (near)
void show_stat_e_growth_message(Machine &m);     // 0x36BE54 (near)
void show_stat_d_growth_message(Machine &m);     // 0x36BEF4 (near)
void show_stat_c_growth_message(Machine &m);     // 0x36BF94 (near)
void show_ability_gained_message(Machine &m);    // 0x36C034 (near)
void show_ability_uses_up_message(Machine &m);   // 0x36C1AD (near)
void show_ability_uses_down_message(Machine &m); // 0x36C24C (near)
void show_ability_replaced_message(Machine &m);  // 0x36C0CF (near)
// Swap acting actor and target, their action/effect IDs, and the target's resolved group.
void swap_actor_and_target(Machine &m);          // 0x3640A7 (near)
void show_target_item_message(Machine &m);       // 0x36AFF9 (near)
void show_actor_item_failed_message(Machine &m); // 0x36A053 (near)
void show_target_attack_message(Machine &m);     // 0x36A969 (near)
void show_no_effect_message(Machine &m);         // 0x36B1BA (near)
void show_status_bit0_message(Machine &m);       // 0x36B48B (near)
void show_status_bit1_message(Machine &m);       // 0x36B4D0 (near)
void show_status_bit2_message(Machine &m);       // 0x36B522 (near)
void show_status_bit3_message(Machine &m);       // 0x36B567 (near)
void show_status_bit4_message(Machine &m);       // 0x36B5B9 (near)
void show_status_bit5_message(Machine &m);       // 0x36B681 (near)
void show_status_bit6_message(Machine &m);       // 0x36B6C6 (near)
void show_status_bit7_message(Machine &m);       // 0x36B70B (near)
void show_status_inflicted_message(Machine &m);  // 0x36BC84 (near)
// Swap actor and target and apply twice the pending damage through the HP meter.
void resolve_drain_hit(Machine &m); // 0x366F2A (near)
// Apply a follow-up status after immunity/resistance checks, counting death or petrification.
void resolve_status_hit(Machine &m); // 0x366FFD (near)
// Apply item-field-4 damage, randomized unless the actor's field-0Ch mask matches the item.
void resolve_fixed_damage_hit(Machine &m);    // 0x367129 (near)
void toggle_meat_menu_cursor(Machine &m);     // 0x364A06 (near)
void show_party_surprise_message(Machine &m); // 0x36C2EB (near)
void show_enemy_surprise_message(Machine &m); // 0x36C333 (near)
void battle_actor_pointer_core(Machine &m);
void draw_battle_cursor_tiles_alt_core(Machine &m);
void draw_battle_cursor_tiles_core(Machine &m, uint16_t dest_base = 0xC000);
void draw_battle_marker_sprite_core(Machine &m);
void lookup_table_byte_core(Machine &m);
} // namespace sawgod::game
