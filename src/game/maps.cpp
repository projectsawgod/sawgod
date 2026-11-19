// Extracts field map graphics and loads authored GB map replacements.
#include "game/maps.h"

#include "emu/wsc.h"
#include "engine/indexed_png.h"
#include "game/map_lz.h"
#include "game/tables.h"
#include "game/text.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace sawgod::game {

namespace {

constexpr uint32_t kMapsRom = 0x35A830;                             // tables/maps: 18 bytes per map
constexpr unsigned kCellsBudget = 0xC80, kAttributesBudget = 0x800; // load_field_map's input budgets

struct Header {
    uint8_t bank = 0;
    uint16_t data[4] = {}; // cells stream, metatile tiles, mirror bits, attributes stream
};

std::vector<uint8_t> header_row(int id, const emu::Wsc &w, const DataTables *tables) {
    if (tables)
        if (const uint8_t *r = tables->row(Table::maps, unsigned(id)))
            return std::vector<uint8_t>(r, r + 18);
    std::vector<uint8_t> row(18);
    for (unsigned i = 0; i < 18; ++i)
        row[i] = w.rom[kMapsRom + unsigned(id) * 18 + i];
    return row;
}

Header header(int id, const emu::Wsc &w, const DataTables *tables) {
    const auto r = header_row(id, w, tables);
    Header h;
    h.bank = r[1];
    for (int i = 0; i < 4; ++i)
        h.data[i] = uint16_t(r[size_t(2 + i * 2)] | r[size_t(3 + i * 2)] << 8);
    return h;
}

uint32_t bank_base(uint8_t bank) { return uint32_t(bank & 0x3F) << 16; }

std::vector<uint8_t> bank_bytes(const emu::Wsc &w, uint8_t bank, uint16_t off, size_t n) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i)
        v[i] = w.rom[bank_base(bank) + uint16_t(off + i)];
    return v;
}

std::string grid_csv(const uint8_t *cells, int side) {
    std::string s;
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x)
            s += (x ? "," : "") + std::to_string(cells[y * side + x]);
        s += '\n';
    }
    return s;
}

bool grid_from_csv(const std::string &csv, uint8_t *cells, int side, std::string *error) {
    const auto rows = parse_csv(csv);
    if (int(rows.size()) != side) {
        *error = std::to_string(rows.size()) + " rows, not " + std::to_string(side);
        return false;
    }
    for (int y = 0; y < side; ++y) {
        if (int(rows[size_t(y)].size()) != side) {
            *error = "row " + std::to_string(y + 1) + ": " + std::to_string(rows[size_t(y)].size()) +
                     " values, not " + std::to_string(side);
            return false;
        }
        for (int x = 0; x < side; ++x) {
            char *end = nullptr;
            const std::string &c = rows[size_t(y)][size_t(x)];
            const long v = std::strtol(c.c_str(), &end, 10);
            if (c.empty() || *end || v < 0 || v > 255) {
                *error = "row " + std::to_string(y + 1) + ", column " + std::to_string(x + 1) + ": \"" + c +
                         "\" isn't 0-255";
                return false;
            }
            cells[y * side + x] = uint8_t(v);
        }
    }
    return true;
}

} // namespace

bool map_has_data(int id, const emu::Wsc &w, const DataTables *tables) {
    return id > 0 && id < kMapCount && header(id, w, tables).bank != 0;
}

MapData extract_map(int id, const emu::Wsc &w) {
    const Header h = header(id, w, nullptr);
    MapData m;
    const auto cells = lz_unpack(bank_bytes(w, h.bank, h.data[0], kCellsBudget).data(), kCellsBudget, {});
    const auto attrs =
        lz_unpack(bank_bytes(w, h.bank, h.data[3], kAttributesBudget).data(), kAttributesBudget, {});
    std::copy_n(cells.bytes.begin(), std::min(cells.bytes.size(), m.cells.size()), m.cells.begin());
    std::copy_n(attrs.bytes.begin(), std::min(attrs.bytes.size(), m.attributes.size()), m.attributes.begin());
    const uint32_t types = 0x260000 + uint32_t(uint8_t(id - 1)) * 64,
                   flags = 0x2634C0 + uint32_t(uint8_t(id - 1)) * 32;
    for (int i = 0; i < 32; ++i) {
        m.type_words[size_t(i)] =
            uint16_t(w.rom[types + unsigned(i) * 2] | w.rom[types + unsigned(i) * 2 + 1] << 8);
        m.type_bytes[size_t(i)] = w.rom[flags + unsigned(i)];
    }
    const auto tiles = bank_bytes(w, h.bank, h.data[1], 256), mirror = bank_bytes(w, h.bank, h.data[2], 64);
    for (int i = 0; i < 256; ++i) {
        m.tiles[size_t(i)] = tiles[size_t(i)];
        m.mirror[size_t(i)] = uint8_t(mirror[size_t(i / 4)] >> (6 - (i & 3) * 2) & 3);
    }
    return m;
}

