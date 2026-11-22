// Selects extracted WSC or GB graphics while preserving the native game state.
#include "game/gfx_swap.h"
#include "engine/cpu_scope.h"
#include "game/gb_battle_effects_native.h"

#include "emu/wsc.h"
#include "engine/indexed_png.h"
#include "game/bank34.h"
#include "game/machine.h"
#include "game/session.h"
#include "game/text.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <tuple>
#include <unordered_map>

namespace sawgod::game {

namespace {

constexpr uint16_t kMapId = 0xD0DC;                    // current_map
constexpr uint16_t kPackedProgress = 0xD0A6;           // the story counters, two nibbles a byte (even: high)
constexpr uint16_t kSetSeg = 0x1000, kSetOff = 0x1900; // the field's metatile set: 256 x 4 tile words
constexpr uint16_t kTiles = 0x8000;                    // tile 0x200 (4bpp packed, 32 bytes a tile)
constexpr uint16_t kScreen1 = 0x1000;                  // the field's screen 1 (32 x 32 words)
constexpr uint16_t kPaletteRam = 0xFE00, kPaletteTarget = 0xC600; // palettes; the fades' target palettes
constexpr int kPalette = 6; // the field's GB greys (a background palette the field doesn't use)
constexpr uint16_t kGreys[4] = {0xFFF, 0xAAA, 0x555, 0x000};   // GB shades 0-3 (ORIJINARU's palette)
constexpr uint16_t kSpriteGreys[4] = {0, 0xFFF, 0xAAA, 0x000}; // GB sprite colors 1-3 (0: transparent)
constexpr int kSpritePalette = 8;                              // the player's (sprite palette 0)
constexpr uint16_t kPlayerTiles = 0x4000 + 0x10 * 32, kPlayerTileBytes = 8 * 32; // its two 4-tile slots
constexpr uint16_t kTravelMode = 0xD0D6, kLeaderType = 0xD005;                   // 0: on foot
constexpr uint16_t kPose = 0xD521, kPoseFrame = 0xD526; // draw_player_sprite's pose: low 6 bits (+ frame)
constexpr uint32_t kTitleLogoPalette = 0x2F6050, kTitleTextPalette = 0x2F7A08; // ROM: the WSC title's
constexpr uint16_t kFontSeg = 0x9000, kFontOff = 0x0080; // the WSC font (32 bytes a code) and, at 9000:0000,
constexpr uint16_t kWindowPaletteOff = 0x0000;           // the window palettes
constexpr int kFontTile = 0x100, kFontCodes = 0xE0;
// the window palette in GB greys: the fill (9-11) white, the text (2) black, its shadow (1) white, the
// frame's grey (7) and the rest by brightness
constexpr uint16_t kWindowGreys[16] = {0,     0xFFF, 0x000, 0x555, 0x555, 0xAAA, 0x000, 0x555,
                                       0xAAA, 0xFFF, 0xFFF, 0xFFF, 0xAAA, 0xAAA, 0x555, 0x555};
constexpr uint8_t kShadeIndex[4] = {10, 8, 7, 2}; // GB shade -> a window palette index that has that grey
// the WSC's poses (down, up, left, right; 2 frames each) as GB poses (down, up, side facing left, side
// stepping facing right), mirrored or not: the GB walks down and up by mirroring, and its two side poses face
// opposite ways, so one of them is mirrored for each direction
constexpr struct {
    int pose;
    bool mirror;
} kGbPose[8] = {{0, false}, {0, true}, {1, false}, {1, true}, {2, false}, {3, true}, {2, true}, {3, false}};
// Extra script gestures use the room-82 body for lying poses; otherwise the GB character
// stands facing the gesture's way while its native timing runs.
// WSC walking poses: 0 down, 2 up, 4 left, 6 right; the internal pose 8 selects the body.
// Standing facings 8..11 use one frame per direction; unlisted gestures face down.
int gb_walk_pose(int set, int pose) {
    if ((set < 29 && pose == 17) || (set == 38 && pose == 16) || (set == 36 && pose == 13))
        return 8; // lying down: the GB's own body, as it draws it (never rotated)
    // Poses 8..11 are standing down/up/left/right, in the same order as walking pairs.
    if (pose >= 8 && pose <= 11)
        return (pose - 8) * 2;
    return pose < 8 ? pose : 0;
}
constexpr uint16_t kGroupRecords = 0xD500, kGroupTypes = 0xD6D5, kGroupCount = 0xD6D4;
constexpr uint16_t kBlankCell = 0x01FF;     // an empty screen-2 cell
constexpr uint16_t kDissolveMasks = 0xBC18; // E000:BC18 + step: dissolve_enemy_group's mask for each pass

uint16_t group_tile(int group) { return uint16_t(0x300 + 0x28 * group); }
uint16_t cell(int col, int row) { return screen_map_offset(1, uint16_t(row << 8 | (col & 0xFF))); }
uint16_t group_word(int group, int i) {
    return uint16_t(0x2000 | (4 + group) << 9 | ((group_tile(group) + i) & 0x1FF));
}
uint16_t ram16(const emu::Wsc &w, uint16_t at) { return uint16_t(w.ram[at] | w.ram[uint16_t(at + 1)] << 8); }

} // namespace

bool GfxSwap::load_field_map(int id, const std::string &root, std::string *error) {
    return load_field_map(
        id,
        [&](const std::string &file) {
            std::ifstream f(root + "/" + file, std::ios::binary);
            return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
        },
        error);
}

bool GfxSwap::load_field_map(int id, const std::function<std::vector<uint8_t>(const std::string &)> &read,
                             std::string *error) {
    error->clear();
    char name[32];
    std::snprintf(name, sizeof name, "gb/%03d", id);
    const std::string base = name;
    const auto png = read(base + "_metatiles.png"), lookup = read(base + "_lookup.csv");
    if (png.empty() && lookup.empty())
        return false;
    if (png.empty() || lookup.empty()) {
        *error = base + ": incomplete map resources";
        return false;
    }
    if (!add_map(id, png, std::string(lookup.begin(), lookup.end()), error))
        return false;
    const auto cells = read(base + "_cells.csv");
    if (!cells.empty() && !add_cells(id, std::string(cells.begin(), cells.end()), error))
        return false;
    // A separate visibility mask leaves the pinned source triplet unchanged.
    // Coordinates have no visible WSC detail, including paths hidden by darkness.
    const auto hidden = read(base + "_hidden.csv");
    if (!hidden.empty()) {
        const auto rows = parse_csv(std::string(hidden.begin(), hidden.end()));
        if (rows.empty() || rows[0] != std::vector<std::string>{"x", "y"}) {
            *error = "hidden cells: needs x,y columns";
            return false;
        }
        std::vector<int> positions;
        for (size_t r = 1; r < rows.size(); ++r) {
            if (rows[r].size() != 2) {
                *error = "hidden cells: needs two integers";
                return false;
            }
            long v[2];
            for (int c = 0; c < 2; ++c) {
                char *end = nullptr;
                v[c] = std::strtol(rows[r][size_t(c)].c_str(), &end, 10);
                if (end == rows[r][size_t(c)].c_str() || *end || v[c] < -8 || v[c] > 71) {
                    *error = "hidden cells: invalid coordinate";
                    return false;
                }
            }
            positions.push_back(int((v[1] & 255) * 256 + (v[0] & 255)));
        }
        Map &map = maps_.at(id);
        const std::array<uint8_t, 256> white{};
        auto found = std::find(map.metatiles.begin(), map.metatiles.end(), white);
        if (found == map.metatiles.end()) {
            if (map.metatiles.size() == 128) {
                *error = "hidden cells: no room for white art";
                return false;
            }
            map.metatiles.push_back(white);
            found = map.metatiles.end() - 1;
        }
        const int g = int(found - map.metatiles.begin());
        for (int p : positions)
            map.by_cell[p] = g;
    }
    return true;
}

bool GfxSwap::add_map(int id, const std::vector<uint8_t> &png, const std::string &csv, std::string *error) {
    Map m;
    m.gb.fill(-1);
    IndexedImage img;
    if (!decode_indexed_png(png, {0xFFFFFF, 0xAAAAAA, 0x555555, 0x000000}, &img, error))
        return false;
    if (img.height != 16 || img.width % 16 || img.width == 0 || img.width > 16 * 128)
        return *error = "metatiles: 16 px tall, 16 px per metatile (at most 128)", false;
    for (int g = 0; g < img.width / 16; ++g) {
        std::array<uint8_t, 256> px{};
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                px[size_t(y * 16 + x)] = img.pixels[size_t(y * img.width + g * 16 + x)] & 3;
        m.metatiles.push_back(px);
    }
    const auto rows = parse_csv(csv);
    if (rows.empty() || rows[0].size() < 2 || rows[0][0] != "wsc" || rows[0][1] != "gb")
        return *error = "lookup: needs wsc,gb columns", false;
    for (size_t r = 1; r < rows.size(); ++r) {
        if (rows[r].size() < 2)
            continue;
        const long wsc = std::strtol(rows[r][0].c_str(), nullptr, 10),
                   gb = std::strtol(rows[r][1].c_str(), nullptr, 10);
        if (wsc < 0 || wsc > 255 || gb < 0 || gb >= long(m.metatiles.size()))
            return *error = "lookup: row " + std::to_string(r + 1) + " out of range", false;
        if (rows[0].size() >= 3 && rows[0][2] == "type" && rows[r].size() >= 3 && !rows[r][2].empty()) {
            char *end = nullptr;
            const long type = std::strtol(rows[r][2].c_str(), &end, 10);
            if (*end || type < 0 || type >= 32)
                return *error = "lookup: attribute type out of range", false;
            m.by_type[int(wsc * 32 + type)] = int(gb);
        } else {
            m.gb[size_t(wsc)] = int(gb);
        }
    }
    maps_[id] = std::move(m);
    install_hooks(); // track field cells even while the second picture is disabled
    return true;
}

bool GfxSwap::add_cells(int id, const std::string &csv, std::string *error) {
    const auto map = maps_.find(id);
    if (map == maps_.end())
        return *error = "cells: load the map's metatiles first", false;
    const auto rows = parse_csv(csv);
    if (rows.empty() || rows[0] != std::vector<std::string>{"x", "y", "gb"})
        return *error = "cells: needs x,y,gb columns", false;
    std::map<int, int> cells;
    for (size_t r = 1; r < rows.size(); ++r) {
        if (rows[r].size() != 3)
            return *error = "cells: row needs three integers", false;
        long values[3];
        for (int c = 0; c < 3; ++c) {
            char *end = nullptr;
            values[c] = std::strtol(rows[r][size_t(c)].c_str(), &end, 10);
            if (end == rows[r][size_t(c)].c_str() || *end)
                return *error = "cells: invalid integer", false;
        }
        const auto [x, y, g] = std::tuple{values[0], values[1], values[2]};
        if (x < -8 || x > 71 || y < -8 || y > 71 || g < 0 || g >= long(map->second.metatiles.size()))
            return *error = "cells: coordinate or metatile out of range", false;
        if (!cells.emplace(int((y & 255) * 256 + (x & 255)), int(g)).second)
            return *error = "cells: duplicate coordinate", false;
    }
    map->second.by_cell = std::move(cells);
    return true;
}

bool GfxSwap::add_sprites(const std::vector<uint8_t> &png, const std::string &csv, std::string *error) {
    IndexedImage img;
    if (!decode_indexed_png(png, {0xFF00FF, 0xFFFFFF, 0xAAAAAA, 0x000000}, &img, error))
        return false;
    if (img.width != 64 || img.height % 16 || img.height == 0)
        return *error = "sprites: 64 px wide (4 poses), 16 px per sprite", false;
    std::vector<std::array<uint8_t, 4 * 256>> sprites(size_t(img.height / 16));
    for (size_t s = 0; s < sprites.size(); ++s)
        for (int pose = 0; pose < 4; ++pose)
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    sprites[s][size_t(pose * 256 + y * 16 + x)] =
                        img.pixels[(s * 16 + size_t(y)) * 64 + size_t(pose * 16 + x)] & 3;
    std::array<int, 256> of_type;
    of_type.fill(-1);
    const auto rows = parse_csv(csv);
    if (rows.empty() || rows[0].size() < 2 || rows[0][0] != "type" || rows[0][1] != "gb")
        return *error = "sprite types: needs type,gb columns", false;
    for (size_t r = 1; r < rows.size(); ++r) {
        if (rows[r].size() < 2)
            continue;
        const long type = std::strtol(rows[r][0].c_str(), nullptr, 10),
                   gb = std::strtol(rows[r][1].c_str(), nullptr, 10);
        if (type < 0 || type > 255 || gb < 0 || gb >= long(sprites.size()))
            return *error = "sprite types: row " + std::to_string(r + 1) + " out of range", false;
        of_type[size_t(type)] = int(gb);
    }
    sprites_ = std::move(sprites);
    sprite_of_type_ = of_type;
    sprite_of_set_.fill(-1);
    install_hooks();
    return true;
}

bool GfxSwap::add_objects(const std::vector<uint8_t> &png, const std::string &csv, std::string *error) {
    IndexedImage img;
    if (!decode_indexed_png(png, {0xFF00FF, 0xFFFFFF, 0xAAAAAA, 0x000000}, &img, error))
        return false;
    if (img.width != 128 || img.height == 0 || img.height % 16)
        return *error = "objects: 128 px wide, 16 px per row", false;
    const auto rows = parse_csv(csv);
    if (rows.empty() || rows[0] != std::vector<std::string>{"set", "row", "poses"})
        return *error = "objects: needs set,row,poses columns", false;
    decltype(objects_) objects;
    for (size_t r = 1; r < rows.size(); ++r) {
        if (rows[r].size() != 3)
            return *error = "objects: needs three integers", false;
        int v[3];
        for (int c = 0; c < 3; ++c) {
            char *end = nullptr;
            const long n = std::strtol(rows[r][size_t(c)].c_str(), &end, 10);
            if (end == rows[r][size_t(c)].c_str() || *end || n < 0 || n > 255)
                return *error = "objects: invalid integer", false;
            v[c] = int(n);
        }
        if (v[1] >= img.height / 16 || v[2] < 1 || v[2] > 8 || objects.count(v[0]))
            return *error = "objects: invalid row, pose count or duplicate set", false;
        auto &poses = objects[v[0]];
        poses.resize(size_t(v[2]));
        for (int pose = 0; pose < v[2]; ++pose)
            for (int q = 0; q < 4; ++q)
                for (int y = 0; y < 8; ++y)
                    for (int x = 0; x < 8; x += 2) {
                        const auto px = [&](int xx) {
                            return img.pixels[size_t((v[1] * 16 + q / 2 * 8 + y) * 128 + pose * 16 +
                                                     q % 2 * 8 + xx)];
                        };
                        poses[size_t(pose)][size_t(q * 32 + y * 4 + x / 2)] = uint8_t(px(x) << 4 | px(x + 1));
                    }
    }
    objects_ = std::move(objects);
    install_hooks();
    return true;
}

// The GB's lying body (ORIJINARU room 82: the children's bodies and the Nuclear Bomb diary), drawn as the GB
// draws it for every WSC lying pose. Only pixel placement changes; native OAM/state stays intact.
bool GfxSwap::add_corpse(const std::vector<uint8_t> &png, std::string *error) {
    IndexedImage img;
    if (!decode_indexed_png(png, {0xFF00FF, 0xFFFFFF, 0xAAAAAA, 0x000000}, &img, error))
        return false;
    if (img.width != 16 || img.height != 16)
        return *error = "corpse: needs a 16x16 sprite", false;
    for (int q = 0; q < 4; ++q)
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; x += 2) {
                const auto px = [&](int xx) {
                    return img.pixels[size_t((q / 2 * 8 + y) * 16 + q % 2 * 8 + xx)];
                };
                corpse_[size_t(q * 32 + y * 4 + x / 2)] = uint8_t(px(x) << 4 | px(x + 1));
            }
    has_corpse_ = true;
    install_hooks();
    return true;
}

