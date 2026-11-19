// Loads editable game-data tables and applies their overrides.
#include "game/tables.h"

#include <algorithm>
#include <charconv>
#include <optional>

#include "game/machine.h"
#include "game/session.h"
#include "game/text.h"

namespace sawgod::game {

namespace {

constexpr TableLayout kLayouts[] = {
    // name     segment offset  rom       stride rows max
    {"items", 0x9000, 0x43E0, 0x3943E0, 8, 256, 256}, // item ids are one byte
    {"items", 0x4000, 0xC127, 0x34C127, 4, 256, 256},
    // Shop 22 is the debug shop (its own list after these rows, tables/debug_shop_items) and the game checks
    // for that id (run_shop_menu), so the ROM's 22 shops are all there is room for.
    {"shops", 0x4000, 0xC527, 0x34C527, 10, 22, 22},
    {"monster_types", 0x9000, 0x7A08, 0x397A08, 1, 200, 200},
    {"monster_types", 0x9000, 0x7AD0, 0x397AD0, 1, 200, 200},
    {"monster_types", 0x9000, 0x76E8, 0x3976E8, 2, 200, 200},
    {"monster_types", 0x9000, 0x7878, 0x397878, 2, 200, 200},
    {"encounters", 0x5000, 0x0C3C, 0x350C3C, 8, 141, 141},
    {"encounter_odds", 0x5000, 0x0C31, 0x350C31, 7, 1, 1},
    {"encounter_odds", 0x5000, 0x0C38, 0x350C38, 4, 1, 1},
    {"maps", 0x5000, 0xA830, 0x35A830, 18, 211, 211},
    {"meat_pairs", 0x9000, 0x7B98, 0x397B98, 29, 25, 25},
    {"meat_results", 0x9000, 0x7E6D, 0x397E6D, 16, 25, 25},
    // A field tileset: its tiles in ROM bank id / 4 (gfx/tilesets_bank_NN.png) at `tiles` (tile_count of
    // them); 4 palettes at `palettes` and the metatile attribute words at `attributes` in bank 0x0F.
    {"tilesets", 0x5000, 0xB718, 0x35B718, 8, 60, 60},
    // Pointer tables: word pointers (stride 2) to variable-length records packed after them
    // (DataTables::set_record). Monster types: the type is a byte, but the per-type tables (names, ranks,
    // meat results) have the ROM's 200 rows and the records start right after the 200 pointers, so 200.
    // name       segment offset  rom       stride rows max  data    region head count_at mask per
    {"monsters", 0x6000, 0xD1FB, 0x36D1FB, 2, 200, 200, 0xD38B, 0xC59, 11, 2, 0xFF, 1}, // head + gear ids
    // Formation ids are a byte (0xD6CD) and the ROM has all 256: none to add, but each can change freely.
    {"formations", 0x8000, 0x11C8, 0x3811C8, 2, 256, 256, 0x13C8, 0x460, 1, 0, 0x0F, 2}, // flags + groups
};
static_assert(std::size(kLayouts) == size_t(Table::count));

// A CSV column: `size` bytes at `col` of a row of table `t` (little-endian), or the bits lo..lo+width-1 of
// one byte; `list`: space-separated byte values filling `size` bytes, 0xFF-padded.
struct Column {
    const char *table;
    const char *name;
    Table t;
    uint8_t col, size, lo, width;
    bool list = false;
};
constexpr Column kColumns[] = {
    {"items", "price", Table::item_prices, 0, 4, 0, 32},
    {"items", "flags", Table::item_records, 0, 1, 0, 8},
    {"items", "target", Table::item_records, 1, 1, 0, 8},
    {"items", "action", Table::item_records, 2, 1, 0, 8},
    {"items", "uses", Table::item_records, 3, 1, 0, 8},
    {"items", "power", Table::item_records, 4, 1, 0, 8},
    {"items", "passive_bits", Table::item_records, 5, 1, 0, 8},
    {"items", "byte6", Table::item_records, 6, 1, 0, 8},
    {"items", "byte7_rest", Table::item_records, 7, 1, 0, 7},
    {"items", "ai_target_flag", Table::item_records, 7, 1, 7, 1},
    {"shops", "items", Table::shops, 0, 10, 0, 8, true},
    {"monster_types", "rank", Table::monster_type_rank, 0, 1, 0, 8},
    {"monster_types", "family", Table::monster_type_family, 0, 1, 0, 8},
    {"monster_types", "graphics", Table::monster_type_graphics, 0, 2, 0, 16},
    {"monster_types", "descriptor", Table::monster_type_descriptor, 0, 2, 0, 16},
    {"encounters", "chance", Table::encounters, 0, 1, 0, 8},
    {"encounters", "formation_50pct", Table::encounters, 1, 1, 0, 8},
    {"encounters", "formation_25pct", Table::encounters, 2, 1, 0, 8},
    {"encounters", "formation_12pct", Table::encounters, 3, 1, 0, 8},
    {"encounters", "formation_6pct", Table::encounters, 4, 1, 0, 8},
    {"encounters", "formation_3pct", Table::encounters, 5, 1, 0, 8},
    {"encounters", "formation_1pct_a", Table::encounters, 6, 1, 0, 8},
    {"encounters", "formation_1pct_b", Table::encounters, 7, 1, 0, 8},
    {"encounter_odds", "threshold1", Table::encounter_thresholds, 0, 1, 0, 8},
    {"encounter_odds", "threshold2", Table::encounter_thresholds, 1, 1, 0, 8},
    {"encounter_odds", "threshold3", Table::encounter_thresholds, 2, 1, 0, 8},
    {"encounter_odds", "threshold4", Table::encounter_thresholds, 3, 1, 0, 8},
    {"encounter_odds", "threshold5", Table::encounter_thresholds, 4, 1, 0, 8},
    {"encounter_odds", "threshold6", Table::encounter_thresholds, 5, 1, 0, 8},
    {"encounter_odds", "threshold7", Table::encounter_thresholds, 6, 1, 0, 8},
    {"encounter_odds", "mode0", Table::encounter_modifiers, 0, 1, 0, 8},
    {"encounter_odds", "mode1", Table::encounter_modifiers, 1, 1, 0, 8},
    {"encounter_odds", "mode2", Table::encounter_modifiers, 2, 1, 0, 8},
    {"encounter_odds", "mode3", Table::encounter_modifiers, 3, 1, 0, 8},
    {"maps", "tileset", Table::maps, 0, 1, 0, 8},
    {"maps", "data_bank", Table::maps, 1, 1, 0, 8},
    {"maps", "data0", Table::maps, 2, 2, 0, 16},
    {"maps", "data1", Table::maps, 4, 2, 0, 16},
    {"maps", "data2", Table::maps, 6, 2, 0, 16},
    {"maps", "data3", Table::maps, 8, 2, 0, 16},
    {"maps", "terrain0_encounters", Table::maps, 10, 1, 0, 8},
    {"maps", "terrain0_variant", Table::maps, 11, 1, 0, 8},
    {"maps", "terrain1_encounters", Table::maps, 12, 1, 0, 8},
    {"maps", "terrain1_variant", Table::maps, 13, 1, 0, 8},
    {"maps", "terrain2_encounters", Table::maps, 14, 1, 0, 8},
    {"maps", "terrain2_variant", Table::maps, 15, 1, 0, 8},
    {"maps", "song", Table::maps, 16, 1, 0, 8},
    {"maps", "running_allowed", Table::maps, 17, 1, 0, 8},
    {"meat_pairs", "f0", Table::meat_pairs, 0, 1, 0, 8},
    {"meat_pairs", "f1", Table::meat_pairs, 1, 1, 0, 8},
    {"meat_pairs", "f2", Table::meat_pairs, 2, 1, 0, 8},
    {"meat_pairs", "f3", Table::meat_pairs, 3, 1, 0, 8},
    {"meat_pairs", "f4", Table::meat_pairs, 4, 1, 0, 8},
    {"meat_pairs", "f5", Table::meat_pairs, 5, 1, 0, 8},
    {"meat_pairs", "f6", Table::meat_pairs, 6, 1, 0, 8},
    {"meat_pairs", "f7", Table::meat_pairs, 7, 1, 0, 8},
    {"meat_pairs", "f8", Table::meat_pairs, 8, 1, 0, 8},
    {"meat_pairs", "f9", Table::meat_pairs, 9, 1, 0, 8},
    {"meat_pairs", "f10", Table::meat_pairs, 10, 1, 0, 8},
    {"meat_pairs", "f11", Table::meat_pairs, 11, 1, 0, 8},
    {"meat_pairs", "f12", Table::meat_pairs, 12, 1, 0, 8},
    {"meat_pairs", "f13", Table::meat_pairs, 13, 1, 0, 8},
    {"meat_pairs", "f14", Table::meat_pairs, 14, 1, 0, 8},
    {"meat_pairs", "f15", Table::meat_pairs, 15, 1, 0, 8},
    {"meat_pairs", "f16", Table::meat_pairs, 16, 1, 0, 8},
    {"meat_pairs", "f17", Table::meat_pairs, 17, 1, 0, 8},
    {"meat_pairs", "f18", Table::meat_pairs, 18, 1, 0, 8},
    {"meat_pairs", "f19", Table::meat_pairs, 19, 1, 0, 8},
    {"meat_pairs", "f20", Table::meat_pairs, 20, 1, 0, 8},
    {"meat_pairs", "f21", Table::meat_pairs, 21, 1, 0, 8},
    {"meat_pairs", "f22", Table::meat_pairs, 22, 1, 0, 8},
    {"meat_pairs", "f23", Table::meat_pairs, 23, 1, 0, 8},
    {"meat_pairs", "f24", Table::meat_pairs, 24, 1, 0, 8},
    {"meat_pairs", "f25", Table::meat_pairs, 25, 1, 0, 8},
    {"meat_pairs", "f26", Table::meat_pairs, 26, 1, 0, 8},
    {"meat_pairs", "f27", Table::meat_pairs, 27, 1, 0, 8},
    {"meat_pairs", "f28", Table::meat_pairs, 28, 1, 0, 8},
    {"tilesets", "tile_count", Table::tilesets, 0, 2, 0, 16},
    {"tilesets", "tiles", Table::tilesets, 2, 2, 0, 16},
    {"tilesets", "palettes", Table::tilesets, 4, 2, 0, 16},
    {"tilesets", "attributes", Table::tilesets, 6, 2, 0, 16},
    {"meat_results", "rank0", Table::meat_results, 0, 1, 0, 8},
    {"meat_results", "rank1", Table::meat_results, 1, 1, 0, 8},
    {"meat_results", "rank2", Table::meat_results, 2, 1, 0, 8},
    {"meat_results", "rank3", Table::meat_results, 3, 1, 0, 8},
    {"meat_results", "rank4", Table::meat_results, 4, 1, 0, 8},
    {"meat_results", "rank5", Table::meat_results, 5, 1, 0, 8},
    {"meat_results", "rank6", Table::meat_results, 6, 1, 0, 8},
    {"meat_results", "rank7", Table::meat_results, 7, 1, 0, 8},
    {"meat_results", "rank8", Table::meat_results, 8, 1, 0, 8},
    {"meat_results", "rank9", Table::meat_results, 9, 1, 0, 8},
    {"meat_results", "rank10", Table::meat_results, 10, 1, 0, 8},
    {"meat_results", "rank11", Table::meat_results, 11, 1, 0, 8},
    {"meat_results", "rank12", Table::meat_results, 12, 1, 0, 8},
    {"meat_results", "rank13", Table::meat_results, 13, 1, 0, 8},
    {"meat_results", "rank14", Table::meat_results, 14, 1, 0, 8},
    {"meat_results", "rank15", Table::meat_results, 15, 1, 0, 8},
};

bool parse_uint(std::string_view s, uint64_t &v) {
    while (!s.empty() && s.front() == ' ')
        s.remove_prefix(1);
    while (!s.empty() && s.back() == ' ')
        s.remove_suffix(1);
    const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    return !s.empty() && ec == std::errc() && p == s.data() + s.size();
}

// Read-only name columns in exported files (for people; the loader ignores them): item names, 8 bytes at
// 9000:4BE0, 0xFF-terminated, padded with full-width spaces.
constexpr uint32_t kItemNames = 0x394BE0;

std::string fixed_name(const emu::Wsc &w, uint32_t rom, unsigned stride, unsigned id) {
    static const Charset charset = Charset::system();
    std::string s;
    for (unsigned i = 0; i < stride; ++i) {
        const uint8_t b = w.rom[(rom + id * stride + i) & (w.rom.size() - 1)];
        if (b == 0xFF)
            break;
        s += charset.decode(b);
    }
    while (s.size() >= 3 && s.compare(s.size() - 3, 3, "\u3000") == 0)
        s.resize(s.size() - 3);
    return s;
}
std::string item_name(const emu::Wsc &w, unsigned id) { return fixed_name(w, kItemNames, 8, id); }
std::string type_name(const emu::Wsc &w, unsigned id) { return fixed_name(w, 0x396FE0, 9, id); } // 9000:6FE0

std::string csv_cell(const std::string &s) {
    if (s.find_first_of(",\"\n") == std::string::npos)
        return s;
    std::string out = "\"";
    for (char c : s)
        out += c == '"' ? std::string("\"\"") : std::string(1, c);
    return out + "\"";
}

} // namespace

const TableLayout &table_layout(Table t) { return kLayouts[size_t(t)]; }

const std::vector<std::string> &table_names() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> n;
        for (const TableLayout &l : kLayouts)
            if (std::find(n.begin(), n.end(), l.name) == n.end())
                n.push_back(l.name);
        return n;
    }();
    return names;
}

