// Builds the GB graphics archive from the supplied ROM and authored map metadata.
#include "game/gb_extract.h"
#include "game/gb_battle_effects.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sstream>

#include "emu/wsc.h"
#include "engine/indexed_png.h"

namespace sawgod::game {

namespace {

constexpr uint32_t GB = kGbBase;
constexpr int kSprites = 21; // heroes 0-3, monsters 4-15, people 16-20

const std::vector<uint32_t> kShades = {0xFFFFFF, 0xAAAAAA, 0x555555, 0x000000};
const std::vector<uint32_t> kSpritePalette = {0xFF00FF, 0xFFFFFF, 0xAAAAAA, 0x000000};
const std::vector<uint32_t> kFontPalette = {0xFFFFFF, 0xAAAAAA, 0x555555, 0x000000, 0xFF00FF};
const std::vector<uint32_t> kMonsterPalette = {0xFF00FF, 0xFFFFFF, 0xAAAAAA, 0x555555, 0x000000};

uint16_t word(const std::vector<uint8_t> &rom, uint32_t a) { return uint16_t(rom[a] | rom[a + 1] << 8); }

std::vector<uint8_t> bytes(const std::string &s) { return std::vector<uint8_t>(s.begin(), s.end()); }

// One 2bpp GB tile row: pixel x of the row whose bit planes are lo/hi.
int px2(uint8_t lo, uint8_t hi, int x) { return (lo >> (7 - x) & 1) | (hi >> (7 - x) & 1) << 1; }

// --- sprites -------------------------------------------------------------------------------------------
void sprites(const std::vector<uint8_t> &rom, std::map<std::string, std::vector<uint8_t>> &out) {
    IndexedImage img{64, 16 * kSprites, std::vector<uint8_t>(size_t(64 * 16 * kSprites), 0)};
    for (int s = 0; s < kSprites; ++s)
        for (int pose = 0; pose < 4; ++pose)
            for (int q = 0; q < 4; ++q) { // TL, TR, BL, BR
                const uint32_t base = GB + 0x6000 + uint32_t(s * 0x100 + pose * 0x40 + q * 16);
                for (int y = 0; y < 8; ++y)
                    for (int x = 0; x < 8; ++x)
                        img.pixels[size_t((s * 16 + (q / 2) * 8 + y) * 64 + pose * 16 + (q % 2) * 8 + x)] =
                            uint8_t(px2(rom[base + uint32_t(y * 2)], rom[base + uint32_t(y * 2 + 1)], x));
            }
    out["gb/sprites.png"] = encode_indexed_png(img, kSpritePalette);
    std::string csv = "type,gb\n";
    for (int t = 0; t < 200; ++t)
        csv += std::to_string(t) + "," + std::to_string(rom[GB + 0xB438 + uint32_t(t)] >> 4) + "\n";
    out["gb/sprite_types.csv"] = bytes(csv);
}

// ORIJINARU's large field actors are consecutive 16x16 pieces, not walking-character rows.
// GB room NPC records (98D0, sprite packs 9240) identify the five bosses and the large tree.
// The WSC adds empty slots to Byakko/Suzaku; vehicles use their own mirrored-half OAM table.
void field_objects(const std::vector<uint8_t> &rom, std::map<std::string, std::vector<uint8_t>> &out) {
    constexpr int bases[] = {0x7500, 0x7600, 0x7800, 0x7900, 0x7A00, 0x7B00, 0x7C00,
                             0x7D00, 0x7E80, 0x7E40, 0x7E00, 0x7F00, 0x7EC0, 0x7700};
    constexpr int counts[] = {4, 6, 4, 6, 4, 8, 8, 8, 1, 1, 1, 2, 2, 4};
    constexpr int sets[] = {43, 44, 45, 46, 47, 48, 49, 50, 52, 57, 69, 72, 74, 73};
    constexpr int n = int(std::size(sets));
    IndexedImage img{128, n * 16, std::vector<uint8_t>(size_t(128 * n * 16), 0)};
    std::string csv = "set,row,poses\n";
    for (int row = 0; row < n; ++row) {
        csv +=
            std::to_string(sets[row]) + "," + std::to_string(row) + "," + std::to_string(counts[row]) + "\n";
        for (int pose = 0; pose < counts[row]; ++pose)
            for (int q = 0; q < 4; ++q)
                for (int y = 0; y < 8; ++y)
                    for (int x = 0; x < 8; ++x) {
                        // The GB NPC records have four Byakko/Suzaku pieces at the same occupied
                        // cells as the WSC; WSC-only empty slots must not advance the GB art.
                        if ((row == 1 && (pose == 0 || pose == 5)) || (row == 3 && (pose == 3 || pose == 5)))
                            continue;
                        const int source_pose = row == 1                ? pose - 1
                                                : row == 3 && pose == 4 ? 3
                                                : row == 12             ? 0
                                                                        : pose;
                        const bool vehicle = row >= 5 && row <= 7;
                        // GB 1FC6 selects the vehicle OAM table at 2031. Front views mirror two
                        // half-tiles; the side views use four tiles and their original flips.
                        const int entry = vehicle ? rom[GB + uint32_t(0x2031 + pose * 4 + q)] : 0;
                        const int tile = vehicle ? entry & 15 : source_pose * 4 + q;
                        const int sx = entry & 0x20 ? 7 - x : x, sy = entry & 0x40 ? 7 - y : y;
                        const uint32_t at = GB + uint32_t(bases[row] + tile * 16 + sy * 2);
                        img.pixels[size_t((row * 16 + q / 2 * 8 + y) * 128 + pose * 16 + q % 2 * 8 + x)] =
                            uint8_t(px2(rom[at], rom[at + 1], sx));
                    }
    }
    out["gb/objects.png"] = encode_indexed_png(img, kSpritePalette);
    out["gb/object_sets.csv"] = bytes(csv);
    // Title and field-menu OAM tiles 7C-7F are copied from GB 7FC0-7FFF, in TL/TR/BL/BR order.
    IndexedImage hand{16, 16, std::vector<uint8_t>(256, 0)};
    for (int q = 0; q < 4; ++q)
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                const uint32_t at = GB + uint32_t(0x7FC0 + q * 16 + y * 2);
                hand.pixels[size_t((q / 2 * 8 + y) * 16 + q % 2 * 8 + x)] =
                    uint8_t(px2(rom[at], rom[at + 1], x));
            }
    out["gb/hand.png"] = encode_indexed_png(hand, kSpritePalette);
    // Room 82's child's bodies and the diary/Nuclear Bomb body use pack 4, slot 6,
    // fixed pose 2: GB 7E80 (the same tile group previously labeled meat).
    IndexedImage corpse{16, 16, std::vector<uint8_t>(256, 0)};
    for (int q = 0; q < 4; ++q)
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                const uint32_t at = GB + uint32_t(0x7E80 + q * 16 + y * 2);
                corpse.pixels[size_t((q / 2 * 8 + y) * 16 + q % 2 * 8 + x)] =
                    uint8_t(px2(rom[at], rom[at + 1], x));
            }
    out["gb/corpse.png"] = encode_indexed_png(corpse, kSpritePalette);
}