std::string map_cells_csv(const MapData &m) { return grid_csv(m.cells.data(), kMapCellsSide); }
std::string map_attributes_csv(const MapData &m) { return grid_csv(m.attributes.data(), kMapAttributesSide); }

std::string map_metatiles_csv(const MapData &m) {
    std::string s = "id,tile,mirror\n";
    for (int i = 0; i < 256; ++i)
        s += std::to_string(i) + "," + std::to_string(m.tiles[size_t(i)]) + "," +
             std::to_string(m.mirror[size_t(i)]) + "\n";
    return s;
}

std::string map_types_csv(const MapData &m) {
    std::string s = "type,word_flags,byte_flags\n";
    for (int i = 0; i < 32; ++i)
        s += std::to_string(i) + "," + std::to_string(m.type_words[size_t(i)]) + "," +
             std::to_string(m.type_bytes[size_t(i)]) + "\n";
    return s;
}

std::array<std::string, 4> map_file_names(int id) {
    char n[16];
    std::snprintf(n, sizeof n, "maps/%03d", id);
    const std::string b = n;
    return {b + ".csv", b + "_attributes.csv", b + "_metatiles.csv", b + "_types.csv"};
}

bool map_from_files(const std::string &cells, const std::string &attributes, const std::string &metatiles,
                    const std::string &types, MapData *out, std::string *error) {
    std::string why;
    if (!grid_from_csv(cells, out->cells.data(), kMapCellsSide, &why))
        return *error = "cells: " + why, false;
    if (!grid_from_csv(attributes, out->attributes.data(), kMapAttributesSide, &why))
        return *error = "attributes: " + why, false;
    const auto rows = parse_csv(metatiles);
    if (rows.size() != 257 || rows[0].size() < 3 || rows[0][0] != "id" || rows[0][1] != "tile" ||
        rows[0][2] != "mirror")
        return *error = "metatiles: needs id,tile,mirror and 256 rows", false;
    for (int i = 1; i <= 256; ++i) {
        const auto &r = rows[size_t(i)];
        const long id = r.size() >= 3 ? std::strtol(r[0].c_str(), nullptr, 10) : -1,
                   tile = r.size() >= 3 ? std::strtol(r[1].c_str(), nullptr, 10) : -1,
                   mirror = r.size() >= 3 ? std::strtol(r[2].c_str(), nullptr, 10) : -1;
        if (id != i - 1 || tile < 0 || tile > 255 || mirror < 0 || mirror > 3)
            return *error = "metatiles: row " + std::to_string(i + 1) +
                            " (ids 0-255 in order, tile 0-255, mirror 0-3)",
                   false;
        out->tiles[size_t(i - 1)] = uint8_t(tile), out->mirror[size_t(i - 1)] = uint8_t(mirror);
    }
    const auto trows = parse_csv(types);
    if (trows.size() != 33 || trows[0].size() < 3 || trows[0][0] != "type" || trows[0][1] != "word_flags" ||
        trows[0][2] != "byte_flags")
        return *error = "types: needs type,word_flags,byte_flags and 32 rows", false;
    for (int i = 1; i <= 32; ++i) {
        const auto &r = trows[size_t(i)];
        const long t = r.size() >= 3 ? std::strtol(r[0].c_str(), nullptr, 10) : -1,
                   word = r.size() >= 3 ? std::strtol(r[1].c_str(), nullptr, 10) : -1,
                   byte = r.size() >= 3 ? std::strtol(r[2].c_str(), nullptr, 10) : -1;
        if (t != i - 1 || word < 0 || word > 65535 || byte < 0 || byte > 255)
            return *error = "types: row " + std::to_string(i + 1) +
                            " (types 0-31 in order, words 0-65535, bytes 0-255)",
                   false;
        out->type_words[size_t(i - 1)] = uint16_t(word), out->type_bytes[size_t(i - 1)] = uint8_t(byte);
    }
    return true;
}

