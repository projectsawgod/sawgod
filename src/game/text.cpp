// Resolves localized game strings and assembles battle message text.
#include "game/text.h"
#include "game/text_tokens.h"
#include "game/text_wrap.h"

#include "emu/wsc.h"
#include "game/machine.h"
#include "game/player_names.h"
#include "game/session.h"
#include "game/text_render.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace sawgod::game {

namespace {

// One UTF-8 character starting at s[i]; advances i.
std::string next_char(std::string_view s, size_t &i) {
    const uint8_t c = uint8_t(s[i]);
    const size_t n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    std::string out(s.substr(i, n));
    i += n;
    return out;
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    c = char(std::toupper(uint8_t(c)));
    return c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

// The full-width form of a printable ASCII character (U+FF01..U+FF5E; space -> U+3000), as UTF-8.
std::string full_width(char c) {
    const uint32_t cp = c == ' ' ? 0x3000 : 0xFF01 + uint32_t(c - '!');
    return {char(0xE0 | (cp >> 12)), char(0x80 | ((cp >> 6) & 0x3F)), char(0x80 | (cp & 0x3F))};
}

// A dialogue run's resource text -> script bytes (the dialogue notation): glyphs as code + 0x20
// (proportional: a placeholder, its character noted at its offset), {n} {lf} {box} {clear} {wait}
// {pause:N} {name:N} as their opcodes, [XX] a raw code.
bool encode_dialogue(const std::string &text, const Charset &charset, bool proportional, size_t base,
                     std::vector<uint8_t> &out, std::unordered_map<uint16_t, char32_t> &chars,
                     std::string *error) {
    static const std::map<std::string, uint8_t> kLayout = {
        {"{n}", 0x0D}, {"{lf}", 0x0A}, {"{box}", 0x0C}, {"{clear}", 0x0E}, {"{wait}", 0x13}};
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '{') {
            const size_t close = text.find('}', i);
            if (close == std::string::npos) {
                *error = "unclosed {";
                return false;
            }
            const std::string tok = text.substr(i, close - i + 1);
            const auto it = kLayout.find(tok);
            if (it != kLayout.end()) {
                out.push_back(it->second);
            } else if (tok.rfind("{pause:", 0) == 0 || tok.rfind("{name:", 0) == 0) {
                out.push_back(tok[1] == 'p' ? 0x16 : 0x19);
                out.push_back(uint8_t(std::atoi(tok.c_str() + (tok[1] == 'p' ? 7 : 6))));
            } else {
                *error = "unknown " + tok;
                return false;
            }
            i = close + 1;
            continue;
        }
        size_t j = i;
        const std::string ch = next_char(text, j);
        std::vector<uint8_t> code;
        if (proportional && ch != "[") {
            chars[uint16_t(base + out.size())] = utf8_to_u32(ch)[0];
            out.push_back(0xD9 + 0x20); // a space for anything that reads the bytes; drawn from `chars`
            i = j;
            continue;
        }
        const size_t len = text[i] == '[' && i + 3 < text.size() && text[i + 3] == ']' ? 4 : j - i;
        if (!charset.encode(std::string_view(text).substr(i, len), code, error))
            return false;
        for (uint8_t c : code)
            out.push_back(uint8_t(c + 0x20));
        i += len;
    }
    return true;
}

// A message template's words: literal text as runs (proportional) or code words (2 * code), {n} as
// TextTable::kTemplatePart + n.
// A token form for drawing in the WSC font: tokens kept, every other character as its glyph (U+E000 + code).
std::u32string icon_glyphs(const std::string &form, const Charset &charset) {
    std::u32string out;
    for (size_t i = 0; i < form.size();) {
        if (form[i] == '{') {
            const size_t close = form.find('}', i);
            const size_t end = close == std::string::npos ? form.size() : close + 1;
            for (char c : form.substr(i, end - i))
                out += char32_t(c);
            i = end;
            continue;
        }
        size_t j = i;
        std::string one = next_char(form, j);
        if (one == "[" && i + 3 < form.size() && form[i + 3] == ']') {
            one = form.substr(i, 4);
            j = i + 4;
        }
        std::vector<uint8_t> code;
        if (charset.encode(one, code, nullptr) && code.size() == 1)
            out += char32_t(0xE000 + code[0]);
        i = j;
    }
    return out;
}

// `names`: the message's part names (message_tokens), {name} for part n; {1}, {2}... number them too.
bool parse_template(const std::string &text, const Charset &charset, bool proportional,
                    std::vector<std::u32string> &runs, std::vector<uint16_t> &words, std::string *error,
                    const std::vector<std::string> &names = {}) {
    size_t i = 0;
    auto literal = [&](std::string_view lit) {
        if (lit.empty())
            return true;
        if (proportional) {
            words.push_back(uint16_t(kTextWord | runs.size()));
            runs.push_back(utf8_to_u32(lit));
            return true;
        }
        std::vector<uint8_t> codes;
        if (!charset.encode(lit, codes, error))
            return false;
        for (uint8_t c : codes)
            words.push_back(uint16_t(c * 2));
        return true;
    };
    while (i < text.size()) {
        const size_t open = text.find('{', i);
        if (open == std::string::npos)
            return literal(std::string_view(text).substr(i));
        const size_t close = text.find('}', open);
        if (close == std::string::npos || !literal(std::string_view(text).substr(i, open - i)))
            return close != std::string::npos ? false : (*error = "unclosed {", false);
        const auto token = text.substr(open, close - open + 1);
        if (token == "{n}" || token == "{lf}") {
            words.push_back(uint16_t(kTextWord | runs.size()));
            runs.push_back(utf8_to_u32(token));
            i = close + 1;
            continue;
        }
        const std::string inner = text.substr(open + 1, close - open - 1);
        int n = std::atoi(inner.c_str());
        if (const auto named = std::find(names.begin(), names.end(), inner); named != names.end())
            n = int(named - names.begin()) + 1;
        if (n < 1 || n > 9) {
            *error = "bad placeholder " + text.substr(open, close - open + 1);
            return false;
        }
        words.push_back(uint16_t(TextTable::kTemplatePart + n));
        i = close + 1;
    }
    return true;
}

} // namespace

Charset Charset::system() {
    Charset c;
    for (const CharsetEntry &e : kSystemCharset)
        c.set(e.code, e.utf8);
    return c;
}

