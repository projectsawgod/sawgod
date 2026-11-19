// Bestiary snapshots, paging, and host-drawn monster details.
#include "game/bestiary_ui.h"

#include <algorithm>

#include "game/gfx_swap.h"
#include "game/player_names.h"
#include "game/session.h"
#include "game/tables.h"
#include "game/text_render.h"
#include "game/ui_chrome.h"
#include "game/ui_sprites.h"

namespace sawgod::game {
namespace {
using C = Chrome;
using S = std::u32string;
// Native state (bank34_misc.cpp: run_monster_history_menu_core, draw_history_page_core).
constexpr uint16_t kMember = 0xD846, kCursor = 0xD847, kScroll = 0xD853;
constexpr uint16_t kProgressPointers = 0xB92D; // 4000:, per member: its history (generation, ring start,
                                               // 100 type bytes, 0xFF empty)
constexpr int kEntries = 100;
S number(int n) { return utf8_to_u32(std::to_string(n)); }
int word(const emu::Wsc &w, int at) {
    return w.ram[size_t(at & 0xFFFF)] | w.ram[size_t((at + 1) & 0xFFFF)] << 8;
}
} // namespace

void BestiaryUi::install(emu::Wsc &w) {
    subscriptions_.clear();
    scene_ = Scene::Hidden;
    first_ = 0;
    detail_.reset();
    generation_ = Session::of(w).generation();
    Session::of(w).player_names().enabled = true; // eight-glyph host names, as the other screens show them
    subscriptions_.push_back(
        Session::of(w).hooks().subscribe(hooks::Event::Bestiary, [this, &w](Machine &, const auto &a) {
            frame(w);
            const auto scene = Scene(std::clamp(a.a, 0, 6));
            if (scene != Scene::History)
                first_ = 0;
            if (scene != scene_)
                detail_.reset();
            scene_ = scene;
        }));
    // Member history is for the party's monsters: without one the notice shows. The
    // original also opens it for a party with empty slots, whose trait byte FFh has the monster bit.
    subscriptions_.push_back(
        Session::of(w).hooks().subscribe(hooks::VB::BestiaryHistory, [&w](Machine &, bool, const auto &) {
            for (int i = 0; i < 4; ++i)
                if (const uint8_t trait = w.ram[uint16_t(0xD00A + i * 31)]; trait != 0xFF && (trait & 0x40))
                    return true;
            return false;
        }));
    subscriptions_.push_back(Session::of(w).hooks().subscribe(
        hooks::Value::BestiaryKeys,
        [this, &w](Machine &, int32_t v, const auto &) { return int32_t(keys(w, uint16_t(v))); }));
}

uint16_t BestiaryUi::keys(emu::Wsc &w, uint16_t original) {
    frame(w);
    // The native loop acts on B, then Up/Down (Up first); only a lone Down at the end is dropped.
    if (scene_ != Scene::History || original & (8 | 0x10) || !(original & 0x40))
        return original;
    const auto snapshot = view(w);
    return snapshot.selected + 1 >= snapshot.total ? uint16_t(original & ~0x40) : original;
}

void BestiaryUi::frame(emu::Wsc &w) {
    if (generation_ != Session::of(w).generation()) {
        generation_ = Session::of(w).generation();
        scene_ = Scene::Hidden;
        first_ = 0;
        detail_.reset();
    }
}

BestiaryUi::View BestiaryUi::view(emu::Wsc &w) {
    frame(w);
    Machine m(w);
    View snapshot;
    snapshot.member = std::clamp(int(w.ram[kMember]), 0, 3);
    const int record = 0xD000 + snapshot.member * 31;
    const uint8_t type = w.ram[size_t(record + 5)];
    snapshot.name = Session::of(w).player_names().rendered(size_t(snapshot.member));
    if (snapshot.name.empty())
        snapshot.name = decoded_text(m, 0, uint16_t(record + 1), 4);
    snapshot.type = type_name(m, type);
    snapshot.sprite = type_sprite(m, type);
    const int history = m.rd16(0x4000, uint16_t(kProgressPointers + snapshot.member * 2));
    snapshot.generation = word(w, history);
    int at = word(w, history + 2); // the ring index before the newest entry (history_advance_core)
    const auto advance = [](int i) { return i + 1 > 99 ? 0 : i + 1; };
    std::array<uint8_t, kEntries> entries{};
    for (int k = 0; k < kEntries; ++k) {
        at = advance(at);
        entries[size_t(k)] = w.ram[size_t((history + 4 + at) & 0xFFFF)];
        snapshot.total += entries[size_t(k)] != 255;
    }
    // The native list: fourteen rows from the scroll offset, its cursor on one of them.
    if (scene_ == Scene::History) {
        snapshot.selected = word(w, kScroll) + w.ram[kCursor];
        if (snapshot.selected < first_)
            first_ = snapshot.selected;
        if (snapshot.selected >= first_ + kRows)
            first_ = snapshot.selected - kRows + 1;
    }
    snapshot.first = first_ = std::clamp(first_, 0, kEntries - kRows);
    for (int i = 0; i < kRows; ++i) {
        const int k = snapshot.first + i;
        if (entries[size_t(k)] == 255)
            continue;
        snapshot.rows[size_t(i)].name = type_name(m, entries[size_t(k)]);
        snapshot.rows[size_t(i)].generation =
            std::min(9999, (snapshot.generation - k) & 0xFFFF); // decimal_to_bcd_clamped
    }
    return snapshot;
}

BestiaryUi::CatalogView BestiaryUi::catalog_view(emu::Wsc &w) {
    frame(w);
    Machine m(w);
    CatalogView snapshot;
    snapshot.page = std::clamp(int(w.ram[0xD852]), 0, 4);
    snapshot.row = std::clamp(int(w.ram[0xD844]), 0, 4);
    snapshot.column = std::clamp(int(w.ram[0xD843]), 0, 5);
    snapshot.category = std::clamp(int(w.ram[0xD842]), 0, 1);
    snapshot.selected = std::clamp(int(w.ram[0xD845]), 0, 7);
    snapshot.type = snapshot.page * 30 + snapshot.row * 6 + snapshot.column;
    snapshot.seen = (w.ram[size_t(0xD0EC + snapshot.type / 8)] & (0x80 >> (snapshot.type % 8))) != 0;
    snapshot.name = type_name(m, uint8_t(snapshot.type));
    snapshot.family = host_text(w, "ui_bestiary_family_" + std::to_string(snapshot.type / 6));
    // The native selectors choose this same template; read the mod-aware data without running a routine.
    const auto record = rom_record(Table::monsters, unsigned(snapshot.type), w);
    if (record.size() >= 11) {
        snapshot.stats[0] = std::min(9999, int(record[3] | record[4] << 8));
        for (int i = 1; i < 5; ++i)
            snapshot.stats[size_t(i)] = std::min(99, int(record[size_t(4 + i)]));
        snapshot.ability_count = std::min({8, int(record[2]), int(record.size()) - 11});
        for (int i = 0; i < snapshot.ability_count; ++i) {
            const uint8_t item = record[size_t(11 + i)];
            snapshot.abilities[size_t(i)] =
                item_row(w, item, item >= 252 ? 255 : table_u8(m, Table::item_records, item, 3));
        }
    }
    return snapshot;
}

BestiaryUi::GridLayout BestiaryUi::grid_layout(emu::Wsc &w) {
    GridLayout layout{};
    const int page = std::clamp(int(w.ram[0xD852]), 0, 4);
    // The widest stat line any of the 150 entries can show (mod-aware, as the summary reads them).
    int hp = 0, stat = 0;
    for (unsigned type = 0; type < 150; ++type)
        if (const auto record = rom_record(Table::monsters, type, w); record.size() >= 11) {
            hp = std::max(hp, std::min(9999, int(record[3] | record[4] << 8)));
            for (int i = 1; i < 5; ++i)
                stat = std::max(stat, std::min(99, int(record[size_t(4 + i)])));
        }
    const auto widest = [](int n) { return C::text_width(S(std::to_string(n).size(), U'8')); };
    int need = C::text_width(host_text(w, "ui_bestiary_hp")) + widest(hp);
    for (const char *key : {"ui_party_str", "ui_party_def", "ui_party_agi", "ui_party_mag"})
        need = std::max(need, C::text_width(host_text(w, key)) + widest(stat));
    for (layout.gap = 3; layout.gap > 0 && kStatsX + need > 111 - 5 * layout.gap - 4; --layout.gap) {
    }
    layout.pane = 111 - 5 * layout.gap;
    int next = 20;
    for (int row = 0; row < 5; ++row) {
        const auto label = host_text(w, "ui_bestiary_family_" + std::to_string(page * 5 + row));
        int top = 8, bottom = 0;
        for (char32_t ch : label) {
            if (ch >= 0xE000 && ch < 0xE100) {
                const int tile = 0x100 + int(ch - 0xE000);
                for (int y = 0; y < 8; ++y)
                    for (int x = 0; x < 8; ++x) {
                        const uint8_t byte = w.ram[size_t(0x4000 + tile * 32 + y * 4 + x / 2)];
                        if ((x & 1 ? byte & 15 : byte >> 4) == 2) {
                            top = std::min(top, y);
                            bottom = std::max(bottom, y + 1);
                        }
                    }
            } else {
                const auto glyph = font_glyph(ch);
                for (int y = 0; y < 9; ++y)
                    if (glyph.rows[size_t(y)]) {
                        top = std::min(top, y);
                        bottom = std::max(bottom, y + 1);
                    }
            }
        }
        if (bottom <= top) {
            top = 0;
            bottom = 7;
        }
        layout.labels[size_t(row)] = next - top;
        layout.icons[size_t(row)] = next + bottom - top;
        next = layout.icons[size_t(row)] + 16;
    }
    return layout;
}

namespace {
void catalog_stats(C &c, emu::Wsc &w, const BestiaryUi::CatalogView &v, bool detail, int pane = 0) {
    constexpr const char *labels[] = {"ui_bestiary_hp", "ui_party_str", "ui_party_def", "ui_party_agi",
                                      "ui_party_mag"};
    const int x = detail ? 5 : BestiaryUi::kStatsX, edge = detail ? 54 : pane - 4;
    for (int i = 0; i < 5; ++i) {
        const int y = (detail ? 60 : 36) + i * 12;
        c.text(host_text(w, labels[i]), x, y, C::Ink::Text, 34);
        const auto n = number(v.stats[size_t(i)]);
        c.text(n, edge - C::text_width(n), y, C::Ink::Text, C::text_width(n));
    }
}
void catalog_sprite(C &c, Machine &m, int type, int x, int y, bool second, bool grid, int race_row = 0) {
    if (second) {
        std::array<uint8_t, 128> tiles{};
        constexpr std::array<uint16_t, 16> colors = {0, 0xFFF, 0xAAA, 0};
        if (GfxSwap::type_sprite_tiles(type, tiles))
            for (int q = 0; q < 4; ++q)
                c.tile(tiles.data() + q * 32, colors, 15, x + q % 2 * 8, y + q / 2 * 8);
    } else
        for (int q = 0; q < 4; ++q)
            c.tile(grid ? m.rd16(0x4000, uint16_t(0x5DD5 + race_row * 8 + q * 2))
                        : uint16_t(0x2000 | 8 << 9 | q),
                   x + q % 2 * 8, y + q / 2 * 8, false, !grid);
}
void catalog_portrait(C &c, Machine &m, int type, bool second) {
    // The same ROM/mod descriptor as creation_large_graphic_core. Native category redraws erase its
    // screen-map cells, so the host portrait must not depend on those transient cells. Catalog types use at
    // most 6x6 tiles; the larger boss art is outside this catalog.
    const auto p = monster_picture(m, uint8_t(type), second);
    if (second)
        draw_monster_picture(c, p, 5 + (48 - p.width) / 2, 36 + (48 - p.height) / 2);
    else
        draw_monster_picture(c, p, 5 + (p.width == 48 ? 0 : 8), 36);
}
} // namespace

std::vector<uint32_t> BestiaryUi::picture(emu::Wsc &w, emu::Wsc::ColorMode mode, bool second) {
    frame(w);
    auto px = w.framebuffer_rgb(mode, second);
    if (!active())
        return px;
    C c(w, px, mode, second);
    if (!c.ready())
        return px;
    Machine m(w);
    c.window(0, 0, 224, 144); // opaque; also backs the GB's rounded corners
    c.window(0, 0, 224, 16);
    // The catalog screens keep a gutter before the title for the category arrow.
    const bool catalog = scene_ == Scene::Catalog || scene_ == Scene::Grid || scene_ == Scene::Detail;
    c.text(host_text(w, "ui_bestiary_title"), catalog ? kTitleX : 5, 5, C::Ink::Text, catalog ? 76 : 120);
    if (scene_ == Scene::Empty) {
        // The native notice, its two lines.
        c.window(0, 96, 224, 48);
        c.text(native_text(m, TextKey::run_monster_history_menu_1), 5, 102, C::Ink::Text, 212);
        c.text(native_text(m, TextKey::run_monster_history_menu_2), 5, 114, C::Ink::Text, 212);
        return px;
    }
    if (scene_ == Scene::Catalog || scene_ == Scene::Grid || scene_ == Scene::Detail) {
        const auto snapshot = catalog_view(w);
        const bool detail = scene_ == Scene::Detail;
        const auto page = number(snapshot.page + 1) + U"/5";
        c.text(page, (detail ? 218 : 128) - C::text_width(page), 5, C::Ink::Text, C::text_width(page));
        if (detail) {
            c.text(host_text(w, "ui_bestiary_abilities"), 133, 5, C::Ink::Text, 64);
            c.window(0, 16, 224, 16);
            c.text(snapshot.name, 5, 20, C::Ink::Text, 102);
            c.text(host_text(w, "ui_bestiary_family") + U" " + snapshot.family, 112, 20, C::Ink::Text, 107);
            c.window(0, 32, 60, 112);
            c.window(60, 32, 164, DetailLine::kY - 32);
            catalog_sprite(c, m, snapshot.type, second ? 21 + kGbMenuSpriteDx : 21, 38, second, false);
            catalog_stats(c, w, snapshot, true);
            c.text(host_text(w, "ui_bestiary_back"), 5, 132, C::Ink::Text, 50);
            for (int i = 0; i < 8; ++i) {
                const int y = kAbilityY + i * 11;
                if (i == snapshot.selected) // the arrow marks the selected slot, even an empty one
                    c.cursor(66, y, true, 8);
                if (i >= snapshot.ability_count)
                    continue;
                const auto &r = snapshot.abilities[size_t(i)];
                const auto count = name_utf8(r.count);
                const bool unlimited = r.count == host_text(w, "ui_battle_no_uses");
                const int count_width = unlimited ? C::text_width(r.count) : C::numerals_width(count);
                c.item(r.id, r.name, 77, y, C::Ink::Muted, 136 - count_width);
                if (unlimited)
                    c.text(r.count, 216 - count_width, y, C::Ink::Muted, count_width);
                else
                    c.numerals(count, 216 - count_width, y + 1, C::Ink::Muted);
            }
            const auto &selected = snapshot.abilities[size_t(snapshot.selected)];
            detail_.update(w.frame_count, snapshot.type * 8 + snapshot.selected, selected.flavor, U"", 152);
            detail_.draw(c, DetailLine::kY, 60, 164);
            return px;
        }
        c.text(host_text(w, "ui_bestiary_history"), 159, 5, C::Ink::Muted, 59);
        if (scene_ == Scene::Catalog) // the native category selection
            c.cursor(snapshot.category ? 149 : 6, 5);
        const auto grid = grid_layout(w);
        const int pane = grid.pane, room = pane - 9; // text from x=5 to 1 px before the GB frame
        c.window(0, 16, pane, 112);
        c.window(pane, 16, 224 - pane, 128);
        c.text(snapshot.seen ? snapshot.name : host_text(w, "ui_bestiary_unseen"), 5, 22, C::Ink::Text, room);
        if (snapshot.seen) {
            catalog_portrait(c, m, snapshot.type, second);
            catalog_stats(c, w, snapshot, false, pane);
            c.text(host_text(w, "ui_bestiary_family"), 5, 98, C::Ink::Text, room);
            c.text(snapshot.family, 5, 110, C::Ink::Text, room);
        }
        c.window(0, 128, pane, 16);
        c.text(host_text(w, snapshot.seen ? "ui_bestiary_hint" : "ui_bestiary_back"), 5, 132, C::Ink::Text,
               room);
        for (int row = 0; row < 5; ++row) {
            c.text(host_text(w, "ui_bestiary_family_" + std::to_string(snapshot.page * 5 + row)), pane + 5,
                   grid.labels[size_t(row)], C::Ink::Text, 102, C::Window::Normal, false);
            for (int col = 0; col < 6; ++col) {
                const int type = snapshot.page * 30 + row * 6 + col;
                const int x = grid.x(col), y = grid.icons[size_t(row)];
                const bool seen = (w.ram[size_t(0xD0EC + type / 8)] & (0x80 >> (type % 8))) != 0;
                if (seen)
                    catalog_sprite(c, m, type, x, y, second, true, row);
                else
                    c.text(U"?", x + 4, y + 4, C::Ink::Muted, 8);
            }
        }
        // Up/Down scroll the five pages: the list's scroll marks, as the other lists show them, at the grid
        // window's right edge: up in the first family label's line (labels end by x=203), down at one fixed
        // place on every page, below page 1's last row (y=135). On pages whose family
        // labels have descenders the last row sits lower and the mark overlaps its last cell.
        if (snapshot.page > 0)
            c.more_above(212, 19);
        if (snapshot.page < 4) {
            c.more_below(212, 135);
        }
        // The arrow just left of the selected cell, its tip straddling the middle of a seen 16 px cell, or
        // its black row on the capital middle of an unseen "?": the grid is too dense for the hand. 1 px
        // before what it points at: the cell's picture, or the "?" in the
        // middle of an unseen cell (whose ink starts 5 px in -- from further left the arrow would hide the
        // "?" of the cell before).
        if (scene_ == Scene::Grid)
            c.cursor(grid.x(snapshot.column) - (snapshot.seen ? kGridArrow : kGridArrow - 5),
                     grid.icons[size_t(snapshot.row)] + (snapshot.seen ? 0 : 4), true,
                     snapshot.seen ? 16 : 7);
        return px;
    }
    const auto snapshot = view(w);
    const S generation = host_text(w, "ui_bestiary_generation", number(std::min(snapshot.generation, 9999)));
    c.text(generation, 218 - C::text_width(generation), 5, C::Ink::Text, 100);
    c.window(0, 16, 224, 24);
    draw_type_sprite(c, snapshot.sprite, 5, 20, second);
    c.text(snapshot.name, 25, 22, C::Ink::Text, 68);
    c.text(snapshot.type, 112, 22, C::Ink::Text, 102);
    c.window(0, 40, 224, 104);
    const int edge = 206; // the right gutter keeps the scroll arrows clear of the numbers
    for (int i = 0; i < kRows; ++i) {
        const auto &r = snapshot.rows[size_t(i)];
        if (r.name.empty())
            continue;
        c.text(r.name, 17, 46 + 12 * i, C::Ink::Text, 163);
        const auto n = std::to_string(r.generation);
        c.numerals(n, edge - C::numerals_width(n), 47 + 12 * i);
    }
    // More history beyond the seven rows: the native arrow tile at the window's right, up / down.
    if (snapshot.first > 0)
        c.tile(uint16_t(0x1F0 | 15 << 9), 212, 42);
    if (snapshot.first + kRows < snapshot.total)
        c.tile(uint16_t(0x1F0 | 15 << 9 | 0x8000), 212, 134);
    if (scene_ == Scene::History && snapshot.total > 0) // an empty history has no row to point at
        c.cursor(6, 46 + 12 * (snapshot.selected - snapshot.first));
    return px;
}

} // namespace sawgod::game
