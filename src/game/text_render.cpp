// Draws localized glyphs into the native tile-based text surfaces.
#include "game/text_render.h"

#include "game/player_names.h"

#include <algorithm>
#include <array>
#include <vector>

#include "engine/button_icons.h"
#include "game/bank34.h"
#include "game/machine.h"
#include "game/text.h"
#include "game/text_tokens.h"
#include "game/ui_chrome.h"

namespace sawgod::game {

namespace {

struct Glyph {
    char32_t cp;
    uint8_t advance;
    uint8_t rows[9]; // from the cap top; bit 7 = leftmost pixel
};

constexpr Glyph kGlyphs[] = {
#include "engine/font_m5x7.inc"
    // Opening copyright: hand-drawn at the same seven-pixel cap height as m5x7.
    {0x00A9, 8, {0x38, 0x44, 0xBA, 0xA2, 0xBA, 0x44, 0x38, 0, 0}},
    // Battle action preview: a small right arrow between the actor and the target, in
    // the
    // text's own ink (never the green selection cursor).
    {0x25B8, 5, {0, 0x80, 0xC0, 0xE0, 0xC0, 0x80, 0, 0, 0}},
};

const Glyph &glyph(char32_t c) {
    c = literal_glyph(c);
    for (const Glyph &g : kGlyphs)
        if (g.cp == c)
            return g;
    if (c == U'　' || c == U'\u00A0')
        return glyph(U' ');
    if (c >= U'！' && c <= U'～') // full-width ASCII
        return glyph(char32_t(c - U'！' + U'!'));
    return glyph(U'?');
}

constexpr uint8_t kStroke = 2, kShadow = 1; // font palette indices: white, black (the WSC font's own)
constexpr uint8_t kSpace = 0xD9;            // the font's space
constexpr int kRows = 10;                   // 0-7 the text row, 8-9 hanging into the next

constexpr char32_t kWscGlyph = 0xE000; // U+E000 + code: the WSC font's own glyph (e.g. a kana name)
// m5x7 glyphs sit this many pixels right of the pen (one pixel right reads better
// against the window's left edge); the WSC font's glyphs stay on the tile grid
constexpr int kM5x7Left = 1;

std::array<uint8_t, 64> tile_pixels(Machine &m, uint16_t entry);

// One glyph tile (4bpp packed) redrawn as m5x7 character `c`: the strokes (1, 2) taken out for the space
// tile's pixels, then the character's stroke and shadow.
std::array<uint8_t, 32> m5x7_tile(const std::array<uint8_t, 32> &tile, const std::array<uint8_t, 32> &space,
                                  char32_t c) {
    auto get = [](const std::array<uint8_t, 32> &t, int x, int y) {
        const uint8_t b = t[size_t(y * 4 + x / 2)];
        return uint8_t(x & 1 ? b & 15 : b >> 4);
    };
    std::array<uint8_t, 64> px{};
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            const uint8_t v = get(tile, x, y);
            px[size_t(y * 8 + x)] = v == kStroke || v == kShadow ? get(space, x, y) : v;
        }
    const Glyph &g = glyph(c);
    auto on = [&](int x, int y) {
        x -= kM5x7Left;
        return x >= 0 && x < 8 && y >= 0 && y < 9 && (g.rows[y] >> (7 - x) & 1);
    };
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            if (on(x, y))
                px[size_t(y * 8 + x)] = kStroke;
            else if (on(x - 1, y) || on(x, y - 1))
                px[size_t(y * 8 + x)] = kShadow;
    std::array<uint8_t, 32> out{};
    for (int i = 0; i < 32; ++i)
        out[size_t(i)] = uint8_t(px[size_t(2 * i)] << 4 | px[size_t(2 * i + 1)]);
    return out;
}

// A token at text[i] the renderer draws: a button icon ({A}, {X}...: `icon` set, or its name as text when
// the host gives none) or a layout token ({cursor}, {tab}: `tab`). Its length in characters, or 0.
struct DrawnToken {
    size_t len = 0;
    ButtonIcon icon;
    std::u32string name; // no icon: the button's name, drawn as text
    bool tab = false;
};
DrawnToken drawn_token(std::u32string_view text, size_t i) {
    DrawnToken out;
    if (text[i] != U'{')
        return out;
    if (text.substr(i, 8) == U"{cursor}" || text.substr(i, 5) == U"{tab}") {
        out.len = text[i + 1] == U'c' ? 8 : 5;
        out.tab = true;
        return out;
    }
    TextToken token;
    if (!text_token_at(text.substr(i), &token))
        return out;
    out.len = token.len;
    out.icon = text_token_icon(token);
    if (out.icon.empty()) {
        const std::string name = text_token_text(token);
        out.name.assign(name.begin(), name.end());
    }
    return out;
}

// Renders text into a canvas `cells` * 8 wide, kRows tall (palette indices). `tabs`: the column (in cells)
// each {cursor}/{tab} moves the pen to.
std::vector<uint8_t> render(Machine &m, std::u32string_view text, int cells,
                            const std::vector<int> &tabs = {}) {
    const int w = cells * 8;
    std::vector<uint8_t> px(size_t(w * kRows), 0);
    std::vector<uint8_t> wsc(size_t(w * kRows), 0); // WSC glyphs: their own stroke and shadow, kept as is
    std::vector<uint8_t> bare(size_t(w * kRows),
                              0); // icon pixels that cast no shadow (or are its clear symbol)
    int x = 0;
    size_t tab = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char32_t c = text[i];
        if (c == kItemNameEnd)
            continue;
        if (c >= kInlineKeyItem && c < kInlineKeyItem + 256) {
            const int id = int(c - kInlineKeyItem);
            if (is_kings_gear(id)) {
                const int symbol = Chrome::item_symbol(id, 255);
                const uint16_t entry = m.rd16(0x4000, uint16_t(0xC0E7 + symbol * 2));
                const int tile = (entry & 0x1FF) - 0x80;
                for (int r = 0; r < 8; ++r)
                    for (int b = 0; b < 8; ++b) {
                        const int tx = entry & 0x4000 ? 7 - b : b, ty = entry & 0x8000 ? 7 - r : r;
                        const uint8_t source = m.rd8(0x9000, uint16_t(0x2480 + tile * 32 + ty * 4 + tx / 2));
                        const int value = tx & 1 ? source & 15 : source >> 4;
                        const int column = x + b - 1;
                        if (column >= 0 && column < w && value)
                            px[size_t(r * w + column)] = value == kShadow ? kShadow : kStroke;
                    }
                x += inline_item_advance(id);
                continue;
            }
            constexpr auto art = kKeyItemArt;
            for (int r = 0; r < 7; ++r)
                for (int b = 1; b <= 5; ++b)
                    if (const int column = x + b - 2; art[r * 8 + b] != '.' && column >= 0 && column < w)
                        px[size_t(r * w + column)] = art[r * 8 + b] == '#' ? kShadow : kStroke;
            x += 5;
            continue;
        }
        if (Chrome::is_item_token(c)) {
            x += Chrome::kItemAdvance;
            continue;
        }
        if (const DrawnToken t = drawn_token(text, i); t.len) {
            i += t.len - 1;
            if (t.tab) {
                if (tab < tabs.size())
                    x = std::max(x, tabs[tab] * 8);
                ++tab;
            } else if (!t.icon.empty()) {
                // the icon's 9 rows from the cap top (one lower than host chrome: the cell above isn't ours)
                for (int r = 0; r < t.icon.h; ++r)
                    for (int b = 0; b < t.icon.w; ++b) {
                        const int px_x = x + kM5x7Left + t.icon.dx + b, px_y = t.icon.dy + r;
                        if (px_x < 0 || px_x >= w || px_y < 0 || px_y >= kRows)
                            continue;
                        const uint8_t v = t.icon.at(b, r);
                        if (v == ButtonIcon::Ink || v == ButtonIcon::InkNoShadow)
                            px[size_t(px_y * w + px_x)] = kStroke;
                        if (v == ButtonIcon::InkNoShadow || v == ButtonIcon::Symbol)
                            bare[size_t(px_y * w + px_x)] = 1;
                    }
                x += t.icon.w + 1;
            } else {
                for (char32_t n : t.name) {
                    const Glyph &g = glyph(n);
                    for (int r = 0; r < 9; ++r)
                        for (int b = 0; b < 8; ++b)
                            if ((g.rows[r] >> (7 - b) & 1) && x + kM5x7Left + b < w)
                                px[size_t(r * w + x + kM5x7Left + b)] = kStroke;
                    x += g.advance;
                }
            }
            continue;
        }
        if (c >= kWscGlyph && c < kWscGlyph + 0x100) {
            // its own pixels where they differ from the space glyph's (strokes, shadow, and colored ones
            // such as the heart's)
            const auto tile = tile_pixels(m, uint16_t(0x1F00 | (c - kWscGlyph)));
            const auto space = tile_pixels(m, uint16_t(0x1F00 | kSpace));
            for (int y = 0; y < 8; ++y)
                for (int b = 0; b < 8 && x + b < w; ++b) {
                    const uint8_t v = tile[size_t(y * 8 + b)];
                    if (v == kStroke || v == kShadow || (v && v != space[size_t(y * 8 + b)]))
                        wsc[size_t(y * w + x + b)] = v;
                }
            x += 8;
            continue;
        }
        const Glyph &g = glyph(c);
        for (int r = 0; r < 9; ++r)
            for (int b = 0; b < 8; ++b)
                if ((g.rows[r] >> (7 - b) & 1) && x + kM5x7Left + b < w)
                    px[size_t(r * w + x + kM5x7Left + b)] = kStroke;
        x += g.advance;
    }
    std::vector<uint8_t> out = px;
    for (int r = 0; r < kRows; ++r)
        for (int c = 0; c < w; ++c) {
            if (px[size_t(r * w + c)] != kStroke || bare[size_t(r * w + c)])
                continue;
            if (c + 1 < w && px[size_t(r * w + c + 1)] != kStroke && !bare[size_t(r * w + c + 1)])
                out[size_t(r * w + c + 1)] = kShadow;
            if (r + 1 < kRows && px[size_t((r + 1) * w + c)] != kStroke && !bare[size_t((r + 1) * w + c)])
                out[size_t((r + 1) * w + c)] = kShadow;
        }
    for (size_t i = 0; i < out.size(); ++i)
        if (wsc[i])
            out[i] = wsc[i];
    return out;
}

