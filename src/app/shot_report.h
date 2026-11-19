// What a --shot run writes beside its pictures (app/main.cpp): the machine state of --shot-state, the
// statistics of --shot-benchmark and the scene pictures and log lines of --shot-mask.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "app/mode7_renderer.h"
#include "emu/wsc.h"
#include "engine/enhanced_graphics.h"
#include "game/presentation_scene.h"

namespace sawgod::app {

// --shot-state: RAM, SRAM, I/O ports, the CPU's registers and clocks; with rgb, "RGB1" and the composed
// picture's pixels after them (--shot-state-frames).
std::vector<uint8_t> shot_state(const emu::Wsc &wsc, const std::vector<uint32_t> *rgb);

// --shot-benchmark: per measured frame, in milliseconds.
struct ShotBenchmark {
    std::vector<double> total, cpu, gpu, fill, text, cache;
};

// Logs each series' median and 95th percentile (sorts them).
void log_shot_benchmark(int w, int h, ShotBenchmark &bench, unsigned text_strokes, unsigned text_batches,
                        unsigned fill_quads);

// --shot-mask PREFIX: the scene's ownership masks as pictures (PREFIX: the scene's own pixels,
// .enemy.png, .background.png, .water.png), with a log line about the scene. enhanced: an Enhanced picture
// was uploaded for this view.
void write_shot_masks(const std::string &prefix, const game::PresentationScene &scene, bool enhanced);

// The scene's lights and actor shadows, and how many of them the camera view projected, as log lines.
void log_shot_lights(const game::PresentationScene &scene, const game::PresentationScene &view);

// --shot-mask: the GPU work of the view just drawn, as "GPU ... proxy" log lines (shaded pixels, overlay
// restores, light and shadow influence). graphics: the effects that drew it (the world camera's with
// mode7_uploaded). underlay_pixels: the ordinary picture drawn under it.
void log_shot_gpu_work(const Mode7Renderer &mode7, const EnhancedGraphics &graphics, bool mode7_uploaded,
                       uint64_t underlay_pixels);

// An XRGB8888 surface's pixels as 0xRRGGBB.
std::vector<uint32_t> surface_rgb(const SDL_Surface &rgb);

} // namespace sawgod::app
