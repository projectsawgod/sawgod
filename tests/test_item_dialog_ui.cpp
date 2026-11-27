#include "chrome_fixture.h"
#include "game/item_dialog_ui.h"
#include "game/session.h"
#include "game/text_render.h"
#include <doctest/doctest.h>
using namespace sawgod::game;
namespace fx = chrome_fixture;
using sawgod::emu::Wsc;
using Dialog = ItemDialogUi::Dialog;
using Context = ItemDialogUi::Context;

namespace {
void load_names(Wsc &w) {
    std::string error;
    REQUIRE(Session::of(w).text().load("key,text\nitem_0,Potion\nitem_1,Hi-Potion\nitem_2,Elixir\n"
                                       "item_10,Philosopher's Stone\nitem_11,Door\nitem_18,King's Armor\n"
                                       "wait_unusable_item_message_1,No effect \n"
                                       "wait_equipment_transfer_error_1,Already wearing equipment \n",
                                       Charset::system(), &error, nullptr, nullptr, true));
}
} // namespace

TEST_CASE("item dialog scroll markers follow the visible rows above each footer in both pictures") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    fx::install(w);
    Machine m(w);
    for (bool second : {false, true}) {
        w.set_second(second);
        for (bool equipped : {false, true})
            for (int tab : {0, 1})
                for (int dialog : {2, 3, 4, 6, 7})
                    for (int selected : {0, 4, 7, 8}) {
                        ItemDialogUi ui;
                        ui.install(w);
                        w.ram[0xD852] = uint8_t(tab);
                        w.ram[0xD855] = 1;
                        w.ram[0xD842] = w.ram[0xD843] = uint8_t(selected);
                        w.ram[0xC818] = uint8_t(selected);
                        w.ram[0xC819] = w.ram[0xC81A] = w.ram[0xC81B] = 0;
                        hooks::emit(equipped ? hooks::Event::PartyMenu : hooks::Event::InventoryMenu, m,
                                    {equipped ? 6 : 1});
                        if (dialog < 5)
                            hooks::emit(hooks::Event::ItemDialog, m, {dialog, equipped ? 1 : 0});
                        else
                            hooks::emit(hooks::Event::ItemMessage, m, {dialog - 5});
                        const auto v = ui.view(w);
                        std::vector<int> markers;
                        Chrome::observe = [&](const auto &d) {
                            if (d.kind == Chrome::Draw::Kind::ScrollMarker) {
                                CHECK(d.x == (d.y == 20 && !v.tab ? 100 : 212));
                                CHECK_FALSE(d.pointer);
                                markers.push_back(d.y);
                            }
                        };
                        const auto ram = w.ram;
                        ui.picture(w, Wsc::ColorMode::Raw, second);
                        Chrome::observe = {};
                        std::vector<int> expected;
                        if (v.first > 0)
                            expected.push_back(20);
                        const int bottom = dialog >= 6 ? 60 : v.rows == 7 ? 112 : 64;
                        if (v.first + v.rows < v.total)
                            expected.push_back(bottom - 8);
                        CHECK(markers == expected);
                        CHECK(w.ram == ram);
                    }
    }
}

