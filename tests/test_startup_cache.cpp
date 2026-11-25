#include "engine/derived_cache.h"
#include "engine/indexed_png.h"
#include "engine/persist.h"
#include "game/gfx_swap.h"
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace sawgod::game {
struct GfxSwapCacheProof {
    static bool same(const GfxSwap &a, const GfxSwap &b) {
        if (a.maps_.size() != b.maps_.size())
            return false;
        for (const auto &[id, left] : a.maps_) {
            const auto found = b.maps_.find(id);
            if (found == b.maps_.end())
                return false;
            const auto &right = found->second;
            for (const auto *lookup : {&right.by_cell, &right.by_type}) {
                const std::map<int, int> reference(lookup->begin(), lookup->end());
                for (int key = -1; key <= 65536; ++key) {
                    const auto a = lookup->find(key);
                    const auto b = reference.find(key);
                    if ((a == lookup->end()) != (b == reference.end()))
                        return false;
                    if (a != lookup->end() && a->second != b->second)
                        return false;
                }
            }
            if (left.metatiles != right.metatiles || left.gb != right.gb || left.by_cell != right.by_cell ||
                left.by_type != right.by_type)
                return false;
        }
        return true;
    }
};
} // namespace sawgod::game

TEST_CASE(
    "GB field map cache: hit, every layered input invalidates, decoded output matches the original loader") {
    namespace fs = std::filesystem;
    using sawgod::game::GfxSwap;
    const auto path = (fs::temp_directory_path() / "sawgod-gb-field-cache-test.cache").string();
    fs::remove(path);
    const auto bytes = [](const std::string &s) { return std::vector<uint8_t>(s.begin(), s.end()); };
    sawgod::IndexedImage image{32, 16, std::vector<uint8_t>(32 * 16, 1)};
    for (int y = 0; y < 16; ++y)
        for (int x = 16; x < 32; ++x)
            image.pixels[size_t(y * 32 + x)] = 2;
    const std::vector<uint32_t> palette{0xffffff, 0xaaaaaa, 0x555555, 0};
    std::map<std::string, std::vector<uint8_t>> files{
        {"gb/001_metatiles.png", sawgod::encode_indexed_png(image, palette)},
        {"gb/001_lookup.csv", bytes("wsc,gb,type\n0,0,\n1,1,1\n")},
        {"gb/001_cells.csv", bytes("x,y,gb\n0,0,1\n")},
        {"gb/001_hidden.csv", bytes("x,y\n1,1\n")}};
    const auto read = [&] {
        std::ifstream in(path, std::ios::binary);
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
    };
    auto run = [&](bool expected_hit, bool expected_error = false) {
        GfxSwap reference, actual;
        std::vector<std::string> reference_errors, errors;
        CHECK_FALSE(reference.load_field_maps_cached(files, "", &reference_errors));
        const auto previous = read();
        CHECK(actual.load_field_maps_cached(files, path, &errors) == expected_hit);
        CHECK(errors.empty() == !expected_error);
        CHECK(reference.has_data() == !expected_error);
        CHECK(errors == reference_errors);
        CHECK(sawgod::game::GfxSwapCacheProof::same(reference, actual));
        if (expected_hit)
            CHECK(read() == previous); // no writes on hit
    };
    run(false);
    run(true);
    image.pixels[0] = 3;
    files["gb/001_metatiles.png"] = sawgod::encode_indexed_png(image, palette);
    run(false);
    run(true);
    files["gb/001_lookup.csv"] = bytes("wsc,gb,type\n0,1,\n1,0,1\n");
    run(false);
    run(true);
    files["gb/001_cells.csv"] = bytes("x,y,gb\n-1,2,0\n");
    run(false);
    run(true);
    files["gb/001_hidden.csv"] = bytes("x,y\n-2,3\n");
    run(false);
    run(true);
    files["gb/unrelated.bin"] = {1};
    run(true); // Unused art is not an input to field-map decoding.
    run(true);
    files.erase("gb/001_hidden.csv");
    run(false);
    run(true);
    files["gb/002_cells.csv"] = std::move(files["gb/001_cells.csv"]);
    files.erase("gb/001_cells.csv");
    run(false);
    run(true);
    auto broken = read();
    REQUIRE_FALSE(broken.empty());
    broken.back() ^= 1;
    REQUIRE(sawgod::WriteFileAtomic(path, broken));
    run(false);
    run(true);
    sawgod::write_derived_cache(path, "obsolete-algorithm-v0", {1, 2, 3});
    run(false);
    run(true);
    files["gb/001_lookup.csv"] = bytes("invalid\n");
    run(false, true);
    run(false, true); // bad resources/errors are never cached
    fs::remove(path);
}
