// Shop, inn, and recruitment snapshots and host-drawn service panels.
#pragma once
#include "game/hooks.h"
#include "game/ui_contract.h"
#include "game/ui_detail.h"
#include <array>
#include <vector>
namespace sawgod::game {
class ServiceUi {
  public:
    static constexpr UiElement kElements[] = {{"shop.menu", "activate cancel wrap"},
                                              {"shop.buy", "activate cancel clamp refused"},
                                              {"shop.purchase", "activate cancel refused"},
                                              {"shop.sell", "activate cancel wrap refused"},
                                              {"shop.sale", "activate cancel"},
                                              {"shop.purchase.destination", "activate cancel wrap"},
                                              {"shop.sale.choice", "activate cancel wrap"},
                                              {"guild.replace.choice", "activate cancel wrap"},
                                              {"inn.choice", "activate cancel wrap refused"},
                                              {"guild.members", "activate cancel wrap refused"},
                                              {"guild.replace", "activate cancel"},
                                              {"guild.refusal", "activate cancel"},
                                              {"guild.creation", "activate"},
                                              {"shop.members", "activate cancel wrap refused"},
                                              {"revival.members", "activate cancel wrap refused"},
                                              {"title", "render overflow"},
                                              {"gold", "render overflow"},
                                              {"prices", "render overflow"},
                                              {"detail", "render reset overflow"},
                                              {"row.sell.0", "activate cancel refused"},
                                              {"row.sell.1", "activate cancel refused"},
                                              {"row.sell.2", "activate cancel refused"},
                                              {"row.sell.3", "activate cancel refused"},
                                              {"row.sell.4", "activate cancel refused"},
                                              {"row.sell.5", "activate cancel refused"},
                                              {"row.sell.6", "activate cancel refused"},
                                              {"row.sell.7", "activate cancel refused"},
                                              {"row.buy.0", "activate cancel refused"},
                                              {"row.buy.1", "activate cancel refused"},
                                              {"row.buy.2", "activate cancel refused"},
                                              {"row.buy.3", "activate cancel refused"},
                                              {"row.buy.4", "activate cancel refused"},
                                              {"row.buy.5", "activate cancel refused"},
                                              {"row.buy.6", "activate cancel refused"},
                                              {"row.buy.7", "activate cancel refused"},
                                              {"row.buy.8", "activate cancel refused"},
                                              {"row.buy.9", "activate cancel refused"},
                                              {"row.guild.0", "activate cancel refused"},
                                              {"row.guild.1", "activate cancel refused"},
                                              {"row.guild.2", "activate cancel refused"},
                                              {"row.guild.3", "activate cancel refused"}};
    enum class Scene {
        Hidden,
        Shop,
        Buy,
        Sell,
        Purchase,
        Sale,
        Members,
        Gear,
        Inn,
        Revival,
        Guild,
        Replace,
        Refusal
    };
    // ServiceMenu observation 13 keeps the current layout and replaces its footer with c (TextKey).
    static constexpr int kNotice = 13;
    void install(emu::Wsc &w);
    void frame(emu::Wsc &w);
    bool active() const { return scene() != Scene::Hidden; }
    Scene scene() const { return stack_.empty() ? Scene::Hidden : stack_.back().scene; }
    std::vector<uint32_t> picture(emu::Wsc &w, emu::Wsc::ColorMode mode, bool second = false);

  private:
    std::vector<hooks::Subscription> subscriptions_;
    struct Panel {
        Scene scene;
        int message = -1; // TextKey of the notice replacing the footer, if any
    };
    std::vector<Panel> stack_;
    std::array<std::array<std::vector<uint32_t>, 2>, 2> town_;
    uint64_t generation_ = 0;
    int first_ = 0;
    DetailLine detail_;
};
} // namespace sawgod::game
