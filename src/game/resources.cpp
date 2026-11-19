// Loads extracted game assets and applies resource overrides to a machine.
#include "game/resources.h"

#include "emu/wsc.h"
#include "engine/indexed_png.h"
#include "game/tables.h"
#include "game/text.h"

#include <cstdio>
#include <cstdlib>

namespace sawgod::game {

namespace {

constexpr ResourceRegion kRegions[] = {
#include "game/resource_regions.inc"
};

// Tile pictures' palette: index 0 white .. 15 black (tools/sawgod/formats.py GREY16), as the Python
// extraction writes them.
const std::vector<uint32_t> &grey16() {
    static const std::vector<uint32_t> p = [] {
        std::vector<uint32_t> v;
        for (uint32_t i = 0; i < 16; ++i) {
            const uint32_t g = 255 - i * 17;
            v.push_back(g << 16 | g << 8 | g);
        }
        return v;
    }();
    return p;
}

} // namespace

const std::vector<ResourceRegion> &resource_regions() {
    static const std::vector<ResourceRegion> v(std::begin(kRegions), std::end(kRegions));
    return v;
}

std::vector<uint8_t> extract_region(const ResourceRegion &r, const emu::Wsc &w) {
    const auto at = [&](uint32_t a) { return w.rom[a & (w.rom.size() - 1)]; };
    if (r.format == RegionFormat::Raw) {
        std::vector<uint8_t> out(r.size);
        for (uint32_t i = 0; i < r.size; ++i)
            out[i] = at(r.rom + i);
        return out;
    }
    if (r.format == RegionFormat::Palettes) { // {"palettes": [["#RGB", ...16], ...]}: 12-bit colors
        std::string s = "{\n  \"palettes\": [\n";
        for (uint32_t p = 0; p < r.size / 32; ++p) {
            s += "    [";
            for (uint32_t i = 0; i < 16; ++i) {
                const unsigned v = at(r.rom + p * 32 + i * 2) | at(r.rom + p * 32 + i * 2 + 1) << 8;
                char c[16];
                std::snprintf(c, sizeof c, v < 0x1000 ? "\"#%03X\"" : "\"0x%04X\"", v);
                s += std::string(i ? ", " : "") + c;
            }
            s += p + 1 < r.size / 32 ? "],\n" : "]\n";
        }
        s += "  ]\n}\n";
        return std::vector<uint8_t>(s.begin(), s.end());
    }
    const uint32_t tiles = r.size / 32, cols = uint32_t(r.cols), rows = (tiles + cols - 1) / cols;
    IndexedImage img;
    img.width = int(cols * 8), img.height = int(rows * 8);
    img.pixels.assign(size_t(img.width) * size_t(img.height), 0);
    for (uint32_t t = 0; t < tiles; ++t)
        for (uint32_t y = 0; y < 8; ++y)
            for (uint32_t x = 0; x < 8; ++x) {
                const uint8_t b = at(r.rom + t * 32 + y * 4 + x / 2);
                img.pixels[size_t((t / cols) * 8 + y) * size_t(img.width) + (t % cols) * 8 + x] =
                    uint8_t(x % 2 ? b & 15 : b >> 4);
            }
    return encode_indexed_png(img, grey16());
}

bool build_region(const ResourceRegion &r, const std::vector<uint8_t> &file, std::vector<uint8_t> *bytes,
                  std::string *error) {
    auto fail = [&](const std::string &why) {
        if (error)
            *error = why;
        return false;
    };
    bytes->clear();
    if (r.format == RegionFormat::Raw) {
        if (file.size() != r.size)
            return fail("is " + std::to_string(file.size()) + " bytes, not " + std::to_string(r.size));
        *bytes = file;
        return true;
    }
    if (r.format == RegionFormat::Palettes) { // every "#RGB" / "0xWWWW" string in order
        const std::string s(file.begin(), file.end());
        for (size_t i = 0; (i = s.find('"', i)) != std::string::npos;) {
            const size_t end = s.find('"', i + 1);
            if (end == std::string::npos)
                break;
            const std::string tok = s.substr(i + 1, end - i - 1);
            i = end + 1;
            if (tok.size() > 1 && (tok[0] == '#' || tok.rfind("0x", 0) == 0)) {
                const unsigned v = unsigned(std::strtoul(tok.c_str() + (tok[0] == '#' ? 1 : 2), nullptr, 16));
                bytes->push_back(uint8_t(v));
                bytes->push_back(uint8_t(v >> 8));
            }
        }
        if (bytes->size() != r.size)
            return fail("has " + std::to_string(bytes->size() / 2) + " colors, not " +
                        std::to_string(r.size / 2));
        return true;
    }
    IndexedImage img;
    std::string why;
    if (!decode_indexed_png(file, grey16(), &img, &why))
        return fail(why);
    const uint32_t tiles = r.size / 32, cols = uint32_t(r.cols), rows = (tiles + cols - 1) / cols;
    if (img.width != int(cols * 8) || img.height != int(rows * 8))
        return fail("is " + std::to_string(img.width) + "x" + std::to_string(img.height) + ", not " +
                    std::to_string(cols * 8) + "x" + std::to_string(rows * 8));
    bytes->resize(r.size);
    for (uint32_t t = 0; t < tiles; ++t)
        for (uint32_t y = 0; y < 8; ++y)
            for (uint32_t x = 0; x < 8; x += 2) {
                const size_t p = size_t((t / cols) * 8 + y) * size_t(img.width) + (t % cols) * 8 + x;
                (*bytes)[t * 32 + y * 4 + x / 2] =
                    uint8_t((img.pixels[p] & 15) << 4 | (img.pixels[p + 1] & 15));
            }
    return true;
}

std::vector<bool> resource_coverage(const emu::Wsc &w) {
    std::vector<bool> c(w.rom.size(), false);
    const auto mark = [&](uint32_t from, uint32_t to) {
        for (uint32_t a = from; a < to && a < c.size(); ++a)
            c[a] = true;
    };
    for (const ResourceRegion &r : resource_regions())
        mark(r.rom, r.rom + r.size);
    for (size_t t = 0; t < size_t(Table::count); ++t) {
        const TableLayout &l = table_layout(Table(t));
        mark(l.rom, l.rom + uint32_t(l.stride) * l.rom_rows);
        if (l.per)
            mark(l.rom + uint16_t(l.data - l.offset), l.rom + uint16_t(l.data - l.offset) + l.region);
    }
    for (const TextKeyInfo &k : kTextKeys) {
        uint32_t end = k.string;
        if (k.length)
            end += k.length;
        else if (k.glyphs)
            while (end + 1 < w.rom.size() && !(w.rom[end] == 0xFF && w.rom[end + 1] == 0xFF))
                end += 2;
        else
            while (end < w.rom.size() && w.rom[end] != 0xFF)
                ++end;
        mark(k.string, end + (k.length ? 0 : k.glyphs ? 2 : 1)); // with the terminator
    }
    mark(0x394BE0, 0x394BE0 + 8 * 256);  // item names
    mark(0x396FE0, 0x396FE0 + 9 * 200);  // monster type names
    mark(0x3953E0, 0x3953E0 + 27 * 256); // item descriptions
    for (const DialogueRun &d : kDialogueRuns)
        mark(d.start, d.end);
    return c;
}

} // namespace sawgod::game
