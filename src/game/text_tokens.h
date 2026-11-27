// Explicit tokens in the game's text. The original strings lay values out with
// fullwidth spaces: a label such as ＨＰ　　　／　　　　 leaves room for numbers the code draws at fixed
// tiles, trailing spaces erase what the window held, and a button is a letter (Ａ：けってい). A translator
// can't reproduce that in a proportional font, so every such string has a token form -- ＨＰ{hp}／{maxhp},
// {A}：けってい -- shown in the sheet's japanese column and written by translations:
//
// - value tokens ({hp}, {gold}, {item}...): the draw site passes the value (TextValues) and skips its own
//   fixed-tile draw when the language's string has the token;
// - button tokens ({A}, {B}, {X1}..{Y4}, {Start}, and {X}/{Y} for a whole pad): an icon (text_render);
// - layout tokens ({cursor}, {tab}): the text after one starts at the original's column (the selection hand
//   is a sprite at a fixed place, a second heading sits over its column);
// - trailing padding is gone: the renderer clears the rest of the original's cells itself.
//
// Battle-popup templates name their parts: {actor}, {target}, {item}, {amount}... (message_tokens).
// The base language (the ROM's Japanese) is loaded and drawn exactly as before: the token form is derived
// from it, never stored in its place.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sawgod::game {

// Key-item presentation is shared by item rows, dialogue tokens and fit checks.
constexpr bool is_kings_gear(int id) { return id >= 17 && id <= 19; }
// Equipment keeps an eight-column reservation in both graphics styles; key A uses five.
constexpr int inline_item_advance(int id) { return is_kings_gear(id) ? 8 : 5; }
constexpr bool is_marked_key_item(int id) { return (id >= 20 && id <= 26) || id == 30 || id == 31; }
inline constexpr std::string_view kKeyItemArt = "..HGG..."
                                                ".HG#GS.."
                                                ".GGGSS.."
                                                "...G...."
                                                "..LG...."
                                                "...G...."
                                                "..LG....";
inline constexpr char32_t kInlineKeyItem = 0xF0200, kItemNameEnd = 0xF0300;
// A nonempty display view overrides the resource name; otherwise the token uses item_N.
int key_item_token(std::u32string_view text, size_t *length = nullptr,
                   std::u32string_view *display = nullptr);
// Preserve literal opening delimiters through dialogue/value/raw-code decoding.
inline constexpr char32_t kLiteralBrace = 0xF0301, kLiteralBracket = 0xF0302;
constexpr char32_t literal_glyph(char32_t c) {
    return c == kLiteralBrace ? U'{' : c == kLiteralBracket ? U'[' : c;
}
std::u32string marked_item_run(int id, std::u32string_view name);
std::u32string expand_key_items(std::u32string_view text, const std::function<std::u32string(int)> &name);

class Machine;
struct TextMessage;
enum class TextKey : uint16_t;

struct TokenForm {
    std::string text;      // the token form (UTF-8, codes the charset lacks as [XX])
    std::vector<int> tabs; // the original's cell column for each {cursor}/{tab}, in order
    int cells = 0;         // the original's cells, trailing padding included
};
// The token form of a text key's original string (its UTF-8 as the resource files write it). `buttons_only`:
// only the button tokens (Japanese host surfaces, which draw values themselves). Pure padding gives "".
TokenForm text_tokens(std::string_view key, std::string_view original, bool buttons_only = false);
// A key whose original is only padding: it leaves the sheet (its site's draw just clears cells).
bool padding_key(std::string_view original);

// A base-language (Japanese) string of `key` as host surfaces draw it: its button letters as tokens (icons)
// while the table's button icons are on (TextTable::set_button_icons), else as it is.
class TextTable;
std::u32string with_button_tokens(const TextTable &table, TextKey key, std::u32string_view s);

// The named tokens of a battle-popup message's RAM parts, in their order ("actor", "target", "item"...).
std::vector<std::string> message_tokens(const TextMessage &msg);
// The value tokens the draw sites fill (without braces).
const std::vector<std::string> &value_token_names();
bool is_value_token(std::u32string_view name);

// Values for the value tokens drawn while this object lives (the draw site's scope). Nested scopes add to
// the outer ones.
class TextValues {
  public:
    TextValues();
    ~TextValues();
    void set(std::u32string_view name, std::u32string value) { values_.emplace_back(name, std::move(value)); }
    TextValues(const TextValues &) = delete;
    TextValues &operator=(const TextValues &) = delete;
    // The current value of `name`, or nullptr.
    static const std::u32string *find(std::u32string_view name);

  private:
    std::vector<std::pair<std::u32string, std::u32string>> values_;
    const TextValues *outer_;
};

// The loaded language's string for `key` is proportional and has a value token: the site passes its values
// and skips its own fixed-tile draws.
bool has_value_tokens(Machine &m, TextKey key);
// A number as the game shows it: decimal, at most `digits` digits (larger values show as all nines).
std::u32string decimal_text(unsigned value, unsigned digits);
// An item or monster name as the loaded language has it (its own characters, or the record decoded).
enum class NameTable : uint8_t;
std::u32string name_text(Machine &m, NameTable table, uint8_t id);
// The 0xFF-terminated WSC-code string at seg:off as characters, spaces trimmed (a prepared number or name).
std::u32string ram_text(Machine &m, uint16_t segment, uint16_t offset, unsigned max = 16);

} // namespace sawgod::game
