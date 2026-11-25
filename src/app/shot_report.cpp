#include "app/shot_report.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>

#include "emu/png.h"

namespace sawgod::app {

std::vector<uint8_t> shot_state(const emu::Wsc &wsc, const std::vector<uint32_t> *rgb) {
    std::vector<uint8_t> state(wsc.ram.begin(), wsc.ram.end());
    state.insert(state.end(), wsc.sram.begin(), wsc.sram.end());
    state.insert(state.end(), wsc.io.begin(), wsc.io.end());
    const auto word = [&](uint16_t value) {
        state.push_back(uint8_t(value));
        state.push_back(uint8_t(value >> 8));
    };
    for (const auto value : wsc.cpu.s.r)
        word(value);
    for (const auto value : wsc.cpu.s.seg)
        word(value);
    word(wsc.cpu.s.ip);
    word(wsc.cpu.s.flags);
    state.push_back(uint8_t(wsc.cpu.s.halted));
    word(uint16_t(wsc.scanline));
    word(uint16_t(wsc.line_cycle));
    for (const auto value : {wsc.frame_count, wsc.cpu.cycles})
        for (int shift = 0; shift < 64; shift += 8)
            state.push_back(uint8_t(value >> shift));
    if (rgb) {
        // Audit the exact composed pixels as well as native palette RAM. Enhanced's
        // later gradients and soft text have their own output rounding.
        state.insert(state.end(), {'R', 'G', 'B', '1'});
        for (const auto color : *rgb)
            for (int shift : {16, 8, 0})
                state.push_back(uint8_t(color >> shift));
    }
    return state;
}

void log_shot_benchmark(int w, int h, ShotBenchmark &bench, unsigned text_strokes, unsigned text_batches,
                        unsigned fill_quads) {
    std::sort(bench.total.begin(), bench.total.end());
    if (!bench.total.empty())
        SDL_Log("Presentation cost: %dx%d n=%u median=%.3fms p95=%.3fms (draw + submit + 1px "
                "fence readback + CPU collection/upload, fixed scene)",
                w, h, unsigned(bench.total.size()), bench.total[bench.total.size() / 2],
                bench.total[size_t(double(bench.total.size() - 1) * 0.95)]);
    std::sort(bench.cache.begin(), bench.cache.end());
    if (!bench.cache.empty())
        SDL_Log("UI cache check CPU: median=%.3fms p95=%.3fms (shared by HD Menus and Menu Fill)",
                bench.cache[bench.cache.size() / 2],
                bench.cache[size_t(double(bench.cache.size() - 1) * .95)]);
    std::sort(bench.text.begin(), bench.text.end());
    if (!bench.text.empty())
        SDL_Log("HD Menus CPU: median=%.3fms p95=%.3fms strokes=%u batches=%u",
                bench.text[bench.text.size() / 2], bench.text[size_t(double(bench.text.size() - 1) * .95)],
                text_strokes, text_batches);
    std::sort(bench.fill.begin(), bench.fill.end());
    if (!bench.fill.empty())
        SDL_Log("Menu Fill CPU: median=%.3fms p95=%.3fms quads=%u batches=%u",
                bench.fill[bench.fill.size() / 2], bench.fill[size_t(double(bench.fill.size() - 1) * 0.95)],
                fill_quads, fill_quads ? 1u : 0u);
    std::sort(bench.gpu.begin(), bench.gpu.end());
    if (!bench.gpu.empty())
        SDL_Log("Presentation GPU time: n=%u median=%.3fms p95=%.3fms", unsigned(bench.gpu.size()),
                bench.gpu[bench.gpu.size() / 2], bench.gpu[size_t(double(bench.gpu.size() - 1) * 0.95)]);
    std::sort(bench.cpu.begin(), bench.cpu.end());
    if (!bench.cpu.empty())
        SDL_Log("Presentation CPU collection/upload: median=%.3fms p95=%.3fms",
                bench.cpu[bench.cpu.size() / 2], bench.cpu[size_t(double(bench.cpu.size() - 1) * 0.95)]);
}

void write_shot_masks(const std::string &prefix, const game::PresentationScene &scene, bool enhanced) {
    std::vector<uint32_t> mask(scene.mask.size());
    for (size_t i = 0; i < mask.size(); ++i)
        mask[i] = (scene.mask[i] & 0xFF0000) ? 0xFFFFFF : 0;
    sawgod::emu::write_file(prefix, sawgod::emu::encode_png(mask.data(), 224, 144));
    for (size_t i = 0; i < mask.size(); ++i)
        mask[i] = (scene.mask[i] & 0x00FE00) == 0x00FE00 ? 0xFFFFFF : 0;
    sawgod::emu::write_file(prefix + ".enemy.png", sawgod::emu::encode_png(mask.data(), 224, 144));
    if (!scene.background.empty())
        sawgod::emu::write_file(prefix + ".background.png",
                                sawgod::emu::encode_png(scene.background.data(), 224, 144));
    SDL_Log("Presentation scene: kind=%d map=%d actors=%u enhanced=%d", int(scene.kind), scene.map,
            unsigned(scene.actors.size()), int(enhanced));
    if (!scene.water.empty()) {
        sawgod::emu::write_file(prefix + ".water.png", sawgod::emu::encode_png(scene.water.data(), 224, 144));
        SDL_Log("Presentation water pixels: %u",
                unsigned(std::count_if(scene.water.begin(), scene.water.end(),
                                       [](uint32_t p) { return p != 0; })));
    }
}

void log_shot_lights(const game::PresentationScene &scene, const game::PresentationScene &view) {
    SDL_Log("Presentation projected: lights=%u actors=%u", unsigned(view.lights.size()),
            unsigned(view.actors.size()));
    SDL_Log("Presentation lights: %u", unsigned(scene.lights.size()));
    for (const auto &l : scene.lights)
        SDL_Log("Light: x=%.2f y=%.2f radius=%.1f hard=%.0f RGB=%.2f/%.2f/%.2f "
                "intensity=%.3f type=%d tiles=%.0f direction=%.2f/%.2f",
                l.position[0], l.position[1], l.position[2], l.position[3], l.emission[0], l.emission[1],
                l.emission[2], l.emission[3], l.type, l.beam[3], l.beam[0], l.beam[1]);
    for (const auto &a : scene.actors)
        SDL_Log("  shadow at %.1f,%.1f radii %.1f,%.1f", a[0], a[1], a[2], a[3]);
}

void log_shot_gpu_work(const Mode7Renderer &mode7, const EnhancedGraphics &graphics, bool mode7_uploaded,
                       uint64_t underlay_pixels) {
    SDL_Log("GPU shaded-pixel proxy: ground=%llu ownership=%llu water=%llu atlas=%llu "
            "bloom=%llu effects_total=%llu effect_passes=%u",
            (unsigned long long)mode7.shaded_pixels[0], (unsigned long long)mode7.shaded_pixels[1],
            (unsigned long long)mode7.shaded_pixels[2], (unsigned long long)mode7.atlas_pixels,
            (unsigned long long)graphics.last_work.bloom_pixels,
            (unsigned long long)graphics.last_work.shaded_pixels, graphics.last_work.passes);
    SDL_Log("GPU overlay proxy: restore=%llu ui_grid=%llu underlay=%llu scene_targets=%u",
            (unsigned long long)graphics.ui_regions.pixels,
            (unsigned long long)(mode7_uploaded ? mode7.ui_regions.pixels : 0),
            (unsigned long long)underlay_pixels, mode7_uploaded ? 2u : 0u);
    SDL_Log("GPU influence proxy: lights=%llu light_clear=%llu light_quads=%u "
            "light_passes=%u light_draws=%u shadows=%llu shadow_clear=%llu shadow_quads=%u "
            "composite_samples=%u restore_samples=%u ground_shadow_samples=%u",
            (unsigned long long)graphics.influence_work.pixels,
            (unsigned long long)graphics.influence_work.clear_pixels, graphics.influence_work.quads,
            graphics.influence_work.passes, graphics.influence_work.draws,
            (unsigned long long)mode7.shadow_work.pixels, (unsigned long long)mode7.shadow_work.clear_pixels,
            mode7.shadow_work.quads, sawgod::EnhancedGraphics::composite_influence_samples,
            sawgod::EnhancedGraphics::restore_influence_samples,
            sawgod::Mode7Renderer::ground_shadow_samples);
}

std::vector<uint32_t> surface_rgb(const SDL_Surface &rgb) {
    std::vector<uint32_t> px(size_t(rgb.w) * rgb.h);
    for (int y = 0; y < rgb.h; ++y)
        std::memcpy(&px[size_t(y) * rgb.w], static_cast<uint8_t *>(rgb.pixels) + y * rgb.pitch,
                    size_t(rgb.w) * 4);
    for (uint32_t &p : px)
        p &= 0xFFFFFF;
    return px;
}

} // namespace sawgod::app