bool GfxSwap::add_hand(const std::vector<uint8_t> &png, std::string *error) {
    IndexedImage img;
    if (!decode_indexed_png(png, {0xFF00FF, 0xFFFFFF, 0xAAAAAA, 0x000000}, &img, error))
        return false;
    if (img.width != 16 || img.height != 16)
        return *error = "hand: 16 x 16", false;
    // Sprite colors use brightness quantization, independently of the window text swap.
    // Native palette 15: 2 white, 12 light gray, 1 black (including its native fades).
    constexpr uint8_t colors[] = {0, 2, 12, 1};
    for (int q = 0; q < 4; ++q)
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; x += 2) {
                const auto px = [&](int xx) {
                    return colors[img.pixels[size_t((q / 2 * 8 + y) * 16 + q % 2 * 8 + xx)]];
                };
                hand_[size_t(q * 32 + y * 4 + x / 2)] = uint8_t(px(x) << 4 | px(x + 1));
            }
    has_hand_ = true;
    install_hooks();
    return true;
}

bool GfxSwap::add_font(const std::vector<uint8_t> &png, std::string *error) {
    IndexedImage img;
    if (!decode_indexed_png(png, {0xFFFFFF, 0xAAAAAA, 0x555555, 0x000000, 0xFF00FF}, &img, error))
        return false;
    if (img.width != 128 || img.height != 112)
        return *error = "font: 128 x 112 (codes 0x00-0xDF, 16 a row)", false;
    for (int code = 0; code < kFontCodes; ++code) {
        const auto px = [&](int x, int y) {
            return img.pixels[size_t(((code / 16) * 8 + y) * 128 + (code % 16) * 8 + x)];
        };
        font_has_[size_t(code)] = px(0, 0) <= 3;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; x += 2)
                font_[size_t(code)][size_t(y * 4 + x / 2)] =
                    uint8_t(kShadeIndex[px(x, y) & 3] << 4 | kShadeIndex[px(x + 1, y) & 3]);
    }
    has_font_ = true;
    return true;
}

bool GfxSwap::add_monsters(const std::vector<uint8_t> &png, const std::string &pics_csv,
                           const std::string &types_csv, std::string *error) {
    IndexedImage img;
    if (!decode_indexed_png(png, {0xFF00FF, 0xFFFFFF, 0xAAAAAA, 0x555555, 0x000000}, &img, error))
        return false;
    if (img.width != 8 * 64 || img.height % 64)
        return *error = "monsters: 512 px wide (8 cells of 64 x 64 a row)", false;
    std::map<int, Pic> pics;
    const auto rows = parse_csv(pics_csv);
    if (rows.empty() || rows[0].size() < 3 || rows[0][0] != "pic" || rows[0][1] != "width" ||
        rows[0][2] != "height")
        return *error = "monster pics: needs pic,width,height columns", false;
    for (size_t r = 1; r < rows.size(); ++r) {
        if (rows[r].size() < 3)
            continue;
        const long pic = std::strtol(rows[r][0].c_str(), nullptr, 10),
                   w = std::strtol(rows[r][1].c_str(), nullptr, 10),
                   h = std::strtol(rows[r][2].c_str(), nullptr, 10);
        if (pic < 0 || (pic / 8 + 1) * 64 > img.height || w < 1 || w > 8 || h < 1 || h > 8)
            return *error = "monster pics: row " + std::to_string(r + 1) + " out of range", false;
        Pic &p = pics[int(pic)];
        p.w = int(w), p.h = int(h);
        p.px.resize(size_t(w * 8 * h * 8));
        for (int y = 0; y < h * 8; ++y)
            for (int x = 0; x < w * 8; ++x)
                p.px[size_t(y * w * 8 + x)] =
                    uint8_t(img.pixels[size_t(((pic / 8) * 64 + y) * img.width + (pic % 8) * 64 + x)] % 5);
    }
    std::array<int, 256> of_type;
    of_type.fill(-1);
    const auto types = parse_csv(types_csv);
    if (types.empty() || types[0].size() < 2 || types[0][0] != "type" || types[0][1] != "pic")
        return *error = "monster types: needs type,pic columns", false;
    for (size_t r = 1; r < types.size(); ++r) {
        if (types[r].size() < 2)
            continue;
        const long type = std::strtol(types[r][0].c_str(), nullptr, 10),
                   pic = std::strtol(types[r][1].c_str(), nullptr, 10);
        if (type < 0 || type > 255 || !pics.count(int(pic)))
            return *error = "monster types: row " + std::to_string(r + 1) + " out of range", false;
        of_type[size_t(type)] = int(pic);
    }
    pics_ = std::move(pics);
    pic_of_type_ = of_type;
    install_hooks();
    return true;
}

