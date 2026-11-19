#include "engine/ui_skin.h"
#include "engine/indexed_png.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>

namespace sawgod::ui {

thread_local std::function<void(const Skin::Draw &)> Skin::observe;

namespace {

constexpr uint32_t kFontRom = 0x390080;    // 256 tiles, 4bpp packed, 32 bytes each
constexpr uint32_t kPaletteRom = 0x390000; // 4 palettes x 16 colors (0x0RGB words); palette 0 here

// Atlas: every 8x8 tile in a 12x12 cell (its edge pixels repeated around it, so smooth sampling never
// picks up a neighbor), the whole atlas then enlarged kUp times with nearest-neighbour. Sampled smoothly,
// that draws the font at any size with crisp pixels and only the edges softened ("sharp bilinear"), which
// the menu needs: it is drawn straight at the window's resolution, at non-integer scales.
// Regions (in 12-pixel cells): the 256 font tiles as they are at (0,0); the same tiles as glyphs (the window
// texture, colors 9-11, and color 0 transparent) at (16,0); the tiles with the window texture
// see-through at (0,16); drawn punctuation from (16,16), and a white cell for solid fills at (16,17).
constexpr int kCell = 12;
constexpr int kUp = 4;
constexpr int kAtlasCells = 32;
constexpr int kAtlasPx = kAtlasCells * kCell; // before enlarging
constexpr int kAtlasW = kAtlasPx * kUp, kAtlasH = kAtlasPx * kUp;
constexpr int kOpaqueCX = 0, kGlyphCX = 16, kSheerCY = 16, kExtraCX = 16, kExtraCY = 16, kWhiteCX = 16,
              kWhiteCY = 17, kLowerCX = 16, kLowerCY = 18, kM5x7CX = 16, kM5x7CY = 20;

// m5x7 (Daniel Linssen, CC0; the overlay's font, as the game's own resource
// text): every glyph it has, each in a 10x10 footprint inside a padded cell (9 rows from the cap top plus the
// shadow row, 8 columns plus the shadow column), 16 per row of cells from (kM5x7CX, kM5x7CY). Characters it
// lacks (kana) use the WSC font.
struct M5x7 {
    char32_t cp;
    uint8_t advance;
    uint8_t rows[9]; // from the cap top; bit 7 = leftmost pixel
};
constexpr M5x7 kM5x7[] = {
#include "engine/font_m5x7.inc"
};
constexpr int kM5x7Count = int(sizeof kM5x7 / sizeof kM5x7[0]);
static_assert(kM5x7Count <= 16 * 12, "m5x7 glyphs overflow the atlas");
constexpr int kM5x7Left = 1; // as the game's text (game/text_render.cpp): one pixel right of the pen

// Index of `cp` in kM5x7 (full-width ASCII as ASCII), or -1.
int m5x7_of(char32_t cp) {
    if (cp == U'　')
        cp = U' ';
    if (cp >= U'！' && cp <= U'～')
        cp = char32_t(cp - U'！' + U'!');
    for (int k = 0; k < kM5x7Count; ++k)
        if (kM5x7[k].cp == cp)
            return k;
    return -1;
}

// Our own lower-case letters (assets/ui/font_lowercase.txt): "a" then 8 rows of X/. per letter.
constexpr const char *kLowercase =
#include "ui_font_lowercase.inc"
    ;

// The 8 rows of a lower-case letter's white strokes, from kLowercase.
std::array<std::string, 8> lowercase_rows(char letter) {
    std::array<std::string, 8> rows;
    std::string_view s(kLowercase);
    size_t pos = 0;
    while (pos < s.size()) {
        size_t nl = s.find('\n', pos);
        std::string_view line =
            s.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = nl == std::string_view::npos ? s.size() : nl + 1;
        if (line.size() == 1 && line[0] == letter) {
            for (int y = 0; y < 8 && pos < s.size(); ++y) {
                nl = s.find('\n', pos);
                rows[y] = std::string(
                    s.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos));
                pos = nl == std::string_view::npos ? s.size() : nl + 1;
            }
            break;
        }
    }
    return rows;
}
constexpr uint32_t kSheerAlpha = 230; // 90% opaque: the blurred game shows faintly through menu windows

// Window-frame tiles: top-left, top, top-right, left, center, right, bottom-left, bottom, bottom-right.
constexpr uint8_t kFrameWhite[9] = {0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD};
constexpr uint8_t kFrameOrange[9] = {0xDE, 0xDF, 0xE0, 0xE1, 0xD9, 0xE2, 0xE3, 0xE4, 0xE5};
constexpr uint8_t kFrameGrey[9] = {0xE6, 0xE7, 0xE8, 0xE9, 0xD9, 0xEA, 0xEB, 0xEC, 0xED};
constexpr uint8_t kHand[4] = {0xFB, 0xFC, 0xFD, 0xFE}; // 2x2, pointing right

// The system charset (tools/sawgod/charset.py): codes 0x00-0x4F hiragana, 0x50-0x9F katakana.
constexpr const char *kHira =
    "あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわをん"
    "がぎぐげござじずぜぞだぢづでどばびぶべぼぱぴぷぺぽぁぃぅぇぉっゃゅょ";
constexpr const char *kKata =
    "アイウエオカキクケコサシスセソタチツテトナニヌネノハヒフヘホマミムメモヤユヨラリルレロワヲン"
    "ガギグゲゴザジズゼゾダヂヅデドバビブベボパピプペポァィゥェォッャュョ";

// Drawn punctuation: white strokes ('X'); the shadow is added below and right like the font's.
struct Extra {
    char32_t cp;
    const char *rows[8];
};
constexpr Extra kExtras[] = {
    {U'.', {"........", "........", "........", "........", "........", "..XX....", "..XX....", "........"}},
    {U',', {"........", "........", "........", "........", "..XX....", "..XX....", "...X....", "..X....."}},
    {U'(', {"....XX..", "...XX...", "..XX....", "..XX....", "..XX....", "...XX...", "....XX..", "........"}},
    {U')', {"..XX....", "...XX...", "....XX..", "....XX..", "....XX..", "...XX...", "..XX....", "........"}},
    {U'+', {"........", "...XX...", "...XX...", ".XXXXXX.", ".XXXXXX.", "...XX...", "...XX...", "........"}},
    {U'\'', {"...XX...", "...XX...", "...X....", "........", "........", "........", "........", "........"}},
    {U';', {"........", "........", "..XX....", "..XX....", "........", "..XX....", "..XX....", "..X....."}},
    {U'=', {"........", "........", ".XXXXXX.", "........", ".XXXXXX.", "........", "........", "........"}},
    {U'<', {".....X..", "....XX..", "...XXX..", "..XXXX..", "...XXX..", "....XX..", ".....X..", "........"}},
    {U'>', {"..X.....", "..XX....", "..XXX...", "..XXXX..", "..XXX...", "..XX....", "..X.....", "........"}},
    {U'"', {".XX.XX..", ".XX.XX..", "........", "........", "........", "........", "........", "........"}},
};
constexpr int kExtraCount = int(sizeof kExtras / sizeof kExtras[0]);

char32_t next_codepoint(std::string_view s, size_t &i) {
    const auto c = uint8_t(s[i++]);
    if (c < 0x80)
        return c;
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
    char32_t cp = c & (0x3F >> extra);
    for (int k = 0; k < extra && i < s.size(); ++k)
        cp = (cp << 6) | (uint8_t(s[i++]) & 0x3F);
    return cp;
}

// Index of `cp` in a UTF-8 string of single code points, or -1.
int index_in(const char *list, char32_t cp) {
    std::string_view s(list);
    size_t i = 0;
    for (int n = 0; i < s.size(); ++n)
        if (next_codepoint(s, i) == cp)
            return n;
    return -1;
}

// Lower-case letters are proportional (the upper-case font is fixed at 8): {first stroke column, advance}.
// The advance covers the strokes, their 1-pixel shadow and a 1-pixel gap.
struct LowerMetrics {
    int left, advance;
};
const std::array<LowerMetrics, 26> &lower_metrics() {
    static const std::array<LowerMetrics, 26> m = [] {
        std::array<LowerMetrics, 26> out{};
        for (int k = 0; k < 26; ++k) {
            int lo = 8, hi = -1;
            for (const std::string &row : lowercase_rows(char('a' + k)))
                for (int x = 0; x < int(row.size()) && x < 8; ++x)
                    if (row[x] == 'X') {
                        lo = std::min(lo, x);
                        hi = std::max(hi, x);
                    }
            out[k] = hi < 0 ? LowerMetrics{0, kGlyph} : LowerMetrics{lo, hi - lo + 3};
        }
        return out;
    }();
    return m;
}

// Glyph for a code point: 0-255 = font tile, 256 + k = drawn extra k, 512 + k = our lower-case letter k,
// -1 = blank (space).
int glyph_of(char32_t cp) {
    if (cp >= 0xE000 && cp < 0xE100)
        return int(cp - 0xE000);
    if (cp == U' ' || cp == U'　')
        return -1;
    if (cp >= U'a' && cp <= U'z')
        return 512 + int(cp - U'a');
    if (cp >= U'A' && cp <= U'Z')
        return 0xA1 + int(cp - U'A');
    if (cp >= U'Ａ' && cp <= U'Ｚ')
        return 0xA1 + int(cp - U'Ａ');
    if (cp >= U'0' && cp <= U'9')
        return 0xBB + int(cp - U'0');
    if (cp >= U'０' && cp <= U'９')
        return 0xBB + int(cp - U'０');
    switch (cp) {
    case U'!':
    case U'！':
        return 0xCB;
    case U'?':
    case U'？':
        return 0xCC;
    case U'%':
        return 0xCD;
    case U'@':
        return 0xC5;
    case U'/':
    case U'／':
        return 0xD2;
    case U'-':
    case U'ー':
        return 0xD3;
    case U':':
    case U'：':
        return 0xD4;
    case U'「':
        return 0xC6;
    case U'」':
        return 0xC7;
    case U'、':
        return 0xC8;
    case U'。':
        return 0xC9;
    case U'♥':
        return 0xD1;
    default:
        break;
    }
    if (int k = index_in(kHira, cp); k >= 0)
        return k;
    if (int k = index_in(kKata, cp); k >= 0)
        return 0x50 + k;
    for (int k = 0; k < kExtraCount; ++k)
        if (kExtras[k].cp == cp)
            return 256 + k;
    return 0xCC; // '?'
}

} // namespace

