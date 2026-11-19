#pragma once

#include "app/audio_latency.h"
#include "app/enhanced_music.h"
#include "app/ff_audio.h"
#include "app/host_settings.h"
#include "app/sound_mix.h"
#include "emu/wsc.h"
#include "engine/config.h"
#include "game/bank34.h"
#include "game/bank34_misc.h"
#include "game/bank3d_sound.h"
#include "game/session.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <doctest/doctest.h>
#include <fstream>
#include <iterator>
#include <map>
#include <thread>
#include <tuple>
using namespace sawgod;

namespace {
constexpr std::array locked_songs{135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145,
                                  146, 147, 148, 149, 150, 151, 152, 153, 154, 163, 178};
inline std::vector<uint8_t> music_rom() {
    const char *path = std::getenv("SAWGOD_WSC_ROM");
    if (!path)
        return {};
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
inline std::string music_sf() {
    const char *path = std::getenv("SAWGOD_SOUNDFONT");
    return path ? path : app::default_soundfont();
}
inline void music_request(emu::Wsc &w, int id, bool immediate = false) {
    const auto saved = w.cpu.s;
    game::Machine m(w);
    m.ds() = 0;
    m.cs() = 0xD000;
    m.ax() = uint16_t(id);
    if (immediate)
        game::play_sound_effect_core(m, uint8_t(id));
    else
        game::sound_request_core(m);
    w.cpu.s = saved;
}
inline void music_init(emu::Wsc &w) {
    const auto saved = w.cpu.s;
    game::Machine m(w);
    m.ds() = 0;
    m.cs() = 0xD000;
    game::sound_init(m);
    w.cpu.s = saved;
}
inline void music_save_load(emu::Wsc &w) {
    game::Session::of(w).save_slots().attach_store();
    const auto saved = w.cpu.s;
    const std::vector<uint8_t> driver(w.ram.begin() + 0x600, w.ram.begin() + 0x7BC);
    game::Machine m(w);
    m.cs() = 0x4000;
    m.ss() = 0;
    m.ax() = 0;
    game::write_save_slot_core(m);
    REQUIRE(game::Session::of(w).save_slots().has(0));
    const uint8_t data = w.ram[0xD100];
    w.ram[0xD100] ^= 1;
    m.ax() = 0;
    m.di() = 0xD000;
    game::load_save_slot_core(m);
    CHECK((m.flags() & emu::CF) == 0);
    CHECK(w.ram[0xD100] == data);
    CHECK(std::equal(driver.begin(), driver.end(), w.ram.begin() + 0x600));
    w.cpu.s = saved;
}
inline void check_hardware(const emu::Wsc &a, const emu::Wsc &b) {
    CHECK(a.ram == b.ram);
    CHECK(a.io == b.io);
    CHECK(std::equal(std::begin(a.cpu.s.r), std::end(a.cpu.s.r), std::begin(b.cpu.s.r)));
    CHECK(std::equal(std::begin(a.cpu.s.seg), std::end(a.cpu.s.seg), std::begin(b.cpu.s.seg)));
    CHECK(a.cpu.s.ip == b.cpu.s.ip);
    CHECK(a.cpu.s.flags == b.cpu.s.flags);
    const auto x = a.apu.state(), y = b.apu.state();
    for (int ch = 0; ch < 4; ++ch) {
        CHECK(x.counter[ch] == y.counter[ch]);
        CHECK(x.index[ch] == y.index[ch]);
    }
    CHECK(x.lfsr == y.lfsr);
    CHECK(x.sweep_cycles == y.sweep_cycles);
    CHECK(x.sweep_ticks == y.sweep_ticks);
    CHECK(x.sample_cycles == y.sample_cycles);
    CHECK(x.out_left == y.out_left);
    CHECK(x.out_right == y.out_right);
    // The DC filters are presentation state and legitimately differ when their input is muted.
}
} // namespace
