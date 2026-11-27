#include "test_ui_chrome_support.h"

TEST_CASE("chrome title version has steady gray ink with the title's palette fades") {
    for (bool second : {false, true}) {
        Wsc w(std::vector<uint8_t>(0x400000), 0);
        fx::install(w);
        w.set_second(second);
        for (int fade = 0; fade < 4; ++fade) {
            const uint16_t palette = uint16_t((second ? fade : 3 - fade) * 0x555);
            constexpr uint16_t address = 0xFE00 + 14 * 32 + 5 * 2;
            if (second)
                w.shadow_write16(address, palette);
            else {
                w.ram[address] = uint8_t(palette);
                w.ram[address + 1] = uint8_t(palette >> 8);
            }
            for (auto mode : {Wsc::ColorMode::Raw, Wsc::ColorMode::Lcd}) {
                CAPTURE(second);
                CAPTURE(fade);
                CAPTURE(mode);
                std::vector<uint32_t> pixels(224 * 144);
                Chrome chrome(w, pixels, mode, second);
                const uint32_t raw[] = {128, 85, 42, 0}, lcd[] = {120, 80, 40, 0};
                const uint32_t gb_raw[] = {85, 85, 170, 255}, gb_lcd[] = {80, 80, 160, 240};
                const auto expected = second ? (mode == Wsc::ColorMode::Raw ? gb_raw : gb_lcd)
                                             : (mode == Wsc::ColorMode::Raw ? raw : lcd);
                for (int strength : {0, 50, 100}) {
                    chrome.title_hint_strength(strength);
                    CHECK(chrome.color(Chrome::Ink::TitleVersion) == expected[fade] * 0x010101);
                }
            }
        }
    }
}

TEST_CASE("chrome secondary text stays dim when the battle's muted palette entry is white") {
    for (bool second : {false, true}) {
        Wsc w(std::vector<uint8_t>(0x400000), 0);
        fx::install(w);
        w.set_second(second);
        // Reproduce the battle palette's white Muted entry. GB has black text on white paper.
        for (int pal : {7, 15}) {
            const auto write = [&](int index, uint16_t color) {
                const auto address = uint16_t(0xFE00 + pal * 32 + index * 2);
                if (second)
                    w.shadow_write16(address, color);
                else {
                    w.ram[address] = uint8_t(color);
                    w.ram[address + 1] = uint8_t(color >> 8);
                }
            };
            write(7, 0xFFF);
            write(2, second ? 0x000 : 0xFFF);
            write(1, 0x000);
            if (second)
                write(10, 0xFFF);
        }
        const auto ram = w.ram;
        for (auto mode : {Wsc::ColorMode::Raw, Wsc::ColorMode::Lcd}) {
            std::vector<uint32_t> px(224 * 144);
            Chrome c(w, px, mode, second);
            for (auto style : {Chrome::Window::Normal, Chrome::Window::Inactive}) {
                const auto ink = c.color(Chrome::Ink::Secondary, style);
                CHECK(ink != c.color(Chrome::Ink::Text, style));
                if (mode == Wsc::ColorMode::Raw)
                    CHECK(ink == (second ? 0x555555 : 0xAAAAAA));
            }
        }
        CHECK(w.ram == ram);
    }
}

TEST_CASE("chrome draws nothing until the game's window tiles are loaded") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    std::vector<uint32_t> px(Wsc::kWidth * Wsc::kHeight, 0x123456);
    const auto before = px;
    Chrome c(w, px, Wsc::ColorMode::Raw, false);
    CHECK_FALSE(c.ready());
    c.window(0, 0, 64, 32);
    c.text(U"Hello", 4, 4);
    c.bar(4, 20, 30, 8, 5, 10, "5/10");
    c.cursor(0, 0);
    CHECK(px == before);
}

TEST_CASE("chrome windows are the game's nine-slice at any size, transparent only in the fill") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    fx::install(w);
    const auto ram = w.ram;
    for (const auto [width, height] :
         {std::pair{64, 32}, std::pair{30, 13}, std::pair{56, 28}, std::pair{9, 9}}) {
        CAPTURE(width);
        CAPTURE(height);
        std::vector<uint32_t> px(Wsc::kWidth * Wsc::kHeight, 0x123456);
        Chrome c(w, px, Wsc::ColorMode::Raw, false);
        REQUIRE(c.ready());
        c.window(10, 20, width, height);
        for (int x = 10; x < 10 + width; ++x) {
            CHECK(at(px, x, 20) == fx::kBorder);
            CHECK(at(px, x, 20 + height - 1) == fx::kBorder);
        }
        for (int y = 20; y < 20 + height; ++y) {
            CHECK(at(px, 10, y) == fx::kBorder);
            CHECK(at(px, 10 + width - 1, y) == fx::kBorder);
        }
        CHECK(at(px, 11, 21) == fx::kFill);
        CHECK(at(px, 10 + width - 2, 20 + height - 2) == fx::kFill);
        CHECK(at(px, 9, 20) == 0x123456);
        CHECK(at(px, 10 + width, 20) == 0x123456);
    }
    std::vector<uint32_t> px(Wsc::kWidth * Wsc::kHeight, 0x000000);
    Chrome c(w, px, Wsc::ColorMode::Raw, false);
    c.window(0, 0, 32, 32, Chrome::Window::Inactive, 100);
    CHECK(at(px, 0, 0) == fx::kBorder);
    CHECK(at(px, 5, 5) == 0x000000);
    c.window(0, 40, 32, 32, Chrome::Window::Inactive, 50);
    CHECK(at(px, 5, 45) != 0x000000);
    CHECK(at(px, 5, 45) != fx::kInactiveFill);
    c.window(0, 80, 32, 32, Chrome::Window::Inactive);
    CHECK(at(px, 5, 85) == fx::kInactiveFill);
    CHECK(w.ram == ram);
}

