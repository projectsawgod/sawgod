// Inventory snapshots, item selection, and host-drawn item lists.
#include "game/inventory_ui.h"
#include "game/text_tokens.h"

#include <algorithm>

#include "game/session.h"
#include "game/ui_chrome.h"

namespace sawgod::game {
namespace {
constexpr uint16_t kColumn = 0xD852, kSelection = 0xD842, kSource = 0xD840;
} // namespace

void InventoryUi::install(emu::Wsc &w) {
    subscriptions_.clear();
    visible_ = moving_ = tabs_focused_ = false;
    first_.fill(0);
    detail_.reset();
    auto &h = Session::of(w).hooks();
    subscriptions_.push_back(h.subscribe(hooks::Event::InventoryMenu, [this, &w](Machine &, const auto &a) {
        set_visible(w, a.a != 0);
        moving_ = a.a == 2;
        if (moving_)
            tabs_focused_ = false;
    }));
    subscriptions_.push_back(
        h.subscribe(hooks::Value::InventoryKeys,
                    [this, &w](Machine &, int32_t value, const auto &) { return keys(w, uint16_t(value)); }));
}
void InventoryUi::set_visible(emu::Wsc &w, bool visible) {
    const auto generation = Session::of(w).generation();
    if (generation_ != generation)
        first_.fill(0);
    if (generation_ != generation || !visible || !visible_)
        tabs_focused_ = false;
    if (generation_ != generation || !visible || !visible_)
        detail_.reset();
    generation_ = generation;
    visible_ = visible;
    moving_ = false;
}
void InventoryUi::frame(emu::Wsc &w) {
    if (generation_ != Session::of(w).generation()) {
        visible_ = moving_ = tabs_focused_ = false;
        first_.fill(0);
        detail_.reset();
    }
    if (active())
        view(w); // Advance on game frames even when headless is not drawing a picture.
}
uint16_t InventoryUi::keys(emu::Wsc &w, uint16_t original) {
    frame(w);
    if (!active() || moving_)
        return original;
    original = uint16_t(original & ~0xA00); // Inventory pages use Left/Right only.
    if (tabs_focused_) {
        // Confirm, Back or Down returns to the remembered row; none activates an item or exits the screen.
        if (original & (4 | 8 | 0x40)) {
            tabs_focused_ = false;
            return 0;
        }
        return uint16_t(original & (0x20 | 0x80));
    }
    const int tab = w.ram[kColumn] ? 1 : 0;
    if (!(original & (4 | 8 | 0x40)) && (original & 0x10) && !w.ram[kSelection + tab]) {
        tabs_focused_ = true;
        return 0;
    }
    return original;
}
InventoryUi::View InventoryUi::view(emu::Wsc &w) {
    View snapshot;
    snapshot.tab = w.ram[kColumn] ? 1 : 0;
    snapshot.total = snapshot.tab ? 9 : 10;
    snapshot.tabs_focused = tabs_focused_;
    snapshot.source = moving_ ? int(w.ram[kSource]) : -1;
    snapshot.selected = std::clamp(int(w.ram[kSelection + snapshot.tab]), 0, snapshot.total - 1);
    auto &first = first_[size_t(snapshot.tab)];
    if (snapshot.selected < first)
        first = snapshot.selected;
    if (snapshot.selected >= first + kVisibleRows)
        first = snapshot.selected - kVisibleRows + 1;
    first = std::clamp(first, 0, snapshot.total - kVisibleRows);
    snapshot.first = first;
    for (int i = 0; i < kVisibleRows; ++i)
        snapshot.rows[size_t(i)] = inventory_row(w, snapshot.tab, first + i);
    snapshot.detail = snapshot.rows[size_t(snapshot.selected - first)];
    detail_.update(w.frame_count, snapshot.tab * 16 + snapshot.selected, snapshot.detail.flavor,
                   snapshot.detail.stats);
    return snapshot;
}
std::vector<uint32_t> InventoryUi::picture(emu::Wsc &w, emu::Wsc::ColorMode mode, bool second) {
    frame(w);
    auto px = w.framebuffer_rgb(mode, second);
    if (!active())
        return px;
    Chrome c(w, px, mode, second);
    if (!c.ready())
        return px;
    const auto snapshot = view(w);
    draw_list(c, w, snapshot);
    detail_.draw(c);
    return px;
}
void InventoryUi::draw_list(Chrome &c, emu::Wsc &w, const View &snapshot, bool pointers) {
    c.tabbed_window(0, 0, 224, DetailLine::kY, 20, 112, snapshot.tab);
    using Ink = Chrome::Ink;
    c.text(host_text(w, "ui_inventory_items"), 42, 7, snapshot.tab == 0 ? Ink::Text : Ink::Muted, 65);
    c.text(host_text(w, "ui_inventory_keys"), 141, 7, snapshot.tab == 1 ? Ink::Text : Ink::Muted, 63);
    for (int i = 0; i < kVisibleRows; ++i) {
        const auto &r = snapshot.rows[size_t(i)];
        const int slot = snapshot.first + i;
        c.item(r.id, r.name, 17, 26 + i * 12,
               slot == snapshot.selected && !snapshot.tabs_focused
                   ? (snapshot.tab == 1 && is_kings_gear(r.id) ? Ink::KeyItem : Ink::Text)
               : slot == snapshot.source
                   ? (snapshot.tab == 1 && is_kings_gear(r.id) ? Ink::KeyItem : Ink::Accent)
                   : Ink::Muted,
               170);
        if (pointers && slot == snapshot.source && slot != snapshot.selected)
            c.cursor(6, 26 + i * 12, true, 8);
        c.text(r.count, 216 - Chrome::text_width(r.count), 26 + i * 12, Ink::Text, 24);
    }
    if (snapshot.first > 0)
        c.more_above(snapshot.tab ? 212 : 100, 20); // under the open tab, clear of the closed tab's frame
    if (snapshot.first + kVisibleRows < snapshot.total)
        c.more_below(212, DetailLine::kY - 8);
    if (!pointers)
        return;
    if (snapshot.tabs_focused)
        c.cursor(snapshot.tab == 0 ? 29 : 128, 7);
    else
        c.cursor(6, 26 + (snapshot.selected - snapshot.first) * 12, true,
                 8); // at the row's y: its tip is level with the row's middle
}
} // namespace sawgod::game
