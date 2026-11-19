#include "chrome_fixture.h"
#include "engine/indexed_png.h"
#include "game/gfx_swap.h"
#include "game/save_ui.h"
#include "game/session.h"
#include "game/ui_scene.h"
#include <doctest/doctest.h>
using namespace sawgod::game;
using sawgod::emu::Wsc;

TEST_CASE("save summaries use the store and verify primary and backup SRAM without side effects") {
    Wsc w(std::vector<uint8_t>(0x400000), 0x8000);
    Machine m(w);
    std::vector<uint8_t> p(SaveSlots::kPayload, 0);
    p[0x435] = 0x1B;
    p[0x436] = 0xE4;
    p[0xA2] = 99;
    uint16_t sum = 0;
    for (size_t i = 0; i < p.size(); ++i) {
        m.wr8(0x1000, uint16_t(0x3FB2 + i), p[i]);
        sum = uint16_t(sum + p[i]);
    }
    m.wr16(0x1000, uint16_t(0x3FB2 + p.size()), sum);
    const auto ram = w.ram;
    const auto sram = w.sram;
    const auto regs = w.cpu.s.r;
    const auto flags = w.cpu.s.flags;
    CHECK(bool(SaveUi::payload(w, 0) == p));
    CHECK(bool(w.ram == ram));
    CHECK(bool(w.sram == sram));
    CHECK(w.cpu.s.r == regs);
    CHECK(w.cpu.s.flags == flags);
    // A valid primary wins over the backup; a bad checksum or marker falls back.
    auto primary = p;
    primary[0xA2] = 42;
    uint16_t primary_sum = 0;
    for (size_t i = 0; i < primary.size(); ++i) {
        m.wr8(0x1000, uint16_t(0x3301 + i), primary[i]);
        primary_sum = uint16_t(primary_sum + primary[i]);
    }
    m.wr16(0x1000, uint16_t(0x3301 + primary.size()), primary_sum);
    CHECK(bool(SaveUi::payload(w, 0) == primary));
    m.wr8(0x1000, 0x3301 + 0xA2, 43);
    CHECK(bool(SaveUi::payload(w, 0) == p));
    m.wr8(0x1000, 0x3301 + 0xA2, 42);
    m.wr8(0x1000, 0x3301 + 0x435, 0x1A);
    m.wr16(0x1000, uint16_t(0x3301 + primary.size()), uint16_t(primary_sum - 1));
    CHECK(bool(SaveUi::payload(w, 0) == p));
    m.wr8(0x1000, 0x3FB2 + 0xA2, 98);
    CHECK(SaveUi::payload(w, 0).empty());
    CHECK(SaveUi::payload(w, -1).empty());
    CHECK(SaveUi::payload(w, 10).empty());
    auto &slots = Session::of(w).save_slots();
    slots.attach_store();
    CHECK(SaveUi::payload(w, 0).empty());
    slots.store(0, p);
    CHECK(bool(SaveUi::payload(w, 0) == p));
    CHECK(SaveUi::payload(w, 8).empty());
}
TEST_CASE("save presentation follows AUTO slot mapping and nested confirmations in both pictures") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    chrome_fixture::install(w, 2);
    w.set_second(true);
    Machine m(w);
    auto &s = Session::of(w);
    auto &slots = s.save_slots();
    slots.attach_store();
    slots.request_count(9);
    slots.request_auto(true);
    slots.apply();
    std::vector<uint8_t> p(SaveSlots::kPayload, 0);
    p[0xE4] = 1;
    p[0xE3] = 2;
    slots.store(SaveSlots::kAuto, p, {"First", "Second", "WWWMMMMW", "Fourth"});
    SaveUi ui;
    ui.install(w);
    for (int scene : {1, 2, 3, 4, 5, 6}) {
        UiSceneScope scope(m, hooks::Event::SaveMenu, scene);
        const auto ram = w.ram;
        const auto io = w.io;
        const auto sram = w.sram;
        const auto regs = w.cpu.s.r;
        const auto flags = w.cpu.s.flags;
        for (bool second : {false, true}) {
            const auto fb = w.framebuffer_rgb(Wsc::ColorMode::Raw, second);
            const auto px = ui.picture(w, Wsc::ColorMode::Raw, second);
            CHECK(bool(px != fb));
            CHECK(bool(ui.picture(w, Wsc::ColorMode::Raw, second) == px));
            CHECK(bool(w.ram == ram));
            CHECK(bool(w.io == io));
            CHECK(bool(w.sram == sram));
            CHECK(w.cpu.s.r == regs);
            CHECK(w.cpu.s.flags == flags);
            CHECK(bool(w.framebuffer_rgb(Wsc::ColorMode::Raw, second) == fb));
        }
    }
    CHECK_FALSE(ui.active());
    CHECK(bool(ui.picture(w, Wsc::ColorMode::Raw) == w.framebuffer_rgb(Wsc::ColorMode::Raw)));
    {
        UiSceneScope list(m, hooks::Event::SaveMenu, 2);
        const auto first = ui.picture(w, Wsc::ColorMode::Raw);
        w.ram[0xD842] = slots.show(8);
        CHECK(slots.slot_for_row(w.ram[0xD842]) == 8);
        CHECK(slots.top() == 7);
        const auto ram = w.ram;
        CHECK(bool(ui.picture(w, Wsc::ColorMode::Raw) != first));
        CHECK(bool(w.ram == ram));
        {
            UiSceneScope confirm(m, hooks::Event::SaveMenu, 4);
            CHECK(ui.scene() == SaveUi::Scene::SaveConfirm);
        }
        CHECK(ui.scene() == SaveUi::Scene::Save);
        s.new_generation();
        ui.frame(w);
        CHECK_FALSE(ui.active());
    }
}