// The current pixels of the tile an entry shows (8x8 palette indices), for a cell that only receives
// what hangs into it from above.
std::array<uint8_t, 64> tile_pixels(Machine &m, uint16_t entry) {
    std::array<uint8_t, 64> px{};
    const int tile = (entry & 0x1FF) | (entry & 0x2000 ? 0x200 : 0);
    const uint16_t base = uint16_t(0x4000 + tile * 32);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            const int sx = entry & 0x4000 ? 7 - x : x, sy = entry & 0x8000 ? 7 - y : y;
            const uint8_t b = m.rd8(0, uint16_t(base + sy * 4 + sx / 2));
            px[size_t(y * 8 + x)] = uint8_t(sx & 1 ? b & 15 : b >> 4);
        }
    return px;
}

void place(Machine &m, uint16_t map, uint16_t attr, const std::vector<uint8_t> &px, int cells) {
    auto &table = m.wsc().cell_pixels;
    const int w = cells * 8;
    const uint16_t entry = uint16_t(attr | kTextMarker);
    // the font's space glyph: its tiles carry the window's texture, which the text is drawn over
    const std::array<uint8_t, 64> space = tile_pixels(m, uint16_t(attr | kSpace));
    for (int i = 0; i < cells; ++i) {
        const uint16_t a = uint16_t(map + 2 * i);
        // keep what hangs into this cell from the row above if that override is still showing
        auto old = table.find(a);
        std::array<uint8_t, 64> over{};
        if (old != table.end() && old->second.entry == m.rd16(0, a))
            over = old->second.over;
        m.wr16(0, a, entry);
        emu::Wsc::CellPixels &cell = table[a];
        cell.entry = entry;
        cell.over = over;
        cell.beneath = space;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                const uint8_t v = px[size_t(y * w + i * 8 + x)];
                cell.ink[size_t(y * 8 + x)] = v;
                cell.px[size_t(y * 8 + x)] = v ? v : space[size_t(y * 8 + x)];
            }

        // rows 8-9 into the cell below (same map: 32 cells per row, 32 rows per 2 KB map)
        if ((a & 0x7C0) == 0x7C0)
            continue;
        const uint16_t b = uint16_t(a + 0x40);
        std::array<uint8_t, 64> hang{};
        bool any = false;
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 8; ++x)
                if ((hang[size_t(y * 8 + x)] = px[size_t((8 + y) * w + i * 8 + x)]) != 0)
                    any = true;
        const uint16_t below = m.rd16(0, b);
        auto it = table.find(b);
        if (it != table.end() && it->second.entry == below) {
            it->second.over = hang;
        } else if (any && ((below >> 9) & 15) == ((attr >> 9) & 15)) {
            emu::Wsc::CellPixels &c = table[b];
            c.entry = below;
            c.px = tile_pixels(m, below);
            c.over = hang;
        }
    }
}

} // namespace