void Skin::release() {
    if (texture_)
        SDL_DestroyTexture(texture_);
    if (texture_gb_)
        SDL_DestroyTexture(texture_gb_);
    texture_ = texture_gb_ = nullptr;
}

bool Skin::build(SDL_Renderer *renderer, const std::vector<uint8_t> &rom,
                 const std::vector<uint8_t> &gb_hand) {
    if (rom.size() < kFontRom + 256 * 32)
        return false;
    // palette 0, as Wsc::framebuffer_rgb(Raw) converts the screen
    std::array<uint32_t, 16> rgba{}; // R G B A bytes in memory order
    for (int i = 0; i < 16; ++i) {
        const uint16_t w = uint16_t(rom[kPaletteRom + i * 2] | rom[kPaletteRom + i * 2 + 1] << 8);
        const uint32_t r = (w >> 8) & 15, g = (w >> 4) & 15, b = w & 15;
        const uint32_t R = r * 17, G = g * 17, B = b * 17;
        palette_[i] = IM_COL32(R, G, B, 255);
        rgba[i] = palette_[i]; // IM_COL32 is R in the low byte: the same byte order as SDL_PIXELFORMAT_RGBA32
    }

    std::vector<uint32_t> px(size_t(kAtlasPx) * kAtlasPx, 0);
    // One 8x8 tile into cell (cx, cy), its edges extruded into a 1-pixel border.
    auto cell = [&](int cx, int cy, auto &&pixel) {
        for (int y = -1; y <= 8; ++y)
            for (int x = -1; x <= 8; ++x)
                px[size_t(cy * kCell + 1 + y) * kAtlasPx + cx * kCell + 1 + x] =
                    pixel(std::clamp(x, 0, 7), std::clamp(y, 0, 7));
    };
    for (int t = 0; t < 256; ++t) {
        const uint8_t *d = &rom[kFontRom + t * 32];
        auto idx = [&](int x, int y) {
            const uint8_t byte = d[y * 4 + x / 2];
            return (x & 1) ? byte & 15 : byte >> 4;
        };
        fill_masks_[size_t(t)] = 0;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                if (const int i = idx(x, y); i == 9 || i == 10 || i == 11)
                    fill_masks_[size_t(t)] |= uint64_t(1) << (y * 8 + x);
        cell(kOpaqueCX + t % 16, t / 16, [&](int x, int y) { return idx(x, y) == 0 ? 0u : rgba[idx(x, y)]; });
        cell(kGlyphCX + t % 16, t / 16, [&](int x, int y) {
            const int i = idx(x, y);
            return i == 0 || i == 9 || i == 10 || i == 11 ? 0u : rgba[i];
        });
        cell(kOpaqueCX + t % 16, kSheerCY + t / 16, [&](int x, int y) {
            const int i = idx(x, y);
            if (i == 0)
                return 0u;
            return i == 9 || i == 10 || i == 11 ? (rgba[i] & 0x00FFFFFFu) | (kSheerAlpha << 24) : rgba[i];
        });
    }
    for (int k = 0; k < kExtraCount; ++k) {
        const Extra &e = kExtras[k];
        auto white = [&](int x, int y) { return x >= 0 && y >= 0 && x < 8 && y < 8 && e.rows[y][x] == 'X'; };
        cell(kExtraCX + k, kExtraCY, [&](int x, int y) {
            if (white(x, y))
                return rgba[2];
            return white(x - 1, y) || white(x, y - 1) ? rgba[1] : 0u;
        });
    }
    cell(kWhiteCX, kWhiteCY, [](int, int) { return uint32_t(IM_COL32_WHITE); });
    for (int k = 0; k < 26; ++k) {
        const std::array<std::string, 8> rows = lowercase_rows(char('a' + k));
        auto white = [&](int x, int y) {
            return x >= 0 && y >= 0 && x < 8 && y < 8 && size_t(x) < rows[y].size() && rows[y][x] == 'X';
        };
        cell(kLowerCX + k % 16, kLowerCY + k / 16, [&](int x, int y) {
            if (white(x, y))
                return rgba[2];
            return white(x - 1, y) || white(x, y - 1) ? rgba[1] : 0u;
        });
    }

    for (int k = 0; k < kM5x7Count; ++k) {
        const M5x7 &g = kM5x7[k];
        auto white = [&](int x, int y) {
            x -= kM5x7Left;
            return x >= 0 && y >= 0 && x < 8 && y < 9 && (g.rows[y] >> (7 - x) & 1);
        };
        const int ox = (kM5x7CX + k % 16) * kCell + 1, oy = (kM5x7CY + k / 16) * kCell + 1;
        for (int y = 0; y < 10; ++y)
            for (int x = 0; x < 10; ++x)
                px[size_t(oy + y) * kAtlasPx + ox + x] = white(x, y)                          ? rgba[2]
                                                         : white(x - 1, y) || white(x, y - 1) ? rgba[1]
                                                                                              : 0u;
    }

    std::vector<uint32_t> big(size_t(kAtlasW) * kAtlasH);
    for (int y = 0; y < kAtlasH; ++y)
        for (int x = 0; x < kAtlasW; ++x)
            big[size_t(y) * kAtlasW + x] = px[size_t(y / kUp) * kAtlasPx + x / kUp];

    if (!texture_) {
        texture_ =
            SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, kAtlasW, kAtlasH);
        if (!texture_)
            return false;
        SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
    }
    if (!SDL_UpdateTexture(texture_, nullptr, big.data(), kAtlasW * 4))
        return false;
    // GB style: the glyph half of the atlas (cells from column 16) with white strokes and no shadow
    for (int y = 0; y < kAtlasH; ++y)
        for (int x = kGlyphCX * kCell * kUp; x < kAtlasW; ++x) {
            uint32_t &p = big[size_t(y) * kAtlasW + x];
            p = p == 0 || p == rgba[1] ? 0u : uint32_t(IM_COL32_WHITE);
        }
    // The hand keeps its ROM sprite colors; it is never an ink-tinted font glyph.
    IndexedImage hand;
    std::string error;
    has_gb_hand_ = !gb_hand.empty() &&
                   decode_indexed_png(gb_hand, {0xFF00FF, 0xFFFFFF, 0xAAAAAA, 0x000000}, &hand, &error) &&
                   hand.width == 16 && hand.height == 16;
    if (has_gb_hand_) {
        constexpr uint32_t colors[] = {0, IM_COL32(255, 255, 255, 255), IM_COL32(170, 170, 170, 255),
                                       IM_COL32(0, 0, 0, 255)};
        for (int q = 0; q < 4; ++q) {
            const int t = kHand[q], cx = kGlyphCX + t % 16, cy = t / 16;
            for (int y = -1; y <= 8; ++y)
                for (int x = -1; x <= 8; ++x) {
                    const auto color = colors[hand.pixels[size_t((q / 2 * 8 + std::clamp(y, 0, 7)) * 16 +
                                                                 q % 2 * 8 + std::clamp(x, 0, 7))]];
                    for (int dy = 0; dy < kUp; ++dy)
                        for (int dx = 0; dx < kUp; ++dx)
                            big[size_t((cy * kCell + 1 + y) * kUp + dy) * kAtlasW +
                                (cx * kCell + 1 + x) * kUp + dx] = color;
                }
        }
    }
    if (!texture_gb_) {
        texture_gb_ =
            SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, kAtlasW, kAtlasH);
        if (!texture_gb_)
            return false;
        SDL_SetTextureBlendMode(texture_gb_, SDL_BLENDMODE_BLEND);
    }
    return SDL_UpdateTexture(texture_gb_, nullptr, big.data(), kAtlasW * 4);
}

