// Registry of the decompiled system code (system.cpp): power-on, the main loops and the interrupt handlers.
#include "game/bank35_world.h"
#include "game/machine.h"
#include "game/system.h"

namespace sawgod::game {
namespace {
// {name, rom, fn, clobbers_flags, near, yields, irq, loop}
//
// Loop entries are compared at frame points; IRQ entries return through IRET.
const Routine kSystem[] = {
    {"irq_achievement_window", 0x3C3C94, irq_achievement_window, false, false, false, true},
    {"irq_battle_entry_scroll", 0x351310, irq_battle_entry_scroll, false, false, false, true},
    {"irq_field_dialogue_split", 0x350122, irq_field_dialogue_split, false, false, false, true},
    {"main", 0x340EE0, main, true, false, true, false, true},
    {"irq_vblank", 0x34114E, irq_vblank, false, false, true, true}, // yields: the map effect's call-out
    // game_main is written in bank35_world.cpp (its call-outs are that file's)
    {"game_main", 0x350564, game_main, true, false, true, false, true},
    {"irq_line_compare", 0x3C4EA0, irq_line_compare, false, false, false, true},
    {"irq_destination_warp", 0x35BC4D, irq_destination_warp, false, false, false, true},
    {"irq_battle_raster_shutter", 0x36C7C2, irq_battle_raster_shutter, false, false, false, true},
    {"irq_battle_raster_sweep", 0x36C681, irq_battle_raster_sweep, false, false, false, true},
    {"irq_battle_raster_wave", 0x364CBC, irq_battle_raster_wave, false, false, false, true},
    {"irq_nuclear_flash", 0x377EFA, irq_nuclear_flash, false, false, false, true},
    {"irq_nuclear_wave", 0x377F50, irq_nuclear_wave, false, false, false, true},
    {"reset", 0x3FC000, reset, true, false, true, false, true}, // yields: continues into the native main loop
    {"boot_color_check", 0x3FC7B0, boot_color_check, false, true},
    {"reset_vector", 0x3FFFF0, reset_vector, true, false, true, false, true},
};
} // namespace
const Routine *routines_system(size_t *count) {
    *count = sizeof kSystem / sizeof kSystem[0];
    return kSystem;
}
} // namespace sawgod::game
