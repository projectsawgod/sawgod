#include "chrome_fixture.h"
#include "game/dialogue_ui.h"
#include "game/session.h"
#include <algorithm>
#include <doctest/doctest.h>
using namespace sawgod::game;
using sawgod::emu::Wsc;
namespace {
void token(Machine &m, char32_t c) { hooks::emit(hooks::Event::Dialogue, m, {2, 0, int32_t(c)}); }
} // namespace
TEST_CASE("dialogue observes speakers, narration, page clear, and acknowledgement without changing state") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    chrome_fixture::install(w, 2);
    w.set_second(true);
    Machine m(w);
    DialogueUi ui;
    ui.install(w);
    w.io[0xB2] |= 0x10; // the native window's line split is on
    hooks::emit(hooks::Event::Dialogue, m, {1});
    for (auto c : U"King「Hello")
        if (c)
            token(m, c);
    CHECK(ui.speaker() == U"King");
    REQUIRE(ui.lines().size() == 1);
    CHECK(ui.lines()[0] == U"Hello");
    hooks::emit(hooks::Event::Dialogue, m, {3, 1});
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
        CHECK(bool(ui.picture(w, Wsc::ColorMode::Raw, second) == px));
    }
    hooks::emit(hooks::Event::Dialogue, m, {2, 14});
    CHECK(ui.speaker().empty());
    CHECK(ui.lines()[0].empty());
    for (auto c : U"Narration")
        if (c)
            token(m, c);
    CHECK(ui.speaker().empty());
    hooks::emit(hooks::Event::Dialogue, m, {0});
    CHECK_FALSE(ui.active());
    Session::of(w).apply_scene_settings(w);
    CHECK(w.scene_hidden_palettes == 0);
    Session::of(w).apply_scene_settings(w);
    CHECK(w.scene_hidden_sprite_palettes == 0);
}
TEST_CASE("dialogue supports four Japanese lines, three translated lines, extended party names and reset") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    Machine m(w);
    DialogueUi ui;
    ui.install(w);
    w.io[0xB2] |= 0x10; // the native window's line split is on
    auto &s = Session::of(w);
    s.player_names().enabled = true;
    s.player_names().values[2] = "Beatrice";
    hooks::emit(hooks::Event::Dialogue, m, {1});
    for (int i = 0; i < 4; ++i) {
        hooks::emit(hooks::Event::Dialogue, m, {4, 2, i});
        token(m, U'X');
    }
    token(m, U'「');
    CHECK(ui.speaker() == U"Beatrice");
    token(m, U'あ');
    for (int i = 0; i < 3; ++i) {
        hooks::emit(hooks::Event::Dialogue, m, {2, 13});
        token(m, U'あ');
    }
    CHECK(ui.lines().size() == 4);
    std::string error;
    REQUIRE(s.text().load("key,text\nmonster_0,Human\n", Charset::system(), &error, nullptr, nullptr, true));
    hooks::emit(hooks::Event::Dialogue, m, {1});
    for (int i = 0; i < 4; ++i) {
        token(m, U'A');
        hooks::emit(hooks::Event::Dialogue, m, {2, 13});
    }
    CHECK(ui.lines().size() == 3);
    s.new_generation();
    ui.frame(w);
    CHECK_FALSE(ui.active());
    CHECK(ui.speaker().empty());
}

#include "game/ui_items.h"
TEST_CASE("chest item argument expands its icon inline, including reordered and repeated translations") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    chrome_fixture::install(w);
    Machine m(w);
    DialogueUi ui;
    ui.install(w);
    w.io[0xB2] |= 0x10; // the native window's line split is on
    std::string error;
    REQUIRE(Session::of(w).text().load(
        "key,text\nitem_32,Sword\nui_dialogue_item_obtained,{item} acquired: {item}.\n", Charset::system(),
        &error, nullptr, nullptr, true));
    hooks::emit(hooks::Event::Dialogue, m, {1});
    hooks::emit(hooks::Event::Dialogue, m, {5, 0, 32});
    const auto argument = Chrome::item_run(32, U"Sword");
    CHECK(ui.lines()[0] == argument + U" acquired: " + argument + U".");
    std::vector<Chrome::Draw> draws;
    Chrome::observe = [&](const auto &d) { draws.push_back(d); };
    ui.picture(w, Wsc::ColorMode::Raw, false);
    Chrome::observe = {};
    CHECK(std::count_if(draws.begin(), draws.end(), [](const auto &d) {
              return d.kind == Chrome::Draw::Kind::ItemRun && d.item == 32;
          }) == 2);
    hooks::emit(hooks::Event::Dialogue, m, {5, 1, 32});
    CHECK(ui.lines()[0] == host_text(w, "ui_dialogue_inventory_full")); // no item name in this notice
}

