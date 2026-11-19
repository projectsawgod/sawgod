// The splash: the finished SawGod logo on white, in the game's window, before the game.
// Every ordinary launch shows it, after the ROM screen on the first; the mapped A button skips it.
//   1. up from black (kSplashFadeInNs), then the logo at the window's own resolution (kSplashSharpNs);
//   2. a mosaic, as the SNES's: blocks growing in steps from one window pixel to one WSC pixel
//   (kSplashPixelNs);
//      the last step held as it is (kSplashPixelCleanNs), then in the game's Screen Mode (LCD colors or the
//      IPS grid), faded in (kSplashScreenFadeNs) and held (kSplashScreenNs);
//   3. a fade to black (kSplashBlackNs). The game then starts from power-on, so the original's Squaresoft
//   logo
//      fades in from that black for its full time.
// Also the logo images both it and the ROM screen draw (assets/ui/, ours, compiled in: they show before any
// data files exist).
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <SDL3/SDL.h>

namespace sawgod::ui {
class Input;
struct InputMap;
} // namespace sawgod::ui

namespace sawgod::app {

inline constexpr uint64_t kSplashFadeInNs = 300'000'000;
inline constexpr uint64_t kSplashSharpNs = 900'000'000;
inline constexpr uint64_t kSplashPixelNs = 800'000'000;
inline constexpr int kSplashMosaicSteps = 12;
inline constexpr uint64_t kSplashPixelCleanNs = 1'000'000'000;
inline constexpr uint64_t kSplashScreenFadeNs = 400'000'000;
inline constexpr uint64_t kSplashScreenNs = 1'400'000'000;
inline constexpr uint64_t kSplashBlackNs = 500'000'000;
inline constexpr float kSplashFill =
    0.7f; // the logo's share of the window's width or height, whichever binds

// The logos' pixels, 0xAARRGGBB (straight alpha); empty (and logged) if they can't be decoded.
enum class Logo { OnBlack, OnWhite };
std::vector<uint32_t> load_logo(Logo which, int *w, int *h);

// The SG mark (assets/ui/app_icon_256.png) as the window's title-bar and taskbar icon, on Windows and Linux;
// macOS and Android take theirs from the app bundle and the launcher.
void set_window_icon(SDL_Window *window);

// Where the logo goes in a window of out_w x out_h pixels: centered, kSplashFill of it, its shape kept.
SDL_FRect splash_logo_rect(int out_w, int out_h, int logo_w, int logo_h);

// The logo at the WSC's resolution: kWidth x kHeight 0xRRGGBB, on white, placed as splash_logo_rect places
// it.
struct SplashPixels {
    static constexpr int kWidth = 224, kHeight = 144;
    std::vector<uint32_t> rgb;
};
SplashPixels splash_pixels(const std::vector<uint32_t> &logo_argb, int logo_w, int logo_h);
// Where splash_pixels puts the logo, in game pixels.
SDL_Rect splash_pixel_rect(int logo_w, int logo_h);
// The mosaic's width for the pixel logo (pixel_w game pixels wide, `scale` window pixels per game pixel) at k
// (0..1): from the logo's width on screen down to pixel_w, in kSplashMosaicSteps steps (geometric, so each
// step's blocks grow by the same factor).
int splash_mosaic_width(float k, int pixel_w, float scale);

// How the game shows its picture: its rect for an output size, and the Screen Mode (either may be empty).
struct SplashScreen {
    std::function<SDL_FRect(int, int)> picture;
    std::function<void(std::vector<uint32_t> &)> recolor; // LCD's colors on a 224 x 144 picture
    std::function<void(const SDL_FRect &)> overlay;       // IPS's grid over the picture's rect
};

// Runs the splash on `renderer`; a press of A ends it. False when the window was closed.
bool run_splash(SDL_Renderer *renderer, ui::Input &input, const ui::InputMap &map,
                const SplashScreen &screen);

} // namespace sawgod::app