bool GfxSwap::add_title(const std::vector<uint8_t> &png, std::string *error) {
    IndexedImage img;
    if (!decode_indexed_png(png, {0xFFFFFF, 0xAAAAAA, 0x555555, 0x000000}, &img, error))
        return false;
    if (img.width != 224 || img.height != 144)
        return *error = "title: 224 x 144", false;
    title_.resize(img.pixels.size());
    for (size_t i = 0; i < img.pixels.size(); ++i)
        title_[i] = kGreys[img.pixels[i] & 3];
    return true;
}

void GfxSwap::install_hooks() {
    active_ = this;
    Machine::player_sprite_hook = &GfxSwap::player_hook;
    Machine::npc_sprite_hook = &GfxSwap::npc_hook;
    Machine::field_cell_hook = &GfxSwap::field_cell_hook;
    Machine::enemy_portrait_hook = &GfxSwap::portrait_hook;
    Machine::enemy_dissolve_hook = &GfxSwap::dissolve_hook;
}

GfxSwap::~GfxSwap() {
    if (active_ == this) {
        active_ = nullptr;
        Machine::player_sprite_hook = nullptr;
        Machine::npc_sprite_hook = nullptr;
        Machine::field_cell_hook = nullptr;
        Machine::enemy_portrait_hook = nullptr;
        Machine::enemy_dissolve_hook = nullptr;
    }
}

std::map<int, std::array<uint16_t, 4>> GfxSwap::field_world_words(const emu::Wsc &w) {
    std::map<int, std::array<uint16_t, 4>> out;
    if (!active_ || active_->field_owner_ != &w || active_->tracked_map_ != w.ram[kMapId])
        return out;
    constexpr uint16_t offsets[] = {0, 2, 0x40, 0x42};
    for (const auto &[at, cell] : active_->field_cells_) {
        if (cell.x < -8 || cell.x > 71 || cell.y < -8 || cell.y > 71)
            continue;
        std::array<uint16_t, 4> words{};
        bool world = true;
        for (int q = 0; q < 4; ++q) {
            words[size_t(q)] = ram16(w, uint16_t(at + offsets[q]));
            // A menu/scene can reuse this address with other perfectly valid field words.
            // Only the draw hook establishes which world cell owns the ring slot.
            world &= words[size_t(q)] == cell.words[size_t(q)] && (words[size_t(q)] & 0x2000) &&
                     (words[size_t(q)] >> 9 & 15) < 4;
        }
        if (world)
            out[(cell.y + 8) * 80 + cell.x + 8] = words;
    }
    return out;
}

void GfxSwap::field_cell_hook(Machine &m, uint16_t at, uint8_t id, int type, int x, int y) {
    if (!active_)
        return;
    auto &s = *active_;
    const int map = m.wsc().ram[kMapId];
    if (s.field_owner_ != &m.wsc() || s.tracked_map_ != map) {
        s.field_cells_.clear();
        s.field_owner_ = &m.wsc();
        s.tracked_map_ = map;
    }
    FieldCell entry;
    entry.id = id;
    entry.type = type;
    entry.x = x;
    entry.y = y;
    constexpr uint16_t offsets[] = {0, 2, 0x40, 0x42};
    for (int q = 0; q < 4; ++q) {
        entry.words[size_t(q)] = ram16(m.wsc(), uint16_t(at + offsets[q]));
        entry.foreground[size_t(q)] =
            ram16(m.wsc(), uint16_t(at + 0x800 + offsets[q])) == entry.words[size_t(q)];
    }
    s.field_cells_[at] = entry;
}

void GfxSwap::set_enabled(emu::Wsc &w, bool on) {
    if (on == enabled_)
        return;
    enabled_ = on;
    battle_animation_ = 0;
    w.set_second(on); // a fresh shadow (on), or none (off): nothing of ours left anywhere
    field_map_ = -1;
    field_overlay_.clear();
    sparse_tiles_.clear();
    player_shown_ = false;
    npc_tiles_.clear();
    npc_palettes_.clear();
    for (Portrait &p : portraits_)
        p.painted = false;
    icons_ = {};
    backdrop_swaps_.clear();
    w.palette_swaps.clear();
    w.screen1_image_swap = -1;
    w.upper_palette_greys = on;
    w.second_bg_zero_palette = on ? kPalette : -1; // GB shade 0 = index 1 (shade s -> color s + 1)
    if (on) {
        install_hooks();
        install_battle_hooks(w);
        intro_.attach(w);
        intro_.draw_map = [this](int id, int x, int y, auto &image, int top) {
            draw_intro_map(id, x, y, image, top);
        };
        frame(w);
    }
}

bool GfxSwap::load_battle_effects(const std::function<std::vector<uint8_t>(const std::string &)> &read,
                                  std::string *error) {
    const auto actions = read("gb/effect_actions.csv"), rows = read("gb/effect_rows.csv");
    if (actions.empty() || rows.empty())
        return false;
    const auto fail = [&](const std::string &message) {
        battle_effects_.clear();
        if (error)
            *error = message;
        return false;
    };
    battle_effects_.clear();
    battle_effect_of_action_.fill(0);
    battle_effect_flags_.fill(0);
    try {
        for (const auto &r : parse_csv(std::string(actions.begin(), actions.end()))) {
            if (r.size() != 3 || r[0] == "action")
                continue;
            const int id = std::stoi(r[0]);
            if (id < 0 || id > 255)
                return fail("effect action out of range");
            battle_effect_of_action_[size_t(id)] = std::stoi(r[1]);
            battle_effect_flags_[size_t(id)] = std::stoi(r[2]);
        }
        for (const auto &r : parse_csv(std::string(rows.begin(), rows.end()))) {
            if (r.size() != 11 || r[0] == "effect")
                continue;
            int v[11];
            for (int i = 0; i < 11; ++i)
                v[i] = std::stoi(r[size_t(i)]);
            if (v[0] < 1 || v[0] > 255 || v[1] < 0 || v[1] >= 2048 || v[2] < 0 || v[3] >= 64 || v[2] > v[3])
                return fail("effect frame/row out of range");
            auto &frames = battle_effects_[v[0]].frames;
            frames.resize(std::max(frames.size(), size_t(v[1] + 1)));
            for (int y = v[2]; y <= v[3]; ++y) {
                auto &row = frames[size_t(v[1])].rows[size_t(y)];
                row.dx = v[4];
                row.dy = v[5];
                row.fill = v[6];
                for (int c = 0; c < 4; ++c)
                    row.colors[size_t(c)] = uint8_t(v[7 + c]);
            }
        }
        for (auto &[id, effect] : battle_effects_) {
            IndexedImage atlas;
            if (!decode_indexed_png(read("gb/effect_" + std::to_string(id) + ".png"),
                                    {0xFF00FF, 0xFFFFFF, 0xAAAAAA, 0x555555, 0x000000}, &atlas, error))
                return false;
            if (atlas.width != 224 * 8 || atlas.height < int((effect.frames.size() + 7) / 8) * 64)
                return fail("effect atlas dimensions");
            for (size_t f = 0; f < effect.frames.size(); ++f)
                for (int y = 0; y < 64; ++y)
                    for (int x = 0; x < 224; ++x)
                        effect.frames[f].sprites[size_t(y * 224 + x)] =
                            atlas.pixels[(f / 8 * 64 + size_t(y)) * size_t(atlas.width) + f % 8 * 224 +
                                         size_t(x)];
        }
    } catch (const std::exception &e) {
        return fail(std::string("invalid GB effect resource: ") + e.what());
    }

    return true;
}

void GfxSwap::begin_battle_effect(Machine &m, const hooks::Args &a) {
    effect_id_ = 0;
    if (effect_played_ || a.c < 0 || a.c > 255 || m.wsc().ram[0xDBFF] < 4)
        return;
    bool known_landing = false;
    for (auto [action, entry] : kGbNativeLandings) {
        if (action != a.c)
            continue;
        if (entry != a.a)
            return;
        known_landing = true;
        break;
    }
    // Equipment and monster abilities can alias the same action (for example C6
    // and 6A both cast Fire). A new alias must still wait for the landing phase;
    // the charge/projectile routines never consume its one GB effect timeline.
    if (!known_landing)
        switch (a.a) {
        case 0x370DC0: // missile projectile
        case 0x371270: // missile trails
        case 0x372690: // thrown rock
        case 0x373780: // scripture opening
        case 0x373970: // scripture charge
        case 0x374140: // whirlpool opening
        case 0x3747D0: // rain droplets
        case 0x374F00: // riddle opening
        case 0x375100: // riddle charge
        case 0x375490: // gaze opening
        case 0x3765E0: // telekinesis approach
        case 0x378130: // spell casting
        case 0x3784B0: // cloud opening
        case 0x3788B0: // thunder opening
        case 0x378EA0: // fire opening
        case 0x379AE0: // scripture closing
            return;
        default:
            break;
        }
    const int id = battle_effect_of_action_[size_t(a.c)];
    if (!(battle_effect_flags_[size_t(a.c)] & 0x80) || !battle_effects_.contains(id))
        return;
    effect_id_ = id;
    effect_action_ = a.c;
    effect_start_ = m.wsc().frame_count;
    effect_duration_ = 1;
    effect_target_ = m.wsc().ram[0xDC00];
    for (auto [entry, duration] : kGbNativeDurations)
        if (entry == a.a)
            effect_duration_ = std::max(1, duration);
    effect_played_ = true;
    shadow_battlefield(m.wsc());
}

void GfxSwap::apply_battle_effect(emu::Wsc &w) {
    if (!effect_id_ || !battle_effects_.contains(effect_id_))
        return;
    const auto &frames = battle_effects_.at(effect_id_).frames;
    if (frames.empty())
        return;
    const uint64_t age = w.frame_count - effect_start_;
    const auto &frame =
        frames[std::min(frames.size() - 1, size_t(age * frames.size() / uint64_t(effect_duration_)))];
    auto &image = w.second_scene_image;
    const auto before = image;
    for (int y = 0; y < 80; ++y)
        for (int x = 0; x < 224; ++x) {
            const auto &row = frame.rows[size_t(y * 64 / 80)];
            int c = row.fill;
            if (c < 0) {
                const int sx = x + row.dx, sy = y + row.dy;
                uint16_t px = 0xFFF;
                if (sx >= 0 && sx < 224 && sy >= 0 && sy < 80)
                    px = before[size_t(sy * 224 + sx)];
                c = 0;
                for (int shade = 0; shade < 4; ++shade)
                    if (kGreys[shade] == px)
                        c = shade;
                c = row.colors[size_t(c)];
            }
            image[size_t(y * 224 + x)] = kGreys[std::clamp(c, 0, 3)];
        }
    const bool all = (battle_effect_flags_[size_t(effect_action_)] & 0x40) != 0;
    for (int g = 0; g < 3; ++g) {
        const auto &p = portraits_[size_t(g)];
        if (!p.painted || (!all && g != (effect_target_ < 3 ? effect_target_ : 0)))
            continue;
        const int dx = p.gx * 8 + p.gw * 4 - 112, dy = p.gy * 8 - 4 + p.gh * 4 - 28;
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 224; ++x) {
                const int c = frame.sprites[size_t(y * 224 + x)];
                const int sx = x + dx, sy = y + dy;
                if (c > 0 && c <= 4 && sx >= 0 && sx < 224 && sy >= 0 && sy < 80)
                    image[size_t(sy * 224 + sx)] = kGreys[c - 1];
            }
    }
}

