#include "game/text_fit.h"
#include <algorithm>
#include <doctest/doctest.h>
using namespace sawgod::game;

namespace {
const char *kBase = "key,text,routine,kind\n"
                    "monster_1,ガーリック,,\n"
                    "item_3,ロングソード,,\n"
                    "item_obtained_suffix,　を　てにいれた,,text\n"
                    "msg:R:D60F+a_1+R:D64F+a_2,{1}は　{2}で　,a,message\n"
                    "dlg_1,{name:1}「やあ」{wait}{n}げんきか？,dialogue,dialogue\n"
                    "menu_1,ＨＰ,m,text\n";

bool has(const std::vector<FitIssue> &issues, const std::string &key, const std::string &problem,
         bool error = true) {
    for (const auto &i : issues)
        if (i.key == key && i.problem.find(problem) != std::string::npos && i.error == error)
            return true;
    return false;
}
} // namespace

TEST_CASE("text fit: a translation that fits passes") {
    const auto issues = check_fit(kBase, "key,text\n"
                                         "monster_1,Garlic\n"
                                         "item_3,Longsword\n"
                                         "msg:R:D60F+a_1+R:D64F+a_2,{1} uses {2}\n"
                                         "dlg_1,{name:1}: Hi!{n}How are you?\n"
                                         "menu_1,HP\n");
    CHECK(std::none_of(issues.begin(), issues.end(), [](const auto &i) { return i.error; }));
}

TEST_CASE("text fit: budgets by key family") {
    CHECK(fit_budget("msg:x", "").width == 216);
    CHECK(fit_budget("item_3", "").width == 104);
    CHECK(fit_budget("item_obtained_suffix", "　を　てにいれた").surface == "menu (original layout)");
    CHECK(fit_budget("dlg_1", "").lines == 3);
    CHECK(fit_budget("menu_1", "ＨＰ").width == 16);
    CHECK_FALSE(fit_budget("menu_1", "ＨＰ").hard);
}

TEST_CASE("text fit: worst-case names, placeholders, font, unknown keys") {
    const auto issues = check_fit(
        kBase, "key,text\n"
               "monster_1,Extraordinarily Long Garlic Monster\n"     // plate too wide; longest actor name
               "msg:R:D60F+a_1+R:D64F+a_2,{1} attacks with a {2}!\n" // too wide with that name
               "item_3,Long☃sword\n"                                 // ☃ not in either font
               "dlg_1,{name:2} says: {n}hello\n"                     // a different party member
               "menu_1,Hit Points\n"                                 // wider than the original: a warning
               "nope,x\n");
    CHECK(has(issues, "monster_1", "too wide"));
    CHECK_FALSE(has(issues, "msg:R:D60F+a_1+R:D64F+a_2", "too wide with the longest names"));
    CHECK(has(issues, "item_3", "not in the font"));
    CHECK(has(issues, "dlg_1", "party names"));
    CHECK(has(issues, "menu_1", "wider than the original layout", false));
    CHECK(has(issues, "nope", "unknown key"));
    const auto missing = check_fit(kBase, "key,text\nmsg:R:D60F+a_1+R:D64F+a_2,{1} attacks!\n");
    CHECK(has(missing, "msg:R:D60F+a_1+R:D64F+a_2", "tokens differ"));
}

TEST_CASE("text fit: dialogue lines and boxes, with eight-glyph names") {
    const auto issues = check_fit(kBase, "key,text\n"
                                         "dlg_1,\"{name:1}: well, hello there, my good old friend, and "
                                         "another very long sentence!{n}2{n}3{n}4{n}5\"\n");
    CHECK_FALSE(has(issues, "dlg_1", "line too wide"));
    CHECK(has(issues, "dlg_1", "lines, scrolls", false));
    const auto paced = check_fit(kBase, "key,text\ndlg_1,{name:1}: Hi!{wait}{clear}How are you?{pause:8}\n");
    CHECK(paced.empty()); // pacing codes are the translator's
}

TEST_CASE("text fit: the WSC font's symbols count as drawable") {
    CHECK(check_fit(kBase, "key,text\nitem_3,○Flame\n").empty());
}

