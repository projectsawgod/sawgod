#include "test_field_preview_support.h"

TEST_CASE("field preview uses the runtime field words and rejects invalid coordinates") {
    emu::Wsc w(fixture_rom(), 0);
    game::MapData map;
    map.mirror[0] = 1;
    REQUIRE(tools::prepare_field_preview(w, 1, map, 0, 0));
    CHECK(w.cpu.s.halted);
    CHECK(w.ram[0xD0DC] == 1);
    CHECK(w.ram[0x1000] == 1); // top-right source quadrant, mirrored horizontally
    CHECK(w.ram[0x1001] == 0x60);
    CHECK_FALSE(tools::prepare_field_preview(w, 1, map, 51, 0));
    CHECK_FALSE(tools::prepare_field_preview(w, 1, map, 0, -1));
    CHECK_FALSE(tools::prepare_field_preview(w, 211, map, 0, 0));
}

TEST_CASE("field swap loads independent map resources and preserves the first picture and RAM") {
    emu::Wsc w(fixture_rom(), 0);
    game::MapData map;
    REQUIRE(tools::prepare_field_preview(w, 1, map, 0, 0));
    IndexedImage strip;
    strip.width = 32;
    strip.height = 16;
    strip.pixels.resize(32 * 16);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 32; ++x)
            strip.pixels[size_t(y * 32 + x)] = uint8_t(x < 16 ? 3 : 1);
    const auto png = encode_indexed_png(strip, {0xFFFFFF, 0xAAAAAA, 0x555555, 0x000000});
    game::GfxSwap swap;
    std::string error;
    REQUIRE(swap.add_map(1, png, "wsc,gb\n0,0\n", &error));
    REQUIRE(swap.add_map(2, png, "wsc,gb\n0,1\n", &error));
    w.run_frame();
    const auto plain = w.framebuffer();
    const auto before = w.ram;
    const auto sram_before = w.sram;
    swap.set_enabled(w, true);
    w.run_frame();
    CHECK(w.ram == before);
    CHECK(w.sram == sram_before);
    CHECK(w.framebuffer() == plain);
    CHECK(w.framebuffer(true)[0] == 0x000);
    // Repeated captures of the same map must repopulate tiles after the fixture clears its shadow.
    for (int frame = 0; frame < 3; ++frame) {
        swap.set_enabled(w, false);
        REQUIRE(tools::prepare_field_preview(w, 1, map, 0, 0));
        swap.set_enabled(w, true);
        w.run_frame();
        CHECK(w.framebuffer(true)[0] == 0x000);
        CHECK(w.framebuffer() == plain);
    }
    w.write8(0xD0DC, 2);
    // The destination becomes visible when its native cell writer publishes it.
    game::Machine destination(w);
    game::Machine::field_cell_hook(destination, 0x1000, 0, 0, 0, 0);
    const auto second = w.ram;
    swap.frame(w);
    w.run_frame();
    CHECK(w.ram == second);
    CHECK(w.sram == sram_before);
    CHECK(w.framebuffer() == plain);
    CHECK(w.framebuffer(true)[0] == 0xAAA);
    swap.set_enabled(w, false);
    w.run_frame();
    CHECK(w.ram == second);
    CHECK(w.sram == sram_before);
    CHECK(w.framebuffer() == plain);
}

TEST_CASE("map PNG mirrors both quadrant positions and pixels in the same direction") {
    emu::Wsc w(fixture_rom(), 0);
    game::MapData map;
    std::vector<uint32_t> colors;
    for (unsigned c = 0; c < 64; ++c)
        colors.push_back(c < 16 ? c * 0x111111 : 0);
    IndexedImage plain;
    std::string error;
    REQUIRE(decode_indexed_png(game::render_map_png(1, map, w, nullptr), colors, &plain, &error));
    for (int mirror = 1; mirror < 4; ++mirror) {
        map.mirror[0] = uint8_t(mirror);
        IndexedImage flipped;
        REQUIRE(decode_indexed_png(game::render_map_png(1, map, w, nullptr), colors, &flipped, &error));
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                const int sx = mirror & 1 ? 15 - x : x, sy = mirror & 2 ? 15 - y : y;
                CHECK(flipped.pixels[size_t(y * 1024 + x)] == plain.pixels[size_t(sy * 1024 + sx)]);
            }
    }
}