bool Charset::add_csv(std::string_view csv, std::string *error) {
    for (const auto &row : parse_csv(csv)) {
        if (row.size() < 2 || row[0] == "code" || row[0].empty())
            continue;
        const int hi = hex_digit(row[0][0]), lo = row[0].size() > 1 ? hex_digit(row[0][1]) : -1;
        if (hi < 0 || lo < 0 || row[0].size() != 2) {
            if (error)
                *error = "charset: bad code \"" + row[0] + "\"";
            return false;
        }
        set(uint8_t(hi * 16 + lo), row[1]);
    }
    return true;
}

std::string Charset::decode(uint8_t code) const {
    const auto it = chars_.find(code);
    if (it != chars_.end()) {
        std::vector<uint8_t> back;
        if (it->second.find('[') == std::string::npos && encode(it->second, back, nullptr) &&
            back.size() == 1 && back[0] == code)
            return it->second;
    }
    static const char kHex[] = "0123456789ABCDEF";
    return std::string{'[', kHex[code >> 4], kHex[code & 15], ']'};
}

bool Charset::encode(std::string_view utf8, std::vector<uint8_t> &out, std::string *error) const {
    for (size_t i = 0; i < utf8.size();) {
        if (utf8[i] == '[' && i + 3 < utf8.size() && utf8[i + 3] == ']' && hex_digit(utf8[i + 1]) >= 0 &&
            hex_digit(utf8[i + 2]) >= 0) {
            out.push_back(uint8_t(hex_digit(utf8[i + 1]) * 16 + hex_digit(utf8[i + 2])));
            i += 4;
            continue;
        }
        std::string ch = next_char(utf8, i);
        // English punctuation and letters the WSC font lacks, drawn with its nearest glyphs
        static const std::unordered_map<std::string, std::string> kNearest = {
            {".", "。"}, {",", "、"}, {"-", "ー"}, {";", "："}, {"'", "[D1]"},
            {"Ō", "O"},  {"ō", "o"},  {"Ū", "U"},  {"ū", "u"},  {"\"", "[D1]"}};
        if (codes_.find(ch) == codes_.end()) {
            const auto near = kNearest.find(ch);
            if (near != kNearest.end()) {
                if (near->second[0] == '[') {
                    out.push_back(uint8_t(hex_digit(near->second[1]) * 16 + hex_digit(near->second[2])));
                    continue;
                }
                ch = near->second;
            }
        }
        auto it = codes_.find(ch);
        if (it == codes_.end() && ch.size() == 1 && ch[0] >= ' ' && ch[0] <= '~') {
            it = codes_.find(full_width(ch[0]));
            if (it == codes_.end())
                it = codes_.find(full_width(char(std::toupper(uint8_t(ch[0])))));
        }
        if (it == codes_.end()) {
            if (error)
                *error = "no glyph for \"" + ch + "\"";
            return false;
        }
        out.push_back(it->second);
    }
    return true;
}

std::string csv_cell(std::string_view s) {
    if (s.find_first_of(",\"\n\r") == std::string_view::npos)
        return std::string(s);
    std::string out = "\"";
    for (char c : s)
        out += c == '"' ? std::string("\"\"") : std::string(1, c);
    return out + "\"";
}

std::string merge_text(std::string_view base, std::string_view overlay) {
    std::vector<std::pair<std::string, std::string>> rows;
    std::map<std::string, size_t> at;
    for (std::string_view csv : {base, overlay}) {
        const auto parsed = parse_csv(csv);
        if (parsed.empty())
            continue;
        const auto &head = parsed[0];
        const size_t k = size_t(std::find(head.begin(), head.end(), "key") - head.begin()),
                     t = size_t(std::find(head.begin(), head.end(), "text") - head.begin());
        if (k >= head.size() || t >= head.size())
            continue;
        for (size_t r = 1; r < parsed.size(); ++r) {
            if (parsed[r].size() <= std::max(k, t) || parsed[r][k].empty())
                continue;
            const auto [it, added] = at.emplace(parsed[r][k], rows.size());
            if (added)
                rows.emplace_back(parsed[r][k], parsed[r][t]);
            else
                rows[it->second].second = parsed[r][t];
        }
    }
    std::string out = "key,text\n";
    for (const auto &[key, text] : rows)
        out += csv_cell(key) + "," + csv_cell(text) + "\n";
    return out;
}

std::string export_text(const emu::Wsc &w) {
    static const Charset charset = Charset::system();
    const auto at = [&](uint32_t a) { return w.rom[a & (w.rom.size() - 1)]; };
    const auto decode = [&](const std::vector<uint8_t> &codes) {
        std::string s;
        for (uint8_t c : codes)
            s += charset.decode(c);
        return s;
    };
    std::string out = "key,text,routine,kind\n";
    const auto row = [&](const std::string &key, const std::string &text, std::string_view routine,
                         std::string_view kind) {
        out +=
            csv_cell(key) + "," + csv_cell(text) + "," + csv_cell(routine) + "," + std::string(kind) + "\n";
    };
    // keyed strings: menu/system text (0xFF-terminated) and battle-popup fragments (words 2 * code, 0xFFFF)
    std::map<std::string, std::string> texts;
    for (const TextKeyInfo &k : kTextKeys) {
        std::vector<uint8_t> codes;
        uint32_t a = k.string;
        if (k.length)
            for (unsigned i = 0; i < k.length; ++i)
                codes.push_back(at(a + i));
        else if (k.glyphs)
            for (; !(at(a) == 0xFF && at(a + 1) == 0xFF) && codes.size() < 0x400; a += 2)
                codes.push_back(uint8_t((at(a) | at(a + 1) << 8) / 2));
        else
            for (; at(a) != 0xFF && codes.size() < 0x400; ++a)
                codes.push_back(at(a));
        const std::string name = k.name, text = decode(codes);
        texts[name] = text;
        row(name, text, name.substr(0, name.rfind('_')), k.glyphs ? "glyphs" : "text");
    }
    // battle-popup messages: the template the fragments make, the names and numbers as named tokens
    for (const TextMessage &msg : kTextMessages) {
        std::string_view parts = msg.parts;
        std::string t;
        const std::vector<std::string> names = message_tokens(msg);
        size_t n = 0, part_index = 0;
        while (!parts.empty()) {
            if (msg.japanese_break && part_index++ == size_t(msg.japanese_break))
                t += "{n}";
            const size_t plus = parts.find('+');
            const std::string part(parts.substr(0, plus));
            parts = plus == std::string_view::npos ? std::string_view() : parts.substr(plus + 1);
            if (part.rfind("R:", 0) == 0)
                t += "{" + names[n++] + "}";
            else if (const auto it = texts.find(part); it != texts.end())
                t += it->second;
        }
        row(msg.key, t, msg.routine, "message");
    }
    // the port's own strings in the game picture: their Japanese defaults
    for (const HostTextInfo &h : kHostText)
        row(h.key, h.japanese, h.context, "host");
    // names and item descriptions: fixed-size records, the space padding dropped (the game pads it back)
    struct Records {
        uint32_t rom;
        unsigned size, count;
        const char *prefix;
    };
    for (const Records r : {Records{0x394BE0, 8, 256, "item"}, Records{0x396FE0, 9, 200, "monster"},
                            Records{0x3953E0, 27, 256, "itemdesc"}})
        for (unsigned i = 0; i < r.count; ++i) {
            std::vector<uint8_t> body;
            for (unsigned k = 0; k + 1 < r.size; ++k)
                body.push_back(at(r.rom + i * r.size + k));
            while (!body.empty() && body.back() == 0xD9)
                body.pop_back();
            row(std::string(r.prefix) + "_" + std::to_string(i), decode(body),
                std::string("tables/") + r.prefix + "s", "name");
        }
    // field dialogue: one row per text run of the event scripts, layout codes as {...}
    for (const DialogueRun &d : kDialogueRuns) {
        std::string t;
        for (uint32_t a = d.start; a < d.end;) {
            const uint8_t b = at(a);
            if (b >= 0x20) {
                t += charset.decode(uint8_t(b - 0x20));
                ++a;
            } else if (b == 0x0A || b == 0x0C || b == 0x0D || b == 0x0E || b == 0x13) {
                t += b == 0x0A   ? "{lf}"
                     : b == 0x0C ? "{box}"
                     : b == 0x0D ? "{n}"
                     : b == 0x0E ? "{clear}"
                                 : "{wait}";
                ++a;
            } else if (b == 0x16 || b == 0x19) {
                t += std::string(b == 0x16 ? "{pause:" : "{name:") + std::to_string(at(a + 1)) + "}";
                a += 2;
            } else {
                static const char kHex[] = "0123456789ABCDEF";
                t += std::string{'[', kHex[b >> 4], kHex[b & 15], ']'};
                ++a;
            }
        }
        char key[16];
        std::snprintf(key, sizeof key, "dlg_%06X", unsigned(d.start));
        row(key, t, "dialogue", "dialogue");
    }
    return out;
}

