// Allocation facts used by the presentation rules; no screenshot comparisons.
#include "game/bank35_field.h"
#include "game/bank36_battle.h"
#include <doctest/doctest.h>

using namespace sawgod;
TEST_CASE("enhanced facts: every ordinary NPC allocation stays in tiles 018-097") {
    for (int walk : {0, 1})
        for (int palette = 0; palette < 8; ++palette) {
            emu::Wsc w(std::vector<uint8_t>(0x400000), 0x10000);
            game::Machine m(w);
            w.io[0x60] = 0x80;
            m.ss() = 0;
            m.cs() = 0x5000;
            w.ram[0xD500] = uint8_t(walk);
            for (unsigned slot = 0; slot < 16; ++slot) {
                const auto record = uint16_t(0xD5AE + slot * 20);
                m.wr8(0, uint16_t(record + 17), uint8_t(0x10 | palette)); // visible; skip terrain speed
            }
            game::advance_and_draw_npcs_core(m);
            REQUIRE(w.ram[0xC80F] == 64);
            for (unsigned slot = 0; slot < 64; ++slot) {
                const auto tile = m.rd16(0, uint16_t(0xC000 + slot * 4)) & 511;
                CHECK(tile >= 0x18);
                CHECK(tile < 0x98);
                CHECK(tile == 0x18 + (slot / 4) * 8 + walk * 4 + slot % 4);
            }
        }
}
TEST_CASE("enhanced facts: native battle status icons occupy OAM entries 11-26") {
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    game::Machine m(w);
    m.ss() = 0;
    m.cs() = 0x6000;
    w.io[0x60] = 0x80;
    m.sp() = 0xF000;
    // The public far wrapper returns here after writing all four members' status icons.
    m.wr16(0, m.sp(), 0);
    m.wr16(0, uint16_t(m.sp() + 2), 0x6000);
    for (unsigned slot = 0; slot < 128 * 4; ++slot)
        w.ram[0xC000 + slot] = 0xA5;
    for (unsigned member = 0; member < 4; ++member)
        w.ram[0xD71A + member * 32 + 2] = 4; // poisoned: each icon is drawn
    m.ax() = 1;
    game::draw_battle_status_sprites(m);
    for (unsigned slot = 0; slot < 128; ++slot) {
        const auto entry = m.rd16(0, uint16_t(0xC000 + slot * 4));
        if (slot >= 11 && slot < 27)
            CHECK(entry != 0xA5A5);
        else
            CHECK(entry == 0xA5A5);
    }
}