void Skin::tile(ImDrawList *dl, float x, float y, int cx, int cy, float scale, ImU32 tint, bool ink) const {
    const ImVec2 uv0(float((cx * kCell + 1) * kUp) / kAtlasW, float((cy * kCell + 1) * kUp) / kAtlasH);
    const ImVec2 uv1(float((cx * kCell + 9) * kUp) / kAtlasW, float((cy * kCell + 9) * kUp) / kAtlasH);
    const float s = kGlyph * scale;
    dl->AddImage(ink ? ink_texture() : texture_id(), ImVec2(x, y), ImVec2(x + s, y + s), uv0, uv1,
                 ink ? ink_tint(tint) : tint);
}

float Skin::icon(ImDrawList *dl, float x, float y, const ButtonIcon &icon, ImU32 tint, float scale,
                 std::string_view label) const {
    tint = ink_tint(tint);
    if (observe && !label.empty())
        observe({Draw::Kind::Text, x, y + kIconRise * scale, icon_advance(icon, scale), kGlyph * scale,
                 std::string(label), tint, scale});
    const float ox = x + float(icon.dx) * scale, oy = y + float(icon.dy) * scale;
    // the Icon Type's colors (none in the GB style)
    const auto rgb = [](uint32_t c) { return IM_COL32(c >> 16, (c >> 8) & 0xFF, c & 0xFF, 255); };
    const ImU32 body = icon.body && !gb_ ? rgb(*icon.body) : tint;
    const ImU32 shadow = icon.shadow && !gb_ ? rgb(*icon.shadow) : color(1);
    const bool symbol = icon.symbol && !gb_;
    // a stick's arc: its own color, and in the GB style a gray
    const ImU32 arc = gb_ ? kGbGray : icon.motion ? rgb(*icon.motion) : body;
    for (int pass = gb_ ? 1 : 0; pass < 2; ++pass) // the shadow first, then the ink over it
        for (int r = 0; r < icon.h; ++r)
            for (int c = 0; c < icon.w; ++c) {
                const uint8_t v = icon.at(c, r);
                if (v == ButtonIcon::Clear || (v == ButtonIcon::Symbol && (pass == 0 || !symbol)) ||
                    (pass == 0 && v == ButtonIcon::InkNoShadow))
                    continue;
                const float px = ox + float(c + (pass ? 0 : 1)) * scale,
                            py = oy + float(r + (pass ? 0 : 1)) * scale;
                const ImU32 col = pass == 0                      ? shadow
                                  : v == ButtonIcon::Symbol      ? rgb(*icon.symbol)
                                  : v == ButtonIcon::InkNoShadow ? arc
                                                                 : body;
                dl->AddRectFilled(ImVec2(px, py), ImVec2(px + scale, py + scale), col);
            }
    return icon_advance(icon, scale);
}