TEST_CASE("text fit report is CSV with a header") {
    const auto r = fit_report({{"k", "s", "p, with comma", "t", 10, 5, true}});
    CHECK(r.find("key,severity,surface,width,budget,problem,text\n") == 0);
    CHECK(r.find("k,error,s,10,5,\"p, with comma\",t") != std::string::npos);
}

#include "game/session.h"
#include "game/text.h"
#include "game/text_render.h"

TEST_CASE("host strings: the language's, else the default for the language loaded") {
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    auto &s = Session::of(w);
    const auto fight_ja = host_text(w, "ui_battle_fight");
    CHECK(fight_ja == with_wsc_glyphs(U"たたかう")); // nothing loaded: Japanese, as WSC glyphs
    CHECK(fight_ja[0] >= 0xE000);
    std::string error;
    REQUIRE(
        s.text().load("key,text\nui_battle_run,Flee\n", Charset::system(), &error, nullptr, nullptr, true));
    CHECK(host_text(w, "ui_battle_run") == U"Flee");
    CHECK(host_text(w, "ui_battle_fight") == U"Fight"); // a translation without the key: English default
    CHECK(host_text(w, "ui_battle_group_count", U"3") == U" x3");
    CHECK(host_text(w, "no_such_key").empty());
    // English laid over a Japanese base that has the key: still the English default, never the Japanese.
    REQUIRE(s.text().load("key,text\nui_battle_fight,たたかう\n", Charset::system(), &error));
    REQUIRE(s.text().load("key,text\nui_battle_run,Flee\n", Charset::system(), &error, nullptr, nullptr, true,
                          true));
    CHECK(host_text(w, "ui_battle_fight") == U"Fight");
    CHECK(host_text(w, "ui_battle_run") == U"Flee");
    CHECK(fit_budget("ui_save_autosave", "").width == 28);
    CHECK(host_text(w, "ui_save_autosave") == U"AUTO");
    CHECK(fit_budget("ui_party_current_floor", "").width == 105);
    CHECK(fit_budget("ui_party_highest_floor", "").width == 114);
    CHECK(host_text(w, "ui_save_inherit_first") + U" " + host_text(w, "ui_save_inherit_second") ==
          U"Inherit Bestiary and start a New Game+?");
}

TEST_CASE("text fit: every surface checks its own width and severity") {
    const auto issues = check_fit(kBase, "key,text\nmonster_1,WWWWWWWW\nitem_3,WWWWWWWWWWWWWWWW\n");
    CHECK(has(issues, "monster_1", "too wide", false));      // native 64 px only
    CHECK_FALSE(has(issues, "monster_1", "too wide", true)); // 66 px fits creation's 69
    CHECK(has(issues, "item_3", "too wide", true));          // fits 170, exceeds shop 114 and transfer 129
    const auto surfaces = fit_surfaces("item_3", "");
    CHECK(surfaces.size() >= 10);
    CHECK(fit_budget("ui_inventory_items", "").width == 48);
    CHECK(fit_budget("ui_name_entry_label", "").width == 68);
}

TEST_CASE("text fit: scrolling surfaces have no hard character limit") {
    const std::string base = "key,text\nitemdesc_1,せつめい\nui_inventory_passive,パッシブ\n";
    const auto report = parse_csv(fit_budgets_report(base, "key,text\nitemdesc_1,A very long description\n"));
    REQUIRE(report.size() == 3);
    CHECK(report[1][7] == "ok");
    CHECK(report[2][7] == "ok");
    const auto issues = check_fit(base, "key,text\nitemdesc_1," + std::string(100, 'W') + "\n");
    REQUIRE(issues.size() == 1);
    CHECK_FALSE(issues[0].error);
    CHECK(issues[0].surface == "native item description");
}