// --- font ------------------------------------------------------------------------------------------------
// The WSC font codes' kana (codes 0x00-0x4F hiragana, 0x50- katakana), as UTF-8 code points.
const char32_t kHira[] =
    U"あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわをん"
    U"がぎぐげござじずぜぞだぢづでどばびぶべぼぱぴぷぺぽぁぃぅぇぉっゃゅょ";
const char32_t kGojuon[] =
    U"あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわをん";
const char32_t kVoiced[] = U"がぎぐげござじずぜぞだぢづでどばびぶべぼぱぴぷぺぽ";
const char32_t kUnvoiced[] = U"かきくけこさしすせそたちつてとはひふへほはひふへほ";
const char32_t kGbKata[] =
    U"アイウエオカキクケコサシスセソタチツテトナニヌネノハヒフ.ホマミムメモヤユヨラリルレロワ.ン";

int index_of(const char32_t *s, char32_t c) {
    for (int i = 0; s[i]; ++i)
        if (s[i] == c)
            return i;
    return -1;
}
char32_t to_hira(char32_t c) { return c >= U'ァ' && c <= U'ヶ' ? c - 0x60 : c; }
char32_t to_kata(char32_t c) { return c >= U'ぁ' && c <= U'ゖ' ? c + 0x60 : c; }

