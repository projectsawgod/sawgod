// Bounded presentation lights from connected native spell pixels. No machine state.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
namespace sawgod {
struct LightGroup {
    double weight = 0, x = 0, y = 0;
    int pixels = 0, x0 = 224, y0 = 80, x1 = 0, y1 = 0;
    float center_x() const { return float(x / weight); }
    float center_y() const { return float(y / weight); }
    void merge(const LightGroup &b) {
        weight += b.weight;
        x += b.x;
        y += b.y;
        pixels += b.pixels;
        x0 = std::min(x0, b.x0);
        y0 = std::min(y0, b.y0);
        x1 = std::max(x1, b.x1);
        y1 = std::max(y1, b.y1);
    }
    float radius(float reach) const {
        const float dx = std::max(center_x() - x0 - 0.5f, x1 + 0.5f - center_x());
        const float dy = std::max(center_y() - y0 - 0.5f, y1 + 0.5f - center_y());
        return std::ceil((std::hypot(dx, dy) + reach) / 2) * 2;
    }
};
inline std::vector<LightGroup> connected_light_groups(std::array<uint16_t, 224 * 80> weights) {
    std::array<uint16_t, 224 * 80> queue{};
    std::vector<LightGroup> groups;
    groups.reserve(9);
    for (int seed = 0; seed < 224 * 80; ++seed) {
        if (!weights[size_t(seed)])
            continue;
        LightGroup group;
        size_t head = 0, tail = 0;
        const auto visit = [&](int at) {
            const auto w = weights[size_t(at)];
            if (!w)
                return;
            const int x = at % 224, y = at / 224;
            group.weight += w;
            group.x += (x + 0.5) * w;
            group.y += (y + 0.5) * w;
            ++group.pixels;
            group.x0 = std::min(group.x0, x);
            group.x1 = std::max(group.x1, x);
            group.y0 = std::min(group.y0, y);
            group.y1 = std::max(group.y1, y);
            weights[size_t(at)] = 0;
            queue[tail++] = uint16_t(at);
        };
        visit(seed);
        while (head < tail) {
            const int at = queue[head++], x = at % 224, y = at / 224;
            if (x)
                visit(at - 1);
            if (x < 223)
                visit(at + 1);
            if (y)
                visit(at - 224);
            if (y < 79)
                visit(at + 224);
        }
        groups.push_back(group);
        // Merge nearest centers whenever a ninth group arrives. Pair search stays bounded even
        // for a checkerboard spell (8,960 isolated pixels); no all-pairs list grows with the art.
        if (groups.size() > 8) {
            size_t a = 0, b = 1;
            float nearest = INFINITY;
            for (size_t i = 0; i < groups.size(); ++i)
                for (size_t j = i + 1; j < groups.size(); ++j) {
                    const float dx = groups[i].center_x() - groups[j].center_x();
                    const float dy = groups[i].center_y() - groups[j].center_y();
                    const float d = dx * dx + dy * dy;
                    if (d < nearest) {
                        nearest = d;
                        a = i;
                        b = j;
                    }
                }
            groups[a].merge(groups[b]);
            groups.erase(groups.begin() + b);
        }
    }
    return groups;
}
} // namespace sawgod
