// Measures localized text against the host UI surfaces that display it.
#include "game/text_fit.h"
#include "game/gb_intro.h"
#include "game/text_tokens.h"

#include <algorithm>
#include <map>
#include <regex>
#include <unordered_map>

#include "game/player_names.h"
#include "game/text.h"
#include "game/text_render.h"
#include "game/text_wrap.h"
#include "game/ui_chrome.h"

namespace sawgod::game {
namespace {

struct Row {
    std::string key, text, kind;
};

std::vector<Row> rows(std::string_view csv) {
    std::vector<Row> out;
    const auto table = parse_csv(csv);
    if (table.empty())
        return out;
    int key = -1, text = -1, kind = -1;
    for (int i = 0; i < int(table[0].size()); ++i) {
        key = table[0][size_t(i)] == "key" ? i : key;
        text = table[0][size_t(i)] == "text" ? i : text;
        kind = table[0][size_t(i)] == "kind" ? i : kind;
    }
    if (key < 0 || text < 0)
        return out;
    for (size_t r = 1; r < table.size(); ++r) {
        const auto &row = table[r];
        if (int(row.size()) <= std::max(key, text) || row[size_t(key)].empty())
            continue;
        out.push_back({row[size_t(key)], row[size_t(text)],
                       kind >= 0 && kind < int(row.size()) ? row[size_t(kind)] : ""});
    }
    return out;
}

bool starts(std::string_view s, std::string_view prefix) { return s.substr(0, prefix.size()) == prefix; }

// A key of a numbered table: the prefix, then digits only (item_12, not item_obtained_suffix).
bool numbered(std::string_view s, std::string_view prefix) {
    return starts(s, prefix) && s.size() > prefix.size() &&
           s.find_first_not_of("0123456789", prefix.size()) == std::string_view::npos;
}

int service_notice_lines(std::string_view key) {
    if (numbered(key, "menu_error_"))
        return 2;
    if (key == "confirm_shop_purchase_1" || key == "buy_and_equip_shop_item_1" ||
        key == "buy_and_equip_shop_item_2" || key == "buy_and_equip_shop_item_3" ||
        key == "buy_and_equip_shop_item_5" || key == "buy_shop_item_into_inventory_1" ||
        key == "buy_shop_item_into_inventory_2")
        return 3;
    return 0;
}

std::string native_fit_text(std::string_view text) {
    const auto chars = utf8_to_u32(text);
    static const auto charset = Charset::system();
    std::u32string out;
    for (size_t i = 0; i < chars.size(); ++i) {
        const auto c = chars[i];
        if (c >= U'！' && c <= U'～') {
            std::vector<uint8_t> codes;
            if (charset.encode(name_utf8(std::u32string(1, c)), codes, nullptr) && codes.size() == 1) {
                out += char32_t(0xE000 + codes[0]);
                continue;
            }
        }
        out += c;
    }
    return name_utf8(out);
}
int width(std::string_view utf8) {
    const auto text = utf8_to_u32(utf8);
    const int w = Chrome::text_width(text);
    // Text drawn only in WSC font cells is 8 px per cell: each cell carries its own shadow, so the
    // m5x7 shadow column text_width adds does not apply.
    const auto cells = with_wsc_glyphs(text);
    const bool wsc_only = !cells.empty() && cells.find(U'{') == cells.npos &&
                          std::all_of(cells.begin(), cells.end(),
                                      [](char32_t c) { return (c >= 0xE000 && c < 0xE100) || c == U'　'; });
    return wsc_only ? w - 2 : w;
}

// The codes in braces, in order; `breaks` keeps the line breaks ({n}, {lf}) too.
std::vector<std::string> codes(const std::string &s, bool breaks) {
    static const std::regex code(R"(\{[^}]*\})");
    std::vector<std::string> out;
    for (auto it = std::sregex_iterator(s.begin(), s.end(), code); it != std::sregex_iterator(); ++it) {
        const std::string c = it->str();
        if (key_item_token(utf8_to_u32(c)) >= 0)
            continue;
        if (breaks || (c != "{n}" && c != "{lf}"))
            out.push_back(c);
    }
    return out;
}

std::vector<std::string> split(const std::string &s, const std::regex &on) {
    std::vector<std::string> out;
    std::sregex_token_iterator it(s.begin(), s.end(), on, -1), end;
    for (; it != end; ++it)
        out.push_back(*it);
    if (out.empty())
        out.push_back("");
    return out;
}

// The widest eight-glyph party name: eight of the widest letter.
std::string worst_party_name() {
    std::string best;
    int most = -1;
    for (char c : std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz")) {
        const std::string s(kPlayerNameGlyphs, c);
        if (const int w = width(s); w > most) {
            most = w;
            best = s;
        }
    }
    return best;
}

} // namespace

// These are the same declared widths as Chrome draws. Native-only layouts are warnings.
std::vector<FitBudget> fit_surfaces(std::string_view key, std::string_view base_text) {
    if (starts(key, "ui_options_")) {
        const auto *entry = host_text_info(key);
        const std::string_view english = entry ? entry->english : "";
        const bool help =
            english.size() > 28 || english.find('{') != english.npos || english.ends_with(" options");
        const bool category = english == "Video" || english == "Audio" || english == "Keyboard" ||
                              english == "Gamepad" || english == "Gameplay" || english == "Cheats" ||
                              english == "System" || english == "Debug";
        return {{help       ? "Options help"
                 : category ? "Options category"
                            : "Options label/choice",
                 help       ? 368
                 : category ? 83
                            : 187}};
    }
    // Check the composed data row, including the widest translated companion fields. Checking
    // each short label alone would miss an overflowing combination in the translation sheet.
    if (starts(key, "ui_battle_preview_element_"))
        return {{"battle action preview data", 189, 1, true, false, "",
                 "{preview_separator}{preview_range}{preview_separator}{preview_target}"}};
    if (starts(key, "ui_battle_preview_range_"))
        return {{"battle action preview data", 189, 1, true, false, "{preview_element}{preview_separator}",
                 "{preview_separator}{preview_target}"}};
    if (starts(key, "ui_battle_preview_target_"))
        return {{"battle action preview data", 189, 1, true, false,
                 "{preview_element}{preview_separator}{preview_range}{preview_separator}", ""}};
    if (key == "ui_battle_preview_separator")
        return {{"battle action preview data", 189, 1, true, false, "{preview_element}",
                 "{preview_range}{preview_separator}{preview_target}"}};
    if (key == "title_new_game" || key == "title_continue" || key == "title_original")
        return {{"title menu", 127, 1}};
    if (numbered(key, "world_card_"))
        return {{"GB world-entry page", GbIntro::kCaptionWidth, 1}};
    if (key == "intro_copyright")
        return {{"GB opening splash", 160, 1}};
    if (numbered(key, "intro_caption_"))
        return {{"GB opening caption", GbIntro::caption_width(std::stoi(std::string(key.substr(14)))), 8}};
    if (key == "intro_prologue")
        return {{"GB prologue page", GbIntro::kPrologueWidth, 10}};
    if (key == "ui_ending_notice")
        return {{"ending notice", 192, 7}};
    if (key == "ui_battle_meat_prompt")
        return {{"meat eater prompt", 212, 2}};
    if (key == "ui_battle_esper_change")
        return {{"Esper replacement proposal", 212, 8}};
    if (starts(key, "msg:") || text_message(key))
        return {{"battle log", 216, 3}, {"native battle popup", 200, 1, false}};
    if (numbered(key, "itemdesc_"))
        return {{"inventory detail", 212, 1, false, true},   {"party detail", 212, 1, false, true},
                {"creation detail", 212, 1, false, true},    {"shop detail", 212, 1, false, true},
                {"item dialog detail", 212, 1, false, true}, {"Bestiary ability flavor", 152, 1, false, true},
                {"native item description", 208, 1, false}};
    if (numbered(key, "item_"))
        return {{"inventory item list", 160},
                {"item dialog list", 160},
                {"battle item list", 160},
                {"battle action preview item", 160},
                {"party gear/abilities", 121},
                {"equipment transfer", 113},
                {"equipment key list", 124},
                {"creation gear (three-digit uses)", 108},
                {"shop stock/sale list", 104},
                {"shop confirmation item", 144},
                {"item target/travel", 202},
                {"shop equip-member item title", 204},
                {"Bestiary abilities (two-digit uses, including icon cell)", 117},
                {"native item lists (name before count)", 72, 1, false}};
    if (numbered(key, "monster_"))
        return {{"creation type list", 74},
                {"creation selected type", 74},
                {"party member type", 74},
                {"Bestiary member type", 102},
                {"Bestiary history", 163},
                {"meat preview type", 73},
                {"monster plate", 211, 1, true, false, "", "{group}"},
                // 189 px row minus 14 px arrow/gaps; the party name is expanded by the checker.
                {"battle action preview target", 175, 1, true, false, "{name:0}", "{group}"},
                {"Bestiary catalog/detail name", 87},
                {"native party type", 64, 1, false}};
    if (starts(key, "dlg_"))
        return {{"field dialogue", 214, 3}, {"dialogue speaker tab", 204, 1}};
    if (const int lines = service_notice_lines(key))
        return {{"service notice", 154, lines}};

    std::vector<FitBudget> out;
    // Host strings' budgets come from the surfaces below, not from text.h.
    struct HostSurface {
        const char *key, *surface;
        int width;
    };
    static constexpr HostSurface host[] = {
        {"ui_dialogue_item_obtained", "field acquired-item message", 214},
        {"ui_dialogue_inventory_full", "field item refused: inventory full", 214},
        {"ui_inventory_passive", "passive ability stats", 212},
        {"ui_save_unreadable", "protected save slot refusal", 212},
        {"ui_service_buy", "shop command", 42},
        {"ui_service_sell", "shop command", 42},
        {"ui_service_exit", "shop command", 42},
        {"ui_service_inn_rate", "inn rate", 52},
        {"ui_service_revive_price", "revival price", 50},
        {"ui_service_revive_question", "revival question", 154},
        {"ui_service_sale_offer", "native calculated sale offer", 154},
        {"ui_party_items", "field menu Items category", 48},
        {"ui_party_menu", "field menu title", 70},
        {"ui_party_time", "field menu elapsed time label", 40},
        {"ui_party_status", "field menu Status category", 48},
        {"ui_party_equip", "field menu Equip category", 48},
        {"ui_party_order", "field menu party order category", 48},
        {"ui_party_bestiary", "field menu Bestiary category", 48},
        {"ui_party_config", "field menu native Config category when subscribed", 48},
        {"ui_party_load", "field menu Load category", 48},
        {"ui_party_options", "field menu Options category", 48},
        {"ui_party_save", "field menu Save category", 48},
        {"ui_party_current_floor", "field/save menu current floor", 105},
        {"ui_party_highest_floor", "field/save menu highest floor", 114},
        {"ui_party_kero", "shared gold pane currency", 50},
        // after "999/", right-aligned at 159; a long name is cut short 4 px before it (party_ui.cpp)
        {"ui_status_dead", "member panel: condition in place of max HP", 40},
        {"ui_status_stone", "member panel: condition in place of max HP", 40},
        {"ui_status_curse", "member panel: condition in place of max HP", 40},
        {"ui_status_blind", "member panel: condition in place of max HP", 40},
        {"ui_party_str", "party stats strength label; also the meat preview", 34},
        {"ui_party_def", "party stats defense label; also the meat preview", 34},
        {"ui_party_agi", "party stats agility label; also the meat preview", 34},
        {"ui_party_mag", "party stats magic label; also the meat preview", 34},
        {"ui_party_order_source", "party order: choosing the member to move", 212},
        {"ui_party_order_target", "party order: choosing where it goes", 212},
        {"ui_party_picked_hint", "Status: key hint on the bottom line while a row is picked", 212},
        {"ui_party_gear", "Status/Equip gear heading", 50},
        {"ui_party_abilities", "Status/Equip fixed abilities heading", 50},
        {"ui_inventory_items", "inventory: ordinary-items tab", 65},
        {"ui_inventory_keys", "inventory: key-items tab", 63},
        {"ui_inventory_crystals", "inventory/equipment: Crystal row", 130},
        {"ui_inventory_trash", "inventory: native discard destination", 160},
        {"ui_inventory_crystal_hint", "inventory: Crystal action description", 212},
        {"ui_inventory_trash_hint", "inventory: Trash destination description", 212},
        {"ui_inventory_key_item", "inventory: key-item classification", 212},
        {"ui_inventory_field_item", "inventory: field-only item classification", 212},
        {"ui_inventory_status", "inventory: status-effect item classification", 212},
        {"ui_inventory_guard", "inventory: defense stance item classification", 212},
        {"ui_inventory_healing", "inventory: base potion healing before random spread", 212},
        {"ui_inventory_str", "inventory: strength scaling stat", 48},
        {"ui_inventory_agl", "inventory: agility scaling stat", 48},
        {"ui_inventory_mana", "inventory: magic scaling stat", 48},
        {"ui_inventory_power", "inventory: item's numeric effect power (not final damage)", 212},
        {"ui_inventory_defense", "inventory: gear defense", 212},
        {"ui_orb_pair_missing", "item use: one Orb used without its partner", 212},
        {"ui_item_target", "item use: title over the party target rows", 212},
        {"ui_item_refill_hint", "item refill: hint below the item list", 212},
        {"ui_item_floor", "item travel: the tower floor being chosen", 68},
        {"ui_battle_meat", "meat reward: the dropping monster's meat", 130},
        {"ui_battle_meat_eat", "meat reward: accept", 42},
        {"ui_battle_meat_dont", "meat reward: decline", 42},
        {"ui_battle_esper_allow", "Esper replacement: question", 110},
        {"ui_battle_esper_yes", "Esper replacement: accept", 31},
        {"ui_battle_esper_no", "Esper replacement: decline", 31},
        {"ui_battle_esper_kept", "Esper replacement: declined", 216},
        {"ui_battle_meat_refused", "meat reward: the chosen member can't eat", 212},
        {"ui_battle_hp", "meat preview: hit points label", 34},
        {"ui_battle_fight", "battle command menu, first row", 170},
        {"ui_battle_run", "battle command menu, second row", 170},
        {"ui_battle_empty_slot", "battle item list: an empty equipment slot", 160},
        {"ui_battle_no_uses", "battle item list: uses of an item that never runs out", 24},
        {"ui_battle_group_count", "monster plate: how many in a group of more than one", 24},
        {"ui_battle_empty_member", "battle party card: no member in this slot", 48},
        {"ui_save_overwrite", "occupied save confirmation", 212},
        {"ui_save_inherit_first", "New Game+ confirmation, first line", 212},
        {"ui_save_inherit_second", "New Game+ confirmation, second line", 134},
        {"ui_save_autosave", "save/load list: the autosave slot's label", 28},
        {"ui_name_entry_label", "name entry: label left of the name being typed", 32},
        {"ui_name_entry_space", "name entry: adds a space", 56},
        {"ui_name_entry_delete", "name entry: removes the last glyph", 56},
        {"ui_name_entry_done", "name entry: accepts the name", 50},
        {"ui_name_entry_hiragana", "name entry (Japanese only): page title, {page} = 1/2", 180},
        {"ui_name_entry_katakana", "name entry (Japanese only): page title, {page} = 1/2", 180},
        {"ui_creation_hp", "character creation: hit points label", 20},
        {"ui_bestiary_history", "Bestiary second category", 59},
        {"ui_bestiary_abilities", "Bestiary detail heading", 64},
        {"ui_bestiary_unseen", "Bestiary unseen monster", 87},
        {"ui_bestiary_back", "Bestiary back hint", 50},
        {"ui_bestiary_hint", "Bestiary seen catalog hint", 87},
        {"ui_bestiary_family", "Bestiary family label", 87},
        {"ui_bestiary_family", "Bestiary detail family label", 32},
        {"ui_bestiary_hp", "Bestiary HP label", 34},
        {"ui_bestiary_title", "Bestiary screen title", 76},
        {"ui_bestiary_generation",
         "Bestiary: the member's current form generation, {generation} = number up to 9999", 100},
    };
    const bool detail = starts(key, "ui_inventory_") && key != "ui_inventory_items" &&
                        key != "ui_inventory_keys" && key != "ui_inventory_crystals" &&
                        key != "ui_inventory_trash";
    if (detail) {
        for (const char *surface :
             {"inventory detail", "party detail", "creation detail", "shop detail", "item dialog detail"})
            out.push_back({surface, 212, 1, false, true});
    } else {
        for (const auto &h : host)
            if (key == h.key) {
                out.push_back({h.surface, h.width});
                break;
            }
    }
    const auto add = [&](const char *surface, int width, const char *prefix = "", const char *suffix = "") {
        out.push_back({surface, width, 1, true, false, prefix, suffix});
    };
    if (key == "ui_inventory_items") {
        add("field menu Items category", 48);
        add("party inventory tab", 82);
        add("item dialog gear/inventory tab", 100);
    } else if (key == "ui_inventory_keys") {
        add("party key-items tab", 82);
        add("item dialog gear/key tab", 100);
    } else if (key == "ui_inventory_crystals") {
        add("inventory Crystal row", 160);
        add("item dialog Crystal row", 160);
        add("equipment Crystal row", 130);
    } else if (key == "ui_inventory_trash") {
        add("item dialog Trash row", 160);
    } else if (key == "ui_party_gear") {
        add("party Gear tab", 62);
        add("item dialog Gear tab", 100);
    } else if (key == "ui_party_abilities") {
        add("party Abilities tab", 82);
    } else if (key == "ui_party_order") {
        add("party order title", 212);
    } else if (key == "ui_party_current_floor" || key == "ui_party_highest_floor") {
        add("field menu floor", 154);
    } else if (key == "ui_party_kero") {
        out.clear();
        add("inn currency line", 50);
        add("party currency line", 50);
        // Save right-alignment: 35 px time ends at 151, gold ends at 218.
        add("save gold and currency", 67, "{gold} ");
    } else if (key == "ui_battle_group_count") {
        out.clear();
        add("monster plate count suffix", 211, "{monster}");
        add("battle action preview count suffix", 175, "{name:0}{monster}");
    } else if (key == "ui_party_time") {
        out.clear();
        add("field menu time label", 143, "", " 99:59"); // title ends at 75, right edge 218
    } else if (key == "ui_party_str" || key == "ui_party_def" || key == "ui_party_agi" ||
               key == "ui_party_mag") {
        add("party stats label", 38);
        add("creation stats label", 38);
    } else if (key == "ui_battle_empty_slot") {
        add("party empty gear row", 123);
        add("equipment empty destination", 119);
        add("creation empty gear row", 108);
        add("shop empty sale row", 104);
    } else if (key == "ui_battle_empty_member") {
        add("party empty member row", 130);
    } else if (key == "ui_name_entry_label") {
        out.clear();
        add("name entry label", 68);
    }

    struct NativeUse {
        const char *key, *surface;
        int width;
    };
    static constexpr NativeUse redesigned[] = {
        {"draw_creation_type_screen_1", "creation title", 212},
        {"run_monster_history_menu_1", "Bestiary no-monsters notice", 212},
        {"run_monster_history_menu_2", "Bestiary no-monsters notice", 212},
        {"draw_save_menu_1", "save title", 214},
        {"draw_load_menu_screen_1", "load title", 214},
        {"draw_save_slot_summary_3", "empty save slot", 150},
        {"draw_save_confirmation_1", "save/load Yes", 30},
        {"draw_save_confirmation_2", "save/load No", 29},
        {"draw_save_confirmation_4", "empty-slot save question", 212},
        {"draw_load_slot_prompt_5", "load question", 212},
        {"wait_save_confirmation_1", "save completed notice", 212},
        {"wait_save_confirmation_2", "save completed notice", 212},
        {"draw_load_slot_prompt_1", "invalid load notice", 212},
        {"draw_load_slot_prompt_2", "invalid load notice", 212},
        {"draw_inn_screen_1", "inn title", 214},
        {"draw_inn_screen_2", "inn question", 154},
        {"draw_inn_screen_4", "inn Yes", 65},
        {"draw_inn_screen_5", "inn No", 65},
        {"draw_recruitment_member_screen_1", "Guild title", 214},
        {"draw_recruitment_member_screen_2", "Guild question", 154},
        {"draw_revive_member_screen_1", "revival title", 214},
        {"draw_shop_equipment_member_screen_1", "shop equip-member question", 154},
        {"confirm_member_replacement_1", "Guild replacement question", 154},
        {"replace_selected_party_member_1", "Guild refusal", 154},
        {"draw_member_replacement_confirmation_1", "Guild Yes", 65},
        {"draw_member_replacement_confirmation_2", "Guild No", 65},
        {"draw_shop_main_screen_2", "shop title", 214},
        {"draw_shop_main_screen_3", "shop greeting", 214},
        {"draw_shop_purchase_confirmation_1", "shop purchase question", 154},
        {"draw_shop_purchase_confirmation_2", "shop purchase Yes", 65},
        {"draw_shop_purchase_confirmation_3", "shop purchase No", 65},
        {"draw_shop_sale_confirmation_2", "shop sale Yes", 65},
        {"draw_shop_sale_confirmation_3", "shop sale No", 65},
        {"draw_transformation_menu_state_3", "meat Eat choice", 65},
        {"draw_transformation_menu_state_4", "meat Don't choice", 65},
        {"wait_unusable_item_message_1", "item refusal", 212},
        {"wait_equipment_transfer_error_1", "equipment refusal", 212},
        {"discard_overflow_inventory_item_1", "overflow prompt", 212},
        {"discard_overflow_inventory_item_2", "overflow prompt", 212},
        {"append_party_swap_label_sprites_1", "party order swap message", 212},
    };
    for (const auto &s : redesigned)
        if (key == s.key)
            add(s.surface, s.width);
    if (key == "ui_party_str" || key == "ui_party_def" || key == "ui_party_agi" || key == "ui_party_mag")
        add("Bestiary stat label", 34);
    if (numbered(key, "ui_bestiary_family_")) {
        add("Bestiary catalog family", 87);
        add("Bestiary detail family after label", 70);
    }
    if (!host_text_info(key)) {
        // Unused originals and untranslated native-only labels: warning, never a sheet hard limit.
        const auto base =
            utf8_to_u32(std::regex_replace(std::string(base_text), std::regex(R"(\[[0-9A-Fa-f]{2}\])"), "#"));
        out.push_back({"menu (original layout)", int(base.size()) * 8, 1, false});
        // Remaining Bestiary catalog/detail windows, from bank34_misc.cpp frames and draw columns.
        if (numbered(key, "creation_race_label_")) {
            out.push_back({"native Bestiary race list", 120, 1, false});  // column 12 to frame at 27
            out.push_back({"native Bestiary detail race", 64, 1, false}); // column 20 to screen edge 28
        }
        if (starts(key, "draw_race_selection_screen_")) {
            const int n = key.back() - '0';
            out.push_back({"native Bestiary catalog label", n == 1 ? 56 : n == 2 ? 104 : 48, 1, false});
        }
        if (starts(key, "draw_creation_type_detail_screen_")) {
            const int n = std::stoi(
                std::string(key.substr(std::string_view("draw_creation_type_detail_screen_").size())));
            out.push_back({"native Bestiary detail label",
                           n == 1       ? 56
                           : n == 2     ? 104
                           : n == 3     ? 120
                           : n == 4     ? 64
                           : n == 5     ? 16
                           : n % 2 == 0 ? 32
                                        : 8,
                           1, false});
        }
    }
    return out;
}

FitBudget fit_budget(std::string_view key, std::string_view base_text) {
    const auto surfaces = fit_surfaces(key, base_text);
    FitBudget result;
    // A scrolling key has no hard limit; otherwise keep the old warning-only summary available to callers.
    for (const auto &b : surfaces) {
        if (b.surface == "dialogue speaker tab")
            continue;
        if (b.scrolls) {
            if (result.surface.empty())
                result = b;
            continue;
        }
        if (result.surface.empty() || (b.hard && (!result.hard || result.scrolls)) ||
            (b.hard == result.hard && !result.scrolls && b.width < result.width))
            result = b;
    }
    if (std::any_of(surfaces.begin(), surfaces.end(), [](const auto &b) { return b.scrolls; }) &&
        !result.hard)
        for (const auto &b : surfaces)
            if (b.scrolls)
                return b;
    return result;
}

namespace {
// Frozen after measuring English strings with fit_sample_median.
constexpr double kAverageEnglishWidth = 217.0 / 39.0;

struct Expansion {
    std::unordered_map<std::string, std::string> text;
    std::map<std::string, std::string> buffers;
    std::string party = worst_party_name(), monster, item;
    bool japanese = false;

