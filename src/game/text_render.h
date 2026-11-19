// Variable-width text for loaded languages: m5x7 (Daniel Linssen,
// CC0; src/engine/font_m5x7.inc), unmodified, at the game's native pixels on its 8x8 tile grid -- cap tops
// and baselines on the WSC glyph rows, white with the WSC's shadow (1 px right and below). A string is laid
// out in pixels and shown through emu::Wsc::cell_pixels: its cells get the marker entry attr | kTextMarker (a
// blank font tile no game text uses) and host pixels; descenders and their shadow hang into the cells of
// the next row (their `over` layer). The game overwriting a cell ends that by itself.
//
// Proportional TextTable strings and extended player names take this path: menu text through the
// draw_text family (DS = kTextSegment), battle-popup words >= kTextWord in the icon queue. With those
// host features disabled, the original Japanese keeps its native rendering for equivalence checks.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "emu/wsc.h"

namespace sawgod::game {

class Machine;

inline constexpr uint8_t kTextMarker = 0xE0;  // blank font code (tile 0x1E0) the game never writes
inline constexpr uint16_t kTextWord = 0x8000; // battle-popup word: kTextWord | run (TextTable::run)

// Pixel width of `text` in m5x7 (advances; the last glyph's shadow adds one more pixel).
int text_width(std::u32string_view text);
struct FontGlyph {
    uint8_t advance;
    std::array<uint8_t, 9> rows;
};
// The same font used by tile text, for host presentation layers.
FontGlyph font_glyph(char32_t code);
// m5x7 has the character (directly, or a full-width form's ASCII); font_glyph draws others as '?'.
bool font_has(char32_t code);
// Kana and other characters m5x7 lacks (the WSC font's ○, ×...) as the WSC font's own glyphs (U+E000 + code),
// as the text drawing draws them.
std::u32string with_wsc_glyphs(std::u32string_view text);

// Draws `text` from the screen-map cell at internal-RAM address `map` with entry attribute `attr` (the bits
// the game ORs with a character code, e.g. 0x1F00). At most `max_cells` cells (-1: as many as it needs);
// returns the cells used. Button tokens ({A}, {X1}, {X}...: engine/button_icons.h) draw as icons, white with
// the font's shadow, their symbol clear. With a `layout` (text.h), {cursor}/{tab} move the pen to the
// original's columns and the rest of the original's cells is cleared.
struct TextLayout;
int draw_text_pixels(Machine &m, uint16_t map, uint16_t attr, std::u32string_view text, int max_cells = -1,
                     const TextLayout *layout = nullptr);
// The draw_text family's string from the text table at DS:SI (proportional_text): value tokens filled from
// the site's TextValues (text_tokens.h; a missing one draws nothing), laid out over the original's cells
// and clearing the rest of them (at most `clear_limit`: a counted draw's count).
int draw_table_text(Machine &m, uint16_t map, uint16_t attr, const std::u32string &text,
                    int clear_limit = -1);
// `text` with its value tokens replaced by the current TextValues (missing: nothing).
std::u32string with_values(std::u32string_view text);

// The battle popup's line (draw_status_icons / status_popup_wait): if the 0xFFFF-terminated word queue at
// ES:SI holds text words (>= kTextWord), draws its first `count` items (all if -1) from row `row`, column
// `col`: other words as the game does (tile word at DS:0x2080 + word, plus `attr`), runs of text words in
// m5x7. Returns false (drawing nothing) for a queue without text words.
bool draw_popup_line(Machine &m, uint16_t es, uint16_t si, uint8_t row, uint16_t col, uint16_t attr,
                     int count = -1);

// The characters of the string at DS:SI if it's a proportional language's (text.h), or a base string with
// button icons while those are on (TextTable::set_button_icons), else nullptr: the draw_text family then
// draws it with draw_table_text instead of one font tile per byte.
const std::u32string *proportional_text(Machine &m);
// SI moved to the 0xFF terminator of the string at DS:SI, as the draw_text loops leave it.
void skip_text(Machine &m);

std::u32string utf8_to_u32(std::string_view utf8);

// Numbers in m5x7 (HP, counts...): display overrides
// (emu::Wsc::set_glyph_overrides) for the font tiles of the digits, '/' and ':' -- while a tile holds the WSC
// glyph (or, with `gb_glyph`, the GB picture's glyph for that code) it shows the m5x7 character on that
// glyph's own background. The game's memory is untouched.
std::unordered_map<int, std::vector<emu::Wsc::GlyphOverride>>
m5x7_number_overrides(const emu::Wsc &w,
                      const std::function<const std::array<uint8_t, 32> *(int code)> &gb_glyph);

} // namespace sawgod::game
