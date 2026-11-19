#include "game/text_wrap.h"
#include "game/player_names.h"
#include "game/text.h"
#include "game/text_render.h"
#include "game/ui_chrome.h"
#include <algorithm>
namespace sawgod::game {
namespace {
char32_t kana(char32_t c) {
    if (c >= 0xE000 && c < 0xE100) {
        static const auto charset = Charset::system();
        const auto decoded = utf8_to_u32(charset.decode(uint8_t(c - 0xE000)));
        if (decoded.size() == 1)
            return decoded[0];
    }
    return c;
}
WrapResult wrap_japanese(std::u32string_view source, int width) {
    struct Unit {
        std::u32string text;
        size_t begin, end;
        bool preferred = false;
    };
    WrapResult result;
    std::vector<Unit> units;
    const auto forbidden_start = [](char32_t c) {
        return std::u32string_view(
                   U"。、！？ーゃゅょっぁぃぅぇぉャュョッァィゥェォゎヮヵヶ）」』】〕］｝!?,.")
                   .find(kana(c)) != std::u32string_view::npos;
    };
    const auto forbidden_end = [](char32_t c) {
        return std::u32string_view(U"（「『【〔［｛(").find(kana(c)) != std::u32string_view::npos;
    };
    const auto blank = [](char32_t c) { return c == U' ' || c == U'　' || c == U'\t' || c == 0xE0D9; };
    const auto join = [&](size_t a, size_t b) {
        std::u32string text;
        for (size_t i = a; i < b; ++i)
            text += units[i].text;
        while (!text.empty() && blank(text.back()))
            text.pop_back();
        return text;
    };
    const auto flush = [&](size_t at) {
        for (size_t first = 0; first < units.size();) {
            while (first < units.size() && blank(units[first].text.front()))
                ++first;
            if (first == units.size())
                break;
            size_t last = first + 1, preferred = 0;
            while (last < units.size() && Chrome::text_width(join(first, last + 1)) <= width)
                ++last;
            // A legal boundary cannot strand punctuation/small kana or an opening bracket.
            const auto legal = [&](size_t end) {
                size_t next = end;
                while (next < units.size() && blank(units[next].text.front()))
                    ++next;
                return !forbidden_end(units[end - 1].text.back()) &&
                       (next == units.size() || !forbidden_start(units[next].text.front()));
            };
            if (last < units.size()) {
                for (size_t i = first + 1; i <= last; ++i)
                    if (units[i - 1].preferred && legal(i))
                        preferred = i;
                if (preferred)
                    last = preferred;
                else {
                    while (last > first + 1 && !legal(last))
                        --last;
                    // An indivisible argument plus attached punctuation may itself exceed the box.
                    while (last < units.size() && !legal(last))
                        ++last;
                }
            }
            auto text = join(first, last);
            const int excess = Chrome::text_width(text) - width;
            if (excess > 0)
                result.too_wide.push_back({text, excess});
            result.lines.push_back({std::move(text), units[first].begin, units[last - 1].end});
            first = last;
        }
        if (units.empty())
            result.lines.push_back({{}, at, at});
        units.clear();
    };
    bool boundary = false;
    for (size_t i = 0; i < source.size();) {
        const size_t begin = i;
        if (source[i] == kWrapArgumentStart) {
            Unit unit{{}, i++, i, false};
            while (i < source.size() && source[i] != kWrapArgumentEnd)
                unit.text += source[i++];
            if (i < source.size())
                ++i;
            unit.end = i;
            if (!unit.text.empty())
                units.push_back(std::move(unit));
            boundary = false;
            continue;
        }
        if (source[i] == U'{') {
            const auto end = source.find(U'}', i);
            if (end != source.npos) {
                const auto token = source.substr(i, end - i + 1);
                if (token == U"{n}" || token == U"{lf}" || token == U"{clear}" || token == U"{box}") {
                    flush(i);
                    boundary = true;
                } else if (token != U"{wait}" && !token.starts_with(U"{pause:")) {
                    units.push_back({std::u32string(token), i, end + 1, false});
                    boundary = false;
                }
                i = end + 1;
                continue;
            }
        }
        const char32_t c = source[i++];
        if (c == U'\n' || c == U'\r') {
            flush(begin);
            boundary = true;
            if (c == U'\r' && i < source.size() && source[i] == U'\n')
                ++i;
            continue;
        }
        boundary = false;
        // Latin runs (mode names and numeric values) remain indivisible too.
        if (!units.empty() && c < 128 && !blank(c) && units.back().text.back() < 128 &&
            !blank(units.back().text.back()) && units.back().end == begin) {
            units.back().text += c;
            units.back().end = i;
        } else
            units.push_back({std::u32string(1, c), begin, i, false});
        // Preferred breaks: a space, sentence punctuation, or a particle that ends a phrase (followed by a
        // space, a break, a substituted name or the end) -- never the same kana inside a word (へんかした).
        const auto phrase_end = [&] {
            if (i >= source.size())
                return true;
            const char32_t next = source[i];
            return blank(next) || next == kWrapArgumentStart || next == U'{' || next == U'\n' ||
                   next == U'\r';
        };
        if (blank(c) || std::u32string_view(U"。、！？").find(kana(c)) != std::u32string_view::npos ||
            (std::u32string_view(U"はをにでがのともへや").find(kana(c)) != std::u32string_view::npos &&
             phrase_end()))
            units.back().preferred = true;
    }
    if (!units.empty() || result.lines.empty() || boundary)
        flush(source.size());
    return result;
}
} // namespace
WrapResult wrap_text(std::u32string_view source, int width, bool battle, bool japanese) {
    const bool kana_text = !battle && std::any_of(source.begin(), source.end(), [](char32_t c) {
        return (c >= 0x3040 && c <= 0x30FF) || (c >= 0xE000 && c < 0xE0A0);
    });
    if (japanese || kana_text)
        return wrap_japanese(source, width);
    struct Word {
        std::u32string text;
        size_t begin, end;
        bool space;
    };
    WrapResult result;
    std::vector<Word> words;
    bool space = false;
    const auto close = [](char32_t c) {
        return std::u32string_view(U".,!?…)”’:。 、！？）」』").find(c) != std::u32string_view::npos;
    };
    const auto open = [](char32_t c) {
        return std::u32string_view(U"(“‘「『").find(c) != std::u32string_view::npos;
    };
    const auto join = [&](size_t first, size_t last) {
        std::u32string out;
        for (size_t i = first; i < last; ++i) {
            if (i > first && words[i].space)
                out += U' ';
            out += words[i].text;
        }
        return out;
    };
    const auto flush = [&](size_t at) {
        // Attach punctuation and opening quotes to their neighbors before choosing breaks.
        for (size_t i = 1; i < words.size();) {
            const auto prefix = source.substr(words.front().begin, words[i - 1].end - words.front().begin);
            if (close(words[i].text.front()) || open(words[i - 1].text.back()) ||
                (words[i - 1].text.back() == U'"' && std::count(prefix.begin(), prefix.end(), U'"') % 2)) {
                words[i - 1].text += words[i].space ? U" " : U"";
                words[i - 1].text += words[i].text;
                words[i - 1].end = words[i].end;
                words.erase(words.begin() + i);
            } else
                ++i;
        }
        std::vector<std::pair<size_t, size_t>> rows;
        size_t first = 0;
        while (first < words.size()) {
            size_t last = first + 1;
            while (last < words.size() && Chrome::text_width(join(first, last + 1)) <= width)
                ++last;
            rows.emplace_back(first, last);
            first = last;
        }
        if (rows.size() > 1 && rows.back().second - rows.back().first == 1) {
            auto &prev = rows[rows.size() - 2];
            auto &last = rows.back();
            if (prev.second - prev.first > 1 &&
                Chrome::text_width(join(prev.second - 1, last.second)) <= width) {
                --prev.second;
                --last.first;
            }
        }
        for (auto [a, b] : rows)
            result.lines.push_back({join(a, b), words[a].begin, words[b - 1].end});
        if (words.empty())
            result.lines.push_back({{}, at, at});
        for (const auto &w : words)
            if (const int n = Chrome::text_width(w.text) - width; n > 0)
                result.too_wide.push_back({w.text, n});
        words.clear();
        space = false;
    };
    bool boundary = false;
    const auto sentence_end = [&] {
        if (words.empty() || words.back().text.empty())
            return false;
        return std::u32string_view(U".!?").find(words.back().text.back()) != std::u32string_view::npos;
    };
    for (size_t i = 0; i < source.size();) {
        if (source[i] == U'{') {
            const size_t end = source.find(U'}', i);
            if (end != std::u32string_view::npos) {
                const auto token = source.substr(i, end - i + 1);
                if (token == U"{n}" || token == U"{lf}" || token == U"{clear}" || token == U"{box}") {
                    if (battle && (token == U"{n}" || token == U"{lf}") && !sentence_end()) {
                        space = true;
                        result.ignored_break = true;
                    } else {
                        flush(i);
                        boundary = true;
                    }
                } else if (token != U"{wait}" && !token.starts_with(U"{pause:")) {
                    // Unknown parameters stay visible so reports can diagnose them.
                    for (size_t p = i; p <= end; ++p) {
                        if (words.empty() || space) {
                            words.push_back({{}, p, p, false});
                            words.back().space = space;
                            space = false;
                        }
                        words.back().text += source[p];
                        words.back().end = p + 1;
                    }
                    boundary = false;
                }
                i = end + 1;
                continue;
            }
        }
        const char32_t c = source[i];
        if (c == U' ' || c == U'\n' || c == U'\r' || c == U'\t') {
            const bool cell = c == U'\n' || c == U'\r';
            result.cell_break |= cell && (!battle || !sentence_end());
            if (battle && cell && sentence_end()) {
                flush(i);
                boundary = true;
            }
            if (c == U'\r' && i + 1 < source.size() && source[i + 1] == U'\n')
                ++i;
            space = true;
            ++i;
            continue;
        }
        boundary = false;
        if (words.empty() || space || (words.back().text.back() == U'—' && c != U'~' && c != U'\u00A0')) {
            words.push_back({{}, i, i, space});
            space = false;
        }
        words.back().text += c == U'~' || c == U'\u00A0' ? U' ' : c;
        words.back().end = ++i;
    }
    if (!words.empty() || result.lines.empty() || boundary)
        flush(source.size());
    return result;
}
} // namespace sawgod::game