std::string export_pointer_csv(std::string_view name, const emu::Wsc &w);

std::string export_csv(std::string_view name, const emu::Wsc &w) {
    if (name == "monsters" || name == "formations")
        return export_pointer_csv(name, w);
    const auto rom = [&](Table t, unsigned id, unsigned i) {
        const TableLayout &l = table_layout(t);
        return w.rom[(l.rom + id * l.stride + i) & (w.rom.size() - 1)];
    };
    std::vector<const Column *> cols;
    for (const Column &c : kColumns)
        if (name == c.table)
            cols.push_back(&c);
    if (cols.empty())
        return {};
    unsigned rows = 0;
    for (const Column *c : cols)
        rows = std::max<unsigned>(rows, table_layout(c->t).rom_rows);
    const bool items = name == "items", types = name == "monster_types";
    std::string out = "id";
    if (items || types)
        out += ",name";
    for (const Column *c : cols)
        out += std::string(",") + c->name + (c->list && name == "shops" ? ",items_names" : "");
    out += '\n';
    for (unsigned id = 0; id < rows; ++id) {
        out += std::to_string(id);
        if (items)
            out += "," + csv_cell(item_name(w, id));
        if (types)
            out += "," + csv_cell(type_name(w, id));
        for (const Column *c : cols) {
            out += ',';
            if (c->list) {
                std::string vals, names;
                for (unsigned i = 0; i < c->size; ++i) {
                    const uint8_t v = rom(c->t, id, c->col + i);
                    if (v == 0xFF)
                        break;
                    vals += (vals.empty() ? "" : " ") + std::to_string(v);
                    names += (names.empty() ? "" : " / ") + item_name(w, v);
                }
                out += vals + "," + csv_cell(names);
                continue;
            }
            uint64_t v = 0;
            for (unsigned i = 0; i < c->size; ++i)
                v |= uint64_t(rom(c->t, id, c->col + i)) << (8 * i);
            if (c->size == 1 && c->width < 8)
                v = (v >> c->lo) & ((1u << c->width) - 1);
            out += std::to_string(v);
        }
        out += '\n';
    }
    return out;
}