TEST_CASE("typed GB field lookups distinguish collision attributes and quadrant aliases") {
    emu::Wsc w(fixture_rom(), 0);
    game::MapData map;
    // The two WSC ids have identical graphics; collision attributes still differ.
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            map.cells[size_t((y + 8) * 80 + x + 8)] = uint8_t(x & 1);
            map.attributes[size_t(y * 64 + x)] = uint8_t(x & 1);
        }
    IndexedImage strip;
    strip.width = 32;
    strip.height = 16;
    strip.pixels.resize(512);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 32; ++x)
            strip.pixels[size_t(y * 32 + x)] = uint8_t(x < 16 ? 3 : 1);
    const auto png = encode_indexed_png(strip, {0xFFFFFF, 0xAAAAAA, 0x555555, 0x000000});
    game::GfxSwap swap;
    std::string error;
    REQUIRE(swap.add_map(1, png, "wsc,gb,type\n0,0,\n1,0,\n0,0,0\n1,1,1\n", &error));
    CHECK_FALSE(swap.add_map(2, png, "wsc,gb,type\n0,0,32\n", &error));
    CHECK_FALSE(swap.add_map(2, png, "wsc,gb,type\n0,0,abc\n", &error));
    REQUIRE(tools::prepare_field_preview(w, 1, map, 0, 0));
    const auto ram = w.ram;
    const auto sram = w.sram;
    w.run_frame();
    const auto original = w.framebuffer();
    swap.set_enabled(w, true);
    w.run_frame();
    CHECK(w.framebuffer(true)[0] == 0x000);
    CHECK(w.framebuffer(true)[16] == 0xAAA);
    CHECK(w.framebuffer() == original);
    CHECK(w.ram == ram);
    CHECK(w.sram == sram);
    swap.set_enabled(w, false);
    swap.set_enabled(w, true);
    w.run_frame();
    CHECK(w.framebuffer(true)[16] == 0xAAA);
    // A later non-field screen write must not be repainted by the cell records.
    w.write8(0x1004, 0xFF);
    w.write8(0x1005, 1);
    swap.frame(w);
    CHECK(w.shadow_read8(0x1004) == 0xFF);
    CHECK(w.shadow_read8(0x1005) == 1);
}

TEST_CASE("native field cell tracking leaves CPU and game memory unchanged while handling ring edges") {
    emu::Wsc plain(fixture_rom(), 0), tracked(fixture_rom(), 0);
    game::MapData map;
    IndexedImage strip;
    strip.width = strip.height = 16;
    strip.pixels.assign(256, 3);
    game::GfxSwap swap;
    std::string error;
    REQUIRE(swap.add_map(1, encode_indexed_png(strip, {0xFFFFFF, 0xAAAAAA, 0x555555, 0x000000}),
                         "wsc,gb,type\n0,0,\n0,0,0\n", &error));
    const auto hook = game::Machine::field_cell_hook;
    for (int fixture = 0; fixture < 2; ++fixture) {
        auto &w = fixture ? tracked : plain;
        REQUIRE(tools::prepare_field_preview(w, 1, map, 0, 0));
        game::Machine::field_cell_hook = fixture ? hook : nullptr;
        game::Machine m(w);
        m.ss() = 0;
        m.sp() = 0x7000;
        m.bx() = 0x0100;
        m.di() = 0x7BC; // last column/row of the ring; second cell wraps horizontally
        m.cx() = 0x0202;
        m.wr8(0, 0xD57B, 0x80); // field foreground duplicated onto screen 2
        game::draw_map_rectangle(m);
    }
    game::Machine::field_cell_hook = hook;
    CHECK(plain.ram == tracked.ram);
    CHECK(plain.sram == tracked.sram);
    CHECK(plain.io == tracked.io);
    for (int r = 0; r < 8; ++r)
        CHECK(plain.cpu.s.r[r] == tracked.cpu.s.r[r]);
    for (int r = 0; r < 4; ++r)
        CHECK(plain.cpu.s.seg[r] == tracked.cpu.s.seg[r]);
    CHECK(plain.cpu.s.ip == tracked.cpu.s.ip);
    CHECK(plain.cpu.s.flags == tracked.cpu.s.flags);
    swap.set_enabled(tracked, true);
    const auto before = tracked.ram;
    swap.frame(tracked);
    CHECK(tracked.ram == before);
    CHECK(tracked.shadow_read8(0x17BC) == 0);
    CHECK(tracked.shadow_read8(0x17BD) == 0x2C);
    CHECK(tracked.shadow_read8(0x1FBD) == 0x2C);
    tracked.write8(0xD0DC, 2); // leaving the mapped field releases its foreground copies
    game::Machine destination(tracked);
    game::Machine::field_cell_hook(destination, 0x1000, 0, 0, 0, 0);
    swap.frame(tracked);
    CHECK(tracked.shadow_read8(0x1FBD) == tracked.ram[0x1FBD]);
}

