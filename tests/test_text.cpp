// Text resources (game/text.h): CSV parsing, the charset encoder and the host text table.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdio>

#include "emu/wsc.h"
#include "engine/button_icons.h"
#include "game/bank34.h"
#include "game/bank36_misc.h"
#include "game/machine.h"
#include "game/session.h"
#include "game/text.h"
#include "game/text_render.h"
#include "game/text_tokens.h"

using namespace sawgod::game;

TEST_CASE("parse_csv: quoting, empty fields, CRLF") {
    const auto rows = parse_csv("key,text\r\na,\"x, \"\"y\"\"\"\nb,\n");
    REQUIRE(rows.size() == 3);
    CHECK(rows[1][0] == "a");
    CHECK(rows[1][1] == "x, \"y\"");
    REQUIRE(rows[2].size() == 2);
    CHECK(rows[2][1].empty());
}

TEST_CASE("Charset: system codes, ASCII to full width, raw bytes, refusals") {
    const Charset cs = Charset::system();
    std::vector<uint8_t> out;
    std::string error;
    REQUIRE(cs.encode("あア", out, &error));
    CHECK(out == std::vector<uint8_t>{0x00, 0x50});
    out.clear();
    REQUIRE(cs.encode("Ab 1[D1]", out, &error)); // a -> Ａ (no lower case in the font), space -> 　
    CHECK(out == std::vector<uint8_t>{0xA1, 0xA2, 0xD9, 0xBC, 0xD1});
    out.clear();
    CHECK_FALSE(cs.encode("é", out, &error));
    CHECK(error.find("é") != std::string::npos);
}

TEST_CASE("TextTable: a key that only begins like a name is not one") {
    TextTable t;
    std::string error;
    REQUIRE(t.load("key,text\nitem_0,Potion\nitem_obtained_suffix,{item} obtained.\n", Charset::system(),
                   &error, nullptr, nullptr, /*proportional=*/true));
    const int run = t.name_run(0, 0);
    REQUIRE(run >= 0);
    REQUIRE(t.run(uint16_t(run)));
    CHECK(*t.run(uint16_t(run)) == U"Potion");
}
TEST_CASE("TextTable: overrides, glyph lists as words, sharing, errors") {
    TextTable t;
    CHECK(t.empty());
    CHECK(t.find(TextKey(0)) == -1);
    const std::string first = kTextKeys[0].name;
    int glyph = -1;
    for (int i = 0; i < kTextKeyCount && glyph < 0; ++i)
        if (kTextKeys[i].glyphs)
            glyph = i;
    REQUIRE(glyph >= 0);
    const std::string csv = "key,text,notes\n" + first + ",AB,x\n" + kTextKeys[glyph].name + ",A\n";
    int changed = 0;
    std::string error;
    REQUIRE(t.load(csv, Charset::system(), &error, nullptr, &changed));
    CHECK(changed == 2);
    const int a = t.find(TextKey(0)), g = t.find(TextKey(glyph));
    REQUIRE(a >= 0);
    CHECK(t.byte(uint16_t(a)) == 0xA1);
    CHECK(t.byte(uint16_t(a + 1)) == 0xA2);
    CHECK(t.byte(uint16_t(a + 2)) == 0xFF);
    REQUIRE(g >= 0);
    CHECK(g % 2 == 0);                               // word-aligned
    CHECK(t.byte(uint16_t(g)) == uint8_t(0xA1 * 2)); // code 0xA1 as the word 0x0142
    CHECK(t.byte(uint16_t(g + 1)) == 0x01);
    CHECK(t.byte(uint16_t(g + 2)) == 0xFF);
    CHECK(t.byte(uint16_t(g + 3)) == 0xFF);

    // bad rows are skipped with a warning; a file without key/text columns fails and changes nothing
    CHECK_FALSE(t.load("name,value\n", Charset::system(), &error));
    CHECK(t.find(TextKey(0)) == a);
    std::vector<std::string> warnings;
    REQUIRE(
        t.load("key,text\nno_such_key,A\n" + first + ",é\n", Charset::system(), &error, &warnings, &changed));
    CHECK(changed == 0);
    REQUIRE(warnings.size() == 2);
    CHECK(warnings[0].find("no_such_key") != std::string::npos);
    CHECK(t.find(TextKey(0)) == -1);
    t.clear();
    CHECK(t.find(TextKey(0)) == -1);
}

TEST_CASE("TextTable: dialogue runs, names and description records") {
    TextTable t;
    std::string error;
    std::vector<std::string> warnings;
    char key[32];
    std::snprintf(key, sizeof key, "dlg_%06X", unsigned(kDialogueRuns[0].start));
    const std::string csv = std::string("key,text\n") + key +
                            ",あ{n}{name:2}{wait}\nitem_3,ABC\nmonster_7,ア\n" +
                            "itemdesc_5,X\nitem_999,A\ndlg_000001,A\n";
    int changed = 0;
    REQUIRE(t.load(csv, Charset::system(), &error, &warnings, &changed));
    CHECK(changed == 4);
    CHECK(warnings.size() == 2); // item_999, an unknown dialogue run

    // the run: script bytes (glyph = code + 0x20), then the end marker and the ROM offset to resume at
    const int d = t.dialogue(kDialogueRuns[0].start);
    REQUIRE(d >= 0);
    const uint8_t want[] = {0x20,
                            0x0D,
                            0x19,
                            2,
                            0x13,
                            TextTable::kDialogueEnd,
                            uint8_t(kDialogueRuns[0].end - 0x350000),
                            uint8_t((kDialogueRuns[0].end - 0x350000) >> 8)};
    for (size_t i = 0; i < sizeof want; ++i)
        CHECK(t.byte(uint16_t(d + int(i))) == want[i]);

    // a name: its codes, padded with spaces to the record's length, 0xFF last
    const int n = t.name(0, 3);
    REQUIRE(n >= 0);
    CHECK(t.byte(uint16_t(n)) == 0xA1);
    CHECK(t.byte(uint16_t(n + 3)) == 0xD9);
    CHECK(t.byte(uint16_t(n + 7)) == 0xFF);
    CHECK(t.name(1, 7) >= 0);
    CHECK(t.name(0, 4) == -1);

    // a description record shows through at its ROM offset; others don't
    REQUIRE(t.has_records());
    CHECK(t.record_byte(uint16_t(TextTable::kDescriptions + 27 * 5)) == 0xB8); // Ｘ
    CHECK(t.record_byte(uint16_t(TextTable::kDescriptions + 27 * 5 + 26)) == 0xFF);
    CHECK(t.record_byte(uint16_t(TextTable::kDescriptions + 27 * 4)) == -1);
    CHECK(t.record_byte(0x1234) == -1);
}

