// Runtime word wrapping shared by dialogue, battle log, and translation reports.
#pragma once
#include <string>
#include <string_view>
#include <vector>
namespace sawgod::game {
struct WrapLine {
    std::u32string text;
    size_t begin = 0, end = 0; // source offsets (end excludes trailing break spaces/codes)
};
struct WrapWord {
    std::u32string text;
    int excess = 0;
};
struct WrapResult {
    std::vector<WrapLine> lines;
    std::vector<WrapWord> too_wide;
    bool cell_break = false;
    bool ignored_break = false;
};
// Argument boundaries are presentation-only, removed by the wrapper. They keep a substituted
// name/item/number intact even when it contains spaces, particles, or small kana.
inline constexpr char32_t kWrapArgumentStart = 0xF0000, kWrapArgumentEnd = 0xF0001;
// Names/arguments must already be expanded. {n}/{lf} are hard breaks;
// Cell line breaks (LF/CRLF/CR) are spaces. Pacing codes have no width. Box/clear reset wrapping.
// English battle breaks are hard only after . ! or ?; cell breaks follow the same rule.
WrapResult wrap_text(std::u32string_view text, int width, bool battle = false, bool japanese = false);
} // namespace sawgod::game