    Expansion(const std::vector<Row> &base, const std::vector<Row> &lang, bool ja = false) : japanese(ja) {
        if (japanese)
            party = native_fit_text("ＷＷＷＷＷＷＷＷ");
        for (const auto &r : base)
            text[r.key] = japanese && !host_text_info(r.key) ? native_fit_text(r.text) : r.text;
        // Actual host English fallbacks, even when the supplied file omits a host key.
        for (const auto &h : kHostText)
            text[h.key] = japanese ? h.japanese : h.english;
        for (const auto &r : lang)
            text[r.key] = japanese && !host_text_info(r.key) ? native_fit_text(r.text) : r.text;
        const auto longest = [&](std::string_view prefix) {
            std::string best;
            for (const auto &[key, value] : text)
                if (numbered(key, prefix) && width(value) > width(best))
                    best = value;
            return best;
        };
        monster = longest("monster_");
        item = longest("item_");
        const auto actor = width(party) >= width(monster) ? party : monster;
        buffers = {{"R:D60F", actor},
                   {"R:D62F", actor},
                   {"R:D64F", item},
                   {"R:D66F", japanese ? native_fit_text("９９９９") : "9999"},
                   {"R:DC9B", japanese ? native_fit_text("９９９９９９") : "999999"}};
    }

