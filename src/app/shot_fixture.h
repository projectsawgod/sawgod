// The field fixture of a --shot run (app/main.cpp): a halted field on --shot-map, built without playing to
// it.
#pragma once

#include <cstddef>
#include <cstdint>

#include "app/launch_args.h"
#include "emu/wsc.h"
#include "engine/input.h"
#include "game/gfx_swap.h"

namespace sawgod::app {

// Builds the field at args.shot_map / shot_x / shot_y, `walk` pixels into the review's walk, with the party,
// NPCs and animation the launch asks for, and runs the machine until its picture shows it. False: the map or
// place is not a valid field preview.
bool prepare_shot_field(emu::Wsc &wsc, const LaunchArgs &args, int walk, game::GfxSwap &gfx_swap);

// Game frames run before view `view` of a played run: --frames (plus one in a --shot-run) before the first
// view; after it one per view, or with --shot-display-hz as many as that display's refresh spans.
int shot_view_ticks(const LaunchArgs &args, size_t view, uint64_t frame_ns);

// The menu move of one key letter: u d l r, a (accept), b (back); anything else is no move.
ui::MenuActions shot_menu_actions(char key);

} // namespace sawgod::app
