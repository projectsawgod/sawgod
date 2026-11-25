// Presentation data only; coordinates and radius are in native game pixels.
#pragma once
#include <array>
#include <cstdint>

namespace sawgod {
struct PointLight {
    std::array<float, 4> position{}; // x, y, radius, hard falloff (electric)
    std::array<float, 4> emission{}; // RGB (0..1), intensity including fade/flicker
    std::array<float, 4> beam{};     // unit direction x/y, beam flag, fixture tile count (debug)
    int type = 0;                    // Flame, Soft Glow, Harsh Electric, Pulse, Beam; 6 spell light, 7 gloom
    std::array<float, 2> extent{};   // half-size of a rectangular run of emitting anchors
    bool operator==(const PointLight &) const = default;
};
} // namespace sawgod
