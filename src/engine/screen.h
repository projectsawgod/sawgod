// Screen Mode "LCD": the stock WonderSwan Color screen, a reflective passive-matrix (FSTN) LCD. Tuned by eye
// against photos of unmodded units: no visible pixel grid at viewing distance; what
// reads as "WSC" is the low contrast (blacks lifted to slate), pale desaturated color, a cool green-gray
// cast and smear in motion. Photos can't give exact colors (exposure, white balance), so these are looks,
// not measurements. Presentation only: the game's frames are unchanged.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace sawgod {

// Each 0x00RRGGBB pixel through the FSTN color response; `amount` 0..1 mixes it with the plain picture.
void fstn_color(std::vector<uint32_t> &rgb, float amount = 0.8f);

// The FSTN response as a per-channel scale and offset (0..1): the stock screen's black and white without its
// saturation and midtone curve, for pictures drawn on the GPU (Enhanced): color * scale + offset.
struct FstnWash {
    std::array<float, 3> scale{}, offset{};
};
FstnWash fstn_wash(float amount = 0.8f);

// Smear: `shown` keeps what the screen shows; each new frame pulls it toward `frame` (call once per game
// frame). Empty or mismatched `shown` starts from `frame`.
void fstn_persist(std::vector<uint32_t> &shown, const std::vector<uint32_t> &frame);

// Screen Mode IPS (the subpixel stripes; app/main.cpp multiplies the masks over the scaled picture).
// The strength of the 1-pixel lines for a picture `scale` output pixels per game pixel.
float ips_strength(float scale);
// The 0xAARRGGBB mask along one axis: n output pixels over `pixels` game pixels; `across`: horizontal.
// strength blends each mask toward white (Enhanced: 0.5, WSC/GB: 1).
std::vector<uint32_t> ips_mask(int n, int pixels, bool across, float strength = 1);
// The picture (w x h, 0x00RRGGBB) at an integer `scale` with the IPS masks multiplied in (screenshots).
std::vector<uint32_t> ips_picture(const std::vector<uint32_t> &rgb, int w, int h, int scale);

} // namespace sawgod