    Expansion typical() const {
        auto e = *this;
        const auto median = [&](std::string_view prefix) {
            std::vector<std::string> values;
            for (const auto &[key, value] : text)
                if (numbered(key, prefix) && !value.empty())
                    values.push_back(value);
            std::sort(values.begin(), values.end(), [](const auto &a, const auto &b) {
                return width(a) == width(b) ? a < b : width(a) < width(b);
            });
            return values.empty() ? std::string() : values[values.size() / 2];
        };
        // The normal four-glyph name-entry party (AAAA/BBBB/CCCC/DDDD). The widest
        // allowed eight-glyph player name remains the separate worst-case warning.
        e.party = japanese ? native_fit_text("ＡＡＡＡ") : "AAAA";
        e.monster = median("monster_");
        e.item = median("item_");
        const auto actor = width(e.party) >= width(e.monster) ? e.party : e.monster;
        e.buffers["R:D60F"] = e.buffers["R:D62F"] = actor;
        e.buffers["R:D64F"] = e.item;
        return e;
    }
    std::string expand(std::string_view key, std::string value, bool protect = false) const {
        std::vector<std::string> args;
        if (const auto *msg = text_message(key); msg || starts(key, "msg:")) {
            const auto parts = msg ? std::string(msg->parts) : std::string(key.substr(4));
            for (const auto &p : split(parts, std::regex(R"(\+)")))
                if (starts(p, "R:")) {
                    const auto it = buffers.find(p);
                    args.push_back(it == buffers.end() ? "99999" : it->second);
                }
        } else if (key == "ui_dialogue_item_obtained")
            args = {name_utf8(Chrome::item_run(-1, utf8_to_u32(item)))};
        else if (key == "ui_battle_meat" || key == "ui_battle_meat_prompt")
            args = {monster};
        else if (key == "ui_service_sale_offer")
            args = {"999999"};
        else if (key == "ui_party_current_floor" || key == "ui_party_highest_floor")
            args = {"23F"};
        else if (key == "ui_item_floor")
            args = {"23"};
        else if (key == "ui_bestiary_generation")
            args = {"9999"};
        else if (key == "ui_battle_group_count")
            args = {"9"};
        else if (key == "ui_name_entry_hiragana" || key == "ui_name_entry_katakana")
            args = {"2/2"};
        else if (starts(key, "ui_inventory_"))
            args = {"255"};
        const auto replace = [&](const std::string &token, const std::string &with) {
            size_t at = 0;
            while ((at = value.find(token, at)) != std::string::npos) {
                const auto arg = protect && japanese && !with.empty()
                                     ? name_utf8(std::u32string(1, kWrapArgumentStart)) + with +
                                           name_utf8(std::u32string(1, kWrapArgumentEnd))
                                     : with;
                value.replace(at, token.size(), arg);
                at += arg.size();
            }
        };
        for (size_t i = 0; i < args.size(); ++i)
            replace("{" + std::to_string(i + 1) + "}", args[i]);
        if (key == "ui_battle_esper_change") {
            replace("{actor}", party);
            replace("{ability}", item);
            replace("{new_ability}", item);
        }
        if (const std::string_view param = host_param(key); !param.empty() && !args.empty())
            replace("{" + std::string(param) + "}", args[0]);
        if (const auto *msg = text_message(key)) { // named parts ({actor}, {amount}...: text_tokens.h)
            const auto names = message_tokens(*msg);
            for (size_t i = 0; i < args.size() && i < names.size(); ++i)
                replace("{" + names[i] + "}", args[i]);
        }
        value = name_utf8(expand_key_items(utf8_to_u32(value), [&](int id) {
            const auto it = text.find("item_" + std::to_string(id));
            return it == text.end() ? std::u32string() : utf8_to_u32(it->second);
        }));
        // the menus' value tokens at their widest, layout tokens without width of their own
        replace("{hp}", "999");
        replace("{maxhp}", "999");
        replace("{hearts}", "99");
        replace("{floor}", "99");
        replace("{topfloor}", "99");
        replace("{value}", "99");
        replace("{amount}", "99");
        replace("{item}", item);
        replace("{name}", party);
        replace("{generation}", "9999");
        replace("{cursor}", "");
        replace("{tab}", "");
        value = std::regex_replace(value, std::regex(R"(\{name:[0-9]+\})"), party);
        replace("{inn_price}", "3992"); // four living 1/999 HP members; inn charges per missing HP
        replace("{gold}", "999999");
        replace("{monster}", monster);
        const auto widest = [&](std::string_view prefix) {
            std::string best;
            for (const auto &[k, v] : text)
                if (starts(k, prefix) && width(v) > width(best))
                    best = v;
            return best;
        };
        if (value.find("{preview_") != std::string::npos) {
            replace("{preview_element}", widest("ui_battle_preview_element_"));
            replace("{preview_range}", widest("ui_battle_preview_range_"));
            replace("{preview_target}", widest("ui_battle_preview_target_"));
            replace("{preview_separator}", text.at("ui_battle_preview_separator"));
        }
        const auto group = text.find("ui_battle_group_count");
        replace("{group}", expand_group(group == text.end() ? " x{count}" : group->second));
        // Native base strings use fixed-cell WSC Latin/digits too; host strings keep Latin proportional.
        if (japanese && !host_text_info(key))
            value = native_fit_text(value);
        // Dialogue pacing contributes no pixels; unknown parameters stay visible for diagnostics.
        return std::regex_replace(value, std::regex(R"(\{wait\}|\{pause:[0-9]+\})"), "");
    }
    static std::string expand_group(std::string value) {
        return std::regex_replace(value, std::regex(R"(\{count\})"), "9");
    }
};

// Only the first line in a box can supply the speaker prefix. Ignore colons inside tokens.
size_t speaker_separator(const std::string &line) {
    bool token = false;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line.compare(i, 3, "{n}") == 0 || line.compare(i, 4, "{lf}") == 0)
            return std::string::npos;
        if (line[i] == '{')
            token = true;
        if (!token && line.compare(i, 3, "「") == 0)
            return i;
        if (!token && line[i] == ':') {
            const auto quote = line.find_first_not_of(' ', i + 1);
            if (quote != std::string::npos && (line[quote] == '"' || line.compare(quote, 3, "“") == 0))
                return i;
        }
        if (line[i] == '}')
            token = false;
    }
    return std::string::npos;
}
std::string surface_line(std::string_view key, const FitBudget &b, const std::string &line, bool first) {
    if (!starts(key, "dlg_"))
        return line;
    const size_t at = first ? speaker_separator(line) : std::string::npos;
    if (b.surface == "dialogue speaker tab")
        return at == std::string::npos ? "" : line.substr(0, at);
    if (at != std::string::npos)
        return line.substr(at + (line[at] == ':' ? 1 : 3));
    return line;
}