int gb_code(char32_t c) {
    static const std::map<char32_t, int> other = {{U'っ', 0xB8}, {U'ゃ', 0xB9}, {U'ゅ', 0xBA}, {U'ょ', 0xBB},
                                                  {U'ァ', 0xEA}, {U'ィ', 0xEB}, {U'ッ', 0xEC}, {U'ャ', 0xED},
                                                  {U'ュ', 0xEE}, {U'ョ', 0xEF}, {U'ェ', 0xF5}, {U'ォ', 0xF6},
                                                  {U'ヘ', 0xA6}, {U'ヲ', 0xB6}};
    if (auto it = other.find(c); it != other.end())
        return it->second;
    if (int i = index_of(kGojuon, c); i >= 0)
        return 0x8A + i;
    if (c >= U'ァ' && c <= U'ヶ') {
        const int i = index_of(kGojuon, c - 0x60);
        if (i >= 0 && kGbKata[i] == c)
            return 0xBC + i;
    }
    return -1;
}

using Glyph = std::array<std::array<int, 8>, 8>;

void font(const std::vector<uint8_t> &rom, std::map<std::string, std::vector<uint8_t>> &out) {
    constexpr uint32_t kWscFont = 0x390080; // 9000:0080, 32 bytes a code (4bpp packed)
    // ORIJINARU's menu frame tiles 0x177-0x17F, which it copies from GB 0xF4B8 (2bpp)
    const auto frame_tile = [&](int t) {
        Glyph g{};
        const uint32_t b = GB + 0xF4B8 + uint32_t((t - 0x177) * 16);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                g[size_t(y)][size_t(x)] = px2(rom[b + uint32_t(y * 2)], rom[b + uint32_t(y * 2 + 1)], x);
        return g;
    };
    const auto wsc_glyph = [&](int code) {
        Glyph g{};
        const uint32_t b = kWscFont + uint32_t(code * 32);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                g[size_t(y)][size_t(x)] = rom[b + uint32_t(y * 4 + x / 2)] >> (x % 2 == 0 ? 4 : 0) & 15;
        return g;
    };
    const auto gb_glyph = [&](int code) { // 1bpp: black on white
        Glyph g{};
        const uint32_t b = GB + 0xF100 + uint32_t((code - 0x80) * 8);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                g[size_t(y)][size_t(x)] = rom[b + uint32_t(y)] >> (7 - x) & 1 ? 3 : 0;
        return g;
    };
    std::map<int, char32_t> chars;
    for (int i = 0; kHira[i]; ++i) {
        chars[i] = kHira[i];
        chars[0x50 + i] = to_kata(kHira[i]);
    }
    static const std::map<int, int> frame = {{0xD5, 0x177}, {0xD6, 0x178}, {0xD7, 0x179},
                                             {0xD8, 0x17A}, {0xD9, 0x17F}, {0xDA, 0x17B},
                                             {0xDB, 0x17C}, {0xDC, 0x17D}, {0xDD, 0x17E}};
    static const std::map<int, int> punct = {{0xCB, 0xF3}, {0xCC, 0xF4}, {0xD3, 0xF2}, {0xC6, 0xE8}};
    IndexedImage img{128, 112, std::vector<uint8_t>(128 * 112, 4)};
    for (int code = 0; code < 0xE0; ++code) {
        bool have = true;
        Glyph cell{};
        const auto ch = chars.count(code) ? chars[code] : char32_t(0);
        if (auto f = frame.find(code); f != frame.end()) {
            cell = frame_tile(f->second);
        } else if (auto p = punct.find(code); p != punct.end()) {
            cell = gb_glyph(p->second);
        } else if (code >= 0xBB && code <= 0xC4) {
            cell = gb_glyph(0x80 + code - 0xBB);
        } else if (ch && gb_code(ch) >= 0) {
            cell = gb_glyph(gb_code(ch));
        } else if (ch && index_of(kVoiced, to_hira(ch)) >= 0) {
            char32_t base = kUnvoiced[index_of(kVoiced, to_hira(ch))];
            base = ch == to_hira(ch) ? base : to_kata(base);
            if (gb_code(base) >= 0) { // the GB base kana with the WSC glyph's (han)dakuten (top right)
                cell = gb_glyph(gb_code(base));
                const Glyph voiced = wsc_glyph(code);
                int plain_code = -1;
                for (const auto &[k, v] : chars)
                    if (v == base) {
                        plain_code = k;
                        break;
                    }
                const Glyph plain = wsc_glyph(plain_code);
                for (int y = 0; y < 8; ++y)
                    for (int x = 4; x < 8; ++x)
                        if (voiced[size_t(y)][size_t(x)] == 2 && plain[size_t(y)][size_t(x)] != 2)
                            cell[size_t(y)][size_t(x)] = 3;
            } else {
                have = false;
            }
        } else {
            have = false;
        }
        if (!have)
            continue;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                img.pixels[size_t(((code / 16) * 8 + y) * 128 + (code % 16) * 8 + x)] =
                    uint8_t(cell[size_t(y)][size_t(x)]);
    }
    out["gb/font.png"] = encode_indexed_png(img, kFontPalette);
}

