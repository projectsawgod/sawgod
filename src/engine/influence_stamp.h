// Numerical influence sprites; no game or renderer state.
#pragma once
#include <array>
#include <cstdint>
#include <vector>
namespace sawgod {
struct InfluenceStamp {
    int side = 0, phases = 1;
    std::vector<std::array<uint16_t, 4>> pixels;
};
InfluenceStamp build_influence_stamp(const std::array<float, 6> &key);
} // namespace sawgod
