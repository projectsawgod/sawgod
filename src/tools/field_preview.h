// Deterministic field viewport fixture for sawgod-headless. Loads display data only;
// the CPU stays halted while the normal WSC renderer and GfxSwap make both pictures.
#pragma once
#include "emu/wsc.h"
#include "game/maps.h"

namespace sawgod::tools {
bool prepare_field_preview(emu::Wsc &w, int id, const game::MapData &map, int x, int y);
// Adds the native leader sprite to a halted capture, with its own walking pose.
// Advance the native map effect from reset to the requested fixture frame.
void animate_field_preview(emu::Wsc &w, int frame);
void prepare_field_party(emu::Wsc &w, int pose, int type = 0);
void prepare_field_npcs(emu::Wsc &w);
} // namespace sawgod::tools
