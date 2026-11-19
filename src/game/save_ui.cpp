// Save-slot snapshots, confirmation prompts, and host-drawn save panels.
#include "game/save_ui.h"
#include "game/gfx_swap.h"
#include "game/party_ui.h"
#include "game/session.h"
#include "game/text_render.h"
#include "game/ui_sprites.h"
#include <algorithm>
namespace sawgod::game {
namespace {
using C = Chrome;
using S = std::u32string;
S number(uint32_t n) { return utf8_to_u32(std::to_string(n)); }
uint32_t value(const std::vector<uint8_t> &p, int offset, int length) {
    uint32_t n = 0;
    for (int i = 0; i < length; ++i)
        n |= uint32_t(p[size_t(offset + i)]) << (i * 8);
    return n;
}
void right(C &c, const S &s, int edge, int y) { c.text(s, edge - C::text_width(s), y); }
S name(emu::Wsc &w, int slot, int member, const std::vector<uint8_t> &p) {
    auto &slots = Session::of(w).save_slots();
    if (slots.holds(slot) && !slots.names(slot)[size_t(member)].empty())
        return utf8_to_u32(slots.names(slot)[size_t(member)]);
    S result;
    const auto charset = Charset::system();
    for (int i = 0; i < 4; ++i) {
        const auto ch = p[size_t(member * 31 + 1 + i)];
        if (ch == 255)
            break;
        result += utf8_to_u32(charset.decode(ch));
    }
    return result;
}
} // namespace
std::vector<uint8_t> SaveUi::payload(emu::Wsc &w, int slot) {
    auto &slots = Session::of(w).save_slots();
    if (slots.holds(slot))
        return slots.payload(slot);
    if (slot < 0 || slot >= 3)
        return {};
    Machine m(w);
    for (int backup : {0, 0xCB1}) {
        const auto at = uint16_t(0x3301 + slot * 0x43B + backup);
        std::vector<uint8_t> p(SaveSlots::kPayload);
        uint16_t sum = 0;
        for (size_t i = 0; i < p.size(); ++i) {
            p[i] = m.rd8(0x1000, uint16_t(at + i));
            sum = uint16_t(sum + p[i]);
        }
        if (sum == m.rd16(0x1000, uint16_t(at + p.size())) && p[0x435] == 0x1B && p[0x436] == 0xE4)
            return p;
    }
    return {};
}
void SaveUi::install(emu::Wsc &w) {
    subscriptions_.clear();
    stack_.clear();
    creation_ = naming_ = false;
    generation_ = Session::of(w).generation();
    Session::of(w).player_names().enabled = true;
    subscriptions_.push_back(Session::of(w).hooks().subscribe(
        hooks::Value::SaveCompleteKeys, [](Machine &, int32_t keys, const hooks::Args &) {
            return keys & emu::KEY_B ? int32_t(emu::KEY_A) : keys;
        }));
    subscriptions_.push_back(
        Session::of(w).hooks().subscribe(hooks::Event::SaveMenu, [this, &w](Machine &, const hooks::Args &a) {
            frame(w);
            if (a.b == 1)
                stack_.push_back(Scene(std::clamp(a.a, 0, 7)));
            else if (a.b == -1) {
                if (!stack_.empty())
                    stack_.pop_back();
            } else if (!stack_.empty())
                stack_.back() = Scene(std::clamp(a.a, 0, 7));
        }));
    subscriptions_.push_back(Session::of(w).hooks().subscribe(hooks::Event::CreationMenu,
                                                              [this, &w](Machine &, const hooks::Args &a) {
                                                                  frame(w);
                                                                  creation_ = a.a != 0;
                                                              }));
    subscriptions_.push_back(Session::of(w).hooks().subscribe(hooks::Event::NameEntry,
                                                              [this, &w](Machine &, const hooks::Args &a) {
                                                                  frame(w);
                                                                  naming_ = a.a != 0;
                                                              }));
}
void SaveUi::frame(emu::Wsc &w) {
    if (generation_ != Session::of(w).generation()) {
        stack_.clear();
        creation_ = naming_ = false;
        generation_ = Session::of(w).generation();
    }
}
std::vector<uint32_t> SaveUi::picture(emu::Wsc &w, emu::Wsc::ColorMode mode, bool second) {
    frame(w);
    auto px = w.framebuffer_rgb(mode, second);
    if (!active())
        return px;
    C c(w, px, mode, second);
    c.window(0, 0, 224, 144);
    c.window(0, 0, 224, 16);
    const bool saving = scene() == Scene::Save || scene() == Scene::SaveConfirm ||
                        scene() == Scene::Complete || scene() == Scene::Blocked;
    c.text(menu_text(w, saving ? TextKey::draw_save_menu_1 : TextKey::draw_load_menu_screen_1), 5, 5,
           C::Ink::Text, 214);
    auto &slots = Session::of(w).save_slots();
    for (int row = 0; row < 3; ++row) {
        const int slot = slots.slot_for_row(row), y = 24 + row * 40;
        c.window(0, y, 224, 40);
        c.text(slot == SaveSlots::kAuto ? host_text(w, "ui_save_autosave") : number(slot + 1), 15, y + 5,
               saving && slot == SaveSlots::kAuto ? C::Ink::Muted : C::Ink::Text, 28);
        const auto p = payload(w, slot);
        if (p.size() != SaveSlots::kPayload) {
            c.text(menu_text(w, TextKey::draw_save_slot_summary_3), 65, y + 5, C::Ink::Muted, 150);
            continue;
        }
        const int leader = std::clamp(int(p[0xE3]), 0, 3), at = leader * 31;
        std::array<uint8_t, 128> gb{};
        const bool gb_sprite = second && GfxSwap::type_sprite_tiles(p[size_t(at + 5)], gb);
        static constexpr std::array<uint16_t, 16> kGbGreys = {0, 0xFFF, 0xAAA, 0x000};
        for (int tile = 0; tile < 4; ++tile)
            if (gb_sprite)
                c.tile(gb.data() + tile * 32, kGbGreys, 8 + row, 45 + kGbMenuSpriteDx + tile % 2 * 8,
                       y + 4 + tile / 2 * 8);
            else
                c.tile(uint16_t(0x2000 | ((8 + row) << 9) | (row * 4 + tile)),
                       45 + (second ? kGbMenuSpriteDx : 0) + tile % 2 * 8, y + 4 + tile / 2 * 8, false, true);
        c.text(name(w, slot, leader, p), 65, y + 5, C::Ink::Text, 68);
        right(c, number(value(p, at + 6, 2)) + U"/" + number(value(p, at + 8, 2)), 218, y + 5);
        c.text(host_text(w, "ui_party_current_floor", number(p[0xDB]) + U"F"), 65, y + 16, C::Ink::Text, 105);
        c.tile(uint16_t((15 << 9) | 0x1D1), 190, y + 16);
        c.text(U"×", 198, y + 16, C::Ink::Text, 10);
        c.numerals(std::to_string(p[size_t(at + 10)] & 15), 210, y + 17);
        c.text(host_text(w, "ui_party_highest_floor", number(p[0xDA]) + U"F"), 5, y + 27, C::Ink::Text, 114);
        const uint32_t minutes = std::min(uint32_t(5999), value(p, 0xDF, 4) / 0x1194);
        c.text(number(minutes / 60) + U":" + (minutes % 60 < 10 ? U"0" : U"") + number(minutes % 60), 116,
               y + 27, C::Ink::Text, 35);
        right(c, number(std::min(uint32_t(999999), value(p, 0xA2, 4))) + U" " + host_text(w, "ui_party_kero"),
              218, y + 27);
    }
    c.cursor(6, 29 + std::clamp(int(w.ram[0xD842]), 0, 2) * 40);
    if (slots.top() > 0)
        c.tile(uint16_t((15 << 9) | 0x1F0), 192, 16);
    if (slots.top() + 3 < slots.entries())
        c.tile(uint16_t((15 << 9) | 0x1F0 | 0x8000), 208, 16);
    if (scene() == Scene::Blocked) {
        c.window(0, 0, 224, 24);
        c.text(host_text(w, "ui_save_unreadable"), 5, 5, C::Ink::Text, 212);
    } else if (scene() == Scene::LoadConfirm || scene() == Scene::SaveConfirm) {
        c.window(0, 0, 224, 24);
        const auto p = payload(w, slots.slot_for_row(std::clamp(int(w.ram[0xD842]), 0, 2)));
        const bool inherit = !saving && p.size() == SaveSlots::kPayload && p[0xE4];
        if (inherit) {
            c.text(host_text(w, "ui_save_inherit_first"), 5, 4, C::Ink::Text, 212);
            c.text(host_text(w, "ui_save_inherit_second"), 5, 13, C::Ink::Text, 134);
        } else {
            c.text(saving && !p.empty() ? host_text(w, "ui_save_overwrite")
                                        : menu_text(w, saving ? TextKey::draw_save_confirmation_4
                                                              : TextKey::draw_load_slot_prompt_5),
                   5, 4, C::Ink::Text, 212);
        }
        c.text(menu_text(w, TextKey::draw_save_confirmation_1), 153, 13, C::Ink::Text, 30);
        c.text(menu_text(w, TextKey::draw_save_confirmation_2), 190, 13, C::Ink::Text, 29);
        c.cursor(w.ram[0xD843] ? 179 : 142, 13);
    } else if (scene() == Scene::Complete || scene() == Scene::Invalid) {
        c.window(0, 0, 224, 24);
        c.text(menu_text(w, scene() == Scene::Complete ? TextKey::wait_save_confirmation_1
                                                       : TextKey::draw_load_slot_prompt_1),
               5, 4, C::Ink::Text, 212);
        c.text(menu_text(w, scene() == Scene::Complete ? TextKey::wait_save_confirmation_2
                                                       : TextKey::draw_load_slot_prompt_2),
               5, 13, C::Ink::Text, 212);
    }
    return px;
}
} // namespace sawgod::game
