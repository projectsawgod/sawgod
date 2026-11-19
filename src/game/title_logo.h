// The Enhanced title logo: the WSC title's SaGa logo at 8x, built at run time from the player's own title
// picture (nothing of the game's art ships). The strokes are the art's own pixels, each at 8x, with their
// color gradients smoothed at WSC resolution; the kanji line is re-lettered from assets/ui/title_kanji.png
// (tools/title_logo_kanji.py) over the strokes painted in under it. The strokes have a smooth one-pixel glow
// in their own colors and the lettering a drop shadow.
#pragma once
#include "engine/indexed_png.h"
#include <cstdint>
#include <vector>

namespace sawgod::game {
struct TitleLogo {
    // The canvas: native title pixels x 24..200, y 0..96, at 8x. Its last rows fade into the native picture
    // under it (the strokes run on below the logo).
    static constexpr int kX = 24, kY = 0, kWidth = 176, kHeight = 96, kScale = 8;
    // `native`: the WSC title picture at full brightness, 224x144 0xRRGGBB. Opaque except the fading rows.
    static RgbaImage build(const std::vector<uint32_t> &native);
    static const RgbaImage &kanji();
};
} // namespace sawgod::game