void GfxSwap::install_battle_hooks(emu::Wsc &w) {
    if (battle_owner_ == &w && !battle_subscriptions_.empty() && battle_subscriptions_.front().active())
        return;
    battle_subscriptions_.clear();
    battle_owner_ = &w;
    auto &session = Session::of(w);
    battle_generation_ = session.generation();
    auto &registry = session.hooks();
    battle_subscriptions_.push_back(
        registry.subscribe(hooks::Event::BattleDefeatEffect, [this](Machine &m, const hooks::Args &a) {
            auto &w = m.wsc();
            if (a.a == 1) {
                w.second_scene_raster = emu::Wsc::SceneRaster{{-1, 4}};
            } else if (a.a == 3) {
                w.second_scene_raster = emu::Wsc::SceneRaster{{252, 4}, a.b};
            } else if (a.a == 2) {
                w.second_scene_raster = emu::Wsc::SceneRaster{};
            } else {
                w.second_scene_raster.reset();
                if (a.b >= 0 && a.b < 3)
                    // Hide dead art without releasing palette/tile ownership still used by rewards.
                    portraits_[size_t(a.b)].defeated = true;
            }
        }));
    battle_subscriptions_.push_back(
        registry.subscribe(hooks::Event::FieldViewRebased, [this](Machine &m, const hooks::Args &a) {
            if (field_owner_ != &m.wsc())
                return;
            // The native service display copies the scroll-selected ring into a new origin.
            // Move its world-cell ownership with it, including cells whose WSC words alias.
            const int dx = (uint8_t(a.a - 8) & 0xF0) / 8;
            const int dy = ((a.a >> 8) & 0xF0) / 8;
            std::map<uint16_t, FieldCell> rebased;
            for (const auto &[at, cell] : field_cells_) {
                const int index = (at - kScreen1) / 2;
                const int x = (index % 32 - dx + 32) % 32;
                const int y = (index / 32 - dy + 32) % 32;
                auto moved = cell;
                // The two native screens can have distinct scrolls.
                if (a.a != a.b)
                    moved.foreground.fill(false);
                rebased[uint16_t(kScreen1 + (y * 32 + x) * 2)] = moved;
            }
            field_cells_ = std::move(rebased);
            if (enabled_ && m.wsc().second())
                shadow_field(m);
        }));
    battle_subscriptions_.push_back(
        registry.subscribe(hooks::Event::BattleAction, [this](Machine &, const hooks::Args &) {
            effect_played_ = false;
            effect_id_ = 0;
            party_hp_loss_ = false;
        }));
    battle_subscriptions_.push_back(
        registry.subscribe(hooks::Event::BattleStart, [this](Machine &, const hooks::Args &) {
            battle_active_ = true;
            battlefield_ready_ = party_hp_loss_ = false;
            effect_id_ = 0;
            effect_played_ = false;
        }));
    // A loaded save keeps nothing of the GB picture from before it: no held battle picture, no
    // battle backdrop, title page or title palette binding over the loaded field.
    battle_subscriptions_.push_back(
        registry.subscribe(hooks::Event::SaveLoaded, [this](Machine &m, const hooks::Args &) {
            auto &w = m.wsc();
            battle_animation_ = 0;
            battle_active_ = battlefield_ready_ = party_hp_loss_ = false;
            effect_id_ = 0;
            intro_.reset(w);
            w.hold_second_picture(false);
            w.second_motion_y.clear();
            w.second_scene_image.clear();
            w.second_scene_raster.reset();
            w.forget_second_swaps(); // the cause: the title's palette binding survived its fade to black
        }));
    // Rewards keep the battle layout, and a scripted/debug encounter can resume
    // actions after that handoff. Own the GB scene until the battle actually ends.
    for (auto event : {hooks::Event::BattleEnd, hooks::Event::PresentationField})
        battle_subscriptions_.push_back(registry.subscribe(event, [this](Machine &m, const hooks::Args &) {
            battle_active_ = battlefield_ready_ = party_hp_loss_ = false;
            m.wsc().second_scene_image.clear();
            m.wsc().second_scene_raster.reset();
        }));
    battle_subscriptions_.push_back(
        registry.subscribe(hooks::Event::BattleAnimation, [this](Machine &m, const hooks::Args &a) {
            if (!enabled_)
                return;
            battle_generation_ = Session::of(m.wsc()).generation();
            battle_animation_ = a.a;
            m.wsc().second_motion_y.clear();
            begin_battle_effect(m, a);
            // Keep the native WSC art/palette/OAM sequence out of the second picture.
            // The style-owned battlefield above this retained backing runs ORIJINARU's
            // extracted sprite/raster timeline on the corresponding landing phase.
            m.wsc().hold_second_picture(true);
        }));
    battle_subscriptions_.push_back(
        registry.subscribe(hooks::Event::BattleAnimationEnd, [this](Machine &, const hooks::Args &a) {
            if (battle_animation_ == a.a) {
                battle_animation_ = 0;
                effect_id_ = 0;
            }
        }));
    battle_subscriptions_.push_back(
        registry.subscribe(hooks::Event::BattleHpChange, [this](Machine &, const hooks::Args &a) {
            party_hp_loss_ = a.a >= 0 && a.a < 4 && a.b > 0 && a.c > 0;
        }));
    battle_subscriptions_.push_back(
        registry.subscribe(hooks::Event::BattleShake, [this](Machine &m, const hooks::Args &a) {
            if (!enabled_)
                return;
            auto &w = m.wsc();
            if (a.a) {
                battle_generation_ = Session::of(w).generation();
                battle_animation_ = 0x362B62;
                w.hold_second_picture(true);
                w.second_motion_start = w.frame_count;
                w.second_motion_y.clear();
                // 3F5FFA shakes only on actual party HP loss. Its 3F9819 -> 3F9839
                // reads GB:1FAE7 twice: +8,-8,-8,+8,+8,-8,-8,+8, one native frame each.
                // The native WSC shake is also 16 frames. Status/healing calls stay still.
                if (party_hp_loss_)
                    w.second_motion_y = {8, -8, -8, 8, 8, -8, -8, 8, 8, -8, -8, 8, 8, -8, -8, 8};
                party_hp_loss_ = false;
            } else if (battle_animation_ == 0x362B62) {
                battle_animation_ = 0;
            }
        }));
}

void GfxSwap::frame(emu::Wsc &w) {
    CpuScope cpu_scope("GfxSwap::frame", w.frame_count);
    if (!enabled_ || !w.second())
        return;
    if (Session::of(w).generation() != battle_generation_) {
        battle_animation_ = 0;
        battle_active_ = battlefield_ready_ = party_hp_loss_ = false;
        w.second_scene_image.clear();
        w.second_scene_raster.reset();
        battle_generation_ = Session::of(w).generation();
    }
    if (!battle_animation_)
        w.hold_second_picture(false);
    Machine m(w);
    shadow_font(m);
    shadow_hand(m);
    shadow_field(m);
    shadow_npcs(m);
    shadow_portraits(m);
    shadow_party_icons(m);
    update_palette_swaps(m);
    shadow_battlefield(w);
    intro_.frame(w, true);
}

void GfxSwap::release_icon(emu::Wsc &w, int member) {
    Icon &ic = icons_[size_t(member)];
    if (ic.pal < 0)
        return;
    for (uint16_t word : ic.cells) {
        const int tile = (word & 0x1FF) | (word & 0x2000 ? 0x200 : 0);
        w.shadow_release(uint16_t(0x4000 + tile * 32), 32);
    }
    w.shadow_release(uint16_t(kPaletteRam + ic.pal * 32), 32);
    ic = Icon{};
}

// In a battle (a portrait up), each member panel's 2x2 picture (row 16, column 1 + 7 * member; the cells
// draw_battle_member_panels writes) shows the member's GB sprite facing down, in the GB sprite greys.
bool GfxSwap::type_sprite_tiles(int type, std::array<uint8_t, 128> &tiles) {
    if (!active_ || type < 0 || type > 255 || active_->sprite_of_type_[size_t(type)] < 0)
        return false;
    const auto &src = active_->sprites_[size_t(active_->sprite_of_type_[size_t(type)])];
    for (int q = 0; q < 4; ++q)
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; x += 2) {
                const auto px = [&](int xx) {
                    return src[size_t(((q / 2) * 8 + y) * 16 + (q % 2) * 8 + xx)];
                };
                tiles[size_t(q * 32 + y * 4 + x / 2)] = uint8_t(px(x) << 4 | px(x + 1));
            }
    return true;
}

bool GfxSwap::monster_picture_tiles(int type, std::vector<uint8_t> &tiles, int &width, int &height) {
    if (!active_ || type < 0 || type > 255 || active_->pic_of_type_[size_t(type)] < 0)
        return false;
    const auto &pic = active_->pics_.at(active_->pic_of_type_[size_t(type)]);
    width = pic.w;
    height = pic.h;
    tiles.assign(size_t(width * height * 32), 0);
    for (int q = 0; q < width * height; ++q)
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; x += 2) {
                const auto px = [&](int xx) {
                    return pic.px[size_t((q / width * 8 + y) * width * 8 + q % width * 8 + xx)];
                };
                tiles[size_t(q * 32 + y * 4 + x / 2)] = uint8_t(px(x) << 4 | px(x + 1));
            }
    return true;
}