TEST_CASE("merge_text: an overlay replaces by key and adds keys") {
    const std::string merged =
        merge_text("key,text,routine\na,あ,r\nb,い,r\n", "text,key\nう,b\nえ,c\n"); // any column order
    CHECK(merged == "key,text\na,あ\nb,う\nc,え\n");
    CHECK(merge_text("key,text\na,\"x,y\"\n", "") == "key,text\na,\"x,y\"\n");
}

TEST_CASE("TextTable: a layered language over the base, fixed-length keys") {
    const Charset cs = Charset::system();
    TextTable t;
    std::string error;
    REQUIRE(t.load("key,text\ndraw_party_overview_1,あい\ndraw_party_overview_2,う\n", cs, &error));
    const int base1 = t.find(TextKey::draw_party_overview_1), base2 = t.find(TextKey::draw_party_overview_2);
    REQUIRE(base1 >= 0);
    REQUIRE(t.load("key,text\ndraw_party_overview_2,ABC\n", cs, &error, nullptr, nullptr,
                   /*proportional=*/true,
                   /*layer=*/true));
    CHECK(t.find(TextKey::draw_party_overview_1) == base1); // the base's, still there
    CHECK(t.find(TextKey::draw_party_overview_2) != base2); // the layer's
    CHECK(t.proportional());
    CHECK(t.characters(uint16_t(t.find(TextKey::draw_party_overview_2))) != nullptr);
    CHECK(t.characters(uint16_t(base1)) == nullptr); // the base's string: the WSC font

    // a name-entry sheet: exactly 90 codes, no terminator, padded with spaces (and cut when longer)
    TextTable s;
    std::vector<std::string> warnings;
    REQUIRE(
        s.load("key,text\nname_entry_sheet_0,あい\n", cs, &error, &warnings, nullptr, /*proportional=*/true));
    const int off = s.find(TextKey::name_entry_sheet_0);
    REQUIRE(off >= 0);
    CHECK(s.byte(uint16_t(off)) == 0x00);
    CHECK(s.byte(uint16_t(off + 1)) == 0x01);
    CHECK(s.byte(uint16_t(off + 2)) == 0xD9);
    CHECK(s.byte(uint16_t(off + 89)) == 0xD9);
    CHECK(s.characters(uint16_t(off)) == nullptr); // always WSC codes (the picked character goes into a name)
}

TEST_CASE("Text scan keys: test-skip dialogue runs and the menu-error table") {
    // runs only reachable when a script's 0x11 test fails
    for (const uint32_t start :
         {0x354E80u, 0x354ED2u, 0x355534u, 0x3559C5u, 0x3559FBu, 0x355A31u, 0x355A5Du, 0x355A70u}) {
        bool found = false;
        for (const DialogueRun &r : kDialogueRuns)
            found = found || r.start == start;
        CHECK_MESSAGE(found, "missing dlg_ run at ", start);
    }
    // the error lines wait_menu_error_message draws by index (A2): a pointer table, keys without a site
    const TextKeyInfo &e1 = kTextKeys[size_t(TextKey::menu_error_1)];
    CHECK(e1.string == 0x34C04E);
    CHECK(kTextKeys[size_t(TextKey::menu_error_9)].string == 0x34C0D6);
    CHECK(size_t(TextKey::menu_error_9) - size_t(TextKey::menu_error_0) == 9);
}

TEST_CASE("TextTable: cell breaks become spaces on every surface, explicit dialogue breaks survive") {
    TextTable t;
    std::string error;
    char key[32];
    std::snprintf(key, sizeof key, "dlg_%06X", unsigned(kDialogueRuns[0].start));
    const auto csv = std::string("key,text\n") + key +
                     ",\"one\r\ntwo{n}three\"\n"
                     "itemdesc_5,\"one\ntwo\"\nui_party_gear,\"one\ntwo\"\n";
    REQUIRE(t.load(csv, Charset::system(), &error, nullptr, nullptr, true));
    const int at = t.dialogue(kDialogueRuns[0].start);
    REQUIRE(at >= 0);
    CHECK(t.dialogue_char(uint16_t(at + 3)) == U' ');
    CHECK(t.dialogue_char(uint16_t(at + 4)) == U't');
    CHECK(t.byte(uint16_t(at + 7)) == 0x0D);
    REQUIRE(t.record_chars(uint16_t(TextTable::kDescriptions + 27 * 5)));
    CHECK(*t.record_chars(uint16_t(TextTable::kDescriptions + 27 * 5)) == U"one two");
}

