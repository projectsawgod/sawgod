#include "game/text_render.h"
#include "game/text_wrap.h"
#include "game/ui_chrome.h"
#include <doctest/doctest.h>
using namespace sawgod::game;
TEST_CASE("wrap: greedy and a final runt pulls down one word") {
    auto r = wrap_text(U"one two three four", Chrome::text_width(U"one two three"));
    REQUIRE(r.lines.size() == 2);
    CHECK(r.lines[0].text == U"one two");
    CHECK(r.lines[1].text == U"three four");
}
TEST_CASE("wrap: hard n and lf preserved, cell breaks are spaces, waits do not break") {
    auto r = wrap_text(U"one{wait} two{n}three{lf}four\r\nfive", 214);
    REQUIRE(r.lines.size() == 3);
    CHECK(r.lines[0].text == U"one two");
    CHECK(r.lines[1].text == U"three");
    CHECK(r.lines[2].text == U"four five");
    CHECK(r.cell_break);
    CHECK(wrap_text(U"one{n}", 214).lines.size() == 2);
}
TEST_CASE("wrap: no-break space, too-wide words stay intact and are reported") {
    auto r = wrap_text(U"Sunflower~Garden", 20);
    REQUIRE(r.lines.size() == 1);
    CHECK(r.lines[0].text == U"Sunflower Garden");
    REQUIRE(r.too_wide.size() == 1);
    CHECK(r.too_wide[0].excess == Chrome::text_width(U"Sunflower Garden") - 20);
}
TEST_CASE("wrap: punctuation sticks to its neighbor, em dash offers a break") {
    for (auto c : std::u32string(U".,!?…)”’:")) {
        auto r = wrap_text(U"one two " + std::u32string(1, c) + U" three", Chrome::text_width(U"one two"));
        for (const auto &l : r.lines)
            CHECK((l.text.empty() || l.text.front() != c));
    }
    auto r = wrap_text(U"one ( two three", Chrome::text_width(U"one ("));
    for (const auto &l : r.lines)
        CHECK((l.text.empty() || l.text.back() != U'('));
    r = wrap_text(U"one—two", Chrome::text_width(U"one—"));
    REQUIRE(r.lines.size() == 2);
    CHECK(r.lines[0].text == U"one—");
    CHECK(r.lines[1].text == U"two");
}
TEST_CASE("wrap: runt balancing stops at hard breaks and clear/box") {
    const auto width = Chrome::text_width(U"one two three");
    for (auto end : {U"{n}", U"{clear}", U"{box}"}) {
        auto r = wrap_text(U"one two three four" + std::u32string(end) + U"next", width);
        REQUIRE(r.lines.size() == 3);
        CHECK(r.lines[0].text == U"one two");
        CHECK(r.lines[1].text == U"three four");
    }
}

TEST_CASE("wrap punctuation has production glyphs, including the em dash") {
    for (auto c : std::u32string(U"—“”‘’…"))
        CHECK(Chrome::can_draw(c));
}

TEST_CASE("wrap: no-break wins over an em-dash opportunity and quote groups stay intact") {
    const auto r = wrap_text(U"one—~two", 20);
    REQUIRE(r.lines.size() == 1);
    CHECK(r.lines[0].text == U"one— two");
    for (const auto *text : {U"one \" two three", U"one “ two three"}) {
        const auto q = wrap_text(text, Chrome::text_width(U"one \""));
        for (const auto &line : q.lines)
            CHECK((line.text.empty() || (line.text.back() != U'“' && line.text.back() != U'"')));
    }
}

TEST_CASE("wrap: a closing straight quote still allows a greedy break") {
    const auto r = wrap_text(U"\"one two\" threethree", Chrome::text_width(U"\"one two\""));
    REQUIRE(r.lines.size() == 2);
    CHECK(r.lines[0].text == U"\"one two\"");
    CHECK(r.lines[1].text == U"threethree");
}

