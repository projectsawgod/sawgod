// Resource regions (game/resources.h) and the indexed PNG codec (engine/indexed_png.h).
#include <doctest/doctest.h>

#include "emu/wsc.h"
#include "engine/indexed_png.h"
#include "game/resources.h"

using namespace sawgod;

TEST_CASE("indexed PNG: encode, decode, RGB pixels map to the nearest palette entry") {
    IndexedImage img;
    img.width = 9, img.height = 3;
    for (int i = 0; i < 27; ++i)
        img.pixels.push_back(uint8_t(i % 16));
    const std::vector<uint32_t> palette = {0xFFFFFF, 0x000000, 0xFF0000};
    IndexedImage back;
    std::string error;
    REQUIRE(decode_indexed_png(encode_indexed_png(img, palette), palette, &back, &error));
    CHECK(back.width == 9);
    CHECK(back.height == 3);
    CHECK(back.pixels == img.pixels);
    CHECK_FALSE(decode_indexed_png({1, 2, 3}, palette, &back, &error));
}

TEST_CASE("resource regions: every region extracts and builds back to the ROM's bytes, bad files refused") {
    std::vector<uint8_t> rom(0x400000);
    for (size_t i = 0; i < rom.size(); ++i)
        rom[i] = uint8_t(i * 7 + (i >> 9));
    emu::Wsc w(rom, 0x8000);
    const auto &regions = game::resource_regions();
    REQUIRE(!regions.empty());
    for (const game::ResourceRegion &r : regions) {
        CAPTURE(r.file);
        std::vector<uint8_t> bytes;
        std::string error;
        if (r.format == game::RegionFormat::Palettes) // 12-bit colors: give it ROM bytes that are colors
            for (uint32_t i = 0; i < r.size; i += 2)
                w.rom[r.rom + i + 1] &= 0x0F;
        REQUIRE(game::build_region(r, game::extract_region(r, w), &bytes, &error));
        CHECK(std::equal(bytes.begin(), bytes.end(), w.rom.begin() + r.rom));
    }
    const game::ResourceRegion &raw = regions.back();
    std::vector<uint8_t> bytes;
    std::string error;
    CHECK_FALSE(game::build_region(raw, {1, 2, 3}, &bytes, &error)); // wrong size
    for (const game::ResourceRegion &r : regions)
        if (r.format == game::RegionFormat::Tiles) {
            IndexedImage small;
            small.width = small.height = 8;
            small.pixels.assign(64, 0);
            CHECK_FALSE(game::build_region(r, encode_indexed_png(small, {0}), &bytes, &error)); // wrong size
            break;
        }
}
