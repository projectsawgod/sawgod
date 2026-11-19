// English lettering over the native title menu; native code owns rows, cursor, and timing.
#pragma once
#include "emu/wsc.h"
#include "engine/settings.h"
#include "game/hooks.h"
#include <optional>
#include <string>

namespace sawgod::game {
class TitleMenuLettering {
  public:
    struct StepHint {
        std::optional<FidelityMode> mode;
        std::string down, up; // bound control tokens, as in Options help
    };
    void attach(emu::Wsc &w);
    // The title menu or PUSH ANY BUTTON is up (the logo is on screen).
    bool active(emu::Wsc &w) const;
    void composite(emu::Wsc &w, std::vector<uint32_t> &pixels, emu::Wsc::ColorMode mode, bool second,
                   const StepHint &hint = {}) const;

  private:
    hooks::Subscription subscription_;
    uint64_t generation_ = 0;
    int rows_ = 0;
    bool prompt_ = false;     // PUSH ANY BUTTON waits
    uint64_t pulse_from_ = 0; // frame PUSH ANY BUTTON was drawn, where its pulse starts
};
} // namespace sawgod::game