TEST_CASE("Crystals keeps native availability and choice while its compact pop-up preserves machine state") {
    // Distinct synthetic ROM icons exercise native slot order, including the reversed bottom directions.
    std::vector<uint8_t> rom(0x400000);
    for (int direction = 0; direction < 4; ++direction)
        for (int tile = 0; tile < 4; ++tile) {
            const int at = 0x393F46 + direction * 8 + tile * 2;
            const uint16_t entry = uint16_t((15 << 9) | (0x180 + direction));
            rom[size_t(at)] = uint8_t(entry);
            rom[size_t(at + 1)] = uint8_t(entry >> 8);
        }
    Wsc w(rom, 0);
    fx::install(w);
    for (int tile = 0; tile < 4; ++tile)
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                fx::put(w, 0x180 + tile, x, y, tile == 0 ? 12 : tile == 1 ? 6 : tile == 2 ? 2 : 3);
    for (int tile = 0; tile < 4; ++tile)
        fx::put(w, 0x180 + tile, 1, 1, 0); // a transparent crystal corner must reveal the window fill
    std::string error;
    REQUIRE(Session::of(w).text().load("key,text\nitem_7,Let There Be Light\n", Charset::system(), &error,
                                       nullptr, nullptr, true));
    w.ram[0xD842] = 8;
    w.ram[0xD08A] = 7;
    w.ram[0xD08B] = 50;
    ItemDialogUi ui;
    ui.install(w);
    Machine m(w);
    hooks::emit(hooks::Event::ItemDialog, m, {5, 0});
    CHECK(ui.active());
    CHECK(ui.view(w).context == Context::Inventory);
    CHECK(ui.view(w).rows == InventoryUi::kVisibleRows);
    CHECK(ui.view(w).list[6].name == U"Let There Be Light");
    CHECK(Chrome::text_width(ui.view(w).list[6].name) == 101);
    CHECK(ui.view(w).list[6].count == U"50");
    const auto framebuffer = w.framebuffer_rgb(Wsc::ColorMode::Raw);
    for (int count = 0; count <= 5; ++count) {
        w.ram[0xD0AB] = uint8_t((count << 4) | 7);
        for (int choice = 0; choice < 4; ++choice) {
            w.ram[0xD840] = uint8_t(choice);
            const auto ram = w.ram;
            const auto io = w.io;
            const auto registers = w.cpu.s.r;
            const auto view = ui.view(w);
            CHECK(view.crystal_choice == choice);
            CHECK(view.crystals == (count < 5 ? count : 0));
            const auto picture = ui.picture(w, Wsc::ColorMode::Raw);
            constexpr uint32_t colors[] = {fx::kAccent, fx::kEmpty, fx::kBorder, fx::kText};
            for (int slot = 0; slot < 4; ++slot) {
                const int x = ItemDialogUi::kCrystalX + ItemDialogUi::kCrystalIconX +
                              (slot % 2) * ItemDialogUi::kCrystalColumnPitch;
                const int y =
                    view.crystal_y + ItemDialogUi::kCrystalInsetY + (slot / 2) * ItemDialogUi::kCrystalPitch;
                CHECK(picture[size_t(y * 224 + x)] == (slot < view.crystals ? colors[slot] : fx::kFill));
                CHECK(picture[size_t((y + 1) * 224 + x + 1)] == fx::kFill);
            }
            CHECK(w.ram == ram);
            CHECK(w.io == io);
            CHECK(w.cpu.s.r == registers);
            CHECK(w.framebuffer_rgb(Wsc::ColorMode::Raw) == framebuffer);
        }
    }
    hooks::emit(hooks::Event::ItemDialog, m, {0});
    CHECK_FALSE(ui.active());
    hooks::emit(hooks::Event::ItemDialog, m, {5, 0});
    Session::of(w).new_generation();
    ui.frame(w);
    CHECK_FALSE(ui.active());
}

TEST_CASE("Crystals preserves the opened inventory scroll position and gives only its hand focus") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    fx::install(w);
    load_names(w);
    Machine m(w);
    InventoryUi inventory;
    inventory.install(w);
    ItemDialogUi ui;
    ui.install(w, &inventory);
    hooks::emit(hooks::Event::InventoryMenu, m, {1});
    w.ram[0xD842] = 9;
    CHECK(inventory.view(w).first == 2);
    w.ram[0xD842] = 8; // Up from Trash keeps the existing scroll, rather than recentering Crystals.
    const auto before = inventory.view(w);
    hooks::emit(hooks::Event::InventoryMenu, m, {0});
    hooks::emit(hooks::Event::ItemDialog, m, {5, 0});
    CHECK(ui.view(w).first == before.first);
    CHECK(ui.view(w).selected == before.selected);
    CHECK(ui.view(w).list[6].name == before.rows[6].name);
    for (bool gb : {false, true}) {
        if (gb)
            w.set_second(true);
        std::vector<Chrome::Draw> draws;
        Chrome::observe = [&](const auto &d) { draws.push_back(d); };
        ui.picture(w, Wsc::ColorMode::Raw, gb);
        Chrome::observe = {};
        int pointers = 0, rows = 0;
        for (const auto &d : draws) {
            if (d.kind == Chrome::Draw::Kind::Cursor) {
                ++pointers;
                CHECK(d.width == 16);
                CHECK(d.y == ui.view(w).crystal_y + ItemDialogUi::kCrystalInsetY);
            }
            if (d.kind == Chrome::Draw::Kind::ItemRun)
                ++rows;
        }
        CHECK(pointers == 1);
        CHECK(rows == InventoryUi::kVisibleRows);
    }
    hooks::emit(hooks::Event::ItemDialog, m, {0});
    hooks::emit(hooks::Event::InventoryMenu, m, {1});
    CHECK(inventory.view(w).first == before.first);
}

