#include "chrome_fixture.h"
#include "game/creation_ui.h"
#include "game/player_names.h"
#include "game/session.h"
#include "game/ui_chrome.h"
#include <doctest/doctest.h>
using namespace sawgod::game;
using sawgod::emu::Wsc;

namespace {
constexpr uint16_t kA = 4, kB = 8, kStart = 2, kUp = 0x10, kRight = 0x20, kDown = 0x40, kLeft = 0x80,
                   kY2 = 0x200, kY4 = 0x800;
// Japanese sheets as a non-proportional language lays them (codes through the charset): hiragana with a
// blank key, katakana, and the letters page.
void load_sheets(Wsc &w) {
    std::string error;
    REQUIRE_MESSAGE(Session::of(w).text().load("key,text\n"
                                               "name_entry_sheet_0,あいうえお　かきくけ\n"
                                               "name_entry_sheet_1,アイウ\n"
                                               "name_entry_sheet_2,ＡＢＣ０１！？：／ー「」、。\n",
                                               Charset::system(), &error),
                    error);
}
} // namespace

TEST_CASE("creation presentation follows the type list and name entry, resumes after naming, resets") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    Machine m(w);
    CreationUi ui;
    ui.install(w);
    CHECK(Session::of(w).player_names().enabled);
    CHECK_FALSE(ui.active());
    hooks::emit(hooks::Event::CreationMenu, m, {1});
    CHECK(ui.scene() == CreationUi::Scene::Types);
    CHECK(hooks::filter(hooks::Value::CreationKeys, m, kRight) == 0);
    CHECK(ui.types(w).detail);
    // Name entry suspends the list; it comes back on the detail page it was left on.
    hooks::emit(hooks::Event::CreationMenu, m, {0});
    hooks::emit(hooks::Event::NameEntry, m, {1});
    CHECK(ui.scene() == CreationUi::Scene::Name);
    hooks::emit(hooks::Event::NameEntry, m, {0});
    hooks::emit(hooks::Event::CreationMenu, m, {1});
    CHECK(ui.scene() == CreationUi::Scene::Types);
    CHECK(ui.types(w).detail);
    hooks::emit(hooks::Event::CreationMenu, m, {0});
    CHECK_FALSE(ui.active());
    CHECK(bool(ui.picture(w, Wsc::ColorMode::Raw) == w.framebuffer_rgb(Wsc::ColorMode::Raw)));
    // A later creation (the Guild) starts on the list.
    hooks::emit(hooks::Event::CreationMenu, m, {1});
    CHECK_FALSE(ui.types(w).detail);
    Session::of(w).new_generation();
    ui.frame(w);
    CHECK_FALSE(ui.active());
    {
        CreationUi temporary;
        temporary.install(w);
    }
    hooks::emit(hooks::Event::NameEntry, m, {1});
    CHECK(ui.active()); // another subscription's teardown did not remove ours
}

TEST_CASE("creation keys open and close the detail page and leave every native key alone") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    Machine m(w);
    CreationUi ui;
    ui.install(w);
    CHECK(hooks::filter(hooks::Value::CreationKeys, m, kRight) == kRight); // hidden: original keys
    hooks::emit(hooks::Event::CreationMenu, m, {1});
    for (uint16_t key : {kA, kB, kUp, kDown, kLeft})
        CHECK(hooks::filter(hooks::Value::CreationKeys, m, key) == key);
    CHECK(hooks::filter(hooks::Value::CreationKeys, m, kRight | kDown) == (kRight | kDown));
    CHECK(hooks::filter(hooks::Value::CreationKeys, m, kRight) == 0);
    CHECK(hooks::filter(hooks::Value::CreationKeys, m, kDown) == 0); // the page's own cursor: no type change
    CHECK(hooks::filter(hooks::Value::CreationKeys, m, kUp) == 0);
    CHECK(hooks::filter(hooks::Value::CreationKeys, m, kA) == kA); // name entry opens from it
    CHECK(hooks::filter(hooks::Value::CreationKeys, m, kA | kDown | kLeft) == kA);
    CHECK(ui.types(w).detail);
    CHECK(hooks::filter(hooks::Value::CreationKeys, m, kB) == 0); // B closes it instead of leaving
    CHECK_FALSE(ui.types(w).detail);
    CHECK(hooks::filter(hooks::Value::CreationKeys, m, kRight) == 0);
    CHECK(hooks::filter(hooks::Value::CreationKeys, m, kLeft) == 0);
    CHECK_FALSE(ui.types(w).detail);
    // Rows of six and up page with Left/Right natively: no detail page there.
    w.ram[0xD853] = 6;
    CHECK(hooks::filter(hooks::Value::CreationKeys, m, kRight) == kRight);
    CHECK_FALSE(ui.types(w).detail);
    w.ram[0xD853] = 0;
    // Seven of eight rows show; the list scrolls to the selection.
    for (int selected = 0; selected < 8; ++selected) {
        w.ram[0xD841] = uint8_t(selected);
        const auto v = ui.types(w);
        CHECK(v.first <= selected);
        CHECK(selected < v.first + CreationUi::kVisibleTypes);
    }
    CHECK(ui.types(w).first == 1);
    w.ram[0xD841] = 0;
    CHECK(ui.types(w).first == 0);
}

