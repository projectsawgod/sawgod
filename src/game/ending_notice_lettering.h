// English over the ending's New Game+ notice; native code owns its fades, wait and timing.
#pragma once
#include "emu/wsc.h"
#include "game/hooks.h"
#include <vector>

namespace sawgod::game {
class EndingNoticeLettering {
  public:
    void attach(emu::Wsc &w);
    bool active(emu::Wsc &w) const;
    void composite(emu::Wsc &w, std::vector<uint32_t> &pixels, emu::Wsc::ColorMode mode, bool second) const;

  private:
    std::vector<hooks::Subscription> subscriptions_;
    uint64_t generation_ = 0;
    bool shown_ = false;
};
} // namespace sawgod::game
