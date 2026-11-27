// Item-transfer and use-dialog snapshots and host presentation.
#pragma once

#include <array>
#include <string>
#include <vector>

#include "emu/wsc.h"
#include "game/hooks.h"
#include "game/inventory_ui.h"
#include "game/ui_detail.h"
#include "game/ui_items.h"

namespace sawgod::game {

// Item follow-up dialogs (actor target, refill, floor travel, overflow discard, Crystals) and native item
// messages (no effect, already wearing equipment), drawn as redesigned layouts over the inventory
// and Status/Equip lists. Native selectors keep every selection byte, key and action; unsubscribed, the
// original dialogs show. App: always installed; headless: --item-dialog-ui.
class ItemDialogUi {
  public:
    enum class Dialog { None, Target, Refill, Travel, Overflow, Crystals };
    enum class Message { None, Unusable, Wearing, Gain };
    // Which list the dialog or message belongs to: the inventory, or a member's Status/Equip lists (gear, or
    // the inventory reached from them).
    enum class Context { Unknown, Inventory, Equipped, Field };
    static constexpr int kListRows = 7, kShortRows = 3;
    // Compact two-by-two pop-up beside the source row; enough room for the native hands inside its frame.
    static constexpr int kCrystalX = 88, kCrystalWidth = 80, kCrystalHeight = 56;
    static constexpr int kCrystalIconX = 22, kCrystalHandX = 4, kCrystalInsetY = 8;
    static constexpr int kCrystalColumnPitch = 32, kCrystalPitch = 24;
    struct View {
        Dialog dialog = Dialog::None;
        Message message = Message::None;
        Context context = Context::Unknown;
        int tab = 0, selected = 0, first = 0, total = 0, rows = 0, source = -1;
        bool gear_tabs = false, key_items = false; // tab pair Gear | Items/Key Items (Status/Equip)
        std::array<ItemRow, InventoryUi::kVisibleRows> list;
        ItemRow detail;
        std::u32string item, notice;
        int item_id = -1; // the item being used (target title pane, travel heading)
        int target = 0, floor = 1, highest = 1;
        int crystal_choice = 0, crystals = 0, crystal_y = 0;
    };

    void install(emu::Wsc &w, InventoryUi *inventory = nullptr);
    void frame(emu::Wsc &w);
    bool active() const;
    View view(emu::Wsc &w);
    std::vector<uint32_t> picture(emu::Wsc &w, emu::Wsc::ColorMode mode, bool second = false);

  private:
    void reset();
    std::vector<hooks::Subscription> subscriptions_;
    Dialog dialog_ = Dialog::None;
    Message message_ = Message::None;
    Context context_ = Context::Unknown;
    uint64_t generation_ = 0;
    int first_ = 0, gain_key_ = 0, gain_ = 0;
    bool orb_pair_missing_ = false;
    DetailLine detail_;
    InventoryUi::View crystal_inventory_;
};

} // namespace sawgod::game
