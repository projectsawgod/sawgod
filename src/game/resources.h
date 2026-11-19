// Resource regions ("Resources over ROM"; docs/MODS.md): the game's graphics and the data not
// decoded yet, as files the first run extracts from the ROM -- tiles as indexed PNGs (pixel = color index;
// edit them in any image editor), palettes as JSON, the rest as .bin -- that the game then loads instead of
// the ROM's bytes. A region is a run of the ROM the game reads data from (generated from
// assets/manifest/*.yaml); the app decodes each region's file into the data the game reads there, so the
// pictures, palettes and map data come from the files (and a mod's files over them), not the ROM. A file
// must keep its region's size (a tile sheet its tile count); the code that reads it is the original's.
// Data tables and text have their own stores (tables.h, text.h).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sawgod::emu {
class Wsc;
}

namespace sawgod::game {

enum class RegionFormat : uint8_t { Tiles, Palettes, Raw };

struct ResourceRegion {
    const char *file; // e.g. "gfx/font.png", relative to the resources directory (and a mod's root)
    uint32_t rom;     // ROM offset
    uint32_t size;
    RegionFormat format;
    int cols; // tiles: tiles per row in the picture
};

const std::vector<ResourceRegion> &resource_regions();

// A region's file, extracted from the ROM.
std::vector<uint8_t> extract_region(const ResourceRegion &r, const emu::Wsc &w);
// A region's bytes from its file (exactly r.size); false and `error` if the file doesn't decode or doesn't
// fit (a picture of another size, a palette list of another length...).
bool build_region(const ResourceRegion &r, const std::vector<uint8_t> &file, std::vector<uint8_t> *bytes,
                  std::string *error);

// The ROM bytes the game gets from extracted files instead (true per ROM offset): the resource regions, the
// data tables' rows and records, and every string of the text resource. For the --rom-reads report.
std::vector<bool> resource_coverage(const emu::Wsc &w);

} // namespace sawgod::game