void GfxSwap::shadow_party_icons(Machine &m) {
    emu::Wsc &w = m.wsc();
    const bool battle =
        std::any_of(portraits_.begin(), portraits_.end(), [](const Portrait &p) { return p.painted; });
    for (int member = 0; member < 4; ++member) {
        std::array<uint16_t, 4> words{};
        for (int q = 0; q < 4; ++q)
            words[size_t(q)] = ram16(w, cell(1 + member * 7 + q % 2, 16 + q / 2));
        Icon &ic = icons_[size_t(member)];
        const int gb = sprite_of_type_[w.ram[uint16_t(0xD005 + member * 0x1F)]];
        const bool panel = battle && gb >= 0 && w.ram[uint16_t(0xD000 + member * 0x1F)] != 0xFF &&
                           (words[0] & 0x2000) && ((words[0] >> 9) & 15) >= 8;
        if (ic.pal >= 0 && (!panel || words != ic.cells))
            release_icon(w, member);
        if (!panel || ic.pal >= 0)
            continue;
        const auto &src = sprites_[size_t(gb)];
        for (int q = 0; q < 4; ++q) {
            const int tile = (words[size_t(q)] & 0x1FF) | 0x200;
            const bool hf = words[size_t(q)] & 0x4000, vf = words[size_t(q)] & 0x8000;
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; x += 2) {
                    const auto px = [&](int xx) { // (the cell's own flips undone)
                        const int tx = hf ? 7 - xx : xx, ty = vf ? 7 - y : y;
                        return src[size_t(((q / 2) * 8 + ty) * 16 + (q % 2) * 8 + tx)];
                    };
                    w.shadow_write8(uint16_t(0x4000 + tile * 32 + y * 4 + x / 2),
                                    uint8_t(px(x) << 4 | px(x + 1)));
                }
        }
        const int pal = (words[0] >> 9) & 15;
        for (int c = 0; c < 16; ++c)
            w.shadow_write16(uint16_t(kPaletteRam + pal * 32 + c * 2),
                             c >= 1 && c <= 3 ? kSpriteGreys[c] : 0xFFF);
        ic.cells = words;
        ic.pal = pal;
    }
}

// Each font tile that holds its WSC glyph shows the GB glyph.
void GfxSwap::shadow_font(Machine &m) {
    if (!has_font_)
        return;
    emu::Wsc &w = m.wsc();
    for (int code = 0; code < kFontCodes; ++code) {
        if (!font_has_[size_t(code)])
            continue;
        const uint16_t at = uint16_t(0x4000 + (kFontTile + code) * 32);
        bool glyph = true; // the game's tile is the WSC glyph (not some other picture in these tiles)
        for (int i = 0; i < 32 && glyph; ++i)
            glyph = w.ram[uint16_t(at + i)] == m.rd8(kFontSeg, uint16_t(kFontOff + code * 32 + i));
        if (glyph) {
            if (!w.shadowed(at))
                for (int i = 0; i < 32; ++i)
                    w.shadow_write8(uint16_t(at + i), font_[size_t(code)][size_t(i)]);
        } else if (w.shadowed(at)) {
            w.shadow_release(at, 32);
        }
    }
}

// Only replace the hand while these slots still contain the native font's hand tiles.
// Title, native dialogs and every host hand all read these same shadow tiles.
void GfxSwap::shadow_hand(Machine &m) {
    if (!has_hand_)
        return;
    auto &w = m.wsc();
    for (int q = 0; q < 4; ++q) {
        const uint16_t at = uint16_t(0x4000 + (0x1FB + q) * 32);
        bool original = true;
        for (int b = 0; b < 32; ++b)
            original &= w.ram[uint16_t(at + b)] == m.rd8(kFontSeg, uint16_t(kFontOff + (0xFB + q) * 32 + b));
        if (original)
            for (int b = 0; b < 32; ++b)
                w.shadow_write8(uint16_t(at + b), hand_[size_t(q * 32 + b)]);
        else
            w.shadow_release(at, 32);
    }
}

// The field of a map with GB graphics: the GB metatiles as tiles 0x200 + 4 * metatile + quadrant (in a grey
// palette, 6), and every word of screen 1 that is a quadrant of a WSC metatile replaced by its GB metatile's
// (the game's own scrolling and redraws followed frame by frame). Released anywhere else.
void GfxSwap::shadow_field(Machine &m) {
    CpuScope cpu_scope("GfxSwap::shadow_field", m.wsc().frame_count);
    emu::Wsc &w = m.wsc();
    for (uint16_t at : sparse_tiles_)
        w.shadow_release(at, 32);
    if (!sparse_tiles_.empty())
        w.shadow_release(uint16_t(kPaletteRam + kPalette * 32), 32);
    sparse_tiles_.clear();
    // Release only our matching foreground words, before repainting this frame.
    // A portrait/window that took ownership of either picture must be left alone.
    for (const auto &[at, words] : field_overlay_)
        // Native clears/scrolling writes cannot reach a masked GB cell. Release our old word even when
        // the native word changed; otherwise that foreground survives forever over the next field.
        if (uint16_t(w.shadow_read8(at) | w.shadow_read8(uint16_t(at + 1)) << 8) == words.second)
            w.shadow_release(at, 2);
    field_overlay_.clear();
    // A warp announces its destination before loading/drawing that field. Keep
    // the old GB strip until the cell writer publishes the destination's words;
    // interpreting the outgoing ring with the destination's art flashes garbage.
    const int id =
        w.ram[kMapId] && field_owner_ == &w && !field_cells_.empty() ? tracked_map_ : w.ram[kMapId];
    const auto it = maps_.find(id);
    const Map *gm = it == maps_.end() ? nullptr : &it->second;
    std::vector<uint16_t> set(1024);
    for (size_t i = 0; i < set.size(); ++i)
        set[i] = m.rd16(kSetSeg, uint16_t(kSetOff + i * 2));
    // (never in a battle: its screen 1 may still hold the field's words under the battle screen, and the
    // field's GB tiles and player sprite would show through the battle's own tiles)
    const bool battle =
        std::any_of(portraits_.begin(), portraits_.end(), [](const Portrait &p) { return p.painted; });
    const bool field = gm && !battle && in_field(w, set);
    if (!field || field_map_ < 0 || field_map_ != id)
        release_field_sparkle(w);
    if (!field) {
        if (field_map_ >= 0) {
            w.shadow_release(kTiles, field_tile_bytes_);
            w.shadow_release(uint16_t(kPaletteRam + kPalette * 32), 32);
            w.shadow_release(kScreen1, 32 * 32 * 2);
            field_map_ = -1;
        }
    } else if (std::all_of(gm->gb.begin(), gm->gb.end(), [](int g) { return g < 0; }) &&
               gm->by_type.empty()) {
        // Sparse strict matches leave the original field pixels intact elsewhere.
        // Borrow only upper-bank tiles absent from BOTH visible screen maps. The
        // old full-map strip starts at 0x8000 and would overwrite WSC fallback art.
        if (field_map_ >= 0) {
            w.shadow_release(kTiles, field_tile_bytes_);
            w.shadow_release(uint16_t(kPaletteRam + kPalette * 32), 32);
            w.shadow_release(kScreen1, 2048);
            field_map_ = -1;
        }
        std::array<bool, 512> used{};
        for (int i = 0; i < 2048; ++i) {
            const uint16_t word = ram16(w, uint16_t(kScreen1 + i * 2));
            if (word & 0x2000)
                used[size_t(word & 511)] = true;
        }
        std::map<std::array<uint8_t, 64>, int> slots;
        const auto tile = [&](int g, int q) {
            std::array<uint8_t, 64> px{};
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                    px[size_t(y * 8 + x)] =
                        gm->metatiles[size_t(g)][size_t((q / 2 * 8 + y) * 16 + q % 2 * 8 + x)];
            const auto found = slots.find(px);
            if (found != slots.end())
                return found->second;
            const auto free = std::find(used.begin(), used.end(), false);
            if (free == used.end())
                return -1;
            const int index = int(free - used.begin());
            used[size_t(index)] = true;
            slots.emplace(px, index);
            const uint16_t at = uint16_t(kTiles + index * 32);
            sparse_tiles_.push_back(at);
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; x += 2)
                    w.shadow_write8(uint16_t(at + y * 4 + x / 2), uint8_t((px[size_t(y * 8 + x)] + 1) << 4 |
                                                                          (px[size_t(y * 8 + x + 1)] + 1)));
            return index;
        };
        for (int c = 1; c <= 4; ++c)
            w.shadow_write16(uint16_t(kPaletteRam + kPalette * 32 + c * 2), kGreys[c - 1]);
        constexpr uint16_t offsets[] = {0, 2, 0x40, 0x42};
        if (field_owner_ == &w && tracked_map_ == id)
            for (const auto &[at, entry] : field_cells_) {
                const auto exact = gm->by_cell.find((entry.y & 255) * 256 + (entry.x & 255));
                if (exact == gm->by_cell.end())
                    continue;
                for (int q = 0; q < 4; ++q) {
                    const uint16_t dest = uint16_t(at + offsets[q]);
                    if (dest < kScreen1 || dest >= kScreen1 + 2048 ||
                        ram16(w, dest) != entry.words[size_t(q)])
                        continue;
                    const int index = tile(exact->second, q);
                    if (index < 0)
                        continue;
                    const uint16_t replacement = uint16_t(0x2000 | index | kPalette << 9);
                    w.shadow_write16(dest, replacement);
                    field_overlay_[dest] = {entry.words[size_t(q)], replacement};
                    const uint16_t front = uint16_t(dest + 0x800);
                    if (entry.foreground[size_t(q)] && ram16(w, front) == entry.words[size_t(q)]) {
                        w.shadow_write16(front, replacement);
                        field_overlay_[front] = {entry.words[size_t(q)], replacement};
                    }
                }
            }
    } else {
        if (field_map_ != id) {
            if (field_map_ >= 0)
                w.shadow_release(kTiles, field_tile_bytes_);
            field_tile_bytes_ = gm->metatiles.size() * 4 * 32;
            for (size_t g = 0; g < gm->metatiles.size(); ++g)
                for (int q = 0; q < 4; ++q)
                    for (int y = 0; y < 8; ++y)
                        for (int x = 0; x < 8; x += 2) {
                            const auto px = [&](int xx) {
                                return gm->metatiles[g][size_t(((q / 2) * 8 + y) * 16 + (q % 2) * 8 + xx)] +
                                       1;
                            };
                            w.shadow_write8(
                                uint16_t(kTiles + (g * 4 + size_t(q)) * 32 + size_t(y) * 4 + size_t(x) / 2),
                                uint8_t(px(x) << 4 | px(x + 1)));
                        }
            field_map_ = id;
        }
        for (int c = 0; c < 16; ++c) // shade s -> color s + 1
            w.shadow_write16(uint16_t(kPaletteRam + kPalette * 32 + c * 2),
                             c >= 1 && c <= 4 ? kGreys[c - 1]
                                              : ram16(w, uint16_t(kPaletteRam + kPalette * 32 + c * 2)));
        std::unordered_map<uint16_t, uint16_t> gb_word; // a WSC metatile quadrant's word -> its GB one
        for (int t = 0; t < 256; ++t)
            if (gm->gb[size_t(t)] >= 0)
                for (int q = 0; q < 4; ++q)
                    gb_word.emplace(set[size_t(t * 4 + q)],
                                    uint16_t(0x2000 | (gm->gb[size_t(t)] * 4 + q) | kPalette << 9));
        for (int i = 0; i < 32 * 32; ++i) {
            const uint16_t at = uint16_t(kScreen1 + i * 2), word = ram16(w, at);
            const auto g = gb_word.find(word);
            w.shadow_write16(at, g == gb_word.end() ? word : g->second);
        }
        // Typed and coordinate resources use the actual cell rather than quadrant aliases. Original-word
        // guards leave menu/overlay writes alone; this never writes the game's RAM.
        if ((!gm->by_type.empty() || !gm->by_cell.empty()) && field_owner_ == &w && tracked_map_ == id) {
            constexpr uint16_t offsets[] = {0, 2, 0x40, 0x42};
            for (const auto &[at, entry] : field_cells_) {
                int g = gm->gb[entry.id];
                const auto typed = gm->by_type.find(int(entry.id) * 32 + entry.type);
                if (entry.type >= 0 && typed != gm->by_type.end())
                    g = typed->second;
                const auto exact = gm->by_cell.find((entry.y & 255) * 256 + (entry.x & 255));
                if (exact != gm->by_cell.end())
                    g = exact->second;
                for (int q = 0; g >= 0 && q < 4; ++q) {
                    const uint16_t dest = uint16_t(at + offsets[q]);
                    if (dest < kScreen1 || dest >= kScreen1 + 2048 ||
                        ram16(w, dest) != entry.words[size_t(q)])
                        continue;
                    const uint16_t replacement = uint16_t(0x2000 | (g * 4 + q) | kPalette << 9);
                    w.shadow_write16(dest, replacement);
                    const uint16_t front = uint16_t(dest + 0x800);
                    if (entry.foreground[size_t(q)] && ram16(w, front) == entry.words[size_t(q)]) {
                        w.shadow_write16(front, replacement);
                        field_overlay_[front] = {entry.words[size_t(q)], replacement};
                    }
                }
            }
        }
        draw_field_sparkle(w, *gm, id);
    }
    // Actor buffers own their GB art independently of the tilemap. The ending crops its
    // field beneath credits, so the field-word majority is not a player visibility test.
}