std::vector<std::vector<std::string>> parse_csv(std::string_view csv) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool quoted = false, any = false;
    for (size_t i = 0; i < csv.size(); ++i) {
        const char c = csv[i];
        if (quoted) {
            if (c == '"' && i + 1 < csv.size() && csv[i + 1] == '"') {
                field += '"';
                ++i;
            } else if (c == '"') {
                quoted = false;
            } else {
                field += c;
            }
            continue;
        }
        if (c == '"') {
            quoted = any = true;
        } else if (c == ',') {
            row.push_back(std::move(field));
            field.clear();
            any = true;
        } else if (c == '\n' || c == '\r') {
            if (c == '\r' && i + 1 < csv.size() && csv[i + 1] == '\n')
                ++i;
            if (any || !field.empty()) {
                row.push_back(std::move(field));
                rows.push_back(std::move(row));
            }
            row.clear();
            field.clear();
            any = false;
        } else {
            field += c;
            any = true;
        }
    }
    if (any || !field.empty()) {
        row.push_back(std::move(field));
        rows.push_back(std::move(row));
    }
    return rows;
}

bool TextTable::load(std::string_view csv, const Charset &charset, std::string *error,
                     std::vector<std::string> *warnings, int *changed, bool proportional, bool layer) {
    auto warn = [&](const std::string &msg) {
        if (warnings)
            warnings->push_back(msg);
    };
    auto fail = [&](const std::string &msg) {
        if (error)
            *error = msg;
        return false;
    };
    const auto rows = parse_csv(csv);
    if (rows.empty())
        return fail("text: empty");
    const auto &head = rows[0];
    const auto col = [&](const char *name) {
        return int(std::find(head.begin(), head.end(), name) - head.begin());
    };
    const int key_col = col("key"), text_col = col("text");
    if (key_col >= int(head.size()) || text_col >= int(head.size()))
        return fail("text: needs \"key\" and \"text\" columns");
    std::map<std::string, int> index;
    for (int i = 0; i < kTextKeyCount; ++i)
        index[kTextKeys[i].name] = i;

    // `layer`: on top of what is loaded (its strings stay where they are; this file's keys replace theirs)
    const bool keep = layer && !offsets_.empty();
    std::vector<int> offsets = keep ? offsets_ : std::vector<int>(kTextKeyCount, -1);
    std::vector<uint8_t> data = keep ? data_ : std::vector<uint8_t>();
    std::map<std::vector<uint8_t>, int> placed; // identical strings share space
    std::unordered_map<uint16_t, std::u32string> chars = keep ? chars_ : decltype(chars_)();
    std::vector<std::u32string> runs = keep ? runs_ : decltype(runs_)();
    std::unordered_map<std::string, std::vector<uint16_t>> templates =
        keep ? templates_ : decltype(templates_)();
    std::unordered_map<uint32_t, int> dialogue = keep ? dialogue_ : decltype(dialogue_)();
    auto dialogue_ends = keep ? dialogue_ends_ : decltype(dialogue_ends_)();
    std::unordered_map<uint16_t, char32_t> dialogue_chars =
        keep ? dialogue_chars_ : decltype(dialogue_chars_)();
    std::unordered_map<int, std::pair<int, int>> names = keep ? names_ : decltype(names_)();
    std::unordered_map<uint8_t, int> records = keep ? records_ : decltype(records_)();
    // A proportional (English) layer never keeps the base's host strings: a key it lacks falls back to the
    // built-in English (host_text), not to the Japanese underneath.
    std::unordered_map<std::string, std::u32string> host = keep && !proportional ? host_ : decltype(host_)();
    auto layouts = keep ? layouts_ : decltype(layouts_)();
    auto icon_chars = keep ? icon_chars_ : decltype(icon_chars_)();
    int count = 0;
    for (size_t r = 1; r < rows.size(); ++r) {
        auto row = rows[r];
        if (int(row.size()) <= std::max(key_col, text_col) || row[key_col].empty())
            continue;
        // English cell breaks remain spaces outside battle; Japanese preserves explicit breaks.
        auto &cell = row[text_col];
        for (size_t at = 0;
             !(proportional && (text_message(row[key_col]) || row[key_col].starts_with("msg:"))) &&
             (at = cell.find_first_of("\r\n", at)) != std::string::npos;
             ++at) {
            const size_t count = cell[at] == '\r' && at + 1 < cell.size() && cell[at + 1] == '\n' ? 2 : 1;
            cell.replace(at, count, proportional ? " " : "{n}");
            if (!proportional)
                at += 2;
        }
        if (host_text_info(row[key_col])) { // a host string: kept as characters
            host[row[key_col]] = utf8_to_u32(row[text_col]);
            ++count;
            continue;
        }
        // A name or description: the prefix, then digits only (item_12, not item_obtained_suffix).
        const auto numbered = [&](std::string_view prefix) {
            const std::string_view key = row[key_col];
            return key.rfind(prefix, 0) == 0 && key.size() > prefix.size() &&
                   key.find_first_not_of("0123456789", prefix.size()) == std::string_view::npos;
        };
        const bool desc = numbered("itemdesc_");
        if (desc || numbered("item_") || numbered("monster_")) {
            const bool item = row[key_col][0] == 'i';
            const int id = std::atoi(row[key_col].c_str() + (desc ? 9 : item ? 5 : 8));
            const size_t size = desc ? TextTable::kDescriptionSize : item ? 8 : 9;
            if (id < 0 || id > (item ? 255 : 199)) {
                warn("line " + std::to_string(r + 1) + ": no name \"" + row[key_col] + "\" (skipped)");
                continue;
            }
            // the record: the characters the charset has (others as "?"), padded with spaces, 0xFF last
            std::vector<uint8_t> rec;
            const std::u32string u32 = utf8_to_u32(row[text_col]);
            size_t i = 0;
            for (char32_t c : u32) {
                std::vector<uint8_t> code;
                std::string one;
                size_t j = i;
                one = next_char(row[text_col], j);
                i = j;
                (void)c;
                if (!charset.encode(one, code, nullptr))
                    code = {0xD9};
                rec.insert(rec.end(), code.begin(), code.end());
            }
            rec.resize(size - 1, 0xD9);
            rec.push_back(0xFF);
            if (data.size() + rec.size() > 0x10000)
                return fail("text: more than 64 KB of strings");
            int run = -1;
            if (proportional) {
                run = int(runs.size());
                runs.push_back(u32);
                chars[uint16_t(data.size())] = u32;
            }
            if (desc)
                records[uint8_t(id)] = int(data.size());
            else
                names[(item ? 0 : 1) << 8 | id] = {int(data.size()), run};
            data.insert(data.end(), rec.begin(), rec.end());
            ++count;
            continue;
        }
        if (row[key_col].rfind("dlg_", 0) == 0) { // a field dialogue run
            const uint32_t start = uint32_t(std::strtoul(row[key_col].c_str() + 4, nullptr, 16));
            const DialogueRun *run = nullptr;
            for (const DialogueRun &d : kDialogueRuns)
                if (d.start == start)
                    run = &d;
            if (!run) {
                warn("line " + std::to_string(r + 1) + ": no dialogue run \"" + row[key_col] +
                     "\" (skipped)");
                continue;
            }
            std::vector<uint8_t> bytes;
            std::unordered_map<uint16_t, char32_t> dchars;
            std::string why;
            if (!encode_dialogue(row[text_col], charset, proportional, data.size(), bytes, dchars, &why)) {
                warn(row[key_col] + ": " + why + " (kept the original)");
                continue;
            }
            const uint16_t resume = uint16_t(run->end - 0x350000);
            dialogue_ends[uint16_t(data.size() + bytes.size())] = resume;
            bytes.insert(bytes.end(), {TextTable::kDialogueEnd, uint8_t(resume), uint8_t(resume >> 8)});
            if (data.size() + bytes.size() > 0x10000)
                return fail("text: more than 64 KB of strings");
            dialogue[start] = int(data.size());
            dialogue_chars.insert(dchars.begin(), dchars.end());
            data.insert(data.end(), bytes.begin(), bytes.end());
            ++count;
            continue;
        }
        const TextMessage *msg = text_message(row[key_col]);
        if (msg || row[key_col].rfind("msg:", 0) == 0) { // a battle-popup message template
            std::vector<uint16_t> words;
            std::string why;
            if (!parse_template(row[text_col], charset, proportional, runs, words, &why,
                                msg ? message_tokens(*msg) : std::vector<std::string>{}))
                warn(row[key_col] + ": " + why + " (skipped)");
            else
                templates[msg ? std::string(msg->parts) : row[key_col].substr(4)] = std::move(words), ++count;
            continue;
        }
        const auto it = index.find(row[key_col]);
        if (it == index.end()) {
            warn("line " + std::to_string(r + 1) + ": no key \"" + row[key_col] + "\" (skipped)");
            continue;
        }
        const TextKeyInfo &k = kTextKeys[it->second];
        std::vector<uint8_t> codes, bytes;
        std::string why;
        const std::u32string u32 = utf8_to_u32(row[text_col]);
        if (k.length) { // exactly k.length characters (the game reads them one at a time): WSC codes always
            if (!charset.encode(row[text_col], bytes, &why)) {
                warn(row[key_col] + ": " + why + " (kept the original)");
                continue;
            }
            if (bytes.size() > k.length)
                warn(row[key_col] + ": more than " + std::to_string(k.length) + " characters (cut)");
            bytes.resize(k.length, 0xD9);
        } else if (proportional && k.glyphs) { // one word, kTextWord | run: the characters stay host-side
            const uint16_t w = uint16_t(kTextWord | runs.size());
            runs.push_back(u32);
            bytes = {uint8_t(w), uint8_t(w >> 8), 0xFF, 0xFF};
        } else if (proportional) { // a placeholder of the same length; the renderer draws `chars`
            bytes.assign(u32.size(), 0xD9);
            bytes.push_back(0xFF);
        } else {
            if (!charset.encode(row[text_col], codes, &why)) {
                warn(row[key_col] + ": " + why + " (kept the original)");
                continue;
            }
            if (k.glyphs) { // the battle popup's glyph list: each code as the word 2 * code (table at
                            // 9000:2080)
                for (uint8_t c : codes)
                    bytes.insert(bytes.end(), {uint8_t(c * 2), uint8_t((c * 2) >> 8)});
                bytes.insert(bytes.end(), {0xFF, 0xFF});
            } else {
                bytes = std::move(codes);
                bytes.push_back(0xFF);
            }
        }
        // the original's layout under a proportional string: the string it replaces (the base's), in cells
        const int under = offsets[size_t(it->second)];
        TextLayout layout;
        if (proportional && !k.glyphs && !k.length && under >= 0) {
            if (const auto l = layouts.find(uint16_t(under)); l != layouts.end())
                layout = l->second;
            else {
                std::string original;
                for (size_t i = size_t(under); i < data.size() && data[i] != 0xFF; ++i)
                    original += charset.decode(data[i]);
                const TokenForm form = text_tokens(k.name, original);
                layout = {form.cells, form.tabs};
            }
        }
        // identical strings share space (not proportional text: its placeholder bytes don't identify it)
        const auto done = proportional && !k.glyphs && !k.length ? placed.end() : placed.find(bytes);
        if (done != placed.end()) {
            offsets[size_t(it->second)] = done->second;
        } else {
            if (data.size() & 1)
                data.push_back(0xFF); // word-align (glyph lists are read as words)
            if (data.size() + bytes.size() > 0x10000)
                return fail("text: more than 64 KB of strings");
            offsets[size_t(it->second)] = int(data.size());
            placed[bytes] = int(data.size());
            if (proportional && !k.glyphs && !k.length) {
                chars[uint16_t(data.size())] = u32;
                if (layout.cells)
                    layouts[uint16_t(data.size())] = layout;
            } else if (!proportional && !k.glyphs && !k.length) {
                // the base's button letters as icons, for when they are on (set_button_icons)
                const TokenForm form = text_tokens(k.name, row[text_col], /*buttons_only=*/true);
                if (form.text.find('{') != std::string::npos) {
                    icon_chars[uint16_t(data.size())] = icon_glyphs(form.text, charset);
                    layouts[uint16_t(data.size())] = {form.cells, {}};
                }
            }
            data.insert(data.end(), bytes.begin(), bytes.end());
        }
        ++count;
    }
    if (!keep) { // a new base: its strings are the original's when it is in the WSC font
        original_offsets_.clear();
        original_names_.clear();
        original_records_.clear();
        if (!proportional) {
            original_offsets_ = offsets;
            for (const auto &[source, name] : names)
                original_names_[source] = name.first;
            original_records_ = records;
        }
    }
    offsets_ = std::move(offsets);
    data_ = std::move(data);
    chars_ = std::move(chars);
    runs_ = std::move(runs);
    templates_ = std::move(templates);
    dialogue_ = std::move(dialogue);
    dialogue_ends_ = std::move(dialogue_ends);
    names_ = std::move(names);
    records_ = std::move(records);
    host_ = std::move(host);
    dialogue_chars_ = std::move(dialogue_chars);
    layouts_ = std::move(layouts);
    icon_chars_ = std::move(icon_chars);
    proportional_ = proportional || (keep && proportional_);
    if (changed)
        *changed = count;
    return true;
}

