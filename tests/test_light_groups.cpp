#include "engine/light_groups.h"
#include <doctest/doctest.h>
using namespace sawgod;
TEST_CASE("Connected spell groups use brightness centers and preserve disjoint sources") {
    std::array<uint16_t, 224 * 80> pixels{};
    pixels[10 * 224 + 10] = 10;
    pixels[10 * 224 + 11] = 30;
    pixels[30 * 224 + 100] = 20;
    const auto groups = connected_light_groups(pixels);
    REQUIRE(groups.size() == 2);
    CHECK(groups[0].center_x() == doctest::Approx(11.25));
    CHECK(groups[0].center_y() == doctest::Approx(10.5));
    CHECK(groups[0].pixels == 2);
    CHECK(groups[0].radius(8) >= 8.75f);
    CHECK(groups[1].center_x() == 100.5f);
    CHECK(groups[1].radius(8) == 8);
}
TEST_CASE("Spell grouping caps checkerboard fragmentation at eight without losing weight") {
    std::array<uint16_t, 224 * 80> pixels{};
    double weight = 0, x = 0, y = 0;
    int count = 0;
    for (int row = 0; row < 80; ++row)
        for (int col = 0; col < 224; ++col)
            if ((row + col) % 2 == 0) {
                pixels[size_t(row * 224 + col)] = 1;
                ++count;
                ++weight;
                x += col + 0.5;
                y += row + 0.5;
            }
    const auto groups = connected_light_groups(pixels);
    REQUIRE(groups.size() == 8);
    LightGroup sum;
    for (const auto &g : groups) {
        sum.merge(g);
        CHECK(g.radius(8) >= 8);
    }
    CHECK(sum.pixels == count);
    CHECK(sum.weight == weight);
    CHECK(sum.x == x);
    CHECK(sum.y == y);
    CHECK(connected_light_groups({}).empty());
    pixels.fill(1);
    REQUIRE(connected_light_groups(pixels).size() == 1);
    CHECK(connected_light_groups(pixels)[0].pixels == 224 * 80);
}
