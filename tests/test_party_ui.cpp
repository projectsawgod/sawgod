#include "chrome_fixture.h"
#include "game/game_config.h"
#include "game/party_menu.h"
#include "game/party_ui.h"
#include "game/session.h"
#include <doctest/doctest.h>
using namespace sawgod::game;
using sawgod::emu::Wsc;

TEST_CASE("party presentation follows native menu scenes and hides on exits and reset") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    Machine m(w);
    PartyUi ui;
    ui.install(w);
    CHECK_FALSE(ui.active());
    for (int scene = 1; scene <= 6; ++scene) {
        hooks::emit(hooks::Event::PartyMenu, m, {scene});
        CHECK(ui.active());
        CHECK(int(ui.scene()) == scene);
    }
    hooks::emit(hooks::Event::PartyMenu, m, {0});
    CHECK_FALSE(ui.active());
    CHECK(bool(ui.picture(w, Wsc::ColorMode::Raw) == w.framebuffer_rgb(Wsc::ColorMode::Raw)));
    hooks::emit(hooks::Event::PartyMenu, m, {3});
    Session::of(w).new_generation();
    ui.frame(w);
    CHECK_FALSE(ui.active());
    {
        PartyUi temporary;
        temporary.install(w);
    }
    hooks::emit(hooks::Event::PartyMenu, m, {1});
    CHECK(ui.active()); // another subscription's teardown did not remove ours
}

TEST_CASE("party lists read human gear, mutant four plus four, and all eight monster abilities") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    Machine m(w);
    auto &s = Session::of(w);
    std::string error;
    REQUIRE(s.text().load("key,text\nitem_0,Gear\nitem_1,Ability\n", Charset::system(), &error, nullptr,
                          nullptr, true));
    PartyUi ui;
    ui.install(w);
    for (int i = 0; i < 8; ++i) {
        w.ram[0xD00F + i * 2] = uint8_t(i < 4 ? 1 : 0);
        w.ram[0xD010 + i * 2] = 50;
    }
    hooks::emit(hooks::Event::PartyMenu, m, {3});
    CHECK(ui.view(w).count == 8);
    CHECK_FALSE(ui.view(w).abilities);
    w.ram[0xD00A] = 0x23;
    auto v = ui.view(w);
    CHECK(v.count == 4);
    CHECK(v.abilities);
    CHECK(v.rows[0].name == U"Ability");
    w.ram[0xD842] = 4;
    v = ui.view(w);
    CHECK(v.base == 4);
    CHECK_FALSE(v.abilities);
    CHECK(v.rows[0].name == U"Gear");
    w.ram[0xD00A] = 0x43;
    v = ui.view(w);
    CHECK(v.monster);
    CHECK(v.abilities);
    CHECK(v.count == 8);
}

TEST_CASE("equip inventory shows six rows and follows selection through all key-item slots") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    Machine m(w);
    PartyUi ui;
    ui.install(w);
    hooks::emit(hooks::Event::PartyMenu, m, {5});
    w.ram[0xD852] = w.ram[0xD855] = 1;
    for (int selected = 0; selected < 9; ++selected) {
        w.ram[0xD843] = uint8_t(selected);
        const auto v = ui.view(w);
        CHECK(v.count == 6);
        CHECK(v.inventory);
        CHECK(v.key);
        CHECK(v.first <= selected);
        CHECK(selected < v.first + v.count);
        CHECK(v.first <= 3);
    }
    CHECK(ui.view(w).first == 3);
    w.ram[0xD843] = 0;
    CHECK(ui.view(w).first == 0);
    w.ram[0xD855] = 0;
    w.ram[0xD843] = 7;
    CHECK(ui.view(w).first == 2);
}