void TextTable::clear() {
    offsets_.clear();
    data_.clear();
    chars_.clear();
    runs_.clear();
    message_run_.clear();
    templates_.clear();
    dialogue_.clear();
    dialogue_ends_.clear();
    names_.clear();
    records_.clear();
    dialogue_chars_.clear();
    original_offsets_.clear();
    original_names_.clear();
    original_records_.clear();
    host_.clear();
    layouts_.clear();
    icon_chars_.clear();
    proportional_ = false;
}

const HostTextInfo *host_text_info(std::string_view key) {
    for (const HostTextInfo &h : kHostText)
        if (key == h.key)
            return &h;
    return nullptr;
}

const TextMessage *text_message(std::string_view key) {
    const bool parts = key.substr(0, 4) == "msg:";
    for (const TextMessage &m : kTextMessages)
        if (parts ? key.substr(4) == m.parts : key == m.key)
            return &m;
    return nullptr;
}

std::u32string host_text(emu::Wsc &w, std::string_view key, std::u32string_view arg) {
    const HostTextInfo *info = host_text_info(key);
    if (!info)
        return {};
    const Session *s = Session::find(w);
    const std::u32string *loaded = s ? s->text().host(key) : nullptr;
    std::u32string text =
        loaded ? *loaded : utf8_to_u32(s && s->text().proportional() ? info->english : info->japanese);
    if (const std::string_view param = host_param(key); !param.empty()) {
        const std::u32string token = U"{" + utf8_to_u32(param) + U"}";
        size_t at = 0;
        while ((at = text.find(token, at)) != std::u32string::npos) {
            text.replace(at, token.size(), arg);
            at += arg.size();
        }
    }
    return with_wsc_glyphs(text);
}

