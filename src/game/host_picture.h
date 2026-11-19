// Composes host-rendered menus and dialogue over the current game picture.
#pragma once
#include "emu/wsc.h"
#include "game/ending_notice_lettering.h"
#include "game/hooks.h"
#include "game/intro_lettering.h"
#include "game/title_menu_lettering.h"
#include "game/world_card_lettering.h"
#include <array>
#include <vector>

namespace sawgod::game {
class BattleUi;
class InventoryUi;
class ItemDialogUi;
class PartyUi;
class CreationUi;
class BestiaryUi;
class DialogueUi;
class ServiceUi;
class SaveUi;

// The only host/native picture handoff. Native code emits observations; this object
// owns retained pixels. It never writes native RAM, ports, CPU state, or input.
class HostPicture {
  public:
    struct Screens {
        BattleUi *battle = nullptr;
        InventoryUi *inventory = nullptr;
        ItemDialogUi *item = nullptr;
        PartyUi *party = nullptr;
        CreationUi *creation = nullptr;
        BestiaryUi *bestiary = nullptr;
        DialogueUi *dialogue = nullptr;
        ServiceUi *service = nullptr;
        SaveUi *save = nullptr;
    };
    void install(emu::Wsc &w, Screens screens);
    // Call after the GB shadow has followed the frame, even when not drawing.
    void frame(emu::Wsc &w);
    std::vector<uint32_t> picture(emu::Wsc &w, emu::Wsc::ColorMode mode, bool second = false,
                                  bool lettering = true);
    // One collection may request both endpoints without recomposing either. Do not advance the
    // machine or change host settings between its requests; ordinary picture() calls stay fresh.
    class Collection {
      public:
        explicit Collection(HostPicture &owner) : owner_(owner) {}
        std::vector<uint32_t> picture(emu::Wsc &w, emu::Wsc::ColorMode mode, bool second = false,
                                      bool lettering = true) {
            return owner_.picture_impl(w, mode, second, lettering, &retained_);
        }

      private:
        HostPicture &owner_;
        std::array<bool, 4> retained_{};
    };
    Collection collect() { return Collection(*this); }
    // Read on every composition, so stepping, device changes, and rebinding appear immediately.
    void set_title_step_hint(std::function<TitleMenuLettering::StepHint()> source) {
        title_step_hint_ = std::move(source);
    }
    const WorldCardLettering &world_cards() const { return world_cards_; }
    const TitleMenuLettering &title_menu() const { return title_menu_; }
    // A native palette-controlled picture is advancing (or deliberately black), even
    // when its underlying layout is retained until the next complete submission.
    bool holding() const { return holding_ && !fade_peak_; }
    bool native_battle_effect() const { return defeat_effects_ != 0; }
    bool full_ui() const { return shown_ >= 2 && shown_ != 5; }
    // These compositor owners supply the whole native picture, even with a field still loaded.
    bool native_full_screen(emu::Wsc &w) const {
        return world_cards_.active(w) || title_menu_.active(w) || ending_notice_.active(w) ||
               intro_.active(w);
    }
    int shown() const { return shown_; }

  private:
    std::vector<uint32_t> picture_impl(emu::Wsc &w, emu::Wsc::ColorMode mode, bool second, bool lettering,
                                       std::array<bool, 4> *retained);
    int owner() const;
    void refresh_battle_scene(emu::Wsc &w, emu::Wsc::ColorMode mode, size_t slot);
    std::vector<uint32_t> compose(emu::Wsc &w, emu::Wsc::ColorMode mode, bool second, int next);
    void invalidate(emu::Wsc &w, bool transition = false);
    void sync_generation(emu::Wsc &w);
    std::vector<uint32_t> fade_picture(emu::Wsc &w, std::vector<uint32_t> px, bool host);
    void warp_picture(emu::Wsc &w, std::vector<uint32_t> &px);
    WorldCardLettering world_cards_;
    TitleMenuLettering title_menu_;
    std::function<TitleMenuLettering::StepHint()> title_step_hint_;
    EndingNoticeLettering ending_notice_;
    IntroLettering intro_;
    Screens screens_;
    std::vector<hooks::Subscription> subscriptions_;
    std::array<int, size_t(hooks::Event::Count)> scenes_{};
    std::array<std::vector<uint32_t>, 4> last_;
    std::array<std::vector<emu::RasterPixel>, 4> native_scene_;
    std::array<std::vector<uint32_t>, 4> native_colors_;
    std::array<uint64_t, 4> composed_{}; // the machine frame each was last composed on
    std::array<std::array<uint8_t, emu::Wsc::kWidth * emu::Wsc::kHeight>, 4> masks_{};
    std::array<emu::Wsc::UiFill, 4> fills_;
    std::array<emu::Wsc::UiText, 4> texts_;
    uint64_t generation_ = 0, fade_epoch_ = 0;
    int fades_ = 0, shown_ = 0, defeat_effects_ = 0, warps_ = 0;
    int fade_peak_ = 0; // native palette brightness at fade start; survives the black wait
    bool armed_ = false, native_ready_ = false, holding_ = false;
};
} // namespace sawgod::game
