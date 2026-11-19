// Stores player-entered names and resolves them for native and host text.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sawgod::game {
class Machine;
using PartyNameList = std::array<std::string, 4>;
inline constexpr size_t kPlayerNameGlyphs = 8;
// Earlier local saves allowed ten; retain those strings without displaying the extra glyphs.
inline constexpr size_t kStoredPlayerNameGlyphs = 10;
// One glyph of a name being entered: the charset code the game stores, and the glyph itself when it is one
// the game's sheets lack (lowercase, ASCII punctuation; hooks::Value::NameEntryGlyph), which `code` stands
// for in the machine bytes.
struct NameGlyph {
    uint8_t code = 0;
    char32_t text = 0; // 0: the code's own character
    bool operator==(const NameGlyph &) const = default;
};
// Names are host strings; the four machine bytes are only a projection for the unchanged native menus.
class PlayerNames {
  public:
    bool enabled = false;
    PartyNameList values{};
    std::vector<NameGlyph> draft;
    void reset() {
        values = {};
        draft.clear();
    }
    void accept(size_t member);
    std::u32string rendered(size_t member) const;
    // The draft as drawn: kana and other WSC-only characters as the WSC font's glyphs (U+E000 + code).
    std::u32string draft_text() const;
};
std::string name_utf8(std::u32string_view text);
bool valid_player_name(const std::string &text);
void draw_player_name_editor(Machine &m);
} // namespace sawgod::game
