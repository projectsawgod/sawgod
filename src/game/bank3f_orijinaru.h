// Decompiled routines from bank 0x3F (the ORIJINARU / GB-layout remake mode's own engine, entered by an
// `ljmp` from main and never returning; see asm/bank_3F.s). The bank runs at CS=SS=F000h: unlike bank 0x34
// (which addresses its globals via SS but keeps its own code/data reachable through CS) this bank's own
// working RAM lives at fixed SS-relative offsets used directly by these routines.
// ARCHIVED: ORIJINARU is out of scope (docs/ORIJINARU.md).
#pragma once

#include "game/machine.h"

namespace sawgod::game {

void orij_fill_bytes8(Machine &m);             // 0x3F0E1D (near)
void orij_fill_bytes16(Machine &m);            // 0x3F0E26 (near)
void orij_scan_keypad(Machine &m);             // 0x3F0E4F (near)
void orij_map_keys(Machine &m);                // 0x3F0FBE (near)
void orij_dma_sprite_table(Machine &m);        // 0x3F0C85 (near)
void orij_dma_sprite_table_from(Machine &m);   // 0x3F0C9E (near)
void orij_sprite_quad_positions(Machine &m);   // 0x3F0CD0 (near)
void orij_refresh_cursor_position(Machine &m); // 0x3F0D44 (near)
void orij_random(Machine &m);                  // 0x3F0D70 (near)
void orij_random_range(Machine &m);            // 0x3F0D9A (near)
void orij_read_script_byte(Machine &m);        // 0x3F1120 (near)
void orij_copy_alternating(Machine &m);        // 0x3F23A3 (near)

} // namespace sawgod::game
