#pragma once

#include "emu/wsc.h"
#include "game/gb_extract.h"
#include "game/gfx_swap.h"
#include "game/machine.h"
#include "game/mode7.h"
#include "game/session.h"
#include "game/text.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace sawgod;
namespace {
inline std::vector<uint8_t> transition_rom() {
    const char *path = std::getenv("SAWGOD_WSC_ROM");
    if (!path)
        return {};
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
inline void advance(emu::Wsc &w, game::GfxSwap *swap = nullptr) {
    w.run_frame();
    w.apu.take();
    if (swap) {
        const auto ram = w.ram;
        const auto sram = w.sram;
        const auto io = w.io;
        swap->frame(w);
        REQUIRE(w.ram == ram);
        REQUIRE(w.sram == sram);
        REQUIRE(w.io == io);
    }
}
inline void boot_transition(emu::Wsc &w, game::GfxSwap *swap = nullptr) {
    game::install(w);
    if (swap)
        swap->set_enabled(w, true);
    w.render_lcd = false;
    for (int i = 0; i < 2900; ++i) {
        const bool a = (i >= 1200 && i < 1206) || (i >= 1500 && i < 1506) || (i >= 2700 && i < 2706) ||
                       (i >= 2760 && i < 2766);
        const bool start = (i >= 600 && i < 606) || (i >= 2820 && i < 2826);
        w.set_keys(a ? emu::KEY_A : start ? emu::KEY_START : 0);
        advance(w, swap);
    }
    w.set_keys(0);
    w.ram[0xD0AE] = 255;
}
inline void queue_event(emu::Wsc &w, int kind, int argument) {
    w.ram[0xC825] = 1;
    w.ram[0xC826] = uint8_t(kind);
    w.ram[0xC827] = uint8_t(argument);
}
inline void reach_transition(emu::Wsc &w, int map, game::GfxSwap *swap = nullptr) {
    for (int i = 0; i < 600; ++i) {
        advance(w, swap);
        if (w.ram[0xD0DC] == map && !w.ram[0xD52C] && !w.ram[0xD519] && game::recognized_field(w)) {
            // Loaded descriptors precede the initial field fade and idle loop.
            for (int settle = 0; settle < 120; ++settle)
                advance(w, swap);
            return;
        }
    }
    FAIL("field destination did not settle");
}
} // namespace
