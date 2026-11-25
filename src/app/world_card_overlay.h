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
    void preload(); // decode on a worker after first present, before any card is needed
    void draw(SDL_Renderer *renderer, const game::WorldCardLettering &cards, emu::Wsc &w,
              const SDL_FRect &destination, const std::array<float, 3> &weights);
    void shutdown();

  private:
    std::future<void> preloading_;
    std::array<SDL_Texture *, 32> textures_{};
};
} // namespace sawgod::app
