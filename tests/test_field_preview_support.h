#pragma once

#include <doctest/doctest.h>

#include "engine/indexed_png.h"
#include "game/bank35_field.h"
#include "game/bank35_world.h"
#include "game/gfx_swap.h"
#include "game/machine.h"
#include "tools/field_preview.h"

using namespace sawgod;

namespace {
inline std::vector<uint8_t> fixture_rom() {
    std::vector<uint8_t> rom(0x400000, 0);
    const auto word = [&](unsigned at, uint16_t v) {
        rom[at] = uint8_t(v);
        rom[at + 1] = uint8_t(v >> 8);
    };
    word(0x35B718, 4);
    word(0x35B71A, 0x1000);
    word(0x35B71C, 0x2000);
    word(0x35B71E, 0x3000);
    for (unsigned c = 0; c < 16; ++c)
        word(0x0F2000 + c * 2, uint16_t(c * 0x111));
    for (unsigned q = 0; q < 4; ++q)
        for (unsigned y = 0; y < 8; ++y)
            for (unsigned x = 0; x < 8; x += 2) {
                const auto color = [&](unsigned xx) { return uint8_t((q * 4 + xx + 2 * y) % 15 + 1); };
                rom[0x1000 + q * 32 + y * 4 + x / 2] = uint8_t(color(x) << 4 | color(x + 1));
            }
    return rom;
}
} // namespace

namespace {
template <class Swap> void import_npc_defaults(Swap &swap, emu::Wsc &w) {
    if constexpr (requires { swap.import_field_sprites(w); })
        swap.import_field_sprites(w);
}
} // namespace