TEST_CASE("dialogue shares breakable-space normalization and does not wrap trailing spaces") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    Machine m(w);
    DialogueUi ui;
    ui.install(w);
    w.io[0xB2] |= 0x10; // the native window's line split is on
    std::string error;
    REQUIRE(Session::of(w).text().load("key,text\nmonster_0,Human\n", Charset::system(), &error, nullptr,
                                       nullptr, true));
    hooks::emit(hooks::Event::Dialogue, m, {1});
    for (auto c : std::u32string(U"one  two "))
        token(m, c);
    REQUIRE(ui.lines().size() == 1);
    CHECK(ui.lines()[0] == U"one two ");
}

TEST_CASE("dialogue body stays in place and keeps its gradient when a delayed speaker tab appears") {
    for (bool english : {false, true}) {
        Wsc w(std::vector<uint8_t>(0x400000), 0);
        chrome_fixture::install(w);
        w.set_second(true);
        if (english) {
            std::string error;
            REQUIRE(Session::of(w).text().load("key,text\nmonster_0,Human\n", Charset::system(), &error,
                                               nullptr, nullptr, true));
        }
        Machine m(w);
        DialogueUi ui;
        ui.install(w);
        w.io[0xB2] |= 0x10; // the native window's line split is on
        hooks::emit(hooks::Event::Dialogue, m, {1});
        const int y = english ? 92 : 80;
        Wsc::UiFill fill;
        w.ui_fill = &fill;
        const auto opening = ui.picture(w, Wsc::ColorMode::Raw);
        REQUIRE_FALSE(fill.windows.empty());
        CHECK(fill.windows[0].top == y);
        // The native script has not exposed its first glyph during these opening frames.
        for (int frame = 0; frame < 8; ++frame) {
            ++w.frame_count;
            fill.clear();
            CHECK(ui.picture(w, Wsc::ColorMode::Raw) == opening);
        }
        for (auto code : std::u32string(U"WWWWWWWW「"))
            token(m, code);
        REQUIRE_FALSE(ui.speaker().empty());
        fill.clear();
        const auto tabbed = ui.picture(w, Wsc::ColorMode::Raw);
        REQUIRE_FALSE(fill.windows.empty());
        for (const auto &window : fill.windows) {
            CHECK(window.top == y);
            CHECK(window.bottom == 144);
        }
        // The right edge and the body fill retain their original pixels; only the joined tab seam changes.
        for (int row = y; row < 144; ++row)
            CHECK(tabbed[size_t(row * 224 + 220)] == opening[size_t(row * 224 + 220)]);
        w.ui_fill = nullptr;
    }
}

TEST_CASE("dialogue predicts every speaker from the live stream before typing, including Japanese mods") {
    // Raw Japanese, a Japanese mod, English, and Japanese fallback under English share this contract.
    for (int language = 0; language < 4; ++language) {
        const bool english = language == 2;
        Wsc w(std::vector<uint8_t>(0x400000), 0);
        chrome_fixture::install(w);
        w.set_second(true);
        Machine m(w);
        auto &s = Session::of(w);
        std::vector<uint8_t> bytes;
        REQUIRE(Charset::system().encode("おう「はい", bytes, nullptr));
        for (size_t i = 0; i < bytes.size(); ++i)
            w.rom[0x354B2A + i] = uint8_t(bytes[i] + 0x20);
        w.rom[0x354B2A + bytes.size()] = 0; // stop before unrelated script logic
        if (language) {
            const auto replacement =
                english ? "Dragon King: \"Yes\"{wait}{clear}{name:0}: \"No\"{wait}{clear}Dragon King: \"Yes\""
                        : "りゅうおう「はい{wait}{clear}{name:0}「いいえ{wait}{clear}りゅうおう「はい";
            std::string error;
            REQUIRE(s.text().load("key,text\ndlg_354B2A," + csv_cell(replacement) + "\n", Charset::system(),
                                  &error, nullptr, nullptr, english));
            if (language == 3)
                REQUIRE(s.text().load("key,text\nmonster_0,Human\n", Charset::system(), &error, nullptr,
                                      nullptr, true, true));
        }
        w.ram[0xD001] = 0xCD; // a native name glyph with no unambiguous Unicode mapping
        w.ram[0xD002] = 0xFF;
        CHECK(s.dialogue_name(m, 0) == std::u32string(1, 0xE0CD));
        CHECK(s.dialogue_character(0xCD) == 0xE0CD);
        std::fill_n(w.ram.begin() + 0xD002, 3, 0xD9);
        CHECK(s.dialogue_name(m, 0) == std::u32string(1, 0xE0CD));
        DialogueUi ui;
        ui.install(w);
        w.io[0xB2] |= 0x10;
        hooks::emit(hooks::Event::Dialogue, m, {1});
        m.wr16(0, 0x100, 0x4B2A);
        m.wr16(0, 0x102, 0x5000);
        s.dialogue_frame(m, 0x100);
        const auto start = m.rd16(0, 0x100);
        auto check_plate = [&](const std::u32string &expected) {
            const auto ram = w.ram;
            const auto io = w.io;
            const auto registers = w.cpu.s.r;
            for (bool second : {false, true}) {
                ui.picture(w, Wsc::ColorMode::Raw, second);
                CHECK(ui.speaker() == expected);
                CHECK(ui.lines()[0].empty());
                CHECK(w.ram == ram);
                CHECK(w.io == io);
                CHECK(w.cpu.s.r == registers);
            }
        };
        check_plate(language == 0 ? U"おう" : english ? U"Dragon King" : U"りゅうおう");
        if (!language)
            continue;
        int clears = 0;
        for (uint16_t pos = start; !s.text().dialogue_end(pos); ++pos) {
            if (s.text().byte(pos) != 0x0E)
                continue;
            m.wr16(0, 0x100, pos);
            s.dialogue_frame(m, 0x100);
            hooks::emit(hooks::Event::Dialogue, m, {2, 0x0E});
            if (s.text().byte(uint16_t(pos + 1)) == 0x19) {
                // The parser has already consumed the member argument when it pauses to type.
                m.wr8(0, 0x104, 1);
                m.wr16(0, 0x100, uint16_t(pos + 2));
                s.dialogue_frame(m, 0x100);
                m.wr8(0, 0x104, 0);
            }
            check_plate(++clears == 1 ? std::u32string(1, 0xE0CD) : english ? U"Dragon King" : U"りゅうおう");
        }
        CHECK(clears == 2);
    }
}

