#include "game/mode7.h"
#include "game/session.h"
#include "game/terrain_veil.h"
#include <algorithm>
#include <doctest/doctest.h>

using namespace sawgod;
namespace {
void put(emu::Wsc &w, int at, int value) {
    w.ram[size_t(at)] = uint8_t(value);
    w.ram[size_t(at + 1)] = uint8_t(value >> 8);
}
void display(emu::Wsc &w) {
    w.ram.fill(0);
    w.io.fill(0);
    w.cpu.s.halted = true;
    w.cpu.s.flags = 0;
    w.scanline = w.line_cycle = 0;
    w.io[0x16] = 158;
    w.io[0x60] = 0xE0;
    w.io[0] = 5;
    w.io[7] = 2;
    w.io[4] = 1;
    put(w, 0xFE00, 0x246); // backdrop
    put(w, 0xFF02, 0xECA); // actor palette 8, index 1
    w.capture_layers(true);
}
void card(emu::Wsc &w, int first, int tile, int x, int y, int count) {
    for (int q = 0; q < 4; ++q) {
        std::fill_n(w.ram.begin() + 0x4000 + (tile + q) * 32, 32, 0x11);
        if (q < count) {
            put(w, 0x200 + (first + q) * 4, tile + q);
            w.ram[size_t(0x202 + (first + q) * 4)] = uint8_t(y + q / 2 * 8);
            w.ram[size_t(0x203 + (first + q) * 4)] = uint8_t(x + q % 2 * 8);
        }
    }
}
void latch(emu::Wsc &w) {
    w.publish_terrain_cards();
    w.run_frame();
    w.run_frame();
}
} // namespace

TEST_CASE("terrain veil reconstructs multiple NPCs and party identically on both native phases") {
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0x8000);
    display(w);
    std::vector<uint32_t> previous;
    for (int count : {4, 2}) {
        int first = 0;
        for (int slot = 0; slot < 3; ++slot) {
            const int tile = 0x10 + slot * 8 + (count == 2 ? 4 : 0);
            card(w, first, tile, 30 + slot * 32, 40, count);
            w.terrain_cards[size_t(slot)] = {first, uint16_t(tile), true};
            first += count;
        }
        w.io[6] = uint8_t(first);
        w.publish_terrain_cards();
        w.run_frame();
        if (count == 4)
            w.run_frame();
        // On the second pass the completed raster still uses the old four-entry cards, but OAM
        // already holds the other DMA buffer and its two-entry cards, exactly as a live field frame.
        const auto ram = w.ram;
        const auto io = w.io;
        const auto sram = w.sram;
        auto rgb = w.framebuffer_rgb(emu::Wsc::ColorMode::Raw);
        game::blend_terrain_veil(w, rgb, nullptr);
        REQUIRE(game::veiled_terrain_cards(w).size() == 3);
        for (int slot = 0; slot < 3; ++slot) {
            const int x = 30 + slot * 32;
            CHECK(rgb[size_t(40 * 224 + x)] == 0xEECCAA); // upper body opaque
            CHECK(rgb[size_t(48 * 224 + x)] == 0x888888); // half actor, half backdrop
        }
        if (!previous.empty())
            CHECK(rgb == previous);
        previous = rgb;
        CHECK(w.ram == ram);
        CHECK(w.io == io);
        CHECK(w.sram == sram);
        std::array<uint8_t, 224 * 144> ui{};
        ui[48 * 224 + 30] = 1;
        rgb[48 * 224 + 30] = 0xABCDEF;
        game::blend_terrain_veil(w, rgb, &ui);
        CHECK(rgb[48 * 224 + 30] == 0xABCDEF);
    }
}

TEST_CASE("terrain veil follows latched OAM and rejects overwritten removed and unmasked cards") {
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0x8000);
    display(w);
    card(w, 0, 0x18, -8, 40, 2);
    w.io[6] = 2;
    w.terrain_cards[1] = {0, 0x18, true};
    latch(w);
    REQUIRE(game::veiled_terrain_cards(w).size() == 1);
    CHECK(game::veiled_terrain_cards(w)[0].x == -8);
    w.terrain_cards[1].veiled = false;
    CHECK(game::veiled_terrain_cards(w).size() == 1); // staged change is not this picture
    latch(w);
    CHECK(game::veiled_terrain_cards(w).empty());
    w.terrain_cards[1].veiled = true;
    put(w, 0x200, 0x20); // another NPC overwrote an unreserved card
    latch(w);
    CHECK(game::veiled_terrain_cards(w).empty());
    card(w, 0, 0x18, 20, 40, 2);
    w.io[6] = 0;
    latch(w);
    CHECK(game::veiled_terrain_cards(w).empty());
    w.reset();
    CHECK_FALSE(w.terrain_cards[1].veiled);
    CHECK(game::veiled_terrain_cards(w).empty());
}

