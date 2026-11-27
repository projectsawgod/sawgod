// Item-transfer and use-dialog snapshots and host presentation.
#include "game/item_dialog_ui.h"

#include <algorithm>
#include <functional>

#include "game/party_ui.h"
#include "game/session.h"
#include "game/text_render.h"
#include "game/text_tokens.h"

namespace sawgod::game {
namespace {
using C = Chrome;
using S = std::u32string;
constexpr uint16_t kSource = 0xD840, kSelection = 0xD842, kTarget = 0xD844, kFloor = 0xD84B;
constexpr uint16_t kColumn = 0xD852, kMember = 0xD853, kSourceList = 0xD854, kKeyList = 0xD855;
constexpr uint16_t kInventory = 0xD07C, kKeyItems = 0xD08C, kHighestFloor = 0xD0DA;
constexpr uint16_t kOverflowRow = 0xC818, kOverflowTop = 0xC81A; // discard_overflow_inventory_item's words

int word(const emu::Wsc &w, uint16_t at) { return w.ram[at] | w.ram[uint16_t(at + 1)] << 8; }
uint16_t gear(const emu::Wsc &w) { return uint16_t(0xD00F + std::clamp<int>(w.ram[kMember], 0, 3) * 31); }
} // namespace

void ItemDialogUi::reset() {
    dialog_ = Dialog::None;
    message_ = Message::None;
    context_ = Context::Unknown;
    first_ = gain_key_ = gain_ = 0;
    detail_.reset();
}

void ItemDialogUi::install(emu::Wsc &w, InventoryUi *inventory) {
    subscriptions_.clear();
    reset();
    generation_ = Session::of(w).generation();
    auto &h = Session::of(w).hooks();
    subscriptions_.push_back(
        h.subscribe(hooks::Event::ItemDialog, [this, &w, inventory](Machine &, const hooks::Args &a) {
            frame(w);
            dialog_ = Dialog(std::clamp(a.a, 0, 5));
            message_ = Message::None;
            if (dialog_ != Dialog::None)
                context_ = a.b == 0 ? Context::Inventory : a.b == 1 ? Context::Equipped : Context::Field;
            first_ = 0;
            detail_.reset();
            if (dialog_ == Dialog::Crystals) {
                InventoryUi fallback;
                crystal_inventory_ = inventory ? inventory->view(w) : fallback.view(w);
                crystal_inventory_.source = -1;
                crystal_inventory_.tabs_focused = false;
            }
        }));
    subscriptions_.push_back(
        h.subscribe(hooks::Event::ItemMessage, [this, &w](Machine &, const hooks::Args &a) {
            frame(w);
            message_ = Message(std::clamp(a.a, 0, 3));
            orb_pair_missing_ = message_ == Message::Unusable && a.b == 1;
            if (message_ == Message::Gain) {
                gain_key_ = a.b;
                gain_ = a.c;
            }
            detail_.reset();
        }));
    // The list a message belongs to: the last native list the player was browsing.
    subscriptions_.push_back(
        h.subscribe(hooks::Event::InventoryMenu, [this, &w](Machine &, const hooks::Args &a) {
            frame(w);
            if (a.a)
                context_ = Context::Inventory;
        }));
    subscriptions_.push_back(
        h.subscribe(hooks::Event::PartyMenu, [this, &w](Machine &, const hooks::Args &a) {
            frame(w);
            if (a.a == 3 || a.a == 5 || a.a == 6 || a.a == 9)
                context_ = Context::Equipped; // Status list and its picked row, Equip source/destination
            else if (a.a)
                context_ = Context::Unknown; // field categories and member selection have no item list
        }));
}

void ItemDialogUi::frame(emu::Wsc &w) {
    if (generation_ != Session::of(w).generation()) {
        generation_ = Session::of(w).generation();
        reset();
    }
    if (active())
        view(w); // Advance the detail line on game frames even when headless draws no picture.
}

bool ItemDialogUi::active() const {
    return dialog_ != Dialog::None || (message_ != Message::None && context_ != Context::Unknown);
}

ItemDialogUi::View ItemDialogUi::view(emu::Wsc &w) {
    View v;
    v.dialog = dialog_;
    v.message = message_;
    if (message_ == Message::Unusable)
        v.notice = orb_pair_missing_ ? host_text(w, "ui_orb_pair_missing")
                                     : key_text(w, TextKey::wait_unusable_item_message_1);
    if (message_ == Message::Gain) {
        const auto key = TextKey(gain_key_);
        const auto raw = menu_text(w, key);
        const auto tokenized = utf8_to_u32(text_tokens(kTextKeys[size_t(key)].name, name_utf8(raw)).text);
        TextValues values;
        values.set(U"amount",
                   decimal_text(unsigned(gain_), key == TextKey::show_max_hp_increase_message_1 ? 3 : 2));
        v.notice = with_wsc_glyphs(with_values(tokenized));
    }
    v.context = context_;
    const bool equipped = context_ == Context::Equipped;
    // The item being used: its slot (0xD840) in the inventory, or in the list the Status/Equip source came
    // from (0xD854: a member's gear, else the inventory).
    const uint16_t source_base =
        equipped ? (w.ram[kSourceList] ? kInventory : gear(w)) : (w.ram[kColumn] ? kKeyItems : kInventory);
    const uint16_t item = uint16_t(source_base + std::min<int>(w.ram[kSource], 8) * 2);
    const auto selected_item = item_row(w, w.ram[item], w.ram[uint16_t(item + 1)], source_base == kKeyItems);
    v.item = selected_item.name;
    v.item_id = selected_item.id;
    v.target = std::clamp<int>(w.ram[kTarget], 0, 3);
    v.floor = w.ram[kFloor];
    v.highest = w.ram[kHighestFloor];
    v.crystal_choice = std::clamp<int>(w.ram[kSource], 0, 3);
    const int progress = w.ram[0xD0AB] >> 4;
    v.crystals = progress > 0 && progress < 5 ? progress : 0;
    if (dialog_ == Dialog::Crystals) {
        v.rows = InventoryUi::kVisibleRows;
        v.tab = crystal_inventory_.tab;
        v.selected = crystal_inventory_.selected;
        v.first = crystal_inventory_.first;
        v.total = crystal_inventory_.total;
        for (int row = 0; row < v.rows; ++row)
            v.list[size_t(row)] = inventory_row(w, v.tab, v.first + row);
        v.detail = v.list[size_t(v.selected - v.first)];
        const int source_y = 26 + (v.selected - v.first) * 12;
        v.crystal_y = std::clamp(source_y - kCrystalInsetY, 24, DetailLine::kY - kCrystalHeight);
        detail_.update(w.frame_count, v.tab * 16 + v.selected, v.detail.flavor, v.detail.stats);
        return v;
    }
    if (dialog_ == Dialog::Target || (dialog_ == Dialog::None && message_ == Message::None))
        return v;
    std::function<ItemRow(int)> row;
    const bool long_list =
        message_ == Message::None && (dialog_ == Dialog::Refill || dialog_ == Dialog::Overflow);
    if (long_list) {
        // Refill and overflow choose among the eight ordinary item slots.
        v.rows = kListRows;
        v.total = 8;
        v.gear_tabs = equipped && dialog_ == Dialog::Refill;
        v.tab = v.gear_tabs ? 1 : 0;
        if (dialog_ == Dialog::Overflow)
            v.selected = word(w, kOverflowRow) + word(w, kOverflowTop);
        else
            v.selected = w.ram[equipped ? kSelection + 1 : kSelection];
        if (dialog_ == Dialog::Refill && (equipped ? w.ram[kSourceList] != 0 : !w.ram[kColumn]))
            v.source = w.ram[kSource];
        row = [&w](int slot) { return inventory_row(w, 0, slot); };
    } else if (equipped) {
        v.rows = kShortRows;
        v.gear_tabs = true;
        const bool inventory = w.ram[kColumn] != 0;
        v.key_items = inventory && w.ram[kKeyList] == 1;
        v.tab = inventory ? 1 : 0;
        v.total = v.key_items ? 9 : 8;
        v.selected = w.ram[inventory ? kSelection + 1 : kSelection];
        const uint16_t base = gear(w);
        const bool key = v.key_items;
        if (inventory)
            row = [&w, key](int slot) { return inventory_row(w, key ? 1 : 0, slot); };
        else
            row = [&w, base](int slot) {
                return item_row(w, w.ram[uint16_t(base + slot * 2)], w.ram[uint16_t(base + slot * 2 + 1)]);
            };
    } else {
        v.rows = kShortRows;
        v.tab = w.ram[kColumn] ? 1 : 0;
        v.total = v.tab ? 9 : 10;
        v.selected = w.ram[kSelection + v.tab];
        const int tab = v.tab;
        row = [&w, tab](int slot) { return inventory_row(w, tab, slot); };
    }
    v.selected = std::clamp(v.selected, 0, v.total - 1);
    if (long_list) {
        if (v.selected < first_)
            first_ = v.selected;
        if (v.selected >= first_ + v.rows)
            first_ = v.selected - v.rows + 1;
        v.first = first_ = std::clamp(first_, 0, v.total - v.rows);
    } else {
        // The selection does not move while a message or the floor choice is up: keep it in the middle.
        v.first = std::clamp(v.selected - 1, 0, v.total - v.rows);
    }
    for (int i = 0; i < v.rows; ++i)
        v.list[size_t(i)] = row(v.first + i);
    v.detail = v.list[size_t(v.selected - v.first)];
    if (!long_list)
        detail_.update(w.frame_count, v.tab * 16 + v.selected, v.detail.flavor, v.detail.stats);
    return v;
}

std::vector<uint32_t> ItemDialogUi::picture(emu::Wsc &w, emu::Wsc::ColorMode mode, bool second) {
    frame(w);
    auto px = w.framebuffer_rgb(mode, second);
    if (!active())
        return px;
    C c(w, px, mode, second);
    if (!c.ready())
        return px;
    const auto v = view(w);
    if (v.dialog == Dialog::Crystals) {
        auto background = crystal_inventory_;
        background.rows = v.list;
        InventoryUi::draw_list(c, w, background, false);
        c.window(kCrystalX, v.crystal_y, kCrystalWidth, kCrystalHeight);
        // Bottom-left is direction 3, bottom-right direction 2. Index zero stays see-through.
        constexpr int directions[] = {0, 1, 3, 2};
        Machine m(w);
        for (int slot = 0; slot < v.crystals; ++slot) {
            const int x = kCrystalX + kCrystalIconX + (slot % 2) * kCrystalColumnPitch;
            const int y = v.crystal_y + kCrystalInsetY + (slot / 2) * kCrystalPitch;
            for (int tile = 0; tile < 4; ++tile)
                c.tile(m.rd16(0x9000, uint16_t(0x3F46 + directions[slot] * 8 + tile * 2)), x + (tile % 2) * 8,
                       y + (tile / 2) * 8);
        }
        c.hand(kCrystalX + kCrystalHandX + (v.crystal_choice % 2) * kCrystalColumnPitch,
               v.crystal_y + kCrystalInsetY + (v.crystal_choice / 2) * kCrystalPitch);
        detail_.draw(c);
        return px;
    }
    const auto message = v.message == Message::Unusable ? v.notice
                         : v.message == Message::Wearing
                             ? key_text(w, TextKey::wait_equipment_transfer_error_1)
                             : v.notice;
    // Full-screen menus, as the native ones they replace; the opaque fill also backs the GB's corners.
    c.window(0, 0, 224, 144);
    if (v.dialog == Dialog::Target) {
        const auto title = host_text(w, "ui_item_target");
        c.window(0, 0, std::min(224, C::text_width(title) + 12), 16);
        c.text(title, 5, 5, C::Ink::Text, 212);
        for (int i = 0; i < 4; ++i)
            draw_party_row(c, w, i, 16 + i * kPartyRowHeight, i == v.target, second);
        c.window(0, kPartyFooterY, 224, kPartyFooterHeight);
        if (v.message == Message::None)
            c.item(v.item_id, v.item, 5, 120, C::Ink::Text, 212);
        else
            c.text(message, 5, 120, C::Ink::Text, 212);
        return px;
    }
    c.tabbed_window(0, 0, 224, v.rows == kListRows ? 128 : 64, 20, 112, v.tab);
    if (v.gear_tabs) {
        const auto left = host_text(w, "ui_party_gear");
        const auto right_label = host_text(w, v.key_items ? "ui_inventory_keys" : "ui_inventory_items");
        c.text(left, (112 - C::text_width(left)) / 2, 7, v.tab == 0 ? C::Ink::Text : C::Ink::Muted, 100);
        c.text(right_label, 112 + (112 - C::text_width(right_label)) / 2, 7,
               v.tab == 1 ? C::Ink::Text : C::Ink::Muted, 100);
    } else {
        c.text(host_text(w, "ui_inventory_items"), 42, 7, v.tab == 0 ? C::Ink::Text : C::Ink::Muted, 65);
        c.text(host_text(w, "ui_inventory_keys"), 141, 7, v.tab == 1 ? C::Ink::Text : C::Ink::Muted, 63);
    }
    for (int i = 0; i < v.rows; ++i) {
        const auto &r = v.list[size_t(i)];
        const int slot = v.first + i, y = 26 + i * 12;
        c.item(r.id, r.name, 17, y,
               slot == v.selected ? C::Ink::Text
               : slot == v.source ? C::Ink::Accent
                                  : C::Ink::Muted,
               170);
        if (slot == v.source && slot != v.selected)
            c.cursor(6, y, true, 8);
        // Compact lists have no gutter below the last count: leave room beside it for the scroll mark.
        c.text(r.count, 206 - C::text_width(r.count), y, C::Ink::Text, 24);
    }
    const int list_bottom = v.message != Message::None ? 60 : v.rows == kListRows ? 112 : 64;
    if (v.first > 0)
        c.more_above(v.tab ? 212 : 100, 20);
    if (v.first + v.rows < v.total)
        c.more_below(212, list_bottom - 8);
    c.cursor(6, 26 + (v.selected - v.first) * 12, true, 8);
    if (v.message != Message::None) {
        // The message covers the list's lower frame and meets the detail line: no list frame or empty strip
        // shows above or below it.
        c.window(0, 60, 224, DetailLine::kY - 60);
        c.text(message, 5, 70, C::Ink::Text, 212);
        detail_.draw(c);
        return px;
    }
    if (v.dialog == Dialog::Refill || v.dialog == Dialog::Overflow) {
        c.window(0, 112, 224, 32);
        if (v.dialog == Dialog::Refill) {
            c.text(host_text(w, "ui_item_refill_hint"), 5, 118, C::Ink::Text, 212);
        } else {
            c.text(key_text(w, TextKey::discard_overflow_inventory_item_1), 5, 118, C::Ink::Text, 212);
            c.text(key_text(w, TextKey::discard_overflow_inventory_item_2), 5, 130, C::Ink::Text, 212);
        }
        return px;
    }
    // Travel: the item's name over the floor choice; arrows where the native clamp still allows a move.
    c.window(0, 64, 224, 16);
    c.item(v.item_id, v.item, 5, 68, C::Ink::Text, 212);
    c.window(0, 80, 96, 34); // the two rows freed by the shared detail pane
    c.text(host_text(w, "ui_item_floor", utf8_to_u32(std::to_string(v.floor))), 17, 93, C::Ink::Text, 68);
    if (v.floor < v.highest)
        c.tile(uint16_t((15 << 9) | 0x1F0), 6, 85);
    if (v.floor > 1)
        c.tile(uint16_t((15 << 9) | 0x1F0 | 0x8000), 6, 97);
    detail_.draw(c);
    return px;
}

} // namespace sawgod::game