bool font_has(char32_t c) {
    c = literal_glyph(c);
    if (c == U'　' || c == U'\u00A0' || (c >= U'！' && c <= U'～'))
        return true;
    for (const Glyph &g : kGlyphs)
        if (g.cp == c)
            return true;
    return false;
}

std::u32string with_wsc_glyphs(std::u32string_view s) {
    static const Charset charset = Charset::system();
    std::u32string out;
    for (char32_t c : s) {
        // Typographic punctuation uses existing glyphs; no font metrics are changed.
        if (c == U'—')
            c = U'ー';
        else if (c == U'“' || c == U'”')
            c = U'"';
        else if (c == U'‘' || c == U'’')
            c = U'\'';
        if ((c >= 0x3000 && c < kWscGlyph && !(c >= 0xFF01 && c <= 0xFF5E)) ||
            (c >= 0x80 && c < 0x3000 && !font_has(c))) {
            std::vector<uint8_t> code;
            if (charset.encode(name_utf8(std::u32string(1, c)), code, nullptr) && code.size() == 1)
                c = kWscGlyph + code[0];
        }
        out += c;
    }
    return out;
}

FontGlyph font_glyph(char32_t code) {
    const Glyph &g = glyph(code);
    FontGlyph out{g.advance, {}};
    std::copy(std::begin(g.rows), std::end(g.rows), out.rows.begin());
    return out;
}