void Skin::text(ImDrawList *dl, float x, float y, std::string_view s, ImU32 tint, float scale) const {
    if (s.find('{') != std::string_view::npos) { // button tokens: the runs between them, and their icons
        if (observe)
            observe({Draw::Kind::Text, x, y, text_width(s, scale), kGlyph * scale, std::string(s),
                     ink_tint(tint), scale});
        const auto saved = std::move(observe);
        size_t begin = 0;
        for (size_t i = 0; i <= s.size(); ++i) {
            TextToken token;
            const bool is_token = i < s.size() && text_token_at(s.substr(i), &token);
            if (i < s.size() && !is_token)
                continue;
            text(dl, x, y, s.substr(begin, i - begin), tint, scale);
            x += text_width(s.substr(begin, i - begin), scale);
            if (is_token) {
                if (const ButtonIcon b = text_token_icon(token); b.empty()) {
                    const std::string t = text_token_text(token);
                    text(dl, x, y, t, tint, scale);
                    x += text_width(t, scale);
                } else {
                    x += icon(dl, x, y - kIconRise * scale, b, tint, scale);
                }
                i += token.len - 1;
                begin = i + 1;
            }
        }
        observe = saved;
        return;
    }
    tint = ink_tint(tint);
    if (observe)
        observe({Draw::Kind::Text, x, y, text_width(s, scale), kGlyph * scale, std::string(s), tint, scale});
    for (size_t i = 0; i < s.size();) {
        const char32_t cp = next_codepoint(s, i);
        if (const int k = m5x7_of(cp); k >= 0) {
            const int cx = kM5x7CX + k % 16, cy = kM5x7CY + k / 16;
            const ImVec2 uv0(float((cx * kCell + 1) * kUp) / kAtlasW,
                             float((cy * kCell + 1) * kUp) / kAtlasH);
            const ImVec2 uv1(float((cx * kCell + 11) * kUp) / kAtlasW,
                             float((cy * kCell + 11) * kUp) / kAtlasH);
            const float s10 = 10 * scale;
            dl->AddImage(ink_texture(), ImVec2(x, y), ImVec2(x + s10, y + s10), uv0, uv1, tint);
            x += kM5x7[k].advance * scale;
            continue;
        }
        const int g = glyph_of(cp);
        if (g >= 512) {
            const LowerMetrics &m = lower_metrics()[g - 512];
            tile(dl, x - m.left * scale, y, kLowerCX + (g - 512) % 16, kLowerCY + (g - 512) / 16, scale, tint,
                 true);
            x += m.advance * scale;
            continue;
        }
        if (g >= 256)
            tile(dl, x, y, kExtraCX + g - 256, kExtraCY, scale, tint, true);
        else if (g >= 0)
            tile(dl, x, y, kGlyphCX + g % 16, g / 16, scale, tint, true);
        x += kGlyph * scale;
    }
}