// World 3, second-to-last story step (map 122, story counter 6 = 0xE): the ROM's field animator
// (animate_field_progress_dependent_tiles, 0x359CB2) streams a 16-frame sparkle into tiles 0x200-0x203
// every 8 frames, over the clearing cell beside the landed castle (the warp into it). The GB map draws that
// cell as plain cloud, so the GB picture shows the live WSC frame in GB shades over the cell's GB art: the
// sparkle's four yellows (colors 12-15) as the darker GB shades, its cloud (6-11) as the GB cloud. Only
// while the WSC animates it; anywhere else the borrowed tiles are released.
void GfxSwap::draw_field_sparkle(emu::Wsc &w, const Map &gm, int id) {
    constexpr int kSparkleMap = 122, kSparkleStep = 0xE, kSparkleTile = 0x200;
    constexpr uint8_t kSparkleShade[4] = {3, 3, 2, 1}; // colors 12-15 (dark to light yellow)
    if (id != kSparkleMap || (w.ram[kPackedProgress + 3] >> 4) != kSparkleStep || field_owner_ != &w ||
        tracked_map_ != id)
        return release_field_sparkle(w);
    // Upper-bank tiles that neither screen map uses and the GB strip doesn't own.
    if (sparkle_tiles_[0] < 0) {
        std::array<bool, 512> used{};
        for (size_t i = 0; i < gm.metatiles.size() * 4 && i < used.size(); ++i)
            used[i] = true;
        for (int i = 0; i < 2 * 1024; ++i) {
            const uint16_t word = ram16(w, uint16_t(kScreen1 + i * 2));
            if (word & 0x2000)
                used[size_t(word & 511)] = true;
        }
        for (int t = 0, i = 511; t < 4; ++t) {
            while (i >= 0 && used[size_t(i)])
                --i;
            if (i < 0)
                return release_field_sparkle(w);
            sparkle_tiles_[size_t(t)] = i--;
        }
    }
    constexpr uint16_t offsets[] = {0, 2, 0x40, 0x42};
    std::array<bool, 4> drawn{};
    for (const auto &[at, entry] : field_cells_) {
        const int g = gm.gb[entry.id];
        for (int q = 0; g >= 0 && q < 4; ++q) {
            const uint16_t word = entry.words[size_t(q)];
            const int tile = (word & 0x1FF) | (word & 0x2000 ? 0x200 : 0);
            const uint16_t dest = uint16_t(at + offsets[q]);
            if (tile < kSparkleTile || tile >= kSparkleTile + 4 || dest < kScreen1 ||
                dest >= kScreen1 + 2048 || ram16(w, dest) != word)
                continue;
            const int slot = sparkle_tiles_[size_t(tile - kSparkleTile)];
            if (!drawn[size_t(tile - kSparkleTile)]) {
                drawn[size_t(tile - kSparkleTile)] = true;
                for (int y = 0; y < 8; ++y)
                    for (int x = 0; x < 8; x += 2) {
                        uint8_t pair = 0;
                        for (int dx = 0; dx < 2; ++dx) {
                            const int sx = word & 0x4000 ? 7 - (x + dx) : x + dx,
                                      sy = word & 0x8000 ? 7 - y : y;
                            const uint8_t packed = w.ram[size_t(0x4000 + tile * 32 + sy * 4 + sx / 2)];
                            const int color = sx & 1 ? packed & 15 : packed >> 4;
                            const int shade =
                                color >= 12 ? kSparkleShade[color - 12]
                                            : gm.metatiles[size_t(g)]
                                                          [size_t((q / 2 * 8 + y) * 16 + q % 2 * 8 + x + dx)];
                            pair = uint8_t(pair << 4 | (shade + 1));
                        }
                        w.shadow_write8(uint16_t(kTiles + slot * 32 + y * 4 + x / 2), pair);
                    }
            }
            const uint16_t replacement = uint16_t(0x2000 | slot | kPalette << 9);
            w.shadow_write16(dest, replacement);
            const uint16_t front = uint16_t(dest + 0x800);
            if (entry.foreground[size_t(q)] && ram16(w, front) == word) {
                w.shadow_write16(front, replacement);
                field_overlay_[front] = {word, replacement};
            }
        }
    }
}

void GfxSwap::release_field_sparkle(emu::Wsc &w) {
    for (int &slot : sparkle_tiles_)
        if (slot >= 0) {
            w.shadow_release(uint16_t(kTiles + slot * 32), 32);
            slot = -1;
        }
}

// The screen shows the field when most of screen 1 is words of the current metatile set.
bool GfxSwap::in_field(const emu::Wsc &w, const std::vector<uint16_t> &set) const {
    std::vector<uint16_t> sorted = set;
    std::sort(sorted.begin(), sorted.end());
    int hits = 0;
    for (int i = 0; i < 32 * 32; ++i)
        hits += std::binary_search(sorted.begin(), sorted.end(), ram16(w, uint16_t(kScreen1 + i * 2)));
    return hits >= 32 * 32 / 2;
}

void GfxSwap::player_hook(Machine &m, uint16_t oam) {
    if (active_ && active_->enabled_ && m.wsc().second())
        active_->draw_player(m, oam);
}

// Right after the game puts the player's tiles and sprites in place: its GB sprite in the shadow's tiles, and
// the player's palette (8: only the player uses it) in the GB's sprite colors.
void GfxSwap::draw_player(Machine &m, uint16_t oam) {
    emu::Wsc &w = m.wsc();
    const int gb = sprite_of_type_[w.ram[kLeaderType]];
    uint8_t pose = w.ram[kPose];
    if (pose & 0x40)
        pose = uint8_t((pose & 0x3F) + w.ram[kPoseFrame]);
    const uint16_t tile = m.rd16(m.ss(), oam) & 0x1FF;
    const int travel = w.ram[kTravelMode];
    const auto object = objects_.find(47 + travel);
    if (observe_sprite)
        observe_sprite(w.ram[kMapId], w.ram[0xD520], pose,
                       travel ? travel <= 3 && object != objects_.end() && pose < object->second.size()
                              : gb >= 0);
    if (travel >= 1 && travel <= 3 && object != objects_.end() && pose < object->second.size()) {
        const auto &tiles = object->second[pose];
        for (int b = 0; b < 128; ++b)
            w.shadow_write8(uint16_t(0x4000 + tile * 32 + b), tiles[size_t(b)]);
        for (int c = 1; c <= 3; ++c)
            w.shadow_write16(uint16_t(kPaletteRam + kSpritePalette * 32 + c * 2), kSpriteGreys[c]);
    } else if (travel == 0 && gb >= 0) {
        const auto tiles = actor_tiles(gb, w.ram[0xD520], pose);
        for (int b = 0; b < 128; ++b)
            w.shadow_write8(uint16_t(0x4000 + tile * 32 + b), tiles[size_t(b)]);
        for (int c = 1; c <= 3; ++c)
            w.shadow_write16(uint16_t(kPaletteRam + kSpritePalette * 32 + c * 2), kSpriteGreys[c]);
    } else {
        return release_player(w);
    }
    // The ending can retain an actor while the field tilemap is cropped or replaced.
    // Use the same native tile/OAM ownership as NPCs instead of the field-word heuristic.
    NpcSprite player;
    player.palette = kSpritePalette;
    player.map = w.ram[kMapId];
    player.tile = uint16_t(tile);
    player.oam = oam;
    std::copy_n(w.ram.begin() + oam, 16, player.original_oam.begin());
    std::copy_n(w.ram.begin() + 0x4000 + tile * 32, 128, player.original.begin());
    for (int b = 0; b < 128; ++b)
        player.tiles[size_t(b)] = w.shadow_read8(uint16_t(0x4000 + tile * 32 + b));
    std::erase_if(npcs_, [tile](const auto &entry) {
        return entry.first.second == tile && entry.first.first != kPose;
    });
    npcs_[{kPose, uint16_t(tile)}] = player;
    player_shown_ = true;
}