// Max lines in one box; waits stay inline, explicit clears begin a fresh box.
WrapResult wrapped(const Row &r, const Expansion &e) {
    WrapResult result;
    for (const auto &box : split(r.text, std::regex(R"(\{box\}|\{clear\})"))) {
        auto text = starts(r.key, "dlg_") ? surface_line(r.key, {"field dialogue", 214, 3}, box, true) : box;
        auto part = wrap_text(utf8_to_u32(e.expand(r.key, text, true)), starts(r.key, "dlg_") ? 214 : 216,
                              !starts(r.key, "dlg_"), e.japanese);
        if (part.lines.size() > result.lines.size())
            result.lines = part.lines;
        result.too_wide.insert(result.too_wide.end(), part.too_wide.begin(), part.too_wide.end());
        result.cell_break |= part.cell_break;
        result.ignored_break |= part.ignored_break;
    }
    return result;
}
bool wraps(std::string_view key) { return starts(key, "dlg_") || starts(key, "msg:") || text_message(key); }
std::string wrap_summary(const Row &r, const Expansion &e) {
    const auto typical = e.typical();
    const auto result = wrapped(r, typical), worst = wrapped(r, e);
    std::vector<std::string> notes;
    const size_t capacity = e.japanese && starts(r.key, "dlg_") ? 4 : 3;
    if (result.lines.size() > capacity)
        notes.push_back(std::to_string(result.lines.size()) + " lines, scrolls");
    for (const auto &word : result.too_wide)
        notes.push_back("\"" + name_utf8(word.text) + "\" too wide by " + std::to_string(word.excess) +
                        " px");
    if (starts(r.key, "dlg_"))
        for (const auto &box : split(r.text, std::regex(R"(\{box\}|\{clear\})"))) {
            const auto prefix = surface_line(r.key, {"dialogue speaker tab", 204, 1}, box, true);
            const auto speaker = typical.expand(r.key, prefix);
            const int excess = width(speaker) - 204;
            if (excess > 0)
                notes.push_back("speaker tab: \"" + speaker + "\" too wide by " + std::to_string(excess) +
                                " px");
            else if (width(e.expand(r.key, prefix)) > 204)
                notes.push_back("widest names: speaker exceeds tab width");
        }
    if (result.cell_break)
        notes.push_back("cell line break ignored");
    if (result.ignored_break)
        notes.push_back("mid-sentence break ignored");
    if (worst.lines.size() > result.lines.size())
        notes.push_back("widest names: " + std::to_string(worst.lines.size()) + " lines" +
                        (worst.lines.size() > capacity ? ", scrolls" : ""));
    if (!worst.too_wide.empty() && worst.too_wide.size() > result.too_wide.size())
        notes.push_back("widest names: word exceeds line width");
    std::string out;
    for (const auto &note : notes) {
        if (!out.empty())
            out += "; ";
        out += note;
    }
    return out.empty() ? "ok" : out;
}