TEST_CASE("TextTable: battle templates keep hard n and lf for the shared wrapper") {
    TextTable t;
    std::string error;
    std::vector<std::string> warnings;
    const auto *message = text_message("msg_cannot_escape");
    REQUIRE(message);
    REQUIRE(t.load("key,text\nmsg_cannot_escape,one{n}two{lf}three\n", Charset::system(), &error, &warnings,
                   nullptr, true));
    CHECK(warnings.empty());
    const auto *words = t.message_template(message->parts);
    REQUIRE(words);
    std::u32string text;
    for (auto word : *words) {
        if (word == 0xFFFF)
            break;
        if (word & kTextWord) {
            REQUIRE(t.run(uint16_t(word & 0x7FFF)));
            text += *t.run(uint16_t(word & 0x7FFF));
        }
    }
    CHECK(text == U"one{n}two{lf}three");
}

TEST_CASE("text_tokens: value gaps, buttons, layout tokens, padding") {
    // values where the code draws them; the trailing padding goes (the renderer clears that far)
    TokenForm hp = text_tokens("draw_party_overview_1", "ＨＰ　　　／　　　　");
    CHECK(hp.text == "ＨＰ{hp}／{maxhp}");
    CHECK(hp.cells == 10);
    CHECK(text_tokens("draw_inn_member_summary_2", "　　　／　　　　").text == "{hp}／{maxhp}");
    CHECK(text_tokens("draw_party_overview_2", "[D1]×　").text == "[D1]×{hearts}");
    CHECK(text_tokens("draw_inn_screen_6", "　　　　　　ケロ").text == "{gold}ケロ");
    CHECK(text_tokens("draw_party_overview_18", "いまは　とうの　　かい").text ==
          "いまは　とうの{floor}かい");
    CHECK(text_tokens("draw_party_overview_19", "　　かいまで　いきました").text ==
          "{topfloor}かいまで　いきました");
    CHECK(text_tokens("draw_shop_sale_confirmation_1", "それなら　　　　　　ケロだ！　　").text ==
          "それなら{gold}ケロだ！");
    CHECK(text_tokens("buy_shop_item_into_inventory_2", "　を　かった！　　　").text == "{item}を　かった！");
    CHECK(text_tokens("show_strength_increase_message_1", "こうげきが　０あがりました！　　").text ==
          "こうげきが　{amount}あがりました！");
    CHECK(text_tokens("draw_equipment_review_member_4", "‥　　").text == "‥{value}");
    // a counted draw shows only part of the string; one draws its own value after the label
    TokenForm visible = text_tokens("draw_save_slot_summary_6", "ＨＰ　　　／　　　　");
    CHECK(visible.text == "ＨＰ{hp}");
    CHECK(visible.cells == 5);
    CHECK(text_tokens("draw_creation_type_preview_1", "ＨＰ　　　／　　　　").text == "ＨＰ{hp}");
    // buttons: lone letters, pads with a digit, START; words in Latin letters stay
    CHECK(text_tokens("draw_inventory_screen_2", "Ｘ：せんたく　Ａ：けってい　Ｂ：もどる").text ==
          "{X}：せんたく　{A}：けってい　{B}：もどる");
    CHECK(text_tokens("draw_tower_instructions_1", "Ｘ１、Ｘ３：かいすうせんたく　Ａ：けってい　　").text ==
          "{X1}、{X3}：かいすうせんたく　{A}：けってい");
    CHECK(text_tokens("draw_name_entry_screen_6", "Ｙ４：Ｙ２").text == "{Y4}：{Y2}");
    CHECK(text_tokens("draw_name_entry_screen_8", "ＳＴＡＲＴ").text == "{Start}");
    CHECK(text_tokens("draw_save_slot_summary_3", "ＥＭＰＴＹ").text == "ＥＭＰＴＹ");
    CHECK(text_tokens("draw_debug_settings_menu_2", "ＢＧせんたく").text == "ＢＧせんたく");
    CHECK(text_tokens("wait_save_confirmation_2", "Ａボタンをおしてください！").text ==
          "{A}ボタンをおしてください！");
    // buttons only (host surfaces in Japanese): the gaps and padding stay
    CHECK(text_tokens("draw_refill_instructions_1", "Ｘ：せんたく　Ａ：けってい　　", true).text ==
          "{X}：せんたく　{A}：けってい　　");
    // a cursor slot and a second heading: the original column of the word after them
    TokenForm panel = text_tokens("draw_full_option_panel_1", "ＯＮ　　／　　ＯＦＦ");
    CHECK(panel.text == "ＯＮ　　／{cursor}ＯＦＦ");
    CHECK(panel.tabs == std::vector<int>{7});
    TokenForm heads = text_tokens("draw_race_selection_screen_2", "のうりょく　　けいれき");
    CHECK(heads.text == "のうりょく{tab}けいれき");
    CHECK(heads.tabs == std::vector<int>{7});
    // pure padding leaves the sheet; debug screens and the name-entry grids keep their layout
    CHECK(padding_key("　　　　"));
    CHECK(text_tokens("clear_inn_message_1", "　　　　").text.empty());
    CHECK(text_tokens("L_351E4F_11", "Ａ　じっこう").text == "Ａ　じっこう");
    CHECK(text_tokens("name_entry_sheet_0", "あ　い　").text == "あ　い　");
}