std::string_view host_param(std::string_view key) {
    static constexpr std::pair<std::string_view, std::string_view> kParams[] = {
        {"ui_dialogue_item_obtained", "item"}, {"ui_service_sale_offer", "gold"},
        {"ui_party_current_floor", "floor"},   {"ui_party_highest_floor", "topfloor"},
        {"ui_inventory_healing", "amount"},    {"ui_inventory_power", "amount"},
        {"ui_inventory_defense", "amount"},    {"ui_item_floor", "floor"},
        {"ui_battle_meat", "monster"},         {"ui_battle_meat_prompt", "monster"},
        {"ui_battle_group_count", "count"},    {"ui_name_entry_hiragana", "page"},
        {"ui_name_entry_katakana", "page"},    {"ui_bestiary_generation", "generation"},
    };
    for (const auto &[k, name] : kParams)
        if (k == key)
            return name;
    return {};
}

const TextTable *text_table(emu::Wsc &w) {
    const Session *s = Session::find(w);
    return s ? &s->text() : nullptr;
}

TextPtr text_at(Machine &m, TextKey key) {
    const TextKeyInfo &k = kTextKeys[size_t(key)];
    TextPtr p{k.segment, uint16_t(k.string - (0x340000 + (uint32_t(k.segment) << 4) - 0x40000))};
    if (const Session *s = Session::find(m.wsc()))
        if (const int off = s->text().find(key); off >= 0)
            p = {kTextSegment, uint16_t(off)};
    return p;
}

TextPtr text(Machine &m, TextKey key) {
    TextPtr p = text_at(m, key);
    if (Session *s = Session::find(m.wsc())) {
        if (m.original_popup_text()) { // the base language's string; without one, the ROM's
            const auto &k = kTextKeys[size_t(key)];
            if (const int off = s->text().original_find(key); off >= 0)
                p = {kTextSegment, uint16_t(off)};
            else
                p = {k.segment, uint16_t(k.string - (0x300000 + (uint32_t(k.segment) << 4)))};
        }
        s->note_text(key, p); // for the battle-popup message recorder
    }
    return p;
}

