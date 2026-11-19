// Decompiled routines from bank 0x35 (field/map/event engine, segment 5000h). See asm/bank_35.s.
#pragma once

#include "game/machine.h"

namespace sawgod::game {

void bitstream_read_bit(Machine &m);              // 0x35180E (near)
void bitstream_read_bits(Machine &m);             // 0x351801 (near)
void map_cell_id_at(Machine &m);                  // 0x351A18
void map_tile_flags_word_at(Machine &m);          // 0x351A41
void map_tile_flags_byte_at(Machine &m);          // 0x351A71
void advance_player_animation_timers(Machine &m); // 0x350163
void draw_player_sprite(Machine &m);              // 0x3501C4
void npc_ai_trigger_move(Machine &m);             // 0x35285A
void advance_and_draw_npcs(Machine &m);           // 0x35297E

void advance_and_draw_npcs_core(Machine &m);
void advance_player_animation_timers_core(Machine &m);
void draw_player_sprite_core(Machine &m);
void map_tile_flags_word_at_core(Machine &m);
void npc_ai_trigger_move_core(Machine &m);
} // namespace sawgod::game