TEST_CASE("item dialogs follow native dialogs and messages, their list context, resets and teardown") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    Machine m(w);
    ItemDialogUi ui;
    ui.install(w);
    CHECK_FALSE(ui.active());
    hooks::emit(hooks::Event::ItemDialog, m, {1, 0});
    CHECK(ui.active());
    CHECK(ui.view(w).dialog == Dialog::Target);
    CHECK(ui.view(w).context == Context::Inventory);
    hooks::emit(hooks::Event::ItemMessage, m, {1}); // "no effect" over the target rows
    CHECK(ui.view(w).dialog == Dialog::Target);
    CHECK(ui.view(w).message == ItemDialogUi::Message::Unusable);
    hooks::emit(hooks::Event::ItemMessage, m, {0});
    hooks::emit(hooks::Event::ItemDialog, m, {0});
    CHECK_FALSE(ui.active()); // nothing is shown without a dialog or message
    CHECK(bool(ui.picture(w, Wsc::ColorMode::Raw) == w.framebuffer_rgb(Wsc::ColorMode::Raw)));
    hooks::emit(hooks::Event::ItemMessage, m, {1});
    CHECK(ui.active()); // the inventory remains the list a later message belongs to
    hooks::emit(hooks::Event::ItemMessage, m, {0});
    hooks::emit(hooks::Event::PartyMenu, m, {1}); // the field categories have no item list
    hooks::emit(hooks::Event::ItemMessage, m, {1});
    CHECK_FALSE(ui.active()); // unknown list: the native message shows
    hooks::emit(hooks::Event::PartyMenu, m, {6});
    CHECK(ui.active());
    CHECK(ui.view(w).context == Context::Equipped);
    hooks::emit(hooks::Event::ItemMessage, m, {0});
    hooks::emit(hooks::Event::InventoryMenu, m, {1});
    hooks::emit(hooks::Event::ItemMessage, m, {2});
    CHECK(ui.view(w).context == Context::Inventory);
    hooks::emit(hooks::Event::ItemDialog, m, {4, 2});
    CHECK(ui.view(w).context == Context::Field);
    Session::of(w).new_generation();
    ui.frame(w);
    CHECK_FALSE(ui.active());
    {
        ItemDialogUi temporary;
        temporary.install(w);
    }
    hooks::emit(hooks::Event::ItemDialog, m, {2, 0});
    CHECK(ui.active()); // another instance's teardown did not remove ours
}

TEST_CASE("item dialog lists read the used item, refill and overflow selections and the message's list") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    load_names(w);
    Machine m(w);
    ItemDialogUi ui;
    ui.install(w);
    for (int slot = 0; slot < 8; ++slot) {
        w.ram[0xD07C + slot * 2] = uint8_t(slot % 3);
        w.ram[0xD07D + slot * 2] = 3;
    }
    w.ram[0xD07C + 3 * 2] = 10;
    w.ram[0xD07C + 5 * 2] = 11;
    w.ram[0xD07D + 5 * 2] = 254;
    // Inventory Potion on a target: the item is the native source slot of the selected column.
    w.ram[0xD840] = 4;
    w.ram[0xD844] = 2;
    hooks::emit(hooks::Event::ItemDialog, m, {1, 0});
    auto v = ui.view(w);
    CHECK(v.item == U"Hi-Potion");
    CHECK(v.target == 2);
    // A Status item: the member's gear (0xD854 = 0) or the inventory (1).
    w.ram[0xD853] = 1;
    w.ram[0xD01F + 15 + 2 * 2] = 18;
    w.ram[0xD01F + 16 + 2 * 2] = 254;
    w.ram[0xD840] = 2;
    hooks::emit(hooks::Event::ItemDialog, m, {1, 1});
    CHECK(ui.view(w).item == U"King's Armor");
    w.ram[0xD854] = 1;
    CHECK(ui.view(w).item == U"Elixir");
    // Refill: seven of the eight ordinary slots, scrolling with the native selection; the source marked.
    w.ram[0xD840] = 3;
    w.ram[0xD842] = 3;
    hooks::emit(hooks::Event::ItemDialog, m, {2, 0});
    v = ui.view(w);
    CHECK(v.rows == 7);
    CHECK(v.total == 8);
    CHECK(v.source == 3);
    CHECK(v.first == 0);
    w.ram[0xD842] = 7;
    v = ui.view(w);
    CHECK(v.first == 1);
    CHECK(v.list[6].name == U"Hi-Potion");
    w.ram[0xD842] = 0; // native wrap
    CHECK(ui.view(w).first == 0);
    // Equipped refill: the native selector picks inventory slot 0xD843; the source is the gear item.
    w.ram[0xD854] = 0;
    w.ram[0xD843] = 5;
    hooks::emit(hooks::Event::ItemDialog, m, {2, 1});
    v = ui.view(w);
    CHECK(v.gear_tabs);
    CHECK(v.tab == 1);
    CHECK(v.selected == 5);
    CHECK(v.source == -1);
    // Overflow: the discard cursor's row plus the list's top (two words).
    w.ram[0xC818] = 2;
    w.ram[0xC81A] = 4;
    hooks::emit(hooks::Event::ItemDialog, m, {4, 2});
    v = ui.view(w);
    CHECK(v.selected == 6);
    CHECK(v.first == 0);
    hooks::emit(hooks::Event::ItemDialog, m, {0});
    // A message keeps three rows of its list with the selection in the middle, and the selected detail.
    hooks::emit(hooks::Event::InventoryMenu, m, {2});
    w.ram[0xD852] = 0;
    w.ram[0xD842] = 5;
    hooks::emit(hooks::Event::ItemMessage, m, {1});
    v = ui.view(w);
    CHECK(v.rows == 3);
    CHECK(v.first == 4);
    CHECK(v.detail.name == U"Door");
    w.ram[0xD842] = 9; // Trash: the last three rows
    CHECK(ui.view(w).first == 7);
    hooks::emit(hooks::Event::ItemMessage, m, {0});
    // Equip's "already wearing": the member's gear list, or the inventory with Key Items.
    hooks::emit(hooks::Event::PartyMenu, m, {6});
    w.ram[0xD852] = 0;
    w.ram[0xD842] = 2;
    hooks::emit(hooks::Event::ItemMessage, m, {2});
    v = ui.view(w);
    CHECK(v.gear_tabs);
    CHECK(v.tab == 0);
    CHECK(v.detail.name == U"King's Armor");
    w.ram[0xD852] = 1;
    w.ram[0xD855] = 1;
    w.ram[0xD843] = 8;
    v = ui.view(w);
    CHECK(v.key_items);
    CHECK(v.total == 9);
    CHECK(v.first == 6);
}