TEST_CASE("message_tokens: named battle-popup parts; templates load by name") {
    const TextMessage *open = text_message("msg_open_party_member_popup_1");
    REQUIRE(open);
    CHECK(message_tokens(*open) == std::vector<std::string>{"target", "index", "amount"});
    CHECK(message_tokens(*text_message("msg_announce_used_battle_item")) ==
          std::vector<std::string>{"actor", "item"});
    CHECK(message_tokens(*text_message("msg_ability_gained")) ==
          std::vector<std::string>{"actor", "ability"});
    CHECK(message_tokens(*text_message("msg_party_victory")) == std::vector<std::string>{"leader"});
    CHECK(message_tokens(*text_message("msg_gold_reward")) == std::vector<std::string>{"gold"});
    CHECK(message_tokens(*text_message("msg_damage")) == std::vector<std::string>{"target", "count"});
    // every message's names are distinct, so a template can name each part
    for (const TextMessage &msg : kTextMessages) {
        auto names = message_tokens(msg);
        std::sort(names.begin(), names.end());
        CHECK_MESSAGE(std::adjacent_find(names.begin(), names.end()) == names.end(), msg.key);
    }
    // {amount} and {target} reorder the parts; the numbered form names them too
    TextTable t;
    std::string error;
    std::vector<std::string> warnings;
    REQUIRE(t.load("key,text\nmsg_open_party_member_popup_2,{amount} damage to {target}\n", Charset::system(),
                   &error, &warnings, nullptr, true));
    CHECK(warnings.empty());
    const auto *words = t.message_template(text_message("msg_open_party_member_popup_2")->parts);
    REQUIRE(words);
    REQUIRE(words->size() == 3);
    CHECK((*words)[0] == TextTable::kTemplatePart + 2);
    CHECK((*words)[2] == TextTable::kTemplatePart + 1);
    REQUIRE(t.load("key,text\nmsg_open_party_member_popup_2,{bogus} damage\n", Charset::system(), &error,
                   &warnings, nullptr, true));
    CHECK_FALSE(warnings.empty()); // an unknown name is refused (the row is skipped)
}

TEST_CASE("Tokens in the native text: values, icon widths, the original's cells") {
    // a value comes from the draw site's scope (missing: nothing), a Japanese one in the WSC font
    {
        TextValues outer;
        outer.set(U"hp", U"12");
        TextValues inner;
        inner.set(U"maxhp", U"99");
        CHECK(with_values(U"HP {hp}/{maxhp}") == U"HP 12/99");
    }
    CHECK(with_values(U"HP {hp}") == U"HP ");
    CHECK(with_values(U"{A}: OK {n}") == U"{A}: OK {n}"); // other tokens are the renderer's
    CHECK(decimal_text(5, 2) == U"5");
    CHECK(decimal_text(123, 2) == U"99");
    // a button token: the icon's width plus its shadow, or its name as text without a host source
    sawgod::set_button_token_icons({});
    CHECK(text_width(U"{A}") == text_width(U"A"));
    CHECK(text_width(U"{X}") == text_width(U"X"));
    sawgod::set_button_token_icons([](int t) {
        return t == sawgod::kButtonTokenX ? sawgod::pad_group_icon(false, false)
                                          : sawgod::named_icon("pad A");
    });
    const int a = text_width(U"{A}"), x = text_width(U"{X}");
    CHECK(a == sawgod::named_icon("pad A").w + 1 + 2); // + the pen inset and the shadow column
    CHECK(x == sawgod::kButtonIconHeight + 1 + 2);
    CHECK(text_width(U"{A}: OK") > text_width(U": OK"));
    sawgod::set_button_token_icons({});

    // the draw clears the rest of the original's cells, and {cursor} moves the pen to its column
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    Machine m(w);
    TextLayout layout{10, {}};
    CHECK(draw_text_pixels(m, 0x1000, 0x1F00, U"HP 1/2", -1, &layout) == 10);
    CHECK(draw_text_pixels(m, 0x1000, 0x1F00, U"HP 1/2") == (text_width(U"HP 1/2") + 7) / 8);
    TextLayout panel{9, {7}};
    CHECK(draw_text_pixels(m, 0x1000, 0x1F00, U"On /{cursor}Off", -1, &panel) == 10); // Off from cell 7
}

TEST_CASE("TextTable: the original's layout under a proportional string, base icons when on") {
    const Charset cs = Charset::system();
    TextTable t;
    std::string error;
    REQUIRE(t.load(
        "key,text\ndraw_party_overview_1,ＨＰ　　　／　　　　\ndraw_save_menu_3,Ａ：けってい　Ｂ：もどる\n",
        cs, &error));
    const int icons = t.find(TextKey::draw_save_menu_3);
    REQUIRE(icons >= 0);
    CHECK(t.icon_text(uint16_t(icons)) == nullptr); // off: the base draws as the ROM does
    t.set_button_icons(true);
    REQUIRE(t.icon_text(uint16_t(icons)));
    CHECK(t.icon_text(uint16_t(icons))->substr(0, 3) == U"{A}");
    CHECK((*t.icon_text(uint16_t(icons)))[3] >= 0xE000); // the rest in the WSC font's glyphs
    REQUIRE(t.load("key,text\ndraw_party_overview_1,HP {hp}/{maxhp}\n", cs, &error, nullptr, nullptr,
                   /*proportional=*/true, /*layer=*/true));
    const TextLayout *layout = t.layout(uint16_t(t.find(TextKey::draw_party_overview_1)));
    REQUIRE(layout);
    CHECK(layout->cells == 10);
}