int surface_extra(const Expansion &e, const FitBudget &b) {
    // Creation's uses column is dynamic: three compact digits take 14 px; a translated infinity
    // marker can take more. Reserve the resulting reduction in the name column too.
    if (b.surface == "creation gear (three-digit uses)" || b.surface == "creation empty gear row")
        return std::max(0, width(e.text.at("ui_battle_no_uses")) - Chrome::numerals_width("253"));
    return 0;
}

std::string single_line_summary(const Row &r, const Expansion &e, const FitBudget &b) {
    auto text = e.expand(r.key, b.prefix + r.text + b.suffix);
    const bool cell_break = text.find_first_of("\r\n") != std::string::npos;
    text = std::regex_replace(text, std::regex("\r\n|\r|\n"), " ");
    const int excess = width(text) + surface_extra(e, b) - b.width;
    std::string report =
        (!b.scrolls && excess > 0) ? "\"" + text + "\" too wide by " + std::to_string(excess) + " px" : "ok";
    if (cell_break)
        report = report == "ok" ? "cell line break ignored" : report + "; cell line break ignored";
    return report;
}

int reservation(const Expansion &e, const Row &r, const FitBudget &b) {
    int reserved = 0;
    // Use the largest argument load on any line, per box. Do not count pacing/break tokens.
    for (const auto &box : split(r.text, std::regex(R"(\{box\}|\{clear\})"))) {
        bool first = true;
        for (const auto &line : split(box, std::regex(R"(\{n\}|\{lf\}|\n)"))) {
            const auto projected = surface_line(r.key, b, line, first);
            first = false;
            const auto literal = std::regex_replace(projected, std::regex(R"(\{[^}]*\})"), "");
            reserved =
                std::max(reserved, width(e.expand(r.key, b.prefix + projected + b.suffix)) - width(literal));
        }
    }
    return reserved + surface_extra(e, b);
}
} // namespace