const std::u32string *proportional_text(Machine &m) {
    const TextTable *t = m.text_table();
    if (!t)
        return nullptr;
    if (m.ds() == kTextSegment)
        if (const std::u32string *icons = t->icon_text(m.si()))
            return icons;
    if (!t->proportional())
        return nullptr;
    if (m.ds() == 0x9000) // an overlaid data record (an item description)
        return t->record_chars(m.si());
    return m.ds() == kTextSegment ? t->characters(m.si()) : nullptr;
}

void skip_text(Machine &m) {
    while (m.rd8(m.ds(), m.si()) != 0xFF)
        m.si() = uint16_t(m.si() + 1);
}

std::u32string utf8_to_u32(std::string_view s) {
    std::u32string out;
    for (size_t i = 0; i < s.size();) {
        const uint8_t c = uint8_t(s[i]);
        const int n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        char32_t cp = n == 1 ? c : c & (0x7F >> n);
        for (int k = 1; k < n && i + size_t(k) < s.size(); ++k)
            cp = (cp << 6) | (uint8_t(s[i + size_t(k)]) & 0x3F);
        out.push_back(cp);
        i += size_t(n);
    }
    return out;
}

int text_width(std::u32string_view text) {
    int w = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (const DrawnToken t = drawn_token(text, i); t.len) {
            i += t.len - 1;
            if (!t.icon.empty())
                w += t.icon.w + 1;
            for (char32_t n : t.name)
                w += glyph(n).advance;
            continue;
        }
        const char32_t c = text[i];
        if (c == kItemNameEnd)
            continue;
        if (c >= kInlineKeyItem && c < kInlineKeyItem + 256) {
            w += inline_item_advance(int(c - kInlineKeyItem));
            continue;
        }
        if (Chrome::is_item_token(c)) {
            w += Chrome::kItemAdvance;
            continue;
        }
        w += c >= kWscGlyph && c < kWscGlyph + 0x100 ? 8 : glyph(c).advance;
    }
    return text.empty() ? 0 : w + kM5x7Left + 1; // (+ the shadow column)
}