TEST_CASE("the GB picture draws thin windows: the frame's outer line over a blank light fill") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    fx::install(w);
    w.set_second(true);
    // The GB frame: a double line (outer and inner) in the stroke index, a grey band, a textured fill.
    for (int t = 0; t < 9; ++t)
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                const bool top = t < 3, bottom = t >= 6, left = t % 3 == 0, right = t % 3 == 2;
                const int dx = left ? x : right ? 7 - x : 9, dy = top ? y : bottom ? 7 - y : 9;
                const int d = std::min(dx, dy);
                fx::put(w, 0x1D5 + t, x, y, d == 0 || d == 3 ? 2 : d < 3 ? 8 : (x + y) % 2 ? 10 : 11, true);
            }
    fx::put(w, 0x1D5, 0, 0, 10, true); // a rounded corner: outside the line
    w.shadow_write16(uint16_t(0xFE00 + 15 * 32 + 8 * 2), 0x555);
    w.shadow_write16(uint16_t(0xFE00 + 15 * 32 + 11 * 2), 0x777);
    w.shadow_write16(uint16_t(0xFE00 + 15 * 32 + 2 * 2), 0x000); // GB text and lines: black
    for (const auto style : {Chrome::Window::Normal, Chrome::Window::Inactive}) {
        std::vector<uint32_t> px(Wsc::kWidth * Wsc::kHeight, 0x123456);
        Chrome c(w, px, Wsc::ColorMode::Raw, true);
        c.window(0, 0, 32, 32, style, 30);
        const uint32_t line = c.color(Chrome::Ink::Text);
        REQUIRE(line == 0x000000);
        CHECK(at(px, 0, 0) == 0x123456); // the corner stays open
        CHECK(at(px, 5, 0) == line);
        CHECK(at(px, 0, 5) == line);
        CHECK(at(px, 31, 20) == line);
        // Black-grey-black just inside the edge, then the lightest grey: inner line, band and
        // texture of the GB frame are gone.
        const uint32_t band = at(px, 1, 10);
        CHECK(band != line);
        CHECK(band != 0xFFFFFFu);
        for (const auto [x, y] : {std::pair{16, 1}, std::pair{1, 1}})
            CHECK(at(px, x, y) == band);
        for (const auto [x, y] : {std::pair{2, 10}, std::pair{16, 2}})
            CHECK(at(px, x, y) == line); // the second line
        for (const auto [x, y] : {std::pair{3, 10}, std::pair{10, 10}, std::pair{16, 3}})
            CHECK(at(px, x, y) == 0xFFFFFF);
    }
}

TEST_CASE("chrome text, roles and labeled bars") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    fx::install(w);
    std::vector<uint32_t> px(Wsc::kWidth * Wsc::kHeight, 0);
    Chrome c(w, px, Wsc::ColorMode::Raw, false);
    CHECK(c.color(Chrome::Ink::Accent) == fx::kAccent);
    CHECK(c.text_width(U"Willowyn") == text_width(U"Willowyn"));
    c.text(U"Willowyn", 0, 0, Chrome::Ink::Text, 20);
    CHECK(std::count(px.begin(), px.begin() + Wsc::kWidth * 10, fx::kText) > 0);
    for (int y = 0; y < 10; ++y)
        for (int x = 21; x < Wsc::kWidth; ++x)
            CHECK(at(px, x, y) == 0u); // clipped
    c.bar(0, 20, 30, 8, 1, 3, "1/3");
    CHECK(at(px, 0, 20) == fx::kHpFull);
    CHECK(at(px, 9, 20) == fx::kHpFull);
    CHECK(at(px, 10, 20) == fx::kHpEmpty);
    CHECK(at(px, 29, 27) == fx::kHpEmpty);
    int label = 0;
    for (int y = 20; y < 28; ++y)
        for (int x = 0; x < 30; ++x)
            label += at(px, x, y) == fx::kText;
    CHECK(label > 0);
}

