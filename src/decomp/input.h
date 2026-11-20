#pragma once
#include "engine/input.h"
#include <filesystem>

namespace sawgod::decomp {
class Input {
  public:
    Input() : map_(ui::InputMap::defaults()) {}
    ~Input();
    // Optional file; malformed entries fail with a line number instead of changing controls silently.
    void load(const std::filesystem::path &path);
    void event(const SDL_Event &event);
    uint16_t held() const;

  private:
    ui::InputMap map_;
    std::vector<SDL_Gamepad *> pads_;
};
} // namespace sawgod::decomp