TEST_CASE("GB cell resources override typed art by map coordinates, with transactional parsing") {
    emu::Wsc w(fixture_rom(), 0);
    game::MapData map;
    IndexedImage strip;
    strip.width = 32;
    strip.height = 16;
    strip.pixels.resize(512);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 32; ++x)
            strip.pixels[size_t(y * 32 + x)] = uint8_t(x < 16 ? 3 : 1);
    game::GfxSwap swap;
    std::string error;
    std::string lookup = "wsc,gb,type\n0,0,\n0,0,0\n";
    SUBCASE("two-column lookups with cell resources") { lookup = "wsc,gb\n0,0\n"; }
    SUBCASE("typed lookups with cell resources") {}
    REQUIRE(
        swap.add_map(1, encode_indexed_png(strip, {0xFFFFFF, 0xAAAAAA, 0x555555, 0x000000}), lookup, &error));
    CHECK_FALSE(swap.add_cells(2, "x,y,gb\n5,7,1\n", &error));
    REQUIRE(swap.add_cells(1, "x,y,gb\n5,7,1\n-1,7,1\n-8,-8,1\n71,71,1\n", &error));
    for (const auto csv : {"x,y,gb\n5,7,2\n", "x,y,gb\n72,7,1\n", "x,y,gb\n5,-9,1\n", "x,y,gb\n5,7,abc\n",
                           "x,y,gb\n,7,1\n", "x,y,gb\n5,7,1\n5,7,0\n", "x,y,gb\n5,7\n", "wsc,gb\n0,0\n"})
        CHECK_FALSE(swap.add_cells(1, csv, &error));
    REQUIRE(tools::prepare_field_preview(w, 1, map, 5, 7));
    const auto ram = w.ram;
    w.run_frame();
    const auto original = w.framebuffer();
    swap.set_enabled(w, true);
    w.run_frame();
    CHECK(w.framebuffer(true)[0] == 0xAAA); // same id/type, different coordinates
    CHECK(w.framebuffer(true)[16] == 0x000);
    CHECK(w.framebuffer() == original);
    CHECK(w.ram == ram);
    game::Machine m(w);
    m.ss() = 0;
    m.sp() = 0x7000;
    const auto draw = [&](uint16_t coords) {
        m.bx() = coords;
        m.di() = 0x7BC;
        m.cx() = 0x0102;
        game::draw_map_rectangle(m);
        swap.frame(w);
    };
    draw(0x0705); // native routine passes x=5,y=7; ring destination does not encode map position
    CHECK(w.shadow_read8(0x17BC) == 4);
    CHECK(w.shadow_read8(0x1780) == 0); // x=6 wraps to the first screen column, using default art
    draw(0x0805);                       // same destination/id/type, new y; old override must disappear
    CHECK(w.shadow_read8(0x17BC) == 0);
    draw(0x07FF); // signed left-border coordinate -1
    CHECK(w.shadow_read8(0x17BC) == 4);
    CHECK(w.shadow_read8(0x1780) == 0); // x wraps to 0 in the map, independently of the screen
    draw(0xF8F8);                       // -8,-8: corner of the scrolling border
    CHECK(w.shadow_read8(0x17BC) == 4);
    draw(0x4747); // 71,71: positive border coordinates retain their sign
    CHECK(w.shadow_read8(0x17BC) == 4);
    w.write8(0x17BC, 0xFF);
    w.write8(0x17BD, 1);
    swap.frame(w);
    CHECK(w.shadow_read8(0x17BC) == 0xFF); // later windows remain guarded
}