TEST_CASE("message recorder snapshots reused buffers and keeps announcements beyond 25 words") {
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    w.io[0x60] = 0x80; // enable the color machine's upper RAM
    auto &session = Session::of(w);
    std::string error;
    const auto *msg = text_message("msg_ability_replaced");
    REQUIRE(msg);
    REQUIRE(session.text().load("key,text\nmsg_ability_replaced,{actor}'s {ability} becomes {new_ability}.\n"
                                "show_ability_replaced_message_1, \nshow_ability_replaced_message_2, \n"
                                "show_ability_replaced_message_3, \nshow_ability_replaced_message_4, \n",
                                Charset::system(), &error, nullptr, nullptr, true));
    Machine m(w);
    const auto ram = [&](uint16_t at, uint16_t code, bool first) {
        m.wr16(0, at, code);
        m.wr16(0, uint16_t(at + 2), 0xFFFF);
        m.ds() = 0;
        m.si() = at;
        m.ax() = 255;
        m.bx() = first ? 0 : 255;
        session.message_part(m);
    };
    const auto fragment = [&](TextKey key) {
        const auto p = text(m, key);
        m.ds() = p.segment;
        m.si() = p.offset;
        m.ax() = m.bx() = 255;
        session.message_part(m);
    };
    ram(0xD60F, 0xA1 * 2, true);
    fragment(TextKey::show_ability_replaced_message_1);
    ram(0xD64F, 0xA2 * 2, false);
    fragment(TextKey::show_ability_replaced_message_2);
    ram(0xD64F, 0xA3 * 2, false);
    fragment(TextKey::show_ability_replaced_message_3);
    fragment(TextKey::show_ability_replaced_message_4);
    session.apply_message_template(m);
    std::vector<uint16_t> names;
    for (uint16_t at = 0xD55A; at < 0xD55A + 52 && m.rd16(0, at) != 0xFFFF; at += 2)
        if (!(m.rd16(0, at) & kTextWord))
            names.push_back(m.rd16(0, at));
    CHECK(names == std::vector<uint16_t>{0xA1 * 2, 0xA2 * 2, 0xA3 * 2});

    // No template: the recorded native glyphs also survive the fixed queue's limit.
    m.ds() = 0;
    m.si() = 0xD60F;
    m.bx() = 0;
    m.ax() = 40;
    for (uint16_t i = 0; i < 40; ++i)
        m.wr16(0, uint16_t(m.si() + i * 2), 0xA1 * 2);
    session.message_part(m);
    session.apply_message_template(m);
    CHECK(m.rd16(0, 0xD55A) == (kTextWord | TextTable::kMessageRun));
    CHECK(m.rd16(0, 0xD55C) == 0xFFFF);
    REQUIRE(session.text().run(TextTable::kMessageRun));
    CHECK(session.text().run(TextTable::kMessageRun)->size() == 40);
}

TEST_CASE("joined battle popup previews all complete templates without changing machine state") {
    const std::vector<std::string_view> routines = {"announce_battle_action",
                                                    "announce_battle_defense",
                                                    "show_ability_replaced_message",
                                                    "show_actor_amount_message",
                                                    "show_actor_item_failed_message",
                                                    "show_actor_recovered_message",
                                                    "show_element_resistance_message",
                                                    "show_gold_theft_result_message",
                                                    "show_magic_reflected_or_game_over_message",
                                                    "show_target_attack_message",
                                                    "show_target_description_message",
                                                    "show_status_bit1_message",
                                                    "show_status_bit3_message",
                                                    "show_scripture_ineffective_message",
                                                    "show_target_strength_decreased_message",
                                                    "show_target_agility_decreased_message",
                                                    "show_target_magic_or_defense_decreased_message"};
    int checked = 0;
    for (const auto &message : kTextMessages) {
        const std::string_view routine = message.routine, key = message.key;
        if (std::find(routines.begin(), routines.end(), routine) == routines.end())
            continue;
        CAPTURE(key);
        std::vector<std::string> parts;
        for (std::string_view rest = message.parts; !rest.empty();) {
            const size_t plus = rest.find('+');
            parts.emplace_back(rest.substr(0, plus));
            rest = plus == std::string_view::npos ? std::string_view{} : rest.substr(plus + 1);
        }
        size_t first = 2;
        if (routine == "announce_battle_action" || routine == "show_ability_replaced_message" ||
            routine == "show_actor_item_failed_message" || routine == "show_element_resistance_message" ||
            routine == "show_gold_theft_result_message" || routine == "show_target_attack_message")
            first = 4;
        if (key.ends_with("_group") && routine != "announce_battle_action")
            first = 5;
        const auto tokens = message_tokens(message);
        std::string line = "complete";
        for (const auto &token : tokens)
            line += "|{" + token + "}";
        line += "|end";
        std::string csv = "key,text\n" + std::string(key) + "," + line + "\n";
        for (const auto &part : parts)
            if (!part.starts_with("R:"))
                csv += part + ",fragment\n";
        std::vector<uint8_t> rom(0x400000, 0xFF);
        rom[0x394BE8] = 0xA3; // the replacement item's first glyph
        sawgod::emu::Wsc w(std::move(rom), 0);
        w.io[0x60] = 0x80;
        auto &session = Session::of(w);
        std::string error;
        REQUIRE(session.text().load(csv, Charset::system(), &error, nullptr, nullptr, true));
        Machine m(w);
        m.ss() = 0;
        m.wr8(0, 0xEC83, key.ends_with("_target") ? 0 : key.ends_with("_group") ? 1 : 2);
        m.wr8(0, 0xDC00, 0);
        m.wr8(0, 0xD6D6, 9);
        m.wr16(0, 0xDC0B, 4321);
        m.wr8(0, 0xDBFD, 1);
        m.wr8(0, 0xDC0A, key.starts_with("msg_target_defense_") ? 8 : 10);
        for (uint16_t at : {0xD60F, 0xD62F, 0xD64F, 0xD66F}) {
            m.wr16(0, at, uint16_t(0xA1 * 2));
            m.wr16(0, uint16_t(at + 2), 0xFFFF);
        }
        JoinedBattlePopup joined(m, routine);
        for (size_t i = 0; i < first; ++i) {
            if (parts[i].starts_with("R:")) {
                m.ds() = 0;
                m.si() = uint16_t(std::stoul(parts[i].substr(2), nullptr, 16));
            } else {
                for (size_t k = 0; k < kTextKeyCount; ++k)
                    if (parts[i] == kTextKeys[k].name) {
                        const auto p = text(m, TextKey(k));
                        m.ds() = p.segment;
                        m.si() = p.offset;
                        break;
                    }
            }
            m.ax() = 255;
            m.bx() = i ? 255 : 0;
            session.message_part(m);
        }
        std::u32string expected = U"complete";
        for (size_t i = 0; i < parts.size(); ++i) {
            if (!parts[i].starts_with("R:"))
                continue;
            expected += U'|';
            if (i >= first && parts[i] == "R:D66F" && routine == "announce_battle_action")
                expected += char32_t(0xE000 + 0xBB + 9);
            else if (i >= first && parts[i] == "R:D66F" &&
                     routine.find("_decreased_message") != std::string_view::npos)
                for (char digit : std::string("4321"))
                    expected += char32_t(0xE000 + 0xBB + digit - '0');
            else
                expected += char32_t(
                    0xE000 + (i >= first && routine == "show_ability_replaced_message" ? 0xA3 : 0xA1));
        }
        expected += U"|end";
        const auto before = w.snapshot();
        REQUIRE(session.present_joined_battle_popup(m));
        REQUIRE(session.joined_battle_popup_text());
        CHECK(*session.joined_battle_popup_text() == expected);
        CHECK_FALSE(
            session.present_joined_battle_popup(m)); // the second machine pass keeps the same host popup
        CHECK(*session.joined_battle_popup_text() == expected);
        CHECK(w.ram == before.ram);
        CHECK(w.io == before.io);
        CHECK(w.sram == before.sram);
        CHECK(w.frame_count == before.frame_count);
        CHECK(std::equal(std::begin(w.cpu.s.r), std::end(w.cpu.s.r), std::begin(before.cpu.s.r)));
        CHECK(std::equal(std::begin(w.cpu.s.seg), std::end(w.cpu.s.seg), std::begin(before.cpu.s.seg)));
        CHECK(w.cpu.s.ip == before.cpu.s.ip);
        CHECK(w.cpu.s.flags == before.cpu.s.flags);
        ++checked;
    }
    CHECK(checked == 27);
}