TEST_CASE("item chrome follows native mapping, reads the source atlas, reserves blanks and measures inline "
          "arguments") {
    std::vector<uint8_t> rom(0x400000);
    // Two items: a sword symbol and the native FF blank. The atlas is intentionally NOT in video RAM.
    rom[0x396EE0 + 32] = 1;
    rom[0x396EE0 + 28] = 255;
    rom[0x34C0E9] = 0x81;
    rom[0x34C0EA] = 0x1E; // native map word: tile 81, palette 15
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 4; ++x)
            rom[0x3924A0 + y * 4 + x] = 0x22;
    // The GB game's name for item 32 starts with its circle symbol (0x43): GB symbol tile 3, deliberately
    // unlike the WSC square.
    rom[0x334640 + 32 * 8] = 0x43;
    rom[0x32BF30] = 0x80;
    rom[0x32BF31] = 0x80;
    Wsc w(rom, 0);
    w.io[0xC0] = 3;
    fx::install(w);
    w.set_second(true);
    const auto ram = w.ram;
    const auto io = w.io;
    const auto cpu = w.cpu.s;
    CHECK(Chrome::item_width(U"Sword") == Chrome::text_width(U"Sword") + 10);
    const auto argument = Chrome::item_run(32, U"Sword");
    CHECK(Chrome::text_width(U"Got " + argument + U"!") == Chrome::text_width(U"Got Sword!") + 10);
    CHECK(Chrome::text_width(argument + U" " + argument) == Chrome::text_width(U"Sword Sword") + 20);
    CHECK(Chrome::text_width(U"A{n}B") > 0); // a brace that is no token is measured, not expanded forever
    for (bool gb : {false, true}) {
        std::vector<uint32_t> px(224 * 144, 0x123456);
        std::vector<Chrome::Draw> draws;
        Chrome::observe = [&](const auto &d) { draws.push_back(d); };
        Chrome c(w, px, Wsc::ColorMode::Raw, gb);
        c.item(32, U"Sword", 17, 26, Chrome::Ink::Text, 170);
        c.item(28, U"Blank", 17, 38, Chrome::Ink::Text, 170);
        c.text(U"Got " + argument + U"!", 5, 70, Chrome::Ink::Text, 212);
        Chrome::observe = {};
        using K = Chrome::Draw::Kind;
        const auto name =
            std::find_if(draws.begin(), draws.end(), [](const auto &d) { return d.text == U"Sword"; });
        REQUIRE(name != draws.end());
        CHECK(name->x == 27);
        CHECK(name->y == 26);
        CHECK(name->budget == 160);
        CHECK(std::count_if(draws.begin(), draws.end(),
                            [](const auto &d) { return d.kind == K::ItemIcon; }) == 3);
        for (const auto &d : draws)
            if (d.kind == K::ItemIcon) {
                CHECK(d.width == 8);
                CHECK(d.height == 8);
                CHECK(d.has_icon == (d.item == 32));
                CHECK(d.ink.empty() == (d.item == 28));
                if (gb)
                    for (auto p : d.ink)
                        CHECK((px[p] == 0 || px[p] == 0x555555 || px[p] == 0xAAAAAA || px[p] == 0xFFFFFF));
            }
        // GB style draws the icon of the item's symbol, paper left open; not a grayscale square
        const char *art = Chrome::gb_item_icon(rom[0x396EE0 + 32]);
        REQUIRE(art);
        const auto shade = [](char c) -> uint32_t {
            return c == '#' ? 0 : c == '+' ? 0x555555 : c == '-' ? 0xAAAAAA : 0x123456;
        };
        CHECK(at(px, 17, 26) == (gb ? shade(art[0]) : fx::kText));
        CHECK(at(px, 18, 26) == (gb ? shade(art[1]) : fx::kText));
        CHECK(w.ram == ram);
        CHECK(w.io == io);
        CHECK(std::equal(std::begin(w.cpu.s.r), std::end(w.cpu.s.r), std::begin(cpu.r)));
        CHECK(std::equal(std::begin(w.cpu.s.seg), std::end(w.cpu.s.seg), std::begin(cpu.seg)));
    }
}

TEST_CASE("Chrome can omit a dense label's shadow without changing strokes or measured width") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    chrome_fixture::install(w, 2);
    // The window fixture alone has no font; seed kana stroke/shadow samples as Chrome detects them.
    fx::put(w, 0x10B, 0, 0, 2);
    fx::put(w, 0x10B, 1, 1, 1);
    fx::put(w, 0x10B, 0, 1, 1);
    std::vector<uint32_t> normal(224 * 144), plain = normal;
    Chrome a(w, normal, Wsc::ColorMode::Raw, false), b(w, plain, Wsc::ColorMode::Raw, false);
    std::vector<Chrome::Draw> draws;
    Chrome::observe = [&](const auto &d) { draws.push_back(d); };
    a.text(U"Tiger", 20, 20);
    b.text(U"Tiger", 20, 20, Chrome::Ink::Text, 224, Chrome::Window::Normal, false);
    Chrome::observe = {};
    REQUIRE(draws.size() == 2);
    CHECK(draws[0].width == draws[1].width);
    CHECK(draws[0].strokes == draws[1].strokes);
    CHECK(draws[1].ink.size() < draws[0].ink.size());
    CHECK(draws[1].bottom < draws[0].bottom);
}

TEST_CASE("hand picture audit uses visible tops and keeps captioned rows on the text rule") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    fx::install(w);
    w.set_second(true);
    for (bool second : {false, true}) {
        // Visible top at tile row 1, with blank padding on row 0. Hand top is row 0; fingertip rows 1-2.
        fx::put(w, 0x180, 0, 1, 2, second);
        fx::put(w, 0x1FB, 0, 0, 2, second);
        fx::put(w, 0x1FC, 7, 1, 2, second);
        fx::put(w, 0x1FC, 7, 2, 2, second);
        std::vector<uint32_t> px(224 * 144, 0);
        Chrome c(w, px, Wsc::ColorMode::Raw, second);
        std::array<uint8_t, 32> gb{};
        gb[4] = 0x20;
        const std::array<uint16_t, 16> grays = {0, 0xFFF, 0xAAA, 0};
        const auto check = [&](int hand_y, bool caption) {
            std::vector<Chrome::Draw> draws;
            Chrome::observe = [&](const auto &d) { draws.push_back(d); };
            if (second)
                c.tile(gb.data(), grays, 8, 30, 20);
            else
                c.tile(uint16_t((15 << 9) | 0x180), 30, 20, true);
            if (caption)
                c.text(U"Target", 38, 20);
            c.hand(8, hand_y);
            Chrome::observe = {};
            const auto targets = ui_pilot::pointer_targets(draws);
            REQUIRE(targets[0].box.top == 20);
            REQUIRE(targets[0].ink_top == 21);
            std::vector<std::string> errors;
            ui_pilot::pointer_rules(draws, px, second, errors);
            return errors;
        };
        CHECK(check(21, false).empty());
        for (int hand_y : {20, 22, 26}) {
            const auto errors = check(hand_y, false);
            CHECK(std::any_of(errors.begin(), errors.end(), [](const auto &e) {
                return e.find("is not flush with picture top row 21") != std::string::npos;
            }));
        }
        // Text y=20 keeps hand y=22, even though that is one row below the picture's visible top.
        CHECK(check(22, true).empty());
    }
}

