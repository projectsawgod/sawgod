#include "chrome_fixture.h"
#include "game/inventory_ui.h"
#include "game/session.h"
#include "game/tables.h"
#include "game/ui_chrome.h"
#include <doctest/doctest.h>
using namespace sawgod::game;
namespace fx = chrome_fixture;
using Mode = sawgod::emu::Wsc::ColorMode;

TEST_CASE("inventory scroll markers show only hidden rows, including behind a focused pop-up") {
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    fx::install(w);
    for (bool second : {false, true}) {
        w.set_second(second);
        for (int total : {9, 10})
            for (int first = 0; first <= total - InventoryUi::kVisibleRows; ++first)
                for (bool pointers : {false, true}) {
                    InventoryUi::View view;
                    view.total = total;
                    view.tab = total == 9;
                    view.first = view.selected = first;
                    auto pixels = w.framebuffer_rgb(Mode::Raw, second);
                    Chrome chrome(w, pixels, Mode::Raw, second);
                    std::vector<int> markers;
                    Chrome::observe = [&](const auto &d) {
                        if (d.kind == Chrome::Draw::Kind::ScrollMarker) {
                            CHECK(d.x == (d.y == 20 && !view.tab ? 100 : 212));
                            CHECK_FALSE(d.pointer);
                            markers.push_back(d.y);
                        }
                    };
                    const auto ram = w.ram;
                    InventoryUi::draw_list(chrome, w, view, pointers);
                    Chrome::observe = {};
                    std::vector<int> expected;
                    if (first > 0)
                        expected.push_back(20);
                    if (first + InventoryUi::kVisibleRows < total)
                        expected.push_back(119);
                    CHECK(markers == expected);
                    CHECK(w.ram == ram);
                }
    }
}

TEST_CASE("inventory presentation suspends for native actions, exits, resets and subscription teardown") {
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    Machine m(w);
    fx::install(w);
    auto &session = Session::of(w);
    InventoryUi ui;
    ui.install(w);
    CHECK_FALSE(ui.active());
    CHECK(hooks::filter(hooks::Value::InventoryKeys, m, 0x200) == 0x200);
    hooks::emit(hooks::Event::InventoryMenu, m, {1});
    CHECK(ui.active());
    CHECK(hooks::filter(hooks::Value::InventoryKeys, m, 0x200) == 0);
    CHECK(hooks::filter(hooks::Value::InventoryKeys, m, 0x800) == 0);
    CHECK(hooks::filter(hooks::Value::InventoryKeys, m, 0x804) == 4);   // preserve confirm priority
    CHECK(hooks::filter(hooks::Value::InventoryKeys, m, 0x50) == 0x50); // native up/down unchanged
    w.ram[0xD840] = 3;
    hooks::emit(hooks::Event::InventoryMenu, m, {2});
    CHECK(ui.active());
    CHECK(ui.view(w).source == 3);
    CHECK(hooks::filter(hooks::Value::InventoryKeys, m, 0x200) == 0x200); // move destination stays in its tab
    hooks::emit(hooks::Event::InventoryMenu, m, {0});
    CHECK_FALSE(ui.active());
    CHECK(hooks::filter(hooks::Value::InventoryKeys, m, 0x800) == 0x800);
    CHECK(ui.picture(w, Mode::Raw) == w.framebuffer_rgb(Mode::Raw));
    hooks::emit(hooks::Event::InventoryMenu, m, {1});
    ui.set_enabled(false);
    CHECK(hooks::filter(hooks::Value::InventoryKeys, m, 0x200) == 0x200);
    ui.set_enabled(true);
    session.new_generation();
    ui.frame(w);
    CHECK_FALSE(ui.active());
    CHECK(hooks::filter(hooks::Value::InventoryKeys, m, 0x200) == 0x200);
    {
        InventoryUi temporary;
        temporary.install(w);
    }
    CHECK(hooks::filter(hooks::Value::InventoryKeys, m, 0x200) == 0x200);
}

