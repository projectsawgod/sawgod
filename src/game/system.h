// Decompiled system code: power-on, main and the interrupt handlers (banks 0x34, 0x3C, 0x3F). See system.cpp.
#pragma once

#include "game/machine.h"

namespace sawgod::game {

void main(Machine &m);                      // 0x340EE0 (never returns)
void irq_vblank(Machine &m);                // 0x34114E (interrupt handler)
void irq_achievement_window(Machine &m);    // 0x3C3C94
void irq_battle_entry_scroll(Machine &m);   // 0x351310
void irq_field_dialogue_split(Machine &m);  // 0x350122
void irq_line_compare(Machine &m);          // 0x3C4EA0 (interrupt handler)
void irq_destination_warp(Machine &m);      // 0x35BC4D
void irq_battle_raster_shutter(Machine &m); // 0x36C7C2
void irq_battle_raster_sweep(Machine &m);   // 0x36C681
void irq_battle_raster_wave(Machine &m);    // 0x364CBC
void irq_nuclear_flash(Machine &m);         // 0x377EFA
void irq_nuclear_wave(Machine &m);          // 0x377F50
void reset(Machine &m);                     // 0x3FC000 (never returns: jumps to main)
void boot_color_check(Machine &m);          // 0x3FC7B0 (near)
void reset_vector(Machine &m);              // 0x3FFFF0 (never returns: jumps to reset)

} // namespace sawgod::game