TEST_CASE("arrow art sits one pixel lower in WSC and retains the GB midpoint") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    fx::install(w);
    w.set_second(true);
    for (bool second : {false, true}) {
        // The native arrow's two-row tip: shadow above, stroke below.
        fx::put(w, 0x1EE, 7, 3, 1, second);
        fx::put(w, 0x1EE, 7, 4, 2, second);
        std::vector<uint32_t> px(224 * 144, 0);
        Chrome c(w, px, Wsc::ColorMode::Raw, second);
        std::vector<Chrome::Draw> draws;
        Chrome::observe = [&](const auto &d) { draws.push_back(d); };
        c.text(U"Target", 17, 20);
        c.cursor(6, 20);
        c.more_below(212, 119);
        Chrome::observe = {};
        CHECK(px[size_t((23 + !second) * 224 + 13)] == c.color(Chrome::Ink::Text));
        std::vector<std::string> errors;
        ui_pilot::pointer_rules(draws, px, second, errors);
        CHECK(errors.empty());
        // One row too low used to pass the 1 px tolerance. The black-row rule must reject it.
        auto &arrow = draws[1];
        ++arrow.y;
        for (auto &at : arrow.ink)
            at += 224;
        ui_pilot::pointer_rules(draws, px, second, errors);
        CHECK(std::any_of(errors.begin(), errors.end(), [](const auto &e) {
            return e.find("black tip row is not on the middle") != std::string::npos;
        }));
        // An even-height icon has a middle between rows: preserve the native tip's two rows.
        draws.clear();
        Chrome::observe = [&](const auto &d) { draws.push_back(d); };
        c.item(-1, U"Target", 17, 20);
        c.cursor(6, 20, true, 8);
        Chrome::observe = {};
        errors.clear();
        ui_pilot::pointer_rules(draws, px, second, errors);
        CHECK(errors.empty());
        CHECK(px[size_t((24 + !second) * 224 + 13)] == c.color(Chrome::Ink::Text));
        const auto ordinary = px;
        c.cursor(6, 20, c.arrow(), 8);
        CHECK(px == ordinary); // retained battle arrow uses the same placement as live art.
    }
}

TEST_CASE("GB window geometry follows atlas edits while colors stay live") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    fx::install(w, 2);
    w.set_second(true);
    const auto draw = [&] {
        std::vector<uint32_t> px(224 * 144, 0x123456);
        Chrome c(w, px, Wsc::ColorMode::Raw, true);
        c.window(0, 0, 64, 32);
        return px;
    };
    const auto before = draw();
    CHECK(before[0] != 0x123456);
    fx::put(w, 0x1D5, 0, 0, 0, true);
    const auto hole = draw();
    CHECK(hole[0] == 0x123456);
    fx::put(w, 0x1D5, 0, 0, 2, true);
    w.shadow_write16(0xFE00 + 15 * 32 + 2 * 2, 0);
    const auto recolored = draw();
    CHECK(recolored[0] == 0);
    CHECK(recolored[0] != before[0]);
}

TEST_CASE("window fill records spans, palette endpoints, clip, opacity and final ownership") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    fx::install(w);
    // Three fill indices, with distinct endpoint colors.
    fx::put(w, 0x1D9, 0, 0, 11);
    fx::put(w, 0x1D9, 1, 0, 12);
    w.ram[0xFE00 + 15 * 32 + 11 * 2] = 0x44;
    w.ram[0xFE00 + 15 * 32 + 11 * 2 + 1] = 0x04;
    std::vector<uint32_t> px(224 * 144, 0x123456);
    Wsc::UiFill fill;
    w.ui_fill = &fill;
    Chrome c(w, px, Wsc::ColorMode::Raw, false);
    c.clip(12, 21, 20, 20, [&] { c.window(10, 20, 64, 32, Chrome::Window::Normal, 30); });
    REQUIRE(fill.windows.size() == 1);
    CHECK(fill.windows[0].top == 20);
    CHECK(fill.windows[0].bottom == 52);
    CHECK(fill.windows[0].alpha == 70);
    CHECK(fill.windows[0].light == fx::kAccent);
    CHECK(fill.windows[0].dark == 0x0000E5);
    CHECK(fill.owner[20 * 224 + 12] == 0);
    CHECK(fill.visible(21 * 224 + 12, px[21 * 224 + 12]) == 1);
    CHECK(fill.visible(21 * 224 + 12, 0xABABAB) == 0);
    c.window(10, 20, 64, 32, Chrome::Window::Normal, 30);
    // Translucent over a window's fill: that fill is replaced too, so this window's gradient goes opaque
    // there.
    REQUIRE(fill.windows.size() == 2);
    CHECK(fill.windows[1].alpha == 100);
    CHECK(fill.owner[21 * 224 + 12] == 2);
    c.window(10, 20, 64, 32, Chrome::Window::Normal, 30);
    CHECK(fill.windows.size() == 2); // identical calls reuse an entry
    const auto before = fill.owner;
    c.text(U"Text", 15, 25);
    size_t dropped = 0;
    for (size_t at = 0; at < px.size(); ++at)
        if (before[at] && !fill.visible(at, px[at]))
            ++dropped;
    CHECK(dropped > 0);
    fill.clear();
    c.tabbed_window(0, 8, 224, 120, 20, 100, 0);
    REQUIRE(fill.windows.size() == 1);
    CHECK(fill.windows[0].top == 8);
    CHECK(fill.windows[0].bottom == 128);
    for (size_t at = 0; at < px.size(); ++at)
        if (fill.owner[at])
            CHECK(fill.visible(at, px[at]) == 1);
    CHECK(fill.owner[15 * 224 + 20] == fill.owner[60 * 224 + 20]);
    fill.clear();
    c.speaker_window(0, 76, 224, 68, 70);
    REQUIRE(fill.windows.size() == 1);
    CHECK(fill.windows[0].top == 76);
    CHECK(fill.windows[0].bottom == 144);
    CHECK(fill.owner[80 * 224 + 180] == 0); // restored scene outside speaker tab
    CHECK(fill.owner[100 * 224 + 180] == 1);
    w.set_second(true);
    Chrome gb(w, px, Wsc::ColorMode::Raw, true);
    const auto owners = fill.owner;
    gb.window(0, 0, 224, 144);
    CHECK(fill.owner == owners);
    CHECK(fill.windows.size() == 1);
}

