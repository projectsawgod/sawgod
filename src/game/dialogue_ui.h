// Host dialogue layout and presentation driven by native dialogue events.
#pragma once
#include "game/hooks.h"
#include "game/ui_chrome.h"
#include "game/ui_contract.h"
#include <string>
#include <vector>

namespace sawgod::game {
// Observes typed tokens after native pacing; script flow and input remain native.
class DialogueUi {
  public:
    static constexpr UiElement kElements[] = {{"message", "activate close render"},
                                              {"advance", "activate refused render"},
                                              {"speed", "activate"},
                                              {"speedup", "activate"},
                                              {"pages", "activate clear render"},
                                              {"lines", "render overflow"},
                                              {"speaker", "render overflow"},
                                              {"names", "activate overflow"},
                                              {"numbers", "render overflow"},
                                              {"wait", "activate refused"},
                                              {"pause", "activate"},
                                              {"item-message", "activate refused render"},
                                              {"service-handoff", "activate cancel"},
                                              {"service-choice", "activate cancel wrap refused render"},
                                              {"scripted-event", "activate close"}};
    void install(emu::Wsc &w);
    void frame(emu::Wsc &w);
    // The native window is a scanline split: with its line interrupt off (a scripted wipe clears it), no
    // window is on screen even while a dialogue is open.
    bool active() const { return visible_ && (!wsc_ || wsc_->io[0xB2] & 0x10); }
    const std::u32string &speaker() const { return speaker_; }
    const std::vector<std::u32string> &lines() const { return lines_; }
    std::vector<uint32_t> picture(emu::Wsc &w, emu::Wsc::ColorMode mode, bool second = false);

  private:
    void clear();
    void glyph(char32_t code, bool speaker_colon = false);
    void typed(Machine &m, char32_t code, std::u32string_view tail);
    void next_line(Machine &m);
    void predict_speaker(emu::Wsc &w);
    std::vector<hooks::Subscription> subscriptions_;
    std::vector<std::u32string> lines_{1};
    std::u32string speaker_, wrap_line_, projected_tail_;
    bool visible_ = false, waiting_ = false, wrap_wait_ = false, skip_glyph_ = false,
         speaker_decided_ = false;
    uint64_t generation_ = 0;
    int capacity_ = 4;
    int seal_ = -1;
    uint32_t box_reads_ = 0; // Session::dialogue_reads() when the box opened or cleared
    const emu::Wsc *wsc_ = nullptr;
};
} // namespace sawgod::game
