// Info (Options > Gameplay): extra information drawn over the game, never changing it.
//
// Enemy HP: over each enemy group's picture in battle, centered right above it, a label
// in Chrome's compact numerals on a frameless window fill with "HP/MAX" of the
// group's first living member -- no padding zeros, as wide as the numbers need. The pictures' places follow
// draw_enemy_group_portraits (columns 0Ch / 7, 11h / 4, 0Ch, 14h at row 3; a 6-wide picture one column left,
// a 9-wide one two columns left and two rows up); a group counts as shown while screen 2 still has its
// picture's cells (palette 4 + group).
#pragma once

#include "emu/wsc.h"
#include <string>

namespace sawgod::game {

struct EnemyHpBox {
    int x, y, width;
    std::string text;
    int hp = 0, max = 0;
};

// Every frame (between frames): snapshot the visible groups, or none (`on` false).
void update_enemy_hp_boxes(emu::Wsc &w, bool on);
// After the scene's Chrome pass, in both the app and headless runner. Records fill and text for HD Menus.
void draw_enemy_hp_boxes(emu::Wsc &w, std::vector<uint32_t> &picture, emu::Wsc::ColorMode mode,
                         bool second = false);

} // namespace sawgod::game
