// Fit the native channel operations to latched colors; no clock or machine writes.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace sawgod {
class PaletteFadeFit {
  public:
    void add(uint16_t target, uint16_t shown) {
        any_ = true;
        for (unsigned shift : {0u, 4u, 8u}) {
            const int f = target >> shift & 15, v = shown >> shift & 15;
            if (v == 0)
                hi_ = std::min(hi_, -f);
            else if (v == 15)
                lo_ = std::max(lo_, 15 - f);
            else {
                lo_ = std::max(lo_, v - f);
                hi_ = std::min(hi_, v - f);
            }
            if (v != f) {
                cap_ &= v < f && (cap_level_ < 0 || cap_level_ == v);
                floor_ &= v > f && (floor_level_ < 0 || floor_level_ == v);
                cap_level_ = floor_level_ = v;
            } else {
                unchanged_min_ = std::min(unchanged_min_, f);
                unchanged_max_ = std::max(unchanged_max_, f);
            }
        }
    }
    std::array<float, 2> range() const {
        if (!any_)
            return {0, 1};
        if (lo_ <= hi_) {
            const int k = lo_ > 0 ? lo_ : hi_ < 0 ? hi_ : 0;
            return {float(std::max(0, k)) / 15, float(std::min(15, 15 + k)) / 15};
        }
        if (cap_ && cap_level_ >= unchanged_max_ && cap_level_ >= 0)
            return {0, float(cap_level_) / 15};
        if (floor_ && floor_level_ <= unchanged_min_ && floor_level_ >= 0)
            return {float(floor_level_) / 15, 1};
        return {0, 1}; // palette animation/recolor, rather than a common fade
    }

  private:
    bool any_ = false, cap_ = true, floor_ = true;
    int lo_ = -15, hi_ = 15, cap_level_ = -1, floor_level_ = -1;
    int unchanged_min_ = 15, unchanged_max_ = 0;
};
} // namespace sawgod