TEST_CASE("the detail page's cursor walks the gear rows over the shared description line") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    chrome_fixture::install(w, 2);
    // Type 0's template (6000:[D1FB] -> E000): a human with three gear slots; item 66 strikes with STR.
    const auto rom = [&](uint32_t linear) -> uint8_t & {
        const auto at = w.rom_offset_of(linear);
        REQUIRE(at >= 0);
        return w.rom[size_t(at)];
    };
    rom(0x6D1FB + 1) = 0xE0;
    rom(0x6E000 + 2) = 3;
    rom(0x6E000 + 11) = 66;
    rom(0x6E000 + 12) = 1;
    rom(0x6E000 + 13) = 2;
    rom(0x943E0 + 66 * 8 + 2) = 6;
    rom(0x943E0 + 66 * 8 + 3) = 30;
    rom(0x943E0 + 66 * 8 + 4) = 12;
    Machine m(w);
    std::string error;
    REQUIRE(Session::of(w).text().load("key,text\nitem_66,Chainsaw\nitem_1,Potion\n"
                                       "itemdesc_66,This power saw can cut through anything.\n"
                                       "itemdesc_1,Restores HP.\n",
                                       Charset::system(), &error, nullptr, nullptr, true));
    CreationUi ui;
    ui.install(w);
    w.ram[0xD841] = 2;
    hooks::emit(hooks::Event::CreationMenu, m, {1});
    hooks::filter(hooks::Value::CreationKeys, m, kRight);
    auto v = ui.types(w);
    REQUIRE(v.detail);
    REQUIRE(v.types[2].gear_count == 3);
    CHECK(v.gear == 0);
    CHECK(v.hovered.name == U"Chainsaw");
    CHECK(v.hovered.count == U"30");
    CHECK(v.hovered.flavor == U"This power saw can cut through anything.");
    CHECK(v.hovered.stats ==
          host_text(w, "ui_inventory_power", U"12") + U"   " + host_text(w, "ui_inventory_str"));
    CHECK(Chrome::text_width(v.hovered.flavor) > DetailLine::kTextWidth); // it scrolls
    // Up/Down never reach the native loop: its selection bytes (and so its sounds) stay as they were.
    const auto ram = w.ram;
    const auto press = [&](uint16_t key) { return hooks::filter(hooks::Value::CreationKeys, m, key); };
    CHECK(press(kDown) == 0);
    CHECK(ui.types(w).gear == 1);
    CHECK(ui.types(w).hovered.flavor == U"Restores HP.");
    CHECK(press(kDown) == 0);
    CHECK(press(kDown) == 0); // around the last row
    CHECK(ui.types(w).gear == 0);
    CHECK(press(kUp) == 0); // and the first
    CHECK(ui.types(w).gear == 2);
    CHECK(press(kUp | kLeft) == 0); // Up before Left, as the native order has it
    CHECK(ui.types(w).detail);
    CHECK(ui.types(w).gear == 1);
    CHECK(ui.types(w).selected == 2);
    CHECK(bool(w.ram == ram));
    // The cursor and the description window, in both pictures.
    for (bool second : {false, true}) {
        bool cursor = false, line = false, text = false;
        Chrome::observe = [&](const Chrome::Draw &d) {
            cursor |= d.kind == Chrome::Draw::Kind::Cursor && d.x == 66 && d.y == 26 + 12 + !second;
            line |= d.kind == Chrome::Draw::Kind::Window && d.x == 0 && d.y == DetailLine::kY &&
                    d.width == 224 && d.height == DetailLine::kHeight;
            text |= d.kind == Chrome::Draw::Kind::Text && d.y == DetailLine::kY + 4 &&
                    d.text.starts_with(U"Restores HP.");
            if (d.kind == Chrome::Draw::Kind::Window && d.y == 16)
                CHECK(d.height == DetailLine::kY - 16);
        };
        ui.picture(w, Wsc::ColorMode::Raw, second);
        Chrome::observe = nullptr;
        CHECK(cursor);
        CHECK(line);
        CHECK(text);
    }
    // A keeps the page and its row for name entry's return; B and Left close it; Right opens on row 0.
    CHECK(press(kA) == kA);
    hooks::emit(hooks::Event::CreationMenu, m, {0});
    hooks::emit(hooks::Event::NameEntry, m, {1});
    hooks::emit(hooks::Event::NameEntry, m, {0});
    hooks::emit(hooks::Event::CreationMenu, m, {1});
    CHECK(ui.types(w).detail);
    CHECK(ui.types(w).gear == 1);
    CHECK(press(kB) == 0);
    CHECK_FALSE(ui.types(w).detail);
    CHECK(press(kDown) == kDown); // the list: the native loop changes the type
    CHECK(press(kRight) == 0);
    CHECK(ui.types(w).gear == 0);
    CHECK(press(kDown) == 0);
    CHECK(press(kLeft) == 0);
    CHECK_FALSE(ui.types(w).detail);
    // A type with no gear has no row to stand on.
    rom(0x6E000 + 2) = 0;
    CHECK(press(kRight) == 0);
    CHECK(press(kDown) == 0);
    CHECK(ui.types(w).gear == 0);
    CHECK(ui.types(w).hovered.name.empty());
    CHECK(ui.picture(w, Wsc::ColorMode::Raw).size() == 224 * 144);
}