bool DataTables::empty() const {
    for (const auto &rows : rows_)
        if (!rows.empty())
            return false;
    return true;
}

void DataTables::set_row(Table t, unsigned id, std::vector<uint8_t> bytes) {
    const TableLayout &l = table_layout(t);
    if (l.per || id >= l.max_rows || bytes.size() != l.stride)
        return; // (pointer tables: set_record)
    auto &rows = rows_[size_t(t)];
    if (rows.size() <= id)
        rows.resize(id + 1);
    rows[id] = std::move(bytes);
}

void DataTables::clear() {
    for (auto &rows : rows_)
        rows.clear();
    for (auto &starts : starts_)
        starts.clear();
}

int compare_pointer_table(const DataTables &d, Table t, const emu::Wsc &w, std::vector<std::string> *diffs);

int DataTables::compare_rom(const emu::Wsc &w, std::vector<std::string> *diffs) const {
    int n = 0;
    for (size_t t = 0; t < size_t(Table::count); ++t) {
        const TableLayout &l = table_layout(Table(t));
        if (l.per) {
            n += compare_pointer_table(*this, Table(t), w, diffs);
            continue;
        }
        for (unsigned id = 0; id < rows_[t].size(); ++id) {
            if (rows_[t][id].empty())
                continue;
            ++n;
            for (unsigned i = 0; i < l.stride; ++i)
                if (id >= l.rom_rows ||
                    rows_[t][id][i] != w.rom[(l.rom + id * l.stride + i) & (w.rom.size() - 1)]) {
                    if (diffs)
                        diffs->push_back(std::string(l.name) + " (rom " + std::to_string(l.rom) + ") row " +
                                         std::to_string(id) + " byte " + std::to_string(i));
                    break;
                }
        }
    }
    return n;
}