void Skin::text_sdl(SDL_Renderer *renderer, float x, float y, std::string_view s, float scale, float alpha,
                    bool gb) const {
    SDL_Texture *const atlas = gb ? texture_gb_ : texture_;
    if (gb)
        SDL_SetTextureColorMod(atlas, 0, 0, 0); // its strokes are white
    SDL_SetTextureAlphaModFloat(atlas, alpha);
    auto put = [&](int cx, int cy, int inset, int cells, float px) { // cells x cells font pixels of a cell
        const SDL_FRect src{float((cx * kCell + inset) * kUp), float((cy * kCell + inset) * kUp),
                            float(cells * kUp), float(cells * kUp)};
        const SDL_FRect dst{px, y, cells * scale, cells * scale};
        SDL_RenderTexture(renderer, atlas, &src, &dst);
    };
    for (size_t i = 0; i < s.size();) {
        const char32_t cp = next_codepoint(s, i);
        if (const int k = m5x7_of(cp); k >= 0) {
            put(kM5x7CX + k % 16, kM5x7CY + k / 16, 1, 10, x);
            x += kM5x7[k].advance * scale;
            continue;
        }
        const int g = glyph_of(cp);
        if (g >= 512) {
            const LowerMetrics &m = lower_metrics()[g - 512];
            put(kLowerCX + (g - 512) % 16, kLowerCY + (g - 512) / 16, 1, kGlyph, x - m.left * scale);
            x += m.advance * scale;
            continue;
        }
        if (g >= 256)
            put(kExtraCX + g - 256, kExtraCY, 1, kGlyph, x);
        else if (g >= 0)
            put(kGlyphCX + g % 16, g / 16, 1, kGlyph, x);
        x += kGlyph * scale;
    }
    SDL_SetTextureAlphaModFloat(atlas, 1.0f);
    SDL_SetTextureColorMod(atlas, 255, 255, 255);
}

