// Field maps as legible grids (docs/MODS.md "Map format"): each map with data is
// three files the first run extracts --
//   maps/NNN.csv             80 rows of 80 metatile ids: the map with an 8-cell border (cell x, y at
//                            row y + 8, column x + 8), what load_field_map unpacks to 1000:0000
//   maps/NNN_attributes.csv  64 rows of 64 attribute indexes (into the map's tile flags), 1000:2100
//   maps/NNN_metatiles.csv   the 256-entry metatile set: id, tile (the first of its 4 tiles), mirror (0-3)
//   maps/NNN_types.csv       the 32 attribute types the attributes layer indexes: word_flags and byte_flags
//                            (map_tile_word_flags D53B, map_tile_byte_flags D57B; from bank 0x26)
// -- and the app builds the map banks (ROM 0x200000-0x26FFFF) from them: each layer packed with the original
// packer (map_lz.h) at the offsets tables/maps gives, so unedited maps rebuild the banks byte for byte. A map
// whose data no longer fits where it was (or clashes with data another map shares) moves to free space in its
// bank and its tables/maps row follows. The banks' other bytes come from their raw region files.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sawgod::emu {
class Wsc;
}

namespace sawgod::game {

class DataTables;

inline constexpr int kMapCount = 211;
inline constexpr int kMapCellsSide = 80, kMapAttributesSide = 64;

struct MapData {
    std::array<uint8_t, kMapCellsSide * kMapCellsSide> cells{};
    std::array<uint8_t, kMapAttributesSide * kMapAttributesSide> attributes{};
    std::array<uint8_t, 256> tiles{};      // metatile id -> its first tile (data1)
    std::array<uint8_t, 256> mirror{};     // metatile id -> mirror bits 0-3 (data2, 2 bits each)
    std::array<uint16_t, 32> type_words{}; // attribute type -> word flags (bank 0x26, 64 bytes per map)
    std::array<uint8_t, 32> type_bytes{};  // attribute type -> byte flags (bank 0x26 + 0x34C0, 32 per map)
};

// The maps that have data (tables/maps data_bank != 0), from the store's or the ROM's header.
bool map_has_data(int id, const emu::Wsc &w, const DataTables *tables);
MapData extract_map(int id, const emu::Wsc &w);

// The three files (as above), and back. map_from_files fails (with a message) on a bad file.
std::string map_cells_csv(const MapData &m);
std::string map_attributes_csv(const MapData &m);
std::string map_metatiles_csv(const MapData &m);
std::string map_types_csv(const MapData &m);
bool map_from_files(const std::string &cells, const std::string &attributes, const std::string &metatiles,
                    const std::string &types, MapData *out, std::string *error);
// The file names of a map (without the directory): cells, attributes, metatiles, types.
std::array<std::string, 4> map_file_names(int id);

// A picture of the map (its 64 x 64 cells, 16 px each) drawn as load_field_map sets it up: the metatile set's
// tiles from the map's tileset (tables/tilesets), each quadrant's palette from the tileset's attribute words,
// mirroring from the set; the tileset's 4 palettes. An indexed PNG (for people: the game doesn't read it).
std::vector<uint8_t> render_map_png(int id, const MapData &m, const emu::Wsc &w, const DataTables *tables);

// Writes the maps into the ROM image's map banks at their tables/maps offsets (packed); a map that doesn't
// fit or clashes moves to free space (0xFF bytes no original map uses) and `tables` gets its new offsets.
// `maps`: one per map id (nullptr: keep the ROM's). Returns the maps moved; `error` if one found no room (it
// keeps the ROM's data).
int build_map_banks(emu::Wsc &w, DataTables &tables, const std::vector<const MapData *> &maps,
                    std::vector<std::string> *errors);

} // namespace sawgod::game