bool DataTables::load_csv(std::string_view name, std::string_view csv, const emu::Wsc &w, std::string *error,
                          std::vector<std::string> *warnings, int *changed) {
    for (size_t t = 0; t < size_t(Table::count); ++t)
        if (table_layout(Table(t)).per && name == table_layout(Table(t)).name)
            return load_pointer_csv(Table(t), csv, w, error, warnings, changed);
    const auto lines = parse_csv(csv);
    if (lines.empty()) {
        if (error)
            *error = "empty file";
        return false;
    }
    const auto &header = lines[0];
    int id_col = -1;
    std::vector<std::pair<int, const Column *>> cols; // CSV column -> table column
    for (size_t i = 0; i < header.size(); ++i) {
        if (header[i] == "id")
            id_col = int(i);
        for (const Column &c : kColumns)
            if (name == c.table && header[i] == c.name)
                cols.push_back({int(i), &c});
    }
    if (id_col < 0) {
        if (error)
            *error = "no id column";
        return false;
    }
    auto warn = [&](size_t line, const std::string &what) {
        if (warnings)
            warnings->push_back("line " + std::to_string(line + 1) + ": " + what);
    };
    int count = 0;
    for (size_t li = 1; li < lines.size(); ++li) {
        const auto &cells = lines[li];
        if (cells.size() == 1 && cells[0].empty())
            continue; // blank line
        uint64_t id = 0;
        if (size_t(id_col) >= cells.size() || !parse_uint(cells[id_col], id)) {
            warn(li, "bad id");
            continue;
        }
        // Work on copies of the touched rows (the store's or the ROM's), commit only if the whole line
        // parses.
        std::vector<uint8_t> work[size_t(Table::count)];
        bool ok = true;
        for (const auto &[ci, c] : cols) {
            if (size_t(ci) >= cells.size() || cells[ci].empty())
                continue;
            const TableLayout &l = table_layout(c->t);
            if (id >= l.max_rows) {
                warn(li, "id " + std::to_string(id) + " past the " + std::to_string(l.max_rows) +
                             " rows of " + l.name);
                ok = false;
                break;
            }
            std::vector<uint8_t> &row = work[size_t(c->t)];
            if (row.empty()) {
                if (const uint8_t *cur = this->row(c->t, unsigned(id)))
                    row.assign(cur, cur + l.stride);
                else if (id < l.rom_rows)
                    for (unsigned i = 0; i < l.stride; ++i)
                        row.push_back(w.rom[(l.rom + uint32_t(id) * l.stride + i) &
                                            (w.rom.size() - 1)]); // (ROM bank address)
                else
                    row.assign(l.stride, c->list ? 0xFF : 0);
            }
            if (c->list) {
                std::vector<uint8_t> vals;
                std::string_view s = cells[ci];
                while (ok && !s.empty()) {
                    const size_t sp = s.find(' ');
                    const std::string_view tok = s.substr(0, sp);
                    s = sp == std::string_view::npos ? std::string_view() : s.substr(sp + 1);
                    if (tok.empty())
                        continue;
                    uint64_t v = 0;
                    if (!parse_uint(tok, v) || v > 0xFF) {
                        warn(li, std::string(c->name) + ": bad value '" + std::string(tok) + "'");
                        ok = false;
                    }
                    vals.push_back(uint8_t(v));
                }
                if (ok && vals.size() > c->size) {
                    warn(li, std::string(c->name) + ": more than " + std::to_string(c->size) + " values");
                    ok = false;
                }
                if (!ok)
                    break;
                for (unsigned i = 0; i < c->size; ++i)
                    row[c->col + i] = i < vals.size() ? vals[i] : 0xFF;
                continue;
            }
            uint64_t v = 0;
            if (!parse_uint(cells[ci], v) || (c->width < 64 && v >> c->width)) {
                warn(li, std::string(c->name) + ": bad value '" + cells[ci] + "'");
                ok = false;
                break;
            }
            if (c->size == 1 && c->width < 8) {
                const uint8_t mask = uint8_t(((1u << c->width) - 1) << c->lo);
                row[c->col] = uint8_t((row[c->col] & ~mask) | (uint8_t(v << c->lo) & mask));
            } else {
                for (unsigned i = 0; i < c->size; ++i)
                    row[c->col + i] = uint8_t(v >> (8 * i));
            }
        }
        if (!ok)
            continue;
        bool any = false;
        for (size_t t = 0; t < size_t(Table::count); ++t)
            if (!work[t].empty()) {
                set_row(Table(t), unsigned(id), std::move(work[t]));
                any = true;
            }
        count += any;
    }
    if (changed)
        *changed = count;
    return true;
}