void GfxSwap::import_field_sprites(emu::Wsc &w) {
    if (sprites_.empty() || w.rom.size() != 0x400000)
        return;
    // The first sixteen rows already came from sprites.png. GB rows 16-20 are people, omitted by
    // the old extractor. Import only missing rows; an expanded/modded PNG always wins.
    for (size_t gb = sprites_.size(); gb < 21; ++gb) {
        std::array<uint8_t, 4 * 256> sprite{};
        for (int pose = 0; pose < 4; ++pose)
            for (int q = 0; q < 4; ++q)
                for (int y = 0; y < 8; ++y) {
                    const size_t at = 0x326000 + gb * 256 + size_t(pose * 64 + q * 16 + y * 2);
                    for (int x = 0; x < 8; ++x)
                        sprite[size_t(pose * 256 + (q / 2 * 8 + y) * 16 + q % 2 * 8 + x)] =
                            uint8_t((w.rom[at] >> (7 - x) & 1) | (w.rom[at + 1] >> (7 - x) & 1) << 1);
                }
        sprites_.push_back(sprite);
    }
    sprite_of_set_.fill(-1);
    const auto *session = Session::find(w);
    for (int type = 0; type < 200; ++type) {
        const auto *row =
            session ? session->tables().row(Table::monster_type_graphics, unsigned(type)) : nullptr;
        const int set = (row ? row[0] : w.rom[size_t(0x3976E8 + type * 2)]) & 63;
        if (set < 29 && sprite_of_type_[size_t(type)] >= 0)
            sprite_of_set_[size_t(set)] = sprite_of_type_[size_t(type)];
    }
    // GB rows for the WSC-only people sets (41 from the GB rooms' NPC records: 41 (87)). 29, 30 and 31 are
    // the kings (maps 92, 101 and 91; 29 also on 198 and 200): the GB game draws its kings with the knight,
    // row 13 (the owner, 2026-11-21; 29 and 30 had shown their WSC art in GB greys). 35 is the Dragon King;
    // 39 is Sayaka, the only one on row 17. 32, 36 and 37 use the party classes' sprites (Human M, Human F,
    // Esper F). 38 and 40 are still assigned by role.
    constexpr std::pair<int, int> people[] = {{29, 13}, {30, 13}, {31, 13}, {32, 0}, {33, 10},
                                              {34, 11}, {35, 16}, {36, 1},  {37, 3}, {38, 19},
                                              {39, 17}, {40, 19}, {41, 16}, {42, 20}};
    for (auto [set, gb] : people)
        if (size_t(gb) < sprites_.size())
            sprite_of_set_[size_t(set)] = gb;
    // WSC's additional playable types reuse these same character families. Preserve explicit mod rows.
    for (int type = 200; type < 256; ++type) {
        const auto *row =
            session ? session->tables().row(Table::monster_type_graphics, unsigned(type)) : nullptr;
        const int set = (row ? row[0] : w.rom[size_t(0x3976E8 + type * 2)]) & 63;
        if (sprite_of_type_[size_t(type)] < 0)
            sprite_of_type_[size_t(type)] = sprite_of_set_[size_t(set)];
    }
}
std::array<uint8_t, 128> GfxSwap::actor_tiles(int gb, int set, int pose) const {
    pose = gb_walk_pose(set, pose);
    if (pose >= 8) {
        if (has_corpse_)
            return corpse_;
        pose = 0; // older/modded resources without the body keep the standing fallback
    }
    std::array<uint8_t, 128> tiles{};
    const auto &src = sprites_[size_t(gb)];
    const int from = kGbPose[pose].pose * 256;
    const bool mirror = kGbPose[pose].mirror;
    for (int q = 0; q < 4; ++q)
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; x += 2) {
                const auto px = [&](int xx) {
                    const int sx = q % 2 * 8 + xx, sy = q / 2 * 8 + y;
                    return src[size_t(from + sy * 16 + (mirror ? 15 - sx : sx))];
                };
                tiles[size_t(q * 32 + y * 4 + x / 2)] = uint8_t(px(x) << 4 | px(x + 1));
            }
    return tiles;
}
void GfxSwap::npc_hook(Machine &m, uint16_t record, uint16_t oam, uint8_t pose) {
    if (active_)
        active_->draw_npc(m, record, oam, pose);
}
void GfxSwap::draw_npc(Machine &m, uint16_t record, uint16_t oam, uint8_t pose) {
    auto &w = m.wsc();
    const int palette = (ram16(w, oam) >> 9 & 7) + 8;
    const int set = w.ram[size_t(0xD5A6 + (palette - 8))];
    const int gb = set < int(sprite_of_set_.size()) ? sprite_of_set_[size_t(set)] : -1;
    const auto object = objects_.find(set);
    const bool is_object = object != objects_.end() && pose < object->second.size();
    if (observe_sprite)
        observe_sprite(w.ram[kMapId], set, pose, is_object || gb >= 0);
    if (!is_object && gb < 0) {
        std::erase_if(npcs_, [record](const auto &entry) { return entry.first.first == record; });
        return;
    }
    NpcSprite npc;
    npc.palette = palette;
    npc.map = w.ram[kMapId];
    npc.tile = ram16(w, oam) & 511;
    npc.oam = oam;
    std::copy_n(w.ram.begin() + oam, 16, npc.original_oam.begin());
    std::copy_n(w.ram.begin() + 0x4000 + npc.tile * 32, 128, npc.original.begin());
    if (is_object) {
        npc.tiles = object->second[pose];
    } else {
        npc.tiles = actor_tiles(gb, set, pose);
    }
    // Palette 15 is shared with windows and cursors. Use its native sprite-gray indices instead of
    // recoloring that palette (which would also recolor the host hand and every native window).
    if (palette == 15) {
        constexpr uint8_t colors[] = {0, 2, 12, 1};
        for (auto &b : npc.tiles)
            b = uint8_t(colors[b >> 4] << 4 | colors[b & 15]);
    }
    std::erase_if(npcs_, [record, &npc](const auto &entry) {
        return entry.first.second == npc.tile && entry.first.first != record;
    });
    npcs_[{record, npc.tile}] = npc;
    if (enabled_ && w.second()) {
        npc_tiles_.push_back(uint16_t(0x4000 + npc.tile * 32));
        if (palette != 15)
            npc_palettes_.push_back(uint16_t(kPaletteRam + palette * 32));
        for (int b = 0; b < 128; ++b)
            w.shadow_write8(uint16_t(0x4000 + npc.tile * 32 + b), npc.tiles[size_t(b)]);
        for (int c = 1; c <= 3 && palette != 15; ++c)
            w.shadow_write16(uint16_t(kPaletteRam + palette * 32 + c * 2), kSpriteGreys[c]);
    }
}
void GfxSwap::shadow_npcs(Machine &m) {
    auto &w = m.wsc();
    for (auto at : npc_tiles_)
        w.shadow_release(at, 128);
    for (auto at : npc_palettes_)
        w.shadow_release(at, 32);
    npc_tiles_.clear();
    npc_palettes_.clear();
    const auto latched = w.presentation_sprites();
    for (const auto &[key, npc] : npcs_) {
        const auto record = key.first;
        const uint16_t at = uint16_t(0x4000 + npc.tile * 32);
        // The game alternates tile buffers before OAM latches at VBlank. Keep both
        // the queued and displayed pose; scripts may also wait without redrawing.
        bool displayed = false;
        for (size_t i = 0; !displayed && i + 16 <= latched.size(); i += 4)
            displayed = std::equal(npc.original_oam.begin(), npc.original_oam.end(), latched.begin() + i);
        const bool queued =
            (record == kPose || (w.ram[record] != 0xFF && !(w.ram[uint16_t(record + 0x11)] & 8))) &&
            std::equal(npc.original_oam.begin(), npc.original_oam.end(), w.ram.begin() + npc.oam);
        if (npc.map != w.ram[kMapId] || (!queued && !displayed) ||
            !std::equal(npc.original.begin(), npc.original.end(), w.ram.begin() + at))
            continue;
        for (int b = 0; b < 128; ++b)
            w.shadow_write8(uint16_t(at + b), npc.tiles[size_t(b)]);
        for (int c = 1; c <= 3 && npc.palette != 15; ++c)
            w.shadow_write16(uint16_t(kPaletteRam + npc.palette * 32 + c * 2), kSpriteGreys[c]);
        npc_tiles_.push_back(at);
        if (npc.palette != 15)
            npc_palettes_.push_back(uint16_t(kPaletteRam + npc.palette * 32));
    }
}
void GfxSwap::release_player(emu::Wsc &w) {
    std::erase_if(npcs_, [](const auto &entry) { return entry.first.first == kPose; });
    if (!player_shown_)
        return;
    w.shadow_release(kPlayerTiles, kPlayerTileBytes);
    w.shadow_release(uint16_t(kPaletteRam + kSpritePalette * 32), 32);
    player_shown_ = false;
}

// After the game draws enemy group `group`'s picture: the GB one in its place in the shadow (in the group's
// tiles and cells, bottom-aligned and centered), its colors 1-4 shown as GB greys.
void GfxSwap::portrait_hook(Machine &m, int group, uint16_t at) {
    if (!active_ || !active_->enabled_ || !m.wsc().second() || group < 0 || group > 2)
        return;
    active_->paint_portrait(m, group, at);
}

