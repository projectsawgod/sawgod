// The world-map minimap's rules (app/minimap.h) and its settings and button.
#include "app/minimap.h"
#include "engine/config.h"
#include "engine/input.h"
#include "engine/settings.h"
#include <doctest/doctest.h>
#include <string>

using namespace sawgod;
using namespace sawgod::app;

TEST_CASE("Minimap sizes round to whole window pixels per tile and sit in the picture's corner") {
    struct Case {
        float x, y, w, h, scale;
        int small, large;
    };
    // The game picture (main.cpp GameRect): 1728x1111 (a window), 5x, 6x, a letterboxed
    // 2560x1440.
    for (const auto &c : {Case{0, 0, 1728, 1111, 7.714f, 4, 6}, Case{0, 0, 1120, 720, 5, 3, 5},
                          Case{0, 0, 1344, 864, 6, 3, 5}, Case{160, 0, 2240, 1440, 10, 6, 9}}) {
        for (auto size : {MinimapSize::Small, MinimapSize::Large}) {
            const auto l = minimap_layout(c.x, c.y, c.w, c.h, c.scale, size);
            INFO(c.w << "x" << c.h << " " << int(size));
            CHECK(l.tile == (size == MinimapSize::Small ? c.small : c.large));
            CHECK(l.side == 64 * l.tile);
            const int inset = int(std::lround(4 * c.scale));
            CHECK(l.x + l.side + inset == int(c.x + c.w));
            CHECK(l.y + l.side + inset == int(c.y + c.h));
            CHECK(std::abs(float(l.side) / c.h - (size == MinimapSize::Small ? 0.25f : 0.375f)) < 0.08f);
        }
        CHECK(minimap_layout(c.x, c.y, c.w, c.h, c.scale, MinimapSize::Off).side == 0);
    }
    // Ocean and Sky drop one row top and bottom: shorter, still bottom-anchored; the others stay square.
    CHECK(minimap_trim(102).rows == 1);
    CHECK(minimap_trim(102).columns == 0);
    CHECK(minimap_trim(122).rows == 2);
    CHECK(minimap_trim(122).columns == 1);
    CHECK(minimap_trim(88).rows + minimap_trim(88).columns == 0);
    CHECK(minimap_trim(136).rows + minimap_trim(136).columns == 0);
    const auto square = minimap_layout(0, 0, 1728, 1111, 7.714f, MinimapSize::Small);
    const auto trimmed = minimap_layout(0, 0, 1728, 1111, 7.714f, MinimapSize::Small, {2, 1});
    CHECK(square.height == square.side);
    CHECK(trimmed.height == 60 * trimmed.tile);
    CHECK(trimmed.side == 62 * trimmed.tile);
    CHECK(trimmed.x + trimmed.side == square.x + square.side);
    CHECK(trimmed.y + trimmed.height == square.y + square.height);
    // A tiny window still gets one pixel per tile.
    CHECK(minimap_layout(0, 0, 224, 144, 1, MinimapSize::Small).tile == 1);
}

TEST_CASE("The Minimap button cycles Off Small Large and Options On restores the last size") {
    bool on = false, large = false;
    minimap_cycle(on, large);
    CHECK(minimap_size(on, large) == MinimapSize::Small);
    minimap_cycle(on, large);
    CHECK(minimap_size(on, large) == MinimapSize::Large);
    minimap_cycle(on, large);
    CHECK(minimap_size(on, large) == MinimapSize::Off);
    on = true; // Options > Enhanced > Minimap: On
    CHECK(minimap_size(on, large) == MinimapSize::Large);
    minimap_cycle(on, large);
    CHECK(minimap_size(on, large) == MinimapSize::Off);
    minimap_cycle(on, large); // from Off the button always starts Small
    CHECK(minimap_size(on, large) == MinimapSize::Small);
}

TEST_CASE("Minimap settings default to Small, survive a save and show On/Off in Enhanced") {
    Config config;
    Settings settings;
    settings.register_all(config);
    CHECK(settings.minimap->on());
    CHECK(settings.minimap_size->get() == 0);
    CHECK(settings.minimap->category == "Video");
    CHECK(settings.minimap->type == CVar::Type::Bool);
    CHECK(settings.minimap->hidden);
    CHECK(settings.minimap_size->hidden);
    CHECK(config.overlong_help().empty());
    config.set(*settings.minimap, 0);
    config.set(*settings.minimap_size, 1);
    const std::string saved = config.to_toml();
    CHECK(saved.find("minimap = false") != std::string::npos);
    CHECK(saved.find("minimap_size = \"large\"") != std::string::npos);
    Config again;
    Settings loaded;
    loaded.register_all(again);
    again.from_toml(saved);
    CHECK(minimap_size(loaded.minimap->on(), loaded.minimap_size->get() == 1) == MinimapSize::Off);
    again.set(*loaded.minimap, 1);
    CHECK(minimap_size(loaded.minimap->on(), loaded.minimap_size->get() == 1) == MinimapSize::Large);
}