uint8_t table_u8(Machine &m, Table t, unsigned row, unsigned col) {
    const TableLayout &l = table_layout(t);
    if (const Session *s = Session::find(m.wsc()); s && col < l.stride)
        if (const uint8_t *r = s->tables().row(t, row))
            return r[col];
    return m.rd8(l.segment, uint16_t(l.offset + row * l.stride + col));
}

uint8_t table_at(Machine &m, Table t, uint16_t seg, uint16_t off) {
    const TableLayout &l = table_layout(t);
    if (l.per) { // a pointer table: its pointers and records as the store packs them
        if (seg == l.segment)
            if (const Session *s = Session::find(m.wsc()))
                if (const int v = s->tables().pointer_table_byte(t, off); v >= 0)
                    return uint8_t(v);
        return m.rd8(seg, off);
    }
    if (seg == l.segment)
        if (const Session *s = Session::find(m.wsc()); s && s->tables().overridden(t)) {
            const unsigned rel = uint16_t(off - l.offset);
            if (const uint8_t *r = s->tables().row(t, rel / l.stride);
                r && rel < unsigned(l.max_rows) * l.stride)
                return r[rel % l.stride];
        }
    return m.rd8(seg, off);
}

// --- Pointer tables: variable-length records behind a word pointer table (monsters, formations).
//
// The store keeps every row's record as its own byte vector (any length its count byte gives) and packs them
// back to back from the layout's `data` offset, in id order, for the game to read: the pointer table's words
// and the records' bytes at those offsets are served from the store (table_at), so a longer gear list or a
// changed formation needs no ROM space. The ROM's records are packed exactly this way already, so the default
// extract gives the ROM's very pointers and bytes. The packed records must end by offset FFFFh.