TEST_CASE("Mode 7 distant NPC terrain uses real steady alpha and keeps deliberate NPC blink") {
    std::vector<uint8_t> rom(0x400000);
    // Sprite set 0, ROM bank 0: a solid 16x16 sprite at offset 0100.
    rom[0x3940B1] = 1;
    rom[0x393F72] = 2;
    std::fill_n(rom.begin() + 0x100, 128, 0x11);
    emu::Wsc w(std::move(rom), 0x8000);
    display(w);
    for (int i = 0; i < 16; ++i)
        w.ram[size_t(0xD5AE + i * 20)] = 255;
    w.ram[0xD5AE] = 0;
    put(w, 0xD5B4, 10 * 32);
    put(w, 0xD5B6, 10 * 32);
    // read8's ROM window at 90000 maps bank 39 with C0=FF; the art lives in bank zero.
    w.io[0xC0] = 0xFF;
    w.io[0xC2] = 0;
    std::vector<uint32_t> previous;
    for (int frame : {0, 1}) {
        w.ram[0xC80C] = uint8_t(frame);
        w.ram[0xD57B] = 0x20;
        auto cards = game::field_npc_sprites(w);
        REQUIRE(cards.size() == 1);
        CHECK(cards[0].veil);
        CHECK((*cards[0].pixels)[0] == 0xFFEECCAA);
        CHECK((*cards[0].pixels)[8 * 16] == 0x80EECCAA);
        if (!previous.empty())
            CHECK(*cards[0].pixels == previous);
        previous = *cards[0].pixels;
    }
    w.ram[0xD57B] = 0x40;
    auto cards = game::field_npc_sprites(w);
    REQUIRE(cards.size() == 1);
    CHECK_FALSE(cards[0].veil);
    CHECK((*cards[0].pixels)[8 * 16] == 0);
    w.ram[0xD5BF] = 0x10; // floating objects ignore terrain instead of disappearing
    cards = game::field_npc_sprites(w);
    REQUIRE(cards.size() == 1);
    CHECK_FALSE(cards[0].veil);
    CHECK((*cards[0].pixels)[8 * 16] == 0xFFEECCAA);
    w.ram[0xD5BF] = 0x20;
    w.ram[0xC80C] = 8;
    CHECK(game::field_npc_sprites(w).empty());
}

TEST_CASE("terrain veil provenance follows OAM upload before the hardware latch") {
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0x8000);
    display(w);
    card(w, 0, 0x18, 40, 40, 2);
    w.io[6] = 2;
    w.terrain_cards[1] = {0, 0x18, true};
    latch(w);
    REQUIRE(game::veiled_terrain_cards(w).size() == 1);
    // The next field update stages a different DMA buffer on ordinary terrain. It has not uploaded
    // OAM yet: both the old raster and its reconstructed lower body must remain visible.
    w.terrain_cards[1] = {0, 0x1C, false};
    w.run_frame();
    w.run_frame();
    REQUIRE(game::veiled_terrain_cards(w).size() == 1);
    CHECK(game::veiled_terrain_cards(w)[0].words[0] == 0x18);
    card(w, 0, 0x1C, 40, 40, 4);
    w.io[6] = 4;
    latch(w);
    CHECK(game::veiled_terrain_cards(w).empty());
}

TEST_CASE("visible NPC cards follow latched OAM when a pending record has already entered the screen") {
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0x8000);
    display(w);
    game::Session::of(w);
    for (int slot = 0; slot < 16; ++slot)
        w.ram[size_t(0xD5AE + slot * 20)] = 255;
    w.ram[0xD5AE] = 0;
    put(w, 0xD5B4, 80 * 2);
    put(w, 0xD5B6, 0);            // pending NPC top is already y=0
    card(w, 0, 0x18, 80, -16, 4); // the displayed OAM is still clipped entirely above the screen
    w.io[6] = 4;
    w.terrain_cards[1] = {0, 0x18, false};
    latch(w);
    game::PresentationScene field;
    field.kind = game::PresentationScene::Kind::Field;
    field.map = 88;
    game::Mode7World world;
    game::LightTags lights;
    game::LandmarkTags landmarks;
    REQUIRE(world.update(w, field, lights, landmarks));
    const auto npc = std::find_if(world.sprites().begin(), world.sprites().end(),
                                  [](const auto &c) { return c.record == 0xD5AE; });
    REQUIRE(npc != world.sprites().end());
    CHECK(npc->y == -1);
    CHECK(std::count_if(world.sprites().begin(), world.sprites().end(),
                        [](const auto &c) { return c.record == 0xD5AE; }) == 1);
    auto picture = game::mode7_neutral_picture(world.surface(), world.sprites(), world.scroll());
    CHECK(picture.sources[80] == 0); // no early actor pixel leaks into row zero
}

TEST_CASE("geometry veil reference uses the displayed card while flat WSC keeps its existing clock") {
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0x8000);
    display(w);
    auto &session = game::Session::of(w);
    card(w, 0, 0x18, 40, 40, 2);
    w.io[6] = 2;
    w.terrain_cards[1] = {0, 0x18, true};
    latch(w);
    const auto native = w.framebuffer_rgb(emu::Wsc::ColorMode::Raw);
    // Next update changes sprite palette after scanout; the displayed card remains the old color.
    put(w, 0xFF02, 0xF00);
    const auto memory = w.snapshot();
    auto flat = native, geometry = native;
    game::blend_terrain_veil(w, flat, nullptr);
    game::blend_terrain_veil(w, geometry, nullptr, &session.raster_field(w));
    CHECK(flat[48 * 224 + 40] == 0x902233);
    CHECK(geometry[48 * 224 + 40] == 0x888888);
    CHECK(w.ram == memory.ram);
    CHECK(w.io == memory.io);
    CHECK(w.sram == memory.sram);
}