TEST_CASE("eight inventory rows scroll to every native slot and preserve independent tab selection") {
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    InventoryUi ui;
    ui.set_visible(w, true);
    for (int tab = 0; tab < 2; ++tab) {
        w.ram[0xD852] = uint8_t(tab);
        for (int slot = 0; slot < (tab ? 9 : 10); ++slot) {
            w.ram[0xD842 + tab] = uint8_t(slot);
            const auto v = ui.view(w);
            CHECK(v.selected == slot);
            CHECK(v.first <= slot);
            CHECK(v.first + 8 > slot);
            CHECK(v.first <= v.total - 8);
        }
    }
    w.ram[0xD852] = 0;
    auto v = ui.view(w);
    CHECK(v.selected == 9);
    CHECK(v.first == 2);
    CHECK(v.detail.name == host_text(w, "ui_inventory_trash"));
    w.ram[0xD842] = 8;
    CHECK(ui.view(w).detail.name == host_text(w, "ui_inventory_crystals"));
    w.ram[0xD842] = 0; // native wrap to top
    CHECK(ui.view(w).first == 0);
    w.ram[0xD852] = 1;
    CHECK(ui.view(w).selected == 8);
    CHECK(ui.view(w).first == 1);
}

TEST_CASE("inventory displays host names and flavor intact, counts once, and modded stat fields") {
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    auto &s = Session::of(w);
    std::string error;
    REQUIRE(s.text().load("key,text\nitem_0,Shimmering Longsword\nitemdesc_0,A blade shining in the dark.\n",
                          Charset::system(), &error, nullptr, nullptr, true));
    REQUIRE(s.tables().load_csv("items", "id,flags,action,power\n0,8,0,20\n", w, &error));
    w.ram[0xD07C] = 0;
    w.ram[0xD07D] = 50;
    InventoryUi ui;
    auto r = ui.view(w).detail;
    CHECK(r.name == U"Shimmering Longsword");
    CHECK(r.count == U"50");
    CHECK(r.stats == U"Defense 20");
    CHECK(r.flavor == U"A blade shining in the dark.");
    CHECK(r.stats.find(U"Uses") == std::u32string::npos);
    CHECK(Chrome::text_width(r.name) <= 170);
    CHECK(Chrome::text_width(r.flavor) <= 212);
    w.ram[0xD07D] = 254;
    CHECK(ui.view(w).detail.count == U"--");
    w.ram[0xD07D] = 0;
    CHECK(ui.view(w).detail.count == U"0");
    w.ram[0xD07D] = 255;
    CHECK(ui.view(w).detail.name == U"Empty");
    CHECK(ui.view(w).detail.flavor.empty());
    w.ram[0xD852] = 1;
    w.ram[0xD08C] = 0;
    w.ram[0xD08D] = 1;
    CHECK(ui.view(w).detail.name == r.name);
    CHECK(ui.view(w).detail.count.empty());
    CHECK(ui.view(w).detail.stats == U"Key item");
    w.ram[0xD852] = 0;
    w.ram[0xD07D] = 3;
    REQUIRE(s.tables().load_csv("items", "id,flags,action,power\n0,2,0,254\n", w, &error));
    CHECK(ui.view(w).detail.stats == U"Field item"); // revive byte is not defense
    REQUIRE(s.tables().load_csv("items", "id,flags,action,power\n0,3,25,253\n", w, &error));
    CHECK(ui.view(w).detail.stats == U"Status effect"); // cure mask is not power
}

TEST_CASE("joined inventory chrome reverses the foreground and rendering preserves machine state") {
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    fx::install(w);
    InventoryUi ui;
    ui.set_visible(w, true);
    const auto ordinary = w.framebuffer_rgb(Mode::Raw);
    const auto ram = w.ram;
    const auto io = w.io;
    const auto registers = w.cpu.s.r;
    for (int tab = 0; tab < 2; ++tab) {
        w.ram[0xD852] = uint8_t(tab);
        const auto picture = ui.picture(w, Mode::Raw);
        CHECK(picture[20 * 224 + 60] == (tab == 0 ? fx::kFill : fx::kBorder));
        CHECK(picture[20 * 224 + 160] == (tab == 1 ? fx::kFill : fx::kBorder));
        CHECK(picture[DetailLine::kY * 224 + 160] == fx::kBorder);
        CHECK(w.framebuffer_rgb(Mode::Raw) == ordinary);
    }
    w.ram[0xD852] = ram[0xD852];
    CHECK(w.ram == ram);
    CHECK(w.io == io);
    CHECK(w.cpu.s.r == registers);
    w.set_second(true);
    const auto gb_ram = w.ram;
    for (int tab = 0; tab < 2; ++tab) {
        w.ram[0xD852] = uint8_t(tab);
        CHECK(ui.picture(w, Mode::Raw, true).size() == 224 * 144);
    }
    w.ram[0xD852] = gb_ram[0xD852];
    CHECK(w.ram == gb_ram);
}