TEST_CASE("joined popup keeps original name words while presenting full player names") {
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000, 0xFF), 0);
    w.io[0x60] = 0x80;
    auto &s = Session::of(w);
    s.player_names().enabled = true;
    s.player_names().values[0] = "WWWWWWWW";
    std::string error;
    REQUIRE(s.text().load("key,text\nmsg_actor_recovered,{actor} recovered.\n"
                          "show_actor_recovered_message_1,translated fragment\nitemdesc_5,X\n",
                          Charset::system(), &error, nullptr, nullptr, true));
    Machine m(w);
    m.ss() = 0;
    constexpr uint16_t description = TextTable::kDescriptions + 27 * 5;
    CHECK(m.rd8(0x9000, description) == 0xB8);
    m.wr8(0, 0xD001, 0xA1);
    m.wr8(0, 0xD002, 0xA2);
    m.wr8(0, 0xD003, 0xFF);
    {
        JoinedBattlePopup joined(m, "show_actor_recovered_message");
        CHECK(m.rd8(0x9000, description) == 0xFF); // scrolling must read the original tile-table bytes
        Machine host(w);
        CHECK(host.rd8(0x9000, description) == 0xB8); // other host UI remains localized while the fiber waits
        CHECK(text(host, TextKey::show_actor_recovered_message_1).segment == kTextSegment);
        m.ax() = 0;
        m.di() = 0xD60F;
        build_status_icons_core(m);
        CHECK(m.rd16(0, 0xD60F) == 0xA1 * 2);
        CHECK(m.rd16(0, 0xD611) == 0xA2 * 2);
        CHECK(m.rd16(0, 0xD613) == 0xFFFF);
        CHECK(m.ax() == 0xFFFF);
        CHECK(m.cx() == 2);
        CHECK(m.si() == 0xD003);
        CHECK(m.di() == 0xD613);
        m.ds() = 0;
        m.si() = 0xD60F;
        m.ax() = 255;
        m.bx() = 0;
        append_word_queue_core(m);
        const auto p = text(m, TextKey::show_actor_recovered_message_1);
        CHECK(p.segment == 0x8000); // translation is recorded for the host, not written to the queue
        m.ds() = p.segment;
        m.si() = p.offset;
        m.ax() = m.bx() = 255;
        append_word_queue_core(m);
        CHECK(m.rd16(0, 0xD55A) == 0xA1 * 2);
        CHECK(m.rd16(0, 0xD55C) == 0xA2 * 2);
        CHECK(m.rd16(0, 0xD55E) == 0xFFFF);
        const auto before = w.snapshot();
        REQUIRE(s.present_joined_battle_popup(m));
        REQUIRE(s.joined_battle_popup_text());
        CHECK(*s.joined_battle_popup_text() == U"WWWWWWWW recovered.");
        CHECK(w.ram == before.ram);
        CHECK(w.io == before.io);
    }
    CHECK_FALSE(s.joining_battle_popup());
    CHECK(m.rd8(0x9000, description) == 0xB8);
    CHECK(text(m, TextKey::show_actor_recovered_message_1).segment == kTextSegment);
}