TEST_CASE(
    "icons retain backing fill through encoded background and index zero, but never through foreground") {
    std::vector<uint8_t> rom(0x400000);
    rom[0x396EE0 + 32] = 1;
    rom[0x34C0E9] = 0x81;
    rom[0x34C0EA] = 0x1E;
    rom[0x3924A0] = 0xA2; // item icon: encoded window fill followed by real ink
    Wsc w(rom, 0);
    w.io[0xC0] = 3;
    fx::install(w);
    fx::put(w, 0x1D1, 0, 0, 10); // heart's clear notch is a nonzero fill index
    fx::put(w, 0x1D1, 1, 0, 2);
    fx::put(w, 0x1EE, 0, 0, 10); // same rule for a sprite-palette cursor
    fx::put(w, 0x1EE, 1, 0, 2);
    Wsc::UiFill fill;
    w.ui_fill = &fill;
    std::vector<uint32_t> px(224 * 144);
    Chrome c(w, px, Wsc::ColorMode::Raw, false);
    c.window(0, 0, 224, 144);
    const auto backing = fill.owner;
    c.tile(uint16_t(15 << 9 | 0x1D1), 10, 20);
    c.tile(uint16_t(7 << 9 | 0x1EE), 30, 20, false, true);
    c.item(32, U"Sword", 50, 20);
    for (int x : {10, 30, 50}) {
        CHECK(fill.visible(20 * 224 + x, px[20 * 224 + x]) == backing[20 * 224 + x]);
        CHECK(fill.owner[20 * 224 + x + 1] == 0);                         // opaque icon ink
        CHECK(fill.owner[20 * 224 + x + 2] == backing[20 * 224 + x + 2]); // index 0
    }
    for (int transparency : {30, 100}) {
        fill.clear();
        px[25 * 224 + 85] = 0x123456;
        c.window(80, 20, 32, 32, Chrome::Window::Normal, transparency);
        const auto beneath = px[25 * 224 + 85];
        c.tile(uint16_t(15 << 9 | 0x1D1), 85, 25);
        CHECK(px[25 * 224 + 85] == beneath);
        CHECK(fill.visible(25 * 224 + 85, beneath) != 0);
    }
    c.tile(uint16_t(15 << 9 | 0x1D1), 10, 20, true); // explicitly opaque
    CHECK(fill.owner[20 * 224 + 10] == 0);
    c.tile(uint16_t(15 << 9 | 0x1D1), 30, 20, false, false, true); // character art
    CHECK(fill.owner[20 * 224 + 30] == 0);
    c.tile(uint16_t(15 << 9 | 0x1D1), 10, 20); // old ownership must not resurrect
    CHECK(fill.owner[20 * 224 + 10] == 0);
}

TEST_CASE("frameless window fill keeps tile-grid phase and records HD fill beneath numeral shadows") {
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    chrome_fixture::install(w);
    chrome_fixture::put(w, 0x10B, 0, 0, 1); // the WSC font casts a shadow
    // A patterned fill makes the absolute tile-grid phase observable at a non-aligned plate origin.
    chrome_fixture::put(w, 0x1D9, 3, 1, 7);
    std::vector<uint32_t> picture(224 * 144, chrome_fixture::kEmpty);
    sawgod::emu::Wsc::UiFill fill;
    sawgod::emu::Wsc::UiText text;
    w.ui_fill = &fill;
    w.ui_text = &text;
    Chrome c(w, picture, sawgod::emu::Wsc::ColorMode::Raw, false);
    c.window_fill(11, 17, 37, 8);
    CHECK(picture[17 * 224 + 11] == 0x777777);
    CHECK(picture[17 * 224 + 12] == chrome_fixture::kFill);
    REQUIRE(fill.windows.size() == 1);
    CHECK(fill.windows[0].top == 17);
    CHECK(fill.windows[0].bottom == 25);
    c.numerals("231/231", 13, 18);
    bool shadow = false, stroke = false;
    for (int y = 0; y < 144; ++y)
        for (int x = 0; x < 224; ++x) {
            const size_t at = size_t(y * 224 + x);
            if (x < 11 || x >= 48 || y < 17 || y >= 25) {
                CHECK(picture[at] == chrome_fixture::kEmpty);
                CHECK(fill.owner[at] == 0);
            } else if (text.visible(at, picture.data()) == sawgod::emu::Wsc::UiText::Shadow) {
                shadow = true;
                CHECK(fill.owner[at] == 1);
            } else if (text.visible(at, picture.data()) == sawgod::emu::Wsc::UiText::Stroke) {
                stroke = true;
                CHECK(fill.owner[at] == 0);
            }
        }
    CHECK(shadow);
    CHECK(stroke);
    // GB uses the same compact geometry with its light window fill and shadowless black ink.
    w.set_second(true);
    w.shadow_write8(0x4000 + 0x10B * 32, 0);
    fill.clear();
    text.clear();
    std::fill(picture.begin(), picture.end(), chrome_fixture::kEmpty);
    Chrome gb(w, picture, sawgod::emu::Wsc::ColorMode::Raw, true);
    gb.window_fill(11, 17, 37, 8);
    CHECK(picture[17 * 224 + 11] == 0xFFFFFF);
    gb.numerals("231/231", 13, 18);
    CHECK(fill.windows.empty());
    CHECK(picture[18 * 224 + 14] == gb.color(Chrome::Ink::Text));
}