TEST_CASE("opaque field and equipment pictures preserve RAM IO registers and both ordinary pictures") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    chrome_fixture::install(w, 2);
    w.set_second(true);
    w.shadow_write16(0xFE00 + 15 * 32 + 2 * 2, 0x000);
    w.shadow_write16(0xFE00 + 15 * 32 + 10 * 2, 0xFFF);
    auto &s = Session::of(w);
    std::string error;
    REQUIRE(s.text().load("key,text\nmonster_0,Human\n", Charset::system(), &error, nullptr, nullptr, true));
    s.player_names().values = {"WWWMMMMW", "Beatrice", "Cornelia", "Dimitria"};
    GameConfig config;
    config.set(s, true); // production app has six categories
    Machine m(w);
    PartyUi ui;
    ui.install(w);
    for (int scene = 1; scene <= 6; ++scene) {
        hooks::emit(hooks::Event::PartyMenu, m, {scene});
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
    for (const auto &key : {"ui_party_current_floor", "ui_party_highest_floor"})
        CHECK(Chrome::text_width(host_text(w, key, U"23F")) <= 154);
    CHECK(Chrome::text_width(host_text(w, "ui_party_status")) <= 48);
}

TEST_CASE("party order shows the hovered member's stats, the hand on the hovered member, read-only") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    chrome_fixture::install(w, 2);
    auto &s = Session::of(w);
    std::string error;
    REQUIRE(s.text().load("key,text\nmonster_0,Human\n", Charset::system(), &error, nullptr, nullptr, true));
    s.player_names().values = {"Will", "Beatrice", "Cornelia", "Dimitria"};
    for (int i = 0; i < 4; ++i)
        for (int stat = 0; stat < 4; ++stat)
            w.ram[size_t(0xD00B + i * 31 + stat)] = uint8_t(10 * i + stat);
    // Sprite tile for the native hand (0x1FB): a distinct pixel only the hand draws.
    w.ram[0x4000 + 0x1FB * 32] = 0x30;
    Machine m(w);
    PartyUi ui;
    ui.install(w);
    hooks::emit(hooks::Event::PartyMenu, m, {7});
    CHECK(ui.scene() == PartyUi::Scene::OrderSource);
    w.ram[0xD840] = 0;
    w.ram[0xD842] = 2;
    const auto ram = w.ram;
    const auto io = w.io;
    const auto source = ui.picture(w, Wsc::ColorMode::Raw);
    hooks::emit(hooks::Event::PartyMenu, m, {8});
    CHECK(ui.scene() == PartyUi::Scene::OrderDestination);
    const auto destination = ui.picture(w, Wsc::ColorMode::Raw);
    // The hand appears on the hovered row (member 2) only while a destination is chosen.
    int changed = 0;
    for (int y = 68; y < 84; ++y)
        for (int x = 4; x < 20; ++x)
            changed += source[size_t(y * 224 + x)] != destination[size_t(y * 224 + x)];
    CHECK(changed > 0);
    // The stats column follows the hovered member.
    w.ram[0xD842] = 3;
    CHECK(bool(ui.picture(w, Wsc::ColorMode::Raw) != destination));
    w.ram[0xD842] = 2;
    w.set_second(true);
    CHECK(ui.picture(w, Wsc::ColorMode::Raw, true).size() == 224 * 144);
    CHECK(bool(w.ram == ram));
    CHECK(bool(w.io == io));
    hooks::emit(hooks::Event::PartyMenu, m, {0});
    // A final close releases the host picture immediately; nested scopes restore their parent.
    CHECK_FALSE(ui.active());
    CHECK(bool(ui.picture(w, Wsc::ColorMode::Raw, true) == w.framebuffer_rgb(Wsc::ColorMode::Raw, true)));
    for (const auto &key : {"ui_party_order_source", "ui_party_order_target"})
        CHECK(Chrome::text_width(host_text(w, key)) <= 212);
}

