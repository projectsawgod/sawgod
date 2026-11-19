// A character/monster type's 16x16 menu sprite, battle picture and name, read from the game's data for host
// pictures that show more types at once than the game loads (creation's eight types, the Bestiary's member,
// the meat preview's two forms).
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "game/text_keys.h"

namespace sawgod::game {
class Machine;
class Chrome;

// The sprite draw_creation_type_preview_core loads for a type: four 4bpp tiles and one palette through the
// type's graphics tables (9000:76E8, 3F70, 40B0, 40B2), placed by the preview icon's 2x2 screen-map words.
struct TypeSprite {
    std::array<uint8_t, 128> tiles{};
    std::array<uint16_t, 16> colors{}; // 12-bit
    std::array<uint16_t, 4> cells{};   // tile 0xD4 + n, flips
};
TypeSprite type_sprite(Machine &m, uint8_t type);
// The GB menu sprites (the GB art, or the WSC art in GB greys) sit this many pixels right of the WSC place.
// The WSC art is the GB art colored, so the two overlay pixel for pixel at 0 (measured on the Guild and
// Status/Equip headers); battle cards keep their own 1 px to clear the HP numerals.
inline constexpr int kGbMenuSpriteDx = 0;
// Drawn at (x, y) as hardware palette 8 (GB greys in the GB picture; GB x + kGbMenuSpriteDx),
// `fade` steps darker per channel (the native palette fade).
void draw_type_sprite(Chrome &c, const TypeSprite &s, int x, int y, bool second, int fade = 0);
// A monster type's battle picture (the large graphic creation_large_graphic_core draws): in the WSC picture
// through the type's descriptor (9000:7878) and the picture directory at E000, as tiles, screen-map words and
// colors; in the GB picture the active GB swap's portrait in the GB greys. Sizes in pixels; empty: none.
struct MonsterPicture {
    int width = 0, height = 0;
    std::vector<uint8_t> tiles;  // 32 bytes per tile, packed 4bpp
    std::vector<uint16_t> cells; // per 8x8 cell, row by row: tile index and flips
    std::array<uint16_t, 16> colors{};
    int palette = 11; // the hardware palette it shows as (GB greys for 8-15 in the GB picture)
};
MonsterPicture monster_picture(Machine &m, uint8_t type, bool second);
void draw_monster_picture(Chrome &c, const MonsterPicture &p, int x, int y);
// The type's name: the loaded language's, else the ROM record's (trailing blanks dropped).
std::u32string type_name(Machine &m, uint8_t type);
// A native string a host picture shows, in the loaded language when it has the key (no recorder side effect).
std::u32string native_text(Machine &m, TextKey key);
// `length` charset codes at segment:offset, up to 0xFF, as text (trailing blanks dropped).
std::u32string decoded_text(Machine &m, uint16_t segment, uint16_t offset, int length);
} // namespace sawgod::game
