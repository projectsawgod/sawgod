// Accelerated fade review: real no-input boot, or an isolated driver fade with supplied parameters.
#include "app/enhanced_music.h"
#include "app/gb_music.h"
#include "app/sound_mix.h"
#include "game/bank3d_sound.h"
#include "game/session.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

using namespace sawgod;
namespace {
int survey(const std::vector<uint8_t> &rom) {
    std::puts("song,last_audible,last_active,longest_active_silence");
    for (int song = 135; song <= 178; ++song) {
        if (!app::gb_song_map(uint8_t(song)) && !app::enhanced_song(song))
            continue;
        emu::Wsc w(rom, 0);
        game::Machine m(w);
        m.ds() = 0;
        m.cs() = 0xD000;
        game::sound_init(m);
        m.ax() = uint16_t(song);
        game::sound_request_core(m);
        int last_audible = -1, last_active = -1, quiet = 0, longest = 0;
        for (int frame = 0; frame < 25000; ++frame) {
            app::paused_tick(w);
            w.apu.take();
            bool active = false, audible = false;
            for (int slot = 0; slot < 8; ++slot) {
                const size_t b = size_t(0x62C + slot * 0x32);
                if ((w.ram[b] | w.ram[b + 1] << 8) == 0xFFFF)
                    continue;
                active = true;
                audible |= w.io[size_t(0x88 + (w.ram[b + 2] & 3))] != 0;
            }
            if (audible)
                last_audible = frame;
            if (active)
                last_active = frame;
            quiet = active && !audible ? quiet + 1 : 0;
            longest = std::max(longest, quiet);
            if (!active)
                break;
        }
        std::printf("%d,%d,%d,%d\n", song, last_audible, last_active, longest);
    }
    return 0;
}
} // namespace
int main(int argc, char **argv) {
    if (argc != 3 && argc != 5 && argc != 8 && argc != 11 && argc != 13)
        return 2; // ROM, soundfont, prefix, boot[:first:count] | song, interval, mode, fade frame,
                  // optionally request, request frame, count, and a second request/frame pair.
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<uint8_t> rom{std::istreambuf_iterator<char>(in), {}};
    if (rom.size() != 0x400000)
        return 2;
    if (argc == 3)
        return std::string(argv[2]) == "survey" ? survey(rom) : 2;
    emu::Wsc w(rom, 0);
    app::GbMusic gb;
    if (!gb.load(rom))
        return 1;
    gb.attach(game::Session::of(w));
    app::EnhancedObserver observer;
    observer.attach(game::Session::of(w));
    app::EnhancedRenderer renderer(argv[2]);
    if (!renderer.available())
        return 1;
    const bool boot = argc == 5;
    int first = boot ? 4100 : 0, count = boot ? 5230 : std::atoi(argv[7]) + 380;
    if (boot)
        std::sscanf(argv[4], "boot:%d:%d", &first, &count);
    if (argc >= 11)
        count = std::atoi(argv[10]);
    if (boot)
        game::install(w);
    else {
        game::Machine m(w);
        const auto saved = w.cpu.s;
        m.ds() = 0;
        m.cs() = 0xD000;
        game::sound_init(m);
        m.ax() = uint16_t(std::atoi(argv[4]));
        game::sound_request_core(m);
        w.cpu.s = saved;
    }
    const std::string prefix = argv[3];
    FILE *wsc = std::fopen((prefix + "_wsc.pcm").c_str(), "wb");
    FILE *gameboy = std::fopen((prefix + "_gb.pcm").c_str(), "wb");
    FILE *enhanced = std::fopen((prefix + "_enhanced.pcm").c_str(), "wb");
    FILE *trace = std::fopen((prefix + "_driver.csv").c_str(), "w");
    if (!wsc || !gameboy || !enhanced || !trace)
        return 1;
    std::fprintf(
        trace, "frame,mode,progress,slot,step,volume,original,target,envelope,ceiling,port,pitch,samples\n");
    std::vector<app::EnhancedFrame> frames;
    for (int i = 0; i < count; ++i) {
        const int mode = boot ? 0 : std::atoi(argv[6]);
        const int fade_frame = boot ? 0 : std::atoi(argv[7]);
        if (!boot && mode != 0 && (i == fade_frame || (mode == 1 && i == fade_frame + 120))) {
            game::Machine m(w);
            const auto saved = w.cpu.s;
            m.ds() = 0;
            // Mode one's scene caller restores a preceding mode-two fade.
            m.ax() = uint16_t(mode == 1 && i == fade_frame ? 2 : mode);
            m.cx() = uint16_t((std::atoi(argv[5]) << 8) | 1);
            game::sound_channel_reset_entry_core(m);
            w.cpu.s = saved;
        }
        if ((argc >= 11 && i == std::atoi(argv[9])) || (argc == 13 && i == std::atoi(argv[12]))) {
            game::Machine m(w);
            const auto saved = w.cpu.s;
            m.ds() = 0;
            m.ax() = uint16_t(std::atoi(argv[argc == 13 && i == std::atoi(argv[12]) ? 11 : 8]));
            game::sound_request_core(m);
            w.cpu.s = saved;
        }
        if (boot)
            w.run_frame();
        else
            app::paused_tick(w);
        auto a = w.apu.take();
        std::vector<int16_t> b(a.size());
        gb.frame(w, b, 1);
        auto f = observer.frame(w);
        f.samples.assign(a.size(), 0);
        f.gain = f.cue_gain = 1;
        frames.push_back(std::move(f));
        if (i >= first) {
            std::fwrite(a.data(), sizeof(int16_t), a.size(), wsc);
            std::fwrite(b.data(), sizeof(int16_t), b.size(), gameboy);
        }
        for (int slot = 4; slot < 8; ++slot) {
            const int base = 0x62C + slot * 0x32, hw = w.ram[size_t(base + 2)] & 3;
            auto word = [&](int at) { return w.ram[size_t(at)] | w.ram[size_t(at + 1)] << 8; };
            std::fprintf(trace, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%zu\n", i, w.ram[0x624], w.ram[0x625],
                         slot, word(base), w.ram[size_t(base + 8)], w.ram[size_t(base + 0x2C)],
                         w.ram[size_t(base + 0x2D)], w.ram[size_t(base + 0x28)], w.ram[size_t(base + 0x12)],
                         w.io[size_t(0x88 + hw)], word(base + 0x10), a.size() / 2);
        }
    }
    for (size_t i = 0; i < frames.size(); ++i) {
        renderer.mix(frames[i], std::span<const app::EnhancedFrame>(frames).subspan(
                                    i + 1, std::min<size_t>(8, frames.size() - i - 1)));
        if (int(i) >= first)
            std::fwrite(frames[i].samples.data(), sizeof(int16_t), frames[i].samples.size(), enhanced);
    }
    std::fclose(wsc);
    std::fclose(gameboy);
    std::fclose(enhanced);
    std::fclose(trace);
}