std::vector<uint8_t> render_map_png(int id, const MapData &m, const emu::Wsc &w, const DataTables *tables) {
    const auto map_row = header_row(id, w, tables);
    const uint8_t tileset = map_row[0];
    std::vector<uint8_t> ts(8);
    const uint8_t *tr = tables ? tables->row(Table::tilesets, tileset) : nullptr;
    for (unsigned i = 0; i < 8; ++i)
        ts[i] = tr ? tr[i] : w.rom[0x35B718 + tileset * 8u + i];
    // load_field_map: the tiles from ROM bank tileset / 4 (port C2 = (tileset >> 2) + 0xC0), the palettes and
    // attribute words from bank 0x0F (C2 = 0xCF)
    const uint32_t bank = uint32_t(tileset >> 2) << 16, data_bank = 0x0F0000;
    const uint16_t tiles = uint16_t(ts[2] | ts[3] << 8), palettes = uint16_t(ts[4] | ts[5] << 8),
                   attributes = uint16_t(ts[6] | ts[7] << 8);
    const auto rom = [&](uint16_t off) { return w.rom[(bank + off) & (w.rom.size() - 1)]; };
    const auto data = [&](uint16_t off) { return w.rom[(data_bank + off) & (w.rom.size() - 1)]; };
    std::vector<uint32_t> colors; // the 4 palettes: 12-bit 0RGB -> 0xRRGGBB
    for (unsigned c = 0; c < 64; ++c) {
        const unsigned v = data(uint16_t(palettes + c * 2)) | data(uint16_t(palettes + c * 2 + 1)) << 8;
        colors.push_back(((v >> 8 & 15) * 17) << 16 | ((v >> 4 & 15) * 17) << 8 | (v & 15) * 17);
    }
    constexpr int kSide = 64 * 16;
    IndexedImage img;
    img.width = img.height = kSide;
    img.pixels.assign(size_t(kSide) * kSide, 0);
    // a metatile's quadrant q: tile id * 4 + q (mirrored metatiles swap quadrants, as the loader's table)
    static constexpr int kQuadrant[4][4] = {{0, 1, 2, 3}, {1, 0, 3, 2}, {2, 3, 0, 1}, {3, 2, 1, 0}};
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const uint8_t t = m.cells[size_t((y + 8) * kMapCellsSide + x + 8)];
            const uint8_t base = m.tiles[t], mirror = m.mirror[t];
            for (int pos = 0; pos < 4;
                 ++pos) { // screen quadrant: 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right
                const int q = kQuadrant[mirror][pos];
                const unsigned tile = base * 4u + unsigned(q);
                const unsigned palette = data(uint16_t(attributes + base * 4 + q)) & 3;
                for (int py = 0; py < 8; ++py)
                    for (int px = 0; px < 8; ++px) {
                        const int sx = mirror & 1 ? 7 - px : px, sy = mirror & 2 ? 7 - py : py; // 1: h, 2: v
                        const uint8_t b = rom(uint16_t(tiles + tile * 32 + sy * 4 + sx / 2));
                        const uint8_t c = uint8_t(sx % 2 ? b & 15 : b >> 4);
                        img.pixels[size_t(y * 16 + (pos / 2) * 8 + py) * kSide +
                                   size_t(x * 16 + (pos % 2) * 8 + px)] = uint8_t(palette * 16 + c);
                    }
            }
        }
    return encode_indexed_png(img, colors);
}