TEST_CASE("Enhanced pilot selection requires luminance shape and checks finished text strokes") {
    using namespace ui_pilot;
    std::vector<uint32_t> output(size_t(enhanced_width * enhanced_height), 0x111111);
    std::vector<uint32_t> ink;
    for (int y = 10; y < 14; ++y)
        for (int x = 10; x < 14; ++x)
            ink.push_back(uint32_t(y * 224 + x));
    const auto paint = [&](uint32_t color) {
        for (auto at : ink)
            for (int y = 0; y < 3; ++y)
                for (int x = 0; x < 3; ++x)
                    output[(at / 224 * 3 + size_t(y)) * enhanced_width + at % 224 * 3 + size_t(x)] = color;
    };
    CHECK_FALSE(visible_shape(output, ink)); // missing/overwritten cursor
    paint(0x2A0000);                         // different hue, almost the same luminance
    CHECK_FALSE(visible_shape(output, ink));
    paint(0xFFFFFF);
    CHECK(visible_shape(output, ink));
    const SDL_Rect selected{20, 30, 56, 32}, peer{100, 30, 56, 32};
    CHECK_FALSE(distinct_windows(output, selected, peer));
    for (int y = selected.y * 3; y < (selected.y + selected.h) * 3; ++y)
        for (int x = selected.x * 3; x < (selected.x + selected.w) * 3; ++x)
            output[size_t(y * enhanced_width + x)] = 0xFFFFFF;
    CHECK(distinct_windows(output, selected, peer));
    std::vector<uint32_t> native(224 * 144, 0x111111);
    Wsc::UiText text;
    native[10 * 224 + 10] = 0xFFFFFF;
    text.pixels[10 * 224 + 10] = {Wsc::UiText::Stroke, 0xFFFFFF, 0x111111};
    std::vector<std::string> errors;
    enhanced_rules(output, native, text, {}, errors);
    CHECK(errors.empty());
    output[30 * enhanced_width + 30] = 0;
    enhanced_rules(output, native, text, {}, errors);
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].find("text strokes") != std::string::npos);
}

TEST_CASE("Enhanced pilot uses real fill and soft shadow passes without mutating the machine") {
    using namespace ui_pilot;
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    fx::install(w);
    auto sentinel_text = std::make_unique<Wsc::UiText>();
    auto sentinel_fill = std::make_unique<Wsc::UiFill>();
    w.ui_text = sentinel_text.get();
    w.ui_fill = sentinel_fill.get();
    std::vector<RenderResult> results;
    append_enhanced(w, RenderResult{"test--0", "wsc-en", "test", "", {}, 0}, {}, results, [&] {
        std::vector<uint32_t> native(224 * 144, 0);
        Chrome c(w, native, Wsc::ColorMode::Raw, false);
        c.window(0, 0, 224, 144);
        c.text(U"Text", 20, 20);
        return native;
    });
    REQUIRE(results.size() == 1);
    CHECK(results[0].variant == "enh-en");
    CHECK(results[0].errors.empty());
    CHECK(w.ui_text == sentinel_text.get());
    CHECK(w.ui_fill == sentinel_fill.get());
    // A synthetic gradient verifies this is not just nearest-neighbor enlargement.
    Wsc::UiFill fill;
    Wsc::UiText text;
    fill.windows.push_back({0, 144, 0xAAAAAA, 0x111111, 100});
    std::vector<uint32_t> native(224 * 144, 0x555555);
    std::fill(fill.owner.begin(), fill.owner.end(), 1);
    fill.color = native;
    EnhancedPicture presenter;
    const auto pixels = presenter.draw(native, fill, text);
    CHECK(pixels.size() == size_t(672 * 432));
    bool subpixel_gradient = false;
    for (int row = 1; row < 143; ++row)
        subpixel_gradient |= pixels[(row * 3) * 672 + 100] != pixels[(row * 3 + 2) * 672 + 100];
    CHECK(subpixel_gradient);
    CHECK(pixels[100 * 672 + 100] != native[100 / 3 * 224 + 100 / 3]);
}

TEST_CASE("HP meter colors follow the window palette black fade") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    fx::install(w);
    const auto palette = w.ram;
    for (auto mode : {Wsc::ColorMode::Raw, Wsc::ColorMode::Lcd})
        for (int step : {0, 1, 7, 15}) {
            for (int i = 0; i < 16; ++i) {
                const int at = 0xFE00 + 15 * 32 + i * 2;
                const int original = palette[size_t(at)] | palette[size_t(at + 1)] << 8;
                uint16_t faded = 0;
                for (int shift : {0, 4, 8})
                    faded |= uint16_t(std::max(0, (original >> shift & 15) - step) << shift);
                w.ram[size_t(at)] = uint8_t(faded);
                w.ram[size_t(at + 1)] = uint8_t(faded >> 8);
            }
            std::vector<uint32_t> px(224 * 144);
            Chrome c(w, px, mode, false);
            c.bar(10, 10, 32, 8, 1, 2, "1/2");
            const auto faded = [step](uint32_t color) {
                uint32_t out = 0;
                for (int shift : {0, 8, 16})
                    out |= uint32_t(std::max(0, int(color >> shift & 255) - 17 * step)) << shift;
                return out;
            };
            CHECK(at(px, 10, 14) == faded(Chrome::kPartyHp.full));
            CHECK(at(px, 41, 14) == faded(Chrome::kPartyHp.empty));
            if (step == 15)
                CHECK(std::all_of(px.begin(), px.end(), [](auto pixel) { return pixel == 0; }));
        }
}

