// Button icons (ours, assets/ui/button_icons.png + .csv): 9-pixel pictures of pad
// buttons, keys and the WSC's own buttons, drawn inline with text in the options menu and the game's hints.
// Pixels are ink (the style's text color; the WSC style adds the font's 1-pixel shadow down-right), ink
// without a shadow (the stick arcs), symbol (clear pixels the ink encloses: a disc's letter or shape) or
// clear.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sawgod {

struct ButtonIcon {
    enum : uint8_t { Clear = 0, Ink = 1, InkNoShadow = 2, Symbol = 3 };
    int w = 0, h = 0;
    int dx = 0, dy = 0;      // where the picture sits relative to the 9-pixel icon box on the text line
    std::vector<uint8_t> px; // w * h of Clear / Ink / InkNoShadow / Symbol
    // The Icon Type's button colors (0xRRGGBB), in the WSC and Enhanced styles;
    // GB ignores them. Unset: the ink in the text's color, the symbol clear, the style's own shadow.
    // `motion`: the ink without a shadow (a stick's arc, its direction).
    std::optional<uint32_t> body, symbol, shadow, motion;
    bool empty() const { return px.empty(); }
    uint8_t at(int x, int y) const { return px[size_t(y) * size_t(w) + size_t(x)]; }
};

inline constexpr int kButtonIconHeight = 9;

// An icon of the set by its index name ("pad A", "d-pad up", "WSC X1", "key Enter"), or nullptr.
const ButtonIcon *button_icon(std::string_view name);
// A keycap with `label` (upper-cased) cut out in the set's 3x5 lettering, as wide as the label; empty when a
// character has no glyph.
ButtonIcon keycap_icon(std::string_view label);

// Text tokens. {X1}..{Y4}, {A}, {B}, {Start} (in the WscButton order, engine/input.h): the button the player
// presses for that WSC button; translations write the token where the button goes ("{B} Back").
// {X} and {Y}: a whole pad (the game's lone Ｘ / Ｙ: "move with X"), tokens kButtonTokenX / kButtonTokenY.
// {icon:NAME}: one icon by its index name ("{icon:pad X}"), or a keycap with {icon:key:LABEL}.
inline constexpr int kButtonTokenCount = 11;
inline constexpr int kButtonTokenX = 11, kButtonTokenY = 12;
struct TextToken {
    int button = -1;  // a WSC button token, or -1 for {icon:...}
    std::string icon; // {icon:NAME}'s NAME
    size_t len = 0;   // characters the token takes
};
// The token that `s` starts with (at a '{'), or false.
template <class Char> bool text_token_at(std::basic_string_view<Char> s, TextToken *token);
// The WSC name of button token `i` ("X1", "A", "Start"; "X", "Y" for the pads).
const char *button_token_name(int i);
// What a token draws: its icon, or empty for text (then text_token_text).
ButtonIcon text_token_icon(const TextToken &token);
std::string text_token_text(const TextToken &token);
// An icon by index name, or a keycap for "key:LABEL"; empty if there is none. A pad icon may carry its Icon
// Type as a prefix, "n:", "x:" or "p:" ("x:pad A"), for that family's button colors.
ButtonIcon named_icon(std::string_view name);

// A whole pad's icon: X as a d-pad, Y as the right stick (the default bindings' layout), or on the keyboard
// the arrow keys' cross and a WASD keycap.
ButtonIcon pad_group_icon(bool y, bool keyboard);

// The host says what each button token shows (the bound key or pad button, per the player's last device and
// Icon Type); unset or empty: the token's name as text.
void set_button_token_icons(std::function<ButtonIcon(int token)> source);
ButtonIcon button_token_icon(int token);

template <class Char> bool text_token_at(std::basic_string_view<Char> s, TextToken *token) {
    if (s.empty() || s[0] != Char('{'))
        return false;
    const size_t close = s.find(Char('}'));
    if (close == std::basic_string_view<Char>::npos || close > 40)
        return false;
    std::string inner;
    for (size_t k = 1; k < close; ++k) {
        if (s[k] < 0x20 || s[k] > 0x7E)
            return false;
        inner += char(s[k]);
    }
    *token = {};
    token->len = close + 1;
    if (inner.rfind("icon:", 0) == 0) {
        token->icon = inner.substr(5);
        return !token->icon.empty();
    }
    for (int i = 0; i <= kButtonTokenY; ++i)
        if (inner == button_token_name(i)) {
            token->button = i;
            return true;
        }
    return false;
}

} // namespace sawgod
