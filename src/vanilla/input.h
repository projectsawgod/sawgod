#pragma once
#include "engine/input.h"
#include <filesystem>

namespace sawgod::vanilla {
class Input {
  public:
    Input() : map_(ui::InputMap::defaults()) {}
    ~Input();
    // Optional file; malformed entries fail with a line number instead of changing controls silently.
    void load(const std::filesystem::path &path);
    void event(const SDL_Event &event);
    uint16_t held() const;
    const ui::InputMap &mapping() const { return map_; }

  private:
    ui::InputMap map_;
    std::vector<SDL_Gamepad *> pads_;
};
} // namespace sawgod::vanilla