#include "game/text_tokens.h"
TEST_CASE("seal ink crosses native runs, follows wrapped lines and resets with its message or clear") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    chrome_fixture::install(w);
    Machine m(w);
    auto &session = Session::of(w);
    std::string error;
    REQUIRE(session.text().load("key,text\ndlg_357419,A {seal:black}black \n"
                                "dlg_357454,{box}seal{/seal} protects the door.\n",
                                Charset::system(), &error, nullptr, nullptr, true));
    DialogueUi ui;
    ui.install(w);
    w.io[0xB2] |= 0x10;
    hooks::emit(hooks::Event::Dialogue, m, {1});
    auto type_run = [&](uint32_t address) {
        const auto start = uint16_t(session.text().dialogue(address));
        for (uint16_t pos = start; !session.text().dialogue_end(pos); ++pos) {
            const auto byte = session.text().byte(pos);
            // interpret_field_dialogue consumes a run's initial 0x0C without drawing a token.
            if (pos == start && byte == 0x0C)
                continue;
            m.wr16(0, 0x100, pos);
            m.wr16(0, 0x102, kTextSegment);
            session.dialogue_frame(m, 0x100);
            token(m, session.dialogue_character(uint8_t(byte - 0x20)));
        }
    };
    type_run(0x357419);
    type_run(0x357454);
    REQUIRE(ui.lines().size() == 1);
    CHECK(ui.lines()[0] == expand_seals(U"A {seal:black}black seal{/seal} protects the door."));
    auto px = ui.picture(w, Wsc::ColorMode::Raw);
    CHECK(std::find(px.begin(), px.end(), 0x9696A8) != px.end());
    for (const int reset : {0, 0x0E, 0x0C}) {
        token(m, kInlineSeal);
        if (reset == 0) {
            hooks::emit(hooks::Event::Dialogue, m, {0});
            hooks::emit(hooks::Event::Dialogue, m, {1});
        } else
            hooks::emit(hooks::Event::Dialogue, m, {2, reset});
        for (auto c : std::u32string(U"The seal's been broken!"))
            token(m, c);
        hooks::emit(hooks::Event::Dialogue, m, {2, 13});
        token(m, U'H');
        CHECK(ui.lines().back() == U"H");
        px = ui.picture(w, Wsc::ColorMode::Raw);
        CHECK(std::find(px.begin(), px.end(), 0x9696A8) == px.end());
    }
    hooks::emit(hooks::Event::Dialogue, m, {1});
    token(m, kInlineSeal + 3);
    token(m, U'H');
    hooks::emit(hooks::Event::Dialogue, m, {2, 13});
    token(m, U'H');
    CHECK(ui.lines().back() == std::u32string(1, kSealInk + 3) + U"H");
}