float Skin::text_width(std::string_view s, float scale) {
    int w = 0;
    for (size_t i = 0; i < s.size();) {
        if (TextToken token; text_token_at(s.substr(i), &token)) {
            const ButtonIcon b = text_token_icon(token);
            w += b.empty() ? int(text_width(text_token_text(token))) : b.w + 2;
            i += token.len;
            continue;
        }
        const char32_t cp = next_codepoint(s, i);
        if (const int k = m5x7_of(cp); k >= 0) {
            w += kM5x7[k].advance;
            continue;
        }
        const int g = glyph_of(cp);
        w += g >= 512 ? lower_metrics()[g - 512].advance : kGlyph;
    }
    return w * scale;
}

void Skin::frame(ImDrawList *dl, float x, float y, int w, int h, Frame style, bool sheer) const {
    if (observe)
        observe({Draw::Kind::Frame, x, y, float(w), float(h), {}});
    if (gb_) {
        // white paper inside a black line one game pixel wide (two for the highlighted frame)
        const float line = style == Frame::Orange ? 4.0f : 2.0f;
        const auto ink = style == Frame::Grey ? kGbGray : kGbInk;
        rect(dl, x, y, float(w), line, ink);
        rect(dl, x, y + h - line, float(w), line, ink);
        rect(dl, x, y + line, line, h - 2 * line, ink);
        rect(dl, x + w - line, y + line, line, h - 2 * line, ink);
        rect(dl, x + line, y + line, w - 2 * line, h - 2 * line,
             (kGbPaper & 0x00FFFFFFu) | ((sheer ? kSheerAlpha : 255) << 24));
        return;
    }
    const int oy = sheer ? kSheerCY : 0;
    const uint8_t *t = style == Frame::Orange ? kFrameOrange
                       : style == Frame::Grey ? kFrameGrey
                                              : kFrameWhite;
    const int cols = w / 8, rows = h / 8;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            const int col = c == 0 ? 0 : c == cols - 1 ? 2 : 1;
            const int row = r == 0 ? 0 : r == rows - 1 ? 2 : 1;
            const uint8_t id = t[row * 3 + col];
            tile(dl, x + c * 8, y + r * 8, (enhanced_ ? kGlyphCX : kOpaqueCX) + id % 16,
                 (enhanced_ ? 0 : oy) + id / 16, 1.0f);
        }
    if (enhanced_) {
        // Match MenuFill's palette gradient on the native fill mask, including rounded corners.
        const auto brightness = [](ImU32 c) {
            return 30 * (c & 255) + 59 * (c >> 8 & 255) + 11 * (c >> 16 & 255);
        };
        ImU32 light = palette_[9], dark = palette_[9];
        for (int i : {10, 11}) {
            if (brightness(palette_[i]) > brightness(light))
                light = palette_[i];
            if (brightness(palette_[i]) < brightness(dark))
                dark = palette_[i];
        }
        const auto gradient = [&](int row) {
            ImU32 color = (sheer ? kSheerAlpha : 255) << 24;
            const float f = float(row) / h;
            for (int shift : {0, 8, 16}) {
                const float top = float(light >> shift & 255);
                const float bottom = float((dark >> shift & 255) * 9 / 10);
                color |= uint32_t(std::lround(top + (bottom - top) * f)) << shift;
            }
            return color;
        };
        for (int yy = 0; yy < h; ++yy) {
            const auto filled = [&](int xx) {
                const int row = yy < 8 ? 0 : yy >= h - 8 ? 2 : 1;
                const int col = xx < 8 ? 0 : xx >= w - 8 ? 2 : 1;
                return (fill_masks_[t[row * 3 + col]] >> ((yy % 8) * 8 + xx % 8)) & 1;
            };
            for (int xx = 0; xx < w;) {
                if (!filled(xx)) {
                    ++xx;
                    continue;
                }
                const int first = xx++;
                while (xx < w && filled(xx))
                    ++xx;
                dl->AddRectFilledMultiColor(ImVec2(x + first, y + yy), ImVec2(x + xx, y + yy + 1),
                                            gradient(yy), gradient(yy), gradient(yy + 1), gradient(yy + 1));
            }
        }
    }
}