TEST_CASE("sparse GB cells preserve native fallback tiles across frames and map transitions") {
    emu::Wsc w(fixture_rom(), 0);
    game::MapData map;
    game::GfxSwap swap;
    IndexedImage strip;
    strip.width = strip.height = 16;
    strip.pixels.assign(256, 3);
    const auto png = encode_indexed_png(strip, {0xFFFFFF, 0xAAAAAA, 0x555555, 0x000000});
    std::string error;
    REQUIRE(swap.add_map(1, png, "wsc,gb\n", &error));
    REQUIRE(swap.add_cells(1, "x,y,gb\n0,0,0\n", &error));
    REQUIRE(swap.add_map(2, png, "wsc,gb\n0,0\n", &error));
    for (int id : {1, 2, 1}) {
        swap.set_enabled(w, false);
        REQUIRE(tools::prepare_field_preview(w, id, map, 0, 0));
        w.run_frame();
        const auto original = w.framebuffer();
        const auto ram = w.ram;
        const auto sram = w.sram;
        swap.set_enabled(w, true);
        for (int frame = 0; frame < 3; ++frame) {
            swap.frame(w);
            w.run_frame();
            CHECK(w.ram == ram);
            CHECK(w.sram == sram);
            CHECK(w.framebuffer() == original);
            for (int y = 0; y < emu::Wsc::kHeight; ++y)
                for (int x = 0; x < emu::Wsc::kWidth; ++x) {
                    const size_t p = size_t(y * emu::Wsc::kWidth + x);
                    const bool gb = id == 2 || (x < 16 && y < 16);
                    REQUIRE(w.framebuffer(true)[p] == (gb ? 0 : original[p]));
                }
        }
    }
}

TEST_CASE("GB destination announcement keeps outgoing art until the destination cell draw") {
    emu::Wsc w(fixture_rom(), 0);
    game::GfxSwap swap; // observe the native cell writer from the first field
    IndexedImage strip;
    strip.width = strip.height = 16;
    strip.pixels.assign(256, 3);
    std::string error;
    REQUIRE(swap.add_map(1, encode_indexed_png(strip, {0xFFFFFF, 0xAAAAAA, 0x555555, 0x000000}),
                         "wsc,gb\n0,0\n", &error));
    strip.pixels.assign(256, 1);
    REQUIRE(swap.add_map(2, encode_indexed_png(strip, {0xFFFFFF, 0xAAAAAA, 0x555555, 0x000000}),
                         "wsc,gb\n0,0\n", &error));
    REQUIRE(tools::prepare_field_preview(w, 1, game::MapData{}, 0, 0));
    swap.set_enabled(w, true);
    swap.frame(w);
    w.run_frame();
    const auto outgoing = w.framebuffer(true);
    const auto native = w.framebuffer();
    REQUIRE(outgoing[0] == 0);
    // The warp writes current_map before initialize_field_display's first wait.
    // Stretch that interval to three frames: no destination words exist yet.
    w.write8(0xD0DC, 2);
    for (int frame = 0; frame < 3; ++frame) {
        const auto ram = w.ram;
        swap.frame(w);
        w.run_frame();
        CHECK(w.ram == ram);
        CHECK(w.framebuffer() == native);
        CHECK(w.framebuffer(true) == outgoing);
    }
    // Publish the new map through the same field-cell hook as draw_map_viewport.
    swap.set_enabled(w, false);
    REQUIRE(tools::prepare_field_preview(w, 2, game::MapData{}, 0, 0));
    swap.set_enabled(w, true);
    swap.frame(w);
    w.run_frame();
    CHECK(w.framebuffer(true)[0] == 0xAAA);
    CHECK(w.framebuffer() == native);
}

