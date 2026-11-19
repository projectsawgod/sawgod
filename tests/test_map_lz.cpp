// The field maps' LZ packing (game/map_lz.h).
#include <doctest/doctest.h>

#include "game/map_lz.h"

using namespace sawgod::game;

TEST_CASE("map LZ: pack then unpack gives the bytes back, the stream ends itself, budgets cut it") {
    std::vector<uint8_t> data;
    for (int i = 0; i < 9000; ++i) // runs, repeats past the 4 KB ring, and noise
        data.push_back(uint8_t(i < 3000   ? (i / 7) % 5
                               : i < 6000 ? data[size_t(i - 3000)]
                                          : (i * 2654435761u) >> 24));
    const std::vector<uint8_t> packed = lz_pack(data);
    CHECK(packed.size() < data.size());
    const LzUnpacked back = lz_unpack(packed.data(), packed.size(), std::vector<uint8_t>(4096, 0));
    CHECK(back.bytes == data);
    CHECK_FALSE(back.reads_before);
    CHECK(back.consumed == packed.size());
    CHECK(lz_unpack(packed.data(), 0, {}).bytes.empty()); // no input: zero bits, the end marker

    // a hand-made stream: literal 0x41, copy 3 from ring position 1 (overlapping), end
    // bits: 1 01000001 | 0 000000000001 0001 | 0 000000000000
    const std::vector<uint8_t> s = {0xA0, 0x80, 0x04, 0x40, 0x00};
    CHECK(lz_unpack(s.data(), s.size(), {}).bytes == std::vector<uint8_t>{0x41, 0x41, 0x41, 0x41});
}

TEST_CASE("map LZ: a copy from the ring's prior contents is noticed") {
    // copy 2 from position 5 with nothing written yet: bits 0 000000000101 0000 | 0 000000000000
    const std::vector<uint8_t> s = {0x00, 0x28, 0x00, 0x00, 0x00};
    std::vector<uint8_t> ring(4096, 0);
    ring[5] = 7, ring[6] = 8;
    const LzUnpacked u = lz_unpack(s.data(), s.size(), ring);
    CHECK(u.bytes == std::vector<uint8_t>{7, 8});
    CHECK(u.reads_before);
}