void GfxSwap::paint_portrait(Machine &m, int group, uint16_t at) {
    emu::Wsc &w = m.wsc();
    Portrait &p = portraits_[size_t(group)];
    release_portrait(w, group);
    p.defeated = false;
    const uint16_t rec = uint16_t(kGroupRecords + group * 12);
    p.at = at;
    p.w = w.ram[uint16_t(rec + 9)];
    p.h = w.ram[uint16_t(rec + 10)];
    const int type = w.ram[uint16_t(kGroupTypes + group * 2)];
    if (pic_of_type_[size_t(type)] < 0)
        return;
    // the group's colors as the game copies them (its palette source, skipped by rec + 8 palettes)
    const uint16_t source = uint16_t(ram16(w, uint16_t(rec + 2)) + w.ram[uint16_t(rec + 8)] * 0x20);
    for (int c = 0; c < 16; ++c) {
        p.swap.from[size_t(c)] = m.rd16(0xE000, uint16_t(source + c * 2)) & 0x0FFF;
        p.swap.to[size_t(c)] = c >= 1 && c <= 4 ? kGreys[c - 1] : p.swap.from[size_t(c)];
    }
    const Pic &pic = pics_.at(pic_of_type_[size_t(type)]);
    // tiles: the group's 40 (a lone group's picture may use the others' too)
    const int limit = w.ram[kGroupCount] <= 1 && group == 0 ? 120 : 40;
    p.gw = pic.w;
    p.gh = std::min(pic.h, limit / pic.w);
    const int col = p.at & 0xFF, row = p.at >> 8;
    p.gx = std::clamp(col + (p.w - p.gw) / 2, 0, 32 - p.gw);
    p.gy = std::max(row + p.h - p.gh, 0);
    p.wsc_cells.clear(); // the game's picture as it drew it: while it's there, ours is
    for (int y = 0; y < p.h; ++y)
        for (int x = 0; x < p.w; ++x) {
            p.wsc_cells.push_back(ram16(w, cell(col + x, row + y)));
            w.shadow_write16(cell(col + x, row + y), kBlankCell);
        }
    const int skip = pic.h - p.gh; // rows cut off the top when it doesn't fit
    p.tiles.assign(size_t(p.gw * p.gh * 32), 0);
    for (int i = 0; i < p.gw * p.gh; ++i) {
        const int tx = i % p.gw, ty = i / p.gw + skip;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; x += 2) {
                const auto px = [&](int xx) {
                    return pic.px[size_t((ty * 8 + y) * pic.w * 8 + tx * 8 + xx)];
                };
                p.tiles[size_t(i * 32 + y * 4 + x / 2)] = uint8_t(px(x) << 4 | px(x + 1));
            }
        w.shadow_write16(cell(p.gx + i % p.gw, p.gy + i / p.gw), group_word(group, i));
    }
    for (size_t i = 0; i < p.tiles.size(); ++i)
        w.shadow_write8(uint16_t(0x4000 + group_tile(group) * 32 + i), p.tiles[i]);
    p.painted = true;
    battle_active_ = battlefield_ready_ = true;
    shadow_battlefield(w);
}

void GfxSwap::shadow_battlefield(emu::Wsc &w) {
    if (!battle_active_ || !battlefield_ready_) {
        w.second_scene_image.clear();
        return;
    }
    // ORIJINARU's enemy field is plain white. Paint only our extracted GB portraits;
    // the WSC can freely reuse every palette, tile slot, map cell and sprite beneath it.
    auto &image = w.second_scene_image;
    image.assign(224 * 80, 0xFFF);
    for (const auto &p : portraits_) {
        if (!p.painted || p.defeated)
            continue;
        for (int y = 0; y < p.gh * 8; ++y)
            for (int x = 0; x < p.gw * 8; ++x) {
                const int tile = y / 8 * p.gw + x / 8;
                const uint8_t byte = p.tiles[size_t(tile * 32 + y % 8 * 4 + x % 8 / 2)];
                const int index = x & 1 ? byte & 15 : byte >> 4;
                const int sx = p.gx * 8 + x, sy = p.gy * 8 + y - 4;
                if (index > 0 && index <= 4 && sx >= 0 && sx < 224 && sy >= 0 && sy < 80)
                    image[size_t(sy * 224 + sx)] = kGreys[index - 1];
            }
    }
    apply_battle_effect(w);
}

// A defeated group dissolves: each pass ANDs the picture's bytes with the pass's mask; the GB tiles too.
void GfxSwap::dissolve_hook(Machine &m, int group, int step) {
    if (!active_ || !active_->enabled_ || !m.wsc().second() || group < 0 || group > 2)
        return;
    Portrait &p = active_->portraits_[size_t(group)];
    if (!p.painted)
        return;
    const uint8_t mask = m.rd8(0xE000, uint16_t(kDissolveMasks + step));
    for (size_t i = 0; i < p.tiles.size(); ++i) {
        p.tiles[i] &= mask;
        m.wsc().shadow_write8(uint16_t(0x4000 + group_tile(group) * 32 + i), p.tiles[i]);
    }
    active_->shadow_battlefield(m.wsc());
}

void GfxSwap::release_portrait(emu::Wsc &w, int group) {
    Portrait &p = portraits_[size_t(group)];
    if (!p.painted)
        return;
    const int col = p.at & 0xFF, row = p.at >> 8;
    for (int y = 0; y < p.h; ++y)
        for (int x = 0; x < p.w; ++x)
            w.shadow_release(cell(col + x, row + y), 2);
    for (int i = 0; i < p.gw * p.gh; ++i)
        w.shadow_release(cell(p.gx + i % p.gw, p.gy + i / p.gw), 2);
    w.shadow_release(uint16_t(0x4000 + group_tile(group) * 32), p.tiles.size());
    p.painted = false;
}

// A group's GB picture stays while the game's cells still show its picture; the backdrop (palettes 0-3, as
// the game loads them: their fade target) shows white while any does.
void GfxSwap::shadow_portraits(Machine &m) {
    emu::Wsc &w = m.wsc();
    bool battle = false;
    for (int g = 0; g < 3; ++g) {
        Portrait &p = portraits_[size_t(g)];
        if (!p.painted)
            continue;
        const int col = p.at & 0xFF, row = p.at >> 8;
        bool there = true;
        for (int y = 0; y < p.h && there; ++y)
            for (int x = 0; x < p.w && there; ++x)
                there = ram16(w, cell(col + x, row + y)) == p.wsc_cells[size_t(y * p.w + x)];
        if (!there && !battle_active_ && !battle_animation_)
            release_portrait(w, g);
        battle = battle || p.painted;
    }
    if (!battle) {
        backdrop_swaps_.clear();
        return;
    }
    for (int pal = 0; pal < 4; ++pal) {
        emu::Wsc::PaletteSwap swap;
        for (int c = 0; c < 16; ++c)
            swap.from[size_t(c)] = ram16(w, uint16_t(kPaletteTarget + pal * 32 + c * 2)) & 0x0FFF;
        swap.to.fill(0xFFF);
        if (std::none_of(backdrop_swaps_.begin(), backdrop_swaps_.end(),
                         [&](const emu::Wsc::PaletteSwap &b) { return b.from == swap.from; }))
            backdrop_swaps_.push_back(swap);
    }
}

// The second picture's display options: the window palettes (9000:0000, 4: plain, a lighter fill
// (highlighted), red text, both) in greys (a lighter fill light grey, red text dark grey), the monsters', the
// backdrop's, the title's, and every sprite in greys.
void GfxSwap::update_palette_swaps(Machine &m) {
    emu::Wsc &w = m.wsc();
    auto &swaps = w.palette_swaps;
    swaps.clear();
    w.palette_swap_target = kPaletteTarget;
    w.upper_palette_greys = true;
    w.screen1_image_swap = -1;
    for (int v = 0; v < 4 && has_font_; ++v) {
        emu::Wsc::PaletteSwap swap;
        swap.sprites = false; // the hand cursor and party icons: greys by brightness
        for (int c = 0; c < 16; ++c) {
            const uint16_t from = m.rd16(kFontSeg, uint16_t(kWindowPaletteOff + v * 32 + c * 2)) & 0x0FFF;
            swap.from[size_t(c)] = from;
            swap.to[size_t(c)] = c == 0                         ? from
                                 : (v & 1) && c >= 9 && c <= 11 ? uint16_t(0xAAA)
                                 : (v & 2) && c == 2            ? uint16_t(0x555)
                                                                : kWindowGreys[c];
        }
        swaps.push_back(swap);
    }
    for (const Portrait &p : portraits_)
        if (p.painted)
            swaps.push_back(p.swap);
    if (!title_.empty()) { // the title's logo palette and its menu text's, from the ROM
        const auto &rom = w.rom;
        const auto rom_palette = [&](uint32_t at) {
            std::array<uint16_t, 16> c;
            for (int i = 0; i < 16; ++i) {
                const uint32_t a = uint32_t(at + i * 2) & uint32_t(rom.size() - 1);
                c[size_t(i)] = uint16_t((rom[a] | rom[(a + 1) & (rom.size() - 1)] << 8) & 0x0FFF);
            }
            return c;
        };
        emu::Wsc::PaletteSwap logo, text;
        logo.from = logo.to = rom_palette(kTitleLogoPalette);
        text.from = rom_palette(kTitleTextPalette);
        for (int c = 0; c < 16; ++c) { // white text with a dark shadow -> black text, no shadow
            const int v = text.from[size_t(c)] >> 4 & 15;
            text.to[size_t(c)] = c == 0    ? text.from[0]
                                 : v >= 13 ? 0x000
                                 : v >= 9  ? 0x555
                                 : v >= 5  ? 0xAAA
                                           : 0xFFF;
        }
        w.screen1_image_swap = int(swaps.size());
        swaps.push_back(logo);
        swaps.push_back(text);
        if (w.screen1_image.size() != title_.size())
            w.screen1_image = title_;
        w.screen2_hidden_palettes = 0x0007; // the logo's subtitle (palettes 0-2)
    }
    swaps.insert(swaps.end(), backdrop_swaps_.begin(), backdrop_swaps_.end());
}

void GfxSwap::draw_intro_map(int id, int x, int y, std::vector<uint16_t> &image, int repeat_top) const {
    const auto found = maps_.find(id);
    if (found == maps_.end())
        return; // missing optional resources leave a white GB page, never WSC art
    const auto &map = found->second;
    for (int py = 0; py < 144; ++py)
        for (int px = 0; px < 224; ++px) {
            const int sx = x - 112 + px;
            int sy = y - 72 + py;
            if (sy < repeat_top)
                sy = repeat_top + ((sy - repeat_top) % 16 + 16) % 16;
            const int cx = sx >= 0 ? sx / 16 : (sx - 15) / 16;
            const int cy = sy >= 0 ? sy / 16 : (sy - 15) / 16;
            const auto cell = map.by_cell.find((cy & 255) * 256 + (cx & 255));
            if (cell != map.by_cell.end())
                image[size_t(py * 224 + px)] =
                    kGreys[map.metatiles[size_t(cell->second)][size_t((sy & 15) * 16 + (sx & 15))]];
        }
}
} // namespace sawgod::game