TEST_CASE("GB foreground ownership follows native clears and ring reuse") {
    emu::Wsc w(fixture_rom(), 0);
    game::GfxSwap swap;
    IndexedImage strip;
    strip.width = strip.height = 16;
    strip.pixels.assign(256, 3);
    std::string error;
    REQUIRE(swap.add_map(1, encode_indexed_png(strip, {0xFFFFFF, 0xAAAAAA, 0x555555, 0x000000}),
                         "wsc,gb\n0,0\n", &error));
    REQUIRE(swap.add_cells(1, "x,y,gb\n0,0,0\n", &error));
    REQUIRE(tools::prepare_field_preview(w, 1, game::MapData{}, 0, 0));
    // The native foreground duplicates a field metatile, then clears it as the ring scrolls.
    constexpr uint16_t offsets[] = {0, 2, 0x40, 0x42};
    for (auto offset : offsets) {
        const uint16_t source = uint16_t(0x1000 + offset);
        w.write8(uint16_t(source + 0x800), w.ram[source]);
        w.write8(uint16_t(source + 0x801), w.ram[source + 1]);
    }
    game::Machine m(w);
    game::Machine::field_cell_hook(m, 0x1000, 0, 0, 0, 0);
    swap.set_enabled(w, true);
    REQUIRE(w.shadowed(0x1800));
    REQUIRE(w.shadow_read8(0x1801) == 0x2C);
    for (auto offset : offsets) {
        w.write8(uint16_t(0x1800 + offset), 0xFF);
        w.write8(uint16_t(0x1801 + offset), 1);
    }
    // The background words did not change, so only observing native foreground writes can retire this.
    swap.frame(w);
    for (auto offset : offsets) {
        CHECK_FALSE(w.shadowed(uint16_t(0x1800 + offset)));
        CHECK(w.shadow_read8(uint16_t(0x1800 + offset)) == 0xFF);
        CHECK(w.shadow_read8(uint16_t(0x1801 + offset)) == 1);
    }
}

TEST_CASE("Enhanced field cache rejects reused screen words across fidelity switches") {
    int id = 1;
    SUBCASE("a field with installed GB map resources") {}
    SUBCASE("a field with native fallback tiles") { id = 2; }
    emu::Wsc w(fixture_rom(), 0);
    game::MapData map;
    IndexedImage strip;
    strip.width = strip.height = 16;
    strip.pixels.assign(256, 3);
    game::GfxSwap swap;
    std::string error;
    REQUIRE(swap.add_map(1, encode_indexed_png(strip, {0xFFFFFF, 0xAAAAAA, 0x555555, 0x000000}),
                         "wsc,gb\n0,0\n", &error));
    REQUIRE(tools::prepare_field_preview(w, id, map, 0, 0));
    const auto original = game::GfxSwap::field_world_words(w);
    REQUIRE(original.size() == 256);
    // A window/scene copies other valid upper-bank words into the same physical ring.
    // Their tile bank and palette alone do not establish ownership of the old world coordinate.
    for (int q : {0, 2, 0x40, 0x42}) {
        w.write8(uint16_t(0x1000 + q), 0x10);
        w.write8(uint16_t(0x1001 + q), 0x20);
    }
    for (int cycle = 0; cycle < 4; ++cycle) {
        swap.set_enabled(w, true);
        const auto ram = w.ram;
        const auto sram = w.sram;
        const auto live = game::GfxSwap::field_world_words(w);
        CHECK(live.count(8 * 80 + 8) == 0);
        CHECK(live.size() == original.size() - 1);
        CHECK(w.ram == ram);
        CHECK(w.sram == sram);
        swap.set_enabled(w, false);
    }
    // A real field draw reestablishes ownership and allows event changes through.
    game::Machine m(w);
    game::Machine::field_cell_hook(m, 0x1000, 0, 0, 0, 0);
    CHECK(game::GfxSwap::field_world_words(w).at(8 * 80 + 8)[0] == 0x2010);
}

