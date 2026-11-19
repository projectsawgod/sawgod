#include "engine/influence_stamp.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
namespace sawgod {
namespace {
float smooth(float a, float b, float v) {
    const float t = std::clamp((v - a) / (b - a), 0.f, 1.f);
    return t * t * (3 - 2 * t);
}
struct Axis {
    std::vector<float> values;
    std::vector<size_t> index;
    Axis(int side, float radius, float extent) {
        std::vector<float> distance(size_t(side), 0);
        for (int i = 0; i < side; ++i) {
            const float u = (i + 0.5f) / side * 2 - 1;
            const float p = u * (radius + extent);
            distance[size_t(i)] = std::max(std::abs(p) - extent, 0.f);
        }
        values = distance;
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());
        index.reserve(size_t(side));
        for (float d : distance)
            index.push_back(size_t(std::lower_bound(values.begin(), values.end(), d) - values.begin()));
    }
};
InfluenceStamp regular_stamp(const std::array<float, 6> &key, int side) {
    const int type = int(key[0]);
    const float radius = key[1];
    const Axis x(side, radius, key[2]), y(side, radius, key[3]);
    const int width = int(x.values.size()), height = int(y.values.size());
    // A run has many identical distances inside its rectangle and along each edge. Evaluate and
    // convert each distinct pair once, retaining the original float arguments and operation order.
    std::vector<std::array<float, 4>> values(size_t(width) * height);
    for (int row = 0; row < height; ++row)
        for (int col = 0; col < width; ++col) {
            const float distance = std::hypot(x.values[size_t(col)], y.values[size_t(row)]);
            const float d = distance / radius;
            float amount = type >= 6 ? std::exp(-d * d * 3) * (1 - smooth(0.6f, 1, d))
                                     : std::exp(-d * d * 10) * (1 - smooth(0.5f, 1, d));
            if (type == 2)
                amount *= 1 - smooth(0.28f, 0.5f, d) * 0.25f;
            if (type == 3)
                amount *= 0.2f * (0.08f + 0.92f * smooth(2, 9, distance));
            if (distance >= radius)
                amount = 0;
            values[size_t(row * width + col)] = {amount, 0, 0, 0};
        }
    std::vector<std::array<uint16_t, 4>> half(values.size());
    if (!SDL_ConvertPixels(width, height, SDL_PIXELFORMAT_RGBA128_FLOAT, values.data(), width * 16,
                           SDL_PIXELFORMAT_RGBA64_FLOAT, half.data(), width * 8))
        return {};
    InfluenceStamp out{side, 1, {}};
    out.pixels.resize(size_t(side) * side);
    for (int row = 0; row < side; ++row)
        for (int col = 0; col < side; ++col)
            out.pixels[size_t(row * side + col)] =
                half[y.index[size_t(row)] * size_t(width) + x.index[size_t(col)]];
    return out;
}
} // namespace
InfluenceStamp build_influence_stamp(const std::array<float, 6> &key) {
    const int type = int(key[0]);
    const bool beam = type == 4 && key[4] > 1.5f;
    const int side = type < 0 ? 32 : std::max(8, int(std::ceil(2 * (key[1] + std::max(key[2], key[3])))));
    const int phases = beam ? 16 : 1;
    if (type >= 0 && type != 4)
        return regular_stamp(key, side);
    std::vector<std::array<float, 4>> pixels(size_t(side) * side * phases);
    const auto smooth = [](float a, float b, float v) {
        const float t = std::clamp((v - a) / (b - a), 0.f, 1.f);
        return t * t * (3 - 2 * t);
    };
    for (int phase = 0; phase < phases; ++phase)
        for (int y = 0; y < side; ++y)
            for (int x = 0; x < side; ++x) {
                const float u = (x + 0.5f) / side * 2 - 1, v = (y + 0.5f) / side * 2 - 1;
                float amount = 0;
                if (type < 0) {
                    const float r = (u * u + v * v) * 2.5f;
                    amount = key[1] > 0 ? 0.45f * (1 - smooth(0.55f, 1, std::sqrt(r)))
                                        : 0.6875f * std::exp(-r * 1.7f) * (1 - smooth(1, 2.5f, r));
                } else {
                    const float radius = key[1], px = u * (radius + key[2]), py = v * (radius + key[3]);
                    const float distance = std::hypot(std::max(std::abs(px) - key[2], 0.f),
                                                      std::max(std::abs(py) - key[3], 0.f));
                    const float d = distance / radius;
                    amount = type >= 6 ? std::exp(-d * d * 3) * (1 - smooth(0.6f, 1, d))
                                       : std::exp(-d * d * 10) * (1 - smooth(0.5f, 1, d));
                    if (type == 2)
                        amount *= 1 - smooth(0.28f, 0.5f, d) * 0.25f;
                    if (type == 3)
                        amount *= 0.2f * (0.08f + 0.92f * smooth(2, 9, distance));
                    if (type == 4) {
                        const float along = -py, across = px,
                                    width = key[4] == 1 ? 5 : 3 + std::max(along, 0.f) * 0.18f;
                        const float fade = 1 - smooth(0, radius, along);
                        amount = along >= 0 ? fade * std::exp(-across * across / (width * width) * 2) *
                                                  (1 - smooth(width, width * 1.8f, std::abs(across)))
                                            : 0;
                        if (beam)
                            for (int mote = 0; mote < 3; ++mote) {
                                const float f = std::fmod(float(phase) / phases + mote / 3.f, 1.f),
                                            a = (0.1f + f * 0.75f) * radius, w = 3 + a * 0.18f;
                                const float sideways = std::sin(f * 6.2831853f + mote * 2.1f) * w * 0.55f;
                                const float xx = along - a, yy = across - sideways;
                                if (along >= 0)
                                    amount += fade * std::exp(-(xx * xx + yy * yy) / 0.7f) *
                                              std::sin(f * 3.1415927f) * 0.7f;
                            }
                    }
                    if (distance >= radius)
                        amount = 0;
                }
                pixels[size_t((phase * side + y) * side + x)] = {amount, 0, 0, 0};
            }
    std::vector<std::array<uint16_t, 4>> half(pixels.size());
    if (!SDL_ConvertPixels(side, side * phases, SDL_PIXELFORMAT_RGBA128_FLOAT, pixels.data(), side * 16,
                           SDL_PIXELFORMAT_RGBA64_FLOAT, half.data(), side * 8))
        return {};
    return {side, phases, std::move(half)};
}
} // namespace sawgod