namespace {

// Header columns of a record (`col` within the record), as kColumns; the list tail is handled by name.
constexpr Column kRecordColumns[] = {
    {"monsters", "meat_drop", Table::monsters, 0, 1, 0, 3},
    {"monsters", "unknown_rank_bits", Table::monsters, 0, 1, 3, 3},
    {"monsters", "esper_or_mecha", Table::monsters, 0, 1, 6, 1},
    {"monsters", "is_monster_row", Table::monsters, 0, 1, 7, 1},
    {"monsters", "byte1", Table::monsters, 1, 1, 0, 8},
    {"monsters", "gear_count", Table::monsters, 2, 1, 0, 8}, // = the gear list's length (checked, not set)
    {"monsters", "hp", Table::monsters, 3, 2, 0, 16},
    {"monsters", "strength", Table::monsters, 5, 1, 0, 8},
    {"monsters", "defense", Table::monsters, 6, 1, 0, 8},
    {"monsters", "agility", Table::monsters, 7, 1, 0, 8},
    {"monsters", "magic", Table::monsters, 8, 1, 0, 8},
    {"monsters", "gold", Table::monsters, 9, 2, 0, 16},
    {"formations", "cant_run", Table::formations, 0, 1, 4, 1},
};
constexpr unsigned kFormationGroups = 3; // the game keeps 3 (0xD6DB..0xD6E0); the count nibble allows 15

uint64_t get_field(const uint8_t *rec, const Column &c) {
    uint64_t v = 0;
    for (unsigned i = 0; i < c.size; ++i)
        v |= uint64_t(rec[c.col + i]) << (8 * i);
    return c.size == 1 && c.width < 8 ? (v >> c.lo) & ((1u << c.width) - 1) : v;
}

void set_field(uint8_t *rec, const Column &c, uint64_t v) {
    if (c.size == 1 && c.width < 8) {
        const uint8_t mask = uint8_t(((1u << c.width) - 1) << c.lo);
        rec[c.col] = uint8_t((rec[c.col] & ~mask) | (uint8_t(v << c.lo) & mask));
    } else {
        for (unsigned i = 0; i < c.size; ++i)
            rec[c.col + i] = uint8_t(v >> (8 * i));
    }
}

// The space-separated byte list of a cell (false on a bad value).
bool parse_list(std::string_view s, std::vector<uint8_t> &out) {
    out.clear();
    while (!s.empty()) {
        const size_t sp = s.find(' ');
        const std::string_view tok = s.substr(0, sp);
        s = sp == std::string_view::npos ? std::string_view() : s.substr(sp + 1);
        if (tok.empty())
            continue;
        uint64_t v = 0;
        if (!parse_uint(tok, v) || v > 0xFF)
            return false;
        out.push_back(uint8_t(v));
    }
    return true;
}

} // namespace

size_t record_size(const TableLayout &l, const uint8_t *rec) {
    return l.head + size_t(rec[l.count_at] & l.count_mask) * l.per;
}

std::vector<uint8_t> rom_record(Table t, unsigned id, const emu::Wsc &w) {
    const TableLayout &l = table_layout(t);
    const auto at = [&](uint32_t a) { return w.rom[a & (w.rom.size() - 1)]; };
    const uint32_t p = (l.rom & ~0xFFFFu) | uint32_t(at(l.rom + 2 * id) | at(l.rom + 2 * id + 1) << 8);
    std::vector<uint8_t> rec(l.head);
    for (unsigned i = 0; i < l.head; ++i)
        rec[i] = at(p + i);
    rec.resize(record_size(l, rec.data()));
    for (size_t i = l.head; i < rec.size(); ++i)
        rec[i] = at(p + uint32_t(i));
    return rec;
}

std::string export_pointer_csv(std::string_view name, const emu::Wsc &w) {
    const bool monsters = name == "monsters";
    const Table t = monsters ? Table::monsters : Table::formations;
    std::string out = monsters ? "id,name" : "id";
    for (const Column &c : kRecordColumns)
        if (c.t == t)
            out += std::string(",") + c.name;
    if (monsters)
        out += ",gear,gear_names";
    else
        for (unsigned g = 1; g <= kFormationGroups; ++g) {
            const std::string n = "group" + std::to_string(g);
            out += "," + n + "_type," + n + "_name," + n + "_min," + n + "_max";
        }
    out += '\n';
    for (unsigned id = 0; id < table_layout(t).rom_rows; ++id) {
        const std::vector<uint8_t> rec = rom_record(t, id, w);
        out += std::to_string(id);
        if (monsters)
            out += "," + csv_cell(type_name(w, id));
        for (const Column &c : kRecordColumns)
            if (c.t == t)
                out += "," + std::to_string(get_field(rec.data(), c));
        if (monsters) {
            std::string vals, names;
            for (size_t i = table_layout(t).head; i < rec.size(); ++i) {
                vals += (vals.empty() ? "" : " ") + std::to_string(rec[i]);
                names += (i == table_layout(t).head ? "" : " / ") + item_name(w, rec[i]);
            }
            out += "," + vals + "," + csv_cell(names);
        } else {
            const unsigned n = rec[0] & 0xF;
            for (unsigned g = 0; g < kFormationGroups; ++g) {
                if (g >= n) {
                    out += ",,,,";
                    continue;
                }
                const uint8_t type = rec[1 + 2 * g], count = rec[2 + 2 * g];
                out += "," + std::to_string(type) + "," + csv_cell(type_name(w, type)) + "," +
                       std::to_string(count & 0xF) + "," + std::to_string(count >> 4);
            }
        }
        out += '\n';
    }
    return out;
}