TEST_CASE("GB field hooks preserve native actors when sprite assets are missing") {
    emu::Wsc w(fixture_rom(), 0);
    game::GfxSwap swap;
    IndexedImage bg;
    bg.width = bg.height = 16;
    bg.pixels.assign(256, 0);
    std::string error;
    REQUIRE(
        swap.add_map(1, encode_indexed_png(bg, {0xFFFFFF, 0xAAAAAA, 0x555555, 0}), "wsc,gb\n0,0\n", &error));
    REQUIRE(tools::prepare_field_preview(w, 1, game::MapData{}, 0, 0));
    swap.set_enabled(w, true);
    tools::prepare_field_party(w, 0);
    const auto ram = w.ram;
    swap.frame(w);
    CHECK(w.ram == ram);
    CHECK(w.shadow_read8(0x4200) == w.ram[0x4200]);
}

TEST_CASE("GB NPC tile buffers stay owned until the displayed OAM latches their replacement") {
    emu::Wsc w(fixture_rom(), 0);
    game::GfxSwap swap;
    IndexedImage atlas{64, 16, std::vector<uint8_t>(64 * 16, 1)};
    IndexedImage corpse{16, 16, std::vector<uint8_t>(256, 3)};
    const std::vector<uint32_t> pal = {0xFF00FF, 0xFFFFFF, 0xAAAAAA, 0};
    std::string error;
    REQUIRE(swap.add_sprites(encode_indexed_png(atlas, pal), "type,gb\n0,0\n", &error));
    swap.import_field_sprites(w);
    REQUIRE(swap.add_corpse(encode_indexed_png(corpse, pal), &error));
    REQUIRE(tools::prepare_field_preview(w, 1, game::MapData{}, 0, 0));
    w.ram[0xD5AE] = w.ram[0xD5BF] = w.ram[0xD5A7] = 0;
    swap.set_enabled(w, true);
    game::Machine m(w);
    m.ss() = 0;
    const auto draw = [&](int tile) {
        for (int q = 0; q < 4; ++q) {
            m.wr16(0, uint16_t(0xC000 + q * 4), uint16_t(0x200 | (tile + q)));
            m.wr8(0, uint16_t(0xC002 + q * 4), uint8_t(48 + q / 2 * 8));
            m.wr8(0, uint16_t(0xC003 + q * 4), uint8_t(88 + q % 2 * 8));
        }
        game::Machine::npc_sprite_hook(m, 0xD5AE, 0xC000, 17);
    };
    const auto latch = [&]() {
        std::copy_n(w.ram.begin() + 0xC000, 16, w.ram.begin() + 0x200);
        w.io[4] = 1;
        w.io[5] = 0;
        w.io[6] = 4;
        w.cpu.s.flags = 2;
        w.cpu.s.halted = true;
        w.run_frame();
    };
    draw(0x18);
    latch();
    swap.frame(w);
    draw(0x1C); // queued OAM changes before VBlank; the old four tiles still display
    swap.frame(w);
    CHECK(w.shadow_read8(0x4300) == 0x33);
    CHECK(w.shadow_read8(0x4380) == 0x33);
    latch();
    swap.frame(w);
    CHECK(w.shadow_read8(0x4300) == w.ram[0x4300]);
    CHECK(w.shadow_read8(0x4380) == 0x33);
}
