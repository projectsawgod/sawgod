// Game data tables as host-side, named resources the native code reads (docs/DATA_TABLES.md "Runtime").
//
// A table is a run of fixed-size rows the game reads at segment:offset + row * stride. The Session owns a
// DataTables store (session.h), filled from the extracted CSVs (tools/assets.py extract: the source of truth,
// byte-identical to the ROM -- sawgod-headless --tables-verify and the difftests with --tables prove it) and
// any mod's CSVs layered on top: no ROM writes, no size limits, rows past the ROM's where the game's id space
// has room (TableLayout::max_rows). A table nothing was loaded for still reads the ROM (transitional).
//
// Natives read through two accessors:
// - table_u8(m, Table::item_records, id, field): a row's byte, by row and column;
// - table_at(m, Table::shops, seg, off): the byte the original reads at seg:off, for decompiled code that
//   still computes the address (the override applies when seg:off falls inside the table's rows).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "emu/wsc.h"

namespace sawgod::game {

class Machine;

enum class Table : uint8_t {
    item_records, // tables/items: flags, target, action, uses, power, passive_bits, byte6, byte7 (9000:43E0)
    item_prices,  // tables/items: price, 32-bit gold (4000:C127)
    shops,        // tables/shops: up to 10 item ids, 0xFF-padded (4000:C527)
    monster_type_rank,       // tables/monster_types: rank (9000:7A08)
    monster_type_family,     // tables/monster_types: family (9000:7AD0)
    monster_type_graphics,   // tables/monster_types: graphics, sprite set/palette selector (9000:76E8, words)
    monster_type_descriptor, // tables/monster_types: descriptor, large graphic and formation layer
                             // (9000:7878)
    encounters,              // tables/encounters: chance + 7 formation slots (5000:0C3C)
    encounter_thresholds,    // tables/encounter_odds: 7 slot thresholds (5000:0C31)
    encounter_modifiers,     // tables/encounter_odds: 4 travel-mode chance modifiers (5000:0C38)
    maps,                    // tables/maps: field map header, 18 bytes (5000:A830)
    meat_pairs,              // tables/meat_pairs: result row for a family pair (9000:7B98)
    meat_results,            // tables/meat_results: monster type by result row + rank (9000:7E6D)
    tilesets, // tables/tilesets: tile count, offsets of tiles, palettes, metatile attributes (5000:B718)
    // Pointer tables (variable-length records, TableLayout::per != 0):
    monsters,   // tables/monsters: hp, stats, gear list, gold... (pointers 6000:D1FB, records from 6000:D38B)
    formations, // tables/formations: flags + up to 3 (type, min/max) groups (pointers 8000:11C8, records
                // 8000:13C8)
    count
};

struct TableLayout {
    const char *name; // the resource (tables/<name>.csv)
    uint16_t segment; // where the game reads it
    uint16_t offset;
    uint32_t rom;      // its ROM address (linear)
    uint16_t stride;   // bytes per row
    uint16_t rom_rows; // rows in the ROM
    uint16_t max_rows; // rows the game's id space can reach (rows past rom_rows exist only in the store)
    // Pointer tables only (per != 0): segment:offset/rom/stride 2 are the word pointer table, and the records
    // it points at start at `data` in the same segment, `region` bytes in the ROM. A record is `head` bytes
    // plus (byte[count_at] & count_mask) * per bytes of list.
    uint16_t data = 0;
    uint16_t region = 0;
    uint8_t head = 0, count_at = 0, count_mask = 0, per = 0;
};

const TableLayout &table_layout(Table t);

// The tables' resource names ("items", "shops"), and one as CSV text extracted from the ROM -- the same
// columns tools/sawgod/tables.py writes, plus read-only names: the first-run extraction (and a loaded file
// equals the ROM: DataTables::compare_rom).
const std::vector<std::string> &table_names();
std::string export_csv(std::string_view name, const emu::Wsc &w);

class DataTables {
  public:
    // A table's row from a loaded override, or nullptr (the game reads the ROM's).
    const uint8_t *row(Table t, unsigned id) const {
        const auto &rows = rows_[size_t(t)];
        return id < rows.size() && !rows[id].empty() ? rows[id].data() : nullptr;
    }
    bool overridden(Table t) const { return !rows_[size_t(t)].empty(); }
    bool empty() const;
    // Replace one row (stride bytes); id < max_rows.
    void set_row(Table t, unsigned id, std::vector<uint8_t> bytes);
    void clear();
    // The loaded rows that differ from the ROM's (one line each into `diffs`); returns the rows compared.
    int compare_rom(const emu::Wsc &w, std::vector<std::string> *diffs) const;

    // Load tables/<name>.csv text (name: "items" or "shops"): rows matched by `id`, only the columns given
    // change (the rest stay the store's, or with no row loaded yet the ROM's from `w`); read-only and unknown
    // columns are ignored. A bad row is
    // skipped with a warning; a file without an `id` column fails and changes nothing.
    bool load_csv(std::string_view name, std::string_view csv, const emu::Wsc &w, std::string *error,
                  std::vector<std::string> *warnings = nullptr, int *changed = nullptr);

    // Pointer tables (variable-length records). A loaded pointer table holds every row (the ROM's fill the
    // rows a CSV doesn't give); the game sees the records packed back to back from `data` in id order, and
    // pointers to them -- for the ROM's own records exactly the ROM's layout and pointers.
    const std::vector<uint8_t> *record(Table t, unsigned id) const {
        const auto &rows = rows_[size_t(t)];
        return id < rows.size() ? &rows[id] : nullptr;
    }
    // Replace one record (its length must match its count byte; the packed records must end by offset
    // FFFFh); the table's other rows come from the ROM in `w` the first time. False if refused.
    bool set_record(Table t, unsigned id, std::vector<uint8_t> bytes, const emu::Wsc &w);
    // The byte the game reads at offset `off` of the table's segment (pointer table or records), or -1 when
    // `off` lies outside them (or the table isn't loaded).
    int pointer_table_byte(Table t, uint16_t off) const;

  private:
    bool load_pointer_csv(Table t, std::string_view csv, const emu::Wsc &w, std::string *error,
                          std::vector<std::string> *warnings, int *changed);
    void pack(Table t);

    std::vector<std::vector<uint8_t>> rows_[size_t(Table::count)];
    std::vector<uint32_t> starts_[size_t(Table::count)]; // pointer tables: each record's offset, then the end
};

// A row's byte (column `col` < stride) as the game sees it: the store's row, else the ROM's.
uint8_t table_u8(Machine &m, Table t, unsigned row, unsigned col);
// The byte the original code reads at seg:off: the store's when seg:off lies in an overridden row.
uint8_t table_at(Machine &m, Table t, uint16_t seg, uint16_t off);
// A pointer table's record length from its bytes (head + list), and the ROM's record for a row.
size_t record_size(const TableLayout &l, const uint8_t *rec);
std::vector<uint8_t> rom_record(Table t, unsigned id, const emu::Wsc &w);
inline uint16_t table_at16(Machine &m, Table t, uint16_t seg, uint16_t off) {
    return uint16_t(table_at(m, t, seg, off) | table_at(m, t, seg, uint16_t(off + 1)) << 8);
}

} // namespace sawgod::game