std::vector<uint16_t> Session::popup_name_words(Machine &m, NameTable table, uint8_t id) const {
    if (text_.proportional())
        if (const int run = text_.name_run(int(table), id); run >= 0)
            return {uint16_t(kTextWord | run)};
    const int host = text_.name(int(table), id);
    const TextPtr record =
        host >= 0 ? TextPtr{kTextSegment, uint16_t(host)}
                  : TextPtr{0x9000, uint16_t(table == NameTable::Item ? 0x4BE0 + 8 * id : 0x6FE0 + 9 * id)};
    std::vector<uint16_t> words;
    for (unsigned i = 0; i < (table == NameTable::Item ? 8u : 9u); ++i) {
        const uint8_t code = m.rd8(record.segment, uint16_t(record.offset + i));
        if (code == 0xD9 || code == 0xFF)
            break;
        words.push_back(uint16_t(code * 2));
    }
    return words;
}

void Session::record_popup_words(uint16_t buffer, std::vector<uint16_t> words) {
    for (auto &[at, value] : joined_popup_->names_)
        if (at == buffer) {
            value = std::move(words);
            return;
        }
    joined_popup_->names_.emplace_back(buffer, std::move(words));
}

void Session::record_popup_name(Machine &m, uint16_t buffer, NameTable table, uint8_t id) {
    record_popup_words(buffer, popup_name_words(m, table, id));
}

void Session::message_part(Machine &m) {
    if (m.bx() != 0xFF) {
        message_.clear(); // append_word_queue with BX != 0xFF starts the queue over
        message_timing_words_ = 0;
        message_battle_ = false;
    }
    const bool ram = m.ds() == 0;
    const bool keyed = !ram && last_key_ >= 0 && last_ptr_.segment == m.ds() && last_ptr_.offset == m.si();
    if (text_.proportional() && !m.original_popup_text()) {
        message_battle_ |= ram && (m.si() == 0xD60F || m.si() == 0xD62F || m.si() == 0xD64F);
        TextPtr original{m.ds(), m.si()};
        if (keyed) {
            const auto &key = kTextKeys[size_t(last_key_)];
            message_battle_ |= (key.site >> 16) == 0x36;
            original = {key.segment, uint16_t(key.string - (0x300000 + (uint32_t(key.segment) << 4)))};
        }
        uint16_t count = 0;
        for (uint16_t i = 0; i < m.ax() && count < m.ax(); ++i) {
            const uint16_t word = m.rd16(original.segment, uint16_t(original.offset + 2 * i));
            if (word == 0xFFFF)
                break;
            uint16_t length = 1;
            if (ram && (word & kTextWord)) {
                const uint16_t run = word & 0x7FFF;
                const int source = text_.name_source(run);
                if (source >= 0) {
                    const bool item = (source >> 8) == int(NameTable::Item);
                    const uint32_t record = (item ? 0x94BE0 : 0x96FE0) + (source & 255) * (item ? 8 : 9);
                    length = 0;
                    while (length < (item ? 8 : 9)) {
                        const uint8_t glyph = m.wsc().read8(record + length);
                        if (glyph == 0xD9 || glyph == 0xFF)
                            break;
                        ++length;
                    }
                } else if (run >= TextTable::kPlayerNameRun && run < TextTable::kPlayerNameRun + 4) {
                    const uint16_t record = uint16_t(0xD001 + (run - TextTable::kPlayerNameRun) * 31);
                    length = 0;
                    while (length < 4 && m.rd8(m.ss(), uint16_t(record + length)) != 0xFF)
                        ++length;
                }
            }
            count = uint16_t(std::min<unsigned>(m.ax(), count + length));
        }
        // The original append searches at most 25 existing words, then appends the new source.
        message_timing_words_ = uint16_t(std::min<uint16_t>(25, message_timing_words_) + count);
    }
    MessagePart part{ram ? -1 : keyed ? last_key_ : -2, ram ? m.si() : uint16_t(0), m.ax(), {}};
    TextPtr pointer{m.ds(), m.si()};
    if (joined_popup_ && m.original_popup_text()) {
        if (ram)
            for (const auto &[at, words] : joined_popup_->names_)
                if (at == part.ram) {
                    part.words = words;
                    message_.push_back(std::move(part));
                    return;
                }
        if (keyed)
            pointer = text_at(m, TextKey(last_key_)); // presentation records the language, not its clock
    }
    for (uint16_t k = 0; k < part.count; ++k) {
        const uint16_t word = m.rd16(pointer.segment, uint16_t(pointer.offset + 2 * k));
        if (word == 0xFFFF)
            break;
        part.words.push_back(word);
    }
    message_.push_back(std::move(part));
}

