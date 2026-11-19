// Save-slot snapshots, confirmation prompts, and host-drawn save panels.
#pragma once
#include "game/hooks.h"
#include "game/ui_chrome.h"
#include "game/ui_contract.h"
#include <vector>
namespace sawgod::game {
// Opaque, read-only Save/Load picture pass. Native slot mapping, selectors, writes, loading and
// Bestiary inheritance remain authoritative; nested scopes only describe their presentation.
class SaveUi {
  public:
    static constexpr UiElement kElements[] = {
        {"save.field", "activate cancel"},
        {"save.list", "activate cancel clamp"},
        {"load.list", "activate cancel clamp"},
        {"save.choice", "activate cancel wrap"},
        {"load.choice", "activate cancel wrap"},
        {"save.overwrite", "activate cancel"},
        {"save.complete", "activate cancel"},
        {"load.invalid", "activate cancel"},
        {"load.inherit", "activate cancel"},
        {"save.auto", "activate cancel refused"},
        {"save.summary", "render overflow content"},
        {"save.names", "roundtrip"},
        {"save.cursor", "render"},
        {"save.scroll", "render"},
        {"save.files", "roundtrip overwrite missing corrupt version retry"},
        {"save.autosave", "idle map moving event isolation"},
        {"save.slot.1", "activate cancel refused"},
        {"save.slot.2", "activate cancel refused"},
        {"save.slot.3", "activate cancel refused"},
        {"save.slot.4", "activate cancel refused"},
        {"save.slot.5", "activate cancel refused"},
        {"save.slot.6", "activate cancel refused"},
        {"save.slot.7", "activate cancel refused"},
        {"save.slot.8", "activate cancel refused"},
        {"save.slot.9", "activate cancel refused"},
    };
    enum class Scene { Hidden, Load, Save, LoadConfirm, SaveConfirm, Complete, Invalid, Blocked };
    void install(emu::Wsc &w);
    void frame(emu::Wsc &w);
    Scene scene() const { return stack_.empty() ? Scene::Hidden : stack_.back(); }
    bool active() const { return !creation_ && !naming_ && scene() != Scene::Hidden; }
    std::vector<uint32_t> picture(emu::Wsc &w, emu::Wsc::ColorMode mode, bool second = false);
    // Read-only summary source, including the original SRAM fallback and its backup/checksum.
    static std::vector<uint8_t> payload(emu::Wsc &w, int slot);

  private:
    std::vector<hooks::Subscription> subscriptions_;
    std::vector<Scene> stack_;
    uint64_t generation_ = 0;
    bool creation_ = false, naming_ = false;
};
} // namespace sawgod::game