TEST_CASE("item dialog pictures cover cleanly, fit their text and preserve machine state in both pictures") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    fx::install(w);
    load_names(w);
    Machine m(w);
    ItemDialogUi ui;
    ui.install(w);
    for (int slot = 0; slot < 8; ++slot) {
        w.ram[0xD07C + slot * 2] = uint8_t(slot % 3);
        w.ram[0xD07D + slot * 2] = 3;
    }
    w.ram[0xD0DA] = 23;
    w.ram[0xD84B] = 16;
    const auto ram = w.ram;
    const auto io = w.io;
    const auto registers = w.cpu.s.r;
    const auto ordinary = w.framebuffer_rgb(Wsc::ColorMode::Raw);
    // "Already wearing equipment" over the inventory: the list keeps three whole rows, the message window
    // covers the list's lower frame and meets the detail line -- no row, frame or strip shows between.
    hooks::emit(hooks::Event::InventoryMenu, m, {2});
    hooks::emit(hooks::Event::ItemMessage, m, {2});
    auto px = ui.picture(w, Wsc::ColorMode::Raw);
    CHECK(px[60 * 224 + 100] == fx::kBorder); // the message window's top frame
    CHECK(px[64 * 224 + 100] == fx::kFill);   // its fill where the list's frame was
    CHECK(px[121 * 224 + 100] == fx::kFill);  // still the message's fill just above the detail line
    CHECK(px[DetailLine::kY * 224 + 100] == fx::kBorder); // the detail line's frame
    int stray = 0;                                        // row text between the list and the message text
    for (int y = 61; y < 70; ++y)
        for (int x = 17; x < 200; ++x)
            stray += px[size_t(y * 224 + x)] == fx::kText;
    CHECK(stray == 0);
    CHECK(Chrome::text_width(key_text(w, TextKey::wait_equipment_transfer_error_1)) <= 212);
    CHECK(key_text(w, TextKey::wait_equipment_transfer_error_1) == U"Already wearing equipment");
    hooks::emit(hooks::Event::ItemMessage, m, {0});
    for (int dialog = 1; dialog <= 4; ++dialog) {
        CAPTURE(dialog);
        hooks::emit(hooks::Event::ItemDialog, m, {dialog, dialog == 4 ? 2 : 0});
        for (bool second : {false, true}) {
            if (second && !w.second())
                w.set_second(true);
            const auto fb = w.framebuffer_rgb(Wsc::ColorMode::Raw, second);
            px = ui.picture(w, Wsc::ColorMode::Raw, second);
            CHECK(px.size() == 224 * 144);
            CHECK(bool(px != fb));
            CHECK(bool(ui.picture(w, Wsc::ColorMode::Raw, second) == px)); // paused redraw
            CHECK(bool(w.framebuffer_rgb(Wsc::ColorMode::Raw, second) == fb));
        }
        CHECK(bool(w.ram == ram));
        CHECK(bool(w.io == io));
        CHECK(w.cpu.s.r == registers);
    }
    CHECK(bool(w.framebuffer_rgb(Wsc::ColorMode::Raw) == ordinary));
    for (const auto &key : {"ui_item_target", "ui_item_refill_hint"})
        CHECK(Chrome::text_width(host_text(w, key)) <= 212);
    CHECK(Chrome::text_width(host_text(w, "ui_item_floor", U"99")) <= 68);
}

