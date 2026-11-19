// Water evidence and latched raster coverage. Nothing here changes game state.
#include "game/light_tags.h"
#include "game/maps.h"
#include "game/tables.h"
#include <algorithm>
#include <map>
#include <set>

namespace sawgod::game {
namespace {
bool water_role(uint16_t flags) {
    const unsigned lo = flags & 255, hi = flags >> 8;
    // Same precedence as attempt_player_step.
    return !(hi & 0xF4) && (lo & 1) && lo != 15;
}
struct Strip {
    int first, count, frames;
    unsigned source;
};
std::vector<Strip> strips(unsigned callback) {
    // Actual source uploads from bank35_world.cpp. Other callbacks remain color/role candidates.
    switch (callback) {
    case 0x957E:
        return {{0, 7, 4, 0x0D0700}};
    case 0x95DB:
        return {{0, 3, 4, 0x0B0000}, {3, 1, 2, 0x0D0000}};
    case 0x9679:
        return {{0, 2, 2, 0x0D0100}};
    case 0x96D8:
        return {{0, 9, 2, 0x0CF580}, {9, 3, 2, 0x0D0300}};
    case 0x9776:
        return {{0, 5, 8, 0x0B0600}, {5, 6, 2, 0x0B2E00}, {11, 1, 2, 0x0D0600}};
    case 0x984B:
        return {{0, 9, 4, 0x0B2E00}};
    case 0x98A8:
        return {{0, 3, 8, 0x0B4000}};
    case 0x9905:
        return {{0, 2, 8, 0x0B6000}};
    case 0x9962:
        return {{0, 4, 8, 0x0B6800}};
    case 0x99BF:
        return {{0, 2, 8, 0x0B8800}};
    case 0x9A1C:
        return {{0, 5, 8, 0x0B0000}};
    case 0x9A79:
        return {{0, 3, 16, 0x0B2800}};
    case 0x9AD6:
        return {{0, 6, 2, 0x0B6D00}};
    case 0x9B33:
        return {{0, 3, 16, 0x0D1500}};
    case 0x9B90:
        return {{0, 2, 4, 0x0CF180}};
    case 0x9BED:
        return {{0, 2, 4, 0x0B4000}};
    case 0x9C4A:
        return {{0, 1, 4, 0x0D0000}};
    case 0x9DFC:
        return {{0, 3, 16, 0x0D2D00}, {3, 7, 8, 0x0D4500}};
    case 0x9E93:
        return {{0, 1, 4, 0x0CEF80}};
    case 0x9EF0:
        return {{0, 1, 32, 0x0B9800}};
    case 0xA0E9:
        return {{0, 6, 2, 0x0DB300}};
    default:
        return {};
    }
}
struct ReviewedWater {
    int tileset, metatile,
        first_row = 0; // rows above first_row are never water (horizon tiles: the sky above)
};
struct WaterColor {
    int tileset;
    unsigned color;
};
#include "game/water_defaults.generated.inc"

bool blue(unsigned c) {
    const unsigned r = c >> 8 & 15, g = c >> 4 & 15, b = c & 15;
    return b >= 5 && b >= r + 2 && b >= g && g >= r;
}
} // namespace

// The reviewed tiles are water. Only the water's own pixels
// get the effect, never the stone, sand or buildings around it. There are no kinds: seas, rivers
// and fountains share one pattern; a tile that never touches other water on any map is a still puddle.
void LightTags::seed_water(const emu::Wsc &w, const DataTables *tables) {
    std::map<int, std::set<int>> reviewed;
    std::map<std::pair<int, int>, int> first_row;
    for (const auto &r : kReviewedWater) {
        reviewed[r.tileset].insert(r.metatile);
        if (r.first_row)
            first_row[{r.tileset, r.metatile}] = r.first_row;
    }
    struct Evidence {
        int wet = 0, total = 0;
        bool touches_water = false;
        std::set<int> maps;
        std::vector<Strip> animation;
    };
    std::map<int, std::map<int, Evidence>> used;
    const auto row = [&](Table t, int id, unsigned base, int stride) {
        const auto *r = tables ? tables->row(t, unsigned(id)) : nullptr;
        return r ? r : w.rom.data() + base + unsigned(id * stride);
    };
    for (int id = 1; id < kMapCount; ++id) {
        if (!map_has_data(id, w, tables))
            continue;
        const int set = row(Table::maps, id, 0x35A830, 18)[0];
        const auto m = extract_map(id, w);
        const unsigned at = 0x3593D5 + unsigned(id) * 2;
        const auto animation = strips(w.rom[at] | w.rom[at + 1] << 8);
        const auto &water = reviewed[set];
        const auto tile = [&](int x, int y) { return m.tiles[m.cells[size_t((y + 8) * 80 + x + 8)]]; };
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const int b = tile(x, y);
                auto &e = used[set][b];
                e.maps.insert(id);
                ++e.total;
                e.wet += water_role(m.type_words[m.attributes[size_t(y * 64 + x)]]);
                if (!animation.empty())
                    e.animation = animation;
                if (water.count(b))
                    for (const auto &[dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
                        if (x + dx >= 0 && x + dx < 64 && y + dy >= 0 && y + dy < 64 &&
                            water.count(tile(x + dx, y + dy)))
                            e.touches_water = true;
            }
    }
    for (const auto &[set, tiles] : used) {
        const auto *ts = row(Table::tilesets, set, 0x35B718, 8);
        const auto word = [&](int i) { return uint16_t(ts[i] | ts[i + 1] << 8); };
        const auto data = [&](uint16_t off) { return w.rom[0x0F0000 + off]; };
        const auto pixels = [&](int b) {
            std::array<unsigned, 256> out{};
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) {
                    const int q = y / 8 * 2 + x / 8;
                    const int pal = data(uint16_t(word(6) + b * 4 + q)) & 3;
                    const unsigned src = unsigned((set >> 2) * 65536) +
                                         uint16_t(word(2) + (b * 4 + q) * 32 + (y % 8) * 4 + x % 8 / 2);
                    const unsigned packed = w.rom[src];
                    const int index = x % 2 ? packed & 15 : packed >> 4;
                    const uint16_t at = uint16_t(word(4) + pal * 32 + index * 2);
                    out[size_t(y * 16 + x)] = data(at) | (data(uint16_t(at + 1)) << 8 & 0xF00);
                }
            return out;
        };
        // Water colors: the set's dark/muddy list, else the colors of its open-water tiles (nearly all blue).
        // Blue stonework, roofs and fountain rims in the same picture use other colors.
        std::set<unsigned> colors;
        for (const auto &c : kWaterColors)
            if (c.tileset == set)
                colors.insert(c.color);
        const bool listed = !colors.empty();
        if (!listed)
            for (const int b : reviewed[set]) {
                const auto px = pixels(b);
                if (std::count_if(px.begin(), px.end(), blue) >= 240)
                    colors.insert(px.begin(), px.end());
            }
        for (const auto &[b, e] : tiles) {
            if (b >= word(0) / 4)
                continue;
            WaterTag tag;
            tag.metatile = b;
            int changing = 0, frame_count = 0;
            const Strip *strip = nullptr;
            for (const auto &a : e.animation)
                if (b >= a.first && b < a.first + a.count)
                    strip = &a;
            if (strip) {
                frame_count = strip->frames;
                const unsigned src = strip->source + unsigned((b - strip->first) * strip->frames * 128);
                for (int f = 1; f < strip->frames; ++f)
                    for (int i = 0; i < 128; ++i)
                        changing += w.rom[src + unsigned(f * 128 + i)] != w.rom[src + unsigned(i)];
            }
            const auto px = pixels(b);
            const auto water = [&](unsigned c) {
                return listed ? colors.count(c) != 0 : blue(c) && (colors.empty() || colors.count(c));
            };
            std::array<bool, 256> on{};
            for (size_t i = 0; i < 256; ++i)
                on[i] = water(px[i]);
            // Small holes inside the water (shallows, foam, floating weed) keep the effect: a blue pixel
            // (any listed color for dark water) with water on two sides (three for dark water) joins it.
            for (int pass = 0; pass < 3; ++pass) {
                auto next = on;
                for (int y = 0; y < 16; ++y)
                    for (int x = 0; x < 16; ++x) {
                        const size_t i = size_t(y * 16 + x);
                        if (on[i] || !(listed || blue(px[i])))
                            continue;
                        int sides = 0;
                        for (const auto &[dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
                            sides += x + dx >= 0 && x + dx < 16 && y + dy >= 0 && y + dy < 16 &&
                                     on[size_t((y + dy) * 16 + x + dx)];
                        next[i] = sides >= (listed ? 3 : 2);
                    }
                on = next;
            }
            // A reviewed horizon tile: the sky above the water line is the water's own blue.
            if (const auto limit = first_row.find({set, b}); limit != first_row.end())
                std::fill_n(on.begin(), size_t(limit->second * 16), false);
            int count = 0;
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    if (on[size_t(y * 16 + x)]) {
                        tag.mask[size_t(y)] |= uint16_t(1u << x);
                        ++count;
                    }
            const bool checked = reviewed[set].count(b) != 0;
            if (!checked && (count < 4 || !e.wet))
                continue;
            tag.enabled = checked && count > 0;
            tag.kind = e.touches_water ? "flowing" : "still";
            tag.confidence = tag.enabled ? 1.0f : 0.3f;
            tag.evidence = std::to_string(e.wet) + "/" + std::to_string(e.total) +
                           " cells have water/vehicle collision role; " + std::to_string(count) +
                           "/256 water pixels; reused in " + std::to_string(e.maps.size()) + " maps";
            if (strip)
                tag.evidence += "; " + std::to_string(frame_count) + "-frame source strip, " +
                                std::to_string(changing) + " changing bytes";
            tag.evidence += checked ? std::string("; reviewed water") +
                                          (e.touches_water ? "" : "; never touches other water: still puddle")
                                    : "; not reviewed as water";
            water_sets_[set].push_back(tag);
        }
    }
}
const WaterTag *LightTags::water(int set, int base, int mirror) const {
    const auto it = water_sets_.find(set);
    if (it == water_sets_.end())
        return nullptr;
    const WaterTag *fallback = nullptr;
    for (const auto &t : it->second)
        if (t.metatile == base) {
            if (t.mirror == mirror)
                return &t;
            if (t.mirror == -1)
                fallback = &t;
        }
    return fallback;
}
bool LightTags::water_at(const emu::FrameLayers &layers, int x, int y, int set) const {
    if (x < 0 || x >= 224 || y < 0 || y >= 144 || !layers.valid[size_t(y)])
        return false;
    const auto &p = layers.planes[emu::FrameLayers::Screen1][size_t(y * 224 + x)];
    if (!(p.flags & emu::RasterPixel::Present) || p.tile < 0x200 || p.tile >= 0x400 || p.palette > 3)
        return false;
    const int mirror =
        (p.flags & emu::RasterPixel::HFlip ? 1 : 0) | (p.flags & emu::RasterPixel::VFlip ? 2 : 0);
    const auto *tag = water(set, (p.tile - 0x200) / 4, mirror);
    if (!tag || !tag->enabled)
        return false;
    const auto &regs = layers.registers[size_t(y)];
    const int q = (p.tile & 3) ^ mirror;
    int tx = q % 2 * 8 + ((x + regs[0x10]) & 7), ty = q / 2 * 8 + ((y + regs[0x11]) & 7);
    if (mirror & 1)
        tx = 15 - tx;
    if (mirror & 2)
        ty = 15 - ty;
    return (tag->mask[size_t(ty)] & (1u << tx)) != 0;
}
std::vector<uint32_t> LightTags::water_pixels(const emu::FrameLayers &layers, const uint32_t *mask,
                                              int set) const {
    std::array<std::array<const WaterTag *, 4>, 128> lookup{};
    for (int b = 0; b < 128; ++b)
        for (int m = 0; m < 4; ++m)
            lookup[size_t(b)][size_t(m)] = water(set, b, m);
    std::vector<uint32_t> out(224 * 144), coverage(out.size());
    std::vector<uint8_t> paint(out.size());
    for (int y = 0; y < 144; ++y) {
        if (!layers.valid[size_t(y)])
            continue;
        const auto &regs = layers.registers[size_t(y)];
        for (int x = 0; x < 224; ++x) {
            const size_t at = size_t(y * 224 + x);
            const auto &p = layers.planes[emu::FrameLayers::Screen1][at];
            if (!(p.flags & emu::RasterPixel::Present) || p.tile < 0x200 || p.tile >= 0x400 || p.palette > 3)
                continue;
            const int mirror =
                (p.flags & emu::RasterPixel::HFlip ? 1 : 0) | (p.flags & emu::RasterPixel::VFlip ? 2 : 0);
            const auto *t = lookup[size_t((p.tile - 0x200) / 4)][size_t(mirror)];
            if (!t || !t->enabled)
                continue;
            const int q = (p.tile & 3) ^ mirror;
            int tx = q % 2 * 8 + ((x + regs[0x10]) & 7), ty = q / 2 * 8 + ((y + regs[0x11]) & 7);
            if (mirror & 1)
                tx = 15 - tx;
            if (mirror & 2)
                ty = 15 - ty;
            paint[at] = (t->mask[size_t(ty)] & (1u << tx)) != 0;
            const unsigned r = p.color >> 8 & 15, g = p.color >> 4 & 15, b = p.color & 15;
            // White/cyan foam inside a tagged water tile is surface for shore distance,
            // though unmasked foam keeps its original pixels. It must not make fake shores.
            if (!paint[at] && !(b >= 5 && b >= g && g >= r))
                continue;
            // Topology includes water under actors/UI: they do not create fake shore edges.
            coverage[at] = uint32_t(t->kind == "still" ? 3 : 1);
        }
    }
    for (int y = 0; y < 144; ++y)
        for (int x = 0; x < 224; ++x) {
            const size_t at = size_t(y * 224 + x);
            if (!paint[at] || mask[at])
                continue;
            int depth = 12;
            for (int d = 1; d < 12 && y - d >= 0; ++d)
                if (!coverage[size_t((y - d) * 224 + x)]) {
                    depth = d;
                    break;
                }
            const auto &regs = layers.registers[size_t(y)];
            out[at] = uint32_t((x + regs[0x10]) & 255) << 16 | uint32_t((y + regs[0x11]) & 255) << 8 |
                      (coverage[at] * 16 + unsigned(depth));
        }
    return out;
}
} // namespace sawgod::game
