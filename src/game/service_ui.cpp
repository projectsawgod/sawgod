// Shop, inn, and recruitment snapshots and host-drawn service panels.
#include "game/service_ui.h"
#include "game/party_ui.h"
#include "game/session.h"
#include "game/text_render.h"
#include "game/text_wrap.h"
#include "game/ui_items.h"
#include <algorithm>

namespace sawgod::game {
namespace {
using C = Chrome;
using S = std::u32string;
int word(emu::Wsc &w, int at) { return w.ram[size_t(at)] | w.ram[size_t(at + 1)] << 8; }
S number(uint32_t n) { return utf8_to_u32(std::to_string(n)); }
uint32_t amount(emu::Wsc &w, int at) { return uint32_t(word(w, at)) | uint32_t(word(w, at + 2)) << 16; }
void title(C &c, const S &s) {
    c.window(0, 0, 224, 16);
    c.text(s, 5, 4, C::Ink::Text, 214); // one up from the other title bars: descenders clear the frame
}
void choices(C &c, emu::Wsc &w, int selected, TextKey yes, TextKey no, int offset = 0,
             int cursor_offset = 0) {
    c.text(menu_text(w, yes), 17, 130 + offset, C::Ink::Text, 65);
    c.text(menu_text(w, no), 93, 130 + offset, C::Ink::Text, 65);
    c.cursor(selected ? 82 : 6, 130 + offset + cursor_offset);
}
uint32_t price(Machine &m, int id) {
    uint32_t price = 0;
    for (int i = 0; i < 4; ++i)
        price |= uint32_t(table_u8(m, Table::item_prices, id, i)) << (8 * i);
    return price;
}
uint32_t sale_price(Machine &m, int id, int uses) {
    const auto full = price(m, id);
    const int maximum = table_u8(m, Table::item_records, id, 3);
    if (maximum == 254)
        return full / 2;
    if (!maximum)
        return 0;
    return ((uint32_t(full * uint32_t(uses * 16)) >> 4) / unsigned(maximum)) / 2;
}
} // namespace
void ServiceUi::install(emu::Wsc &w) {
    subscriptions_.clear();
    stack_.clear();
    generation_ = Session::of(w).generation();
    Session::of(w).player_names().enabled = true;
    subscriptions_.push_back(Session::of(w).hooks().subscribe(
        hooks::Event::ServiceMenu, [this, &w](Machine &, const hooks::Args &a) {
            frame(w);
            if (a.b == 1) {
                if (stack_.empty()) {
                    w.refresh_second(); // the town behind the windows, even if GB isn't showing yet
                    for (int mode = 0; mode < 2; ++mode)
                        for (int second = 0; second < 2; ++second)
                            town_[size_t(mode)][size_t(second)] = w.framebuffer_rgb(
                                mode ? emu::Wsc::ColorMode::Lcd : emu::Wsc::ColorMode::Raw, second);
                    first_ = 0;
                    detail_.reset();
                }
                stack_.push_back(a.a == kNotice ? Panel{scene(), a.c} : Panel{Scene(std::clamp(a.a, 0, 12))});
            } else if (a.b == -1) {
                if (!stack_.empty())
                    stack_.pop_back();
            } else if (!stack_.empty()) {
                if (a.a == kNotice)
                    stack_.back().message = a.c;
                else
                    stack_.back() = Panel{Scene(std::clamp(a.a, 0, 12))};
            }
        }));
    // Two-way choices (shop Yes / No, Inn, Guild replacement) sit side by side here, so Left / Right move the
    // game's up/down choice too.
    subscriptions_.push_back(Session::of(w).hooks().subscribe(
        hooks::Value::ServiceChoiceKeys, [this](Machine &, int32_t value, const hooks::Args &) {
            if (!active())
                return value;
            uint16_t keys = uint16_t(value);
            if (keys & 0x80) // X4 left: the previous choice, as X1 up
                keys |= 0x10;
            if (keys & 0x20) // X2 right: the next choice, as X3 down
                keys |= 0x40;
            return int32_t(keys);
        }));
}
void ServiceUi::frame(emu::Wsc &w) {
    if (generation_ != Session::of(w).generation()) {
        stack_.clear();
        detail_.reset();
        generation_ = Session::of(w).generation();
    }
}
std::vector<uint32_t> ServiceUi::picture(emu::Wsc &w, emu::Wsc::ColorMode mode, bool second) {
    frame(w);
    if (!active())
        return w.framebuffer_rgb(mode, second);
    auto px = town_[mode == emu::Wsc::ColorMode::Lcd ? 1 : 0][size_t(second)];
    if (px.empty())
        px = w.framebuffer_rgb(mode, second);
    C c(w, px, mode, second);
    const auto scene = this->scene();
    const int message = stack_.back().message;
    const auto notice = [&](int y, int height) {
        if (message < 0)
            return;
        auto text = menu_text(w, TextKey(message));
        const auto item = C::item_run(w.ram[0xD855], item_row(w, w.ram[0xD855], 1).name);
        for (size_t at = 0; (at = text.find(U"{item}", at)) != S::npos; at += item.size())
            text.replace(at, 6, item);
        // A notice owns the input: erase the former choices and their pointer together.
        c.window(0, y, 164, height);
        const auto wrapped = wrap_text(text, 154);
        for (size_t row = 0; row < wrapped.lines.size(); ++row)
            c.text(wrapped.lines[row].text, 5, y + 4 + int(row) * 12, C::Ink::Text, 154);
    };
    const bool service = scene == Scene::Inn || scene == Scene::Revival || scene == Scene::Guild ||
                         scene == Scene::Replace || scene == Scene::Refusal || scene == Scene::Members;
    if (service) {
        title(c, scene == Scene::Members
                     ? C::item_run(w.ram[0xD855], item_row(w, w.ram[0xD855], 1).name)
                     : menu_text(w, scene == Scene::Inn       ? TextKey::draw_inn_screen_1
                                    : scene == Scene::Revival ? TextKey::draw_revive_member_screen_1
                                                              : TextKey::draw_recruitment_member_screen_1));
        const int selected = scene == Scene::Inn       ? -1
                             : scene == Scene::Members ? w.ram[0xD844]
                             : scene == Scene::Replace || scene == Scene::Guild || scene == Scene::Refusal
                                 ? w.ram[0xD840]
                                 : w.ram[0xD841];
        for (int i = 0; i < 4; ++i)
            party_row(c, w, i, 16 + i * kPartyRowHeight,
                      message < 0 && scene != Scene::Replace && selected == i, second, true);
        party_gold(c, w, true);
        c.window(0, kPartyFooterY, 164, kPartyFooterHeight);
        if (message >= 0) {
            notice(kPartyFooterY, kPartyFooterHeight);
        } else if (scene == Scene::Inn) {
            // The price as the gold pane below shows the party's gold: the amount, then the currency on
            // its own line (so the largest price, 3992, and a longer currency word both fit), then the rate.
            c.window(164, 16, 60, 44);
            const auto price = number(amount(w, 0xD84C)), kero = host_text(w, "ui_party_kero");
            c.text(price, 218 - C::text_width(price), 22, C::Ink::Text, 50);
            c.text(kero, 218 - std::min(50, C::text_width(kero)), 34, C::Ink::Text, 50);
            c.text(host_text(w, "ui_service_inn_rate"), 168, 46, C::Ink::Text, 52);
            c.text(menu_text(w, TextKey::draw_inn_screen_2), 5, 120, C::Ink::Text, 154);
            // The GB inn arrow's black tip row sits one pixel above the choice midpoint.
            choices(c, w, w.ram[0xD841], TextKey::draw_inn_screen_4, TextKey::draw_inn_screen_5, 2,
                    second ? 1 : 0);
        } else if (scene == Scene::Revival) {
            c.window(164, 16, 60, 16);
            c.text(host_text(w, "ui_service_revive_price"), 169, 20, C::Ink::Text, 50);
            c.text(host_text(w, "ui_service_revive_question"), 5, 120, C::Ink::Text, 154);
        } else if (scene == Scene::Members) {
            c.text(menu_text(w, TextKey::draw_shop_equipment_member_screen_1), 5, 120, C::Ink::Text, 154);
        } else {
            c.text(menu_text(w, scene == Scene::Replace   ? TextKey::confirm_member_replacement_1
                                : scene == Scene::Refusal ? TextKey::replace_selected_party_member_1
                                                          : TextKey::draw_recruitment_member_screen_2),
                   5, 120, C::Ink::Text, 154);
            if (scene == Scene::Replace)
                choices(c, w, w.ram[0xD841], TextKey::draw_member_replacement_confirmation_1,
                        TextKey::draw_member_replacement_confirmation_2, 2);
        }
        return px;
    }
    Machine m(w);
    title(c, menu_text(w, TextKey::draw_shop_main_screen_2));
    party_gold(c, w);
    if (scene == Scene::Shop) {
        const auto question = menu_text(w, TextKey::draw_shop_main_screen_3);
        c.window(0, 24, std::min(224, C::text_width(question) + 12), 20);
        c.text(question, 5, 30, C::Ink::Text, 214);
        c.window(0, 44, 64, 44);
        constexpr const char *keys[] = {"ui_service_buy", "ui_service_sell", "ui_service_exit"};
        for (int i = 0; i < 3; ++i)
            c.text(host_text(w, keys[i]), 17, 50 + i * 12, C::Ink::Text, 42);
        c.cursor(6, 50 + std::clamp(int(w.ram[0xD841]), 0, 2) * 12);
        return px;
    }
    const bool sell = scene == Scene::Sell || scene == Scene::Sale;
    const bool confirm = scene == Scene::Purchase || scene == Scene::Sale;
    const int selected = sell ? std::clamp(int(w.ram[0xD845]), 0, 7)
                              : std::clamp(int(w.ram[0xD853]) + int(w.ram[0xD842]), 0, 251);
    const int count = confirm ? 4 : 5;
    const int total = sell ? 8 : std::clamp(int(w.ram[0xD854]), 1, 252);
    if (selected < first_)
        first_ = selected;
    if (selected >= first_ + count)
        first_ = selected - count + 1;
    first_ = std::clamp(first_, 0, std::max(0, total - count));
    // Four pixels below the last row keep the more marker clear of prices and the GB inner frame.
    const int list_height = count * 12 + 14;
    c.window(0, 16, 164, list_height);
    ItemRow detail;
    for (int row = 0; row < count && first_ + row < total; ++row) {
        const int slot = first_ + row;
        const uint8_t id = sell                    ? w.ram[size_t(0xD07C + slot * 2)]
                           : w.ram[0xD852] == 0x16 ? uint8_t(slot)
                                                   : table_u8(m, Table::shops, w.ram[0xD852], slot);
        const auto item = sell ? inventory_row(w, 0, slot) : item_row(w, id, 1);
        const int y = 22 + row * 12;
        c.item(item.id, item.name, 17, y, C::Ink::Text, 114);
        // The selected sale offer is calculated by native code, including remaining uses.
        const auto value = sell && slot == selected && confirm ? amount(w, 0xD84C)
                           : sell ? sale_price(m, id, w.ram[size_t(0xD07D + slot * 2)])
                                  : price(m, id);
        const auto digits = std::to_string(value);
        if (sell && w.ram[size_t(0xD07D + slot * 2)] == 255)
            continue;
        c.numerals(digits, 156 - C::numerals_width(digits), y + 1);
        if (slot == selected)
            detail = item;
    }
    if (first_ > 0)
        c.more_above(152, 19);
    if (first_ + count < total)
        c.more_below(152, 16 + list_height - 7);
    if (message < 0 && !confirm)
        c.cursor(6, 22 + (selected - first_) * 12, true, 8);
    if (message >= 0) {
        notice(96, 48);
    } else if (confirm) {
        c.window(0, 96, 164, 48);
        c.text(sell ? host_text(w, "ui_service_sale_offer", number(amount(w, 0xD84C)))
                    : menu_text(w, TextKey::draw_shop_purchase_confirmation_1),
               5, 102, C::Ink::Text, 154);
        c.item(w.ram[sell ? 0xD07C + selected * 2 : 0xD855],
               item_row(w, w.ram[sell ? 0xD07C + selected * 2 : 0xD855], 1).name, 5, 114, C::Ink::Text, 154);
        choices(c, w, w.ram[sell ? 0xD846 : 0xD843],
                sell ? TextKey::draw_shop_sale_confirmation_2 : TextKey::draw_shop_purchase_confirmation_2,
                sell ? TextKey::draw_shop_sale_confirmation_3 : TextKey::draw_shop_purchase_confirmation_3);
    } else {
        detail_.update(w.frame_count, selected + (sell ? 256 : 0), detail.flavor, detail.stats);
        detail_.draw(c, 112 - DetailLine::kHeight); // anchored above the gold pane
    }
    return px;
}
} // namespace sawgod::game