TEST_CASE("stat item notices show the native gain in both languages and clear on acknowledgement") {
    for (bool japanese : {false, true}) {
        Wsc w(std::vector<uint8_t>(0x400000), 0);
        fx::install(w, 2);
        w.set_second(true);
        Machine m(w);
        ItemDialogUi ui;
        ui.install(w);
        auto &text = Session::of(w).text();
        std::string error;
        REQUIRE(text.load(japanese
                              ? "key,text\nshow_strength_increase_message_1,こうげきが０あがりました\n"
                              : "key,text\nshow_strength_increase_message_1,STR increased by {amount}!\n",
                          Charset::system(), &error, nullptr, nullptr, true));
        hooks::emit(hooks::Event::ItemDialog, m, {1, 0});
        const auto ram = w.ram;
        for (int gain : {0, 1, 99}) {
            hooks::emit(hooks::Event::ItemMessage, m,
                        {3, int(TextKey::show_strength_increase_message_1), gain});
            const auto expected = japanese
                                      ? U"こうげきが" + utf8_to_u32(std::to_string(gain)) + U"あがりました"
                                      : U"STR increased by " + utf8_to_u32(std::to_string(gain)) + U"!";
            INFO("notice=" << name_utf8(ui.view(w).notice) << " expected=" << name_utf8(expected));
            CHECK(ui.view(w).notice == with_wsc_glyphs(expected));
            for (bool second : {false, true}) {
                bool shown = false;
                Chrome::observe = [&](const auto &d) {
                    shown |= d.kind == Chrome::Draw::Kind::Text && d.text == with_wsc_glyphs(expected) &&
                             d.x == 5 && d.y == 120;
                };
                ui.picture(w, Wsc::ColorMode::Raw, second);
                Chrome::observe = {};
                CHECK(shown);
            }
            CHECK(w.ram == ram);
            hooks::emit(hooks::Event::ItemMessage, m, {0});
            CHECK(ui.view(w).notice.empty());
            CHECK(ui.view(w).message == ItemDialogUi::Message::None);
        }
    }
}

#include "game/bank34.h"
#include "game/bank35_world.h"
#include "game/text_tokens.h"
TEST_CASE("only the Orb combine failure selects the required hint and all other refusals stay shared") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    fx::install(w);
    load_names(w);
    Machine m(w);
    m.ss() = m.ds() = 0;
    m.sp() = 0xC000;
    ItemDialogUi ui;
    ui.install(w);
    hooks::emit(hooks::Event::InventoryMenu, m, {1});
    struct ReachedWait {};
    int reason = -1;
    auto subscription =
        Session::of(w).hooks().subscribe(hooks::Event::ItemMessage, [&](Machine &, const hooks::Args &args) {
            if (args.a == 1) {
                reason = args.b;
                throw ReachedWait{};
            }
        });
    for (int item : {21, 22, 0, 17, 20, 23, 31}) {
        // Real failure and shared entry points, stopped at the presentation hook before the key wait.
        std::fill(w.ram.begin() + 0xD000, w.ram.begin() + 0xD0A0, 255);
        w.ram[0xD08C] = uint8_t(item);
        try {
            if (item == 21 || item == 22)
                combine_progression_items_core(m);
            else
                wait_unusable_item_message_core(m);
            FAIL("refusal did not reach its wait");
        } catch (const ReachedWait &) {
        }
        CHECK(reason == (item == 21 || item == 22 ? 1 : 0));
        CHECK(ui.view(w).notice == (reason ? marked_item_run(22, U"Nothing happened.") : U"No effect"));
    }
    // Built-in Japanese uses exactly the original words and supports the same literal token.
    Session::of(w).text().clear();
    CHECK(host_text(w, "ui_orb_pair_missing") ==
          with_wsc_glyphs(marked_item_run(22, U"なにも　おきなかった")));
}
