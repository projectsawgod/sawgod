// Presentation-only battle lights. Coordinates and radii are native game pixels.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace sawgod {
struct BattleLighting {
    // x center, y center, picture bottom, target gain (zero means absent).
    std::array<std::array<float, 4>, 3> groups{};
    std::array<float, 3> selection{}; // each group's share of the target selection (its outline)
    float weight = 0;
    static float strength(bool enabled, bool battle, float enhanced, float gb = 0) {
        return enabled && battle ? std::clamp(enhanced, 0.f, 1.f) * (1 - std::clamp(gb, 0.f, 1.f)) : 0.f;
    }
    static float pulse(double seconds) {
        return 1.f + 1.2f * (0.5f + 0.5f * float(std::sin(seconds * (2 * 3.141592653589793 / 1.2))));
    }
    static float gaussian(float x, float y, float rx, float ry) {
        return std::exp(-(x * x / (rx * rx) + y * y / (ry * ry)));
    }
    static float enemy_lift(float gain, float x, float y) {
        return 0.15f * (gain - 1) * std::clamp(1.6f * gaussian(x, y, 40, 48), 0.f, 1.f);
    }
    // CPU reference of the shader's enemy path at full scene exposure, including the Enhanced/option gate.
    std::array<float, 3> enemy(std::array<float, 3> color, float x, float y) const {
        if (weight <= 0)
            return color;
        auto lit = color;
        for (const auto &g : groups) {
            if (g[3] <= 0)
                continue;
            const float k = enemy_lift(g[3], x - g[0], y - g[1]);
            for (size_t c = 0; c < 3; ++c)
                lit[c] = lit[c] * (1 + k) + k * 0.25f * std::array{0.12f, 0.06f, 0.f}[c];
        }
        for (size_t c = 0; c < 3; ++c)
            color[c] += (std::clamp(lit[c], 0.f, 1.f) - color[c]) * weight;
        return color;
    }
};
// Interpolate from the current mix, so even a second move during a fade stays continuous.
// Leaving target selection clears the light immediately; the room's vignette and dimming remain.
class BattleTargetLight {
  public:
    std::array<float, 3> gains(int target, double seconds) {
        if (target < 0 || target >= 3) {
            *this = {};
            return {1, 1, 1};
        }
        if (target_ < 0 || seconds < changed_) {
            from_ = {};
            from_[size_t(target)] = 1;
            target_ = target;
            changed_ = seconds;
        }
        auto mix = weights(seconds);
        if (target != target_) {
            from_ = mix;
            target_ = target;
            changed_ = seconds;
        }
        const float strength = BattleLighting::pulse(seconds) - 1;
        return {1 + strength * mix[0], 1 + strength * mix[1], 1 + strength * mix[2]};
    }
    // Each group's share of the selection after gains(): 1 for the target, crossfading over 0.25 s.
    std::array<float, 3> weights(double seconds) const {
        if (target_ < 0)
            return {};
        const float alpha = std::clamp(float((seconds - changed_) / 0.25), 0.f, 1.f);
        auto result = from_;
        for (size_t i = 0; i < 3; ++i)
            result[i] += ((int(i) == target_ ? 1.f : 0.f) - result[i]) * alpha;
        return result;
    }

  private:
    int target_ = -1;
    double changed_ = 0;
    std::array<float, 3> from_{};
};
} // namespace sawgod