void DataTables::pack(Table t) {
    const TableLayout &l = table_layout(t);
    auto &starts = starts_[size_t(t)];
    starts.assign(1, l.data);
    for (const auto &rec : rows_[size_t(t)])
        starts.push_back(starts.back() + uint32_t(rec.size()));
}

bool DataTables::set_record(Table t, unsigned id, std::vector<uint8_t> bytes, const emu::Wsc &w) {
    const TableLayout &l = table_layout(t);
    if (!l.per || id >= l.max_rows || bytes.size() < l.head || bytes.size() != record_size(l, bytes.data()))
        return false;
    auto &rows = rows_[size_t(t)];
    if (rows.empty()) {
        for (unsigned i = 0; i < l.rom_rows; ++i)
            rows.push_back(rom_record(t, i, w));
        pack(t);
    }
    if (rows.size() <= id)
        rows.resize(id + 1, std::vector<uint8_t>(l.head, 0)); // (an empty record: count 0)
    if (starts_[size_t(t)].back() - rows[id].size() + bytes.size() > 0x10000)
        return false;
    rows[id] = std::move(bytes);
    pack(t);
    return true;
}

int DataTables::pointer_table_byte(Table t, uint16_t off) const {
    const auto &rows = rows_[size_t(t)];
    const auto &starts = starts_[size_t(t)];
    if (rows.empty())
        return -1;
    const TableLayout &l = table_layout(t);
    const unsigned rel = uint16_t(off - l.offset);
    if (rel < 2 * rows.size()) { // the pointer table
        const uint32_t p = starts[rel / 2];
        return rel & 1 ? int((p >> 8) & 0xFF) : int(p & 0xFF);
    }
    if (off < l.data || off >= starts.back())
        return -1;
    const size_t k =
        size_t(std::upper_bound(starts.begin(), starts.end(), uint32_t(off)) - starts.begin()) - 1;
    return rows[k][off - starts[k]];
}

int compare_pointer_table(const DataTables &d, Table t, const emu::Wsc &w, std::vector<std::string> *diffs) {
    const TableLayout &l = table_layout(t);
    const auto at = [&](uint32_t a) { return w.rom[a & (w.rom.size() - 1)]; };
    int n = 0;
    for (unsigned id = 0;; ++id) {
        const std::vector<uint8_t> *rec = d.record(t, id);
        if (!rec)
            return n;
        ++n;
        const int ptr = d.pointer_table_byte(t, uint16_t(l.offset + 2 * id)) |
                        d.pointer_table_byte(t, uint16_t(l.offset + 2 * id + 1)) << 8;
        if (id >= l.rom_rows || *rec != rom_record(t, id, w) ||
            ptr != (at(l.rom + 2 * id) | at(l.rom + 2 * id + 1) << 8))
            if (diffs)
                diffs->push_back(std::string(l.name) + " (pointers rom " + std::to_string(l.rom) + ") row " +
                                 std::to_string(id));
    }
}

