// Token forms of the original strings, named battle-message parts, and the draw sites' values.
#include "game/text_tokens.h"

#include <algorithm>
#include <array>

#include "game/machine.h"
#include "game/player_names.h"
#include "game/text.h"
#include "game/text_render.h"

namespace sawgod::game {
int seal_token(std::u32string_view text, size_t *length) {
    if (length)
        *length = 0;
    constexpr std::u32string_view tokens[] = {U"{seal:black}", U"{seal:blue}",     U"{seal:white}",
                                              U"{seal:red}",   U"{seal:fourfold}", U"{/seal}"};
    for (int i = 0; i < 6; ++i)
        if (text.starts_with(tokens[i])) {
            if (length)
                *length = tokens[i].size();
            return i;
        }
    return -1;
}
std::u32string expand_seals(std::u32string_view text) {
    std::u32string out;
    for (size_t i = 0; i < text.size();) {
        size_t length = 0;
        const int seal = seal_token(text.substr(i), &length);
        if (seal >= 0) {
            out += kInlineSeal + seal;
            i += length;
        } else
            out += text[i++];
    }
    return out;
}
int validate_seals(std::u32string_view text, std::vector<std::string> &errors, int open) {
    for (size_t i = 0; i < text.size(); ++i) {
        size_t key_length = 0;
        if (key_item_token(text.substr(i), &key_length) >= 0) {
            i += key_length - 1; // A key token's display text is literal, including opening braces.
            continue;
        }
        if (!text.substr(i).starts_with(U"{seal:") && !text.substr(i).starts_with(U"{/seal"))
            continue;
        size_t length = 0;
        const int seal = seal_token(text.substr(i), &length);
        if (seal < 0)
            errors.emplace_back(
                "invalid seal token: expected black, blue, white, red or fourfold and closing }");
        else if (seal == 5) {
            if (open < 0)
                errors.emplace_back("{/seal} with no open seal");
            open = -1;
        } else {
            if (open >= 0)
                errors.emplace_back("nested seal span");
            open = seal;
        }
        if (length)
            i += length - 1;
    }
    return open;
}

int key_item_token(std::u32string_view text, size_t *length, std::u32string_view *display) {
    if (length)
        *length = 0;
    if (display)
        *display = {};
    constexpr std::u32string_view prefix = U"{key:item_";
    if (!text.starts_with(prefix))
        return -1;
    size_t i = prefix.size();
    int id = 0;
    const size_t first = i;
    for (; i < text.size() && text[i] >= U'0' && text[i] <= U'9'; ++i) {
        id = id * 10 + int(text[i] - U'0');
        if (id > 255)
            return -1;
    }
    if (i == first || i >= text.size() || (i - first > 1 && text[first] == U'0'))
        return -1;
    std::u32string_view label;
    if (text[i] == U':') {
        const size_t begin = ++i;
        i = text.find(U'}', begin);
        if (i == std::u32string_view::npos || i == begin)
            return -1;
        label = text.substr(begin, i - begin);
    } else if (text[i] != U'}')
        return -1;
    if (length)
        *length = i + 1;
    if (display)
        *display = label;
    return id;
}
std::u32string marked_item_run(int id, std::u32string_view name) {
    if (!is_marked_key_item(id) && !is_kings_gear(id))
        return std::u32string(name);
    return std::u32string(1, kInlineKeyItem + id) + std::u32string(name) + kItemNameEnd;
}
std::u32string expand_key_items(std::u32string_view text, const std::function<std::u32string(int)> &name) {
    std::u32string out;
    for (size_t i = 0; i < text.size();) {
        size_t length = 0;
        std::u32string_view display;
        if (const int id = key_item_token(text.substr(i), &length, &display); id >= 0) {
            std::u32string label = display.empty() ? name(id) : std::u32string(display);
            if (!display.empty())
                for (auto &c : label)
                    c = c == U'{' ? kLiteralBrace : c == U'[' ? kLiteralBracket : c;
            // A requirement marks the words with key A, even when it requires equipment.
            out += marked_item_run(!display.empty() && is_kings_gear(id) ? 20 : id, label);
            i += length;
        } else
            out += text[i++];
    }
    return out;
}

namespace {

bool starts(std::string_view s, std::string_view prefix) { return s.substr(0, prefix.size()) == prefix; }

// One original cell per element: a character, or a [XX] code the charset has no character for.
std::vector<std::string> cells_of(std::string_view s) {
    std::vector<std::string> out;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '[' && i + 3 < s.size() && s[i + 3] == ']') {
            out.emplace_back(s.substr(i, 4));
            i += 4;
            continue;
        }
        const uint8_t c = uint8_t(s[i]);
        const size_t n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        out.emplace_back(s.substr(i, n));
        i += n;
    }
    return out;
}