double fit_sample_median(std::string_view sample) {
    std::vector<double> measurements;
    for (const auto &r : rows(sample)) {
        auto text = std::regex_replace(r.text, std::regex(R"(\{[^}]*\})"), "");
        for (const auto &line : split(text, std::regex("\n"))) {
            const auto chars = utf8_to_u32(line);
            if (!chars.empty() &&
                std::all_of(chars.begin(), chars.end(), [](char32_t c) { return c >= 32 && c < 127; }))
                measurements.push_back(double(width(line)) / double(chars.size()));
        }
    }
    if (measurements.empty())
        return 0;
    std::sort(measurements.begin(), measurements.end());
    const size_t mid = measurements.size() / 2;
    return measurements.size() % 2 ? measurements[mid] : (measurements[mid - 1] + measurements[mid]) / 2;
}

std::string fit_budgets_report(std::string_view base_csv, std::string_view sample_csv, bool all_surfaces,
                               bool japanese) {
    const auto base = rows(base_csv), sample = rows(sample_csv);
    const Expansion e(base, sample, japanese);
    std::unordered_map<std::string, std::string> selected;
    for (const auto &r : sample)
        selected[r.key] = r.text;
    std::string out = "key,surface,width,lines,hard,scrolls,reserved,fits,average_px\n";
    for (const auto &r : base) {
        const auto surfaces = fit_surfaces(r.key, r.text);
        Row templ = r;
        if (const auto it = selected.find(r.key); it != selected.end())
            templ.text = it->second;
        else if (const auto *h = host_text_info(r.key))
            templ.text = japanese ? h->japanese : h->english;
        int available = -1;
        FitBudget tightest = fit_budget(r.key, r.text);
        for (const auto &b : surfaces) {
            if (!b.hard || b.scrolls)
                continue;
            if (b.surface == "dialogue speaker tab" && speaker_separator(templ.text) == std::string::npos)
                continue;
            const int room = std::max(0, b.width - reservation(e, templ, b));
            if (available < 0 || room < available) {
                available = room;
                tightest = b;
            }
        }
        // Native-only keys still get an advisory number; a scrolling key has no limit.
        if (available < 0 && !tightest.scrolls)
            available = std::max(0, tightest.width - reservation(e, templ, tightest));
        const auto emit = [&](FitBudget b) {
            if (japanese && b.surface == "field dialogue")
                b.lines = 4;
            const int reserved = reservation(e, templ, b);
            std::string fits = wraps(r.key) ? wrap_summary(templ, e) : single_line_summary(templ, e, b);
            if (r.key == "ui_battle_meat_prompt" || r.key == "ui_battle_esper_change" ||
                service_notice_lines(r.key)) {
                const auto layout = wrap_text(utf8_to_u32(e.expand(r.key, templ.text)), b.width);
                fits = "wraps; " + std::to_string(layout.lines.size()) + " lines";
            }
            // A native key's Japanese is the ROM's own text: in an original-layout cell span it fits by
            // definition (the span and its argument reservation are calibrated for translations).
            if (japanese && !b.hard && !wraps(r.key) && !host_text_info(r.key))
                fits = "ok";
            if (r.key == "ui_ending_notice" && japanese) // lettered only for proportional languages
                fits = "ok (not shown in Japanese: the original screen stays)";
            else if (r.key == "ui_ending_notice") { // wrapped and centered (ending_notice_lettering.cpp)
                const auto layout = wrap_text(utf8_to_u32(templ.text), b.width, false, japanese);
                fits = !layout.too_wide.empty()
                           ? "\"" + name_utf8(layout.too_wide[0].text) + "\" too wide by " +
                                 std::to_string(layout.too_wide[0].excess) + " px"
                       : int(layout.lines.size()) > b.lines
                           ? std::to_string(layout.lines.size()) + " lines, too many"
                           : "ok";
            }
            if (starts(r.key, "intro_caption_") || r.key == "intro_prologue") {
                const auto text = utf8_to_u32(templ.text);
                const auto pages =
                    r.key == "intro_prologue" ? GbIntro::pages(text) : std::vector<std::u32string>{text};
                size_t lines = 0;
                for (const auto &page : pages)
                    lines = std::max(lines, wrap_text(page, b.width, false, japanese).lines.size());
                fits = "wraps; " + std::to_string(lines) + " lines per page";
            }
            out += csv_cell(r.key) + "," + csv_cell(b.surface) + "," + std::to_string(b.width) + "," +
                   std::to_string(b.lines) + "," + (b.hard ? "1" : "0") + "," + (b.scrolls ? "1" : "0") +
                   "," + std::to_string(reserved) + "," + csv_cell(fits) + "," +
                   std::to_string(kAverageEnglishWidth) + "\n";
        };
        if (all_surfaces)
            for (const auto &b : surfaces)
                emit(b);
        else
            emit(tightest);
    }
    return out;
}

