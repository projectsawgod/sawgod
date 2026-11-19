// Battle presentation snapshots and host-drawn battle windows.
#pragma once

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "emu/wsc.h"
#include "game/hooks.h"
#include "game/ui_contract.h"

namespace sawgod::game {

// Live battle presentation. Original input, combat and machine state remain authoritative.
// App: standard battle layout, always enabled; headless: --battle-ui for deterministic comparisons.
// After a victory the rewards stay in this layout (the cards and the log), and so does the meat
// menu (hooks::Event::MeatMenu): the drop, Eat/Don't, the eater and (the Config preview option on) the
// eater's before/after forms with five stats. The battle log has three rows for the newest message and the
// one before it (battle_ui.cpp).
inline constexpr int kBattleLogWidth = 216;
inline constexpr int kBattleLogLines = 3;
// 4 px top inset + three 10 px glyph/shadow rows + 1 px clear before the party divider.
// As before, the pane's lower frame extends behind the party cards: 35 px remain visible.
inline constexpr int kBattleLogTop = 77, kBattleLogHeight = 40, kBattleLogPitch = 10;
std::vector<std::u32string> battle_log_rows(std::u32string_view newest, std::u32string_view previous,
                                            size_t scroll = 0, bool localized = true);

struct BattleActionPreview {
    int item;
    std::u32string name, uses, actor, target, details;
};

class BattleUi {
  public:
    // Declared controls for the screen-8 pilot. There is one native eight-row list per
    // member; weapons, consumables, shields and abilities share it (no Item/Guard tabs).
    static constexpr UiElement kElements[] = {{"command.fight", "activate cancel"},
                                              {"command.run", "activate wrap"},
                                              {"command.root", "ignored"},
                                              {"list", "activate cancel wrap refused"},
                                              {"row.0", "activate cancel refused"},
                                              {"row.1", "activate cancel refused"},
                                              {"row.2", "activate cancel refused"},
                                              {"row.3", "activate cancel refused"},
                                              {"row.4", "activate cancel refused"},
                                              {"row.5", "activate cancel refused"},
                                              {"row.6", "activate cancel refused"},
                                              {"row.7", "activate cancel refused"},
                                              {"members", "order back skip"},
                                              {"targets.enemy", "activate cancel wrap default skip"},
                                              {"targets.ally", "activate cancel wrap default refused"},
                                              {"targets.direct", "activate"},
                                              {"uses", "render content"},
                                              {"plates", "render overflow"},
                                              {"hp.status", "render content overflow"},
                                              {"session", "reset"},
                                              {"log", "render dismiss"},
                                              {"auto-target", "activate cancel wrap effect"},
                                              {"meat.choice", "activate cancel wrap"},
                                              {"meat.members", "activate cancel wrap refused"},
                                              {"esper.confirm", "activate cancel wrap effect pause render"}};
    void install(emu::Wsc &w);
    void frame(emu::Wsc &w);
    void set_transparency(int percent);
    void set_enhanced_weight(float weight) { enhanced_weight_ = std::clamp(weight, 0.f, 1.f); }
    int enemy_target(const emu::Wsc &w) const;
    // Read-only snapshot of the pending action, present only during live enemy-target selection.
    std::optional<BattleActionPreview> action_preview(emu::Wsc &w) const;
    // Test/preview switch: off shows the original battle windows.
    void set_enabled(bool on);
    std::vector<uint32_t> picture(emu::Wsc &w, emu::Wsc::ColorMode mode, bool second = false);
    // Test/preview override. Otherwise use the session's full name, falling back to the four machine name
    // bytes.
    void set_party_name(size_t slot, std::u32string name);
    bool active() const { return active_ && (ready_ || meat_ != 0 || (rewards_ && enabled_)); }
    // Direct combat images accept palette publications; cached selectors/rewards and
    // an ending battle keep their complete retained picture until its next submission.
    bool live_palette(const emu::Wsc &w) const;
    int meat_stage() const { return meat_; }
    bool auto_target_setting() const { return auto_target_; }

  private:
    std::u32string party_name(emu::Wsc &w, int member) const;
    struct Number {
        int actor, amount;
        uint64_t frame;
    };
    std::vector<hooks::Subscription> subscriptions_;
    std::vector<Number> numbers_;
    std::array<int, 31> last_hp_{};
    std::array<bool, 31> notified_{};
    std::array<std::u32string, 4> names_;
    std::u32string message_, previous_message_;
    uint64_t message_frame_ = 0;
    bool active_ = false, ready_ = false, executing_ = false, rewards_ = false;
    bool enabled_ = true, auto_target_ = false;
    std::array<std::vector<uint32_t>, 4> selector_battlefield_;
    int meat_ = 0;           // MeatMenu stage (hooks.h); 0: not in the meat menu
    int refused_eater_ = -1; // the member A was refused for, while the eater cursor stays on it
    // The list arrow as Eat/Don't showed it: the eater choice reuses its tile for the native target cursor.
    std::array<uint8_t, 64> arrow_{};
    bool arrow_kept_ = false;
    // The battlefield as the drop announcement showed it, per picture (WSC/GB) and color mode (Raw/Lcd).
    std::array<std::vector<uint32_t>, 4> battlefield_;
    uint64_t generation_ = 0;
    int transparency_ = 30;
    float enhanced_weight_ = 0;
};

} // namespace sawgod::game
