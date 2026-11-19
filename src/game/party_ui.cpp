// Party, status, and equipment snapshots and host-drawn panels.
#include "game/party_ui.h"
#include "game/party_menu.h"
#include "game/text_tokens.h"

#include <algorithm>
#include <bit>
#include <string>

#include "game/gfx_swap.h"
#include "game/player_names.h"
#include "game/session.h"
#include "game/text_render.h"
#include "game/ui_sprites.h"

namespace sawgod::game {
namespace {
using C = Chrome;
using S = std::u32string;
int word(emu::Wsc &w, int at) { return w.ram[size_t(at)] | (w.ram[size_t(at + 1)] << 8); }
S number(int n) { return utf8_to_u32(std::to_string(n)); }
S decode(Machine &m, uint16_t segment, uint16_t at, int length) {
    static const auto charset = Charset::system();
    S s;
    for (int i = 0; i < length; ++i) {
        const auto code = m.rd8(segment, uint16_t(at + i));
        if (code == 255)
            break;
        s += utf8_to_u32(charset.decode(code));
    }
    while (!s.empty() && (s.back() == U' ' || s.back() == U'　'))
        s.pop_back();
    return s;
}
S member_name(emu::Wsc &w, int member) {
    auto name = Session::of(w).player_names().rendered(size_t(member));
    if (name.empty()) {
        Machine m(w);
        name = decode(m, 0, uint16_t(0xD001 + member * 31), 4);
    }
    return name;
}
S member_type(emu::Wsc &w, int member) {
    Machine m(w);
    const auto id = w.ram[size_t(0xD005 + member * 31)];
    const auto &t = Session::of(w).text();
    const int run = t.name_run(int(NameTable::Monster), id);
    if (run >= 0)
        if (const auto *s = t.run(uint16_t(run)))
            return *s;
    const auto p = name_record(m, NameTable::Monster, id);
    return decode(m, p.segment, p.offset, 8);
}
void right(C &c, const S &s, int x, int y) { c.text(s, x - C::text_width(s), y); }
// Status/Equip: the member pane is 25 px, the bottom line 17 (DetailLine), the list between them 102 px with
// rows 11 px apart, so all eight rows fit with room for descenders.
constexpr int kStatusTop = 25, kRowPitch = 11;
// The list's arrow: 1 px clear of the GB frame's inner line (x 60-62) on its left, 1 px before the icon
// cell on its right ("don't cram the arrow"). A row marked by the hand starts 8 px further right.
constexpr int kArrowX = 64, kNameX = 74, kHandX = 64, kHandNameX = 82, kNameEnd = 205;
// Choosing points with the original's hand; `arrow` keeps the list arrow (party order's hovered member).
// `inset` moves HP and hearts that many pixels left (the field menu, whose category hand reaches into
// the row's right end).
void member_row(C &c, emu::Wsc &w, int slot, int y, bool choosing, bool second, bool picked = false,
                bool compact = false, bool arrow = false, int inset = 0, int height = kPartyRowHeight) {
    c.window(0, y, 164, height);
    const int at = 0xD000 + slot * 31;
    if (w.ram[size_t(at)] == 255) {
        c.text(host_text(w, "ui_battle_empty_member"), 25, y + 4, C::Ink::Muted, 130);
        if (choosing || picked)
            c.hand(4, y + 6);
        return;
    }
    const int gutter = picked || (choosing && !arrow) ? 20 : choosing ? 10 : 0;
    Machine m(w);
    // The GB picture shows the member's GB sprite, not the WSC one in grays.
    std::array<uint8_t, 128> gb{};
    const bool gb_sprite = second && GfxSwap::type_sprite_tiles(w.ram[size_t(at + 5)], gb);
    static constexpr std::array<uint16_t, 16> kGbGreys = {0, 0xFFF, 0xAAA, 0x000};
    int sprite_top = 16; // first row of the 16x16 sprite cell that has ink
    for (int i = 0; i < 4; ++i)
        if (gb_sprite) {
            c.tile(gb.data() + i * 32, kGbGreys, 8 + slot, 5 + gutter + kGbMenuSpriteDx + (i % 2) * 8,
                   y + 4 + (i / 2) * 8);
            for (int row = 7; row >= 0; --row)
                if (std::any_of(gb.begin() + i * 32 + row * 4, gb.begin() + i * 32 + row * 4 + 4,
                                [](uint8_t b) { return b != 0; }))
                    sprite_top = std::min(sprite_top, (i / 2) * 8 + row);
        } else {
            const uint16_t entry = compact ? m.rd16(0x4000, uint16_t(0x926F + slot * 8 + i * 2))
                                           : uint16_t(0x2000 | (8 + slot) << 9 | (slot * 4 + i));
            c.tile(entry, 5 + gutter + (second ? kGbMenuSpriteDx : 0) + (i % 2) * 8, y + 4 + (i / 2) * 8,
                   false, !compact, compact);
            if (const int row = c.tile_ink_top(entry, !compact); row < 8)
                sprite_top = std::min(sprite_top, (i / 2) * 8 + row);
        }
    // A condition takes max HP's place, as in the original (its lowest bit wins: dead, stone, curse, blind).
    static constexpr const char *kConditions[] = {"ui_status_dead", "ui_status_stone", "ui_status_curse",
                                                  "ui_status_blind"};
    const int condition = std::countr_zero(unsigned(w.ram[size_t(at)] & 15));
    const S hp = number(word(w, at + 6)) + U"/" +
                 (condition < 4 ? host_text(w, kConditions[condition]) : number(word(w, at + 8)));
    right(c, hp, 159 - inset, y + 4);
    // A long name stops 4 px short of the HP (a condition word is wider than max HP).
    const int name_room = std::min(68, 159 - inset - C::text_width(hp) - 4 - (25 + gutter));
    c.text(member_name(w, slot), 25 + gutter, y + 4, C::Ink::Text, name_room);
    c.text(member_type(w, slot), 25 + gutter, y + 13, C::Ink::Text, 74);
    // The heart sits clear of the ×.
    c.tile(uint16_t((15 << 9) | 0x1D1), 135 - inset, y + 13);
    c.text(U"×", 143 - inset, y + 13, C::Ink::Text, 10);
    c.numerals(std::to_string(w.ram[size_t(at + 10)] & 15), 153 - inset, y + 14);
    // The hand points at the member's sprite: its top row is level with the sprite's first inked
    // row. The arrow keeps the name line (tip rows 3-4).
    if (picked || (choosing && !arrow))
        c.hand(4, y + 4 + (sprite_top < 16 ? sprite_top : 0));
    else if (choosing)
        c.cursor(6, y + 3);
}
void gold(C &c, emu::Wsc &w, bool member_stack) {
    const int offset = member_stack ? 2 : 0;
    c.window(164, member_stack ? kPartyFooterY : 112, 60, member_stack ? kPartyFooterHeight : 32);
    right(
        c,
        number(int(std::min(uint32_t(999999), uint32_t(word(w, 0xD0A2)) | uint32_t(word(w, 0xD0A4)) << 16))),
        218, 118 + offset);
    right(c, host_text(w, "ui_party_kero"), 218, 130 + offset);
}
void stats(C &c, emu::Wsc &w, int member) {
    c.window(0, kStatusTop, 60, DetailLine::kY - kStatusTop);
    constexpr const char *labels[] = {"ui_party_str", "ui_party_def", "ui_party_agi", "ui_party_mag"};
    for (int i = 0; i < 4; ++i) {
        c.text(host_text(w, labels[i]), 5, 55 + i * 12, C::Ink::Text, 38);
        right(c, number(w.ram[size_t(0xD00B + member * 31 + i)]), 55, 55 + i * 12);
    }
}
} // namespace

void draw_party_row(Chrome &c, emu::Wsc &w, int slot, int y, bool choosing, bool second, bool picked) {
    member_row(c, w, slot, y, choosing, second, picked);
}
S party_name(emu::Wsc &w, int member) { return member_name(w, member); }
S party_type(emu::Wsc &w, int member) { return member_type(w, member); }
void party_row(C &c, emu::Wsc &w, int member, int y, bool selected, bool second, bool compact) {
    member_row(c, w, member, y, selected, second, false, compact);
}
void party_gold(C &c, emu::Wsc &w, bool member_stack) { gold(c, w, member_stack); }
void party_stats(C &c, emu::Wsc &w, int member) { stats(c, w, member); }
S menu_text(emu::Wsc &w, TextKey key) {
    auto &table = Session::of(w).text();
    const int offset = table.find(key);
    if (offset >= 0) {
        if (const auto *s = table.characters(uint16_t(offset)))
            return *s;
        Machine m(w);
        return with_button_tokens(table, key, decode(m, kTextSegment, uint16_t(offset), 255));
    }
    Machine m(w);
    const auto &info = kTextKeys[size_t(key)];
    return with_button_tokens(table, key, decode(m, info.segment, uint16_t(info.string), 255));
}

void PartyUi::install(emu::Wsc &w) {
    subscriptions_.clear();
    scene_ = Scene::Hidden;
    suspended_ = false;
    parents_.clear();
    Session::of(w).player_names().enabled = true;
    generation_ = Session::of(w).generation();
    first_.fill(0);
    detail_.reset();
    subscriptions_.push_back(Session::of(w).hooks().subscribe(
        hooks::Event::PartyMenu, [this, &w](Machine &, const hooks::Args &args) {
            frame(w);
            if (args.b == 1) {
                parents_.push_back(suspended_ ? held_scene_ : scene_);
                return;
            }
            if (args.b == 2) {
                held_scene_ = scene_;
                scene_ = Scene::Hidden;
                suspended_ = true;
                return;
            }
            const auto scene = args.b == -1 ? (parents_.empty() ? Scene::Hidden : parents_.back())
                                            : Scene(std::clamp(args.a, 0, 10));
            if (args.b == -1 && !parents_.empty())
                parents_.pop_back();
            suspended_ = false;

            // (picking a Status row and putting it back keep the list and its description where they are)
            const bool pick = (scene_ == Scene::Status && scene == Scene::StatusPicked) ||
                              (scene_ == Scene::StatusPicked && scene == Scene::Status);
            if (scene_ != scene && !pick) {
                detail_.reset();
                first_.fill(0);
            }
            scene_ = scene;
        }));
}
void PartyUi::frame(emu::Wsc &w) {
    if (generation_ != Session::of(w).generation()) {
        scene_ = Scene::Hidden;
        generation_ = Session::of(w).generation();
        first_.fill(0);
        detail_.reset();
        suspended_ = false;
        parents_.clear();
    }
    if (active() && scene_ != Scene::Field && scene_ != Scene::OrderSource &&
        scene_ != Scene::OrderDestination && scene_ != Scene::OrderSwap)
        view(w);
}
PartyUi::View PartyUi::view(emu::Wsc &w) {
    View snapshot;
    snapshot.member = std::clamp(int(w.ram[0xD853]), 0, 3);
    const int at = 0xD000 + snapshot.member * 31;
    snapshot.monster = (w.ram[size_t(at + 10)] & 64) != 0;
    snapshot.mutant = !snapshot.monster && (w.ram[size_t(at + 10)] & 32) != 0;
    const bool editing = scene_ == Scene::Equip || scene_ == Scene::EquipDestination;
    snapshot.inventory = editing && w.ram[0xD852] != 0;
    snapshot.key = snapshot.inventory && w.ram[0xD855] == 1;
    snapshot.selected = std::clamp(int(w.ram[snapshot.inventory ? 0xD843 : 0xD842]), 0, snapshot.key ? 8 : 7);
    snapshot.abilities =
        !snapshot.inventory && (snapshot.monster || (snapshot.mutant && snapshot.selected < 4));
    if (snapshot.inventory) {
        snapshot.count = 6;
        auto &first = first_[size_t(snapshot.key)];
        if (snapshot.selected < first)
            first = snapshot.selected;
        if (snapshot.selected >= first + snapshot.count)
            first = snapshot.selected - snapshot.count + 1;
        snapshot.first = first = std::clamp(first, 0, (snapshot.key ? 9 : 8) - snapshot.count);
        for (int i = 0; i < snapshot.count; ++i)
            snapshot.rows[size_t(i)] = inventory_row(w, snapshot.key ? 1 : 0, first + i);
    } else {
        snapshot.count = snapshot.mutant ? 4 : 8;
        snapshot.base = snapshot.mutant && !snapshot.abilities ? 4 : 0;
        for (int i = 0; i < snapshot.count; ++i) {
            const int item = at + 15 + (snapshot.base + i) * 2;
            snapshot.rows[size_t(i)] = item_row(w, w.ram[size_t(item)], w.ram[size_t(item + 1)]);
        }
    }
    const int index =
        snapshot.inventory ? snapshot.selected - snapshot.first : snapshot.selected - snapshot.base;
    snapshot.detail = snapshot.rows[size_t(std::clamp(index, 0, snapshot.count - 1))];
    detail_.update(w.frame_count,
                   snapshot.member * 64 + (snapshot.inventory ? (snapshot.key ? 32 : 16) : 0) +
                       snapshot.selected,
                   snapshot.detail.flavor, snapshot.detail.stats);
    return snapshot;
}
std::vector<uint32_t> PartyUi::picture(emu::Wsc &w, emu::Wsc::ColorMode mode, bool second) {
    frame(w);
    if (scene_ == Scene::Hidden)
        return w.framebuffer_rgb(mode, second);
    return picture_now(w, mode, second);
}
std::vector<uint32_t> PartyUi::picture_now(emu::Wsc &w, emu::Wsc::ColorMode mode, bool second) {
    auto px = w.framebuffer_rgb(mode, second);
    C c(w, px, mode, second);
    if (!c.ready())
        return px;
    // These screens cover the full display. Opaque native fills also back the GB's rounded corners.
    c.window(0, 0, 224, 144);
    if (scene_ == Scene::Field) {
        c.window(0, 0, 224, 16);
        c.text(host_text(w, "ui_party_menu"), 5, 5, C::Ink::Text, 70);
        const uint32_t frames = uint32_t(word(w, 0xD0DF)) | uint32_t(word(w, 0xD0E1)) << 16;
        const int minutes = int(std::min(uint32_t(5999), frames / 0x1194));
        const auto mm = minutes % 60;
        right(c,
              host_text(w, "ui_party_time") + U" " + number(minutes / 60) + U":" + (mm < 10 ? U"0" : U"") +
                  number(mm),
              218, 5);
        for (int i = 0; i < 4; ++i)
            member_row(c, w, i, 16 + i * kPartyRowHeight, false, second, false, false, false,
                       5); // clear of the hand
        c.window(164, 16, 60, kPartyFooterY - 16);
        constexpr const char *keys[] = {"ui_party_items", "ui_party_status",   "ui_party_equip",
                                        "ui_party_order", "ui_party_bestiary", "ui_party_config",
                                        "ui_party_save"};
        Machine m(w);
        const bool config = hooks::should(hooks::VB::ConfigTab, m);
        const PartyMenuRows rows(config, hooks::filter(hooks::Value::PartyMenuRows, m, 0));
        int row = 0;
        for (int i = 0; i < 7; ++i) {
            if (!config && i == 5)
                continue;
            if (i == 6 && rows.hosts) {
                c.text(host_text(w, "ui_party_options"), 171, 22 + row * rows.pitch(), C::Ink::Text, 48);
                ++row;
            }
            c.text(host_text(w, keys[i]), 171, 22 + row * rows.pitch(), C::Ink::Text, 48);
            ++row;
        }
        if (rows.hosts == 2) {
            c.text(host_text(w, "ui_party_load"), 171, 22 + row * rows.pitch(), C::Ink::Text, 48);
            ++row;
        }
        // The original's hand, its fingertip level with the category's capitals.
        c.hand(155, 24 + std::clamp(int(w.ram[0xD841]), 0, row - 1) * rows.pitch());
        c.window(0, kPartyFooterY, 164, kPartyFooterHeight);
        c.text(host_text(w, "ui_party_current_floor", number(w.ram[0xD0DB]) + U"F"), 5, 120, C::Ink::Text,
               154);
        c.text(host_text(w, "ui_party_highest_floor", number(w.ram[0xD0DA]) + U"F"), 5, 132, C::Ink::Text,
               154);
        gold(c, w, true);
        return px;
    }
    if (scene_ == Scene::OrderSource || scene_ == Scene::OrderDestination || scene_ == Scene::OrderSwap) {
        // Party order, as Equip: the arrow is on the hovered member (0xD842) while
        // picking; while choosing where to move it the hand is the live cursor on the hovered member and the
        // arrow stays on the picked one (0xD840) as an anchor, unless both are the same row (hand only). The
        // right column has the hovered member's stats.
        const bool destination = scene_ == Scene::OrderDestination || scene_ == Scene::OrderSwap;
        const int hovered = std::clamp(int(w.ram[0xD842]), 0, 3),
                  picked = std::clamp(int(w.ram[0xD840]), 0, 3);
        c.window(0, 0, 224, 16);
        c.text(host_text(w, "ui_party_order"), 5, 5, C::Ink::Text, 212);
        for (int i = 0; i < 4; ++i) {
            const bool anchor = destination && i == picked && i != hovered;
            member_row(c, w, i, 16 + i * kPartyRowHeight, i == hovered || anchor, second,
                       destination && i == hovered, false, !destination || anchor);
        }
        c.window(164, 16, 60, kPartyFooterY - 16);
        if (w.ram[size_t(0xD000 + hovered * 31)] != 255) {
            constexpr const char *labels[] = {"ui_party_str", "ui_party_def", "ui_party_agi", "ui_party_mag"};
            for (int i = 0; i < 4; ++i) {
                c.text(host_text(w, labels[i]), 169, 38 + i * 12, C::Ink::Text, 34);
                right(c, number(w.ram[size_t(0xD00B + hovered * 31 + i)]), 219, 38 + i * 12);
            }
        }
        c.window(0, kPartyFooterY, 224, kPartyFooterHeight);
        // While the two run, the original's own label (its speech bubble beside the rows) says what happens.
        c.text(scene_ == Scene::OrderSwap
                   ? menu_text(w, TextKey::append_party_swap_label_sprites_1)
                   : host_text(w, destination ? "ui_party_order_target" : "ui_party_order_source"),
               5, 120, C::Ink::Text, 212);
        return px;
    }
    const auto snapshot = view(w);
    const bool choosing = scene_ == Scene::StatusMembers || scene_ == Scene::EquipMembers;
    member_row(c, w, snapshot.member, 0, choosing, second, false, false, false, 0, kStatusTop);
    c.window(164, 0, 60, kStatusTop);
    const auto heading =
        host_text(w, snapshot.monster || snapshot.abilities ? "ui_party_abilities" : "ui_party_gear");
    if (!snapshot.mutant) // the capitals (7 px) in the middle of the pane: 9 px above and below
        c.text(heading, 164 + (60 - C::text_width(heading)) / 2, (kStatusTop - 7) / 2, C::Ink::Text, 50);
    stats(c, w, snapshot.member);
    const int top = snapshot.mutant ? kStatusTop + 4 : kStatusTop;
    int row_y = top + 7;
    if (snapshot.mutant || snapshot.inventory) {
        c.tabbed_window(60, top, 164, DetailLine::kY - top, 20, 72,
                        snapshot.inventory || snapshot.abilities ? 1 : 0);
        const auto left = host_text(w, "ui_party_gear");
        const auto right_label =
            host_text(w, snapshot.inventory ? (snapshot.key ? "ui_inventory_keys" : "ui_inventory_items")
                                            : "ui_party_abilities");
        c.text(left, 60 + (72 - C::text_width(left)) / 2, top + 7, C::Ink::Text, 62);
        c.text(right_label, 132 + (92 - C::text_width(right_label)) / 2, top + 7, C::Ink::Text, 82);
        row_y = top + 26;
    } else {
        c.window(60, top, 164, DetailLine::kY - top);
    }
    const int first = snapshot.inventory ? snapshot.first : snapshot.base;
    // An empty ("None") row picked and still hovered carries nothing yet (A again opens the other list): it
    // keeps the picking look, the arrow and unindented names. The hand and the anchor come
    // when the cursor moves on.
    bool placing = scene_ == Scene::EquipDestination;
    if (placing && w.ram[0xD854] == w.ram[0xD852] && w.ram[0xD840] == snapshot.selected) {
        const int source = w.ram[0xD840] - first;
        if (source >= 0 && source < snapshot.count) {
            const auto &r = snapshot.rows[size_t(source)];
            placing = !(r.id < 0 && r.name == host_text(w, "ui_battle_empty_slot"));
        }
    }
    for (int i = 0; i < snapshot.count; ++i) {
        const auto &r = snapshot.rows[size_t(i)];
        const int y = row_y + i * kRowPitch;
        // Abilities are muted; the selected one has the arrow, as a gear row.
        const auto ink = snapshot.abilities ? C::Ink::Muted : C::Ink::Text;
        const bool picked = scene_ == Scene::StatusPicked;
        const int name_x = placing || picked ? kHandNameX : kNameX;
        c.item(r.id, r.name, name_x, y, ink, (snapshot.key ? kNameEnd + 11 : kNameEnd) - name_x);
        if (!snapshot.key) {
            if (r.count == host_text(w, "ui_battle_no_uses"))
                right(c, r.count, 216, y);
            else
                c.numerals(name_utf8(r.count), 216 - C::numerals_width(name_utf8(r.count)), y + 1, ink);
        }
    }
    if (scene_ == Scene::StatusPicked) {
        // The picked row (0xD840) takes only A and B: the hand stays on it, as on Equip's picked source.
        const int source = w.ram[0xD840] - snapshot.base;
        if (source >= 0 && source < snapshot.count)
            c.hand(kHandX, row_y + 2 + source * kRowPitch);
        c.window(0, DetailLine::kY, 224, DetailLine::kHeight);
        c.text(host_text(w, "ui_party_picked_hint"), 5, DetailLine::kY + 4, C::Ink::Text, 212);
        return px;
    }
    if (placing) {
        c.hand(kHandX, row_y + 2 + (snapshot.selected - first) * kRowPitch);
        if (w.ram[0xD854] == w.ram[0xD852]) {
            const int source = w.ram[0xD840] - first;
            if (source >= 0 && source < snapshot.count && w.ram[0xD840] != snapshot.selected)
                c.cursor(kArrowX, row_y + source * kRowPitch, true, 8);
        }
    } else if (!choosing) {
        c.cursor(kArrowX, row_y + (snapshot.selected - first) * kRowPitch, true, 8);
    }
    if (snapshot.inventory && snapshot.first > 0)
        c.more_above(212, row_y - 6); // under the open tab's label, 2 px clear of the first row's count
    if (snapshot.inventory && snapshot.first + snapshot.count < (snapshot.key ? 9 : 8))
        c.more_below(212, DetailLine::kY - 8); // 1 px clear of the GB frame and the last row's count
    // Up/Down switch to the previous/next member (they wrap), so a party of two or more shows both marks,
    // centered in the stat box: up at its top edge, down at its bottom. Not while an
    // item is picked or being placed, when the keys are not member keys.
    int members = 0;
    for (int m = 0; m < 4; ++m)
        members += w.ram[size_t(0xD000 + m * 31)] != 255;
    if (members > 1 && scene_ != Scene::StatusPicked && !placing) {
        c.more_above(26, kStatusTop + 3);
        c.more_below(26, DetailLine::kY - 8);
    }
    detail_.draw(c);
    return px;
}
} // namespace sawgod::game