TEST_CASE("concave tab piping and speaker coverage stay aligned with the window") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    fx::install(w, 2);
    // A convex lower-left slice: gray inside its vertical pipe, black below its horizontal pipe.
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            fx::put(w, 0x1DB, x, y, y == 7 ? 6 : y == 6 || x == 0 ? 2 : x == 1 ? 7 : 10);
    Wsc::UiText text;
    Wsc::UiFill fill;
    std::array<uint8_t, 224 * 144> coverage{};
    w.ui_text = &text;
    w.ui_fill = &fill;
    w.ui_coverage = &coverage;
    std::vector<uint32_t> px(224 * 144, 0x123456);
    Chrome c(w, px, Wsc::ColorMode::Raw, false);
    c.speaker_window(0, 76, 224, 68, 70);
    REQUIRE(text.corners.size() == 1);
    const auto &corner = text.corners.front();
    CHECK(at(px, corner.x + 1, corner.y + 2) == 0x222222);
    CHECK(at(px, corner.x + 5, corner.y + 7) == 0x777777);
    CHECK(corner.bevel == 0x222222);
    CHECK(corner.shadow == 0x777777);
    CHECK(std::all_of(coverage.begin(), coverage.begin() + 76 * 224, [](auto value) { return value == 0; }));
    CHECK(coverage[91 * 224 + 180] == 0);
    CHECK(coverage[92 * 224 + 180] == 255);
}

TEST_CASE("GB HP labels fade their black ink into the picture beneath them") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    fx::install(w);
    w.set_second(true);
    w.shadow_write16(0xFE00 + 15 * 32 + 2 * 2, 0); // black GB text
    const auto palette = w.ram;
    for (int step : {0, 7, 15}) {
        for (int i = 0; i < 16; ++i) {
            const int at = 0xFE00 + 15 * 32 + i * 2;
            const int original = palette[size_t(at)] | palette[size_t(at + 1)] << 8;
            uint16_t faded = 0;
            for (int shift : {0, 4, 8})
                faded |= uint16_t(std::max(0, (original >> shift & 15) - step) << shift);
            w.ram[size_t(at)] = uint8_t(faded);
            w.ram[size_t(at + 1)] = uint8_t(faded >> 8);
        }
        std::vector<uint32_t> px(224 * 144, 0xFFFFFF);
        Chrome c(w, px, Wsc::ColorMode::Raw, true);
        c.bar(10, 10, 32, 8, 1, 1, "1");
        const unsigned alpha = unsigned(100 * (15 - step) / 15);
        const unsigned gray = 255 * (100 - alpha) / 100;
        CHECK(at(px, 26, 11) == gray * 0x010101);
        if (step == 15)
            CHECK(std::all_of(px.begin(), px.end(), [](auto pixel) { return pixel == 0xFFFFFF; }));
    }
}

#include "game/text_tokens.h"
TEST_CASE("key A matches the locked bitmap, text shadow, inline placement and scoped name ink") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    fx::install(w);
    w.set_second(true);
    // Independent transcription of the locked seven-row sprite.
    constexpr std::string_view art = "..HGG..."
                                     ".HG#GS.."
                                     ".GGGSS.."
                                     "...G...."
                                     "..LG...."
                                     "...G...."
                                     "..LG....";
    const auto shade = [](char v, bool gb) -> uint32_t {
        if (gb)
            return v == '#' ? 0xF0F0F0 : 0xA0A0A0;
        return v == 'H'   ? 0xFFEC96
               : v == 'G' ? 0xFCC438
               : v == 'S' ? 0xD0881C
               : v == 'L' ? 0xFFD664
                          : 0x100A06;
    };
    for (bool gb : {false, true}) {
        std::vector<uint32_t> px(224 * 144, 0x123456), expected = px;
        Chrome c(w, px, Wsc::ColorMode::Raw, gb);
        std::vector<Chrome::Draw> inline_draws;
        Chrome::observe = [&](const auto &d) { inline_draws.push_back(d); };
        c.text(Chrome::item_run(20, U"Air Seed") + U"!", 20, 30);
        Chrome::observe = {};
        CHECK(std::any_of(inline_draws.begin(), inline_draws.end(), [](const auto &d) {
            return d.kind == Chrome::Draw::Kind::ItemIcon && d.item == 20 && d.x == 18 && d.y == 30;
        }));
        // Trimmed key starts at pen-1, so source cell column 1 sits at x=19.
        for (int pass = gb ? 1 : 0; pass < 2; ++pass)
            for (int y = 0; y < 7; ++y)
                for (int x = 1; x <= 5; ++x)
                    if (art[y * 8 + x] != '.') {
                        const int at = (30 + y) * 224 + 18 + x;
                        if (!pass) {
                            expected[size_t(at + 1)] = 0x101010;
                            expected[size_t(at + 224)] = 0x101010;
                        } else
                            expected[size_t(at)] = shade(art[y * 8 + x], gb);
                    }
        for (int y = 29; y < 39; ++y)
            for (int x = 18; x < 25; ++x)
                CHECK(at(px, x, y) == at(expected, x, y));
        CHECK(at(px, 26, 31) == (gb ? 0x505050 : 0xFFD664)); // first A stroke
        std::vector<Chrome::Draw> draws;
        Chrome::observe = [&](const auto &d) { draws.push_back(d); };
        c.item(20, U"Air Seed", 17, 50);
        c.item(20, U"Air Seed", 17, 70, Chrome::Ink::Muted);
        Chrome::observe = {};
        const auto name = std::find_if(draws.begin(), draws.end(),
                                       [](const auto &d) { return d.text == U"Air Seed" && d.y == 50; });
        REQUIRE(name != draws.end());
        CHECK(name->x == 27);
        CHECK(at(px, 28, 51) == (gb ? 0x505050 : 0xFFD664));
        CHECK(at(px, 28, 71) == c.color(Chrome::Ink::Muted));
        // Following punctuation is ordinary text, including its original shadow policy.
        std::vector<uint32_t> plain(224 * 144, 0x123456);
        Chrome p(w, plain, Wsc::ColorMode::Raw, gb);
        const int bang = 20 + 5 + Chrome::text_width(U"Air Seed") - 2;
        p.text(U"!", bang, 30);
        for (int y = 30; y < 40; ++y)
            for (int x = bang + 1; x < bang + Chrome::text_width(U"!"); ++x)
                if (at(plain, x, y) != 0x123456)
                    CHECK(at(px, x, y) == at(plain, x, y));
    }
}