TEST_CASE("Equip destination has one live hand and only a distinct visible source anchor") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    chrome_fixture::install(w, 2);
    w.set_second(true);
    PartyUi ui;
    ui.install(w);
    Machine m(w);
    hooks::emit(hooks::Event::PartyMenu, m, {6});
    using Kind = Chrome::Draw::Kind;
    for (bool second : {false, true})
        for (bool inventory : {false, true})
            for (bool same_pane : {false, true})
                for (int picked : {0, 1, 7}) {
                    w.ram[0xD852] = uint8_t(inventory);
                    w.ram[0xD854] = uint8_t(same_pane ? inventory : !inventory);
                    w.ram[0xD840] = uint8_t(picked);
                    w.ram[0xD842] = w.ram[0xD843] = 0;
                    std::vector<Chrome::Draw> draws;
                    Chrome::observe = [&](const auto &d) { draws.push_back(d); };
                    ui.picture(w, Wsc::ColorMode::Raw, second);
                    Chrome::observe = {};
                    const auto v = ui.view(w);
                    const int row_y = inventory ? 51 : 32;
                    int arrows = 0, hands = 0;
                    for (const auto &d : draws) {
                        if (d.kind != Kind::Cursor || !d.pointer)
                            continue;
                        if (d.width == 16) {
                            ++hands;
                            CHECK(d.y == row_y + 2);
                        } else {
                            ++arrows;
                            CHECK(d.y == row_y + 11 * picked + !second);
                        }
                    }
                    CHECK(hands == 1);
                    CHECK(arrows == int(same_pane && picked != 0 && picked < v.count));
                    CHECK(std::count_if(draws.begin(), draws.end(), [](const auto &d) {
                              return d.kind == Kind::ScrollMarker && !d.pointer && d.x == 212 && d.y == 119;
                          }) == int(inventory));
                }
}

TEST_CASE("Equip: an empty slot picked and still hovered keeps the arrow and unindented names") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    chrome_fixture::install(w, 2);
    PartyUi ui;
    ui.install(w);
    Machine m(w);
    hooks::emit(hooks::Event::PartyMenu, m, {6});
    using Kind = Chrome::Draw::Kind;
    const int member = w.ram[0xD853] & 3, gear = 0xD000 + member * 31 + 15;
    w.ram[size_t(gear + 2)] = 1; // slot 0 holds item 1; slot 1 is empty (FF uses)
    w.ram[size_t(gear + 3)] = 0xFF;
    w.ram[size_t(gear + 1)] = 5;
    w.ram[0xD852] = w.ram[0xD854] = 0;
    for (bool second : {false, true})
        for (int hovered : {0, 1})
            for (int picked : {0, 1}) {
                w.set_second(second);
                w.ram[0xD840] = uint8_t(picked);
                w.ram[0xD842] = uint8_t(hovered);
                std::vector<Chrome::Draw> draws;
                Chrome::observe = [&](const auto &d) { draws.push_back(d); };
                ui.picture(w, Wsc::ColorMode::Raw, second);
                Chrome::observe = {};
                const bool empty_pick = picked == 1 && hovered == 1;
                int arrows = 0, hands = 0;
                for (const auto &d : draws)
                    if (d.kind == Kind::Cursor && d.pointer)
                        ++(d.width == 16 ? hands : arrows);
                CHECK(hands == int(!empty_pick));
                CHECK(arrows == int(empty_pick || picked != hovered));
                const auto name = std::find_if(draws.begin(), draws.end(), [](const auto &d) {
                    return (d.kind == Kind::ItemIcon || d.kind == Kind::Text || d.kind == Kind::ItemRun) &&
                           d.y >= 32 && d.y < 40 && d.x >= 70 && d.x < 90;
                });
                REQUIRE(name != draws.end());
                CHECK(name->x == (empty_pick ? 74 : 82));
            }
}

TEST_CASE("party menu close releases its picture immediately and nested returns restore the parent") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    chrome_fixture::install(w, 2);
    PartyUi ui;
    ui.install(w);
    Machine m(w);
    for (bool second : {false, true}) {
        w.set_second(second);
        hooks::emit(hooks::Event::PartyMenu, m, {3});
        ui.picture(w, Wsc::ColorMode::Raw, second);
        hooks::emit(hooks::Event::PartyMenu, m, {0});
        CHECK_FALSE(ui.active());
        CHECK(bool(ui.picture(w, Wsc::ColorMode::Raw, second) ==
                   w.framebuffer_rgb(Wsc::ColorMode::Raw, second)));
    }
}