// --- monsters ----------------------------------------------------------------------------------------------
void monsters(const std::vector<uint8_t> &rom, std::map<std::string, std::vector<uint8_t>> &out) {
    struct Pic {
        int w, h;
        uint32_t base;
    };
    std::map<int, Pic> pics;
    int bases[5], sizes[5][2];
    for (int g = 0; g < 5; ++g) {
        sizes[g][0] = rom[GB + 0xB9C8 + uint32_t(2 * g)];
        sizes[g][1] = rom[GB + 0xB9C9 + uint32_t(2 * g)];
        bases[g] = word(rom, GB + 0xB9D2 + uint32_t(2 * g));
    }
    for (int g = 0; g < 5; ++g) {
        const int w = sizes[g][0], h = sizes[g][1], end = g < 4 ? bases[g + 1] : 0x8000;
        for (int k = 0; k < (end - bases[g]) / (w * h * 16); ++k)
            pics[g * 16 + k] = {w, h, GB + 0x10000 + uint32_t(bases[g] - 0x4000 + k * w * h * 16)};
    }
    const int rows = pics.rbegin()->first / 8 + 1;
    IndexedImage img{8 * 64, rows * 64, std::vector<uint8_t>(size_t(8 * 64 * rows * 64), 0)};
    for (const auto &[pic, p] : pics) {
        const int W = p.w * 8, H = p.h * 8;
        std::vector<int> px(size_t(W * H));
        for (int t = 0; t < p.w * p.h; ++t)
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                    px[size_t(((t / p.w) * 8 + y) * W + (t % p.w) * 8 + x)] =
                        px2(rom[p.base + uint32_t(t * 16 + y * 2)],
                            rom[p.base + uint32_t(t * 16 + y * 2 + 1)], x);
        // white connected to the edge is outside the monster: transparent
        std::vector<char> outside(size_t(W * H), 0);
        std::vector<std::pair<int, int>> todo;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                if ((x == 0 || x == W - 1 || y == 0 || y == H - 1) && px[size_t(y * W + x)] == 0)
                    todo.emplace_back(x, y);
        while (!todo.empty()) {
            const auto [x, y] = todo.back();
            todo.pop_back();
            if (x < 0 || y < 0 || x >= W || y >= H || outside[size_t(y * W + x)] ||
                px[size_t(y * W + x)] != 0)
                continue;
            outside[size_t(y * W + x)] = 1;
            todo.insert(todo.end(), {{x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}});
        }
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                img.pixels[size_t(((pic / 8) * 64 + y) * img.width + (pic % 8) * 64 + x)] =
                    uint8_t(outside[size_t(y * W + x)] ? 0 : px[size_t(y * W + x)] + 1);
    }
    out["gb/monsters.png"] = encode_indexed_png(img, kMonsterPalette);
    std::string pics_csv = "pic,width,height\n", types_csv = "type,pic\n";
    for (const auto &[pic, p] : pics)
        pics_csv += std::to_string(pic) + "," + std::to_string(p.w) + "," + std::to_string(p.h) + "\n";
    for (int t = 0; t < 200; ++t)
        if (pics.count(rom[GB + 0xB900 + uint32_t(t)]))
            types_csv += std::to_string(t) + "," + std::to_string(rom[GB + 0xB900 + uint32_t(t)]) + "\n";
    out["gb/monster_pics.csv"] = bytes(pics_csv);
    out["gb/monster_types.csv"] = bytes(types_csv);
}