// Load tables/monsters or tables/formations: rows matched by `id`, only the cells given change (an empty cell
// keeps the store's value, or the ROM's). monsters: `gear` replaces the list (and gear_count with it; a
// gear_count cell must equal the list's length). formations: each groupN_type cell given sets group N (empty:
// no group N; empty groupN_min/max keep the current group N's), then the groups are compacted and counted.
bool DataTables::load_pointer_csv(Table t, std::string_view csv, const emu::Wsc &w, std::string *error,
                                  std::vector<std::string> *warnings, int *changed) {
    const TableLayout &l = table_layout(t);
    const auto lines = parse_csv(csv);
    if (lines.empty()) {
        if (error)
            *error = "empty file";
        return false;
    }
    const auto &header = lines[0];
    int id_col = -1, list_col = -1;
    int group_cols[kFormationGroups][3]; // type, min, max
    for (auto &g : group_cols)
        g[0] = g[1] = g[2] = -1;
    std::vector<std::pair<int, const Column *>> cols;
    for (size_t i = 0; i < header.size(); ++i) {
        const std::string &h = header[i];
        if (h == "id")
            id_col = int(i);
        if (t == Table::monsters && h == "gear")
            list_col = int(i);
        for (unsigned g = 0; g < kFormationGroups && t == Table::formations; ++g)
            for (unsigned k = 0; k < 3; ++k)
                if (h == "group" + std::to_string(g + 1) + (k == 0 ? "_type" : k == 1 ? "_min" : "_max"))
                    group_cols[g][k] = int(i);
        for (const Column &c : kRecordColumns)
            if (c.t == t && h == c.name)
                cols.push_back({int(i), &c});
    }
    if (id_col < 0) {
        if (error)
            *error = "no id column";
        return false;
    }
    auto warn = [&](size_t line, const std::string &what) {
        if (warnings)
            warnings->push_back("line " + std::to_string(line + 1) + ": " + what);
    };
    const auto cell = [](const std::vector<std::string> &cells, int ci) -> std::string_view {
        return ci >= 0 && size_t(ci) < cells.size() ? std::string_view(cells[ci]) : std::string_view();
    };
    int count = 0;
    for (size_t li = 1; li < lines.size(); ++li) {
        const auto &cells = lines[li];
        if (cells.size() == 1 && cells[0].empty())
            continue;
        uint64_t id = 0;
        if (!parse_uint(cell(cells, id_col), id)) {
            warn(li, "bad id");
            continue;
        }
        if (id >= l.max_rows) {
            warn(li, "id " + std::to_string(id) + " past the " + std::to_string(l.max_rows) + " rows of " +
                         l.name);
            continue;
        }
        std::vector<uint8_t> rec;
        if (const std::vector<uint8_t> *cur = record(t, unsigned(id)))
            rec = *cur;
        else
            rec = id < l.rom_rows ? rom_record(t, unsigned(id), w) : std::vector<uint8_t>(l.head, 0);
        bool ok = true;
        std::optional<uint64_t> gear_count;
        for (const auto &[ci, c] : cols) {
            const std::string_view v = cell(cells, ci);
            if (v.empty())
                continue;
            uint64_t x = 0;
            if (!parse_uint(v, x) || (c->width < 64 && x >> c->width)) {
                warn(li, std::string(c->name) + ": bad value '" + std::string(v) + "'");
                ok = false;
                break;
            }
            if (t == Table::monsters && c->col == l.count_at)
                gear_count = x;
            else
                set_field(rec.data(), *c, x);
        }
        if (!ok)
            continue;
        if (t == Table::monsters) {
            std::vector<uint8_t> gear(rec.begin() + l.head, rec.end());
            if (!cell(cells, list_col).empty() && !parse_list(cell(cells, list_col), gear)) {
                warn(li, "gear: bad value '" + std::string(cell(cells, list_col)) + "'");
                continue;
            }
            if (gear.size() > 0xFF || (gear_count && *gear_count != gear.size())) {
                warn(li, "gear_count is " + std::to_string(gear_count.value_or(0)) + " but gear has " +
                             std::to_string(gear.size()) + " ids");
                continue;
            }
            rec.resize(l.head);
            rec[l.count_at] = uint8_t(gear.size());
            rec.insert(rec.end(), gear.begin(), gear.end());
        } else {
            std::vector<std::pair<uint8_t, uint8_t>> cur, groups; // (type, count: min | max << 4)
            for (unsigned g = 0; g < (rec[0] & 0xFu); ++g)
                cur.push_back({rec[1 + 2 * g], rec[2 + 2 * g]});
            for (unsigned g = 0; g < kFormationGroups && ok; ++g) {
                if (group_cols[g][0] < 0) { // not in the file: unchanged
                    if (g < cur.size())
                        groups.push_back(cur[g]);
                    continue;
                }
                const std::string_view type = cell(cells, group_cols[g][0]);
                if (type.empty())
                    continue; // no group g
                const bool have = g < cur.size();
                uint64_t ty = 0, lo = 0, hi = 0;
                const auto num = [&](int k, uint64_t fallback, uint64_t &out) {
                    const std::string_view v = cell(cells, group_cols[g][k]);
                    if (v.empty()) {
                        out = fallback;
                        return have;
                    }
                    return parse_uint(v, out) && out <= 15;
                };
                if (!parse_uint(type, ty) || ty > 0xFF || !num(1, have ? cur[g].second & 0xF : 0, lo) ||
                    !num(2, have ? cur[g].second >> 4 : 0, hi)) {
                    warn(li, "group" + std::to_string(g + 1) + ": type 0-255, min and max 0-15");
                    ok = false;
                }
                groups.push_back({uint8_t(ty), uint8_t(lo | hi << 4)});
            }
            if (!ok)
                continue;
            rec.resize(1);
            rec[0] = uint8_t((rec[0] & ~0x0F) | groups.size());
            for (const auto &[ty, c] : groups) {
                rec.push_back(ty);
                rec.push_back(c);
            }
        }
        if (!set_record(t, unsigned(id), std::move(rec), w)) {
            warn(li, std::string("the ") + l.name + " records would pass offset FFFFh of their segment");
            continue;
        }
        ++count;
    }
    if (changed)
        *changed = count;
    return true;
}

} // namespace sawgod::game
