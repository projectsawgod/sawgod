#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

#include "game/bank35_field.h"

using namespace sawgod;

TEST_CASE("field NPC sprite allocation matches the ROM for terrain and visibility flags") {
    const char *path = std::getenv("SAWGOD_WSC_ROM");
    if (!path) {
        MESSAGE("ROM comparison skipped: set SAWGOD_WSC_ROM");
        return;
    }
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    const std::vector<uint8_t> rom((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    for (uint8_t flags : {0x00, 0x10, 0x20, 0x30, 0x08, 0x51}) {
        for (uint8_t terrain : {0x00, 0x20, 0x40, 0x60}) {
            for (uint16_t frame : {0, 1, 8, 9}) {
                CAPTURE(flags);
                CAPTURE(terrain);
                CAPTURE(frame);
                emu::Wsc original(rom, 0x10000), native(rom, 0x10000);
                game::Machine setup(original);
                original.io[0x60] = 0x80; // color RAM and GDMA
                original.io[0xC1] = 0;
                original.cpu.s.seg[emu::CS] = 0x5000;
                original.cpu.s.seg[emu::SS] = 0;
                original.cpu.s.ip = 0x297E;
                original.cpu.s.r[emu::SP] = 0xE000;
                original.cpu.s.flags = emu::DF; // no interrupts while directly executing the ROM routine
                setup.wr16(0, 0xE000, 0x1234);
                setup.wr16(0, 0xE002, 0x5000);
                setup.wr16(0, 0xC80C, frame);
                setup.wr8(0, 0xC80F, 4); // player's four sprites precede the NPCs
                setup.wr8(0, 0xD57B, terrain);
                for (int slot = 0; slot < 16; ++slot)
                    setup.wr8(0, uint16_t(0xD5AE + slot * 0x14), 0xFF);
                // A flagged NPC followed by an ordinary NPC: a missing allocation overwrites the first.
                for (int slot = 0; slot < 2; ++slot) {
                    const uint16_t npc = uint16_t(0xD5AE + slot * 0x14);
                    setup.wr8(0, npc, 0);
                    setup.wr16(0, uint16_t(npc + 6), uint16_t((8 + slot) * 32));
                    setup.wr16(0, uint16_t(npc + 8), 4 * 32);
                    setup.wr8(0, uint16_t(npc + 0x11), slot == 0 ? flags : 0);
                }
                native.restore(original.snapshot());
                for (int steps = 0; original.cpu.s.ip != 0x1234 && steps < 5000; ++steps)
                    original.cpu.step();
                REQUIRE(original.cpu.s.ip == 0x1234);
                game::Machine m(native);
                game::advance_and_draw_npcs(m);
                CHECK(std::equal(std::begin(native.cpu.s.r), std::end(native.cpu.s.r),
                                 std::begin(original.cpu.s.r)));
                CHECK(std::equal(std::begin(native.cpu.s.seg), std::end(native.cpu.s.seg),
                                 std::begin(original.cpu.s.seg)));
                CHECK(native.cpu.s.ip == original.cpu.s.ip);
                CHECK((native.cpu.s.flags & (emu::DF | emu::IF)) ==
                      (original.cpu.s.flags & (emu::DF | emu::IF)));
                const bool visible = !(flags & 0x08) && !((flags & 0x20) && (frame & 8));
                CHECK(native.terrain_cards[1].veiled == (visible && !(flags & 0x10) && (terrain & 0x20)));
                CHECK(native.terrain_cards[2].veiled == bool(terrain & 0x20));
                CHECK(native.io == original.io);
                CHECK(native.sram == original.sram);
                // The original's pushes leave dead stack bytes; compare every other RAM byte.
                for (size_t addr = 0; addr < native.ram.size(); ++addr) {
                    if (addr >= 0xDF80 && addr < 0xE000)
                        continue;
                    if (native.ram[addr] != original.ram[addr]) {
                        CAPTURE(addr);
                        CHECK(native.ram[addr] == original.ram[addr]);
                        break;
                    }
                }
                if (flags == 0x10 && terrain == 0) {
                    CHECK(native.ram[0xC80F] == 12);
                    CHECK(m.rd16(0, 0xC010) == 0x18);
                    CHECK(m.rd16(0, 0xC020) == 0x20);
                }
            }
        }
    }
}
