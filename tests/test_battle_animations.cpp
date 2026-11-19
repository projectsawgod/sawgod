#include "battle_animation_support.h"
#include <cstdlib>
#include <doctest/doctest.h>
#include <fstream>
#include <iterator>
using namespace sawgod;
TEST_CASE("pending battle switches preserve every native raster RAM register and VBlank") {
    const char *path = std::getenv("SAWGOD_WSC_ROM");
    if (!path) {
        MESSAGE("set SAWGOD_WSC_ROM for native animation comparison");
        return;
    }
    std::ifstream input(path, std::ios::binary);
    const std::vector<uint8_t> rom{std::istreambuf_iterator<char>(input), {}};
    REQUIRE(rom.size() == 0x400000);
    struct Case {
        battle_test::Body body;
        int palette, waits;
    };
    // Native animation lengths, including the final sprite-hide VBlank.
    for (const Case c : {Case{game::play_blizzard_animation_core, 0, 85},
                         {game::play_thunder_spell_flash_animation_core, 0, 127},
                         {game::play_cloud_spell_flash_animation_core, 0, 65},
                         {game::play_battle_effect_7_core, 1, 58},
                         {game::play_battle_effect_7_core, 4, 58},
                         {game::play_death_animation_core, 4, 40},
                         {game::play_spell_casting_animation_core, 0, 57},
                         {game::play_spell_casting_animation_core, 1, 57},
                         {game::play_spell_casting_animation_core, 4, 57},
                         {game::play_fire_spell_opening_animation_core, 0, 49},
                         {game::play_thunder_spell_opening_animation_core, 0, 49},
                         {game::play_cloud_spell_opening_animation_core, 0, 29}}) {
        battle_test::Driver off(rom, c.body, c.palette), on(rom, c.body, c.palette);
        off.observer.battle_animations({});
        on.observer.battle_animations({true, true, true, true, true, true});
        int lit_frames = 0;
        for (;;) {
            const bool a = off.step(), b = on.step();
            REQUIRE(a == b);
            CHECK(off.w.ram == on.w.ram);
            CHECK(off.w.io == on.w.io);
            CHECK(off.w.sram == on.w.sram);
            CHECK(std::equal(std::begin(off.w.cpu.s.r), std::end(off.w.cpu.s.r), std::begin(on.w.cpu.s.r)));
            CHECK(std::equal(std::begin(off.w.cpu.s.seg), std::end(off.w.cpu.s.seg),
                             std::begin(on.w.cpu.s.seg)));
            CHECK(off.w.cpu.s.flags == on.w.cpu.s.flags);
            CHECK(off.w.cpu.s.ip == on.w.cpu.s.ip);
            CHECK(off.w.cpu.cycles == on.w.cpu.cycles);
            CHECK(off.w.frame_count == on.w.frame_count);
            CHECK(off.w.framebuffer() == on.w.framebuffer());
            if (!a)
                break;
            const auto ram = on.w.ram, io_ram = off.w.ram;
            const auto io = on.w.io;
            const auto plain = off.observer.describe(off.w, true, false, false);
            const auto enhanced = on.observer.describe(on.w, true, false, false);
            CHECK(plain.lights.empty());
            int changed_non_source = 0, changed_ui = 0;
            const auto *layers = on.w.frame_layers(false, false);
            REQUIRE(layers);
            for (size_t at = 0; at < plain.mask.size(); ++at) {
                const bool changed =
                    plain.mask[at] != enhanced.mask[at] || plain.background[at] != enhanced.background[at];
                const auto &top = layers->top(at);
                const bool source = &top == &layers->planes[emu::FrameLayers::SpriteFront][at] ||
                                    &top == &layers->planes[emu::FrameLayers::SpriteBehind][at];
                changed_non_source += changed && !source;
                changed_ui += changed && at >= 80 * 224;
            }
            CHECK(changed_non_source == 0); // only effect art gains exact restoration / bloom exclusion
            CHECK(changed_ui == 0);
            CHECK(enhanced.lights.size() <= 256); // no source patches silently lost on the GPU
            lit_frames += !enhanced.lights.empty();
            CHECK(on.w.ram == ram);
            CHECK(off.w.ram == io_ram);
            CHECK(on.w.io == io);
        }
        CAPTURE(c.palette);
        CAPTURE(off.waits);
        CAPTURE(c.waits);
        CHECK(off.waits == c.waits);
        CHECK(lit_frames > 0);
    }
}
