#pragma once

#include "app/display_motion.h"
#include "app/mode7_renderer.h"
#include "engine/config.h"
#include "engine/enhanced_graphics.h"
#include "engine/presentation_renderer.h"
#include "engine/screen.h"
#include "engine/settings.h"
#include "game/game_config.h"
#include "game/mode7.h"
#include "game/session.h"
#include "tools/field_preview.h"
#include <cmath>
#include <doctest/doctest.h>
#include <fstream>

using namespace sawgod;

namespace {
inline std::vector<uint8_t> world_rom(bool evidence = false) {
    std::vector<uint8_t> rom(0x400000);
    const auto put = [&](int at, int word) {
        rom[size_t(at)] = uint8_t(word);
        rom[size_t(at + 1)] = uint8_t(word >> 8);
    };
    rom[0x35A830 + 88 * 18 + 1] = 0xE0;
    put(0x35B718, 8);
    put(0x35B71A, 0x1000);
    put(0x35B71C, 0x2000);
    put(0x35B71E, 0x3000);
    put(0xF2002, 0x123);
    put(0xF2004, 0xABC);
    std::fill_n(rom.begin() + 0x1000, 4 * 32, 0x11);
    std::fill_n(rom.begin() + 0x1080, 4 * 32, 0x22);
    if (evidence)
        rom[0x1000] = 0x12; // visible terrain evidence, including in the all-zero map fixture
    return rom;
}
} // namespace
