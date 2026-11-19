#include "chrome_fixture.h"
#include "game/bestiary_ui.h"
#include "game/session.h"
#include "game/ui_chrome.h"
#include <doctest/doctest.h>
using namespace sawgod::game;
using sawgod::emu::Wsc;

namespace {
// A history where the zero ROM's pointer table (4000:B92D) points: 0x0000, generation, ring index and
// 100 entries (0xFF empty) -- `count` entries after the ring index, wrapping past 99.
void history(Wsc &w, int generation, int start, int count) {
    w.ram[0] = uint8_t(generation);
    w.ram[1] = uint8_t(generation >> 8);
    w.ram[2] = uint8_t(start);
    w.ram[3] = 0;
    for (int i = 0; i < 100; ++i)
        w.ram[size_t(4 + i)] = 255;
    for (int k = 0; k < count; ++k)
        w.ram[size_t(4 + (start + 1 + k) % 100)] = uint8_t(10 + k);
}
} // namespace

TEST_CASE("bestiary presentation follows member selection, the history list and the notice, resets") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    Machine m(w);
    BestiaryUi ui;
    ui.install(w);
    CHECK(Session::of(w).player_names().enabled);
    CHECK_FALSE(ui.active());
    for (int scene = 1; scene <= 6; ++scene) {
        hooks::emit(hooks::Event::Bestiary, m, {scene});
        CHECK(int(ui.scene()) == scene);
    }
    hooks::emit(hooks::Event::Bestiary, m, {0});
    CHECK_FALSE(ui.active());
    CHECK(bool(ui.picture(w, Wsc::ColorMode::Raw) == w.framebuffer_rgb(Wsc::ColorMode::Raw)));
    hooks::emit(hooks::Event::Bestiary, m, {2});
    Session::of(w).new_generation();
    ui.frame(w);
    CHECK_FALSE(ui.active());
    {
        BestiaryUi temporary;
        temporary.install(w);
    }
    hooks::emit(hooks::Event::Bestiary, m, {1});
    CHECK(ui.active()); // another subscription's teardown did not remove ours
}

TEST_CASE("bestiary history reads the ring newest first, counts generations down and scrolls seven rows") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    Machine m(w);
    history(w, 1234, 95, 25); // wraps past entry 99
    BestiaryUi ui;
    ui.install(w);
    hooks::emit(hooks::Event::Bestiary, m, {1});
    auto v = ui.view(w);
    CHECK(v.generation == 1234);
    CHECK(v.total == 25);
    CHECK(v.selected == -1); // member selection: no list cursor
    CHECK(v.first == 0);
    CHECK(v.rows[0].generation == 1234);
    CHECK(v.rows[6].generation == 1228);
    CHECK_FALSE(v.rows[0].name.empty());
    // The native list: scroll 0xD853 + cursor 0xD847 (0..13).
    hooks::emit(hooks::Event::Bestiary, m, {2});
    for (int selected = 0; selected < 25; ++selected) {
        w.ram[0xD853] = uint8_t(std::max(0, selected - 13));
        w.ram[0xD847] = uint8_t(selected - w.ram[0xD853]);
        v = ui.view(w);
        CHECK(v.selected == selected);
        CHECK(v.first <= selected);
        CHECK(selected < v.first + BestiaryUi::kRows);
        CHECK(v.rows[size_t(selected - v.first)].generation == 1234 - selected);
    }
    CHECK(v.first == 18);
    w.ram[0xD853] = w.ram[0xD847] = 0;
    CHECK(ui.view(w).first == 0);
    // Leaving the list starts it at the top again.
    hooks::emit(hooks::Event::Bestiary, m, {1});
    CHECK(ui.view(w).first == 0);
}

TEST_CASE("bestiary Down stops at the last history entry and leaves every other key alone") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    Machine m(w);
    history(w, 3, 10, 3);
    BestiaryUi ui;
    ui.install(w);
    CHECK(hooks::filter(hooks::Value::BestiaryKeys, m, 0x40) == 0x40); // hidden: original keys
    hooks::emit(hooks::Event::Bestiary, m, {2});
    CHECK(hooks::filter(hooks::Value::BestiaryKeys, m, 0x40) == 0x40);
    w.ram[0xD847] = 2;
    CHECK(hooks::filter(hooks::Value::BestiaryKeys, m, 0x40) == 0);
    CHECK(hooks::filter(hooks::Value::BestiaryKeys, m, 0x40 | 0x20) == 0x20);
    for (uint16_t key : {0x10, 0x8, 0x50, 0x48, 0x4})
        CHECK(hooks::filter(hooks::Value::BestiaryKeys, m, key) == key);
}

TEST_CASE("bestiary pictures preserve RAM IO registers in both pictures") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    chrome_fixture::install(w, 2);
    w.set_second(true);
    w.shadow_write16(0xFE00 + 15 * 32 + 2 * 2, 0x000);
    w.shadow_write16(0xFE00 + 15 * 32 + 10 * 2, 0xFFF);
    history(w, 1234, 50, 20);
    Machine m(w);
    BestiaryUi ui;
    ui.install(w);
    for (int scene = 1; scene <= 6; ++scene) {
        hooks::emit(hooks::Event::Bestiary, m, {scene});
        const auto ram = w.ram;
        const auto io = w.io;
        const auto registers = w.cpu.s.r;
        for (bool second : {false, true}) {
            const auto fb = w.framebuffer_rgb(Wsc::ColorMode::Raw, second);
            const auto px = ui.picture(w, Wsc::ColorMode::Raw, second);
            CHECK(px.size() == 224 * 144);
            CHECK(bool(px != fb));
            CHECK(bool(w.framebuffer_rgb(Wsc::ColorMode::Raw, second) == fb));
            CHECK(bool(w.ram == ram));
            CHECK(bool(w.io == io));
            CHECK(w.cpu.s.r == registers);
            CHECK(bool(ui.picture(w, Wsc::ColorMode::Raw, second) == px)); // paused redraw
        }
    }
    CHECK(Chrome::text_width(host_text(w, "ui_bestiary_generation", U"9999")) <= 100);
}
