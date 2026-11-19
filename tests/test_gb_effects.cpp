#include "test_gb_effects_support.h"

TEST_CASE("GB effects retain their own picture through every native battle animation without changing the "
          "machine") {
    const char *path = std::getenv("SAWGOD_WSC_ROM");
    if (!path) {
        MESSAGE("set SAWGOD_WSC_ROM for animation proof");
        return;
    }
    std::ifstream input(path, std::ios::binary);
    const std::vector<uint8_t> rom{std::istreambuf_iterator<char>(input), {}};
    REQUIRE(rom.size() == 0x400000);
    struct Case {
        int entry;
        battle_test::Body body;
    };
    const Case cases[] = {
        {0x370160, game::play_hit_sprite_effect_core},
        {0x370360, game::play_impact_sprite_effect_core},
        {0x370570, game::play_battle_effect_3_core},
        {0x370770, game::play_bow_animation_core},
        {0x3709A0, game::play_firearm_animation_core},
        {0x370BC0, game::play_whip_animation_core},
        {0x370DC0, game::play_missile_projectile_animation_core},
        {0x371060, game::play_explosive_blast_animation_core},
        {0x371270, game::play_missile_trails_animation_core},
        {0x371500, game::play_destructive_ray_animation_core},
        {0x371700, game::play_lightsaber_animation_core},
        {0x371900, game::play_battle_effect_4_core},
        {0x371B00, game::play_battle_effect_5_core},
        {0x371D00, game::play_chainsaw_animation_core},
        {0x371F00, game::play_spinning_strike_animation_core},
        {0x372110, game::play_automatic_gun_animation_core},
        {0x372300, game::play_wave_cannon_animation_core},
        {0x372690, game::play_thrown_rock_animation_core},
        {0x372870, game::play_rock_impact_animation_core},
        {0x372A70, game::play_life_drain_animation_core},
        {0x372C80, game::play_battle_effect_6_core},
        {0x372E80, game::play_psychoblast_animation_core},
        {0x373090, game::play_flame_animation_core},
        {0x373370, game::play_blizzard_animation_core},
        {0x373570, game::play_innate_lightning_animation_core},
        {0x373780, game::play_scripture_opening_animation_core},
        {0x373970, game::play_scripture_flash_animation_core},
        {0x373B50, game::play_flare_animation_core},
        {0x374040, game::play_earthquake_animation_core},
        {0x374140, game::play_whirlpool_sprites_animation_core},
        {0x374300, game::play_whirlpool_background_animation_core},
        {0x374410, game::play_tornado_animation_core},
        {0x3745B0, game::play_sandtrap_animation_core},
        {0x3747D0, game::play_acid_rain_droplets_animation_core},
        {0x374980, game::play_acid_rain_splash_animation_core},
        {0x374B10, game::play_self_destruct_animation_core},
        {0x374F00, game::play_riddle_opening_animation_core},
        {0x375100, game::play_riddle_flash_animation_core},
        {0x3752F0, game::play_riddle_fade_animation_core},
        {0x375490, game::play_hypnotic_gaze_animation_core},
        {0x3756E0, game::play_hypnotic_gaze_portraits_animation_core},
        {0x375930, game::play_song_animation_core},
        {0x375B40, game::play_entangle_animation_core},
        {0x375D40, game::play_web_animation_core},
        {0x375F40, game::play_battle_effect_7_core},
        {0x376140, game::play_liquid_jet_animation_core},
        {0x376350, game::play_formation_tremble_animation_core},
        {0x3765E0, game::play_telekinesis_animation_core},
        {0x376810, game::play_curse_eye_animation_core},
        {0x376A80, game::play_death_animation_core},
        {0x376C80, game::play_healing_glow_animation_core},
        {0x376ED0, game::play_defense_increase_animation_core},
        {0x377140, game::play_protective_aura_animation_core},
        {0x377440, game::play_revenge_sword_animation_core},
        {0x377660, game::play_glass_sword_shatter_animation_core},
        {0x377860, game::play_decisive_slash_animation_core},
        {0x377A6E, game::play_nuclear_bomb_animation_core},
        {0x377FC0, game::play_formation_palette_effect_core},
        {0x378130, game::play_spell_casting_animation_core},
        {0x378270, game::play_full_healing_animation_core},
        {0x3784B0, game::play_cloud_spell_opening_animation_core},
        {0x3786A0, game::play_cloud_spell_flash_animation_core},
        {0x3788B0, game::play_thunder_spell_opening_animation_core},
        {0x378AA0, game::play_thunder_spell_flash_animation_core},
        {0x378CB0, game::play_cold_breath_animation_core},
        {0x378EA0, game::play_fire_spell_opening_animation_core},
        {0x379090, game::play_fire_spell_flash_animation_core},
        {0x379280, game::play_beam_rifle_animation_core},
        {0x379480, game::play_status_cure_animation_core},
        {0x3796C0, game::play_shield_animation_core},
        {0x379AE0, game::play_scripture_closing_animation_core},
    };
    for (const auto &c : cases) {
        CAPTURE(c.entry);
        battle_test::Driver off(rom, c.body), on(rom, c.body);
        game::GfxSwap swap;
        swap.set_enabled(on.w, true);
        // Distinct synthetic GB pixels: a shadow-only tile and palette, never ROM pixels.
        on.w.shadow_write16(0x1000, 1);
        for (int i = 0; i < 32; ++i)
            on.w.shadow_write8(uint16_t(0x4020 + i), 0x12);
        on.w.shadow_write16(0xFE02, 0xAAA);
        on.w.shadow_write16(0xFE04, 0x555);
        auto prime = [](emu::Wsc &w) {
            w.cpu.s.halted = true;
            w.cpu.s.flags = 0;
            w.run_frame();
            w.run_frame();
            w.cpu.s.halted = false;
        };
        prime(off.w);
        prime(on.w);
        const auto gb = on.w.framebuffer(true);
        int waits = 0;
        for (;;) {
            const bool a = off.step(), b = on.step();
            REQUIRE(a == b);
            CHECK(off.w.ram == on.w.ram);
            CHECK(off.w.io == on.w.io);
            CHECK(off.w.sram == on.w.sram);
            CHECK(off.w.cpu.cycles == on.w.cpu.cycles);
            CHECK(off.w.frame_count == on.w.frame_count);
            CHECK(off.w.cpu.s.ip == on.w.cpu.s.ip);
            CHECK(off.w.cpu.s.flags == on.w.cpu.s.flags);
            CHECK(std::equal(std::begin(off.w.cpu.s.r), std::end(off.w.cpu.s.r), std::begin(on.w.cpu.s.r)));
            CHECK(std::equal(std::begin(off.w.cpu.s.seg), std::end(off.w.cpu.s.seg),
                             std::begin(on.w.cpu.s.seg)));
            CHECK(off.w.framebuffer() == on.w.framebuffer());
            if (!a)
                break;
            ++waits;
            CHECK(on.w.framebuffer(true) == gb);
            REQUIRE(on.w.frame_layers(true));
            REQUIRE(off.w.frame_layers(false));
            swap.frame(on.w);
        }
        CHECK(waits > 0);
        swap.frame(on.w);
        swap.set_enabled(on.w, false);
        CHECK_FALSE(on.w.second());
    }
}