void Skin::hand(ImDrawList *dl, float x, float y, float scale) const {
    if (observe)
        observe({Draw::Kind::Hand, x, y, 2 * kGlyph * scale, 2 * kGlyph * scale, {}, 0, scale});
    if (gb_ && !has_gb_hand_)
        return;
    for (int i = 0; i < 4; ++i) {
        const int cx = kGlyphCX + kHand[i] % 16, cy = kHand[i] / 16;
        const ImVec2 uv0(float((cx * kCell + 1) * kUp) / kAtlasW, float((cy * kCell + 1) * kUp) / kAtlasH);
        const ImVec2 uv1(float((cx * kCell + 9) * kUp) / kAtlasW, float((cy * kCell + 9) * kUp) / kAtlasH);
        const ImVec2 at(x + (i & 1) * 8 * scale, y + (i >> 1) * 8 * scale);
        dl->AddImage(ImTextureID(intptr_t(gb_ ? texture_gb_ : texture_)), at,
                     ImVec2(at.x + 8 * scale, at.y + 8 * scale), uv0, uv1);
    }
}

void Skin::glyph_tile(ImDrawList *dl, float x, float y, int t, ImU32 tint, float scale) const {
    if (observe)
        observe({Draw::Kind::Glyph, x, y, kGlyph * scale, kGlyph * scale, {}, tint, scale});
    tint = ink_tint(tint);
    tile(dl, x, y, kGlyphCX + t % 16, t / 16, scale, tint, true);
}