TEST_CASE("Up focuses inventory tabs, directional switching preserves rows and confirm/back return to list") {
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    Machine m(w);
    InventoryUi ui;
    ui.install(w);
    hooks::emit(hooks::Event::InventoryMenu, m, {1});
    CHECK(ui.keys(w, 0x10) == 0);
    CHECK(ui.view(w).tabs_focused);
    hooks::emit(hooks::Event::InventoryMenu, m, {1}); // emitted every native polling loop
    CHECK(ui.view(w).tabs_focused);
    CHECK(ui.keys(w, 0x20) == 0x20);
    w.ram[0xD852] = 1; // native horizontal handler selects Key Items
    w.ram[0xD843] = 3;
    CHECK(ui.view(w).selected == 3);
    CHECK(ui.keys(w, 0x10) == 0); // Up stays on header
    CHECK(ui.keys(w, 0x40) == 0);
    CHECK_FALSE(ui.view(w).tabs_focused);
    CHECK(ui.view(w).selected == 3);
    CHECK(ui.keys(w, 0x10) == 0x10); // elsewhere, native Up still moves through rows
    w.ram[0xD843] = 0;
    for (uint16_t leave : {uint16_t(4), uint16_t(8), uint16_t(0x40)}) {
        CHECK(ui.keys(w, 0x10) == 0);
        CHECK(ui.view(w).tabs_focused);
        CHECK(ui.keys(w, leave) == 0);
        CHECK_FALSE(ui.view(w).tabs_focused);
    }
    CHECK(ui.keys(w, 0x50) == 0x50); // opposite directions keep native Down priority
    ui.keys(w, 0x10);
    hooks::emit(hooks::Event::InventoryMenu, m, {0});
    hooks::emit(hooks::Event::InventoryMenu, m, {1});
    CHECK_FALSE(ui.view(w).tabs_focused);
    ui.keys(w, 0x10);
    Session::of(w).new_generation();
    ui.frame(w);
    CHECK_FALSE(ui.view(w).tabs_focused);
}

TEST_CASE("chrome audit observes visible ink and leaves the inventory picture unchanged") {
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    fx::install(w);
    std::string error;
    REQUIRE(Session::of(w).text().load("key,text\nui_inventory_items,Items\n", Charset::system(), &error,
                                       nullptr, nullptr, true));
    InventoryUi ui;
    ui.set_visible(w, true);
    const auto expected = ui.picture(w, Mode::Raw);
    const auto ram = w.ram;
    std::vector<Chrome::Draw> draws;
    struct Clear {
        ~Clear() { Chrome::observe = {}; }
    } clear;
    Chrome::observe = [&](const auto &draw) { draws.push_back(draw); };
    CHECK(ui.picture(w, Mode::Raw) == expected);
    CHECK(w.ram == ram);
    CHECK_FALSE(draws.empty());
    bool text_seen = false, cursor_seen = false;
    for (const auto &d : draws) {
        if (d.kind == Chrome::Draw::Kind::Window) {
            CHECK(d.x >= 0);
            CHECK(d.y >= 0); // intermediate connector construction is not part of the output
        }
        if (d.kind == Chrome::Draw::Kind::Text && d.text == U"Items") {
            text_seen = true;
            CHECK(d.width == Chrome::text_width(d.text));
            CHECK(d.left < d.right);
            CHECK(d.top < d.bottom);
            CHECK(d.left >= d.x);
        }
        cursor_seen |= d.kind == Chrome::Draw::Kind::Cursor;
    }
    CHECK(text_seen);
    CHECK(cursor_seen);
}

