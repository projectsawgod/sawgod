#include "app/frame_timing.h"
#include "app/picture_motion.h"
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <string>
using namespace sawgod::app;
TEST_CASE("Final pixel measurements expose repeats reverse steps and party jitter despite even states") {
    constexpr int width = 256, height = 160;
    std::vector<uint32_t> original(width * height);
    uint32_t rng = 1;
    for (auto &p : original) {
        rng = rng * 1664525 + 1013904223;
        p = rng & 0xFFFFFF;
    }
    const auto reference = pixel_patch(original, width, height, 48, 48, 48, 48);
    for (const auto delta : {std::array<int, 2>{3, 0}, {0, 0}, {-2, 0}, {1, -3}}) {
        std::vector<uint32_t> composite(original.size());
        for (int y = 8; y < height - 8; ++y)
            for (int x = 8; x < width - 8; ++x)
                composite[size_t(y * width + x)] = original[size_t((y - delta[1]) * width + x - delta[0])];
        // Nominal coordinates claim a steady +3; the actual final picture disagrees.
        const auto measured = match_pixels(reference, composite, width, height, 3, 0);
        REQUIRE(measured.valid);
        CHECK(measured.x == delta[0]);
        CHECK(measured.y == delta[1]);
        CHECK(measured.error == 0);
    }
    std::vector<uint32_t> black(original.size());
    CHECK_FALSE(match_pixels(pixel_patch(black, width, height, 48, 48, 48, 48), black, width, height).valid);
    // Independent opaque actor template catches a moving party on a stationary ground.
    PictureMotion probe;
    std::vector<uint32_t> art(256, 0xFFFFFFFF);
    probe.measure(original, width, height, 64, 79, {}, 1, {160, 80}, 1, art);
    auto jittered = original;
    for (int y = 65; y < 81; ++y)
        for (int x = 154; x < 170; ++x)
            jittered[size_t(y * width + x)] = original[size_t(y * width + x - 2)];
    const auto measured = probe.measure(jittered, width, height, 64, 79, {}, 1, {160, 80}, 1, art);
    REQUIRE(measured.ground_match.valid);
    CHECK(measured.ground_match.x == 0);
    REQUIRE(measured.party_match.valid);
    CHECK(measured.party[0] == 162);
    // CSV records actual measured pixels, with invalid values clearly identified.
    const auto path = std::filesystem::temp_directory_path() / "sawgod-picture-motion-test.csv";
    {
        FrameTimingLog log;
        REQUIRE(log.open(path.string()));
        FrameTimingSample s;
        s.pixel_probe = true;
        s.field_picture = true;
        s.upload_work = {129024, 1, 0};
        s.pixels = measured;
        log.write(s);
    }
    std::ifstream in(path);
    std::string header, row;
    std::getline(in, header);
    std::getline(in, row);
    CHECK(header.find("pixel_ground_dx") != std::string::npos);
    CHECK(header.find("pixel_party_x") != std::string::npos);
    CHECK(header.find("gpu_present_id,gpu_ms,gpu_valid") != std::string::npos);
    CHECK(header.find("field_picture,scanout_missed_refreshes") != std::string::npos);
    CHECK(header.ends_with("upload_bytes,upload_calls,upload_allocations"));
    CHECK(row.ends_with(",129024,1,0"));
    CHECK(std::count(header.begin(), header.end(), ',') == std::count(row.begin(), row.end(), ','));
    in.close(); // Windows cannot remove an open file
    std::filesystem::remove(path);
}