void Skin::rect(ImDrawList *dl, float x, float y, float w, float h, ImU32 col) const {
    const ImVec2 uv0(float((kWhiteCX * kCell + 3) * kUp) / kAtlasW,
                     float((kWhiteCY * kCell + 3) * kUp) / kAtlasH);
    const ImVec2 uv1(float((kWhiteCX * kCell + 7) * kUp) / kAtlasW,
                     float((kWhiteCY * kCell + 7) * kUp) / kAtlasH);
    dl->AddImage(texture_id(), ImVec2(x, y), ImVec2(x + w, y + h), uv0, uv1, col);
}

void Skin::bar(ImDrawList *dl, float x, float y, int segments, int filled, bool focused, float cell,
               float h) const {
    if (observe)
        observe({Draw::Kind::Bar, x, y, segments * cell, h, {}, 0, 1, segments, filled, focused});
    if (gb_) { // black cells, the unlit ones pale; no shadow
        for (int i = 0; i < segments; ++i)
            rect(dl, x + i * cell, y, cell - 2, h - 1, i < filled ? kGbInk : kGbPale);
        return;
    }
    // cells in the font's style: a lit (white, orange when selected) or dark block with a black shadow
    // one unit right and below
    for (int i = 0; i < segments; ++i) {
        const float cx = x + i * cell;
        const bool lit = i < filled;
        rect(dl, cx, y, cell - 2, h - 1, lit ? (focused ? orange() : white()) : dark());
        rect(dl, cx + cell - 2, y + 1, 1, h - 1, black());
        rect(dl, cx + 1, y + h - 1, cell - 2, 1, black());
    }
}

} // namespace sawgod::ui