TEST_CASE("party submenu scenes restore parents while shared presentation owns retained pixels") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    chrome_fixture::install(w, 2);
    PartyUi ui;
    ui.install(w);
    Machine m(w);
    hooks::emit(hooks::Event::PartyMenu, m, {0, 1});
    hooks::emit(hooks::Event::PartyMenu, m, {1});
    ui.picture(w, Wsc::ColorMode::Raw);
    hooks::emit(hooks::Event::PartyMenu, m, {0, 2}); // category -> submenu setup
    CHECK_FALSE(ui.active());
    hooks::emit(hooks::Event::InventoryMenu, m, {1}); // Items now owns the picture
    CHECK_FALSE(ui.active());
    hooks::emit(hooks::Event::InventoryMenu, m, {0});
    CHECK_FALSE(ui.active());
    hooks::emit(hooks::Event::PartyMenu, m, {0, 1});
    hooks::emit(hooks::Event::PartyMenu, m, {3});
    ui.picture(w, Wsc::ColorMode::Raw);
    hooks::emit(hooks::Event::PartyMenu, m, {0, -1});
    CHECK(ui.scene() == PartyUi::Scene::Field);
    CHECK(ui.active());
    hooks::emit(hooks::Event::PartyMenu, m, {0, -1});
    CHECK_FALSE(ui.active());
    CHECK(ui.picture(w, Wsc::ColorMode::Raw) == w.framebuffer_rgb(Wsc::ColorMode::Raw));
}

TEST_CASE("party category mapping inserts host actions without changing native dispatch") {
    for (bool config : {false, true})
        for (int hosts : {0, 1, 2}) {
            const PartyMenuRows rows(config, hosts);
            CHECK(rows.count() == (config ? 7 : 6) + hosts);
            CHECK(rows.native(rows.save()) == 6);
            for (int row = 0; row < 5; ++row)
                CHECK(rows.native(row) == row);
            if (config)
                CHECK(rows.native(5) == 5);
            if (hosts)
                CHECK(rows.native(rows.options()) == -1);
            if (hosts == 2)
                CHECK(rows.native(rows.load()) == -1);
        }
}

TEST_CASE("host party rows fit both styles without changing progress") {
    for (bool second : {false, true}) {
        Wsc w(std::vector<uint8_t>(0x400000), 0);
        chrome_fixture::install(w, 2);
        w.set_second(true);
        auto &session = Session::of(w);
        std::string error;
        REQUIRE(session.text().load("key,text\nwait_save_confirmation_2,Press {A} to confirm.\n",
                                    Charset::system(), &error, nullptr, nullptr, true));
        PartyUi ui;
        ui.install(w);
        auto rows = session.hooks().subscribe(hooks::Value::PartyMenuRows,
                                              [](Machine &, int32_t, const hooks::Args &) { return 2; });
        auto config = session.hooks().subscribe(hooks::VB::ConfigTab,
                                                [](Machine &, bool, const hooks::Args &) { return false; });
        Machine m(w);
        w.ram[0xD841] = 7;
        session.player_names().values.fill("Will");
        const auto ram = w.ram;
        for (int scene : {1}) {
            hooks::emit(hooks::Event::PartyMenu, m, {scene});
            std::vector<Chrome::Draw> draws;
            Chrome::observe = [&](const auto &d) { draws.push_back(d); };
            ui.picture(w, Wsc::ColorMode::Raw, second);
            Chrome::observe = {};
            for (int slot = 0; slot < 4; ++slot) {
                const int row = 16 + slot * 25;
                for (auto label : {U"Will", U"0/0"})
                    CHECK(std::any_of(draws.begin(), draws.end(), [&](const auto &d) {
                        return d.kind == Chrome::Draw::Kind::Text && d.text == label && d.y == row + 4;
                    }));
            }
            for (const auto &[label, y] : {std::pair{U"Options", 77}, {U"Save", 88}, {U"Load", 99}})
                CHECK(std::any_of(draws.begin(), draws.end(), [&](const auto &d) {
                    return d.kind == Chrome::Draw::Kind::Text && d.text == label && d.x == 171 && d.y == y;
                }));
            CHECK(std::count_if(draws.begin(), draws.end(), [](const auto &d) {
                      return d.kind == Chrome::Draw::Kind::Cursor && d.pointer;
                  }) == (scene == 1 ? 1 : 0));
            for (const auto &d : draws)
                if (d.kind == Chrome::Draw::Kind::Cursor)
                    for (const auto at : d.ink)
                        CHECK(int(at / 224) < kPartyFooterY);
            CHECK(w.ram == ram);
        }
        session.party_load_pending = true;
        session.new_generation();
        CHECK_FALSE(session.party_load_pending);
    }
}