namespace {
// The pen after the text with its tab stops (text_width ignores them: it can't know the columns).
int laid_out_width(std::u32string_view text, const std::vector<int> &tabs) {
    if (tabs.empty())
        return text_width(text);
    int x = 0;
    size_t tab = 0, begin = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        const DrawnToken t = i < text.size() ? drawn_token(text, i) : DrawnToken{};
        if (i < text.size() && !t.tab) {
            if (t.len)
                i += t.len - 1;
            continue;
        }
        if (i > begin)
            x += text_width(text.substr(begin, i - begin)) - kM5x7Left - 1;
        if (i < text.size()) {
            if (tab < tabs.size())
                x = std::max(x, tabs[tab] * 8);
            ++tab;
            i += t.len - 1;
            begin = i + 1;
        }
    }
    return x + kM5x7Left + 1;
}
} // namespace

std::u32string with_values(std::u32string_view text) {
    std::u32string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == U'{') {
            const size_t close = text.find(U'}', i);
            if (close != std::u32string_view::npos) {
                const std::u32string_view name = text.substr(i + 1, close - i - 1);
                if (is_value_token(name)) {
                    if (const std::u32string *v = TextValues::find(name))
                        out += with_wsc_glyphs(*v); // a Japanese name or condition in the WSC font
                    i = close;
                    continue;
                }
            }
        }
        out += text[i];
    }
    return out;
}

int draw_text_pixels(Machine &m, uint16_t map, uint16_t attr, std::u32string_view text, int max_cells,
                     const TextLayout *layout) {
    const std::vector<int> no_tabs;
    const std::vector<int> &tabs = layout ? layout->tabs : no_tabs;
    int cells = (laid_out_width(text, tabs) + 7) / 8;
    if (text.empty())
        cells = 0;
    if (layout)
        cells = std::max(cells, layout->cells); // the rest of the original's cells: cleared
    if (max_cells >= 0 && cells > max_cells)
        cells = max_cells;
    if (cells <= 0)
        return 0;
    place(m, map, attr, render(m, text, cells, tabs), cells);
    return cells;
}

int draw_table_text(Machine &m, uint16_t map, uint16_t attr, const std::u32string &text, int clear_limit) {
    const TextTable *t = m.text_table();
    const TextLayout *found = t && m.ds() == kTextSegment ? t->layout(m.si()) : nullptr;
    TextLayout layout = found ? *found : TextLayout{};
    if (clear_limit >= 0)
        layout.cells = std::min(layout.cells, clear_limit);
    // the WSC font's own glyphs for what m5x7 lacks: [XX] codes (the heart, [D1]) and characters like ×
    std::u32string shown = with_values(text), glyphs;
    for (size_t i = 0; i < shown.size(); ++i) {
        if (shown[i] == U'[' && i + 3 < shown.size() && shown[i + 3] == U']') {
            const auto hex = [](char32_t c) {
                return c >= U'0' && c <= U'9'   ? int(c - U'0')
                       : c >= U'A' && c <= U'F' ? int(c - U'A') + 10
                                                : -1;
            };
            if (const int hi = hex(shown[i + 1]), lo = hex(shown[i + 2]); hi >= 0 && lo >= 0) {
                glyphs += char32_t(kWscGlyph + (hi << 4 | lo));
                i += 3;
                continue;
            }
        }
        glyphs += shown[i];
    }
    return draw_text_pixels(m, map, attr, with_wsc_glyphs(glyphs), -1, found ? &layout : nullptr);
}