TEST_CASE("GB battlefield ownership survives native palette and tilemap replacement") {
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    w.io[0x60] = 0xE0;
    w.io[0] = 3;
    w.io[7] = 0x32;
    w.cpu.s.halted = true;
    w.cpu.s.flags = 0;
    game::GfxSwap swap;
    IndexedImage image;
    image.width = 512;
    image.height = 64;
    image.pixels.assign(512 * 64, 0);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            image.pixels[size_t(y * 512 + x)] = 4;
    std::string error;
    REQUIRE(swap.add_monsters(encode_indexed_png(image, {0xFF00FF, 0xFFFFFF, 0xAAAAAA, 0x555555, 0x000000}),
                              "pic,width,height\n0,1,1\n", "type,pic\n0,0\n", &error));
    swap.set_enabled(w, true);
    game::Machine m(w);
    game::hooks::emit(game::hooks::Event::BattleStart, m);
    w.ram[0xD509] = w.ram[0xD50A] = w.ram[0xD6D4] = 1;
    game::Machine::enemy_portrait_hook(m, 0, 0x0101);
    const auto before = w.ram;
    w.run_frame();
    w.run_frame();
    CHECK(w.ram == before);
    CHECK(w.framebuffer(true)[0] == 0xFFF);
    CHECK(w.framebuffer(true)[4 * 224 + 8] == 0);
    // The WSC can replace every palette and its portrait's map words mid-action.
    std::fill(w.ram.begin() + 0xFE00, w.ram.end(), 0x0F);
    w.ram[0x1842] = 0xFF;
    w.ram[0x1843] = 0xFF;
    swap.frame(w);
    w.run_frame();
    CHECK(w.framebuffer(true)[0] == 0xFFF);
    CHECK(w.framebuffer(true)[4 * 224 + 8] == 0);
    CHECK(w.framebuffer()[0] != w.framebuffer(true)[0]);
    // The boss replay resumes actions after a rewards handoff. Rewards still use
    // the battle layout, so releasing this picture would expose WSC art/palettes.
    game::hooks::emit(game::hooks::Event::BattleRewards, m);
    swap.frame(w);
    game::hooks::emit(game::hooks::Event::BattleAnimation, m, {0x370160, 0, 0x21});
    w.run_frame();
    CHECK_FALSE(w.second_scene_image.empty());
    CHECK(w.framebuffer(true)[0] == 0xFFF);
    CHECK(w.framebuffer(true)[4 * 224 + 8] == 0);
    game::hooks::emit(game::hooks::Event::BattleAnimationEnd, m, {0x370160});
    swap.frame(w);
    // Native defeat scroll/window registers also move the style-owned GB portrait.
    game::hooks::emit(game::hooks::Event::BattleDefeatEffect, m, {1, 0});
    w.io[0] = 0x23;
    w.io[8] = 0;
    w.io[9] = 0;
    w.io[10] = 18;
    w.io[11] = 79;
    w.io[0x10] = 4;
    w.io[0x11] = 252;
    w.io[0x12] = 252;
    w.io[0x13] = 4;
    const auto defeat_ram = w.ram;
    const auto defeat_io = w.io;
    swap.frame(w);
    w.run_frame();
    CHECK(w.framebuffer(true)[4 * 224 + 4] == 0xFFF); // no copy before the native plane copy
    game::hooks::emit(game::hooks::Event::BattleDefeatEffect, m, {3, 12});
    w.run_frame();
    CHECK(w.framebuffer(true)[4 * 224 + 4] == 0);      // SCR1 moves the left portion left four pixels
    CHECK(w.framebuffer(true)[4 * 224 + 8] == 0xFFF);  // the right portion is absent from SCR1
    CHECK(w.framebuffer(true)[4 * 224 + 12] == 0xFFF); // the left portion is absent from SCR2
    CHECK(w.framebuffer(true)[4 * 224 + 16] == 0);     // SCR2 moves the right portion right four pixels
    CHECK(w.framebuffer(true)[4 * 224 + 19] == 0xFFF); // native window clips the right portion
    CHECK(w.ram == defeat_ram);
    CHECK(w.io == defeat_io);
    game::hooks::emit(game::hooks::Event::BattleDefeatEffect, m, {2});
    swap.frame(w);
    w.run_frame();
    CHECK(w.framebuffer(true)[4 * 224 + 4] == 0xFFF); // native plane clear precedes palette restoration
    const auto palettes = w.palette_swaps;
    REQUIRE(w.shadowed(0xA000)); // the portrait allocation still supplies reward/UI palette mappings
    game::hooks::emit(game::hooks::Event::BattleDefeatEffect, m, {0, 0});
    swap.frame(w);
    w.run_frame();
    CHECK(w.shadowed(0xA000));
    REQUIRE(w.palette_swaps.size() == palettes.size());
    for (size_t i = 0; i < palettes.size(); ++i) {
        CHECK(w.palette_swaps[i].from == palettes[i].from);
        CHECK(w.palette_swaps[i].to == palettes[i].to);
    }
    CHECK(std::all_of(w.second_scene_image.begin(), w.second_scene_image.end(),
                      [](auto pixel) { return pixel == 0xFFF; })); // the defeated portrait stays gone
    game::hooks::emit(game::hooks::Event::BattleEnd, m);
    CHECK(w.second_scene_image.empty());
}