// --- title -----------------------------------------------------------------------------------------------
// ORIJINARU's title screen, reached by its own menu (tests/movies/orijinaru_play.txt, frames 600-1500: skip
// the opening, PUSH ANY BUTTON, down twice to オリジナル, select), captured at frame 2050; the logo is rows
// 0-79 of its 160-px column, centered on white.
void title(const std::vector<uint8_t> &rom, std::map<std::string, std::vector<uint8_t>> &out) {
    static const size_t kSramSize[] = {0, 0x2000, 0x8000, 0x20000, 0x40000, 0x80000};
    const uint8_t save_type = rom[rom.size() - 16 + 0x0B];
    // Keep the emulator off the first-run worker's limited stack, including when title is inlined.
    const auto machine = std::make_unique<emu::Wsc>(rom, save_type <= 5 ? kSramSize[save_type] : 0);
    auto &wsc = *machine;
    struct Tap {
        int frame;
        uint16_t keys;
    };
    // taps hold a button for 6 frames (tools/movie.py read_movie)
    const Tap taps[] = {{600, emu::KEY_START},
                        {1200, emu::KEY_A},
                        {1300, emu::KEY_X3},
                        {1340, emu::KEY_X3},
                        {1500, emu::KEY_A}};
    uint16_t held = 0;
    for (int done = 0; done < 2050;) {
        wsc.set_keys(held);
        wsc.run_frame();
        ++done;
        held = 0;
        for (const Tap &t : taps)
            if (done >= t.frame && done < t.frame + 6)
                held = t.keys;
    }
    const std::vector<uint32_t> rgb = wsc.framebuffer_rgb(emu::Wsc::ColorMode::Lcd);
    IndexedImage img{224, 144, std::vector<uint8_t>(224 * 144, 0)};
    static const int kLevels[4] = {240, 160, 80, 0};
    for (int y = 0; y < 80; ++y)
        for (int x = 32; x < 192; ++x) {
            const uint32_t c = rgb[size_t(y * emu::Wsc::kWidth + x)];
            // PIL's "L": (R * 299 + G * 587 + B * 114) / 1000
            const int v = int(((c >> 16 & 0xFF) * 299 + (c >> 8 & 0xFF) * 587 + (c & 0xFF) * 114) / 1000);
            int best = 0;
            for (int s = 1; s < 4; ++s)
                if (std::abs(v - kLevels[s]) < std::abs(v - kLevels[best]))
                    best = s;
            img.pixels[size_t(y * 224 + x)] = uint8_t(best);
        }
    out["gb/title.png"] = encode_indexed_png(img, kShades);
}