TEST_CASE("battle popup clock counts original fragments names and digits before translation") {
    std::vector<uint8_t> rom(0x400000, 0xFF);
    const auto &key = kTextKeys[size_t(TextKey::show_damage_message_1)];
    // A three-word original fragment and a five-byte original monster name.
    for (unsigned i = 0; i < 3; ++i) {
        rom[key.string + 2 * i] = 0x42;
        rom[key.string + 2 * i + 1] = 0;
    }
    for (unsigned i = 0; i < 5; ++i)
        rom[0x396FE0 + i] = 0xA1;
    sawgod::emu::Wsc w(rom, 0);
    w.io[0x60] = 0x80;
    auto &s = Session::of(w);
    std::string error;
    REQUIRE(s.text().load("key,text\nmonster_0,Monster\nshow_damage_message_1,translated fragment\n",
                          Charset::system(), &error, nullptr, nullptr, true));
    Machine m(w);
    m.ss() = 0;
    const auto queue = [&](uint16_t buffer, std::initializer_list<uint16_t> words, bool first) {
        unsigned i = 0;
        for (auto word : words)
            m.wr16(0, uint16_t(buffer + 2 * i++), word);
        m.wr16(0, uint16_t(buffer + 2 * i), 0xFFFF);
        m.ds() = 0;
        m.si() = buffer;
        m.bx() = first ? 0 : 255;
        m.ax() = 255;
        s.message_part(m);
    };
    const int run = s.text().name_run(int(NameTable::Monster), 0);
    REQUIRE(run >= 0);
    queue(0xD62F, {uint16_t(kTextWord | run)}, true);
    const auto fragment = text(m, TextKey::show_damage_message_1);
    m.ds() = fragment.segment;
    m.si() = fragment.offset;
    m.bx() = m.ax() = 255;
    s.message_part(m);
    queue(0xD66F, {0xBB * 2, 0xBC * 2}, false);
    CHECK(s.apply_message_template(m) == 10); // 5 name + 3 fragment + 2 digits
    CHECK(s.apply_message_template(m) == 0);  // consumed, never reused for another popup

    m.wr8(0, 0xD001, 0xA1);
    m.wr8(0, 0xD002, 0xA2);
    m.wr8(0, 0xD003, 0xFF);
    queue(0xD60F, {uint16_t(kTextWord | TextTable::kPlayerNameRun)}, true);
    CHECK(s.apply_message_template(m) == 2); // the machine projection, even for a longer host name
    queue(0xD60F, {uint16_t(kTextWord | run)}, true);
    queue(0xD60F, {0xA1 * 2}, true);
    CHECK(s.apply_message_template(m) == 1); // BX=0 discards the previous clock as well as its text
}

#include "game/text_wrap.h"
#include "game/ui_chrome.h"
TEST_CASE("key item marker: one ID predicate, explicit resource token and measured runs") {
    const std::vector<int> marked_ids{20, 21, 22, 23, 24, 25, 26, 30, 31};
    for (int id = -1; id <= 256; ++id)
        CHECK(is_marked_key_item(id) ==
              (std::find(marked_ids.begin(), marked_ids.end(), id) != marked_ids.end()));
    size_t length = 0;
    CHECK(key_item_token(U"{key:item_20}!", &length) == 20);
    CHECK(length == 13);
    CHECK(key_item_token(U"{key:item_255}") == 255);
    std::u32string_view display;
    CHECK(key_item_token(U"{key:item_20:Air Seeds}!", &length, &display) == 20);
    CHECK(length == 23);
    CHECK(display == U"Air Seeds");
    CHECK(key_item_token(U"{key:item_20}", &length, &display) == 20);
    CHECK(display.empty());
    CHECK(key_item_token(U"{key:item_31:Board's: [D1] {literal}", &length, &display) == 31);
    CHECK(display == U"Board's: [D1] {literal");
    for (auto invalid : {U"{key:item_256}", U"{key:item_-1}", U"{key:item_020}", U"{key:item_}",
                         U"{key:item_20x}", U"{key:item_20", U"{key:item_20:}", U"{key:item_20:Air Seeds",
                         U"{key:item_256:Air Seeds}", U"{key:item_020:Seeds}"})
        CHECK(key_item_token(invalid) == -1);
    const auto resolve = [](int id) { return id == 20 ? U"Air Seed" : U"King's Sword"; };
    const auto marked = expand_key_items(U"{key:item_20}", resolve);
    CHECK(marked == marked_item_run(20, U"Air Seed"));
    CHECK(text_width(marked) == 5 + text_width(U"Air Seed"));
    CHECK(Chrome::text_width(marked) == 5 + Chrome::text_width(U"Air Seed"));
    CHECK(Chrome::text_width(Chrome::item_run(20, U"Air Seed", true)) ==
          10 + Chrome::text_width(U"Air Seed"));
    const auto plain = expand_key_items(U"{key:item_17}", resolve);
    CHECK(plain == marked_item_run(17, U"King's Sword"));
    CHECK(text_width(plain) == 8 + text_width(U"King's Sword"));
    CHECK(Chrome::item_symbol(17, 29) == 1);
    CHECK(Chrome::item_symbol(18, 29) == 8);
    CHECK(Chrome::item_symbol(19, 29) == 9);
    CHECK(Chrome::item_symbol(32, 1) == 1);
    const auto wrapped = wrap_text(U"Dip an " + marked + U" in ocean water.", 70);
    CHECK(std::any_of(wrapped.lines.begin(), wrapped.lines.end(),
                      [&](const auto &line) { return line.text.find(marked) != std::u32string::npos; }));
}

