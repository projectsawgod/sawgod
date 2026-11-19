#include "engine/falloff_field.h"
#include "engine/influence_stamp.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <doctest/doctest.h>

TEST_CASE("spell mask lookup retains every float16 texel of the nearest-center oracle") {
    std::vector<std::array<uint32_t, 2>> masks{{0, 0}, {~0u, ~0u}, {0xA55A5AA5, 0x12345678}};
    for (int bit = 0; bit < 64; ++bit) {
        std::array<uint32_t, 2> mask{};
        mask[size_t(bit / 32)] = uint32_t(1) << (bit % 32);
        masks.push_back(mask);
    }
    uint32_t random = 0xB12D4891;
    for (int i = 0; i < 24; ++i) {
        random = random * 1664525 + 1013904223;
        const uint32_t a = random;
        random = random * 1664525 + 1013904223;
        masks.push_back({a, random});
    }
    for (float radius : {4.f, 8.f, 5.5f}) {
        const int halo = int(std::ceil(radius)) + 1, side = (8 + 2 * halo) * 3;
        for (const auto &mask : masks) {
            CAPTURE(radius);
            CAPTURE(mask[0]);
            CAPTURE(mask[1]);
            std::vector<std::array<float, 4>> oracle(size_t(side) * side);
            // Brute force all emitting centers, independently of the lookup's row tables and winner.
            for (int y = 0; y < side; ++y)
                for (int x = 0; x < side; ++x) {
                    float distance = 1e20f;
                    const float px = (float(x) + 0.5f) / 3 - halo;
                    const float py = (float(y) + 0.5f) / 3 - halo;
                    for (int bit = 0; bit < 64; ++bit)
                        if (mask[size_t(bit / 32)] & (uint32_t(1) << (bit % 32))) {
                            const float dx = px - (bit % 8 + 0.5f), dy = py - (bit / 8 + 0.5f);
                            distance = std::min(distance, dx * dx + dy * dy);
                        }
                    const float d = std::sqrt(distance) / radius;
                    const float t = std::clamp((d - 0.6f) / 0.4f, 0.f, 1.f);
                    oracle[size_t(y * side + x)] = {std::exp(-d * d * 3) * (1 - t * t * (3 - 2 * t)), 0, 0,
                                                    0};
                }
            std::vector<std::array<uint16_t, 4>> half(oracle.size());
            REQUIRE(SDL_ConvertPixels(side, side, SDL_PIXELFORMAT_RGBA128_FLOAT, oracle.data(), side * 16,
                                      SDL_PIXELFORMAT_RGBA64_FLOAT, half.data(), side * 8));
            CHECK(sawgod::falloff_pixels(mask, radius) == half);
        }
    }
}

TEST_CASE("field stamp tables retain every original float16 texel at fractional projected sizes") {
    const auto smooth = [](float a, float b, float v) {
        const float t = std::clamp((v - a) / (b - a), 0.f, 1.f);
        return t * t * (3 - 2 * t);
    };
    for (int type : {0, 1, 2, 3, 6, 7})
        for (float radius : {8.f, 25.6f, 32.f})
            for (const auto extent :
                 {std::array<float, 2>{0, 0}, {12.8f, 25.6f}, {38.4f, 0}, {89.6f, 51.2f}, {32, 44.8f}}) {
                const std::array<float, 6> key{float(type), radius, extent[0], extent[1], 0, 0};
                CAPTURE(key);
                const int side = std::max(8, int(std::ceil(2 * (radius + std::max(extent[0], extent[1])))));
                std::vector<std::array<float, 4>> scalar(size_t(side) * side);
                for (int y = 0; y < side; ++y)
                    for (int x = 0; x < side; ++x) {
                        const float u = (x + 0.5f) / side * 2 - 1, v = (y + 0.5f) / side * 2 - 1;
                        const float px = u * (radius + extent[0]), py = v * (radius + extent[1]);
                        const float distance = std::hypot(std::max(std::abs(px) - extent[0], 0.f),
                                                          std::max(std::abs(py) - extent[1], 0.f));
                        const float d = distance / radius;
                        float amount = type >= 6 ? std::exp(-d * d * 3) * (1 - smooth(0.6f, 1, d))
                                                 : std::exp(-d * d * 10) * (1 - smooth(0.5f, 1, d));
                        if (type == 2)
                            amount *= 1 - smooth(0.28f, 0.5f, d) * 0.25f;
                        if (type == 3)
                            amount *= 0.2f * (0.08f + 0.92f * smooth(2, 9, distance));
                        if (distance >= radius)
                            amount = 0;
                        scalar[size_t(y * side + x)] = {amount, 0, 0, 0};
                    }
                std::vector<std::array<uint16_t, 4>> expected(scalar.size());
                REQUIRE(SDL_ConvertPixels(side, side, SDL_PIXELFORMAT_RGBA128_FLOAT, scalar.data(), side * 16,
                                          SDL_PIXELFORMAT_RGBA64_FLOAT, expected.data(), side * 8));
                const auto actual = sawgod::build_influence_stamp(key);
                CHECK(actual.side == side);
                CHECK(actual.phases == 1);
                CHECK(actual.pixels == expected);
            }
}