constexpr std::string_view kSpace = "　";

// Value and layout gaps: the original's cells (a substring) -> the token form. `{cursor}`/`{tab}` mark
// where the original's next word starts (its column is recorded); `^` anchors at the string's start, `$`
// at its end (after the trailing padding is gone).
struct Rule {
    std::string_view from, to;
};
constexpr Rule kValueRules[] = {
    {"ＨＰ　　　／　　　　", "ＨＰ{hp}／{maxhp}"},
    {"^ＨＰ　　　$", "ＨＰ{hp}"},
    {"^　　　／　　　　$", "{hp}／{maxhp}"},
    {"[D1]×　", "[D1]×{hearts}"},
    {"それなら　　　　　　ケロだ！", "それなら{gold}ケロだ！"},
    {"^　　　　　　ケロ$", "{gold}ケロ"},
    {"とうの　　かい", "とうの{floor}かい"},
    {"^　　かいまで", "{topfloor}かいまで"},
    {"^‥　　$", "‥{value}"},
    {"^　を　かった！", "{item}を　かった！"},
    {"^　を　てにいれた$", "{item}を　てにいれた"},
    {"０あがりました", "{amount}あがりました"},
};
constexpr Rule kLayoutRules[] = {
    {"ＯＮ　　／　　ＯＦＦ", "ＯＮ　　／{cursor}ＯＦＦ"},
    {"はやい　／　　おそい", "はやい　／{cursor}おそい"},
    {"ステレオ／　　モノラル", "ステレオ／{cursor}モノラル"},
    {"のうりょく　　けいれき", "のうりょく{tab}けいれき"},
    {"ステータス　　　のうりょく", "ステータス{tab}のうりょく"},
    {"^　ＯＮ$", "ＯＮ"}, // the current value, aligned right with ＯＦＦ
};

// Keys whose original draw is shorter than the string (draw_text_n_white with a count): the visible part,
// and its token form where the code draws its own value after it.
struct Visible {
    std::string_view key;
    int cells;
    std::string_view form = {};
};
// Keys whose value is drawn right before the label, with no gap in the string: the token form starts at the
// value's cell (the site draws it there), and `cells` clears from there to what's to its right.
struct Leading {
    std::string_view key, form;
    int cells;
};
constexpr Leading kLeading[] = {
    // the member's name at column 13, the label at 18; to the window's right edge
    {"draw_monster_history_screen_2", "{name}の　へんかけいれき", 15},
    // the generation (up to four digits, ones at column 5) before だいめ at 7; up to the history page
    {"draw_monster_history_screen_1", "{generation}だいめ", 9},
};
constexpr Visible kVisible[] = {
    {"draw_save_slot_summary_6", 5}, // ＨＰ　　　: the current HP only
    // ＨＰ, then the template's HP three cells on
    {"draw_creation_type_detail_screen_5", 2, "ＨＰ{hp}"},
    {"draw_creation_selected_preview_1", 2, "ＨＰ{hp}"},
    {"draw_creation_type_preview_1", 2, "ＨＰ{hp}"},
};

bool fullwidth_latin(const std::string &c) {
    return c.size() == 3 && ((c >= "Ａ" && c <= "Ｚ") || (c >= "ａ" && c <= "ｚ"));
}

// Button letters -> tokens: a fullwidth A, B, X or Y (X and Y with an optional digit 1-4) that isn't part of
// a word in Latin letters (ＥＭＰＴＹ, ＢＧ), and ＳＴＡＲＴ.
std::vector<std::string> button_tokens(const std::vector<std::string> &in) {
    std::vector<std::string> out;
    for (size_t i = 0; i < in.size(); ++i) {
        static const std::array<std::string, 5> kStart = {"Ｓ", "Ｔ", "Ａ", "Ｒ", "Ｔ"};
        if (i + 5 <= in.size() && std::equal(kStart.begin(), kStart.end(), in.begin() + long(i)) &&
            (i + 5 == in.size() || !fullwidth_latin(in[i + 5])) && (i == 0 || !fullwidth_latin(in[i - 1]))) {
            out.push_back("{Start}");
            i += 4;
            continue;
        }
        const std::string &c = in[i];
        const bool pad = c == "Ｘ" || c == "Ｙ";
        if ((pad || c == "Ａ" || c == "Ｂ") && (i == 0 || !fullwidth_latin(in[i - 1]))) {
            size_t n = 1;
            std::string digit;
            if (pad && i + 1 < in.size() && in[i + 1] >= "１" && in[i + 1] <= "４" && in[i + 1].size() == 3)
                digit = std::string(1, char('1' + (uint8_t(in[i + 1][2]) - uint8_t("１"[2])))), n = 2;
            if (i + n == in.size() || !fullwidth_latin(in[i + n])) {
                const char letter = c == "Ａ" ? 'A' : c == "Ｂ" ? 'B' : c == "Ｘ" ? 'X' : 'Y';
                out.push_back("{" + std::string(1, letter) + digit + "}");
                i += n - 1;
                continue;
            }
        }
        out.push_back(c);
    }
    return out;
}

