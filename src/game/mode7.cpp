// Builds perspective field-map presentation from the current map layers.
#include "game/mode7.h"
#include "engine/presentation_diagnostics.h"
#include "game/game_config.h"
#include "game/gfx_swap.h"
#include "game/maps.h"
#include "game/presentation_ownership.h"
#include "game/session.h"
#include "game/terrain_veil.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <tuple>

namespace sawgod::game {
namespace {
// World 4's ruined city blocks stand as raised slabs (on 136 and its later copy, map 24).
bool is_ruined_block(const Landmark &e) {
    return GameConfig::world_of(e.map) == 136 && e.id.find("-ruined-block-") != std::string::npos;
}
constexpr float kPi = 3.14159265358979323846f;
// World pixel y of the authored top border (loaded grid row 2); see World::extend.
constexpr int kMode7AuthoredTop = -96;
uint32_t rgb(uint16_t c) {
    return uint32_t((c >> 8 & 15) * 17 << 16 | (c >> 4 & 15) * 17 << 8 | (c & 15) * 17);
}
uint16_t ram16(const emu::Wsc &w, int at) { return uint16_t(w.ram[size_t(at)] | w.ram[size_t(at + 1)] << 8); }
struct World {
    emu::Wsc &w;
    const LightTags &tags;
    int set, bank;
    size_t sram_mask;
    std::array<std::array<uint32_t, 16>, 16> colors{};
    std::map<int, std::array<uint16_t, 4>> words;
    std::map<int, int> repair;
    explicit World(emu::Wsc &machine, const LightTags &materials, int tileset)
        : w(machine), tags(materials), set(tileset), bank(int(w.io[0xC1]) << 16),
          sram_mask(std::has_single_bit(w.sram.size()) ? w.sram.size() - 1 : 0),
          words(GfxSwap::field_world_words(w)) {
        for (int p = 0; p < 16; ++p)
            for (int i = 0; i < 16; ++i)
                colors[size_t(p)][size_t(i)] = rgb(w.presentation_palette_color(p, i, false, false));
    }
    // Beyond the authored map, repeat its outermost two border cells outward, the same rule
    // as extend_edges in mode7.frag.hlsl. Two cells keep the borders' 2x2 patterns (checkerboards, paired
    // mountains). The loaded 80x80 grid spans -128..1151, but its top two rows are cell 00 filler on every
    // map, never on the WSC's screen; the border starts at row 2.
    static int extend(int p, int first) {
        if (p < first)
            return first + ((p - first) % 32 + 32) % 32;
        if (p > 1151)
            return 1120 + (p - 1120) % 32;
        return p;
    }
    static int extend_x(int x) { return extend(x, -128); }
    static int extend_y(int y) { return extend(y, kMode7AuthoredTop); }
    uint8_t data(int at) const {
        const size_t address = size_t(bank) + size_t(at);
        // Terrain collection makes thousands of wrapped reads. The cartridge's power-of-two SRAM
        // needs no integer division; synthetic/nonstandard sizes retain the same modular wrapping.
        return w.sram[sram_mask ? address & sram_mask : address % w.sram.size()];
    }
    struct Pixel {
        uint32_t color;
        int tile, mirror, palette, x, y;
    };
    Pixel sample(float x, float y, bool ground = true, int id_override = -1) const {
        const int ix = extend_x(int(std::floor(x)));
        const int iy = extend_y(int(std::floor(y)));
        const int bx = ix + 128, by = iy + 128;
        const int key = (by / 16) * 80 + bx / 16;
        const auto patched = ground ? repair.find(key) : repair.end();
        const int id = id_override >= 0 ? id_override : patched == repair.end() ? data(key) : patched->second;
        const int at = 0x1900 + id * 8 + (by % 16 / 8) * 4 + (bx % 16 / 8) * 2;
        const auto live = words.find((by / 16) * 80 + bx / 16);
        const int quadrant = (by % 16 / 8) * 2 + bx % 16 / 8;
        const int word = live == words.end() || patched != repair.end() || id_override >= 0
                             ? (data(at) | data(at + 1) << 8)
                             : live->second[size_t(quadrant)];
        const int tile = (word & 511) | (word & 0x2000 ? 512 : 0);
        const int mirror = word >> 14;
        const int px = mirror & 1 ? 7 - (bx & 7) : bx & 7;
        const int py = mirror & 2 ? 7 - (by & 7) : by & 7;
        const int byte = w.ram[size_t((tile < 512 ? 0x4000 : 0x8000) + (tile & 511) * 32 + py * 4 + px / 2)];
        const int index = px & 1 ? byte & 15 : byte >> 4;
        return {colors[size_t(word >> 9 & 15)][size_t(index)], tile, mirror, word >> 9 & 15, ix, iy};
    }
    bool foreground(const Pixel &p) const {
        const int x = p.x >= 0 ? p.x / 16 : -1, y = p.y >= 0 ? p.y / 16 : -1;
        return !repair.count(((p.y + 128) / 16) * 80 + (p.x + 128) / 16) && x >= 0 && x < 64 && y > 0 &&
               y < 63 && (w.ram[size_t(0xD57B + data(0x2100 + y * 64 + x))] & 0x80);
    }
    const WaterTag *water(const Pixel &p) const {
        if (p.tile < 512)
            return nullptr;
        const auto *tag = tags.water(set, (p.tile - 512) / 4, p.mirror);
        if (!tag || !tag->enabled)
            return nullptr;
        int x = (p.tile & 1) * 8 + ((p.x + 128) & 7);
        int y = (p.tile & 2) * 4 + ((p.y + 128) & 7);
        // Quadrant ids are source quadrants, already permuted by the loaded word.
        if (p.mirror & 1)
            x = (p.tile & 1) * 8 + 7 - ((p.x + 128) & 7);
        if (p.mirror & 2)
            y = (p.tile & 2) * 4 + 7 - ((p.y + 128) & 7);
        return tag->mask[size_t(y)] & (1u << x) ? tag : nullptr;
    }
};
struct Card {
    float x, y;
    int width, height, bottom;
    bool front, landmark;
    std::vector<uint32_t> pixels;
    std::vector<uint8_t> covered;
    const Landmark *entry = nullptr;
    bool slab = false;
    int extra = 0, shaft_tip = 0;
};
struct LandmarkBuild {
    std::vector<Card> cards;
    std::vector<const Landmark *> standing;
};
LandmarkBuild build_landmarks(World &world, int map, const LandmarkTags &table, const Mode7Camera &camera,
                              float left, float top, bool cull) {
    const int set = world.set;
    LandmarkBuild result;
    auto &cards = result.cards;
    auto &standing = result.standing;
    const auto key = [](int x, int y) { return (y + 8) * 80 + x + 8; };
    // Guard every cell against the live atlas: moved/changed mod art never leaves stale cutouts.
    for (const auto &e : table.entries()) {
        if (!e.enabled || e.map != map || e.tileset != set)
            continue;
        bool valid = true;
        for (const auto &c : e.cells) {
            const auto p = world.sample(float(c.x * 16), float(c.y * 16), false);
            valid &= p.tile >= 512 && (p.tile - 512) / 4 == c.metatile && p.mirror == c.mirror;
        }
        if (valid)
            standing.push_back(&e);
    }
    std::map<int, bool> footprint;
    for (const auto *e : standing)
        for (const auto &c : e->cells)
            footprint[key(c.x, c.y)] = true;
    const auto plain = [&](int x, int y) {
        if (x < 0 || x >= 64 || y < 0 || y >= 64 || footprint.count(key(x, y)))
            return false;
        const auto p = world.sample(float(x * 16), float(y * 16), false);
        return p.tile >= 512 && table.plain(map, set, (p.tile - 512) / 4);
    };
    const auto winner = [](const std::map<int, int> &counts) {
        int id = -1, count = -1;
        for (const auto &[tile, n] : counts)
            if (n > count) {
                id = tile;
                count = n;
            } // stable lower-local-ID tie break
        return id;
    };
    for (const auto *e : standing) {
        std::map<int, int> surrounding;
        int minx = 64, maxx = 0, miny = 64;
        for (const auto &c : e->cells) {
            minx = std::min(minx, c.x);
            maxx = std::max(maxx, c.x);
            miny = std::min(miny, c.y);
            for (const auto &[dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
                if (plain(c.x + dx, c.y + dy))
                    ++surrounding[world.data(key(c.x + dx, c.y + dy))];
        }
        // Interiors have no exterior neighbor. Use the footprint's modal plain perimeter;
        // a forest-enclosed footprint searches outward along orthogonal rays to existing plain art.
        for (int distance = 2; surrounding.empty() && distance < 64; ++distance)
            for (const auto &c : e->cells)
                for (const auto &[dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
                    if (plain(c.x + dx * distance, c.y + dy * distance))
                        ++surrounding[world.data(key(c.x + dx * distance, c.y + dy * distance))];
        if (surrounding.empty())
            continue; // no invented floor when a mod removes every plain tile
        for (const auto &c : e->cells) {
            std::map<int, int> neighbors;
            for (const auto &[dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
                if (plain(c.x + dx, c.y + dy))
                    ++neighbors[world.data(key(c.x + dx, c.y + dy))];
            world.repair[key(c.x, c.y)] = winner(neighbors.empty() ? surrounding : neighbors);
        }
        const int width = (maxx - minx + 1) * 16, painted = (e->base_row - miny) * 16;
        const bool slab = is_ruined_block(*e);
        const bool endless = e->extra_shaft_rows == -1;
        const auto base = camera.project(float((minx + maxx + 1) * 8) - left, float(e->base_row * 16) - top);
        // Enough stretched painted art to extend above the viewport at every camera/scroll position.
        const int extra = endless && !cull ? 0
                          : endless ? std::max(16, int(std::ceil((base[1] + 160) / std::max(0.1f, base[2]))))
                                    : int(std::lround(e->extra_shaft_rows * 16));
        const int height = painted + extra;
        const float wx = float((minx + maxx + 1) * 8) - left, wy = float(e->base_row * 16) - top;
        const auto projected = camera.project(wx, wy);
        // Still repair offscreen art, but do not decode distant invisible cards.
        if (cull && (projected[2] <= 0 || projected[0] + width * projected[2] / 2 < 0 ||
                     projected[0] - width * projected[2] / 2 > 224 || (projected[1] < 0 && !slab) ||
                     projected[1] - height * projected[2] > 144))
            continue;
        Card card{wx,
                  wy,
                  width,
                  height,
                  height,
                  true,
                  true,
                  std::vector<uint32_t>(size_t(width * height)),
                  std::vector<uint8_t>(size_t(width * height))};
        card.entry = e;
        card.slab = slab;
        card.extra = extra;
        card.shaft_tip = e->shaft_row * 16;
        std::vector<uint8_t> background(size_t(width * painted), 1);
        std::vector<uint32_t> source(size_t(width * painted));
        std::set<uint32_t> extra_floor;
        for (int graphic : e->cutout_ground_metatiles)
            for (int id = 0; id < 256; ++id) {
                const int at = 0x1900 + id * 8;
                const int word = world.data(at) | world.data(at + 1) << 8;
                if (!(word & 0x2000) || (word & 511) / 4 != graphic)
                    continue;
                for (int y = 0; y < 16; ++y)
                    for (int x = 0; x < 16; ++x)
                        extra_floor.insert(world.sample(float(x), float(y), false, id).color);
                break;
            }
        for (const auto &c : e->cells) {
            std::set<uint32_t> floor_colors = extra_floor;
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    floor_colors.insert(world.sample(float(c.x * 16 + x), float(c.y * 16 + y)).color);
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) {
                    const auto pixel = world.sample(float(c.x * 16 + x), float(c.y * 16 + y), false);
                    const size_t at = size_t(((c.y - miny) * 16 + y) * width + (c.x - minx) * 16 + x);
                    source[at] = pixel.color;
                    background[at] = floor_colors.count(pixel.color) ? 1 : 0;
                    card.covered[at] = 1;
                }
        }
        // Remove only edge-connected colors present in the actual repaired floor. Enclosed details stay.
        std::vector<int> flood;
        std::vector<uint8_t> removed(source.size());
        const auto seed = [&](int at) {
            if (!removed[size_t(at)] && (background[size_t(at)] || !card.covered[size_t(at)])) {
                removed[size_t(at)] = 1;
                flood.push_back(at);
            }
        };
        for (int x = 0; x < width; ++x) {
            seed(x);
            seed((painted - 1) * width + x);
        }
        for (int y = 0; y < painted; ++y) {
            seed(y * width);
            seed(y * width + width - 1);
        }
        for (size_t i = 0; i < flood.size(); ++i) {
            const int at = flood[i], x = at % width, y = at / width;
            if (x)
                seed(at - 1);
            if (x + 1 < width)
                seed(at + 1);
            if (y)
                seed(at - width);
            if (y + 1 < painted)
                seed(at + width);
        }
        const auto source_covered = card.covered;
        for (int y = 0; y < height; ++y) {
            // Taller: the whole painted column stretched to the new height (design decision: stretch, never
            // repeat rows). Endless is just tall enough that the tip stays above the top of the screen.
            const int sy = int(int64_t(y) * painted / height);
            for (int x = 0; x < width; ++x) {
                const size_t from = size_t(sy * width + x), to = size_t(y * width + x);
                card.pixels[to] = source[from];
                card.covered[to] = !removed[from] && source_covered[from];
            }
        }
        // The metatile base can contain transparent ground rows below the visible
        // cutout. Rotate around the visible bottom edge, not that padded rectangle:
        // otherwise the last painted row rises above its own ground contact as tilt grows.
        int padding = 0;
        while (padding < card.height &&
               std::none_of(card.covered.begin() + (card.height - padding - 1) * card.width,
                            card.covered.begin() + (card.height - padding) * card.width,
                            [](uint8_t covered) { return covered != 0; }))
            ++padding;
        if (padding == card.height)
            continue;
        card.y -= float(padding);
        card.height -= padding;
        card.bottom -= padding;
        card.pixels.resize(size_t(card.width * card.height));
        card.covered.resize(size_t(card.width * card.height));
        if (endless && !cull)
            card.extra = -1; // stretch the cached painted art at draw time
        cards.push_back(std::move(card));
    }
    return result;
}
uint32_t average(const std::array<uint32_t, 4> &pixels) {
    uint32_t out = 0;
    for (unsigned shift : {0u, 8u, 16u}) {
        unsigned sum = 0;
        for (auto p : pixels)
            sum += p >> shift & 255;
        out |= ((sum + 2) / 4) << shift;
    }
    return out;
}
} // namespace
std::array<float, 2> Mode7Camera::ground(float x, float y) const { return ground_at_height(x, y, 0); }
std::array<float, 2> Mode7Camera::ground_at_height(float x, float y, float height) const {
    if (pixel_scale > 0)
        return {origin[0] + x * output_scale[0] / pixel_scale,
                origin[1] + y * output_scale[1] / pixel_scale + height};
    const float angle = tilt * kPi / 180, f = 224 / (2 * std::tan(lens * kPi / 360));
    const float v = y - 79;
    const float dy = (v * (f * distance - height * std::cos(angle)) + height * f * std::sin(angle)) /
                     (f * std::cos(angle) + v * std::sin(angle));
    return {112 + (x - 112) * (f * distance - dy * std::sin(angle) - height * std::cos(angle)) / f, 79 + dy};
}
std::array<float, 3> Mode7Camera::project(float x, float y) const { return elevated(x, y, 0); }
std::array<float, 3> Mode7Camera::elevated(float x, float y, float height) const {
    if (pixel_scale > 0)
        return {(x - origin[0]) * pixel_scale / output_scale[0],
                (y - origin[1] - height) * pixel_scale / output_scale[1], pixel_scale / output_scale[0]};
    const float angle = tilt * kPi / 180, f = 224 / (2 * std::tan(lens * kPi / 360));
    const float scale = f / (f * distance - (y - 79) * std::sin(angle) - height * std::cos(angle));
    return {112 + (x - 112) * scale, 79 + ((y - 79) * std::cos(angle) - height * std::sin(angle)) * scale,
            scale};
}
float Mode7Fade::amount(uint64_t now) const {
    const float t = float(std::min<uint64_t>(now >= start_ ? now - start_ : 0, duration_)) / float(duration_);
    return from_ + ((target_ ? 1.0f : 0.0f) - from_) * t;
}
float Mode7Fade::update(bool perspective, uint64_t now) {
    if (perspective != target_) {
        from_ = amount(now);
        start_ = now;
        target_ = perspective;
    }
    return amount(now);
}
void Mode7Fade::settle(bool perspective) {
    target_ = perspective;
    from_ = perspective ? 1.0f : 0.0f;
    start_ = 0;
}
float SceneEffectFade::update(const PresentationScene &scene, uint64_t now) {
    using Kind = PresentationScene::Kind;
    const bool active = scene.kind == Kind::Field || scene.kind == Kind::Battle;
    if (active && settle_) {
        kind_ = scene.kind;
        map_ = scene.map;
        fade_.settle(true);
        settle_ = false;
    }
    if (active) {
        if (scene.kind != kind_ || (scene.kind == Kind::Field && scene.map != map_)) {
            // If both scenes are visible, leave the current strength continuously and cross neutral.
            if (fade_.amount(now) > 0)
                rise_ = now + 100'000'000;
            kind_ = scene.kind;
            map_ = scene.map;
        }
    } else {
        rise_ = 0;
    }
    const bool target = active && (!rise_ || now >= rise_);
    const float value = fade_.update(target, now);
    fading_ = active && (value < 1 || !target);
    return value;
}
FieldZoomEntry::Step FieldZoomEntry::step(int map, bool field, bool enhanced, bool hidden, int source_map,
                                          bool flat_source, float amount, bool hidden_entry) {
    Step result = Step::Fade;
    // Arriving (a warp, a load, back from a battle or a full-screen menu). A zoom still lowering on this
    // same map arrives at the settled framing too. Only a fidelity change animates zoom.
    if (field && enhanced && shown_ != map && hidden_entry)
        result = Step::Settle;
    // The zoomed map is gone or covered: nothing of it may linger over the next picture.
    else if (!field && flat_source && amount > 0 && hidden_entry && (hidden || source_map != map))
        result = Step::Drop;
    shown_ = field ? map : -1;
    return result;
}
FieldZoomEntry::Step WorldTiltEntry::step(int map, bool field, bool enhanced, bool hidden_entry) {
    using Step = FieldZoomEntry::Step;
    const auto result = !hidden_entry               ? Step::Fade
                        : !field                    ? Step::Drop
                        : enhanced && shown_ != map ? Step::Settle
                                                    : Step::Fade;
    shown_ = field ? map : -1;
    return result;
}
bool mode7_allowed(int map, FidelityMode level, bool field, bool layout) {
    return GameConfig::mode7_map(map) && level == FidelityMode::Enhanced && field && !layout;
}
bool crystal_door_vignette(const emu::Wsc &w) {
    // field_script_active, field_event_kind, field_event_argument (choose_tower_destination queues 2Bh-2Fh)
    return w.ram[0xD52C] == 1 && w.ram[0xC826] == 2 && w.ram[0xC827] >= 0x2B && w.ram[0xC827] <= 0x2F;
}
bool field_zoom_allowed(int map, FidelityMode level, bool field, bool layout) {
    return map > 0 && map < kMapCount && !GameConfig::mode7_map(map) && level == FidelityMode::Enhanced &&
           field && !layout;
}
Mode7Camera field_zoom_camera(float zoom, float amount, int width, int height, std::array<float, 2> scroll,
                              bool continuous) {
    Mode7Camera camera{0, 24, amount, 1};
    camera.output_scale = {float(std::max(1, width)) / 224, float(std::max(1, height)) / 144};
    const float scale = std::min(camera.output_scale[0], camera.output_scale[1]);
    const float wanted = std::clamp(zoom, 1.f, 2.f);
    const int low = std::max(1, int(std::floor(scale / wanted))), high = low + 1;
    // Compare actual zooms, rather than rounding the reciprocal (different midpoints).
    const float settled =
        float(std::abs(scale / float(low) - wanted) < std::abs(scale / float(high) - wanted) ? low : high);
    const float a = std::clamp(amount, 0.f, 1.f);
    camera.pixel_scale = a >= 1 ? settled : scale + (settled - scale) * a;
    // The share of the settled view's extra map now in sight: the framing moves with the scale.
    const float growth = scale / settled - 1;
    const float shown = a >= 1                       ? 1
                        : std::abs(growth) < 0.0001f ? a
                                                     : (scale / camera.pixel_scale - 1) / growth;
    for (size_t axis = 0; axis < 2; ++axis) {
        const float anchor = axis ? 79.f : 112.f;
        const float span = float(axis ? height : width) / settled;
        // Follow the original party anchor and preserve the authored eight-cell border.
        // Clamp the expanded view to the loaded map, never wrap or expose an invented cell.
        const float offset = anchor - anchor * camera.output_scale[axis] / settled;
        const float start =
            continuous ? scroll[axis] + std::floor(offset) : std::floor(scroll[axis] + offset);
        camera.origin[axis] = (std::clamp(start, -128.f, std::floor(1152 - span)) - scroll[axis]) * shown;
    }
    return camera;
}
namespace {
std::array<uint32_t, 128> field_actor_records(const emu::Wsc &live) {
    const auto *session = static_cast<const Session *>(live.game_session.get());
    const auto &w = session ? session->raster_field(live) : live;
    std::array<uint32_t, 128> result{};
    const auto oam = w.presentation_sprites();
    const auto &cards = w.presentation_terrain_cards();
    for (size_t slot = 0; slot < cards.size(); ++slot) {
        const auto &card = cards[slot];
        if (card.first < 0 || size_t(card.first * 4 + 7) >= oam.size())
            continue;
        const size_t at = size_t(card.first * 4);
        if ((oam[at] | oam[at + 1] << 8) != card.word || (oam[at + 4] | oam[at + 5] << 8) != card.word + 1)
            continue;
        for (int q = 0; q < 4 && at + size_t(q * 4 + 3) < oam.size(); ++q) {
            const size_t i = at + size_t(q * 4);
            if ((oam[i] | oam[i + 1] << 8) == card.word + q &&
                oam[i + 2] == uint8_t(oam[at + 2] + q / 2 * 8) &&
                oam[i + 3] == uint8_t(oam[at + 3] + q % 2 * 8))
                result[size_t(card.first + q)] = slot ? uint32_t(0xD5AE + (slot - 1) * 20) : 0xD520u;
        }
    }
    // Party art has its own fixed pair of DMA buffers, also used by synthetic fixtures.
    for (size_t i = 0; i + 3 < oam.size(); i += 4) {
        const int tile = (oam[i] | oam[i + 1] << 8) & 511;
        if (tile >= 0x10 && tile < 0x18)
            result[i / 4] = 0xD520u;
    }
    return result;
}
} // namespace
bool field_script_sprite(int word) {
    const int palette = 8 + (word >> 9 & 7), tile = word & 511;
    return !(kUiPalettes >> palette & 1) || (palette == 15 && tile >= 0x18 && tile < 0x98);
}
std::array<uint32_t, emu::FrameLayers::Pixels> mode7_ui_mask(const emu::Wsc &w, bool battle, bool scene,
                                                             bool host_only, bool world_sprites) {
    std::array<uint32_t, emu::FrameLayers::Pixels> out{};
    const auto *layers = w.frame_layers(false, scene);
    if (!layers) {
        out.fill(0xFF0000);
        return out;
    }
    const auto actors = field_actor_records(w);
    for (size_t at = 0; at < out.size(); ++at) {
        bool ui = (w.ui_coverage && (*w.ui_coverage)[at]) || (!host_only && !layers->valid[at / 224]);
        for (int layer = 1; layer < emu::FrameLayers::Count && !host_only; ++layer) {
            const auto &p = layers->planes[size_t(layer)][at];
            if (!(p.flags & emu::RasterPixel::Present))
                continue;
            // With world sprites every field sprite is drawn in the field; UI palettes still own theirs.
            // Entry numbers are not consulted then: the picture can keep lines drawn from the sprite table
            // before the latched one (the game's tick slips against the raster about every eighth frame),
            // where the same entry belonged to someone else.
            ui |= UiOwnership{!battle, battle, false, true}.owns(
                p, layer, (world_sprites && !battle) || (p.sprite < actors.size() && actors[p.sprite]));
        }
        if (UiOwnership{!battle, battle}.panel(int(at / 224)))
            ui = true;
        out[at] = ui ? 0xFF0000 : 0;
    }
    return out;
}
const std::array<uint32_t, emu::FrameLayers::Pixels> &
Mode7UiMask::get(const emu::Wsc &w, bool battle, bool scene, bool host_only, bool world_sprites) {
    // Built every call: a rebuild (~0.08 ms) costs less than comparing and copying the 2.7 MB of layer
    // planes a cache needs to prove its inputs unchanged.
    mask_ = mode7_ui_mask(w, battle, scene, host_only, world_sprites);
    ++rebuilds;
    return mask_;
}
static Mode7Picture render_world(emu::Wsc &w, const PresentationScene &flat, const LightTags &tags,
                                 const Mode7Camera &camera, bool snap, const LandmarkTags *landmarks,
                                 const std::map<int, std::array<uint16_t, 4>> *words, int saved_set) {
    Mode7Picture out;
    if (flat.kind != PresentationScene::Kind::Field || !GameConfig::mode7_map(flat.map) ||
        w.sram.size() < 0x2900 || (w.io[0x60] & 0xE0) != 0xE0)
        return out;
    const auto *row = Session::of(w).tables().row(Table::maps, unsigned(flat.map));
    const int set = saved_set >= 0 ? saved_set : row ? row[0] : w.rom[0x35A830 + unsigned(flat.map) * 18];
    World world(w, tags, set);
    if (words)
        world.words = *words;
    if (camera.amount == 0 || camera.tilt == 0)
        return {w.framebuffer_rgb(emu::Wsc::ColorMode::Raw), flat};
    const float left = float(int16_t(ram16(w, 0xD59D))), top = float(int16_t(ram16(w, 0xD59F)));
    static const LandmarkTags defaults;
    const auto &table = landmarks ? *landmarks : defaults;
    auto built = build_landmarks(world, flat.map, table, camera, left, top, true);
    auto &cards = built.cards;
    const auto &standing = built.standing;
    const auto key = [](int x, int y) { return (y + 8) * 80 + x + 8; };
    out.scene = flat;
    out.scene.background.clear();
    out.scene.actors.clear();
    out.scene.mask = flat.mask;
    for (auto &m : out.scene.mask)
        m &= 0xFF0000; // sprites acquire projected ownership below
    out.pixels.resize(224 * 144);
    out.scene.water.assign(out.pixels.size(), 0);
    for (int y = 0; y < 144; ++y)
        for (int x = 0; x < 224; ++x) {
            const size_t at = size_t(y * 224 + x);
            const auto point = camera.ground(float(x) + 0.5f, float(y) + 0.5f);
            const auto pixel = world.sample(left + point[0], top + point[1]);
            out.pixels[at] = pixel.color;
            if (world.foreground(pixel))
                out.scene.mask[at] |= 0x004000;
            // A bounded 2x2 area filter only where the footprint exceeds one world pixel.
            // Fixed subpixel taps avoid switching distant texels into bright single-pixel sparkle.
            if (y < 64 && camera.tilt > 0) {
                std::array<uint32_t, 4> taps{};
                for (int i = 0; i < 4; ++i) {
                    const auto p =
                        camera.ground(float(x) + (i & 1 ? 0.75f : 0.25f), float(y) + (i & 2 ? 0.75f : 0.25f));
                    taps[size_t(i)] = world.sample(left + p[0], top + p[1]).color;
                }
                const float amount = std::clamp(float(64 - y) / 48, 0.0f, 1.0f) * camera.amount;
                const uint32_t filtered = average(taps);
                uint32_t mixed = 0;
                for (unsigned sh : {0u, 8u, 16u})
                    mixed |= uint32_t((pixel.color >> sh & 255) * (1 - amount) +
                                      (filtered >> sh & 255) * amount + 0.5f)
                             << sh;
                out.pixels[at] = mixed;
            }
            if (const auto *tag = world.water(pixel))
                // World phase survives camera motion. Screen-space shoreline reflection cannot use
                // ground distance directly; keep tint/shimmer and suppress that approximation here.
                out.scene.water[at] = uint32_t(pixel.x & 255) << 16 | uint32_t(pixel.y & 255) << 8 |
                                      unsigned((tag->kind == "still" ? 3 : 1) * 16 + 12);
        }
    out.scene.lights.clear();
    // Reuse the reviewed material/light extraction in aligned flat chunks covering the wider footprint.
    // This does not activate actors or change the game's terrain ring.
    const auto northWest = camera.ground(0, 0), southEast = camera.ground(224, 144);
    const int x0 = int(std::floor((left + std::min(northWest[0], 0.0f) - 32) / 16)) * 16;
    const int y0 = int(std::floor((top + northWest[1] - 32) / 16)) * 16;
    const int x1 = int(std::ceil(left + std::max(southEast[0], 224.0f) + 32));
    const int y1 = int(std::ceil(top + southEast[1] + 32));
    auto lightLayers = std::make_unique<emu::FrameLayers>();
    std::array<uint32_t, emu::FrameLayers::Pixels> lightMask{};
    for (int oy = y0; oy < y1; oy += 144)
        for (int ox = x0; ox < x1; ox += 224) {
            for (int y = 0; y < 144; ++y) {
                lightLayers->valid[size_t(y)] = 1;
                lightLayers->registers[size_t(y)][0x10] = uint8_t(ox);
                lightLayers->registers[size_t(y)][0x11] = uint8_t(oy);
                for (int x = 0; x < 224; ++x) {
                    const auto p = world.sample(float(ox + x), float(oy + y), false);
                    const size_t at = size_t(y * 224 + x);
                    auto &r = lightLayers->planes[emu::FrameLayers::Screen1][at];
                    r.tile = uint16_t(p.tile);
                    r.palette = uint8_t(p.palette);
                    r.color = uint16_t((p.color >> 16 & 255) / 17 << 8 | (p.color >> 8 & 255) / 17 << 4 |
                                       (p.color & 255) / 17);
                    r.flags = emu::RasterPixel::Present | (p.mirror & 1 ? emu::RasterPixel::HFlip : 0) |
                              (p.mirror & 2 ? emu::RasterPixel::VFlip : 0);
                    lightMask[at] =
                        (ox + x < -128 || ox + x > 1151 || oy + y < kMode7AuthoredTop || oy + y > 1151)
                            ? 0xFF0000
                            : 0;
                }
            }
            auto lights = tags.visible(*lightLayers, lightMask.data(), set, w.frame_count);
            for (auto &light : lights) {
                light.position[0] += float(ox) - left;
                light.position[1] += float(oy) - top;
                out.scene.lights.push_back(light);
            }
        }
    for (auto &light : out.scene.lights) {
        auto p = camera.project(light.position[0], light.position[1]);
        const float wx = left + light.position[0], wy = top + light.position[1];
        for (const auto *e : standing) {
            if (!world.repair.count(key(e->cells.front().x, e->cells.front().y)))
                continue;
            const auto cell = std::find_if(e->cells.begin(), e->cells.end(), [&](const LandmarkCell &c) {
                return wx >= c.x * 16 && wx < (c.x + 1) * 16 && wy >= c.y * 16 && wy < (c.y + 1) * 16;
            });
            if (cell != e->cells.end()) {
                if (is_ruined_block(*e))
                    p = camera.elevated(light.position[0], light.position[1], 16 * camera.amount);
                else {
                    const float distance = float(e->base_row * 16) - wy;
                    const auto base = camera.project(light.position[0], float(e->base_row * 16) - top -
                                                                            distance * (1 - camera.amount));
                    p = {base[0], base[1] - distance * camera.amount * base[2], base[2]};
                }
                break;
            }
        }
        light.position[0] = p[0];
        light.position[1] = p[1];
        light.position[2] *= p[2];
        light.extent[0] *= p[2];
        light.extent[1] *= p[2] * std::cos(camera.tilt * kPi / 180);
    }
    // Latched OAM carries only the game's active sprites. Wider terrain never activates NPCs.
    // Decode complete cards, including the strip outside the original screen and clipped edge cells.
    struct Actor {
        int x, y;
        bool front = false;
        std::vector<emu::RasterPixel> cells;
    };
    std::map<std::pair<int, int>, Actor> actors;
    const auto sprites = w.presentation_sprites();
    for (size_t i = 0; i + 3 < sprites.size(); i += 4) {
        const int word = sprites[i] | sprites[i + 1] << 8, tile = word & 511;
        const int palette = 8 + (word >> 9 & 7);
        if (palette == 15 || tile < 0x10 || tile >= 0x98)
            continue;
        const int q = tile & 3;
        const int dx = q % 2, dy = q / 2;
        int ox = (sprites[i + 3] - dx * 8) & 255, oy = (sprites[i + 2] - dy * 8) & 255;
        if (ox >= 240)
            ox -= 256;
        if (oy >= 240)
            oy -= 256;
        auto &actor = actors[{oy, ox}];
        actor.x = ox;
        actor.y = oy;
        actor.front |= (word & 0x2000) != 0;
        emu::RasterPixel p;
        p.tile = uint16_t(tile);
        p.palette = uint8_t(palette);
        p.x = int16_t(ox + dx * 8);
        p.y = int16_t(oy + dy * 8);
        p.flags = uint8_t(emu::RasterPixel::Present | (word & 0x4000 ? emu::RasterPixel::HFlip : 0) |
                          (word & 0x8000 ? emu::RasterPixel::VFlip : 0));
        actor.cells.push_back(p);
    }
    for (const auto &[key, actor] : actors) {
        (void)key;
        const auto foot = camera.project(float(actor.x + 8), float(actor.y + 15));
        const float scale = foot[2];
        float fx = foot[0], fy = foot[1];
        if (snap) {
            fx = std::round(fx);
            fy = std::round(fy);
        }
        if (!world.water(world.sample(left + actor.x + 8, top + actor.y + 15)))
            out.scene.actors.push_back(
                {fx, fy, 6 * scale, 2 * scale * scale * std::cos(camera.tilt * kPi / 180)});
        // Inverse nearest sampling preserves an upright card's pixel shapes, without holes at >1 scale.
        std::array<uint32_t, 256> card{};
        std::array<bool, 256> covered{};
        for (const auto &p : actor.cells) {
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) {
                    const int cx = p.x - actor.x + x, cy = p.y - actor.y + y;
                    if (cx < 0 || cx >= 16 || cy < 0 || cy >= 16)
                        continue;
                    const int sx = p.flags & emu::RasterPixel::HFlip ? 7 - x : x;
                    const int sy = p.flags & emu::RasterPixel::VFlip ? 7 - y : y;
                    const int byte = w.ram[size_t(0x4000 + p.tile * 32 + sy * 4 + sx / 2)];
                    const int index = sx & 1 ? byte & 15 : byte >> 4;
                    if (!index)
                        continue;
                    card[size_t(cy * 16 + cx)] =
                        rgb(w.presentation_palette_color(p.palette, index, false, true));
                    covered[size_t(cy * 16 + cx)] = true;
                }
        }
        Card upright{float(actor.x + 8),
                     float(actor.y + 15),
                     16,
                     16,
                     15,
                     actor.front,
                     false,
                     std::vector<uint32_t>(card.begin(), card.end()),
                     std::vector<uint8_t>(covered.begin(), covered.end())};
        cards.push_back(std::move(upright));
    }
    // Both kinds share ground depth. Terrain occlusion is independent of previously painted cards.
    const auto terrain_mask = out.scene.mask;
    std::stable_sort(cards.begin(), cards.end(), [](const Card &a, const Card &b) { return a.y < b.y; });
    for (const auto &card : cards) {
        const float angle = camera.tilt * kPi / 180;
        const float f = 224 / (2 * std::tan(camera.lens * kPi / 360));
        const float a = card.landmark ? camera.amount : 1;
        const float base = card.y - 79;
        const auto foot = camera.project(card.x, card.y);
        const float shift_x = snap && !card.slab ? (std::round(foot[0]) - foot[0]) * a : 0;
        const float shift_y = snap && !card.slab ? (std::round(foot[1]) - foot[1]) * a : 0;
        // Inverse rasterization of each complete surface keeps roofs unified and depth-sorted.
        for (int y = 0; y < 144; ++y)
            for (int x = 0; x < 224; ++x) {
                const float v = float(y) + 0.5f - 79 - shift_y;
                float distance =
                    (base * f * std::cos(angle) - v * (f * camera.distance - base * std::sin(angle))) /
                    (f * ((1 - a) * std::cos(angle) + a) + v * (1 - a) * std::sin(angle));
                float scale = f / (f * camera.distance - (base - distance * (1 - a)) * std::sin(angle));
                int cx = int(
                    std::floor((float(x) + 0.5f - 112 - shift_x) / scale + 112 - card.x + card.width / 2));
                float source_distance = distance;
                if (card.extra) {
                    const float below_shaft = float(card.height - card.extra - card.shaft_tip);
                    if (distance >= below_shaft + card.extra * a)
                        source_distance += card.extra * (1 - a);
                    else if (distance > below_shaft && a > 0)
                        source_distance = below_shaft + (distance - below_shaft) / a;
                }
                int cy = int(std::floor(card.bottom - source_distance));
                if (card.slab) {
                    const auto roof = camera.ground_at_height(float(x) + 0.5f, float(y) + 0.5f, 16 * a);
                    cx = int(std::floor(roof[0] - card.x + card.width / 2));
                    cy = int(std::floor(roof[1] - card.y + card.height));
                    if (cx < 0 || cx >= card.width || cy < 0 || cy >= card.height ||
                        !card.covered[size_t(cy * card.width + cx)]) {
                        // South wall: the slab's own bottom-row art, one world tile high.
                        const float h = (base * f * std::cos(angle) -
                                         v * (f * camera.distance - base * std::sin(angle))) /
                                        (f * std::sin(angle) - v * std::cos(angle));
                        if (a <= 0 || !std::isfinite(h) || h < 0 || h >= 16 * a)
                            continue;
                        scale = f / (f * camera.distance - base * std::sin(angle) - h * std::cos(angle));
                        cx = int(std::floor((float(x) + 0.5f - 112 - shift_x) / scale + 112 - card.x +
                                            card.width / 2));
                        cy = card.height - 16 + int(std::floor(16 - h / a));
                    }
                }
                const size_t at = size_t(y * 224 + x);
                if (scale <= 0 || cx < 0 || cx >= card.width || cy < 0 || cy >= card.height ||
                    !card.covered[size_t(cy * card.width + cx)] || (out.scene.mask[at] & 0xFF0000) ||
                    (!card.front && (terrain_mask[at] & 0x004000)))
                    continue;
                out.pixels[at] = card.pixels[size_t(cy * card.width + cx)];
                out.scene.mask[at] |= 0x004000;
                out.scene.water[at] = 0;
            }
    }

    const auto ordinary = w.framebuffer_rgb(emu::Wsc::ColorMode::Raw);
    for (size_t at = 0; at < out.pixels.size(); ++at)
        if (out.scene.mask[at] & 0xFF0000) {
            out.pixels[at] = ordinary[at];
            out.scene.water[at] = 0;
        }
    return out;
}
Mode7Picture mode7_picture(emu::Wsc &w, const PresentationScene &flat, const LightTags &tags,
                           const Mode7Camera &camera, bool snap, const LandmarkTags *landmarks) {
    return render_world(w, flat, tags, camera, snap, landmarks, nullptr, -1);
}
void Mode7Source::latch(emu::Wsc &w, const PresentationScene &scene) {
    if (!machine_)
        machine_ = std::make_unique<emu::Wsc>(w.rom, 0);
    auto &session = Session::of(w);
    auto &source = session.raster_field(w);
    machine_->restore(w.snapshot()); // retain the completed native pixels for the zero-camera endpoint
    source.copy_presentation_to(*machine_);
    words_ = &source == &w ? GfxSwap::field_world_words(w) : session.raster_field_words();
    scene_ = scene;
    const auto *row = Session::of(w).tables().row(Table::maps, unsigned(scene.map));
    tileset_ = row ? row[0] : w.rom[0x35A830 + unsigned(scene.map) * 18];
}
Mode7Picture Mode7Source::picture(const LightTags &tags, const Mode7Camera &camera, bool snap,
                                  const LandmarkTags *landmarks) const {
    if (!machine_)
        return {};
    return render_world(*machine_, scene_, tags, camera, snap, landmarks, &words_, tileset_);
}
namespace {
uint64_t hash_word(uint64_t h, uint64_t value) { return (h ^ value) * 1099511628211ull; }
uint64_t hash_bytes(const uint8_t *p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i)
        h = hash_word(h, p[i]);
    return h;
}
} // namespace
std::array<float, 2> field_raster_scroll(const emu::Wsc &w) {
    std::array<float, 2> result{};
    for (int axis = 0; axis < 2; ++axis) {
        const int world = int16_t(ram16(w, 0xD59D + axis * 2));
        const int pending = w.ram[size_t(0xD511 + axis)];
        const int displayed = w.io[size_t(0x10 + axis)];
        result[size_t(axis)] = float(world + int8_t(uint8_t(displayed - pending)));
    }
    return result;
}
std::vector<Mode7Sprite> field_npc_sprites(emu::Wsc &w) {
    std::vector<Mode7Sprite> result;
    for (int slot = 0; slot < 16; ++slot) {
        const int at = 0xD5AE + slot * 20;
        const int pose = w.ram[size_t(at)], flags = w.ram[size_t(at + 17)];
        if (pose == 255 || (flags & 0x08) || ((flags & 0x20) && (ram16(w, 0xC80C) & 8)))
            continue;
        const int x = ram16(w, at + 6) >> 1, y = ram16(w, at + 8) >> 1;
        const int gx = x / 16, gy = y / 16;
        int count = 4;
        bool veiled = false;
        if (!(flags & 0x10) && gx < 64 && gy > 0 && gy < 63 && w.sram.size() >= 0x3100) {
            const int attr = w.read8(0x12100 + unsigned(gy * 64 + gx));
            const int terrain = w.ram[size_t(0xD57B + attr)];
            veiled = (terrain & 0x20) != 0;
            if ((terrain & 0x60) && !veiled)
                count = 2;
        }
        const int set = w.ram[size_t(0xD5A6 + (flags & 7))];
        const int table_at = set * 4;
        const auto read_word = [&](uint32_t a) { return uint16_t(w.read8(a) | w.read8(a + 1) << 8); };
        const uint16_t offset = read_word(0x940B0 + unsigned(table_at));
        const int bank = read_word(0x93F72 + unsigned(table_at)) & 15;
        // The native writer draws before advancing the blink timer. Match that raster
        // rather than using the following pose on the single frame where the toggle changes.
        const int blink = w.ram[0xD5A3] ^ (w.ram[0xC811] && w.ram[0xD5A4] == 0x20 ? 1 : 0);
        const int facing = (pose + ((pose & 0x40) ? blink : 0)) & 63;
        const uint16_t source = uint16_t(offset + facing * 128);
        std::array<uint8_t, 128> art{};
        uint64_t h = hash_word(veiled ? 2 : 1, unsigned(count));
        for (unsigned i = 0; i < art.size(); ++i) {
            art[i] = w.read8(unsigned(bank << 16) | uint16_t(source + i));
            h = hash_word(h, art[i]);
        }
        const int palette = 8 + (flags & 7);
        std::array<uint32_t, 16> colors{};
        for (int i = 1; i < 16; ++i) {
            colors[size_t(i)] = 0xFF000000 | rgb(w.presentation_palette_color(palette, i, false, true));
            h = hash_word(h, colors[size_t(i)]);
        }
        auto pixels = std::make_shared<std::vector<uint32_t>>(256);
        auto paint = std::make_shared<std::vector<uint8_t>>(256);
        for (int q = 0; q < count; ++q)
            for (int py = 0; py < 8; ++py)
                for (int px = 0; px < 8; ++px) {
                    const int byte = art[size_t(q * 32 + py * 4 + px / 2)];
                    const int index = px & 1 ? byte & 15 : byte >> 4;
                    const size_t to = size_t((q / 2 * 8 + py) * 16 + q % 2 * 8 + px);
                    (*pixels)[to] = colors[size_t(index)] & (veiled && q >= 2 ? 0x80FFFFFFu : 0xFFFFFFFFu);
                    (*paint)[to] = uint8_t(index ? (flags & 7) << 4 | index : 0);
                }
        Mode7Sprite sprite;
        sprite.id = h;
        sprite.record = unsigned(at);
        sprite.x = float(x + 8);
        sprite.y = float(y + 15);
        sprite.width = sprite.height = 16;
        sprite.bottom = 15;
        sprite.veil = veiled;
        sprite.front = false; // native field objects are behind foreground terrain
        sprite.pixels = std::move(pixels);
        sprite.paint = std::move(paint);
        result.push_back(std::move(sprite));
    }
    return result;
}
namespace {
// Objects built from several 16x16 NPC records (the floating castle: four records with flag 0x10, "ignore
// terrain") stand as one card, like the world map's landmarks, instead of four cards with seams between them.
// Pieces join when they ignore terrain and sit on neighboring 16-pixel cells.
void join_object_pieces(const emu::Wsc &w, std::vector<Mode7Sprite> &sprites,
                        std::map<uint64_t, Mode7Sprite> &cache) {
    const auto piece = [&](const Mode7Sprite &s) {
        return !s.landmark && s.width == 16 && s.height == 16 && s.record >= 0xD5AE &&
               s.record < 0xD5AE + 16 * 20 && (s.record - 0xD5AE) % 20 == 0 &&
               (w.ram[size_t(s.record + 17)] & 0x10);
    };
    std::vector<size_t> pieces;
    for (size_t i = 0; i < sprites.size(); ++i)
        if (piece(sprites[i]))
            pieces.push_back(i);
    if (pieces.size() < 2)
        return;
    std::vector<int> group(pieces.size(), -1);
    std::vector<bool> joined(sprites.size());
    std::vector<Mode7Sprite> objects;
    for (size_t first = 0; first < pieces.size(); ++first) {
        if (group[first] >= 0)
            continue;
        std::vector<size_t> members{first};
        group[first] = int(first);
        for (size_t at = 0; at < members.size(); ++at) {
            const auto &a = sprites[pieces[members[at]]];
            for (size_t j = 0; j < pieces.size(); ++j) {
                const auto &b = sprites[pieces[j]];
                const float dx = std::abs(a.x - b.x), dy = std::abs(a.y - b.y);
                if (group[j] < 0 &&
                    ((dx == 16 && dy == 0) || (dx == 0 && dy == 16) || (dx == 16 && dy == 16))) {
                    group[j] = int(first);
                    members.push_back(j);
                }
            }
        }
        if (members.size() < 2)
            continue;
        float left = 1e9f, top = 1e9f, right = -1e9f, bottom = -1e9f;
        uint64_t h = 3;
        for (size_t m : members) {
            const auto &s = sprites[pieces[m]];
            left = std::min(left, s.x - 8);
            top = std::min(top, s.y - 15);
            right = std::max(right, s.x + 8);
            bottom = std::max(bottom, s.y + 1);
        }
        for (size_t m : members) {
            const auto &s = sprites[pieces[m]];
            h = hash_word(hash_word(hash_word(h, s.id), unsigned(s.x - left)), unsigned(s.y - top));
        }
        Mode7Sprite object;
        object.id = h;
        object.width = int(right - left);
        object.height = int(bottom - top);
        object.bottom = object.height - 1;
        object.x = (left + right) / 2;
        object.y = bottom - 1;
        object.front = false;
        if (auto found = cache.find(h); found != cache.end()) {
            object.pixels = found->second.pixels;
            object.paint = found->second.paint;
        } else {
            auto pixels = std::make_shared<std::vector<uint32_t>>(size_t(object.width * object.height));
            auto paint = std::make_shared<std::vector<uint8_t>>(pixels->size());
            bool painted = true;
            for (size_t m : members) {
                const auto &s = sprites[pieces[m]];
                painted &= bool(s.paint);
                const int ox = int(s.x - 8 - left), oy = int(s.y - 15 - top);
                for (int y = 0; y < 16; ++y)
                    for (int x = 0; x < 16; ++x)
                        if (const uint32_t c = (*s.pixels)[size_t(y * 16 + x)]; c >> 24) {
                            const size_t to = size_t((oy + y) * object.width + ox + x);
                            (*pixels)[to] = c;
                            if (s.paint)
                                (*paint)[to] = (*s.paint)[size_t(y * 16 + x)];
                        }
            }
            object.pixels = pixels;
            if (painted)
                object.paint = paint;
            cache.emplace(h, object);
        }
        for (size_t m : members)
            joined[pieces[m]] = true;
        objects.push_back(std::move(object));
    }
    std::vector<Mode7Sprite> kept;
    for (size_t i = 0; i < sprites.size(); ++i)
        if (!joined[i])
            kept.push_back(std::move(sprites[i]));
    for (auto &object : objects)
        kept.push_back(std::move(object));
    sprites = std::move(kept);
}
} // namespace
bool Mode7World::update(emu::Wsc &live, const PresentationScene &flat, const LightTags &tags,
                        const LandmarkTags &table, bool extend_npcs) {
    auto &session = Session::of(live);
    auto &w = session.raster_field(live);
    if (flat.kind != PresentationScene::Kind::Field ||
        !(GameConfig::mode7_map(flat.map) ||
          field_zoom_allowed(flat.map, FidelityMode::Enhanced, true, false)) ||
        w.sram.size() < 0x2900 || (w.io[0x60] & 0xE0) != 0xE0)
        return false;
    const auto *row = Session::of(w).tables().row(Table::maps, unsigned(flat.map));
    const int set = row ? row[0] : w.rom[0x35A830 + unsigned(flat.map) * 18];
    const bool new_map = map_ != flat.map || set_ != set;
    const bool fresh = presentation_diagnostics::fresh() || new_map;
    if (fresh) {
        static std::atomic<uint64_t> next_generation{1};
        surface_.generation = next_generation.fetch_add(1);
        map_ = flat.map;
        set_ = set;
        if (new_map)
            words_.clear();
        repair_.clear();
        landmarks_.clear();
        actor_cache_.clear();
        light_chunks_ = {};
        landmark_hash_ = 0;
        surface_.words.resize(160 * 160);
        surface_.indices.resize(8192);
        surface_.water.resize(256 * 1024);
        light_words_ = {};
    }
    World world(w, tags, set);
    if (&w != &live)
        world.words = session.raster_field_words();
    // Bulk comparisons are cheap; only actual topology changes visit the 6400 cells.
    bool topology_changed = fresh;
    const auto bank = size_t(world.bank) % w.sram.size();
    const bool contiguous = bank + topology_.size() <= w.sram.size();
    if (fresh || !contiguous || std::memcmp(topology_.data(), w.sram.data() + bank, topology_.size())) {
        for (size_t i = 0; i < topology_.size(); ++i) {
            const auto value = world.data(int(i));
            if (topology_[i] != value) {
                if (i < 6400)
                    words_.erase(int(i)); // a real cell edit invalidates its old ring words
                topology_[i] = value;
                topology_changed = true;
            }
        }
    }
    if (fresh || std::memcmp(foreground_flags_.data(), w.ram.data() + 0xD57B, 32)) {
        std::copy_n(w.ram.data() + 0xD57B, 32, foreground_flags_.begin());
        topology_changed = true;
    }
    // Retain actual field-ring/event overrides after they leave the viewport.
    // Ordinary incoming rows equal the atlas; walking does not invalidate world topology/cards.
    for (const auto &[cell, words] : world.words) {
        auto found = words_.find(cell);
        std::array<uint16_t, 4> original{};
        for (int q = 0; q < 4; ++q) {
            const int at = 0x1900 + world.data(cell) * 8 + q * 2;
            original[size_t(q)] = uint16_t(world.data(at) | world.data(at + 1) << 8);
        }
        const auto &previous = found == words_.end() ? original : found->second;
        if (previous != words) {
            words_[cell] = words;
            topology_changed = true;
        }
    }
    world.words = words_;
    if (topology_changed)
        ++topology_revision_;
    scene_.kind = flat.kind;
    scene_.map = flat.map;
    scroll_ = field_raster_scroll(w);
    auto &tiles = tile_hashes_;
    bool tiles_changed = fresh, palettes_changed = fresh;
    std::array<bool, 1024> changed_tiles{};
    std::array<uint16_t, 16> changed_palettes{};
    for (int i = 0; i < 1024; ++i) {
        const auto *source = &w.ram[size_t((i < 512 ? 0x4000 : 0x8000) + (i & 511) * 32)];
        auto *previous = reinterpret_cast<uint8_t *>(surface_.indices.data()) + i * 32;
        if (!fresh && std::memcmp(previous, source, 32) == 0)
            continue;
        std::memcpy(previous, source, 32);
        tiles[size_t(i)] = hash_bytes(source, 32);
        uint16_t colors = 0;
        for (int b = 0; b < 32; ++b)
            colors |= uint16_t((1u << (source[b] >> 4)) | (1u << (source[b] & 15)));
        tile_colors_[size_t(i)] = colors;
        tiles_changed = true;
        changed_tiles[size_t(i)] = true;
    }
    std::array<uint64_t, 16> palettes{};
    for (int p = 0; p < 16; ++p) {
        uint64_t h = 1;
        for (int i = 0; i < 16; ++i) {
            const auto c = world.colors[size_t(p)][size_t(i)];
            h = hash_word(h, c);
            auto &previous = surface_.palettes[size_t(p * 16 + i)];
            if (previous != c)
                changed_palettes[size_t(p)] |= uint16_t(1u << i);
            palettes_changed |= previous != c;
            previous = c;
        }
        palettes[size_t(p)] = h;
    }
    if (fresh) {
        std::fill(surface_.water.begin(), surface_.water.end(), 0);
        for (int mirror = 0; mirror < 4; ++mirror)
            for (int tile = 512; tile < 1024; ++tile) {
                // Store source orientation once; the tile-word mirror handles both art and tags.
                const auto *tag = tags.water(set, (tile - 512) / 4, mirror);
                if (!tag || !tag->enabled)
                    continue;
                for (int y = 0; y < 8; ++y)
                    for (int x = 0; x < 8; ++x)
                        surface_.water[size_t((mirror * 256 + tile / 32 * 8 + y) * 256 + tile % 32 * 8 + x)] =
                            tag->mask[size_t((tile & 2) * 4 + y)] & (1u << ((tile & 1) * 8 + x))
                                ? uint32_t((tag->kind == "still" ? 3 : 1) * 16 + 12) << 16
                                : 0;
            }
    }
    const auto cell_hash = [&](int cell, int replacement) {
        const int id = replacement < 0 ? world.data(cell) : replacement;
        const auto live = words_.find(cell);
        uint64_t h = hash_word(1, unsigned(id));
        for (int q = 0; q < 4; ++q) {
            const int at = 0x1900 + id * 8 + q * 2;
            const int word = replacement < 0 && live != words_.end()
                                 ? live->second[size_t(q)]
                                 : world.data(at) | world.data(at + 1) << 8;
            const int tile = (word & 511) | (word & 0x2000 ? 512 : 0);
            landmark_tiles_[size_t(tile)] = true;
            const int palette = word >> 9 & 15;
            const auto colors = tile_colors_[size_t(tile)];
            landmark_palettes_[size_t(palette)] |= colors;
            h = hash_word(hash_word(h, unsigned(word)), tiles[size_t(tile)]);
            // Shared palette animation may change water colors absent from a landmark's art.
            for (int index = 0; index < 16; ++index)
                if (colors & (1u << index))
                    h = hash_word(h, world.colors[size_t(palette)][size_t(index)]);
        }
        return h;
    };
    // Topology changes repair selection. Animation elsewhere does not rebuild landmark cards.
    const auto landmark_signature = [&]() {
        uint64_t h = hash_word(1, table.revision());
        h = hash_word(h, topology_revision_);
        for (int i = 0; i < 128; ++i)
            h = hash_word(h, table.plain(map_, set, i));
        for (const auto &e : table.entries()) {
            if (e.map != map_ || e.tileset != set)
                continue;
            h = hash_word(h, e.enabled);
            if (!e.enabled)
                continue;
            h = hash_word(h, unsigned(e.base_row));
            h = hash_word(h, std::bit_cast<uint32_t>(e.extra_shaft_rows));
            h = hash_word(h, unsigned(e.shaft_row));
            for (const auto &c : e.cells) {
                h = hash_word(h, unsigned((c.y + 8) * 80 + c.x + 8));
                h = hash_word(h, unsigned(c.metatile * 4 + c.mirror));
                h = hash_word(h, cell_hash((c.y + 8) * 80 + c.x + 8, -1));
            }
            for (int graphic : e.cutout_ground_metatiles) {
                h = hash_word(h, unsigned(graphic));
                for (int id = 0; id < 256; ++id) {
                    const int word = world.data(0x1900 + id * 8) | world.data(0x1901 + id * 8) << 8;
                    if ((word & 0x2000) && (word & 511) / 4 == graphic) {
                        h = hash_word(h, cell_hash(0, id));
                        break;
                    }
                }
            }
        }
        // Floor colors determine the edge-connected card cutout, so retain their art dependencies too.
        for (const auto &[cell, id] : repair_)
            h = hash_word(h, cell_hash(cell, id));
        return h;
    };
    bool landmark_inputs_changed =
        fresh || topology_changed || table_revision_ != table.revision() || palettes_refreshed_;
    palettes_refreshed_ = false;
    for (size_t i = 0; i < changed_tiles.size(); ++i)
        landmark_inputs_changed |= changed_tiles[i] && landmark_tiles_[i];
    for (size_t i = 0; i < changed_palettes.size(); ++i)
        landmark_inputs_changed |= (changed_palettes[i] & landmark_palettes_[i]) != 0;
    if (landmark_inputs_changed) {
        landmark_tiles_.fill(false);
        landmark_palettes_.fill(0);
    }
    const uint64_t landmark_hash = landmark_inputs_changed ? landmark_signature() : landmark_hash_;
    table_revision_ = table.revision();
    const bool landmarks_changed = fresh || landmark_hash != landmark_hash_;
    if (landmarks_changed) {
        Mode7Camera camera;
        // Field props stay on the flat plane, including any world-style mod landmark tags.
        auto built = GameConfig::mode7_map(map_) ? build_landmarks(world, map_, table, camera, 0, 0, false)
                                                 : LandmarkBuild{};
        repair_ = world.repair;
        landmarks_.clear();
        ++card_revision_;
        for (const auto &card : built.cards) {
            Mode7Sprite sprite;
            sprite.id = (card_revision_ << 32) | (landmarks_.size() + 1);
            sprite.x = card.x;
            sprite.y = card.y;
            sprite.width = card.width;
            sprite.height = card.height;
            sprite.bottom = card.bottom;
            sprite.extra = card.extra;
            sprite.shaft_tip = card.shaft_tip;
            sprite.footprint = std::make_shared<const std::vector<LandmarkCell>>(card.entry->cells);
            sprite.landmark = true;
            sprite.front = card.front;
            auto pixels = std::make_shared<std::vector<uint32_t>>(card.pixels);
            for (size_t i = 0; i < pixels->size(); ++i)
                (*pixels)[i] = card.covered[i] ? (*pixels)[i] | 0xFF000000 : 0;
            sprite.pixels = pixels;
            landmarks_.push_back(std::move(sprite));
        }
        // Include the new floor dependencies now, avoiding a redundant rebuild on the next frame.
        landmark_hash_ = landmark_signature();
    }
    world.repair = repair_;
    surface_.dirty.clear();
    std::array<uint64_t, 54> light_hashes{};
    std::array<bool, 54> lit{};
    if (topology_changed || landmarks_changed) {
        light_words_ = {};
        for (int cell = 0; cell < 6400; ++cell) {
            const int x = cell % 80 * 16 - 128, y = cell / 80 * 16 - 128;
            const auto source = world.sample(float(x), float(y), false);
            const int chunk = (cell / 80 / 9) * 6 + cell % 80 / 14;
            if (source.tile >= 512) {
                const auto *tag = tags.find(set, (source.tile - 512) / 4, source.mirror);
                if (tag && tag->enabled) {
                    const int id = world.data(cell);
                    for (int q = 0; q < 4; ++q) {
                        const auto live = words_.find(cell);
                        const int at = 0x1900 + id * 8 + q * 2;
                        light_words_[size_t(chunk)].push_back(
                            live == words_.end() ? uint16_t(world.data(at) | world.data(at + 1) << 8)
                                                 : live->second[size_t(q)]);
                    }
                }
            }
            const auto repaired = repair_.find(cell);
            const int id = repaired == repair_.end() ? world.data(cell) : repaired->second;
            const auto live = words_.find(cell);
            const bool foreground = repaired == repair_.end() && world.foreground(source);
            bool changed = fresh;
            for (int q = 0; q < 4; ++q) {
                const int at = 0x1900 + id * 8 + q * 2;
                const int word = repaired == repair_.end() && live != words_.end()
                                     ? live->second[size_t(q)]
                                     : world.data(at) | world.data(at + 1) << 8;
                const uint32_t encoded =
                    uint32_t((word & 255) << 16 | (word >> 8) << 8 | (foreground ? 64 : 0));
                auto &previous =
                    surface_.words[size_t((cell / 80 * 2 + q / 2) * 160 + cell % 80 * 2 + q % 2)];
                changed |= encoded != previous;
                previous = encoded;
            }
            if (changed) {
                surface_.dirty.push_back(cell);
                surface_.changed[size_t(cell)] = surface_.revision + 1;
            }
        }
    }
    if (!surface_.dirty.empty())
        ++surface_.revision;
    // Actor DMA writes use the same tile RAM as terrain. They must update actor art, but do
    // not invalidate the 1280x1280 ground atlas and its mips unless the ground actually uses them.
    // Rebuild dependencies on real layout edits, including mods that put lower-bank art on the ground.
    const bool layout_changed = fresh || !surface_.dirty.empty();
    if (layout_changed) {
        ground_tiles_ = {};
        ground_palettes_ = {};
        for (auto encoded : surface_.words) {
            const int word = int((encoded >> 16 & 255) | (encoded >> 8 & 255) << 8);
            ground_tiles_[size_t((word & 511) | (word & 0x2000 ? 512 : 0))] = true;
            ground_palettes_[size_t(word >> 9 & 15)] = true;
        }
    }
    bool ground_art_changed = layout_changed, ground_colors_changed = layout_changed;
    if (tiles_changed)
        for (size_t i = 0; i < changed_tiles.size(); ++i)
            ground_art_changed |= ground_tiles_[i] && changed_tiles[i];
    if (palettes_changed)
        for (size_t i = 0; i < changed_palettes.size(); ++i)
            ground_colors_changed |= ground_palettes_[i] && changed_palettes[i];
    if (ground_art_changed)
        ++surface_.tile_revision;
    if (ground_colors_changed)
        ++surface_.palette_revision;
    for (size_t i = 0; i < light_words_.size(); ++i) {
        lit[i] = !light_words_[i].empty();
        uint64_t h = hash_word(1, topology_revision_);
        for (auto word : light_words_[i]) {
            const int tile = (word & 511) | (word & 0x2000 ? 512 : 0);
            h = hash_word(hash_word(h, tiles[size_t(tile)]), palettes[size_t(word >> 9 & 15)]);
        }
        light_hashes[i] = h;
    }
    // Light extraction runs only for changed emitting chunks, using the same reviewed tag rules.
    // A chunk's tagged light-tile pixels depend only on the layout (topology_revision_): collect them
    // once per layout, cell by cell (untagged cells cost nothing). Fades, palette animation and animated
    // tile art (flames) then re-read just those pixels' live index and color; LightTags::visible sees
    // exactly the pixels it would in a full chunk (it skips every other pixel), so the lights are the same.
    // Chunks that can reach the screen (project() keeps lights within 64 px of it) update first, longest
    // stale first, up to light_pixel_budget; the rest catch up nearest first, a few per frame, so no fade
    // step, light-tile animation or map entry costs a whole map in one frame.
    static const std::array<uint32_t, emu::FrameLayers::Pixels> no_mask{};
    // Scratch only: neither retained presentation inputs nor machine state. Palette fades update
    // these sparse pixels every frame; keep the multi-megabyte backing allocation between calls.
    struct LightScratch {
        std::unique_ptr<emu::FrameLayers> layers;
        std::vector<size_t> drawn;
    };
    static thread_local LightScratch scratch;
    auto &layers = scratch.layers;
    auto &drawn = scratch.drawn;
    const uint64_t layout = topology_revision_ + 1;
    constexpr int kOffscreenPerFrame = 3;
    ++light_clock_;
    std::array<std::tuple<float, uint64_t, int>, 54> order{};
    int stale = 0;
    for (int i = 0; i < 54; ++i) {
        auto &chunk = light_chunks_[size_t(i)];
        if (chunk.hash == light_hashes[size_t(i)] && chunk.layout == layout) {
            chunk.stale_since = 0;
            continue;
        }
        if (!chunk.stale_since)
            chunk.stale_since = light_clock_;
        const float left = float(i % 6 * 224 - 128), top = float(i / 6 * 144 - 128);
        const float dx = std::max({0.f, scroll_[0] - 64 - (left + 224), left - (scroll_[0] + 288)});
        const float dy = std::max({0.f, scroll_[1] - 64 - (top + 144), top - (scroll_[1] + 208)});
        order[size_t(stale++)] = {dx * dx + dy * dy, chunk.stale_since, i};
    }
    std::sort(order.begin(), order.begin() + stale);
    int offscreen = 0;
    size_t extracted = 0;
    for (int k = 0; k < stale; ++k) {
        const int i = std::get<2>(order[size_t(k)]);
        if (!presentation_diagnostics::fresh() && std::get<0>(order[size_t(k)]) > 0 &&
            ++offscreen > kOffscreenPerFrame)
            break;
        if (!presentation_diagnostics::fresh() && light_pixel_budget && extracted >= light_pixel_budget)
            break;
        auto &chunk = light_chunks_[size_t(i)];
        chunk.hash = light_hashes[size_t(i)];
        chunk.stale_since = 0;
        chunk.lights.clear();
        const int ox = i % 6 * 224 - 128, oy = i / 6 * 144 - 128;
        if (chunk.layout != layout || !lit[size_t(i)]) {
            chunk.layout = layout;
            chunk.pixels.clear();
            // Chunks are 14 x 9 whole cells; cells past the loaded map (column/row 80) are masked.
            for (int cy = 0; lit[size_t(i)] && cy < 9; ++cy)
                for (int cx = 0; cx < 14; ++cx) {
                    const int cell_x = (ox + 128) / 16 + cx, cell_y = (oy + 128) / 16 + cy;
                    if (cell_x >= 80 || cell_y >= 80)
                        continue;
                    const int key = cell_y * 80 + cell_x, id = world.data(key);
                    const auto live = world.words.find(key);
                    for (int q = 0; q < 4; ++q) {
                        const int at = 0x1900 + id * 8 + q / 2 * 4 + q % 2 * 2;
                        const int word = live == world.words.end() ? world.data(at) | world.data(at + 1) << 8
                                                                   : live->second[size_t(q)];
                        const int tile = (word & 511) | (word & 0x2000 ? 512 : 0), mirror = word >> 14;
                        const int palette = word >> 9 & 15;
                        if (tile < 512 || palette > 3)
                            continue;
                        const auto *tag = tags.find(set, (tile - 512) / 4, mirror);
                        if (!tag || !tag->enabled)
                            continue;
                        for (int v = 0; v < 8; ++v)
                            for (int u = 0; u < 8; ++u) {
                                const int px = mirror & 1 ? 7 - u : u, py = mirror & 2 ? 7 - v : v;
                                const int address =
                                    (tile < 512 ? 0x4000 : 0x8000) + (tile & 511) * 32 + py * 4 + px / 2;
                                chunk.pixels.push_back({uint8_t(cx * 16 + q % 2 * 8 + u),
                                                        uint8_t(cy * 16 + q / 2 * 8 + v), uint16_t(tile),
                                                        uint16_t(address), uint8_t(palette), uint8_t(mirror),
                                                        (px & 1) != 0});
                            }
                    }
                }
        }
        extracted += chunk.pixels.size();
        if (chunk.pixels.empty())
            continue;
        if (!layers) {
            layers = std::make_unique<emu::FrameLayers>();
            layers->valid.fill(1);
        }
        for (size_t at : drawn)
            layers->planes[emu::FrameLayers::Screen1][at] = {};
        drawn.clear();
        for (int y = 0; y < 144; ++y) {
            layers->registers[size_t(y)][0x10] = uint8_t(ox);
            layers->registers[size_t(y)][0x11] = uint8_t(oy);
        }
        for (const auto &p : chunk.pixels) {
            const int byte = w.ram[p.address];
            const uint32_t color = world.colors[p.palette][size_t(p.low ? byte & 15 : byte >> 4)];
            const size_t at = size_t(p.y * 224 + p.x);
            auto &r = layers->planes[emu::FrameLayers::Screen1][at];
            r.tile = p.tile;
            r.palette = p.palette;
            r.color =
                uint16_t((color >> 16 & 255) / 17 << 8 | (color >> 8 & 255) / 17 << 4 | (color & 255) / 17);
            r.flags = emu::RasterPixel::Present | (p.mirror & 1 ? emu::RasterPixel::HFlip : 0) |
                      (p.mirror & 2 ? emu::RasterPixel::VFlip : 0);
            drawn.push_back(at);
        }
        chunk.lights = tags.visible(*layers, no_mask.data(), set, 0);
        for (auto &l : chunk.lights) {
            l.position[0] += float(ox);
            l.position[1] += float(oy);
            l.emission[3] /= LightTags::animation(l.type, 0);
        }
    }
    // Visible field actors use the actual latched OAM, including clipping and the old DMA
    // buffer. NPC records can already have advanced before that OAM is replaced. Decode records
    // only for the wider margin, where the native game has not allocated a card.
    struct Actor {
        int x = 0, y = 0;
        uint32_t record = 0;
        bool front = false, veil = false;
        std::array<int, 4> words{-1, -1, -1, -1};
    };
    std::map<std::tuple<int, int, uint32_t>, Actor> actors;
    const auto oam = w.presentation_sprites();
    const auto records = field_actor_records(w);
    std::set<uint32_t> native_records;
    for (size_t i = 0; i + 3 < oam.size(); i += 4) {
        const uint32_t record = records[i / 4];
        if (!record)
            continue;
        const int word = oam[i] | oam[i + 1] << 8, tile = word & 511;
        const int q = tile & 3;
        int x = (oam[i + 3] - q % 2 * 8) & 255, y = (oam[i + 2] - q / 2 * 8) & 255;
        if (x >= 240)
            x -= 256;
        if (y >= 240)
            y -= 256;
        auto &a = actors[{y, x, record}];
        a.x = x;
        a.y = y;
        a.record = record;
        a.front |= (word & 0x2000) != 0;
        a.words[size_t(q)] = word;
        native_records.insert(record);
    }
    for (const auto &card : veiled_terrain_cards(w))
        for (auto &[key, actor] : actors) {
            (void)key;
            if (actor.x == card.x && actor.y == card.y && actor.words[0] == card.words[0]) {
                actor.words = card.words;
                actor.veil = true;
            }
        }
    sprites_ = landmarks_;
    for (const auto &[key, a] : actors) {
        (void)key;
        uint64_t h = a.veil ? 2 : 1;
        for (int word : a.words) {
            h = hash_word(h, unsigned(word));
            if (word < 0)
                continue;
            h = hash_word(h, tiles[size_t(word & 511)]);
            for (int index = 1; index < 16; ++index)
                h = hash_word(h, w.presentation_palette_color(8 + (word >> 9 & 7), index, false, true));
        }
        auto found = actor_cache_.find(h);
        if (found == actor_cache_.end()) {
            Mode7Sprite sprite;
            sprite.id = h;
            sprite.width = sprite.height = 16;
            sprite.bottom = 15;
            auto pixels = std::make_shared<std::vector<uint32_t>>(256);
            auto paint = std::make_shared<std::vector<uint8_t>>(256);
            for (int q = 0; q < 4; ++q) {
                const int word = a.words[size_t(q)];
                if (word < 0)
                    continue;
                for (int y = 0; y < 8; ++y)
                    for (int x = 0; x < 8; ++x) {
                        const int sx = word & 0x4000 ? 7 - x : x, sy = word & 0x8000 ? 7 - y : y;
                        const int byte = w.ram[size_t(0x4000 + (word & 511) * 32 + sy * 4 + sx / 2)];
                        const int index = sx & 1 ? byte & 15 : byte >> 4;
                        if (!index)
                            continue;
                        const size_t to = size_t((q / 2 * 8 + y) * 16 + q % 2 * 8 + x);
                        (*pixels)[to] =
                            (a.veil && q >= 2 ? 0x80000000u : 0xFF000000u) |
                            rgb(w.presentation_palette_color(8 + (word >> 9 & 7), index, false, true));
                        (*paint)[to] = uint8_t((word >> 9 & 7) << 4 | index);
                    }
            }
            sprite.pixels = pixels;
            sprite.paint = paint;
            found = actor_cache_.emplace(h, std::move(sprite)).first;
        }
        auto sprite = found->second;
        sprite.record = a.record;
        sprite.x = float(a.x + 8) + scroll_[0];
        sprite.y = float(a.y + 15) + scroll_[1];
        sprite.front = a.front;
        sprite.veil = a.veil;
        sprites_.push_back(std::move(sprite));
    }
    // Script-only sprites: entries no field record owns stay on the field where the game
    // put them, as cards of touching entries, so the camera moves and scales them with what they act on.
    // The native table's order decides what they cover: entries after every actor's are under the actors,
    // the others over them (the game puts a column of light between the party and the boss it hides).
    {
        struct Piece {
            int x = 0, y = 0, word = 0, group = 0, index = 0;
            int drawn_out = 0; // beyond the screen: 1-4 repeats the entry's top, bottom, left or right pixels
        };
        std::array<std::vector<Piece>, 2> bands; // over the actors, under them
        size_t last_actor = oam.size();
        for (size_t i = 0; i + 3 < oam.size(); i += 4)
            if (records[i / 4])
                last_actor = i;
        for (size_t i = 0; i + 3 < oam.size(); i += 4) {
            const int word = oam[i] | oam[i + 1] << 8;
            int x = oam[i + 3], y = oam[i + 2];
            if (x >= 248)
                x -= 256;
            if (y >= 248)
                y -= 256;
            if (records[i / 4] || !field_script_sprite(word) || x >= 224 || y >= 144)
                continue;
            auto &band = bands[i < last_actor ? 0 : 1];
            band.push_back({x, y, word, int(band.size()), int(i / 4), 0});
        }
        for (size_t layer = 0; layer < bands.size(); ++layer) {
            auto &band = bands[layer];
            for (bool merged = true; merged;) {
                merged = false;
                for (auto &a : band)
                    for (auto &b : band)
                        if (a.group != b.group && std::abs(a.x - b.x) <= 8 && std::abs(a.y - b.y) <= 8) {
                            const int from = std::max(a.group, b.group), to = std::min(a.group, b.group);
                            for (auto &c : band)
                                if (c.group == from)
                                    c.group = to;
                            merged = true;
                        }
            }
            // The camera can show more of the field than the game's screen. Art drawn to run off the screen
            // (a column of light: three or more entries of a card exactly on one edge) goes on beyond it
            // with its edge pixels drawn out; anything else ends where the game drew it.
            const size_t drawn = band.size();
            for (size_t at = 0; at < drawn; ++at) {
                const Piece piece = band[at];
                const std::array<std::array<int, 3>, 4> edges{{{0, -8, piece.y == 0},
                                                               {0, 8, piece.y == 136},
                                                               {-8, 0, piece.x == 0},
                                                               {8, 0, piece.x == 216}}};
                for (int edge = 0; edge < 4; ++edge) {
                    const auto [dx, dy, at_edge] = edges[size_t(edge)];
                    const bool runs_off =
                        at_edge &&
                        std::count_if(band.begin(), band.begin() + long(drawn), [&](const Piece &other) {
                            return other.group == piece.group &&
                                   (dx ? other.x == piece.x : other.y == piece.y);
                        }) >= 3;
                    for (int step = 1; runs_off && step <= (dx ? 14 : 9); ++step)
                        band.push_back({piece.x + dx * step, piece.y + dy * step, piece.word, piece.group,
                                        piece.index, edge + 1});
                }
            }
            for (int group = 0; group < int(drawn); ++group) {
                int left = 1 << 20, top = 1 << 20, right = -(1 << 20), bottom = -(1 << 20);
                bool front = false;
                int order = 128;
                for (const auto &piece : band)
                    if (piece.group == group) {
                        order = std::min(order, piece.index);
                        left = std::min(left, piece.x);
                        top = std::min(top, piece.y);
                        right = std::max(right, piece.x + 8);
                        bottom = std::max(bottom, piece.y + 8);
                        front |= (piece.word & 0x2000) != 0;
                    }
                if (right < left)
                    continue;
                uint64_t h = 4 + layer;
                for (const auto &piece : band) {
                    if (piece.group != group)
                        continue;
                    h = hash_word(hash_word(h, unsigned(piece.x - left)), unsigned(piece.y - top));
                    h = hash_word(hash_word(h, unsigned(piece.word | piece.drawn_out << 16)),
                                  tiles[size_t(piece.word & 511)]);
                    for (int index = 1; index < 16; ++index)
                        h = hash_word(
                            h, w.presentation_palette_color(8 + (piece.word >> 9 & 7), index, false, true));
                }
                auto found = actor_cache_.find(h);
                if (found == actor_cache_.end()) {
                    Mode7Sprite sprite;
                    sprite.id = h;
                    sprite.width = right - left; // a whole number of tiles, so the card's center is whole
                    sprite.height = bottom - top;
                    sprite.bottom = sprite.height - 1;
                    auto pixels =
                        std::make_shared<std::vector<uint32_t>>(size_t(sprite.width * sprite.height));
                    auto paint = std::make_shared<std::vector<uint8_t>>(pixels->size());
                    // The earliest entry is in front: paint the table backwards.
                    for (auto piece = band.rbegin(); piece != band.rend(); ++piece) {
                        if (piece->group != group)
                            continue;
                        const int word = piece->word;
                        for (int y = 0; y < 8; ++y)
                            for (int x = 0; x < 8; ++x) {
                                const int from_x = piece->drawn_out == 3 ? 0 : piece->drawn_out == 4 ? 7 : x;
                                const int from_y = piece->drawn_out == 1 ? 0 : piece->drawn_out == 2 ? 7 : y;
                                const int sx = word & 0x4000 ? 7 - from_x : from_x;
                                const int sy = word & 0x8000 ? 7 - from_y : from_y;
                                const int byte = w.ram[size_t(0x4000 + (word & 511) * 32 + sy * 4 + sx / 2)];
                                const int index = sx & 1 ? byte & 15 : byte >> 4;
                                if (!index)
                                    continue;
                                const size_t to =
                                    size_t((piece->y - top + y) * sprite.width + piece->x - left + x);
                                (*pixels)[to] = 0xFF000000u | rgb(w.presentation_palette_color(
                                                                  8 + (word >> 9 & 7), index, false, true));
                                (*paint)[to] = uint8_t((word >> 9 & 7) << 4 | index);
                            }
                    }
                    sprite.pixels = pixels;
                    sprite.paint = paint;
                    found = actor_cache_.emplace(h, std::move(sprite)).first;
                }
                auto sprite = found->second;
                if (std::none_of(sprite.pixels->begin(), sprite.pixels->end(),
                                 [](uint32_t c) { return c >> 24; }))
                    continue;
                sprite.x = float(left) + float(sprite.width) / 2 + scroll_[0];
                sprite.y = float(bottom - 1) + scroll_[1];
                sprite.front = front;
                sprite.layer = layer ? -1 : 1;
                sprite.order = order;
                sprites_.push_back(std::move(sprite));
            }
        }
    }
    if (extend_npcs) {
        auto npcs = field_npc_sprites(w);
        for (auto &npc : npcs)
            if (!native_records.count(npc.record))
                sprites_.push_back(std::move(npc));
    }
    join_object_pieces(w, sprites_, actor_cache_);
    std::stable_sort(sprites_.begin(), sprites_.end(), mode7_depth_less);
    if (actor_cache_.size() > 256) {
        std::set<uint64_t> active;
        for (const auto &c : sprites_)
            if (!c.landmark)
                active.insert(c.id);
        for (auto i = actor_cache_.begin(); i != actor_cache_.end();) {
            if (!active.count(i->first))
                i = actor_cache_.erase(i);
            else
                ++i;
        }
    }
    return true;
}
void Mode7World::refresh_palettes(emu::Wsc &live) {
    if (map_ < 0 || surface_.words.empty())
        return;
    auto &w = Session::of(live).raster_field(live);
    bool ground = false;
    for (int p = 0; p < 16; ++p)
        for (int i = 0; i < 16; ++i) {
            const auto c = rgb(w.presentation_palette_color(p, i, false, false));
            auto &previous = surface_.palettes[size_t(p * 16 + i)];
            if (previous == c)
                continue;
            previous = c;
            ground |= ground_palettes_[size_t(p)];
            palettes_refreshed_ = true;
        }
    if (ground)
        ++surface_.palette_revision;
    std::array<uint32_t, 128> colors{};
    for (int p = 0; p < 8; ++p)
        for (int i = 1; i < 16; ++i)
            colors[size_t(p * 16 + i)] = rgb(w.presentation_palette_color(8 + p, i, false, true));
    for (auto &sprite : sprites_) {
        if (!sprite.paint || !sprite.pixels || sprite.paint->size() != sprite.pixels->size())
            continue;
        const auto &paint = *sprite.paint;
        const auto recolor = [&](size_t at) {
            return ((*sprite.pixels)[at] & 0xFF000000u) | colors[paint[at]];
        };
        size_t at = 0;
        while (at < paint.size() && (!paint[at] || recolor(at) == (*sprite.pixels)[at]))
            ++at;
        if (at == paint.size())
            continue;
        auto pixels = std::make_shared<std::vector<uint32_t>>(*sprite.pixels);
        for (; at < paint.size(); ++at)
            if (paint[at])
                (*pixels)[at] = recolor(at);
        // The renderer keys card textures on the id: new colors are new art.
        sprite.id =
            hash_bytes(reinterpret_cast<const uint8_t *>(pixels->data()), pixels->size() * 4) | 1ull << 63;
        sprite.pixels = std::move(pixels);
    }
}
// This is an identity raster backend, not another world reconstruction: update() owns all
// map/override/NPC decoding and this reads exactly the buffers uploaded by Mode7Renderer.
Mode7NeutralPicture mode7_neutral_picture(const Mode7Surface &surface,
                                          const std::vector<Mode7Sprite> &sprites,
                                          std::array<float, 2> scroll) {
    Mode7NeutralPicture out;
    if (surface.words.size() != 160 * 160 || surface.indices.size() != 8192)
        return out;
    out.pixels.resize(224 * 144);
    out.sources.resize(out.pixels.size());
    std::vector<bool> foreground(out.pixels.size());
    for (int y = 0; y < 144; ++y)
        for (int x = 0; x < 224; ++x) {
            const int wx = World::extend_x(int(std::floor(scroll[0])) + x) + 128;
            const int wy = World::extend_y(int(std::floor(scroll[1])) + y) + 128;
            const auto packed = surface.words[size_t(wy / 8 * 160 + wx / 8)];
            const unsigned word = ((packed >> 16) & 255) | (packed & 0xFF00);
            const unsigned tile = (word & 511) | (word & 8192 ? 512u : 0u);
            const int sx = word & 16384 ? 7 - (wx & 7) : wx & 7;
            const int sy = word & 32768 ? 7 - (wy & 7) : wy & 7;
            // Native byte storage is deliberately identical to the GPU's packed RGBA upload.
            const auto *bytes = reinterpret_cast<const uint8_t *>(surface.indices.data());
            const unsigned byte = bytes[tile * 32 + unsigned(sy * 4 + sx / 2)];
            const unsigned index = sx & 1 ? byte & 15 : byte >> 4;
            const size_t at = size_t(y * 224 + x);
            out.pixels[at] = surface.palettes[(word >> 9 & 15) * 16 + index];
            foreground[at] = (packed & 255) != 0;
        }
    // Identity specialization of mode7.frag.hlsl: tilt=0, distance=1, unfold=0,
    // 224x144 output, no IPS/shadows/water. Reconstruction and ordering remain update()'s.
    // Unlike mode7_picture's zero-tilt fast path, this resolves the actual GPU inputs.
    for (const auto &card : sprites) {
        if (!card.pixels || card.width <= 0 || card.height <= 0)
            continue;
        const float fx = card.x - scroll[0], fy = card.y - scroll[1];
        const float anchor_x = card.landmark ? fx : std::floor(fx + 0.5f);
        const float anchor_y = card.landmark ? fy : std::floor(fy + 0.5f);
        // Keep the renderer's virtual shaft dimensions and nearest texture resolve,
        // including its dynamically extended tower cards (extra < 0).
        const int extra = card.extra < 0 ? std::max(16, int(std::ceil(fy + 160))) : card.extra;
        const int height = card.height + (card.extra < 0 ? extra : 0);
        const int bottom = card.bottom + (card.extra < 0 ? extra : 0);
        const int below = height - extra - card.shaft_tip;
        const float top = anchor_y - float(bottom) + (card.landmark ? float(extra) : 0.f);
        const float end = anchor_y - float(bottom) + float(height);
        for (int y = 0; y < 144; ++y) {
            const float screen_y = float(y) + 0.5f;
            if (screen_y < top || screen_y >= end)
                continue;
            const float d = anchor_y - screen_y;
            const float source_d = d + (card.landmark && extra && d >= below ? float(extra) : 0);
            const int virtual_y = int(std::floor(float(bottom) - source_d));
            if (virtual_y < 0 || virtual_y >= height)
                continue;
            const int cy = virtual_y * card.height / height;
            for (int x = std::max(0, int(std::floor(anchor_x - card.width / 2)));
                 x < std::min(224, int(std::ceil(anchor_x + card.width / 2))); ++x) {
                const int cx = int(std::floor(float(x) + 0.5f - anchor_x + card.width / 2));
                if (cx < 0 || cx >= card.width)
                    continue;
                const size_t at = size_t(y * 224 + x);
                if (!card.front && foreground[at])
                    continue;
                const uint32_t pixel = (*card.pixels)[size_t(cy * card.width + cx)];
                const unsigned alpha = pixel >> 24;
                if (alpha < 64)
                    continue;
                if (alpha < 192)
                    out.pixels[at] = ((pixel & 0xFEFEFE) >> 1) + ((out.pixels[at] & 0xFEFEFE) >> 1);
                else
                    out.pixels[at] = pixel & 0xFFFFFF;
                out.sources[at] = card.landmark ? 1u : card.record;
            }
        }
    }
    return out;
}
bool Mode7World::water_at(float x, float y) const {
    if (surface_.words.empty() || surface_.water.empty())
        return false;
    const int px = World::extend_x(int(std::floor(x))) + 128;
    const int py = World::extend_y(int(std::floor(y))) + 128;
    const uint32_t packed = surface_.words[size_t((py / 8) * 160 + px / 8)];
    const int word = int(packed >> 16 & 255) | int(packed >> 8 & 255) << 8;
    const int tile = (word & 511) | (word & 0x2000 ? 512 : 0);
    const int tx = word & 0x4000 ? 7 - (px & 7) : px & 7;
    const int ty = word & 0x8000 ? 7 - (py & 7) : py & 7;
    return surface_.water[size_t(((word >> 14) * 256 + tile / 32 * 8 + ty) * 256 + tile % 32 * 8 + tx)] != 0;
}
PresentationScene Mode7World::project(const Mode7Camera &camera, bool snap, uint64_t frame) const {
    (void)snap; // Continuous perspective positions also drive the unsnapped scene effects.
    PresentationScene out;
    out.kind = scene_.kind;
    out.map = map_;
    const float cosine = std::cos(camera.tilt * kPi / 180);
    for (const auto &card : sprites_) {
        if (card.landmark)
            continue;
        auto p = camera.project(card.x - scroll_[0], card.y - scroll_[1]);
        if (p[2] <= 0 || p[0] < -32 || p[0] > 256 || p[1] < -32 || p[1] > 176)
            continue;
        out.actors.push_back({p[0], p[1], 6 * p[2], 2 * p[2] * p[2] * cosine});
    }
    for (const auto &chunk : light_chunks_)
        for (auto light : chunk.lights) {
            const float wx = light.position[0], wy = light.position[1];
            auto p = camera.project(wx - scroll_[0], wy - scroll_[1]);
            for (const auto &card : landmarks_) {
                if (wx < card.x - card.width / 2 || wx >= card.x + card.width / 2 ||
                    wy < card.y - card.height + card.extra || wy >= card.y)
                    continue;
                if (!std::any_of(card.footprint->begin(), card.footprint->end(), [&](const auto &cell) {
                        return wx >= cell.x * 16 && wx < (cell.x + 1) * 16 && wy >= cell.y * 16 &&
                               wy < (cell.y + 1) * 16;
                    }))
                    continue;
                const float d = card.y - wy;
                const auto foot =
                    camera.project(wx - scroll_[0], card.y - scroll_[1] - d * (1 - camera.amount));
                p = {foot[0], foot[1] - d * camera.amount * foot[2], foot[2]};
                break;
            }
            if (p[2] <= 0 || p[0] < -64 || p[0] > 288 || p[1] < -64 || p[1] > 208)
                continue;
            light.position[0] = p[0];
            light.position[1] = p[1];
            light.position[2] *= p[2];
            light.extent[0] *= p[2];
            light.extent[1] *= p[2] * cosine;
            light.emission[3] *= LightTags::animation(light.type, frame);
            out.lights.push_back(light);
        }
    return out;
}
} // namespace sawgod::game
