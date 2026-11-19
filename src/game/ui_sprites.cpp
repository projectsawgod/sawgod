// Decodes and draws character-type sprites for host menu pictures.
#include "game/ui_sprites.h"
#include "game/text_tokens.h"

#include <algorithm>

#include "game/gfx_swap.h"
#include "game/session.h"
#include "game/tables.h"
#include "game/text_render.h"
#include "game/ui_chrome.h"

namespace sawgod::game {
namespace {
constexpr uint16_t kTypeIconCells = 0xB2F9; // 4000:, the preview icon's 2x2 screen-map words
}

TypeSprite type_sprite(Machine &m, uint8_t type) {
    TypeSprite s;
    const uint16_t gfx = table_at16(m, Table::monster_type_graphics, 0x9000, uint16_t(0x76E8 + type * 2));
    const uint16_t bp = uint16_t((gfx & 0xFF) * 4);
    const uint16_t segment = m.rd16(0x9000, uint16_t(0x3F70 + bp));
    const uint16_t tiles = m.rd16(0x9000, uint16_t(0x40B0 + bp));
    const uint16_t palette = uint16_t(m.rd16(0x9000, uint16_t(0x40B2 + bp)) + (gfx >> 8) * 32);
    for (int i = 0; i < 128; ++i)
        s.tiles[size_t(i)] = m.rd8(segment, uint16_t(tiles + i));
    for (int i = 0; i < 16; ++i)
        s.colors[size_t(i)] = m.rd16(segment, uint16_t(palette + i * 2)) & 0x0FFF;
    for (int i = 0; i < 4; ++i)
        s.cells[size_t(i)] = m.rd16(0x4000, uint16_t(kTypeIconCells + i * 2));
    return s;
}

void draw_type_sprite(Chrome &c, const TypeSprite &s, int x, int y, bool second, int fade) {
    std::array<uint16_t, 16> colors{};
    for (size_t i = 0; i < 16; ++i)
        for (int shift = 0; shift < 12; shift += 4)
            colors[i] = uint16_t(colors[i] | std::max(0, (s.colors[i] >> shift & 15) - fade) << shift);
    x += second ? kGbMenuSpriteDx : 0;
    for (int i = 0; i < 4; ++i) {
        const uint16_t cell = s.cells[size_t(i)];
        c.tile(s.tiles.data() + (((cell & 0x1FF) - 0xD4) & 3) * 32, colors, 8, x + i % 2 * 8, y + i / 2 * 8,
               cell & 0x4000, cell & 0x8000);
    }
}

MonsterPicture monster_picture(Machine &m, uint8_t type, bool second) {
    MonsterPicture p;
    if (second) {
        int width = 0, height = 0;
        if (!GfxSwap::monster_picture_tiles(type, p.tiles, width, height))
            return p;
        p.width = width * 8;
        p.height = height * 8;
        for (int q = 0; q < width * height; ++q)
            p.cells.push_back(uint16_t(q));
        p.colors = {0, 0xFFF, 0xAAA, 0x555};
        p.palette = 15;
        return p;
    }
    const uint16_t descriptor =
        table_at16(m, Table::monster_type_descriptor, 0x9000, uint16_t(0x7878 + type * 2));
    const int pic = descriptor & 255, source = pic * 6;
    const uint16_t data = m.rd16(0xE000, uint16_t(source));
    const uint16_t palette = uint16_t(m.rd16(0xE000, uint16_t(source + 2)) + (descriptor >> 8) * 32);
    const uint16_t map = m.rd16(0xE000, uint16_t(source + 4));
    const uint16_t size = m.rd16(0xE000, uint16_t(0x120 + pic * 2));
    p.width = (size & 255) * 8;
    p.height = (size >> 8) * 8;
    for (int i = 0; i < 16; ++i)
        p.colors[size_t(i)] = m.rd16(0xE000, uint16_t(palette + i * 2));
    for (int q = 0; q < p.width * p.height / 64; ++q) {
        const uint16_t cell = m.rd16(0xE000, uint16_t(map + q * 2));
        p.cells.push_back(uint16_t(q | (cell & 0xC000)));
        for (int i = 0; i < 32; ++i)
            p.tiles.push_back(m.rd8(0xE000, uint16_t(data + (cell & 0x1FF) * 32 + i)));
    }
    return p;
}

void draw_monster_picture(Chrome &c, const MonsterPicture &p, int x, int y) {
    const int columns = p.width / 8;
    for (size_t q = 0; q < p.cells.size(); ++q)
        c.tile(p.tiles.data() + (p.cells[q] & 0x1FF) * 32, p.colors, p.palette, x + int(q) % columns * 8,
               y + int(q) / columns * 8, p.cells[q] & 0x4000, p.cells[q] & 0x8000);
}

std::u32string decoded_text(Machine &m, uint16_t segment, uint16_t offset, int length) {
    static const auto charset = Charset::system();
    std::u32string s;
    for (int i = 0; i < length; ++i) {
        const auto code = m.rd8(segment, uint16_t(offset + i));
        if (code == 255)
            break;
        s += utf8_to_u32(charset.decode(code));
    }
    while (!s.empty() && (s.back() == U' ' || s.back() == U'　'))
        s.pop_back();
    return s;
}

std::u32string native_text(Machine &m, TextKey key) {
    const TextPtr p = text_at(m, key);
    if (const auto *t = m.text_table(); t && t->proportional() && p.segment == kTextSegment)
        if (const auto *s = t->characters(p.offset))
            return *s;
    return with_button_tokens(Session::of(m.wsc()).text(), key, decoded_text(m, p.segment, p.offset, 64));
}

std::u32string type_name(Machine &m, uint8_t type) {
    const auto &t = Session::of(m.wsc()).text();
    const int run = t.name_run(int(NameTable::Monster), type);
    if (run >= 0)
        if (const auto *s = t.run(uint16_t(run)))
            return *s;
    const TextPtr p = name_record(m, NameTable::Monster, type);
    return decoded_text(m, p.segment, p.offset, 8);
}
} // namespace sawgod::game
