#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

#include "game/bank3d_sound.h"

using namespace sawgod;

TEST_CASE("sound driver: shared portamento register results match the ROM through sound_tick") {
    const char *path = std::getenv("SAWGOD_WSC_ROM");
    if (!path) {
        MESSAGE("Set SAWGOD_WSC_ROM for the sound driver oracle check");
        return;
    }
    std::ifstream in(path, std::ios::binary);
    const std::vector<uint8_t> rom{std::istreambuf_iterator<char>(in), {}};
    REQUIRE(rom.size() == 0x400000);
    emu::Wsc original(rom, 0), native(rom, 0);
    const auto clean = original.snapshot();
    constexpr uint16_t kChannels = 0x62C, kStride = 0x32;
    struct Glide {
        uint8_t mode, pitch, step, cap, progress, owners;
        uint16_t countdown;
    };
    // Include below/equal/above the cap, byte overflow/underflow, accumulated ownership,
    // the last owner's reset, a pending countdown, and already-completed/inactive glides.
    for (const Glide c :
         {Glide{1, 0, 1, 10, 0, 15, 1}, Glide{1, 9, 1, 10, 0, 15, 1}, Glide{1, 10, 3, 10, 0x10, 15, 1},
          Glide{1, 255, 1, 10, 0, 15, 1}, Glide{1, 10, 1, 10, 0x70, 15, 1}, Glide{1, 9, 1, 10, 0, 15, 2},
          Glide{1, 9, 1, 10, 0xF0, 15, 1}, Glide{2, 10, 1, 9, 0, 15, 1}, Glide{2, 10, 1, 10, 2, 15, 1},
          Glide{2, 0, 1, 0, 0, 15, 1}, Glide{2, 10, 1, 0, 0, 15, 2}, Glide{0, 10, 1, 10, 0, 15, 1}}) {
        for (int idx = 3; idx < 8; ++idx) {
            for (bool tick : {false, true}) {
                CAPTURE(int(c.mode));
                CAPTURE(int(c.pitch));
                CAPTURE(int(c.step));
                CAPTURE(int(c.cap));
                CAPTURE(int(c.progress));
                CAPTURE(int(c.owners));
                CAPTURE(c.countdown);
                CAPTURE(idx);
                CAPTURE(tick);
                original.restore(clean);
                game::Machine m(original);
                m.cs() = 0xD000;
                original.cpu.s.ip = tick ? 0xC825 : 0xCCFE;
                m.ss() = m.ds() = 0;
                m.sp() = 0x2000;
                m.wr16(0, 0x2000, 0x1234);
                m.wr16(0, 0x2002, 0x4000);
                m.ax() = 0xABCD;
                m.cx() = 0xBEEF;
                m.di() = 0xCAFE;
                m.flags() = emu::DF;
                for (int channel = 0; channel < 8; ++channel)
                    m.wr16(0, uint16_t(kChannels + channel * kStride), 0xFFFF);
                // sound_tick starts at the first physical slot, with the logical index from 0x60B.
                const uint16_t slot = tick ? kChannels : uint16_t(kChannels + idx * kStride);
                m.si() = slot;
                m.wr16(0, slot, 2);
                m.wr8(0, slot + 2, 0); // hardware channel 0: no time-dependent LFSR read
                m.wr8(0, slot + 8, c.pitch);
                m.wr8(0, slot + 9, 10); // keep this note, without decoding a new track command
                m.wr16(0, slot + 0x10, 0x400);
                m.wr8(0, slot + 0x22, c.step);
                m.wr16(0, slot + 0x23, 0x2304);
                m.wr16(0, slot + 0x25, c.countdown);
                m.wr8(0, slot + 0x2C, c.cap);
                m.wr8(0, slot + 0x2D, c.cap);
                m.wr8(0, 0x605, uint8_t(idx));
                m.wr8(0, 0x60B, uint8_t(idx));
                m.wr8(0, 0x60E, c.owners);
                m.wr8(0, 0x624, c.mode);
                m.wr8(0, 0x625, c.progress);
                native.restore(original.snapshot());
                int steps = 0;
                while (original.cpu.s.seg[emu::CS] != 0x4000 && steps++ < 10000)
                    original.cpu.step();
                REQUIRE(steps < 10000);
                game::Machine n(native);
                if (tick)
                    game::sound_tick(n);
                else
                    game::sound_advance_shared_portamento(n);
                CHECK(std::equal(std::begin(original.cpu.s.r), std::end(original.cpu.s.r),
                                 std::begin(native.cpu.s.r)));
                CHECK(std::equal(std::begin(original.cpu.s.seg), std::end(original.cpu.s.seg),
                                 std::begin(native.cpu.s.seg)));
                CHECK(original.cpu.s.ip == native.cpu.s.ip);
                CHECK((original.cpu.s.flags & (emu::DF | emu::IF | emu::TF)) ==
                      (native.cpu.s.flags & (emu::DF | emu::IF | emu::TF)));
                // Exclude the scratch stack, whose push/pop writes are intentionally omitted by natives.
                CHECK(std::equal(original.ram.begin() + 0x600, original.ram.begin() + 0x97D,
                                 native.ram.begin() + 0x600));
                CHECK(original.io == native.io);
            }
        }
    }
}