TEST_CASE("GB party hit motion uses the original vertical split without moving the enemy field") {
    emu::Wsc w({}, 0);
    std::vector<uint32_t> rows(224 * 144);
    for (int y = 0; y < 144; ++y)
        std::fill_n(rows.begin() + y * 224, 224, uint32_t(y));
    w.second_motion_y = {8, -8, -8, 8, 8, -8, -8, 8, 8, -8, -8, 8, 8, -8, -8, 8};
    w.second_motion_start = 10;
    w.frame_count = 11;
    auto up = rows;
    w.move_second_picture(up);
    CHECK(up[71 * 224] == 71);
    CHECK(up[72 * 224] == 80);
    CHECK(up[135 * 224] == 143);
    CHECK(up[136 * 224] == 0xFFFFFF);
    w.frame_count = 12;
    auto down = rows;
    w.move_second_picture(down);
    CHECK(down[87 * 224] == 87);
    CHECK(down[88 * 224] == 80);
    CHECK(down[143 * 224] == 135);
    w.frame_count = 27;
    auto after = rows;
    w.move_second_picture(after);
    CHECK(after == rows);
}

TEST_CASE("battle effect observations identify the acting action rather than the defender") {
    const char *path = std::getenv("SAWGOD_WSC_ROM");
    if (!path)
        return;
    std::ifstream input(path, std::ios::binary);
    const std::vector<uint8_t> rom{std::istreambuf_iterator<char>(input), {}};
    REQUIRE(rom.size() == 0x400000);
    battle_test::Driver driver(rom, game::play_spell_casting_animation_core, 0, 0x6A);
    driver.w.ram[0xDBFD] = 0x8C; // an enemy's selected Bite, not the player's Fire book
    int observed = -1;
    auto token = game::Session::of(driver.w).hooks().subscribe(
        game::hooks::Event::BattleAnimation,
        [&](game::Machine &, const game::hooks::Args &a) { observed = a.c; });
    while (driver.step()) {
    }
    CHECK(observed == 0x6A);
}