TEST_CASE("text fit: sheet templates reserve repeated arguments and shared currency") {
    const std::string sample = "key,text\nmonster_1,Garlic\nitem_3,Longsword\n";
    const auto report = parse_csv(fit_budgets_report(kBase, sample));
    for (const auto &r : report)
        if (r[0] == "msg:R:D60F+a_1+R:D64F+a_2") {
            CHECK(std::stoi(r[6]) > 100);
            CHECK(r[7] == "ok");
        }
    const auto repeated = parse_csv(fit_budgets_report("key,text\nmsg:R:D60F+x,{1} {1}\n", sample));
    REQUIRE(repeated.size() == 2);
    CHECK(std::stoi(repeated[1][6]) >= 128);
    // Kero fits every pane now that the inn shows it on its own line (it was 54 px of 50 beside the price);
    // the save summary still reserves its gold in front of the currency.
    const auto currency = check_fit("key,text\nui_party_kero,ケロ\n", "key,text\nui_party_kero,Kero\n");
    CHECK_FALSE(has(currency, "ui_party_kero", "too wide"));
    const auto long_currency =
        check_fit("key,text\nui_party_kero,ケロ\n", "key,text\nui_party_kero,Kerokerokero\n");
    CHECK(has(long_currency, "ui_party_kero", "too wide"));
    CHECK(fit_sample_median("key,text\na,WW\nb,ii\n") > 0);
}

TEST_CASE("text fit: dialogue speaker and body are separate surfaces") {
    const auto issues =
        check_fit("key,text\ndlg_a,はなし\n", "key,text\ndlg_a,Hero: “" + std::string(32, 'A') + "”\n");
    CHECK(issues.empty()); // body fits 214 even though prefix plus body does not
    const auto wide =
        check_fit("key,text\ndlg_a,はなし\n", "key,text\ndlg_a," + std::string(30, 'W') + ": “Hi”\n");
    REQUIRE(wide.size() == 1);
    CHECK(wide[0].surface == "dialogue speaker tab");
}

TEST_CASE("text fit: every host key declares a surface") {
    for (const auto &h : kHostText) {
        CAPTURE(h.key);
        CHECK_FALSE(fit_surfaces(h.key, h.japanese).empty());
        CHECK_FALSE(fit_budget(h.key, h.japanese).surface.empty());
    }
}

TEST_CASE("text fit: battle preview checks the composed data row and the actor plus target") {
    std::string base = "key,text\n", english = base;
    for (const auto &h : kHostText)
        if (std::string_view(h.key).find("ui_battle_preview_") == 0) {
            base += std::string(h.key) + "," + h.japanese + "\n";
            english += std::string(h.key) + "," + h.english + "\n";
        }
    CHECK(fit_budget("ui_battle_preview_element_fire", "").width == 189);
    INFO(fit_report(check_fit(base, english)));
    CHECK(check_fit(base, english).empty());
    CHECK(check_fit(base, base).empty()); // all Japanese defaults are drawable and fit together
    CHECK(check_fit(base, "key,text\nui_battle_preview_element_fire,Fire\n"
                          "ui_battle_preview_range_ranged,Ranged\nui_battle_preview_target_group,One group\n")
              .empty());
    // Individually these labels fit, but their three-field combination does not.
    const auto issues = check_fit(
        base, "key,text\nui_battle_preview_element_fire,WWWWWWWWWWWW\n"
              "ui_battle_preview_range_ranged,WWWWWWWWWWWW\nui_battle_preview_target_group,WWWWWWWWWWWW\n");
    CHECK(has(issues, "ui_battle_preview_element_fire", "too wide", true));
    CHECK(has(issues, "ui_battle_preview_range_ranged", "too wide", true));
    CHECK(has(issues, "ui_battle_preview_target_group", "too wide", true));
    CHECK(has(check_fit(base, "key,text\nui_battle_preview_separator,WWWWWWWWWWWWWW\n"),
              "ui_battle_preview_separator", "too wide", true));
    const auto surfaces = fit_surfaces("monster_1", "");
    const auto target = std::find_if(surfaces.begin(), surfaces.end(), [](const auto &b) {
        return b.surface == "battle action preview target";
    });
    REQUIRE(target != surfaces.end());
    CHECK(target->width == 175); // actor, arrow and count share the row with this name
    CHECK(target->prefix == "{name:0}");
}

