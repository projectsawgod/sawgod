// The field maps' LZ packing (decompress_field_map, rom 0x351797; "Map format"): a bitstream read MSB
// first -- 1 then 8 bits: a literal; 0 then 12 bits: a position in a 4 KB ring dictionary (0 ends the stream)
// and 4 bits: length - 2. Every output byte is also written to the ring at `cursor`, which starts at 1. In
// the game the ring is RAM at SS:D6FE, so unpacking leaves its bytes there (ring_writes reproduces that).
#pragma once

#include <cstdint>
#include <vector>

namespace sawgod::game {

struct LzUnpacked {
    std::vector<uint8_t> bytes;
    size_t consumed = 0;       // input bytes read
    bool reads_before = false; // a copy read ring positions this stream hadn't written (the game's prior RAM)
};

// Unpacks at most `max_input` bytes of `src` (the game's CX budget; past it the bits read as 0, which ends
// the stream). `ring` is the dictionary's initial contents (4096 bytes; the game's RAM at D6FE).
LzUnpacked lz_unpack(const uint8_t *src, size_t max_input, const std::vector<uint8_t> &ring);

// A stream that unpacks to `bytes`: the original packer's own choices (the ROM's map layers repack to
// their exact bytes).
std::vector<uint8_t> lz_pack(const std::vector<uint8_t> &bytes);

// The ring writes unpacking `bytes` makes: (ring position, value) in order -- the RAM side effect at D6FE.
inline uint16_t lz_ring_position(size_t i) { return uint16_t((1 + i) & 0xFFF); }

} // namespace sawgod::game