TEST_CASE("name keys hold the original glyph codes and host-only glyphs project to the nearest one") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    Machine m(w);
    load_sheets(w);
    CreationUi ui;
    ui.install(w);
    hooks::emit(hooks::Event::NameEntry, m, {1});
    const auto &pages = ui.pages(w);
    REQUIRE(pages.size() == 3); // hiragana, katakana, Latin: Japanese keeps the kana
    CHECK(pages[0].kana == 1);
    CHECK(pages[0].rows[0].size() == 9);     // the blank key is Space's, not a grid key
    CHECK(pages[0].rows[0][5].code == 0x05); // か after the blank
    CHECK(pages[1].kana == 2);
    const auto &latin = pages[2];
    CHECK(latin.kana == 0);
    REQUIRE(latin.rows.size() == 7);
    const auto key = [&](char32_t g) {
        for (const auto &row : latin.rows)
            for (const auto &k : row)
                if (k.shown == g)
                    return k;
        FAIL("no key");
        return CreationUi::Key{};
    };
    CHECK(key(U'A').code == 0xA1);
    CHECK(key(U'A').text == 0);
    CHECK(key(U'0').code == 0xBB);
    CHECK(key(U'!').code == 0xCB);
    CHECK(key(U'/').code == 0xD2);
    CHECK(key(U'a').code == 0xA1);
    CHECK(key(U'a').text == U'a');
    CHECK(key(U'-').code == 0xD3);
    CHECK(key(U'(').code == 0xC6);
    CHECK(key(U'.').code == 0xC9);
    CHECK(key(U'Z').code == 0xD9); // not on these sheets: the blank
    CHECK(key(U'Z').text == U'Z');

    // A adds the key under the host cursor; the native append takes it once.
    CHECK(hooks::filter(hooks::Value::NameEntryKeys, m, kA) == kA);
    CHECK(hooks::filter(hooks::Value::NameEntryGlyph, m, 0x77) == 0x00); // あ
    CHECK(hooks::filter(hooks::Value::NameEntryGlyph, m, 0x77) == 0x77);
    CHECK(hooks::filter(hooks::Value::NameEntryKeys, m, kRight) == kRight);
    CHECK(ui.name(w).column == 1);
    CHECK(hooks::filter(hooks::Value::NameEntryKeys, m, kLeft | kLeft) == kLeft);
    CHECK(hooks::filter(hooks::Value::NameEntryKeys, m, kLeft) == kLeft); // wraps in its row
    CHECK(ui.name(w).column == 8);
    // Pages: Y2 forward, Y4 back, around.
    CHECK(hooks::filter(hooks::Value::NameEntryKeys, m, kY4) == kY4);
    CHECK(ui.name(w).page == 2);
    hooks::filter(hooks::Value::NameEntryKeys, m, kDown);
    hooks::filter(hooks::Value::NameEntryKeys, m, kA);
    CHECK(hooks::filter(hooks::Value::NameEntryGlyph, m, 0) == int32_t(0xD9 | U'V' << 8)); // row 1, column 8
    // Down from the last row reaches the buttons (the one under the column), Left/Right go round them.
    for (int i = 0; i < 6; ++i)
        hooks::filter(hooks::Value::NameEntryKeys, m, kDown);
    REQUIRE(ui.name(w).buttons);
    CHECK(ui.name(w).button == 2);
    hooks::filter(hooks::Value::NameEntryKeys, m, kLeft);
    hooks::filter(hooks::Value::NameEntryKeys, m, kLeft);
    CHECK(ui.name(w).button == 0);
    CHECK(hooks::filter(hooks::Value::NameEntryKeys, m, kA) == kA); // Space: the blank key's code
    CHECK(hooks::filter(hooks::Value::NameEntryGlyph, m, 0) == 0xD9);
    hooks::filter(hooks::Value::NameEntryKeys, m, kRight);
    CHECK(ui.name(w).button == 1);
    w.ram[0xD855] = 0;
    CHECK(hooks::filter(hooks::Value::NameEntryKeys, m, kA) == 0); // Delete never leaves the editor
    w.ram[0xD855] = 2;
    CHECK(hooks::filter(hooks::Value::NameEntryKeys, m, kA) == kB);
    hooks::filter(hooks::Value::NameEntryKeys, m, kRight);
    CHECK(hooks::filter(hooks::Value::NameEntryKeys, m, kA) == kStart);
    CHECK(hooks::filter(hooks::Value::NameEntryKeys, m, kStart | kA) == (kStart | kA));
    CHECK(hooks::filter(hooks::Value::NameEntryKeys, m, kB) == kB);
    CHECK(hooks::filter(hooks::Value::NameEntryKeys, m, kDown) == kDown);
    CHECK(ui.name(w).row == 0);
    CHECK_FALSE(ui.name(w).buttons);
}

