// What the game's field state says about the motion of the frame just completed (app/main.cpp's game_rgb;
// app/display_motion.h interpolates from it).
#pragma once

#include "app/display_motion.h"
#include "emu/wsc.h"

namespace sawgod::app {

// Fills the native walking step of `state`, and with state.eligible gives each NPC in state.actors its
// subpixel position and its step. raster: the machine the field picture was drawn from.
void native_field_motion(const emu::Wsc &raster, MotionState &state);

} // namespace sawgod::app
