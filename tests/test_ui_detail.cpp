#include "chrome_fixture.h"
#include "game/ui_detail.h"
#include <doctest/doctest.h>
#include <stdexcept>
using namespace sawgod::game;
using sawgod::emu::Wsc;

TEST_CASE("chrome clipping intersects nested viewports and restores after exceptions") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    chrome_fixture::install(w, 2);
    w.set_second(true);
    w.shadow_write16(0xFE00 + 15 * 32 + 2 * 2, 0x000);
    w.shadow_write16(0xFE00 + 15 * 32 + 10 * 2, 0xFFF);
    const auto ram = w.ram;
    const auto io = w.io;
    const auto fb = w.framebuffer_rgb(Wsc::ColorMode::Raw);
    for (bool second : {false, true}) {
        std::vector<uint32_t> px(224 * 144, 0x123456);
        Chrome c(w, px, Wsc::ColorMode::Raw, second);
        c.clip(5, 132, 213, 8, [&] {
            c.text(U"A long description scrolling through its viewport", -50, 132);
            c.clip(10, 130, 10, 14, [&] { c.window(0, 0, 224, 144); });
        });
        CHECK(px != std::vector<uint32_t>(224 * 144, 0x123456));
        bool escaped = false;
        for (int y = 0; y < 144; ++y)
            for (int x = 0; x < 224; ++x)
                if (x < 5 || x >= 218 || y < 132 || y >= 140)
                    escaped |= px[size_t(y * 224 + x)] != 0x123456;
        CHECK_FALSE(escaped);
        const auto before = px;
        c.clip(0, 0, 0, 100, [&] { c.tabbed_window(0, 0, 224, 128, 20, 112, 0); });
        CHECK(px == before);
        CHECK_THROWS_AS(c.clip(10, 10, 10, 10, [] { throw std::runtime_error("test"); }), std::runtime_error);
        c.window(0, 0, 32, 32);
        CHECK(px[5] != 0x123456);
    }
    CHECK(w.ram == ram);
    CHECK(w.io == io);
    CHECK(w.framebuffer_rgb(Wsc::ColorMode::Raw) == fb);
}

TEST_CASE("shared detail holds 151 frames, accumulates half pixels and returns to flavor") {
    DetailLine d;
    const std::u32string flavor = U"A bright blade.", stats = U"Power 20   STR";
    const int travel = Chrome::text_width(flavor + U"  " + stats) - Chrome::text_width(stats); // two spaces
    const auto tick = [&](uint64_t age) {
        d.update(100 + age, 1, flavor, stats);
        return d.offset();
    };
    CHECK(tick(0) == 0);
    CHECK(tick(150) == 0);
    CHECK(tick(151) == 0);
    CHECK(tick(152) == 1);
    CHECK(tick(153) == 1);
    CHECK(tick(150 + 2 * travel) == travel);
    CHECK(tick(301 + 2 * travel) == travel);
    CHECK(tick(302 + 2 * travel) == travel);
    CHECK(tick(303 + 2 * travel) == travel - 1);
    CHECK(tick(302 + 4 * travel) == 0);
    CHECK(tick(302 + 4 * travel + 150) == 0);
    tick(200);
    d.update(301, 2, flavor, stats);
    CHECK(d.offset() == 0);
    d.update(500, 2, flavor, stats);
    CHECK(d.offset() > 0);
    d.update(500, 2, U"A modded description.", stats);
    CHECK(d.offset() == 0);
    d.update(50, 2, U"A modded description.", stats);
    CHECK(d.offset() == 0);
    d.reset();
    d.update(9000, 2, flavor, stats);
    CHECK(d.offset() == 0);
}

