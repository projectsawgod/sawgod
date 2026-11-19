#pragma once

#include "chrome_fixture.h"
#include "engine/enhanced_graphics.h"
#include "engine/presentation_renderer.h"
#include "game/bank34.h"
#include "game/presentation_scene.h"
#include "game/session.h"
#include "game/ui_chrome.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <doctest/doctest.h>
#include <fstream>
#include <iterator>
#include <random>

using namespace sawgod;
namespace {
inline void display(emu::Wsc &w, unsigned seed) {
    std::mt19937 random(seed);
    for (auto &b : w.ram)
        b = uint8_t(random());
    w.cpu.s.halted = true;
    w.cpu.s.flags = 0;
    w.io.fill(0);
    w.io[0x16] = 158;
    w.io[0x60] = 0xE0;
    w.io[0] = 0x3F;
    w.io[7] = 0x32;
    w.io[4] = 1; // sprite table at 0200
    w.io[6] = 128;
    w.io[8] = 30;
    w.io[9] = 20;
    w.io[10] = 190;
    w.io[11] = 120;
    w.io[12] = 0;
    w.io[13] = 0;
    w.io[14] = 223;
    w.io[15] = 143;
    w.scanline = w.line_cycle = 0;
}
inline void exact_layers(const emu::Wsc &w, bool second, bool scene) {
    const auto *layers = w.frame_layers(second, scene);
    REQUIRE(layers);
    const auto rgb = w.framebuffer_rgb(emu::Wsc::ColorMode::Raw, second, scene);
    int bad = 0;
    for (size_t at = 0; at < rgb.size(); ++at) {
        const uint16_t c = layers->color(at);
        bad += rgb[at] != uint32_t((c >> 8 & 15) * 17 << 16 | (c >> 4 & 15) * 17 << 8 | (c & 15) * 17);
    }
    CHECK(bad == 0);
}
} // namespace

namespace {
inline std::string water_fixture(int mirror = -1, bool enabled = true, const std::string &kind = "flowing") {
    std::string out =
        "{\"version\":4,\"tileset\":1,\"metatiles\":[{\"metatile\":0,\"mirror\":" + std::to_string(mirror) +
        ",\"water\":{\"enabled\":" + (enabled ? "true" : "false") + ",\"kind\":\"" + kind +
        "\",\"confidence\":1,\"evidence\":\"asymmetric shore fixture\",\"mask\":[";
    for (int y = 0; y < 16; ++y) {
        if (y)
            out += ',';
        out += y >= 4 ? "\"1111110000000000\"" : "\"0000000000000000\"";
    }
    return out + "]}}]}";
}
} // namespace

namespace {
inline void light_tile(emu::FrameLayers &layers, int tile, int ox, int oy, uint16_t color, int mirror = 0) {
    for (int y = oy; y < oy + 16; ++y) {
        layers.valid[size_t(y)] = 1;
        for (int x = ox; x < ox + 16; ++x) {
            auto &p = layers.planes[emu::FrameLayers::Screen1][size_t(y * 224 + x)];
            const int q = (x - ox) / 8 + (y - oy) / 8 * 2;
            p.tile = uint16_t(0x200 + tile * 4 + (q ^ mirror));
            p.flags = uint8_t(emu::RasterPixel::Present | (mirror & 1 ? emu::RasterPixel::HFlip : 0) |
                              (mirror & 2 ? emu::RasterPixel::VFlip : 0));
            p.color = color;
        }
    }
}
inline std::string light_record(int tile, const char *type, int anchor, int direction = 5) {
    return "{\"metatile\":" + std::to_string(tile) +
           ",\"mirror\":-1,\"light\":{\"enabled\":true,\"type\":\"" + type +
           "\",\"anchor\":" + std::to_string(anchor) + ",\"direction\":" + std::to_string(direction) +
           ",\"color\":\"#FFFFFF\",\"radius\":26,\"intensity\":0.18},\"confidence\":1,\"evidence\":"
           "\"fixture\"}";
}
} // namespace

#ifdef SAWGOD_GPU_TIMESTAMPS
extern "C" bool SawGodRendererTimingStart(SDL_Renderer *, Uint64);
extern "C" bool SawGodRendererTimingPoll(SDL_Renderer *, Uint64 *, double *);

#endif

#include "engine/screen.h"