TEST_CASE("King's inventory names are key ink only in the Items menu Key Items tab") {
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    fx::install(w);
    w.set_second(true);
    for (bool gb : {false, true})
        for (int id : {17, 18, 19})
            for (int tab : {0, 1}) {
                InventoryUi::View view;
                view.tab = tab;
                view.rows[0].id = id;
                view.rows[0].name = U"A";
                std::vector<uint32_t> pixels(224 * 144);
                Chrome chrome(w, pixels, Mode::Raw, gb);
                InventoryUi::draw_list(chrome, w, view);
                CHECK(pixels[27 * 224 + 28] == chrome.color(tab ? Chrome::Ink::KeyItem : Chrome::Ink::Text));
                // Equipment, including Equip's Key Items tab and item dialogs, uses this ordinary list path.
                chrome.item(id, U"A", 17, 50);
                CHECK(pixels[51 * 224 + 28] == chrome.color(Chrome::Ink::Text));
                chrome.item(id, U"A", 17, 70, Chrome::Ink::Muted);
                CHECK(pixels[71 * 224 + 28] == chrome.color(Chrome::Ink::Muted));
            }
}

#include "game/battle_ui.h"
#include "game/item_dialog_ui.h"
#include "game/party_ui.h"
#include "game/service_ui.h"
#include "game/ui_scene.h"
TEST_CASE("King's gear stays ordinary in Status, Equip including Key Items, battle, shops and item dialogs") {
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    fx::install(w);
    w.set_second(true);
    Machine m(w);
    std::string error;
    REQUIRE(Session::of(w).text().load("key,text\nitem_17,A\nitem_18,A\nitem_19,A\n", Charset::system(),
                                       &error, nullptr, nullptr, true));
    PartyUi party;
    ItemDialogUi dialog;
    BattleUi battle;
    ServiceUi shop;
    party.install(w);
    dialog.install(w);
    battle.install(w);
    shop.install(w);
    for (bool gb : {false, true})
        for (int id : {17, 18, 19}) {
            const auto check = [&](auto &ui) {
                std::vector<Chrome::Draw> draws;
                Chrome::observe = [&](const auto &d) {
                    if (d.text == U"A")
                        draws.push_back(d);
                };
                const auto px = ui.picture(w, Mode::Raw, gb);
                Chrome::observe = {};
                REQUIRE_FALSE(draws.empty());
                for (const auto &d : draws) {
                    REQUIRE_FALSE(d.strokes.empty());
                    CHECK(px[d.strokes.front()] != (gb ? 0x505050 : 0xFFD664));
                }
            };
            w.ram[0xD00F] = w.ram[0xD07C] = w.ram[0xD08C] = uint8_t(id);
            w.ram[0xD010] = w.ram[0xD07D] = w.ram[0xD08D] = 254;
            w.ram[0xD852] = w.ram[0xD853] = w.ram[0xD842] = w.ram[0xD843] = 0;
            for (int scene : {3, 5, 6}) {
                hooks::emit(hooks::Event::PartyMenu, m, {scene});
                check(party);
                if (scene != 3) {
                    w.ram[0xD852] = w.ram[0xD855] = 1;
                    check(party);
                    w.ram[0xD852] = 0;
                }
            }
            hooks::emit(hooks::Event::InventoryMenu, m, {1});
            hooks::emit(hooks::Event::ItemMessage, m, {1});
            check(dialog);
            hooks::emit(hooks::Event::ItemMessage, m, {0});
            w.ram[0xD728] = uint8_t(id);
            w.ram[0xD729] = 254;
            w.ram[0xD6E2] = 1;
            hooks::emit(hooks::Event::BattleStart, m);
            hooks::emit(hooks::Event::BattleMenu, m);
            check(battle);
            {
                UiSceneScope selling(m, hooks::Event::ServiceMenu, 3);
                check(shop);
            }
        }
}