TEST_CASE("key item display text is literal, measured and wrapped as one run without a name lookup") {
    const auto no_lookup = [](int) -> std::u32string {
        FAIL("display-text tokens must not resolve an item name");
        return {};
    };
    const auto marked = expand_key_items(U"{key:item_20:Air Seeds}", no_lookup);
    CHECK(marked == marked_item_run(20, U"Air Seeds"));
    CHECK(text_width(marked) == 5 + text_width(U"Air Seeds"));
    CHECK(Chrome::text_width(marked) == 5 + Chrome::text_width(U"Air Seeds"));
    const auto wrapped = wrap_text(U"Many " + marked + U" grow on the palm tree.", 60);
    REQUIRE(wrapped.lines.size() > 1);
    CHECK(std::any_of(wrapped.lines.begin(), wrapped.lines.end(),
                      [&](const auto &line) { return line.text.find(marked) != std::u32string::npos; }));
    const auto plain = expand_key_items(U"{key:item_17:King's Swords}", no_lookup);
    CHECK(plain == marked_item_run(20, U"King's Swords"));
    CHECK(text_width(plain) == 5 + text_width(U"King's Swords"));
    const auto literal = expand_key_items(U"{key:item_20:[D1] {key:literal}", no_lookup);
    const auto label = std::u32string(1, kLiteralBracket) + U"D1] " + kLiteralBrace + U"key:literal";
    CHECK(literal == marked_item_run(20, label));
    CHECK(text_width(literal) == 5 + text_width(U"[D1] {key:literal"));
    CHECK(font_glyph(kLiteralBrace).rows == font_glyph(U'{').rows);
    CHECK(font_glyph(kLiteralBracket).rows == font_glyph(U'[').rows);

    TextTable t;
    std::string error;
    std::vector<std::string> warnings;
    REQUIRE(t.load("key,text\nui_battle_fight,{key:item_20:Air Seeds}\n"
                   "ui_battle_run,{key:item_17:Swords}\n"
                   "dlg_355550,{key:item_20:[D1] {key:literal}\n",
                   Charset::system(), &error, &warnings, nullptr, true));
    CHECK(warnings.empty());
    REQUIRE(t.host("ui_battle_fight"));
    REQUIRE(t.host("ui_battle_run"));
    CHECK(*t.host("ui_battle_fight") == marked);
    CHECK(*t.host("ui_battle_run") == marked_item_run(20, U"Swords"));
    REQUIRE(t.load("key,text\nitem_20,Renamed Seed\n", Charset::system(), &error, &warnings, nullptr, true,
                   true));
    CHECK(*t.host("ui_battle_fight") == marked);
    for (const auto &token : {"{key:item_20:}", "{key:item_20:Air Seeds"}) {
        warnings.clear();
        REQUIRE(t.load(std::string("key,text\nui_battle_fight,") + token + "\n", Charset::system(), &error,
                       &warnings, nullptr, true, true));
        CHECK_FALSE(warnings.empty());
        CHECK(warnings.back().find("invalid or missing") != std::string::npos);
    }
}

TEST_CASE("key item resource references resolve names regardless of CSV order, including layered names") {
    TextTable t;
    std::string error;
    std::vector<std::string> warnings;
    REQUIRE(t.load("key,text\nui_battle_fight,{key:item_20}!\n"
                   "dlg_355423,Dip an {key:item_20} in water.\nitem_20,Air Seed\nitem_17,King's Sword\n"
                   "ui_battle_run,{key:item_17}\n",
                   Charset::system(), &error, &warnings, nullptr, true));
    CHECK(warnings.empty());
    REQUIRE(t.host("ui_battle_fight"));
    CHECK(*t.host("ui_battle_fight") == marked_item_run(20, U"Air Seed") + U"!");
    REQUIRE(t.host("ui_battle_run"));
    CHECK(*t.host("ui_battle_run") == marked_item_run(17, U"King's Sword"));
    REQUIRE(t.load("key,text\nui_battle_fight,{key:item_20}\nitem_20,Changed Seed\n", Charset::system(),
                   &error, &warnings, nullptr, true, true));
    CHECK(*t.host("ui_battle_fight") == marked_item_run(20, U"Changed Seed"));
    REQUIRE(
        t.load("key,text\nitem_20,Modded Seed\n", Charset::system(), &error, &warnings, nullptr, true, true));
    CHECK(*t.host("ui_battle_fight") == marked_item_run(20, U"Modded Seed"));
    REQUIRE(t.load("key,text\nui_battle_fight,{key:item_256}\n", Charset::system(), &error, &warnings,
                   nullptr, true, true));
    REQUIRE_FALSE(warnings.empty());
    CHECK(warnings.back().find("invalid or missing") != std::string::npos);
}

TEST_CASE("key token forms distinguish all item ID classes and preserve ordinary equipment arguments") {
    for (int id = 0; id < 256; ++id) {
        const auto name =
            expand_key_items(utf8_to_u32("{key:item_" + std::to_string(id) + "}"), [](int) { return U"A"; });
        const auto hint = expand_key_items(utf8_to_u32("{key:item_" + std::to_string(id) + ":A}"),
                                           [](int) -> std::u32string {
                                               FAIL("literal looked up");
                                               return {};
                                           });
        const bool marked = is_marked_key_item(id) || is_kings_gear(id);
        CHECK(name == (marked ? marked_item_run(id, U"A") : U"A"));
        CHECK(hint == (marked ? marked_item_run(is_kings_gear(id) ? 20 : id, U"A") : U"A"));
        CHECK(text_width(name) == text_width(U"A") + (marked ? inline_item_advance(id) : 0));
        CHECK(Chrome::text_width(hint) == text_width(U"A") + (marked ? 5 : 0));
        if (is_kings_gear(id))
            CHECK(Chrome::item_run(id, U"A").front() == Chrome::kItemToken + id);
    }
}