TEST_CASE("English name entry offers the Latin page only") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    Machine m(w);
    std::string error;
    REQUIRE(Session::of(w).text().load("key,text\nui_name_entry_done,Done\n", Charset::system(), &error,
                                       nullptr, nullptr, true));
    CreationUi ui;
    ui.install(w);
    hooks::emit(hooks::Event::NameEntry, m, {1});
    REQUIRE(ui.pages(w).size() == 1);
    CHECK(ui.pages(w)[0].kana == 0);
    CHECK(hooks::filter(hooks::Value::NameEntryKeys, m, kY2) == kY2);
    CHECK(ui.name(w).page == 0);
    // Space keeps the machine's blank and a plain space in the host name.
    for (size_t i = 0; i < ui.pages(w)[0].rows.size(); ++i)
        hooks::filter(hooks::Value::NameEntryKeys, m, kDown);
    REQUIRE(ui.name(w).buttons);
    hooks::filter(hooks::Value::NameEntryKeys, m, kA);
    CHECK(hooks::filter(hooks::Value::NameEntryGlyph, m, 0) == int32_t(0xD9 | U' ' << 8));
}

TEST_CASE("host-only glyphs reach the accepted name and the draft") {
    PlayerNames names;
    names.draft = {{0xA1, 0}, {0xA1, U'm'}, {0xD3, U'-'}, {0x00, 0}, {0xBB, 0}};
    CHECK(names.draft_text() == std::u32string(U"Ａm-") + char32_t(0xE000) + U"０");
    names.accept(0);
    CHECK(names.values[0] == "Am-あ0");
}

TEST_CASE("creation and name pictures preserve RAM IO registers in both pictures") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    chrome_fixture::install(w, 2);
    w.set_second(true);
    w.shadow_write16(0xFE00 + 15 * 32 + 2 * 2, 0x000);
    w.shadow_write16(0xFE00 + 15 * 32 + 10 * 2, 0xFFF);
    load_sheets(w);
    Machine m(w);
    CreationUi ui;
    ui.install(w);
    Session::of(w).player_names().draft = {{0xA1, 0}, {0xA1, U'm'}};
    const auto check = [&] {
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
    };
    hooks::emit(hooks::Event::CreationMenu, m, {1});
    check();
    hooks::filter(hooks::Value::CreationKeys, m, kRight);
    check();
    hooks::emit(hooks::Event::CreationMenu, m, {0});
    hooks::emit(hooks::Event::NameEntry, m, {1});
    for (int page = 0; page < 3; ++page) {
        check();
        hooks::filter(hooks::Value::NameEntryKeys, m, kY2);
    }
    for (const auto &key : {"ui_name_entry_space", "ui_name_entry_delete"})
        CHECK(Chrome::text_width(host_text(w, key)) <= 56);
}

TEST_CASE("host tiles show their own colors, and GB greys for upper palettes in the GB picture") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    chrome_fixture::install(w, 2);
    w.set_second(true);
    w.upper_palette_greys = true;
    std::array<uint8_t, 32> tile{};
    tile[0] = 0x10; // pixel (0,0): index 1; (1,0): index 0, skipped
    std::array<uint16_t, 16> colors{};
    colors[1] = 0x0F0;
    for (bool second : {false, true}) {
        auto px = w.framebuffer_rgb(Wsc::ColorMode::Raw, second);
        const auto under = px[1];
        Chrome c(w, px, Wsc::ColorMode::Raw, second);
        REQUIRE(c.ready());
        c.tile(tile.data(), colors, 8, 0, 0);
        CHECK(px[0] == (second ? 0xAAAAAAu : 0x00FF00u));
        CHECK(px[1] == under);
    }
}
