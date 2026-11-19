// Extracts the original GB battle-effect frames for host presentation.
#include "game/gb_battle_effects.h"
#include "emu/wsc.h"
#include "engine/indexed_png.h"
#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <stdexcept>
#include <tuple>
namespace sawgod::game {
namespace {
int shade(uint16_t c) {
    constexpr uint16_t colors[] = {0xFFF, 0xAAA, 0x555, 0};
    for (int i = 0; i < 4; ++i)
        if (colors[i] == c)
            return i;
    throw std::runtime_error("ORIJINARU effect produced a non-GB color");
}
void put(emu::Wsc &w, int at, int value) {
    w.ram[size_t(at)] = uint8_t(value);
    w.ram[size_t(at + 1)] = uint8_t(value >> 8);
}
} // namespace
std::map<std::string, std::vector<uint8_t>>
extract_gb_battle_effects(const std::vector<uint8_t> &rom, const std::function<void(float)> &progress) {
    // the boot replay is about 60% of the time, the effects the rest (measured)
    constexpr float kReplayShare = 0.6f;
    using namespace emu;
    // Extraction runs on the ROM screen's worker, whose stack can be small on macOS.
    const auto machine = std::make_unique<Wsc>(rom, 0x8000);
    auto &w = *machine;
    w.render_lcd = false;
    const std::pair<int, uint16_t> inputs[] = {
        {600, KEY_START},  {606, 0},  {1200, KEY_A},  {1206, 0}, {1300, KEY_X3}, {1306, 0},
        {1340, KEY_X3},    {1346, 0}, {1500, KEY_A},  {1506, 0}, {2100, KEY_A},  {2106, 0},
        {4400, KEY_A},     {4406, 0}, {4700, KEY_A},  {4706, 0}, {5300, KEY_A},  {5306, 0},
        {5360, KEY_START}, {5366, 0}, {6300, KEY_X3}, {6400, 0}, {6500, KEY_X3}, {6600, 0},
        {6700, KEY_X2},    {6800, 0}, {6900, KEY_X2}, {7000, 0}, {7100, KEY_X2}, {7200, 0},
        {7300, KEY_X1},    {7400, 0}};
    size_t next = 0;
    uint16_t keys = 0;
    for (int frame = 0; frame < 8000; ++frame) {
        while (next < std::size(inputs) && inputs[next].first <= frame)
            keys = inputs[next++].second;
        w.set_keys(keys);
        w.run_frame();
        if (progress && frame % 100 == 99)
            progress(kReplayShare * float(frame + 1) / 8000);
    }
    // Direct initialization elides the snapshot into heap storage. make_unique(snapshot())
    // would first materialize the large snapshot argument on the worker's stack.
    const std::unique_ptr<const Wsc::Snapshot> base(new Wsc::Snapshot(w.snapshot()));
    std::map<int, int> item_of_effect;
    std::string actions = "action,effect,flags\n";
    for (int item = 0; item < 256; ++item) {
        const int flags = rom[size_t(0x33B701 + item * 8)], effect = rom[size_t(0x33B706 + item * 8)];
        actions += std::to_string(item) + "," + std::to_string(effect) + "," + std::to_string(flags) + "\n";
        if ((flags & 0x80) && effect)
            item_of_effect.try_emplace(effect, item);
    }
    std::map<std::string, std::vector<uint8_t>> out;
    out["gb/effect_actions.csv"] = {actions.begin(), actions.end()};
    std::string csv = "effect,frame,first,last,dx,dy,fill,c0,c1,c2,c3\n";
    size_t done = 0;
    for (auto [effect, item] : item_of_effect) {
        if (progress)
            progress(kReplayShare + (1 - kReplayShare) * float(done++) / float(item_of_effect.size()));
        w.restore(*base);
        w.on_instruction = {};
        w.render_lcd = true;
        w.capture_layers(true);
        w.set_keys(0);
        // Unique procedural coordinate tiles let raster provenance identify scroll/distortion.
        // These synthetic pixels are never exported; only offsets and palette commands are.
        for (int tile = 128; tile < 288; ++tile)
            for (int y = 0; y < 8; ++y) {
                w.ram[size_t(0x2000 + tile * 16 + y * 2)] = 0x55;
                w.ram[size_t(0x2001 + tile * 16 + y * 2)] = 0x33;
            }
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 20; ++x)
                put(w, 0x1800 + y * 64 + (x + 4) * 2, 128 + y * 20 + x);
        w.ram[0xD3EA] = 1;
        w.ram[0xD3EC] = w.ram[0xD3EE] = 0xFF;
        w.ram[0xF0B1] = 4;
        w.ram[0xF0B2] = uint8_t(item);
        w.cpu.s.seg[CS] = 0xF000;
        w.cpu.s.seg[SS] = w.cpu.s.seg[DS] = w.cpu.s.seg[ES] = 0;
        w.cpu.s.r[SP] = 0xFD00;
        put(w, 0xFD00, 0xFFFF);
        w.cpu.s.r[AX] = uint16_t(effect);
        w.cpu.s.ip = 0x8B1E;
        w.cpu.s.flags = IF | 2;
        w.cpu.s.halted = false;
        w.cpu.flush_queue();
        bool done = false;
        w.on_instruction = [&](const Wsc &state) {
            if (state.cpu.s.seg[CS] == 0xF000 && state.cpu.s.ip == 0xFFFF) {
                done = true;
                w.cpu.s.halted = true;
            }
        };
        std::vector<GbEffectFrame> frames;
        while (!done && frames.size() < 2048) {
            w.run_frame();
            const auto *layers = w.frame_layers();
            GbEffectFrame frame;
            for (int y = 0; y < 64; ++y) {
                auto &row = frame.rows[size_t(y)];
                std::map<std::pair<int, int>, int> shifts;
                int fallback = 0;
                for (int x = 32; x < 192; ++x) {
                    const size_t at = size_t(y * 224 + x);
                    const auto &s2 = layers->planes[FrameLayers::Screen2][at];
                    const auto &s1 = layers->planes[FrameLayers::Screen1][at];
                    const auto &p = s2.flags & RasterPixel::Present ? s2
                                    : s1.flags & RasterPixel::Present
                                        ? s1
                                        : layers->planes[FrameLayers::Backdrop][at];
                    const auto &top = layers->top(at);
                    if (top.sprite != 255)
                        frame.sprites[at] = uint8_t(shade(top.color) + 1);
                    fallback = shade(p.color);
                    if (p.tile < 128 || p.tile >= 288)
                        continue;
                    const int screen = &p == &s2 ? 1 : 0;
                    int px = (x + layers->registers[size_t(y)][size_t(0x10 + screen * 2)]) & 7;
                    int py = (y + layers->registers[size_t(y)][size_t(0x11 + screen * 2)]) & 7;
                    if (p.flags & RasterPixel::HFlip)
                        px = 7 - px;
                    if (p.flags & RasterPixel::VFlip)
                        py = 7 - py;
                    const int id = p.tile - 128;
                    ++shifts[{id % 20 * 8 + px - (x - 32), id / 20 * 8 + py - y}];
                    row.colors[size_t(p.index & 3)] = uint8_t(shade(p.color));
                }
                if (shifts.empty())
                    row.fill = fallback;
                else {
                    const auto best =
                        std::max_element(shifts.begin(), shifts.end(),
                                         [](const auto &a, const auto &b) { return a.second < b.second; });
                    row.dx = best->first.first;
                    row.dy = best->first.second;
                }
            }
            frames.push_back(frame);
        }
        if (!done)
            throw std::runtime_error("ORIJINARU effect extraction exceeded its frame bound");
        IndexedImage atlas{224 * 8, int((frames.size() + 7) / 8) * 64, {}};
        atlas.pixels.assign(size_t(atlas.width * atlas.height), 0);
        for (size_t f = 0; f < frames.size(); ++f) {
            for (int y = 0; y < 64; ++y) {
                const auto &r = frames[f].rows[size_t(y)];
                int last = y;
                while (last + 1 < 64) {
                    const auto &b = frames[f].rows[size_t(last + 1)];
                    if (r.dx != b.dx || r.dy != b.dy || r.fill != b.fill || r.colors != b.colors)
                        break;
                    ++last;
                }
                csv += std::to_string(effect) + "," + std::to_string(f) + "," + std::to_string(y) + "," +
                       std::to_string(last) + "," + std::to_string(r.dx) + "," + std::to_string(r.dy) + "," +
                       std::to_string(r.fill);
                for (int c : r.colors)
                    csv += "," + std::to_string(c);
                csv += "\n";
                y = last;
            }
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 224; ++x)
                    atlas.pixels[(f / 8 * 64 + size_t(y)) * size_t(atlas.width) + f % 8 * 224 + size_t(x)] =
                        frames[f].sprites[size_t(y * 224 + x)];
        }
        out["gb/effect_" + std::to_string(effect) + ".png"] =
            encode_indexed_png(atlas, {0xFF00FF, 0xFFFFFF, 0xAAAAAA, 0x555555, 0x000000});
        std::fprintf(stderr, "GB effect %02X: %zu original frames\n", effect, frames.size());
    }
    out["gb/effect_rows.csv"] = {csv.begin(), csv.end()};
    return out;
}
} // namespace sawgod::game