bool Session::present_joined_battle_popup(Machine &m) {
    constexpr uint16_t kBattleItemIcons = 0xD64F, kBattleNumberText = 0xD66F;
    constexpr uint16_t kEnemyGroups = 0xD6D5, kFooterPanelMode = 0xEC83;
    constexpr uint16_t kTargetGroup = 0xDC00, kMeterInputTarget = 0xDC0B, kActionEffect = 0xDBFD;
    auto &joined = *joined_popup_;
    if (joined.presented_) {
        message_.clear();
        return false;
    }
    joined.presented_ = true;
    std::vector<MessagePart> parts;
    parts.swap(message_);
    std::string prefix;
    for (const auto &p : parts) {
        char buffer[8];
        std::snprintf(buffer, sizeof buffer, "R:%04X", p.ram);
        prefix += (prefix.empty() ? "" : "+") + (p.key >= 0 ? std::string(kTextKeys[p.key].name) : buffer);
    }
    // Select the complete generated signature from the already queued first page. Only the action's
    // three targeting modes share a first page; group/single status prefixes already differ.
    const TextMessage *message = nullptr;
    for (const auto &candidate : kTextMessages) {
        if (candidate.routine != joined.routine_ || !std::string_view(candidate.parts).starts_with(prefix))
            continue;
        if (joined.routine_ == "announce_battle_action") {
            const uint8_t mode = m.rd8(m.ss(), kFooterPanelMode);
            const std::string_view key = candidate.key;
            if (!key.ends_with(mode == 0 ? "_target" : mode == 1 ? "_group" : "_all"))
                continue;
        }
        if (joined.routine_ == "show_target_magic_or_defense_decreased_message") {
            // execute_stat_reduction_action uses the actor-record offset: 0Ah MAG, 08h DEF.
            const bool defense = m.rd8(m.ss(), 0xDC0A) == 8; // battle_item_power
            if (std::string_view(candidate.key).starts_with("msg_target_defense_") != defense)
                continue;
        }
        message = &candidate;
        break;
    }
    if (!message)
        return true;

    const auto read_words = [&](TextPtr pointer) {
        std::vector<uint16_t> words;
        for (unsigned i = 0; i < 255; ++i) {
            const uint16_t word = m.rd16(pointer.segment, uint16_t(pointer.offset + i * 2));
            if (word == 0xFFFF)
                break;
            words.push_back(word);
        }
        return words;
    };
    // Read-only lookahead supplies values the original builds after its first wait. It never calls a
    // game routine, writes scratch RAM, consumes input, or advances the RNG/clock.
    std::string_view remaining = std::string_view(message->parts).substr(prefix.size());
    while (!remaining.empty()) {
        if (remaining.front() == '+')
            remaining.remove_prefix(1);
        const size_t end = remaining.find('+');
        const auto token = remaining.substr(0, end);
        MessagePart part{-1, 0, 255, {}};
        if (token.starts_with("R:")) {
            part.ram = uint16_t(std::stoul(std::string(token.substr(2)), nullptr, 16));
            if (part.ram == kBattleNumberText &&
                (joined.routine_ == "announce_battle_action" ||
                 joined.routine_.find("_decreased_message") != std::string::npos)) {
                const unsigned value =
                    joined.routine_ == "announce_battle_action"
                        ? m.rd8(m.ss(), uint16_t(kEnemyGroups + 1 + 2 * m.rd8(m.ss(), kTargetGroup)))
                        : m.rd16(m.ss(), kMeterInputTarget);
                for (char digit : std::to_string(value))
                    part.words.push_back(uint16_t((0xBB + digit - '0') * 2));
            } else if (part.ram == kBattleItemIcons && joined.routine_ == "show_ability_replaced_message") {
                const uint8_t item = m.rd8(m.ss(), kActionEffect); // replacement ability
                part.words = popup_name_words(m, NameTable::Item, item);
            } else {
                part.words = read_words({0, part.ram});
                for (const auto &[at, words] : joined.names_)
                    if (at == part.ram)
                        part.words = words;
            }
        } else {
            for (size_t i = 0; i < kTextKeyCount; ++i)
                if (token == kTextKeys[i].name) {
                    part.key = int(i);
                    part.words = read_words(text_at(m, TextKey(i)));
                    break;
                }
        }
        parts.push_back(std::move(part));
        remaining = end == std::string_view::npos ? std::string_view{} : remaining.substr(end);
    }

    std::vector<const MessagePart *> ram;
    std::vector<uint16_t> native;
    for (const auto &p : parts) {
        if (p.key < 0)
            ram.push_back(&p);
        native.insert(native.end(), p.words.begin(), p.words.end());
    }
    const auto *words = text_.message_template(message->parts);
    if (!words)
        words = &native;
    const auto append = [&](uint16_t word) {
        if (word & kTextWord) {
            if (const auto *run = text_.run(uint16_t(word & 0x7FFF)))
                joined.text_ += *run;
        } else if (word < 0x200)
            joined.text_ += char32_t(0xE000 + word / 2);
    };
    if (words == &native && !text_.proportional()) {
        for (size_t i = 0; i < parts.size(); ++i) {
            if (message->japanese_break && i == size_t(message->japanese_break))
                joined.text_ += U"{n}";
            if (parts[i].key < 0)
                joined.text_ += kWrapArgumentStart;
            for (uint16_t word : parts[i].words)
                append(word);
            if (parts[i].key < 0)
                joined.text_ += kWrapArgumentEnd;
        }
        return true;
    }
    for (uint16_t word : *words) {
        const int index = word - TextTable::kTemplatePart - 1;
        if (index >= 0 && size_t(index) < ram.size()) {
            if (!text_.proportional())
                joined.text_ += kWrapArgumentStart;
            for (uint16_t value : ram[size_t(index)]->words)
                append(value);
            if (!text_.proportional())
                joined.text_ += kWrapArgumentEnd;
        } else
            append(word);
    }
    return true;
}

uint16_t Session::apply_message_template(Machine &m) {
    const uint16_t timing =
        message_battle_ && text_.proportional() && !m.original_popup_text() ? message_timing_words_ : 0;
    message_timing_words_ = 0;
    message_battle_ = false;
    constexpr uint16_t kQueue = 0xD55A, kQueueWords = 25;
    std::vector<MessagePart> parts;
    parts.swap(message_);
    if (parts.empty())
        return timing;
    std::string signature;
    std::vector<MessagePart> ram;
    for (const MessagePart &p : parts) {
        if (p.key == -2)
            return timing; // a string we can't name: no template can apply
        char buf[8];
        std::snprintf(buf, sizeof buf, "R:%04X", p.ram);
        signature += (signature.empty() ? "" : "+") + (p.key >= 0 ? std::string(kTextKeys[p.key].name) : buf);
        if (p.key < 0)
            ram.push_back(p);
    }
    const std::vector<uint16_t> *words = text_.message_template(signature);
    std::vector<uint16_t> native;
    if (!words) {
        // Preserve complete native messages when no language template was loaded.
        for (const auto &part : parts)
            native.insert(native.end(), part.words.begin(), part.words.end());
        words = &native;
    }
    std::vector<uint16_t> expanded;
    std::u32string presentation;
    auto put = [&](uint16_t w) {
        expanded.push_back(w);
        if (w & kTextWord) {
            if (const auto *run = text_.run(uint16_t(w & 0x7FFF)))
                presentation += *run;
        } else if (w < 0x200)
            presentation += char32_t(0xE000 + w / 2);
    };
    for (uint16_t w : *words) {
        const int part = w - TextTable::kTemplatePart;
        if (part >= 1 && part <= 9) {
            if (size_t(part) > ram.size())
                continue;
            const MessagePart &p = ram[size_t(part - 1)];
            if (!text_.proportional())
                presentation += kWrapArgumentStart;
            for (uint16_t v : p.words)
                put(v);
            if (!text_.proportional())
                presentation += kWrapArgumentEnd;
        } else {
            put(w);
        }
    }
    // A whole announcement may exceed the native 25-word scratch queue. Keep all of its
    // glyphs host-side in one run; never truncate it or write into the neighboring buffers.
    if (expanded.size() > kQueueWords) {
        text_.set_message_run(std::move(presentation));
        expanded = {uint16_t(kTextWord | TextTable::kMessageRun)};
    }
    for (size_t i = 0; i < expanded.size(); ++i)
        m.wr16(0, uint16_t(kQueue + 2 * i), expanded[i]);
    m.wr16(0, uint16_t(kQueue + 2 * expanded.size()), 0xFFFF);
    return timing;
}

