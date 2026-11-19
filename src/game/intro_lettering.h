// English opening text on the native strip/window clock. No machine writes or input handling.
#pragma once
#include "emu/wsc.h"
#include "game/gb_intro.h"
#include <array>
namespace sawgod::game {
class IntroLettering {
  public:
    void attach(emu::Wsc &w);
    bool active(emu::Wsc &w) const;
    void composite(emu::Wsc &w, std::vector<uint32_t> &pixels, emu::Wsc::ColorMode mode) const;

  private:
    hooks::Subscription scenes_, strips_;
    uint64_t generation_ = 0;
    bool prologue_ = false;
    struct Caption {
        WrapResult lines;
        int columns = 0, total = 1;
    };
    std::array<Caption, 8> captions_;
};
} // namespace sawgod::game
