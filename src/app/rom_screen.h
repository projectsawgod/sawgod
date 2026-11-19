// The ROM screen's picture (black, centered pixel text in Silver; the SawGod logo on top, one box
// vertically centered, "Choose File or Drag and Drop ROM": click it to open the file
// picker, or drop a file on it; an error under it; the expected ROM in the footer). Drawn into a 448 x 288
// canvas (twice the WSC screen) that the window scales by whole steps; the logo is drawn over it by the
// window at the window's own resolution (rom_screen_logo()). Separate from the window loop (first_run.cpp) so
// a headless test can render every state.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "app/rom_screen_text.h"

namespace sawgod::app {

inline constexpr int kRomScreenW = 448, kRomScreenH = 288;

struct RomScreenState {
    enum class Mode { Choose, Found, Working } mode = Mode::Choose;
    bool box_hot = false;                    // the choose/drop box hovered or focused
    bool drop_hot = false;                   // a file being dragged over the window
    int found_choice = 0;                    // Found: 0 Yes, 1 No
    const rom_text::Pair *message = nullptr; // an error under the title, or nullptr
    std::string expected_sha1;               // shown with an error
    std::string detail;                      // a short extra line (a setup error), ASCII
    float progress = 0;                      // Working
};

// Hit areas and the logo's place, in canvas pixels.
struct RomScreenRect {
    int x, y, w, h;
    bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};
RomScreenRect rom_screen_logo();
RomScreenRect rom_screen_box();
RomScreenRect rom_screen_yes();
RomScreenRect rom_screen_no();

// The canvas: 0xRRGGBB per pixel, kRomScreenW * kRomScreenH.
std::vector<uint32_t> draw_rom_screen(const RomScreenState &state);

// The logo at a given size: `argb` (0xAARRGGBB, straight alpha, sw * sh) averaged down by area to dw * dh
// (each no larger than the source), returned with premultiplied alpha.
std::vector<uint32_t> scale_rom_screen_logo(const std::vector<uint32_t> &argb, int sw, int sh, int dw,
                                            int dh);

// Silver text (engine/font_silver.inc): the width of a UTF-8 string in pixels, and drawing it.
int silver_width(const std::string &utf8);
void silver_draw(std::vector<uint32_t> &canvas, int width, int height, int x, int y, const std::string &utf8,
                 uint32_t color);

} // namespace sawgod::app