// Applies one rule to the cells: true if it matched. `tabs` gets the original column of each layout token.
bool apply(std::vector<std::string> &cells, const Rule &rule, std::vector<int> &tabs,
           std::vector<int> &column) {
    std::string_view from = rule.from;
    const bool head = !from.empty() && from[0] == '^', tail = !from.empty() && from.back() == '$';
    from = from.substr(head ? 1 : 0, from.size() - (head ? 1 : 0) - (tail ? 1 : 0));
    const std::vector<std::string> pat = cells_of(from);
    for (size_t at = 0; at + pat.size() <= cells.size(); ++at) {
        if (head && at)
            break;
        if (tail && !std::all_of(cells.begin() + long(at + pat.size()), cells.end(),
                                 [](const std::string &c) { return c == kSpace; }))
            continue;
        if (!std::equal(pat.begin(), pat.end(), cells.begin() + long(at)))
            continue;
        // the replacement: literals match the pattern's cells in order; a token takes the gap there (its
        // spaces, or the ０ a number is drawn over)
        std::vector<std::string> rep, to = cells_of(rule.to);
        std::vector<int> cols;
        size_t p = 0; // position in the pattern
        for (size_t k = 0; k < to.size(); ++k) {
            if (to[k] == "{") {
                std::string token;
                for (; k < to.size() && to[k] != "}"; ++k)
                    token += to[k];
                token += "}";
                while (p < pat.size() && (pat[p] == kSpace || pat[p] == "０"))
                    ++p;
                if (token == "{cursor}" || token == "{tab}")
                    tabs.push_back(column[at + p]);
                rep.push_back(token);
                cols.push_back(-1);
                continue;
            }
            rep.push_back(to[k]);
            cols.push_back(p < pat.size() ? column[at + p] : -1);
            ++p;
        }
        cells.erase(cells.begin() + long(at), cells.begin() + long(at + pat.size()));
        cells.insert(cells.begin() + long(at), rep.begin(), rep.end());
        column.erase(column.begin() + long(at), column.begin() + long(at + pat.size()));
        column.insert(column.begin() + long(at), cols.begin(), cols.end());
        return true;
    }
    return false;
}

} // namespace

bool padding_key(std::string_view original) {
    const auto cells = cells_of(original);
    return !cells.empty() &&
           std::all_of(cells.begin(), cells.end(), [](const std::string &c) { return c == kSpace; });
}

TokenForm text_tokens(std::string_view key, std::string_view original, bool buttons_only) {
    TokenForm out;
    std::vector<std::string> cells = cells_of(original);
    out.cells = int(cells.size());
    // Fixed grids read a character at a time (the name-entry sheets) keep every cell.
    if (starts(key, "name_entry_sheet_")) {
        out.text = std::string(original);
        return out;
    }
    if (!buttons_only)
        for (const Leading &l : kLeading)
            if (key == l.key) {
                out.text = std::string(l.form), out.cells = l.cells;
                return out;
            }
    for (const Visible &v : kVisible)
        if (key == v.key && int(cells.size()) > v.cells) {
            cells.resize(size_t(v.cells)), out.cells = v.cells;
            if (!v.form.empty() && !buttons_only) {
                out.text = std::string(v.form);
                return out;
            }
        }
    if (padding_key(original))
        return out;
    std::vector<int> column(cells.size());
    for (size_t i = 0; i < column.size(); ++i)
        column[i] = int(i);
    const bool debug = starts(key, "L_"); // unreachable debug screens: their layout stays positional
    if (!buttons_only) {
        if (!debug)
            for (const Rule &r : kValueRules)
                apply(cells, r, out.tabs, column);
        for (const Rule &r : kLayoutRules)
            if (!debug || r.from == kLayoutRules[2].from) // Stereo/Mono is a debug key on a real panel
                apply(cells, r, out.tabs, column);
        while (!cells.empty() && cells.back() == kSpace)
            cells.pop_back(), column.pop_back();
    }
    if (!debug)
        cells = button_tokens(cells);
    for (const std::string &c : cells)
        out.text += c;
    return out;
}

std::u32string with_button_tokens(const TextTable &table, TextKey key, std::u32string_view s) {
    if (!table.button_icons())
        return std::u32string(s);
    return utf8_to_u32(text_tokens(kTextKeys[size_t(key)].name, name_utf8(s), /*buttons_only=*/true).text);
}

