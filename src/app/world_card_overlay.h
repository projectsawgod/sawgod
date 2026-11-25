#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <future>
namespace sawgod::emu {
class Wsc;
}
namespace sawgod::game {
class WorldCardLettering;
}
namespace sawgod::app {
// Owns the two texture resolutions. No fonts, native writes, or independent clock.
class WorldCardOverlay {
  public:
    ~WorldCardOverlay();
    bool prepare(SDL_Renderer *renderer, int card = 0);
    void preload(); // start during setup, before the splash
    bool decoded() const;
    void wait_for_decode(); // offline capture/tests only; never the live frame loop
    void draw(SDL_Renderer *renderer, const game::WorldCardLettering &cards, emu::Wsc &w,
              const SDL_FRect &destination, const std::array<float, 3> &weights);
    void shutdown();

  private:
    std::shared_future<void> preloading_;
    std::array<SDL_Texture *, 32> textures_{};
};
} // namespace sawgod::app
