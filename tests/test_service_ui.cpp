#include "chrome_fixture.h"
#include "game/service_ui.h"
#include "game/session.h"
#include "game/ui_scene.h"
#include <algorithm>
#include <doctest/doctest.h>
using namespace sawgod::game;
using sawgod::emu::Wsc;

TEST_CASE("service presentation nests confirmations and restores native fallbacks on every exit") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    chrome_fixture::install(w, 2);
    w.set_second(true);
    Machine m(w);
    ServiceUi ui;
    ui.install(w);
    {
        UiSceneScope menu(m, hooks::Event::ServiceMenu, 1);
        CHECK(ui.scene() == ServiceUi::Scene::Shop);
        {
            UiSceneScope buying(m, hooks::Event::ServiceMenu, 2);
            CHECK(ui.scene() == ServiceUi::Scene::Buy);
            {
                UiSceneScope confirmation(m, hooks::Event::ServiceMenu, 4);
                CHECK(ui.scene() == ServiceUi::Scene::Purchase);
            }
            CHECK(ui.scene() == ServiceUi::Scene::Buy);
        }
        CHECK(ui.scene() == ServiceUi::Scene::Shop);
    }
    CHECK_FALSE(ui.active());
    CHECK(bool(ui.picture(w, Wsc::ColorMode::Raw) == w.framebuffer_rgb(Wsc::ColorMode::Raw)));
}
TEST_CASE("services use opaque mode-specific chrome with read-only rendering and reset cleanup") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    chrome_fixture::install(w, 2);
    w.set_second(true);
    Machine m(w);
    auto &s = Session::of(w);
    std::string error;
    REQUIRE(s.text().load("key,text\nitem_0,Hammer\nmonster_0,Human\n", Charset::system(), &error, nullptr,
                          nullptr, true));
    s.player_names().values = {"WWWMMMMW", "Beatrice", "Cornelia", "Dimitria"};
    ServiceUi ui;
    ui.install(w);
    w.ram[0xD854] = 10;
    for (int scene : {1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12}) {
        UiSceneScope menu(m, hooks::Event::ServiceMenu, scene);
        const auto ram = w.ram;
        const auto io = w.io;
        const auto registers = w.cpu.s.r;
        for (bool second : {false, true}) {
            const auto fb = w.framebuffer_rgb(Wsc::ColorMode::Raw, second);
            const auto px = ui.picture(w, Wsc::ColorMode::Raw, second);
            CHECK(px.size() == 224 * 144);
            CHECK(bool(px != fb));
            CHECK(bool(ui.picture(w, Wsc::ColorMode::Raw, second) == px));
            CHECK(bool(w.ram == ram));
            CHECK(bool(w.io == io));
            CHECK(w.cpu.s.r == registers);
            CHECK(bool(w.framebuffer_rgb(Wsc::ColorMode::Raw, second) == fb));
        }
    }
    {
        UiSceneScope menu(m, hooks::Event::ServiceMenu, 8);
        s.new_generation();
        ui.frame(w);
        CHECK_FALSE(ui.active());
    }
}
TEST_CASE("the Guild yields the screen to character creation and shows its refusal in the redesign") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    chrome_fixture::install(w, 2);
    Machine m(w);
    ServiceUi ui;
    ui.install(w);
    UiSceneScope guild(m, hooks::Event::ServiceMenu, 10);
    CHECK(ui.scene() == ServiceUi::Scene::Guild);
    {
        // replace_selected_party_member_core: a living human/esper member cannot be replaced.
        UiSceneScope refusal(m, hooks::Event::ServiceMenu, 12);
        CHECK(ui.scene() == ServiceUi::Scene::Refusal);
        CHECK(bool(ui.picture(w, Wsc::ColorMode::Raw) != w.framebuffer_rgb(Wsc::ColorMode::Raw)));
    }
    {
        // Character creation (new_game_setup) draws its own screens: the native picture shows.
        UiSceneScope creation(m, hooks::Event::ServiceMenu, 0);
        CHECK_FALSE(ui.active());
        CHECK(bool(ui.picture(w, Wsc::ColorMode::Raw) == w.framebuffer_rgb(Wsc::ColorMode::Raw)));
    }
    CHECK(ui.scene() == ServiceUi::Scene::Guild);
}

TEST_CASE("service notices retain their list, suppress selectors, and restore the parent footer") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    chrome_fixture::install(w, 2);
    w.set_second(true);
    Machine m(w);
    ServiceUi ui;
    ui.install(w);
    std::string error;
    REQUIRE(Session::of(w).text().load("key,text\nmenu_error_5,You can't remove yourself!\n",
                                       Charset::system(), &error, nullptr, nullptr, true));
    UiSceneScope guild(m, hooks::Event::ServiceMenu, 10);
    const auto ram = w.ram;
    for (bool second : {false, true}) {
        const auto before = ui.picture(w, Wsc::ColorMode::Raw, second);
        {
            hooks::EventScope notice(m, hooks::Event::ServiceMenu,
                                     {ServiceUi::kNotice, 1, int32_t(TextKey::menu_error_5)}, {0, -1});
            CHECK(ui.scene() == ServiceUi::Scene::Guild);
            std::vector<Chrome::Draw> draws;
            Chrome::observe = [&](const auto &d) { draws.push_back(d); };
            const auto px = ui.picture(w, Wsc::ColorMode::Raw, second);
            Chrome::observe = {};
            CHECK(std::equal(before.begin(), before.begin() + 116 * 224, px.begin()));
            CHECK(std::any_of(draws.begin(), draws.end(),
                              [](const auto &d) { return d.text == U"You can't remove yourself!"; }));
            CHECK(std::none_of(draws.begin(), draws.end(),
                               [](const auto &d) { return d.kind == Chrome::Draw::Kind::Cursor; }));
            CHECK(w.ram == ram);
        }
        CHECK(ui.picture(w, Wsc::ColorMode::Raw, second) == before);
    }
}

TEST_CASE("shop lists mark hidden rows above and below in both graphics styles") {
    for (bool second : {false, true})
        for (int scene : {2, 3, 4, 5}) {
            Wsc w(std::vector<uint8_t>(0x400000), 0);
            chrome_fixture::install(w, 2);
            w.set_second(true);
            Machine m(w);
            ServiceUi ui;
            ui.install(w);
            w.ram[0xD854] = 8;
            UiSceneScope menu(m, hooks::Event::ServiceMenu, scene);
            for (int selected : {0, 5, 7}) {
                w.ram[0xD842] = w.ram[0xD845] = uint8_t(selected);
                std::vector<Chrome::Draw> draws;
                Chrome::observe = [&](const auto &d) { draws.push_back(d); };
                ui.picture(w, Wsc::ColorMode::Raw, second);
                Chrome::observe = {};
                const int count = scene >= 4 ? 4 : 5;
                const int first = selected >= count ? selected - count + 1 : 0;
                CHECK(std::count_if(draws.begin(), draws.end(), [](const auto &d) {
                          return d.kind == Chrome::Draw::Kind::ScrollMarker && d.y == 19;
                      }) == int(first > 0));
                CHECK(std::count_if(draws.begin(), draws.end(), [](const auto &d) {
                          return d.kind == Chrome::Draw::Kind::ScrollMarker && d.y > 19;
                      }) == int(first + count < 8));
                for (const auto &d : draws)
                    if (d.kind == Chrome::Draw::Kind::ScrollMarker) {
                        CHECK_FALSE(d.pointer);
                        CHECK(d.x + d.width < 162);
                        CHECK(d.y + d.height < 16 + count * 12 + 12);
                    }
            }
        }
}
