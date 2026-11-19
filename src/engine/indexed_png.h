// Indexed (palette) PNG images, for graphics resources: tiles extracted as editable pictures whose pixel
// values are the game's color indices (game/resources.h). Any common PNG an editor saves loads back: palette
// images of any bit depth, and grayscale or RGB(A) pixels mapped to the nearest palette entry.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sawgod {

struct IndexedImage {
    int width = 0, height = 0;
    std::vector<uint8_t> pixels; // width * height palette indices
};

// An 8-bit palette PNG; `palette` is 0xRRGGBB per index (at most 256).
std::vector<uint8_t> encode_indexed_png(const IndexedImage &image, const std::vector<uint32_t> &palette);

// Decodes a PNG (not interlaced) to indices: a palette image's own indices; other color types each pixel's
// nearest entry of `palette` (by RGB distance); with `transparent` >= 0, pixels under half alpha get that
// index. False and `error` if it can't.
bool decode_indexed_png(const std::vector<uint8_t> &png, const std::vector<uint32_t> &palette,
                        IndexedImage *image, std::string *error, int transparent = -1);

// Straight-alpha authored overlays. The baker writes noninterlaced RGBA8 PNGs.
struct RgbaImage {
    int width = 0, height = 0;
    std::vector<uint32_t> pixels; // 0xAARRGGBB
};
bool decode_rgba_png(const std::vector<uint8_t> &png, RgbaImage *image, std::string *error);

} // namespace sawgod