int build_map_banks(emu::Wsc &w, DataTables &tables, const std::vector<const MapData *> &maps,
                    std::vector<std::string> *errors) {
    constexpr uint32_t kFirst = 0x200000, kSize = 7 * 0x10000; // banks 0x20-0x26
    // what the original layout uses (so free space is 0xFF bytes outside it)
    std::vector<bool> original(kSize, false);
    for (int id = 0; id < kMapCount; ++id) {
        if (!map_has_data(id, w, nullptr))
            continue;
        const Header h = header(id, w, nullptr);
        const uint32_t b = bank_base(h.bank) - kFirst;
        if (b >= kSize)
            continue;
        const size_t used0 =
                         lz_unpack(bank_bytes(w, h.bank, h.data[0], kCellsBudget).data(), kCellsBudget, {})
                             .consumed,
                     used3 = lz_unpack(bank_bytes(w, h.bank, h.data[3], kAttributesBudget).data(),
                                       kAttributesBudget, {})
                                 .consumed;
        for (const auto &[off, n] : {std::pair{h.data[0], used0}, std::pair{h.data[1], size_t(256)},
                                     std::pair{h.data[2], size_t(64)}, std::pair{h.data[3], used3}})
            for (size_t i = 0; i < n; ++i)
                original[b + uint16_t(off + i)] = true;
    }
    struct Piece {
        int id, field;
        std::vector<uint8_t> bytes;
        bool same; // equals the ROM's bytes where it goes
    };
    std::vector<Piece> pieces;
    for (int id = 0; id < kMapCount && size_t(id) < maps.size(); ++id) {
        if (!maps[size_t(id)] || !map_has_data(id, w, &tables))
            continue;
        const MapData &m = *maps[size_t(id)];
        const Header h = header(id, w, &tables);
        std::vector<uint8_t> mirror(64, 0);
        for (int i = 0; i < 256; ++i)
            mirror[size_t(i / 4)] |= uint8_t((m.mirror[size_t(i)] & 3) << (6 - (i & 3) * 2));
        const std::vector<uint8_t> fields[4] = {
            lz_pack(std::vector<uint8_t>(m.cells.begin(), m.cells.end())),
            std::vector<uint8_t>(m.tiles.begin(), m.tiles.end()), mirror,
            lz_pack(std::vector<uint8_t>(m.attributes.begin(), m.attributes.end()))};
        for (int f = 0; f < 4; ++f) {
            if ((f == 0 && fields[f].size() > kCellsBudget) ||
                (f == 3 && fields[f].size() > kAttributesBudget)) {
                if (errors)
                    errors->push_back("map " + std::to_string(id) + ": " + (f ? "attributes" : "cells") +
                                      " pack to more than the loader reads (the ROM's data kept)");
                continue;
            }
            pieces.push_back(
                {id, f, fields[f], bank_bytes(w, h.bank, h.data[f], fields[f].size()) == fields[f]});
        }
    }
    // unchanged pieces first, so a changed one never displaces data an unchanged map shares
    std::stable_sort(pieces.begin(), pieces.end(),
                     [](const Piece &a, const Piece &b) { return a.same > b.same; });
    std::vector<int16_t> claim(kSize, -1);
    std::vector<bool> moved(size_t(kMapCount), false);
    auto fits = [&](uint32_t at, const std::vector<uint8_t> &bytes) {
        for (size_t i = 0; i < bytes.size(); ++i)
            if (claim[at + i] >= 0 && claim[at + i] != bytes[i])
                return false;
        return true;
    };
    for (const Piece &p : pieces) {
        const Header h = header(p.id, w, &tables);
        const uint32_t b = bank_base(h.bank) - kFirst;
        uint32_t at = b + h.data[p.field];
        bool ok = h.data[p.field] + p.bytes.size() <= 0x10000 && fits(at, p.bytes);
        if (ok && !p.same) // a changed piece in place may not run into other data
            for (size_t i = 0; i < p.bytes.size() && ok; ++i)
                ok = claim[at + i] >= 0 || w.rom[kFirst + at + i] == 0xFF || original[at + i];
        if (!ok) { // free space in the same bank: 0xFF bytes no original map uses and nothing claimed
            ok = false;
            for (uint32_t o = 1; o + p.bytes.size() <= 0x10000 && !ok; ++o) {
                size_t i = 0;
                for (; i < p.bytes.size(); ++i)
                    if (original[b + o + i] || claim[b + o + i] >= 0 || w.rom[kFirst + b + o + i] != 0xFF)
                        break;
                if (i == p.bytes.size())
                    at = b + o, ok = true;
                else
                    o += uint32_t(i);
            }
            if (!ok) {
                if (errors)
                    errors->push_back("map " + std::to_string(p.id) +
                                      ": no room in its bank (the ROM's data kept)");
                continue;
            }
            std::vector<uint8_t> row = header_row(p.id, w, &tables);
            const uint16_t off = uint16_t(at - b);
            row[size_t(2 + p.field * 2)] = uint8_t(off), row[size_t(3 + p.field * 2)] = uint8_t(off >> 8);
            tables.set_row(Table::maps, unsigned(p.id), row);
            moved[size_t(p.id)] = true;
        }
        for (size_t i = 0; i < p.bytes.size(); ++i)
            claim[at + i] = p.bytes[i];
    }
    for (uint32_t i = 0; i < kSize; ++i)
        if (claim[i] >= 0)
            w.rom[kFirst + i] = uint8_t(claim[i]);
    // the attribute types: fixed places in bank 0x26 (index = map id - 1, as load_field_map reads them)
    for (int id = 0; id < kMapCount && size_t(id) < maps.size(); ++id)
        if (const MapData *m = maps[size_t(id)]) {
            const uint32_t types = 0x260000 + uint32_t(uint8_t(id - 1)) * 64,
                           flags = 0x2634C0 + uint32_t(uint8_t(id - 1)) * 32;
            for (int i = 0; i < 32; ++i) {
                w.rom[types + unsigned(i) * 2] = uint8_t(m->type_words[size_t(i)]);
                w.rom[types + unsigned(i) * 2 + 1] = uint8_t(m->type_words[size_t(i)] >> 8);
                w.rom[flags + unsigned(i)] = m->type_bytes[size_t(i)];
            }
        }
    return int(std::count(moved.begin(), moved.end(), true));
}

} // namespace sawgod::game