void Session::dialogue_frame(Machine &m, uint16_t frame) {
    constexpr uint16_t kScript = 0x5000;
    uint16_t cursor = m.rd16(m.ss(), frame), segment = m.rd16(m.ss(), uint16_t(frame + 2));
    if (segment == kTextSegment && text_.dialogue_end(cursor)) { // back to the ROM script, not an FF argument
        cursor = uint16_t(text_.byte(uint16_t(cursor + 1)) | text_.byte(uint16_t(cursor + 2)) << 8);
        segment = kScript;
        m.wr16(m.ss(), frame, cursor);
        m.wr16(m.ss(), uint16_t(frame + 2), segment);
    }
    if (segment == kScript && !text_.empty()) {
        const int host = text_.dialogue(0x350000u + cursor);
        if (host >= 0) { // a run the language replaces
            cursor = uint16_t(host);
            segment = kTextSegment;
            m.wr16(m.ss(), frame, cursor);
            m.wr16(m.ss(), uint16_t(frame + 2), segment);
        }
    }
    dialogue_cursor_ = uint16_t(cursor + 1);
    // Name expansion reads its member argument before the first glyph's native delay.
    // Keep look-ahead on the name token rather than interpreting that argument as an opcode.
    dialogue_pending_cursor_ = uint16_t(cursor - (m.rd8(m.ss(), uint16_t(frame + 4)) == 1));
    dialogue_segment_ = segment;
    ++dialogue_reads_;
    dialogue_host_ = segment == kTextSegment;
    dialogue_char_ = dialogue_host_ ? text_.dialogue_char(cursor) : 0;
}

namespace {
// Unknown/ambiguous charset entries are archival [XX] escapes, not four display characters.
// Keep their native glyph, just as the name editor does.
char32_t dialogue_native_character(uint8_t code) {
    static const auto charset = Charset::system();
    const auto glyph = utf8_to_u32(charset.decode(code));
    return glyph.size() == 1 ? glyph[0] : char32_t(0xE000 + code);
}
} // namespace
std::u32string Session::dialogue_name(Machine &m, int member) const {
    if (member > 3)
        member = m.rd8(m.ss(), 0xD0E3);
    member = std::clamp(member, 0, 3);
    while (member > 0 && m.rd8(m.ss(), uint16_t(0xD000 + member * 31)) == 255)
        --member;
    auto name = player_names_.rendered(size_t(member));
    if (!name.empty())
        return name;
    for (int i = 0; i < 4; ++i) {
        const auto code = m.rd8(m.ss(), uint16_t(0xD001 + member * 31 + i));
        if (code == 255)
            break;
        name += dialogue_native_character(code);
    }
    while (!name.empty() && (name.back() == U' ' || name.back() == U'　'))
        name.pop_back();
    return name;
}
std::u32string Session::dialogue_tail(Machine &m) const { return dialogue_text(m, dialogue_cursor_); }
std::u32string Session::dialogue_pending(Machine &m) const {
    size_t at = dialogue_pending_cursor_;
    const auto code = m.rd8(dialogue_segment_, uint16_t(at));
    if (code == 0x0C || code == 0x0E)
        ++at;
    return dialogue_text(m, at);
}
std::u32string Session::dialogue_text(Machine &m, size_t at) const {
    std::u32string out;
    // Read the same segment as the native typer: Japanese resource overrides use kTextSegment too.
    // Only proportional runs have a separate Unicode character table.
    if (!dialogue_segment_)
        return out;
    const bool proportional = dialogue_host_ && text_.proportional();
    const auto byte = [&](size_t i) { return m.rd8(dialogue_segment_, uint16_t(i)); };
    for (size_t n = 0; n < 65536 && at < 65536; ++n) {
        const uint16_t pos = uint16_t(at++);
        if (dialogue_host_ && text_.dialogue_end(pos))
            break;
        const auto code = byte(pos);
        if (code == 0x0C || code == 0x0E)
            break;
        if (code == 0x0D || code == 0x0A)
            out += U"{n}";
        else if (code == 0x19)
            out += dialogue_name(m, byte(at++));
        else if (code == 0x16)
            ++at;
        else if (code == 0x13)
            continue;
        else if (code >= 0x20) {
            const auto c = proportional ? text_.dialogue_char(pos) : 0;
            out += c ? c : dialogue_native_character(uint8_t(code - 0x20));
        } else
            break; // never look past script logic
    }
    return out;
}

char32_t Session::dialogue_character(uint8_t code) const {
    if (dialogue_host_ && dialogue_char_)
        return dialogue_char_;
    return dialogue_native_character(code);
}

bool Session::dialogue_glyph(Machine &m, uint8_t code) {
    constexpr uint16_t kCursor = 0xDD28, kMap = 0x2000;
    if (!dialogue_host_ || !text_.proportional())
        return false;
    // the host glyph just read, or a party name's glyph (read from RAM): the WSC font's own (U+E000 + code)
    const char32_t c = dialogue_char_ ? dialogue_char_ : char32_t(0xE000 + code);
    dialogue_char_ = 0;
    const uint16_t pos = m.rd16(m.ss(), kCursor);
    if (pos != line_next_) { // the cursor moved (new line, box, clear): a new line starts here
        line_start_ = pos;
        line_.clear();
    }
    line_ += c;
    const int cells = draw_text_pixels(m, uint16_t(kMap + line_start_), 0x1F00, line_);
    line_next_ = uint16_t(line_start_ + 2 * cells);
    m.wr16(m.ss(), kCursor, line_next_);
    return true;
}

TextPtr name_record(Machine &m, NameTable table, uint8_t id) {
    if (const TextTable *t = m.text_table()) {
        const int off = m.original_popup_text() ? t->original_name(int(table), id) : t->name(int(table), id);
        if (off >= 0)
            return {kTextSegment, uint16_t(off)};
    }
    return {0x9000, uint16_t(table == NameTable::Item ? 0x4BE0 + 8 * id : 0x6FE0 + 9 * id)};
}

int name_run(Machine &m, NameTable table, uint8_t id) {
    if (m.original_popup_text())
        return -1;
    const TextTable *t = m.text_table();
    return t && t->proportional() ? t->name_run(int(table), id) : -1;
}

void text_done(Machine &m, TextKey key) {
    const uint16_t segment = kTextKeys[size_t(key)].segment;
    if (m.ds() == kTextSegment)
        m.ds() = segment;
    if (m.es() == kTextSegment)
        m.es() = segment;
}

} // namespace sawgod::game