std::vector<std::string> message_tokens(const TextMessage &msg) {
    const std::string_view key = msg.key;
    std::vector<std::string> parts;
    for (std::string_view p = msg.parts; !p.empty();) {
        const size_t plus = p.find('+');
        parts.emplace_back(p.substr(0, plus));
        p = plus == std::string_view::npos ? std::string_view() : p.substr(plus + 1);
    }
    const bool party = starts(key, "msg_party_");
    const bool ability = starts(key, "msg_ability_") || key == "msg_announce_innate_battle_attack";
    std::vector<std::string> out;
    for (size_t i = 0; i < parts.size(); ++i) {
        const std::string &p = parts[i];
        if (!starts(p, "R:"))
            continue;
        const std::string next = i + 1 < parts.size() ? parts[i + 1] : std::string();
        std::string name;
        if (p == "R:D60F")
            name = party ? "leader" : key == "msg_transformation_item_prompt" ? "monster" : "actor";
        else if (p == "R:D62F")
            name = key == "msg_transformation_result" ? "monster" : "target";
        else if (p == "R:D64F")
            name = ability ? (key == "msg_ability_replaced" &&
                                      std::find(out.begin(), out.end(), "ability") != out.end()
                                  ? "new_ability"
                                  : "ability")
                           : "item";
        else if (p == "R:D66F") {
            if (next == "queue_item_count_footer_2" || next == "append_target_group_suffix_2")
                name = "index"; // which one of a group (たいめ: the Nth)
            else if (key == "msg_announce_battle_action_group" || key == "msg_damage" ||
                     key == "msg_multi_hit_count")
                name = "count";
            else if (key == "msg_gold_stolen")
                name = "gold";
            else
                name = "amount";
        } else if (p == "R:DC9B")
            name = "gold";
        else
            name = "value";
        out.push_back(name);
    }
    return out;
}

const std::vector<std::string> &value_token_names() {
    static const std::vector<std::string> names = {"hp",    "maxhp",    "hearts",    "gold",
                                                   "floor", "topfloor", "value",     "amount",
                                                   "item",  "name",     "generation"};
    return names;
}

bool is_value_token(std::u32string_view name) {
    for (const std::string &n : value_token_names())
        if (name == std::u32string(n.begin(), n.end()))
            return true;
    return false;
}

namespace {
thread_local const TextValues *g_values = nullptr;
}

TextValues::TextValues() : outer_(g_values) { g_values = this; }

TextValues::~TextValues() { g_values = outer_; }

const std::u32string *TextValues::find(std::u32string_view name) {
    for (const TextValues *v = g_values; v; v = v->outer_)
        for (const auto &[k, value] : v->values_)
            if (k == name)
                return &value;
    return nullptr;
}

bool has_value_tokens(Machine &m, TextKey key) {
    const TextTable *t = m.text_table();
    if (!t || !t->proportional())
        return false;
    const int offset = t->find(key);
    const std::u32string *s = offset >= 0 ? t->characters(uint16_t(offset)) : nullptr;
    if (!s)
        return false;
    for (size_t i = s->find(U'{'); i != std::u32string::npos; i = s->find(U'{', i + 1)) {
        const size_t close = s->find(U'}', i);
        if (close != std::u32string::npos &&
            is_value_token(std::u32string_view(*s).substr(i + 1, close - i - 1)))
            return true;
    }
    return false;
}

std::u32string decimal_text(unsigned value, unsigned digits) {
    unsigned limit = 1;
    for (unsigned i = 0; i < digits; ++i)
        limit *= 10;
    if (value >= limit)
        value = limit - 1;
    std::u32string out;
    for (const char c : std::to_string(value))
        out += char32_t(c);
    return out;
}

std::u32string name_text(Machine &m, NameTable table, uint8_t id) {
    if (const TextTable *t = m.text_table()) {
        const int run = t->name_run(int(table), id);
        if (const std::u32string *s = run >= 0 ? t->run(uint16_t(run)) : nullptr)
            return *s;
    }
    const TextPtr p = name_record(m, table, id);
    return ram_text(m, p.segment, p.offset, table == NameTable::Monster ? 8 : 7);
}

std::u32string ram_text(Machine &m, uint16_t segment, uint16_t offset, unsigned max) {
    static const Charset charset = Charset::system();
    std::u32string out;
    for (unsigned i = 0; i < max; ++i) {
        const uint8_t code = m.rd8(segment, uint16_t(offset + i));
        if (code == 0xFF)
            break;
        for (char32_t c : utf8_to_u32(charset.decode(code)))
            out += c == U'　' ? U' ' : c >= U'！' && c <= U'～' ? char32_t(c - U'！' + U'!') : c;
    }
    const size_t a = out.find_first_not_of(U' '), b = out.find_last_not_of(U' ');
    return a == std::u32string::npos ? std::u32string() : out.substr(a, b - a + 1);
}

} // namespace sawgod::game