TEST_CASE("detail endpoints draw flavor and stats with identical frame geometry in both pictures") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    chrome_fixture::install(w, 2);
    w.set_second(true);
    w.shadow_write16(0xFE00 + 15 * 32 + 2 * 2, 0x000);
    w.shadow_write16(0xFE00 + 15 * 32 + 10 * 2, 0xFFF);
    const auto ram = w.ram;
    for (bool second : {false, true}) {
        std::vector<uint32_t> a(224 * 144, 0x123456), b = a, expected = a;
        Chrome ca(w, a, Wsc::ColorMode::Raw, second);
        Chrome cb(w, b, Wsc::ColorMode::Raw, second);
        Chrome ce(w, expected, Wsc::ColorMode::Raw, second);
        DetailLine d;
        d.update(0, 0, U"Flavor", U"Defense 20");
        d.draw(ca);
        const int travel = Chrome::text_width(U"Flavor     Defense 20") - Chrome::text_width(U"Defense 20");
        d.update(151 + 2 * travel, 0, U"Flavor", U"Defense 20");
        d.draw(cb);
        // 17 px, the text's ten rows whole (descenders and their shadow), at y+4
        const int y0 = DetailLine::kY;
        CHECK(y0 == 127);
        ce.window(0, y0, 224, DetailLine::kHeight);
        ce.clip(5, y0 + 4, 213, 10, [&] { ce.text(U"Defense 20", 5, y0 + 4); });
        CHECK(bool(b == expected));
        CHECK(bool(a != b));
        // no descender is cut: "Empty gypsy" draws the same with and without the viewport
        std::vector<uint32_t> cut(224 * 144, 0x123456), whole = cut;
        Chrome cc(w, cut, Wsc::ColorMode::Raw, second), cw(w, whole, Wsc::ColorMode::Raw, second);
        DetailLine g;
        g.update(0, 1, U"Empty gypsy jq", U"");
        int stroke_bottom = 0;
        Chrome::observe = [&](const auto &draw) {
            if (draw.kind == Chrome::Draw::Kind::Text)
                for (auto at : draw.strokes)
                    stroke_bottom = std::max(stroke_bottom, int(at / 224));
        };
        g.draw(cc);
        Chrome::observe = {};
        if (second) {
            CHECK(stroke_bottom == 139);
            CHECK(144 - 3 - stroke_bottom - 1 == 1); // one empty row before the first bottom-frame row
            CHECK(cut[size_t(140 * 224 + 25)] == 0xFFFFFF);
            CHECK(cut[size_t(141 * 224 + 25)] == 0x000000);
        }
        cw.window(0, y0, 224, DetailLine::kHeight);
        cw.text(U"Empty gypsy jq", 5, y0 + 4);
        CHECK(bool(cut == whole));
        for (int y = 0; y < y0 + 4; ++y)
            CHECK(a[size_t(y * 224 + 50)] == b[size_t(y * 224 + 50)]);
    }
    CHECK(w.ram == ram);
}

TEST_CASE("detail handles empty slots, flavor-only entries and stats-only entries") {
    DetailLine d;
    d.update(0, 0, U"", U"");
    d.update(500, 0, U"", U"");
    CHECK(d.offset() == 0);
    d.update(500, 1, U"", U"Key item");
    d.update(1000, 1, U"", U"Key item");
    CHECK(d.offset() == 0);
    d.update(1000, 2, U"Crystals", U"");
    d.update(1500, 2, U"Crystals", U"");
    CHECK(d.offset() == 0);
}

TEST_CASE("detail supports the Bestiary narrow flavor viewport without touching its back hint") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    chrome_fixture::install(w, 2);
    std::vector<uint32_t> px(224 * 144, 0x123456);
    Chrome c(w, px, Wsc::ColorMode::Raw, false);
    DetailLine d;
    const std::u32string flavor(50, U'W');
    const int travel = Chrome::text_width(flavor) - 152;
    d.update(0, 0, flavor, U"", 152);
    d.update(151 + 2 * travel, 0, flavor, U"", 152);
    CHECK(d.offset() == travel);
    d.draw(c, DetailLine::kY, 60, 164);
    for (int y = 0; y < 144; ++y)
        for (int x = 0; x < 224; ++x)
            if (y < DetailLine::kY || x < 60)
                REQUIRE(px[size_t(y * 224 + x)] == 0x123456);
    // Switching viewport width starts its own hold; paused redraw cannot advance it.
    d.update(151 + 2 * travel, 0, flavor, U"", 212);
    CHECK(d.offset() == 0);
}