// --- field maps --------------------------------------------------------------------------------------------
std::vector<std::vector<std::string>> csv_rows(const std::string &text) {
    std::vector<std::vector<std::string>> rows;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        std::vector<std::string> row;
        size_t at = 0;
        while (true) {
            const size_t comma = line.find(',', at);
            row.push_back(line.substr(at, comma == std::string::npos ? std::string::npos : comma - at));
            if (comma == std::string::npos)
                break;
            at = comma + 1;
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

bool to_int(const std::string &s, int *v) {
    char *end = nullptr;
    const long n = std::strtol(s.c_str(), &end, 10);
    if (s.empty() || *end)
        return false;
    *v = int(n);
    return true;
}

} // namespace

int gb_room_tileset(const std::vector<uint8_t> &rom, int room) {
    const uint32_t record = GB + word(rom, GB + 0x98D0 + uint32_t(2 * room)) + 0x4000;
    return rom[record + 2];
}

std::array<GbMetatile, 32> gb_tileset_metatiles(const std::vector<uint8_t> &rom, int set) {
    const uint32_t address = GB + word(rom, GB + 0x8820 + uint32_t(set * 2)) + 0x4000;
    const int pages[4] = {rom[address] >> 4, rom[address] & 15, rom[address + 1] >> 4, rom[address + 1] & 15};
    const auto pool = [&](int tile_byte) { // the 256-tile pool: four 1 KB pages of GB bank 1
        return GB + 0x4000 + uint32_t(pages[tile_byte / 1024] * 1024 + tile_byte % 1024);
    };
    std::array<GbMetatile, 32> out{};
    for (int g = 0; g < 32; ++g)
        for (int q = 0; q < 4; ++q) {
            const int base = rom[address + 2 + uint32_t(g * 4 + q)] * 16;
            for (int y = 0; y < 8; ++y) {
                const uint8_t lo = rom[pool(base + y * 2)], hi = rom[pool(base + y * 2 + 1)];
                for (int x = 0; x < 8; ++x)
                    out[size_t(g)][size_t((q / 2 * 8 + y) * 16 + q % 2 * 8 + x)] = uint8_t(px2(lo, hi, x));
            }
        }
    return out;
}

std::map<std::string, std::vector<uint8_t>> extract_gb_shared(const std::vector<uint8_t> &rom,
                                                              const std::function<void(float)> &progress) {
    // shares of the time (measured): the title replay about 15%, the battle effects the rest
    constexpr float kTitleEnd = 0.15f;
    std::map<std::string, std::vector<uint8_t>> out;
    sprites(rom, out);
    field_objects(rom, out);
    font(rom, out);
    monsters(rom, out);
    title(rom, out);
    if (progress)
        progress(kTitleEnd);
    out.merge(extract_gb_battle_effects(rom, [&](float f) {
        if (progress)
            progress(kTitleEnd + (1 - kTitleEnd) * f);
    }));
    return out;
}

bool build_gb_field_maps(const std::vector<uint8_t> &rom, const std::map<std::string, std::string> &assets,
                         std::map<std::string, std::vector<uint8_t>> *out, std::string *error) {
    std::map<int, std::array<GbMetatile, 32>> sets;
    for (const auto &[name, text] : assets) {
        if (name.size() != 13 || name.substr(3) != "_strip.csv")
            continue;
        const std::string id = name.substr(0, 3);
        const auto rows = csv_rows(text);
        if (rows.empty() || rows[0] != std::vector<std::string>{"set", "metatile", "flip"} ||
            rows.size() < 2) {
            *error = name + ": needs set,metatile,flip rows";
            return false;
        }
        IndexedImage strip{int(rows.size() - 1) * 16, 16, std::vector<uint8_t>((rows.size() - 1) * 256, 0)};
        for (size_t r = 1; r < rows.size(); ++r) {
            int set = 0, g = 0, flip = 0;
            if (rows[r].size() != 3 || !to_int(rows[r][0], &set) || !to_int(rows[r][1], &g) ||
                !to_int(rows[r][2], &flip) || set < 0 || set > 15 || g < 0 || g > 31 || flip < 0 ||
                flip > 3) {
                *error = name + ": invalid row " + std::to_string(r);
                return false;
            }
            if (!sets.count(set))
                sets[set] = gb_tileset_metatiles(rom, set);
            const GbMetatile &t = sets[set][size_t(g)];
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) {
                    const int sy = flip & 2 ? 15 - y : y, sx = flip & 1 ? 15 - x : x;
                    strip.pixels[size_t(y * strip.width + int(r - 1) * 16 + x)] = t[size_t(sy * 16 + sx)];
                }
        }
        (*out)["gb/" + id + "_metatiles.png"] = encode_indexed_png(strip, kShades);
        const auto copy = [&](const std::string &kind) {
            if (auto it = assets.find(id + "_" + kind + ".csv"); it != assets.end())
                (*out)["gb/" + id + "_" + kind + ".csv"] = bytes(it->second);
        };
        copy("lookup");
        copy("hidden");
        if (auto it = assets.find(id + "_cells.csv"); it != assets.end()) { // grid -> x,y,gb
            const auto grid = csv_rows(it->second);
            if (grid.size() != 80) {
                *error = id + "_cells.csv: needs 80 rows";
                return false;
            }
            std::string cells = "x,y,gb\n";
            for (int y = 0; y < 80; ++y) {
                if (grid[size_t(y)].size() != 80) {
                    *error = id + "_cells.csv: row " + std::to_string(y) + " needs 80 values";
                    return false;
                }
                for (int x = 0; x < 80; ++x) {
                    int v = 0;
                    if (!to_int(grid[size_t(y)][size_t(x)], &v)) {
                        *error = id + "_cells.csv: invalid value";
                        return false;
                    }
                    if (v >= 0)
                        cells += std::to_string(x - 8) + "," + std::to_string(y - 8) + "," +
                                 std::to_string(v) + "\n";
                }
            }
            (*out)["gb/" + id + "_cells.csv"] = bytes(cells);
        }
    }
    return true;
}

} // namespace sawgod::game