TEST_CASE("The Minimap button defaults to M and R3 without taking another button's input") {
    using K = ui::Binding::Kind;
    const auto m = ui::InputMap::defaults();
    CHECK(m.keys[int(ui::WscButton::Minimap)][0] == ui::Binding{K::Key, SDL_SCANCODE_M});
    CHECK(m.pad[int(ui::WscButton::Minimap)][0] == ui::Binding{K::PadButton, SDL_GAMEPAD_BUTTON_RIGHT_STICK});
    CHECK(std::string(ui::wsc_button_id(ui::WscButton::Minimap)) == "minimap");
    for (int i = 0; i < ui::kWscButtonCount; ++i) {
        if (i == int(ui::WscButton::Minimap))
            continue;
        for (const auto &b : m.keys[size_t(i)])
            CHECK_FALSE(b == m.keys[int(ui::WscButton::Minimap)][0]);
        for (const auto &b : m.pad[size_t(i)])
            CHECK_FALSE(b == m.pad[int(ui::WscButton::Minimap)][0]);
    }
    // Rebindable like the rest: a file's binding loads and saves.
    Config config;
    config.from_toml("[keyboard]\nminimap = [\"N\"]\n");
    ui::InputMap loaded = ui::InputMap::defaults();
    loaded.load(config);
    CHECK(loaded.keys[int(ui::WscButton::Minimap)][0] == ui::Binding{K::Key, SDL_SCANCODE_N});
}

TEST_CASE("The minimap shows only while the party walks a world map in the Enhanced camera") {
    MinimapScene walking;
    walking.world_map = walking.field = walking.mode7 = true;
    CHECK(minimap_context(walking));
    CHECK(minimap_visible(walking, MinimapSize::Small));
    CHECK(minimap_visible(walking, MinimapSize::Large));
    CHECK_FALSE(minimap_visible(walking, MinimapSize::Off));
    const auto without = [&](auto change) {
        auto s = walking;
        change(s);
        return s;
    };
    // Towns and dungeons, GB/WSC graphics (no world camera), not the field.
    for (const auto &s :
         {without([](auto &s) { s.world_map = false; }), without([](auto &s) { s.mode7 = false; }),
          without([](auto &s) { s.field = false; }), without([](auto &s) { s.battle = true; }),
          without([](auto &s) { s.menu = true; }), without([](auto &s) { s.dialogue = true; }),
          without([](auto &s) { s.transition = true; })}) {
        CHECK_FALSE(minimap_context(s)); // the button is not read there either
        CHECK_FALSE(minimap_visible(s, MinimapSize::Small));
    }
    // Fast-Forward hides it, but the button still works on the world map.
    const auto ff = without([](auto &s) { s.fast_forward = true; });
    CHECK(minimap_context(ff));
    CHECK_FALSE(minimap_visible(ff, MinimapSize::Small));
}

TEST_CASE("Minimap fades over 0.2 s, the party dot breathes and marks the party's tile") {
    MinimapFade fade;
    CHECK(fade.update(true, 1'000'000'000) == 0);
    CHECK(fade.update(true, 1'100'000'000) == doctest::Approx(0.5f));
    CHECK(fade.update(true, 1'200'000'000) == 1);
    CHECK(fade.update(true, 1'900'000'000) == 1);
    CHECK(fade.update(false, 1'950'000'000) == doctest::Approx(0.75f));
    CHECK(fade.update(true, 2'000'000'000) == doctest::Approx(1.f)); // reversing is continuous
    CHECK(fade.update(false, 2'300'000'000) == 0);
    fade.settle(true, 0);
    CHECK(fade.amount() == 1);
    // Rests at the muted 80% white; a short smooth dip to 30% and back every 2.5 s, never off.
    CHECK(minimap_marker_level(0) == doctest::Approx(kMarkerPeak));
    CHECK(minimap_marker_level(kMarkerDipNs / 2) == doctest::Approx(kMarkerLow));
    CHECK(minimap_marker_level(kMarkerDipNs) == doctest::Approx(kMarkerPeak));
    CHECK(minimap_marker_level(kMarkerPeriodNs - 1) == doctest::Approx(kMarkerPeak));
    CHECK(minimap_marker_level(kMarkerPeriodNs + kMarkerDipNs / 2) == doctest::Approx(kMarkerLow));
    for (uint64_t t = 0; t < kMarkerPeriodNs; t += 10'000'000) {
        CHECK(minimap_marker_level(t) >= kMarkerLow - 1e-4f);
        CHECK(minimap_marker_level(t) <= kMarkerPeak + 1e-4f);
        CHECK(std::abs(minimap_marker_level(t + 10'000'000) - minimap_marker_level(t)) < 0.05f); // smooth
    }
    // The game keeps scroll = ((x - 7) * 16 + 8, (y - 4) * 16) for the player's cell (x, y): the dot's tile.
    for (auto [x, y] : {std::pair{0, 0}, {35, 30}, {63, 63}, {12, 50}}) {
        const auto tile = minimap_party_tile(float((x - 7) * 16 + 8), float((y - 4) * 16));
        CHECK(tile[0] == x);
        CHECK(tile[1] == y);
        // Halfway through a step east or south the dot moves on.
        CHECK(minimap_party_tile(float((x - 7) * 16 + 8 + 8), float((y - 4) * 16 + 8))[0] ==
              std::min(63, x + 1));
        CHECK(minimap_party_tile(float((x - 7) * 16 + 8 + 8), float((y - 4) * 16 + 8))[1] ==
              std::min(63, y + 1));
    }
    CHECK(minimap_party_tile(-1000, 5000)[0] == 0); // clamped into the map
    CHECK(minimap_party_tile(-1000, 5000)[1] == 63);
    CHECK(MinimapLook{}.brightness == 0.8f);
    CHECK(MinimapLook{}.opacity == 0.9f);
}