TEST_CASE("text fit: runtime fallback arguments affect only the checked rows") {
    const std::string language = "key,text\nmsg:R:D60F+a_1+R:D64F+a_2,{1} uses {2}\n";
    const std::string context = "key,text\nmonster_1," + std::string(30, 'W') + "\n";
    CHECK(check_fit(kBase, language).empty());
    const auto issues = check_fit(kBase, language, context);
    CHECK_FALSE(has(issues, "msg:R:D60F+a_1+R:D64F+a_2", "too wide with the longest names"));
    for (const auto &issue : issues)
        CHECK(issue.key != "monster_1");
}

TEST_CASE("text fit: sheet reports shared wrapping, scrolling, too-wide words and ignored cell breaks") {
    const auto base = "key,text\ndlg_x,hello\nitem_1,sword\n";
    auto report = parse_csv(fit_budgets_report(base, "key,text\ndlg_x,one{n}two{n}three{n}four\n"));
    REQUIRE(report.size() == 3);
    CHECK(report[1][7] == "4 lines, scrolls");
    report = parse_csv(fit_budgets_report(base, "key,text\ndlg_x,\"hello\nworld\"\n"));
    CHECK(report[1][7] == "cell line break ignored");
    report = parse_csv(fit_budgets_report(base, "key,text\ndlg_x," + std::string(50, 'W') + "\n"));
    CHECK(report[1][7].find("too wide by ") != std::string::npos);
    report = parse_csv(fit_budgets_report(base, "key,text\nitem_1,\"sword\nname\"\n"));
    CHECK(report[2][7].find("cell line break ignored") != std::string::npos);
}

TEST_CASE("text fit: widest names add a line and speaker overflow reaches the sheet report") {
    const auto base = "key,text\ndlg_x,hello\n";
    auto report =
        parse_csv(fit_budgets_report(base, "key,text\ndlg_x,aaaaaaaaaaaaaa {name:0} bbbbbbbbbbbb\n"));
    CHECK(report[1][7].find("widest names: 2 lines") != std::string::npos);
    report = parse_csv(fit_budgets_report(base, "key,text\ndlg_x," + std::string(30, 'W') + ": “hello”\n"));
    CHECK(report[1][7].find("speaker tab: ") != std::string::npos);
    CHECK(report[1][7].find("too wide by ") != std::string::npos);
}

TEST_CASE("text fit: a colon after an explicit break stays in the dialogue body") {
    const auto text = std::string(40, 'W') + "{n}Look: the gate is open.";
    const auto report =
        parse_csv(fit_budgets_report("key,text\ndlg_x,hello\n", "key,text\ndlg_x," + text + "\n"));
    CHECK(report[1][7].find("speaker tab") == std::string::npos);
    const auto issues = check_fit("key,text\ndlg_x,hello\n", "key,text\ndlg_x," + text + "\n");
    CHECK(std::none_of(issues.begin(), issues.end(),
                       [](const auto &i) { return i.surface == "dialogue speaker tab"; }));
    CHECK(has(issues, "dlg_x", "too wide by"));
}

TEST_CASE("text fit: battle room and sentence breaks agree with three-line runtime") {
    CHECK(fit_budget("msg_cannot_escape", "").width == 216);
    CHECK(fit_budget("msg_cannot_escape", "").lines == 3);
    const auto base = "key,text\nmsg_cannot_escape,にげられない\n";
    const auto report =
        parse_csv(fit_budgets_report(base, "key,text\nmsg_cannot_escape,Name{n}attacks!{n}Next!{n}Last!\n"));
    REQUIRE(report.size() >= 2);
    CHECK(report[1][7] == "mid-sentence break ignored");
}

TEST_CASE("text fit: narration colons retain the full body width and never create a speaker tab") {
    const auto report = parse_csv(fit_budgets_report(
        "key,text\ndlg_x,hello\n",
        "key,text\ndlg_x,We've got three kings in these parts: the Shield King and the Sword King.\n"));
    REQUIRE(report.size() == 2);
    CHECK(report[1][1] == "field dialogue");
    CHECK(report[1][2] == "214");
    CHECK(report[1][7].find("speaker tab") == std::string::npos);
}
