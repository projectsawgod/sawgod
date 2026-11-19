// Inventory snapshots, item selection, and host-drawn item lists.
#pragma once

#include <array>
#include <string>
#include <vector>

#include "emu/wsc.h"
#include "game/hooks.h"
#include "game/ui_contract.h"
#include "game/ui_detail.h"
#include "game/ui_items.h"

namespace sawgod::game {

// Inventory presentation. Selection bytes, item actions and targeting stay with the native menu.
class InventoryUi {
  public:
    // Keep this declaration with the screen: adding an element without test evidence fails CTest.
    static constexpr UiElement kElements[] = {{"tab.items", "activate cancel wrap refused"},
                                              {"tab.keys", "activate cancel wrap refused"},
                                              {"cursor.list", "activate cancel wrap refused"},
                                              {"cursor.source", "activate cancel wrap refused"},
                                              {"action.move", "activate cancel wrap refused"},
                                              {"action.use", "activate cancel wrap refused"},
                                              {"action.discard", "activate cancel refused"},
                                              {"row.crystals", "activate cancel wrap refused"},
                                              {"row.trash", "activate cancel wrap refused"},
                                              {"detail", "render reset overflow"},
                                              {"counts", "render empty unlimited"},
                                              {"rows.items", "activate cancel wrap refused"},
                                              {"rows.keys", "activate cancel wrap refused"},
                                              {"dialog.target", "activate cancel wrap refused"},
                                              {"dialog.message", "activate cancel"},
                                              {"dialog.refill", "activate cancel wrap refused"},
                                              {"dialog.travel", "activate cancel clamp"},
                                              {"dialog.crystals", "activate cancel wrap refused"},
                                              {"hint.crystals", "render"},
                                              {"hint.trash", "render"}};
    static constexpr int kVisibleRows = 8;
    using Row = ItemRow;
    struct View {
        int tab = 0, selected = 0, first = 0, total = 0, source = -1;
        std::array<Row, kVisibleRows> rows;
        Row detail;
        bool tabs_focused = false;
    };

    void install(emu::Wsc &w);
    void set_visible(emu::Wsc &w, bool visible);
    void set_enabled(bool enabled) { enabled_ = enabled; }
    void frame(emu::Wsc &w);
    bool active() const { return visible_ && enabled_; }
    // Only the inventory's top-level poll calls this: Up focuses tabs, Left/Right changes pages.
    uint16_t keys(emu::Wsc &w, uint16_t original);
    View view(emu::Wsc &w);
    // Reuse the visible inventory under a focused pop-up, without a competing list pointer.
    static void draw_list(Chrome &chrome, emu::Wsc &w, const View &view, bool pointers = true);
    std::vector<uint32_t> picture(emu::Wsc &w, emu::Wsc::ColorMode mode, bool second = false);

  private:
    std::vector<hooks::Subscription> subscriptions_;
    std::array<int, 2> first_{};
    bool visible_ = false, enabled_ = true, moving_ = false, tabs_focused_ = false;
    uint64_t generation_ = 0;
    DetailLine detail_;
};

} // namespace sawgod::game