TEST_CASE("King's named tokens keep equipment icons at cap top and eight pixel advance while hints use key A") {
    std::vector<uint8_t> rom(0x400000);
    for (int symbol : {1, 8, 9}) {
        rom[0x34C0E7 + symbol * 2] = uint8_t(0x80 + symbol);
        rom[0x34C0E8 + symbol * 2] = 0x1E;
        for (int y = 0; y < 8; ++y)
            rom[0x392480 + symbol * 32 + y * 4] = 0x22;
    }
    Wsc w(rom, 0);
    w.io[0xC0] = 3;
    fx::install(w);
    w.set_second(true);
    for (bool gb : {false, true})
        for (int id : {17, 18, 19}) {
            std::vector<uint32_t> named(224 * 144, 0x123456), ordinary = named, hint = named, key = named;
            Chrome n(w, named, Wsc::ColorMode::Raw, gb), o(w, ordinary, Wsc::ColorMode::Raw, gb),
                h(w, hint, Wsc::ColorMode::Raw, gb), k(w, key, Wsc::ColorMode::Raw, gb);
            n.text(marked_item_run(id, U"A") + U"!", 20, 30);
            o.item(id, U"", gb && id == 19 ? 18 : 19, 30);
            for (int y = 30; y < 38; ++y)
                for (int x = 19; x < 27; ++x)
                    CHECK(named[y * 224 + x] == ordinary[y * 224 + x]);
            CHECK(named[31 * 224 + 29] == n.color(Chrome::Ink::KeyItem));
            h.text(expand_key_items(utf8_to_u32("{key:item_" + std::to_string(id) + ":A}"),
                                    [](int) { return U"unused"; }),
                   20, 30);
            k.text(marked_item_run(20, U"A"), 20, 30);
            CHECK(hint == key);
        }
}

TEST_CASE("seal chrome uses a seven-pixel orb, per-row ink and four GB grays") {
    const uint32_t inks[] = {0x9696A8, 0x78B8FF, 0xFFEEC8, 0xE83830};
    const uint32_t ramp[] = {0xFFF6C4, 0xFBE8AA, 0xF7D990, 0xF4CB76, 0xF0BD5C, 0xECAE42, 0xE8A028};
    const uint32_t bodies[] = {0x9696A8, 0x78B8FF, 0xE6D6B4, 0xE83830, 0xFCC438};
    const uint32_t shades[] = {0x5D5D68, 0x4A729E, 0x8F8570, 0x90231E, 0xD0881C};
    const uint32_t lights[] = {0xCACAD4, 0xBCDCFF, 0xF2EADA, 0xF49C98, 0xFFEC96};
    for (bool second : {false, true}) {
        Wsc w(std::vector<uint8_t>(0x400000), 0);
        fx::install(w);
        w.set_second(second);
        for (int seal = 0; seal < 5; ++seal) {
            std::vector<uint32_t> pixels(224 * 144);
            Chrome chrome(w, pixels, Wsc::ColorMode::Lcd, second);
            chrome.text(std::u32string(1, kInlineSeal + seal) + U"H" + kSealEnd, 10, 20);
            for (int row = 0; row < 7; ++row) {
                for (int col = 0; col < 7; ++col) {
                    const char c = kSealOrbArt[row * 7 + col];
                    if (c != '.') {
                        const auto expected = second     ? (c == 'S'   ? 0x505050
                                                            : c == 'W' ? 0xA0A0A0
                                                                       : 0xF0F0F0)
                                              : c == 'W' ? bodies[seal]
                                              : c == 'S' ? shades[seal]
                                              : c == 'L' ? lights[seal]
                                                         : 0xFFFFFF;
                        CHECK(pixels[(20 + row) * 224 + 11 + col] == expected);
                    }
                }
                // H's left stroke is present on every cap-to-baseline row, 10 px after the orb pen.
                CHECK(pixels[(20 + row) * 224 + 21] == (second      ? 0xA0A0A0
                                                        : seal == 4 ? ramp[row]
                                                                    : inks[seal]));
            }
            CHECK(pixels[27 * 224 + 14] == (second ? 0 : 0x101010));
            if (second)
                for (auto pixel : pixels)
                    CHECK((pixel == 0 || pixel == 0x505050 || pixel == 0xA0A0A0 || pixel == 0xF0F0F0));
        }
    }
}
