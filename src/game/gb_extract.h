// The GB layer's resources (gb.sgz), extracted from ORIJINARU -- the Game Boy game on the WSC cartridge
// (WSC 0x32xxxx = GB 0x0xxxx) -- on first run (first-run ROM extraction). A port of the GB skin tool's
// extraction: no GB ROM is ever needed.
//
//   gb/sprites.png, gb/sprite_types.csv            the GB field sprites and each WSC character type's sprite
//   gb/hand.png                                   the original 16x16 pointing hand
//   gb/corpse.png                                 room-82 child/diary bodies
//   gb/objects.png, gb/object_sets.csv             boss/tree pieces, vehicles, meat, sphere, crystal, chest,
//   door gb/font.png                                    the GB font and ORIJINARU's window frame for the
//   WSC's codes gb/monsters.png, gb/monster_pics.csv, gb/monster_types.csv   the GB monster pictures
//   gb/effect_actions.csv, effect_rows.csv, effect_N.png   original sprite/raster battle timelines
//   gb/title.png                                   the GB title logo (ORIJINARU's title screen, replayed)
//   gb/NNN_metatiles.png, _lookup.csv, _cells.csv, _hidden.csv   the GB field maps
//
// The field maps' grids and pairings are our own authored data (assets/gb_maps/, no ROM content or
// fingerprints): NNN_strip.csv names each picture of the map's metatile strip by GB graphics set, metatile
// and flip, and only those pictures are decoded from the ROM here. The formats match what game/gfx_swap.h
// reads.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace sawgod::game {

inline constexpr uint32_t kGbBase = 0x320000; // the GB ROM inside the WSC ROM

// A GB metatile: 16 x 16 GB shades (0 white .. 3 black), row by row.
using GbMetatile = std::array<uint8_t, 256>;

// The graphics set a GB room draws with, and a set's 32 metatiles (decoded straight from the ROM).
int gb_room_tileset(const std::vector<uint8_t> &rom, int room);
std::array<GbMetatile, 32> gb_tileset_metatiles(const std::vector<uint8_t> &rom, int set);

// Each file of the GB layer except the field maps; `rom` is the whole WSC ROM. The title needs a short replay
// of ORIJINARU's own title sequence (about 2,000 frames of the emulator), the battle effects a longer one
// (about 8,000 frames, then each effect). `progress` (0..1, optional) is called as that work completes.
std::map<std::string, std::vector<uint8_t>>
extract_gb_shared(const std::vector<uint8_t> &rom, const std::function<void(float)> &progress = {});

// The field maps' files, from the authored data in `assets` (file name -> contents, e.g. "001_strip.csv").
// False and `error` when the data is malformed or names something the ROM lacks.
bool build_gb_field_maps(const std::vector<uint8_t> &rom, const std::map<std::string, std::string> &assets,
                         std::map<std::string, std::vector<uint8_t>> *out, std::string *error);

} // namespace sawgod::game