bool draw_popup_line(Machine &m, uint16_t es, uint16_t si, uint8_t row, uint16_t col, uint16_t attr,
                     int count) {
    if (m.original_popup_text())
        return false; // the host popup owns translated pixels; machine VRAM keeps the original writes
    // Proportional languages and full player names use host text words. Other native words keep
    // their original tile-table rendering when no proportional language is loaded.
    const TextTable *loaded = m.text_table();
    if (!loaded)
        return false;
    std::vector<uint16_t> words;
    bool text = false;
    for (int k = 0; k < 64; ++k) {
        const uint16_t w = m.rd16(es, uint16_t(si + 2 * k));
        if (w == 0xFFFF || (count >= 0 && k >= count))
            break;
        text |= loaded->proportional() ? (w & kTextWord) != 0
                                       : w == (kTextWord | TextTable::kMessageRun) ||
                                             (w >= (kTextWord | TextTable::kPlayerNameRun) &&
                                              w < (kTextWord | TextTable::kPlayerNameRun) + 4 &&
                                              loaded->run(uint16_t(w & 0x7FFF)));
        words.push_back(w);
    }
    if (!text)
        return false;
    uint16_t map = screen_map_offset(row, col);
    for (size_t k = 0; k < words.size();) {
        if (words[k] & kTextWord) {
            std::u32string run;
            const TextTable *table = m.text_table();
            for (; k < words.size() && (words[k] & kTextWord); ++k)
                if (const std::u32string *r = table ? table->run(uint16_t(words[k] & 0x7FFF)) : nullptr)
                    run += *r;
            map = uint16_t(map + 2 * draw_text_pixels(m, map, attr, run));
        } else {
            m.wr16(0, map, uint16_t(m.rd16(m.ds(), uint16_t(0x2080 + words[k])) + attr));
            map = uint16_t(map + 2);
            ++k;
        }
    }
    return true;
}

std::unordered_map<int, std::vector<emu::Wsc::GlyphOverride>>
m5x7_number_overrides(const emu::Wsc &w,
                      const std::function<const std::array<uint8_t, 32> *(int code)> &gb_glyph) {
    constexpr uint32_t kFontRom = 0x390080; // the WSC font (32 bytes a code), loaded to tile 0x100 + code
    constexpr int kFontTile = 0x100;
    std::unordered_map<int, std::vector<emu::Wsc::GlyphOverride>> out;
    if (w.rom.size() < kFontRom + 0x100 * 32)
        return out;
    auto rom_glyph = [&](int code) {
        std::array<uint8_t, 32> t{};
        std::copy_n(w.rom.begin() + long(kFontRom + uint32_t(code) * 32), 32, t.begin());
        return t;
    };
    std::vector<std::pair<int, char32_t>> chars = {{0xD2, U'/'}, {0xD4, U':'}};
    for (int d = 0; d < 10; ++d)
        chars.emplace_back(0xBB + d, char32_t(U'0' + d));
    const auto space = rom_glyph(kSpace);
    const std::array<uint8_t, 32> *gb_space = gb_glyph ? gb_glyph(kSpace) : nullptr;
    for (auto [code, c] : chars) {
        auto &list = out[kFontTile + code];
        const auto wsc = rom_glyph(code);
        list.push_back({wsc, m5x7_tile(wsc, space, c)});
        if (const std::array<uint8_t, 32> *gb = gb_glyph ? gb_glyph(code) : nullptr)
            list.push_back({*gb, m5x7_tile(*gb, gb_space ? *gb_space : *gb, c)});
    }
    return out;
}

} // namespace sawgod::game