std::vector<FitIssue> check_fit(std::string_view base_csv, std::string_view language_csv,
                                std::string_view context) {
    const std::vector<Row> base = rows(base_csv), lang = rows(language_csv);
    std::unordered_map<std::string, const Row *> base_by_key;
    for (const Row &r : base)
        base_by_key[r.key] = &r;
    const Expansion expansion(base, context.empty() ? lang : rows(merge_text(context, language_csv)));

    std::vector<FitIssue> issues;
    auto issue = [&](const Row &r, const std::string &surface, const std::string &problem, int w = 0,
                     int budget = 0, bool error = true) {
        issues.push_back({r.key, surface, problem, r.text, w, budget, error});
    };
    for (const Row &r : lang) {
        const auto b = base_by_key.find(r.key);
        if (b == base_by_key.end()) {
            issue(r, "", "unknown key (not in the base language)");
            continue;
        }
        const auto source = utf8_to_u32(r.text);
        for (size_t at = 0; (at = source.find(U"{key:", at)) != std::u32string::npos;) {
            size_t length = 0;
            std::u32string_view display;
            const int id = key_item_token(std::u32string_view(source).substr(at), &length, &display);
            if (id < 0)
                issue(r, "",
                      "invalid {key:item_N} / {key:item_N:Text}: canonical item id (0-255), nonempty Text "
                      "and closing } required");
            else if (display.empty() && !expansion.text.contains("item_" + std::to_string(id)))
                issue(r, "", "missing item_" + std::to_string(id) + " name for {key:item_N}");
            at += length ? length : 1;
        }
        const Row &jp = *b->second;
        const auto surfaces = fit_surfaces(r.key, jp.text);
        const FitBudget budget = fit_budget(r.key, jp.text);
        const auto check_width = [&](const std::string &line, const std::string &problem,
                                     bool first = false) {
            for (const auto &b : surfaces) {
                if (b.scrolls)
                    continue;
                const std::string expanded =
                    expansion.expand(r.key, b.prefix + surface_line(r.key, b, line, first) + b.suffix);
                const int w = width(expanded) + surface_extra(expansion, b);
                if (w > b.width)
                    issue(r, b.surface, problem + ": " + expanded, w, b.width, b.hard);
            }
        };
        // Characters the font can't draw (codes in braces aside).
        const auto literal = name_utf8(expand_key_items(source, [](int) { return std::u32string(); }));
        const std::u32string chars = utf8_to_u32(std::regex_replace(literal, std::regex(R"(\{[^}]*\})"), ""));
        std::u32string missing;
        for (char32_t c : chars)
            if (c != kItemNameEnd && !(c >= kInlineKeyItem && c < kInlineKeyItem + 256))
                if (!Chrome::can_draw(c) && std::find(missing.begin(), missing.end(), c) == missing.end())
                    missing += c;
        if (!missing.empty())
            issue(r, budget.surface, "not in the font: " + name_utf8(missing));

        if (const TextMessage *msg = text_message(r.key); msg || starts(r.key, "msg:")) {
            // {1}, {2}... stand for the signature's buffers in order; the same set as the base's.
            auto placeholders = [](const std::string &s) {
                auto c = codes(s, false);
                std::sort(c.begin(), c.end());
                return c;
            };
            if (placeholders(r.text) != placeholders(jp.text))
                issue(r, budget.surface, "tokens differ from the Japanese (" + jp.text + ")");
            const auto result = wrapped(r, expansion.typical());
            for (const auto &word : result.too_wide)
                issue(r, "battle log",
                      "\"" + name_utf8(word.text) + "\" too wide by " + std::to_string(word.excess) + " px",
                      216 + word.excess, 216);
            const auto report = wrap_summary(r, expansion);
            if (report.find("scrolls") != std::string::npos ||
                report.find("cell line break") != std::string::npos ||
                report.find("widest names") != std::string::npos)
                issue(r, "battle log", report, 0, 0, false);
        } else if (starts(r.key, "dlg_")) {
            // Pacing codes ({wait}, {box}, {clear}, {pause:N}) and line breaks are the translator's; the
            // party names it mentions must stay.
            auto names = [](const std::string &s) {
                std::vector<std::string> out;
                for (const std::string &c : codes(s, false))
                    if (starts(c, "{name:"))
                        out.push_back(c);
                std::sort(out.begin(), out.end());
                return out;
            };
            if (names(r.text) != names(jp.text))
                issue(r, budget.surface, "party names ({name:N}) differ from the Japanese");
            for (const auto &box : split(r.text, std::regex(R"(\{box\}|\{clear\})"))) {
                const auto speaker = surface_line(r.key, {"dialogue speaker tab", 204, 1}, box, true);
                if (const auto w = width(expansion.expand(r.key, speaker)); w > 204)
                    issue(r, "dialogue speaker tab", "speaker too wide", w, 204);
            }
            const auto result = wrapped(r, expansion.typical());
            for (const auto &word : result.too_wide)
                issue(r, "field dialogue",
                      "\"" + name_utf8(word.text) + "\" too wide by " + std::to_string(word.excess) + " px",
                      214 + word.excess, 214);
            const auto report = wrap_summary(r, expansion);
            if (report.find("scrolls") != std::string::npos ||
                report.find("cell line break") != std::string::npos ||
                report.find("widest names") != std::string::npos)
                issue(r, "field dialogue", report, 0, 0, false);
        } else if (starts(r.key, "intro_caption_") || r.key == "intro_prologue") {
            const auto text = utf8_to_u32(r.text);
            const auto pages =
                r.key == "intro_prologue" ? GbIntro::pages(text) : std::vector<std::u32string>{text};
            if (r.key == "intro_prologue" && text.find(U"{n}{n}") == text.npos)
                issue(r, budget.surface, "missing {n}{n} page boundary after the Paradise sentence");
            for (const auto &page : pages) {
                const auto layout =
                    GbIntro::layout(page, r.key == "intro_prologue",
                                    starts(r.key, "intro_caption_") ? std::stoi(r.key.substr(14)) : 0);
                for (const auto &word : layout.too_wide)
                    issue(r, budget.surface, "word too wide: " + name_utf8(word.text),
                          budget.width + word.excess, budget.width);
                if (int(layout.lines.size()) > budget.lines)
                    issue(r, budget.surface, "too many lines on one page");
            }
        } else if (r.key == "ui_ending_notice" || r.key == "ui_battle_meat_prompt" ||
                   r.key == "ui_battle_esper_change" || service_notice_lines(r.key)) {
            if ((host_text_info(r.key) || service_notice_lines(r.key)) &&
                codes(r.text, false) != codes(jp.text, false)) {
                auto translated = codes(r.text, false), original = codes(jp.text, false);
                std::sort(translated.begin(), translated.end());
                std::sort(original.begin(), original.end());
                if (translated != original)
                    issue(r, budget.surface, "tokens differ from the Japanese (" + jp.text + ")");
            }
            const auto layout = wrap_text(utf8_to_u32(expansion.expand(r.key, r.text)), budget.width);
            for (const auto &word : layout.too_wide)
                issue(r, budget.surface, "word too wide: " + name_utf8(word.text), budget.width + word.excess,
                      budget.width);
            if (int(layout.lines.size()) > budget.lines)
                issue(r, budget.surface,
                      "too many lines: " + std::to_string(layout.lines.size()) + " > " +
                          std::to_string(budget.lines));
        } else {
            auto placeholders = [](const std::string &s) {
                auto c = codes(s, false);
                std::sort(c.begin(), c.end());
                return c;
            };
            if (host_text_info(r.key)) {
                if (placeholders(r.text) != placeholders(jp.text))
                    issue(r, budget.surface, "tokens differ from the Japanese (" + jp.text + ")");
            } else if (!starts(r.key, "item_") && !starts(r.key, "monster_") && !starts(r.key, "itemdesc_")) {
                // a menu string: the tokens of its token form (text_tokens.h), each where it belongs
                const std::string form = text_tokens(r.key, jp.text).text;
                if (placeholders(r.text) != placeholders(form))
                    issue(r, budget.surface, "tokens differ from the Japanese (" + form + ")");
            }
            const auto line = std::regex_replace(r.text, std::regex("\r\n|\r|\n"), " ");
            check_width(line, budget.hard ? "too wide" : "wider than the original layout");
        }
    }
    return issues;
}

std::string fit_report(const std::vector<FitIssue> &issues) {
    std::string out = "key,severity,surface,width,budget,problem,text\n";
    for (const FitIssue &i : issues)
        out += csv_cell(i.key) + "," + (i.error ? "error" : "warning") + "," + csv_cell(i.surface) + "," +
               (i.width ? std::to_string(i.width) : "") + "," + (i.budget ? std::to_string(i.budget) : "") +
               "," + csv_cell(i.problem) + "," + csv_cell(i.text) + "\n";
    return out;
}

} // namespace sawgod::game