TEST_CASE("GB save portraits use each stored leader type rather than the current party or native row tiles") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    chrome_fixture::install(w, 2);
    auto &slots = Session::of(w).save_slots();
    slots.attach_store();
    std::vector<uint8_t> p(SaveSlots::kPayload, 0);
    p[0xE3] = 2;
    p[2 * 31 + 5] = 5;
    slots.store(0, p);
    sawgod::IndexedImage atlas{64, 32, std::vector<uint8_t>(64 * 32, 3)};
    std::fill(atlas.pixels.begin() + 64 * 16, atlas.pixels.end(), 2);
    GfxSwap swap;
    std::string error;
    REQUIRE(swap.add_sprites(sawgod::encode_indexed_png(atlas, {0xFF00FF, 0xFFFFFF, 0xAAAAAA, 0x000000}),
                             "type,gb\n0,0\n5,1\n", &error));
    swap.set_enabled(w, true);
    SaveUi ui;
    ui.install(w);
    Machine m(w);
    UiSceneScope scene(m, hooks::Event::SaveMenu, 1);
    const auto ram = w.ram;
    const auto gb = ui.picture(w, Wsc::ColorMode::Raw, true);
    CHECK(gb[30 * 224 + 50] == 0xAAAAAA);
    CHECK(ui.picture(w, Wsc::ColorMode::Raw, false)[30 * 224 + 50] != 0xAAAAAA);
    CHECK(w.ram == ram);
}

TEST_CASE("host save completion accepts either confirm button without changing other keys") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    Machine m(w);
    CHECK(hooks::filter(hooks::Value::SaveCompleteKeys, m, sawgod::emu::KEY_B) == sawgod::emu::KEY_B);
    SaveUi ui;
    ui.install(w);
    for (int keys : {0, int(sawgod::emu::KEY_X1), int(sawgod::emu::KEY_A), int(sawgod::emu::KEY_B),
                     int(sawgod::emu::KEY_A | sawgod::emu::KEY_B)})
        CHECK(hooks::filter(hooks::Value::SaveCompleteKeys, m, keys) ==
              (keys & sawgod::emu::KEY_B ? sawgod::emu::KEY_A : keys));
}
