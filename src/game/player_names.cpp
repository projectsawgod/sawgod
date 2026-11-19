// Stores player-entered names and resolves them for native and host text.
#include "game/player_names.h"
#include "game/bank34.h"
#include "game/session.h"
#include "game/text_render.h"
#include <algorithm>

namespace sawgod::game {
std::string name_utf8(std::u32string_view text) {
    std::string out;
    for (char32_t c : text) {
        if (c < 128)
            out += char(c);
        else if (c < 0x800) {
            out += char(0xC0 | c >> 6);
            out += char(0x80 | (c & 63));
        } else if (c < 0x10000) {
            out += char(0xE0 | c >> 12);
            out += char(0x80 | (c >> 6 & 63));
            out += char(0x80 | (c & 63));
        } else {
            out += char(0xF0 | c >> 18);
            out += char(0x80 | (c >> 12 & 63));
            out += char(0x80 | (c >> 6 & 63));
            out += char(0x80 | (c & 63));
        }
    }
    return out;
}
bool valid_player_name(const std::string &text) {
    const auto u = utf8_to_u32(text);
    if (u.size() > kStoredPlayerNameGlyphs || name_utf8(u) != text)
        return false;
    for (char32_t c : u)
        if (c < 32 || (c >= 0x7F && c < 0xA0) || (c >= 0xD800 && c <= 0xDFFF) || c > 0x10FFFF)
            return false;
    return true;
}
void PlayerNames::accept(size_t member) {
    if (member >= values.size())
        return;
    static const Charset charset = Charset::system();
    std::u32string text;
    for (const NameGlyph &g : draft) {
        if (text.size() >= kPlayerNameGlyphs)
            break;
        if (g.text) {
            text += g.text;
            continue;
        }
        auto glyph = utf8_to_u32(charset.decode(g.code));
        if (glyph.size() == 4 && glyph.front() == U'[' && glyph.back() == U']')
            glyph = std::u32string(1, 0xE000 + g.code);
        for (auto cp : glyph)
            text += cp >= U'！' && cp <= U'～' ? cp - U'！' + U'!' : cp;
    }
    values[member] = name_utf8(text);
}
std::u32string PlayerNames::rendered(size_t member) const {
    if (member >= values.size())
        return {};
    auto text = utf8_to_u32(values[member]);
    if (text.size() > kPlayerNameGlyphs)
        text.resize(kPlayerNameGlyphs);
    static const Charset charset = Charset::system();
    for (auto &c : text) {
        if (c >= 0x3000 && !(c >= U'！' && c <= U'～')) {
            std::vector<uint8_t> code;
            if (charset.encode(name_utf8(std::u32string(1, c)), code, nullptr) && code.size() == 1)
                c = 0xE000 + code[0];
        }
    }
    return text;
}
std::u32string PlayerNames::draft_text() const {
    static const Charset charset = Charset::system();
    std::u32string text;
    for (const NameGlyph &g : draft) {
        if (g.text) {
            text += g.text;
            continue;
        }
        const auto u = utf8_to_u32(charset.decode(g.code));
        if (u.size() == 4 && u.front() == U'[' && u.back() == U']') {
            text += 0xE000 + g.code;
            continue;
        }
        for (auto cp : u)
            text += cp < 0x3000 || (cp >= U'！' && cp <= U'～') ? cp : 0xE000 + g.code;
    }
    return text;
}
void draw_player_name_editor(Machine &m) {
    auto &names = Session::of(m.wsc()).player_names();
    m.ax() = 0;
    m.bx() = 0x10D;
    m.cx() = 0x10F;
    m.dx() = 0x1FF;
    fill_screen_rect_core(m);
    m.ax() = 1;
    m.bx() = 0x10D;
    m.cx() = 0x10F;
    m.dx() = 0x1FF;
    fill_screen_rect_core(m);
    draw_text_pixels(m, screen_map_offset(0, 0x10D), 0x1F00, host_text(m.wsc(), "ui_name_entry_label"), 4);
    const std::u32string text = names.draft_text();
    // Fixed eight-pixel pen positions match the grid and retain eight kana at native size.
    for (size_t i = 0; i < text.size(); ++i)
        draw_text_pixels(m, screen_map_offset(0, uint16_t(0x112 + i)), 0x1F00, std::u32string(1, text[i]), 1);
    m.ds() = m.ss();
    m.si() = 0xD85A;
    m.ax() = 1;
    m.bx() = uint16_t(0x112 + std::min<size_t>(names.draft.size(), kPlayerNameGlyphs - 1));
    m.cx() = 1;
    draw_text_n_white_core(m);
}
} // namespace sawgod::game
