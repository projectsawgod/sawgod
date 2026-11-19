#include "engine/falloff_field.h"
#include "engine/presentation_diagnostics.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <map>
namespace sawgod {
namespace {
struct Kernel {
    int side;
    std::vector<float> row_distance;
    std::vector<uint8_t> nearest;
    std::vector<std::array<float, 8>> vertical;
    std::vector<std::array<uint16_t, 4>> pixels;
    explicit Kernel(float radius) {
        const int halo = int(std::ceil(radius)) + 1;
        side = (8 + 2 * halo) * 3;
        row_distance.resize(size_t(256 * side), 1e20f);
        nearest.resize(row_distance.size());
        vertical.resize(size_t(side));
        for (int x = 0; x < side; ++x) {
            const float px = (float(x) + 0.5f) / 3 - halo;
            for (int col = 0; col < 8; ++col) {
                const float dx = px - (col + 0.5f);
                vertical[size_t(x)][size_t(col)] = dx * dx;
            }
            for (int byte = 1; byte < 256; ++byte)
                for (int col = 0; col < 8; ++col) {
                    if (!(byte & (1 << col)))
                        continue;
                    const size_t at = size_t(byte * side + x);
                    if (vertical[size_t(x)][size_t(col)] < row_distance[at]) {
                        row_distance[at] = vertical[size_t(x)][size_t(col)];
                        nearest[at] = uint8_t(col);
                    }
                }
        }
        // Every result is the falloff of one nearest emitting center. Cache the exact original
        // float operations and SDL conversion for all 64 centers; masks only select among them.
        const size_t area = size_t(side) * side;
        std::vector<std::array<float, 4>> values(area * 64);
        for (int bit = 0; bit < 64; ++bit)
            for (int y = 0; y < side; ++y)
                for (int x = 0; x < side; ++x) {
                    const float d =
                        vertical[size_t(x)][size_t(bit % 8)] + vertical[size_t(y)][size_t(bit / 8)];
                    const float normalized = std::sqrt(d) / radius;
                    const float t = std::clamp((normalized - 0.6f) / 0.4f, 0.f, 1.f);
                    const float f = std::exp(-normalized * normalized * 3) * (1 - t * t * (3 - 2 * t));
                    values[size_t(bit) * area + size_t(y * side + x)] = {f, 0, 0, 0};
                }
        pixels.resize(values.size());
        if (!SDL_ConvertPixels(side, side * 64, SDL_PIXELFORMAT_RGBA128_FLOAT, values.data(), side * 16,
                               SDL_PIXELFORMAT_RGBA64_FLOAT, pixels.data(), side * 8))
            pixels.clear();
    }
};
} // namespace
std::vector<std::array<uint16_t, 4>> falloff_pixels(const std::array<uint32_t, 2> &mask, float radius) {
    // Renderer-thread scratch, bounded even for mods that vary radii. Standard game radii are prepared
    // during startup; a nonstandard radius gets the same exact algorithm and a cached kernel.
    static thread_local std::map<float, Kernel> kernels;
    if (presentation_diagnostics::fresh())
        kernels.clear();
    auto found = kernels.find(radius);
    if (found == kernels.end()) {
        if (kernels.size() == 4) {
            auto oldest = kernels.begin();
            while (oldest != kernels.end() && (oldest->first == 4 || oldest->first == 8))
                ++oldest;
            if (oldest != kernels.end())
                kernels.erase(oldest);
        }
        found = kernels.try_emplace(radius, radius).first;
    }
    const auto &k = found->second;
    if (k.pixels.empty())
        return {};
    const size_t area = size_t(k.side) * k.side;
    std::vector<std::array<uint16_t, 4>> out(area);
    if (!(mask[0] || mask[1]))
        return out;
    std::array<int, 8> bytes{};
    for (int row = 0; row < 8; ++row)
        bytes[size_t(row)] = int(mask[size_t(row / 4)] >> ((row % 4) * 8) & 255);
    const int origin = k.side / 2 - 11; // three times the native coordinate, relative to source center 0
    for (int x = 0; x < k.side; ++x) {
        // Lower envelope of the eight occupied-row parabolas, in exact integer thirds of a pixel.
        // This is the separable squared-distance transform: O(side), rather than eight rows per texel.
        std::array<int, 8> rows{}, weights{};
        std::array<double, 9> edges{};
        int last = -1;
        for (int row = 0; row < 8; ++row) {
            if (!bytes[size_t(row)])
                continue;
            const int col = k.nearest[size_t(bytes[size_t(row)] * k.side + x)];
            const int dx = x - origin - col * 3;
            weights[size_t(row)] = dx * dx;
            const auto intersection = [&](int previous) {
                return double(weights[size_t(row)] + 9 * row * row - weights[size_t(previous)] -
                              9 * previous * previous) /
                       (6 * (row - previous));
            };
            double edge = last < 0 ? -1e30 : intersection(rows[size_t(last)]);
            // Retain zero-width intervals: integer-distance ties must still compare their original
            // float distances, whose rounding can choose a different emitting center.
            while (last > 0 && edge < edges[size_t(last)]) {
                --last;
                edge = intersection(rows[size_t(last)]);
            }
            ++last;
            rows[size_t(last)] = row;
            edges[size_t(last)] = edge;
            edges[size_t(last + 1)] = 1e30;
        }
        int current = 0;
        for (int y = 0; y < k.side; ++y) {
            const int py = y - origin;
            while (current < last && edges[size_t(current + 1)] < py)
                ++current;
            int winner = rows[size_t(current)];
            if (current < last && edges[size_t(current + 1)] == py) {
                const auto distance = [&](int row) {
                    return k.row_distance[size_t(bytes[size_t(row)] * k.side + x)] +
                           k.vertical[size_t(y)][size_t(row)];
                };
                float best = distance(winner);
                for (int next = current + 1; next <= last && edges[size_t(next)] == py; ++next) {
                    const int row = rows[size_t(next)];
                    const float d = distance(row);
                    if (d < best) {
                        best = d;
                        winner = row;
                    }
                }
            }
            const int bit = winner * 8 + k.nearest[size_t(bytes[size_t(winner)] * k.side + x)];
            const size_t at = size_t(y * k.side + x);
            out[at] = k.pixels[size_t(bit) * area + at];
        }
    }
    return out;
}
} // namespace sawgod
