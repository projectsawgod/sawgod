#pragma once
#include "engine/indexed_png.h"
#include <SDL3/SDL.h>
#include <cstdint>
#include <future>
#include <vector>
namespace sawgod::app {
// The Enhanced title logo (game/title_logo.h) over the title picture. It is built once from the title picture
// the first time the logo holds still at full brightness, then follows the title's fades.
class TitleLogoOverlay {
  public:
    ~TitleLogoOverlay();
    // Every presented frame: whether the title is up, and the WSC title picture (224x144 0xRRGGBB).
    // `settled`: a single still (a screenshot) shows the settled logo, not its first instant.
    void observe(bool title, const std::vector<uint32_t> &native, uint64_t now_ns, bool settled = false);
    void draw(SDL_Renderer *renderer, const SDL_FRect &destination, float enhanced_weight);
    // Creates the logo's texture before the game loop; draw() fills it a band of rows per frame.
    bool prepare(SDL_Renderer *renderer);
    bool reset_renderer(SDL_Renderer *renderer);
    // The WSC logo's hold is over: the Enhanced title is showing.
    bool ready() const { return ready_; }
    void shutdown();

  private:
    RgbaImage image_;
    std::future<RgbaImage> building_;
    SDL_Texture *texture_ = nullptr;
    int uploaded_rows_ = 0;
    std::vector<uint32_t> last_;
    int still_ = 0;
    size_t reference_ = 0; // the logo's brightest pixel, to follow the title's fades
    float reference_level_ = 0, brightness_ = 1, opacity_ = 0, held_ = 0;
    bool ready_ = false;
    uint64_t last_ns_ = 0;
    bool title_ = false, settled_ = false;
};
} // namespace sawgod::app