TEST_CASE("battle wrap: English sentence-end breaks and no-break group counts") {
    const auto r = wrap_text(U"Name{n}attacks{lf}a group!\r\nNext{n}sentence?{n}Last.", 216, true);
    REQUIRE(r.lines.size() == 3);
    CHECK(r.lines[0].text == U"Name attacks a group!");
    CHECK(r.lines[1].text == U"Next sentence?");
    CHECK(r.lines[2].text == U"Last.");
    CHECK(r.ignored_break);
    CHECK_FALSE(r.cell_break);
    const auto mid = wrap_text(U"Name\nattacks.", 216, true);
    REQUIRE(mid.lines.size() == 1);
    CHECK(mid.lines[0].text == U"Name attacks.");
    CHECK(mid.cell_break);
    const auto group = wrap_text(U"Name attacks LongGroup\u00A0×9 with weapon!", 90, true);
    for (const auto &line : group.lines) {
        CHECK((line.text.find(U"LongGroup") == std::u32string::npos ||
               line.text.find(U"LongGroup ×9") != std::u32string::npos));
        CHECK_FALSE(line.text.starts_with(U"×"));
    }
    CHECK(Chrome::text_width(U"a\u00A0b") == Chrome::text_width(U"a b"));
}

TEST_CASE("Japanese wrap: particles, full-width spaces and explicit original pages") {
    const auto r = wrap_text(U"アシュラは　けんで　こうげきした！", 66, true, true);
    REQUIRE(r.lines.size() == 3);
    CHECK(r.lines[0].text == U"アシュラは");
    CHECK(r.lines[1].text == U"けんで");
    CHECK(r.lines[2].text == U"こうげきした！");
    const auto pages = wrap_text(U"アシュラは{n}けんを　つかった\r\nこうかが　ない", 216, true, true);
    REQUIRE(pages.lines.size() == 3);
    CHECK(pages.lines[0].text == U"アシュラは");
    CHECK(pages.lines[1].text == U"けんを　つかった");
    CHECK_FALSE(pages.ignored_break);
    CHECK_FALSE(pages.cell_break);
}

TEST_CASE("Japanese wrap: kinsoku for kana, prolonged sound marks and brackets") {
    for (char32_t c : std::u32string(U"。、！？ーゃゅょっぁぃぅぇぉャュョッァィゥェォ」』")) {
        const auto r = wrap_text(U"あいうえお" + std::u32string(1, c) + U"かきくけこ", 42, true, true);
        REQUIRE(r.lines.size() > 1);
        for (const auto &line : r.lines)
            CHECK((line.text.empty() || line.text.front() != c));
        CHECK(r.too_wide.empty());
    }
    for (char32_t c : std::u32string(U"「『（")) {
        const auto r = wrap_text(U"あいうえ" + std::u32string(1, c) + U"かきくけこ", 42, true, true);
        for (const auto &line : r.lines)
            CHECK((line.text.empty() || line.text.back() != c));
    }
}

TEST_CASE("Japanese wrap: expanded arguments stay intact, including particles and spaces") {
    const auto name = std::u32string(1, kWrapArgumentStart) + U"はに　まる" + kWrapArgumentEnd;
    const auto r = wrap_text(U"あいうえお　" + name + U"は　つかった", 74, true, true);
    bool found = false;
    for (const auto &line : r.lines) {
        found |= line.text.find(U"はに　まる") != std::u32string::npos;
        CHECK(line.text.find(kWrapArgumentStart) == std::u32string::npos);
        CHECK(line.text.find(kWrapArgumentEnd) == std::u32string::npos);
    }
    CHECK(found);
    const auto wide = wrap_text(name, 20, true, true);
    REQUIRE(wide.too_wide.size() == 1);
    CHECK(wide.lines[0].text == U"はに　まる");
}

TEST_CASE("Japanese wrap: WSC glyph words use the same boundaries as Unicode kana") {
    const auto unicode = wrap_text(U"あいうえおは　かきくけこで　さしすせそ", 90, true, true);
    const auto native = wrap_text(with_wsc_glyphs(U"あいうえおは　かきくけこで　さしすせそ"), 90, true, true);
    REQUIRE(unicode.lines.size() == native.lines.size());
    for (size_t i = 0; i < unicode.lines.size(); ++i)
        CHECK(with_wsc_glyphs(unicode.lines[i].text) == native.lines[i].text);
}

TEST_CASE("Japanese wrap: a particle kana inside a word is not a preferred break") {
    // へ in へんかした is not the particle: the line breaks at the space before it.
    const auto r = wrap_text(U"アレクサンドラスは　レッドマシュマロに　へんかした！！", 200, true, true);
    REQUIRE(r.lines.size() == 2);
    CHECK(r.lines[0].text == U"アレクサンドラスは　レッドマシュマロに");
    CHECK(r.lines[1].text == U"へんかした！！");
}
