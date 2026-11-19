// SHA-1 and asset archive tests.
#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "engine/assets.h"

using namespace sawgod;

TEST_CASE("sha1 known vectors") {
    const char *abc = "abc";
    CHECK(to_hex(sha1(reinterpret_cast<const uint8_t *>(abc), 3)) ==
          "a9993e364706816aba3e25717850c26c9cd0d89d");
    CHECK(to_hex(sha1(nullptr, 0)) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    std::string two_blocks = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK(to_hex(sha1(reinterpret_cast<const uint8_t *>(two_blocks.data()), two_blocks.size())) ==
          "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
}

TEST_CASE("asset archive: load and apply over a ROM image") {
    std::vector<uint8_t> rom(64, 0xAA);
    Sha1 digest = sha1(rom.data(), rom.size());
    // "GSAWPAK1", rom sha1, kind 1, one entry "x" at offset 4, 3 bytes
    std::vector<uint8_t> file = {'G', 'S', 'A', 'W', 'P', 'A', 'K', '1'};
    file.insert(file.end(), digest.begin(), digest.end());
    for (uint32_t v : {1u, 1u})
        for (int i = 0; i < 4; ++i)
            file.push_back(uint8_t(v >> (8 * i)));
    file.insert(file.end(), {1, 0, 'x', 4, 0, 0, 0, 3, 0, 0, 0, 1, 2, 3});
    std::string path = "test_assets_archive.gsa";
    FILE *f = std::fopen(path.c_str(), "wb");
    REQUIRE(f);
    std::fwrite(file.data(), 1, file.size(), f);
    std::fclose(f);

    std::string error;
    auto archive = AssetArchive::load(path, &error);
    REQUIRE(archive);
    CHECK(archive->kind == AssetArchive::Override);
    REQUIRE(archive->find("x"));
    CHECK(archive->find("x")->rom_offset == 4);
    CHECK(apply_archive(*archive, rom, digest, &error));
    CHECK(rom[3] == 0xAA);
    CHECK(rom[4] == 1);
    CHECK(rom[6] == 3);
    CHECK(rom[7] == 0xAA);

    Sha1 other = digest;
    other[0] ^= 1;
    CHECK_FALSE(apply_archive(*archive, rom, other, &error)); // built for another ROM
    std::remove(path.c_str());
}
