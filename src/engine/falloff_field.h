// Exact float16 distance fields for native 8x8 spell masks, independent of GPU resources.
#pragma once
#include <array>
#include <cstdint>
#include <vector>
namespace sawgod {
std::vector<std::array<uint16_t, 4>> falloff_pixels(const std::array<uint32_t, 2> &mask, float radius);
}
