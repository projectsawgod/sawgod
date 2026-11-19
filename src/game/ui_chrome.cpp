// Draws shared host UI frames, text, and cursors in the active graphics style.
#include "game/ui_chrome.h"

#include <algorithm>
#include <array>
#include <deque>
#include <functional>
#include <optional>
#include <string>

#include "engine/button_icons.h"
#include "game/machine.h"
#include "game/player_names.h"
#include "game/text.h"
#include "game/text_render.h"

namespace sawgod::game {
namespace {
constexpr int kWidth = emu::Wsc::kWidth;
constexpr int kFrame = 0x1D5;                // 3x3: 0x1D5-0x1D7 top, 0x1D8-0x1DA middle, 0x1DB-0x1DD bottom
constexpr int kNormal = 15, kInactive = 7;   // window palettes
constexpr int kArrow = 0x1EE, kHand = 0x1FB; // sprite tiles (the hand is 2x2 from here)
constexpr int kFontTile = 0x100;             // + character code: the WSC font
constexpr uint8_t kStroke = 2, kShadow = 1;  // the font's palette indices (as text_render)
constexpr int kM5x7Left =
    1; // glyphs sit a pixel right of the pen (as text_render's m5x7; WSC kana too, to line up)
constexpr char32_t kWscGlyph = 0xE000;
constexpr int kThinBand = 2;       // GB windows: the line, a grey band, a second line (black-grey-black)
constexpr bool kGbBarFont = false; // GB bar labels: m5x7 (true) or the compact numerals
constexpr int kContrast =
    96; // a role color closer than this (luma, 0-255) to the window's fill reads as Text

int luma(uint32_t c) { return (30 * int(c >> 16 & 255) + 59 * int(c >> 8 & 255) + 11 * int(c & 255)) / 100; }

// Which of a window dimension's three tile parts (start, repeated middle, end) covers pixel p, and the row or
// column in that tile. Under 16 pixels the start and end tiles meet in the middle.
std::pair<int, int> slice(int p, int n) {
    if (n - p <= 8 && (n >= 16 || p >= n / 2))
        return {2, 8 - (n - p)};
    if (p < 8)
        return {0, p};
    return {1, (p - 8) % 8};
}

std::u32string normalized(std::u32string_view s) { return with_wsc_glyphs(s); }
} // namespace

thread_local std::function<void(const Chrome::Draw &)> Chrome::observe;

Chrome::Chrome(emu::Wsc &w, std::vector<uint32_t> &picture, emu::Wsc::ColorMode mode, bool second)
    : w_(w), px_(picture), mode_(mode), second_(second), memory_data_(w.presentation_memory(second).data()) {
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            ready_ |= index(kFrame, x, y) != 0;
            fill_ |= uint16_t(1u << index(kFrame + 4, x, y));
        }
    fill_ &= uint16_t(~1u);
    // The fill's most common color stands for the background text and roles must read against.
    std::array<int, 16> count{};
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            ++count[index(kFrame + 4, x, y)];
    fill_index_ = uint8_t(std::max_element(count.begin(), count.end()) - count.begin());
    // The WSC font draws a shadow (index 1); the GB font has none, and its text goes without.
    // Counted over the kana (codes 0x0B-0x5F): a few GB tiles there use index 1 for other things.
    struct FontShadow {
        std::array<uint8_t, (0x60 - 0x0B) * 32> bytes;
        bool shadow;
    };
    static thread_local std::deque<FontShadow> fonts;
    const auto *font = memory_data_ + 0x4000 + (kFontTile + 0x0B) * 32;
    for (const auto &candidate : fonts)
        if (std::equal(candidate.bytes.begin(), candidate.bytes.end(), font)) {
            shadow_ = candidate.shadow;
            return;
        }
    int strokes = 0, shadows = 0;
    FontShadow fresh;
    std::copy_n(font, fresh.bytes.size(), fresh.bytes.begin());
    for (const uint8_t b : fresh.bytes) {
        strokes += (b >> 4) == kStroke;
        strokes += (b & 15) == kStroke;
        shadows += (b >> 4) == kShadow;
        shadows += (b & 15) == kShadow;
    }
    shadow_ = fresh.shadow = shadows * 2 > strokes;
    if (fonts.size() == 8)
        fonts.pop_front();
    fonts.push_back(std::move(fresh));
}

uint8_t Chrome::index(int tile, int x, int y) const {
    const uint8_t b = memory(uint16_t(0x4000 + tile * 32 + y * 4 + x / 2));
    return x & 1 ? b & 15 : b >> 4;
}

uint32_t Chrome::rgb(int palette, int index, bool sprite) const {
    const size_t key = size_t(palette * 16 + index + (sprite ? 256 : 0));
    const uint64_t bit = uint64_t(1) << (key & 63);
    if (colors_valid_[key >> 6] & bit)
        return colors_[key];
    uint16_t c = w_.presentation_palette_color(palette, index, second_, sprite);
    // Compact menu and save portraits use lower palettes even though they are character sprites.
    // Give those the same four-shade treatment as the runtime's upper sprite palettes.
    if (second_ && sprite && palette < 8) {
        const int lum = (30 * (c >> 8 & 15) + 59 * (c >> 4 & 15) + 11 * (c & 15)) / 100;
        c = lum >= 13 ? 0xFFF : lum >= 8 ? 0xAAA : lum >= 3 ? 0x555 : 0;
    }
    colors_valid_[key >> 6] |= bit;
    return colors_[key] = shown(c);
}

uint32_t Chrome::shown(uint16_t c) const {
    const uint32_t r = c >> 8 & 15, g = c >> 4 & 15, b = c & 15;
    if (mode_ == emu::Wsc::ColorMode::Lcd)
        return (13 * r + 2 * g + b) << 16 | (12 * g + 4 * b) << 8 | (3 * r + 2 * g + 11 * b);
    return r * 17 << 16 | g * 17 << 8 | b * 17;
}

// Like PUSH ANY BUTTON, fully out at strength 0: WSC/Enhanced dim toward the black title page; GB steps
// through its greys to the white page (no ink darker than the step).
uint32_t Chrome::title_hint_faded(uint32_t full) const {
    if (title_hint_strength_ >= 100)
        return full;
    if (second_) {
        static constexpr uint16_t kGbSteps[] = {0xFFF, 0xAAA, 0x555, 0x000};
        const auto step = shown(kGbSteps[std::clamp(title_hint_strength_ * 4 / 100, 0, 3)]);
        uint32_t result = 0;
        for (int shift = 0; shift < 24; shift += 8)
            result |= std::max(full >> shift & 255, step >> shift & 255) << shift;
        return result;
    }
    const auto page = shown(0);
    uint32_t result = 0;
    for (int shift = 0; shift < 24; shift += 8)
        result |= ((title_hint_strength_ * (full >> shift & 255) +
                    (100 - title_hint_strength_) * (page >> shift & 255)) /
                   100)
                  << shift;
    return result;
}

uint32_t Chrome::color(Ink ink, Window style) const {
    // The title's picture lettering uses palette 14: white strokes and a dark edge.
    // GB keeps its native black lettering on the light title page.
    if (ink == Ink::Title || ink == Ink::TitleShadow)
        return rgb(14, ink == Ink::Title ? 5 : 1, false);
    if (ink == Ink::TitleHint || ink == Ink::TitleHintShadow)
        return title_hint_faded(color(ink == Ink::TitleHint ? Ink::Title : Ink::TitleShadow, style));
    if (ink == Ink::TitleVersion) {
        const auto title = color(Ink::Title, style);
        const auto gray = shown(0x555); // GB's dark gray, fading toward its white title page
        uint32_t result = 0;
        for (int shift = 0; shift < 24; shift += 8) {
            const auto channel = title >> shift & 255;
            result |= (second_ ? std::max(channel, gray >> shift & 255) : channel * 128 / 255) << shift;
        }
        return result;
    }
    if (ink == Ink::Higher || ink == Ink::Lower)
        return second_ ? color(Ink::Text, style) : ink == Ink::Higher ? 0x91E061 : 0xFF7860;
    // Opening pictures use their native palette, including the original fades.
    if (ink == Ink::IntroCaption || ink == Ink::IntroClosing || ink == Ink::IntroPrologue ||
        ink == Ink::IntroEdge)
        return rgb(4, ink == Ink::IntroClosing ? 15 : ink == Ink::IntroPrologue ? 2 : 1, false);
    const int pal = style == Window::Inactive ? kInactive : kNormal;
    if (ink == Ink::Secondary) {
        // Keep the text/shadow palette's fade and color mode. GB uses dark gray on paper,
        // with no shadow; WSC/Enhanced uses a dimmer stroke with the usual text shadow.
        const auto text = color(Ink::Text, style);
        const auto toward = second_ ? rgb(pal, fill_index_, false) : color(Ink::Shadow, style);
        uint32_t result = 0;
        for (int shift = 0; shift < 24; shift += 8)
            result |= ((2 * (text >> shift & 255) + (toward >> shift & 255)) / 3) << shift;
        return result;
    }
    static constexpr uint8_t kIndex[] = {kStroke, kShadow, 7, 12, 15}; // Ink order
    const uint32_t c = rgb(pal, kIndex[size_t(ink)], false);
    if (ink == Ink::Muted || ink == Ink::Accent || ink == Ink::Heal)
        if (std::abs(luma(c) - luma(rgb(pal, fill_index_, false))) < kContrast)
            return color(Ink::Text, style);
    return c;
}

void Chrome::pixel(int x, int y, uint32_t color, int alpha) {
    if (x < clip_left_ || x >= clip_right_ || y < clip_top_ || y >= clip_bottom_)
        return;
    if (!opacity_)
        return;
    alpha = alpha * opacity_ / 100;
    if (w_.ui_coverage)
        (*w_.ui_coverage)[size_t(y * kWidth + x)] = 255;
    if (!second_ && w_.ui_fill)
        w_.ui_fill->owner[size_t(y * kWidth + x)] = 0;
    if (!second_ && w_.ui_text) {
        w_.ui_text->pixels[size_t(y * kWidth + x)] = {};
        w_.ui_text->bars.pixels[size_t(y * kWidth + x)] = {};
        w_.ui_text->composed = true;
    }
    auto &p = px_[size_t(y * kWidth + x)];
    if (alpha >= 100) {
        p = color;
        return;
    }
    uint32_t mixed = 0;
    for (int shift = 0; shift < 24; shift += 8)
        mixed |= uint32_t(((color >> shift & 255) * alpha + (p >> shift & 255) * (100 - alpha)) / 100)
                 << shift;
    p = mixed;
}

// Some native icons encode their clear area with a window-fill index instead of index 0.
// Keep the native pixel, but retain its backing window's gradient ownership. Real foreground
// writes still invalidate both records; character art and opaque tiles never take this path.
void Chrome::icon_pixel(int x, int y, int palette, int index, bool sprite) {
    const bool inside = x >= clip_left_ && x < clip_right_ && y >= clip_top_ && y < clip_bottom_;
    const size_t at = inside ? size_t(y * kWidth + x) : 0;
    auto *fill = !second_ ? w_.ui_fill : nullptr;
    const auto owner = fill && inside && (palette == kNormal || palette == kInactive) && (fill_ >> index & 1)
                           ? fill->visible(at, px_[at])
                           : 0;
    // A translucent fill must keep its already blended underlay, even with Menu Fill Off.
    if (owner && fill->windows[owner - 1].alpha < 100)
        return;
    pixel(x, y, rgb(palette, index, sprite));
    if (owner) {
        fill->owner[at] = owner;
        fill->color[at] = px_[at];
    }
}

void Chrome::text_pixel(int x, int y, uint32_t color, bool shadow, bool casts_shadow) {
    if (x < clip_left_ || x >= clip_right_ || y < clip_top_ || y >= clip_bottom_)
        return;
    const size_t at = size_t(y * kWidth + x);
    const auto beneath = px_[at];
    // Shadow pixels retain the fill below them. Acceptance still checks the final text color,
    // so a later icon/window write invalidates both records, even if its color is identical.
    const auto owner = !second_ && w_.ui_fill ? w_.ui_fill->owner[at] : 0;
    pixel(x, y, color);
    if (!second_ && w_.ui_text) {
        w_.ui_text->pixels[at] = {shadow ? emu::Wsc::UiText::Shadow : emu::Wsc::UiText::Stroke, color,
                                  beneath, casts_shadow, soft_shadow_};
        if (shadow && w_.ui_fill)
            w_.ui_fill->owner[at] = owner;
    }
}

void Chrome::clip(int x, int y, int width, int height, const std::function<void()> &draw) {
    const auto old = std::array{clip_left_, clip_top_, clip_right_, clip_bottom_};
    clip_left_ = std::max(clip_left_, x);
    clip_top_ = std::max(clip_top_, y);
    clip_right_ = std::min(clip_right_, x + std::max(0, width));
    clip_bottom_ = std::min(clip_bottom_, y + std::max(0, height));
    const auto restore = [&] {
        clip_left_ = old[0], clip_top_ = old[1], clip_right_ = old[2], clip_bottom_ = old[3];
    };
    try {
        draw();
    } catch (...) {
        restore();
        throw;
    }
    restore();
}

void Chrome::tile(uint16_t entry, int x, int y, bool opaque, bool sprite, bool character) {
    const bool observing = observe && observed_output_ && tiles_observed_;
    Draw draw{Draw::Kind::Tile, x, y, 8, 8, 8, false, {}};
    const int t = (entry & 0x1FF) | (entry & 0x2000 && !sprite ? 0x200 : 0);
    const int pal = sprite ? 8 + (entry >> 9 & 7) : entry >> 9 & 15;
    for (int yy = 0; yy < 8; ++yy)
        for (int xx = 0; xx < 8; ++xx) {
            const int i = index(t, entry & 0x4000 ? 7 - xx : xx, entry & 0x8000 ? 7 - yy : yy);
            if (observing && i && x + xx >= clip_left_ && x + xx < clip_right_ && y + yy >= clip_top_ &&
                y + yy < clip_bottom_)
                draw.ink.push_back(uint32_t((y + yy) * kWidth + x + xx));
            if (i || opaque) {
                if (opaque || character)
                    pixel(x + xx, y + yy, rgb(pal, i, sprite || character));
                else
                    icon_pixel(x + xx, y + yy, pal, i, sprite);
            }
        }
    if (observing)
        observe(draw);
}

void Chrome::tile(const uint8_t *data, const std::array<uint16_t, 16> &colors, int palette, int x, int y,
                  bool hflip, bool vflip) {
    const bool observing = observe && observed_output_ && tiles_observed_;
    Draw draw{Draw::Kind::Tile, x, y, 8, 8, 8, false, {}};
    for (int yy = 0; yy < 8; ++yy)
        for (int xx = 0; xx < 8; ++xx) {
            const int tx = hflip ? 7 - xx : xx, ty = vflip ? 7 - yy : yy;
            const uint8_t b = data[ty * 4 + tx / 2];
            const int i = tx & 1 ? b & 15 : b >> 4;
            if (!i)
                continue;
            if (observing && x + xx >= clip_left_ && x + xx < clip_right_ && y + yy >= clip_top_ &&
                y + yy < clip_bottom_)
                draw.ink.push_back(uint32_t((y + yy) * kWidth + x + xx));
            uint16_t c = colors[size_t(i)] & 0x0FFF;
            if (second_ && w_.second() && w_.upper_palette_greys && palette >= 8)
                c = emu::Wsc::gb_grey(c);
            pixel(x + xx, y + yy, shown(c));
        }
    if (observing)
        observe(draw);
}

uint32_t Chrome::fill_owner(int y, int height, int palette, int alpha) {
    if (w_.ui_fill) {
        uint32_t light = 0, dark = 0xFFFFFF;
        for (int i = 1; i < 16; ++i)
            if (fill_ >> i & 1) {
                const auto c = rgb(palette, i, false);
                if (luma(c) > luma(light))
                    light = c;
                if (luma(c) < luma(dark))
                    dark = c;
            }
        uint32_t deep = 0;
        for (int shift = 0; shift < 24; shift += 8)
            deep |= ((dark >> shift & 255) * 9 / 10) << shift;
        const emu::Wsc::UiFill::Window entry{texture_anchored_ ? fill_top_ : y,
                                             texture_anchored_ ? fill_bottom_ : y + height, light, deep,
                                             alpha};
        auto &windows = w_.ui_fill->windows;
        auto found = std::find(windows.begin(), windows.end(), entry);
        if (found == windows.end()) {
            windows.push_back(entry);
            return uint32_t(windows.size());
        } else
            return uint32_t(found - windows.begin() + 1);
    }
    return 0;
}

void Chrome::window_fill(int x, int y, int width, int height, Window style) {
    if (!ready_ || width <= 0 || height <= 0)
        return;
    if (second_) {
        backdrop(x, y, width, height);
        return;
    }
    const int pal = style == Window::Inactive ? kInactive : kNormal;
    const auto owner = fill_owner(y, height, pal, 100);
    const auto wrap = [](int v) { return (v % 8 + 8) % 8; };
    for (int yy = y; yy < y + height; ++yy)
        for (int xx = x; xx < x + width; ++xx) {
            const int i = index(kFrame + 4, wrap(xx), wrap(yy));
            pixel(xx, yy, rgb(pal, i, false));
            if (owner && xx >= clip_left_ && xx < clip_right_ && yy >= clip_top_ && yy < clip_bottom_) {
                const size_t at = size_t(yy * kWidth + xx);
                w_.ui_fill->owner[at] = owner;
                w_.ui_fill->color[at] = px_[at];
            }
        }
}

void Chrome::window(int x, int y, int width, int height, Window style, int transparency) {
    if (!ready_ || width <= 0 || height <= 0)
        return;
    if (observe && observed_output_)
        observe({Draw::Kind::Window, x, y, width, height, width, false, {}});

    const int pal = style == Window::Inactive ? kInactive : kNormal;
    auto at = [&](int xx, int yy) {
        const auto [row, ty] = slice(yy, height);
        const auto [column, tx] = slice(xx, width);
        return index(kFrame + row * 3 + column, tx, ty);
    };
    if (second_) {
        thin_window(x, y, width, height, at);
        return;
    }
    // Nine-slice sampling is also geometry: resolve it once for each atlas/size,
    // then apply the current palette, opacity, clip and texture anchoring below.
    struct WindowPixels {
        int width, height;
        std::array<uint8_t, 9 * 32> atlas;
        std::vector<uint8_t> indices;
    };
    static thread_local std::deque<WindowPixels> windows;
    std::array<uint8_t, 9 * 32> atlas;
    std::copy_n(memory_data_ + 0x4000 + kFrame * 32, atlas.size(), atlas.begin());
    const WindowPixels *sample = nullptr;
    for (const auto &candidate : windows)
        if (candidate.width == width && candidate.height == height && candidate.atlas == atlas) {
            sample = &candidate;
            break;
        }
    if (!sample) {
        WindowPixels fresh{width, height, atlas, std::vector<uint8_t>(size_t(width * height))};
        for (int yy = 0; yy < height; ++yy)
            for (int xx = 0; xx < width; ++xx)
                fresh.indices[size_t(yy * width + xx)] = uint8_t(at(xx, yy));
        if (windows.size() == 32)
            windows.pop_front();
        windows.push_back(std::move(fresh));
        sample = &windows.back();
    }
    const auto *indices = sample->indices.data();
    const int alpha = 100 - std::clamp(transparency, 0, 100);
    const uint32_t owner = fill_owner(y, height, pal, alpha);
    // A translucent window over another window's fill shows its own gradient there, opaque: the fill beneath
    // is itself replaced, so blending with the native pixel would bring the old tile back.
    uint32_t opaque = 0;
    const auto opaque_owner = [&] {
        if (!opaque) {
            auto entry = w_.ui_fill->windows[owner - 1];
            entry.alpha = 100;
            auto &windows = w_.ui_fill->windows;
            auto found = std::find(windows.begin(), windows.end(), entry);
            if (found == windows.end()) {
                windows.push_back(entry);
                opaque = uint32_t(windows.size());
            } else
                opaque = uint32_t(found - windows.begin() + 1);
        }
        return opaque;
    };
    const auto wrap = [](int v) { return ((v % 8) + 8) % 8; };
    for (int yy = 0; yy < height; ++yy)
        for (int xx = 0; xx < width; ++xx)
            if (int i = indices[yy * width + xx]) {
                if (texture_anchored_ && fill_ >> i & 1)
                    i = index(kFrame + 4, wrap(x + xx - texture_x_), wrap(y + yy - texture_y_));
                const bool inside = x + xx >= clip_left_ && x + xx < clip_right_ && y + yy >= clip_top_ &&
                                    y + yy < clip_bottom_;
                const size_t at = inside ? size_t((y + yy) * kWidth + x + xx) : 0;
                const bool over_fill = owner && inside && alpha < 100 && w_.ui_fill->owner[at];
                const uint32_t beneath = inside ? px_[at] : 0;
                pixel(x + xx, y + yy, rgb(pal, i, false), fill_ >> i & 1 ? alpha : 100);
                if (owner && (fill_ >> i & 1) && inside) {
                    w_.ui_fill->beneath[at] = beneath;
                    w_.ui_fill->owner[at] = over_fill ? opaque_owner() : owner;
                    w_.ui_fill->color[at] = px_[at];
                }
            }
}

void Chrome::backdrop(int x, int y, int width, int height) {
    if (!ready_ || !second_)
        return;
    uint32_t light = 0;
    for (int i = 0; i < 16; ++i)
        if (const uint32_t c = rgb(kNormal, i, false); luma(c) > luma(light))
            light = c;
    for (int yy = y; yy < y + height; ++yy)
        for (int xx = x; xx < x + width; ++xx)
            pixel(xx, yy, light);
}

// Compose only native window slices. Background and rounded connector stay inside chrome,
// including the GB frame's wider inner band; layouts use identical coordinates in both pictures.
void Chrome::tabbed_window(int x, int y, int width, int height, int divider, int split, int active) {
    if (!ready_ || divider < 16 || height < divider + 16 || split < 16 || split > width - 16)
        return;
    // One background pattern for tabs and page, in the page's phase (the tab's fill used to restart at the
    // seam below the active tab).
    texture_anchored_ = true, texture_x_ = x, texture_y_ = y + divider;
    fill_top_ = y, fill_bottom_ = y + height;
    auto *record = second_ ? nullptr : w_.ui_fill;
    auto *text_record = second_ ? nullptr : w_.ui_text;
    window(x, y, width, height);
    const auto fill = px_;
    const auto fill_record = record ? std::optional(*record) : std::nullopt;
    const auto fill_text = text_record ? std::optional(*text_record) : std::nullopt;
    if (active == 0) {
        window(x + split, y, width - split, divider + 8);
        window(x, y, split + 4, divider + 8);
    } else {
        window(x, y, split, divider + 8);
        window(x + split - 4, y, width - split + 4, divider + 8);
    }
    const auto tabs = px_;
    const auto tabs_record = record ? std::optional(*record) : std::nullopt;
    const auto tabs_text = text_record ? std::optional(*text_record) : std::nullopt;
    window(x, y + divider, width, height - divider);
    const auto copy =
        [&](const std::vector<uint32_t> &source, const std::optional<emu::Wsc::UiFill> &source_record,
            const std::optional<emu::Wsc::UiText> &source_text, int x0, int y0, int x1, int y1) {
            for (int yy = std::max(clip_top_, y0); yy < std::min(clip_bottom_, y1); ++yy)
                for (int xx = std::max(clip_left_, x0); xx < std::min(clip_right_, x1); ++xx) {
                    const size_t at = size_t(yy * kWidth + xx);
                    px_[at] = source[at];
                    if (text_record)
                        text_record->pixels[at] = source_text->pixels[at];
                    if (record) {
                        record->owner[at] = source_record->owner[at];
                        record->color[at] = source_record->color[at];
                        record->beneath[at] = source_record->beneath[at];
                    }
                }
        };
    copy(tabs, tabs_record, tabs_text, active == 0 ? x : x + split - 4, y + divider,
         active == 0 ? x + split + 4 : x + width, y + divider + 4);
    auto corner_picture = fill;
    Chrome corner(w_, corner_picture, mode_, second_);
    corner.observed_output_ = false; // only the copied connector fragment reaches the output
    corner.texture_anchored_ = true, corner.texture_x_ = texture_x_, corner.texture_y_ = texture_y_;
    corner.fill_top_ = fill_top_, corner.fill_bottom_ = fill_bottom_;
    // The helper paints a private picture: only copied fragments may claim output ownership.
    auto corner_record = record ? std::optional(*record) : std::nullopt;
    auto corner_text = text_record ? std::optional(*text_record) : std::nullopt;
    auto corner_coverage = w_.ui_coverage ? std::optional(*w_.ui_coverage) : std::nullopt;
    auto *coverage = w_.ui_coverage;
    if (coverage)
        w_.ui_coverage = &*corner_coverage;
    if (text_record)
        w_.ui_text = &*corner_text;
    if (record)
        w_.ui_fill = &*corner_record;
    const int corner_x = x + split + (active == 0 ? (second_ ? 1 : 2) : (second_ ? -9 : -10));
    const int corner_y = y + divider - (second_ ? 5 : 6);
    corner.window(active == 0 ? corner_x : corner_x - 24, y + divider - 30, 32, second_ ? 33 : 32);
    if (!second_) {
        // A concave join reverses the convex slice's bevel: the tab's vertical
        // black pipe meets the body's horizontal light gray pipe.
        const auto black = rgb(kNormal, 6, false), gray = rgb(kNormal, 7, false);
        for (int yy = std::max(clip_top_, corner_y); yy < std::min(clip_bottom_, corner_y + 8); ++yy)
            for (int xx = std::max(clip_left_, corner_x); xx < std::min(clip_right_, corner_x + 8); ++xx) {
                auto &pixel = corner_picture[size_t(yy * kWidth + xx)];
                if (pixel == black)
                    pixel = gray;
                else if (pixel == gray)
                    pixel = black;
            }
    }
    if (coverage)
        w_.ui_coverage = coverage;
    if (record)
        w_.ui_fill = record;
    if (text_record)
        w_.ui_text = text_record;
    copy(fill, fill_record, fill_text, active == 0 ? x + split : corner_x, corner_y,
         active == 0 ? corner_x + 8 : x + split, y + divider + 4);
    copy(corner_picture, corner_record, corner_text, corner_x, corner_y, corner_x + 8, corner_y + 8);
    if (text_record && record) {
        const auto owner = fill_owner(y, height, kNormal, 100);
        const auto &fill_window = record->windows[owner - 1];
        text_record->corners.push_back({corner_x, corner_y, active != 0, fill_window.top, fill_window.bottom,
                                        fill_window.light, fill_window.dark, rgb(kNormal, 2, false),
                                        rgb(kNormal, 6, false), rgb(kNormal, 7, false)});
        const auto id = uint32_t(text_record->corners.size());
        for (int yy = std::max(clip_top_, corner_y); yy < std::min(clip_bottom_, corner_y + 8); ++yy)
            for (int xx = std::max(clip_left_, corner_x); xx < std::min(clip_right_, corner_x + 8); ++xx) {
                const size_t at = size_t(yy * kWidth + xx);
                text_record->pixels[at] = {emu::Wsc::UiText::FrameCorner, px_[at], 0, false, 0, id};
            }
    }
    texture_anchored_ = false;
}

void Chrome::speaker_window(int x, int y, int width, int height, int tab_width) {
    const auto scene = px_;
    auto *record = second_ ? nullptr : w_.ui_fill;
    auto *text_record = second_ ? nullptr : w_.ui_text;
    const auto scene_record = record ? std::optional(*record) : std::nullopt;
    const auto scene_text = text_record ? std::optional(*text_record) : std::nullopt;
    // Coverage too: what is given back beside the tab is the scene, not window (the Enhanced field shows
    // there).
    const auto scene_coverage = w_.ui_coverage ? std::optional(*w_.ui_coverage) : std::nullopt;
    const int split = std::clamp(tab_width, 16, width - 16), divider = 16;
    tabbed_window(x, y, width, height, divider, split, 0);
    const auto restore = [&](int xx, int yy) {
        if (xx >= clip_left_ && xx < clip_right_ && yy >= clip_top_ && yy < clip_bottom_) {
            const size_t at = size_t(yy * kWidth + xx);
            px_[at] = scene[at];
            if (scene_coverage)
                (*w_.ui_coverage)[at] = (*scene_coverage)[at];
            if (text_record) {
                const auto corner = text_record->pixels[at].corner;
                text_record->pixels[at] = corner ? emu::Wsc::UiText::Pixel{emu::Wsc::UiText::FrameCorner,
                                                                           scene[at],
                                                                           scene[at],
                                                                           false,
                                                                           0,
                                                                           corner,
                                                                           true}
                                                 : scene_text->pixels[at];
            }
            if (record) {
                record->owner[at] = scene_record->owner[at];
                record->color[at] = scene_record->color[at];
            }
        }
    };
    for (int yy = y; yy < y + divider; ++yy)
        for (int xx = x + split + (yy < y + divider - 6 ? 4 : 12); xx < x + width; ++xx)
            restore(xx, yy);
    // Preserve the native rounded connector while removing only its outside background.
    std::vector<int> todo;
    std::vector<bool> seen(px_.size());
    for (int yy = y + divider - 6; yy < y + divider; ++yy)
        todo.push_back(yy * kWidth + x + split + 11);
    while (!todo.empty()) {
        const int at = todo.back();
        todo.pop_back();
        const int xx = at % kWidth, yy = at / kWidth;
        if (xx < x + split + 4 || xx >= x + split + 12 || yy < y + divider - 6 || yy >= y + divider ||
            at < 0 || at >= int(px_.size()) || seen[size_t(at)])
            continue;
        seen[size_t(at)] = true;
        if (px_[size_t(at)] == color(Ink::Text) || (!second_ && px_[size_t(at)] == color(Ink::Shadow)))
            continue;
        restore(xx, yy);
        todo.insert(todo.end(), {at - 1, at + 1, at - kWidth, at + kWidth});
    }
}

// The window palette's lightest grey and the next one down (the GB windows' fill and band).
void Chrome::window_greys(uint32_t *light, uint32_t *band) const {
    *light = *band = 0;
    for (int i = 0; i < 16; ++i)
        if (const uint32_t c = rgb(kNormal, i, false); luma(c) > luma(*light))
            *light = c;
    for (int i = 0; i < 16; ++i)
        if (const uint32_t c = rgb(kNormal, i, false); luma(c) < luma(*light) && luma(c) > luma(*band))
            *band = c;
}

// The GB picture: only the outer line of the GB frame -- its stroke pixels that touch
// the outside -- over a blank fill in the lightest gray, whatever the style. The outside is flooded from
// beyond the window's edge through anything but the stroke, which keeps the rounded corners open.
void Chrome::thin_window(int x, int y, int width, int height, const std::function<int(int, int)> &at) {
    // The flood/edge-distance geometry depends only on the nine frame tiles and
    // dimensions, not the live colors, position or clip. Reuse it across pictures;
    // exact atlas comparison also handles ROM/resource edits and new generations.
    struct Shape {
        int width, height;
        std::array<uint8_t, 9 * 32> atlas;
        std::vector<uint8_t> paint; // 0 outside; 1/3 line, 2 band, 4 fill
    };
    static thread_local std::deque<Shape> shapes;
    std::array<uint8_t, 9 * 32> atlas;
    std::copy_n(memory_data_ + 0x4000 + kFrame * 32, atlas.size(), atlas.begin());
    const Shape *shape = nullptr;
    for (const auto &candidate : shapes)
        if (candidate.width == width && candidate.height == height && candidate.atlas == atlas) {
            shape = &candidate;
            break;
        }
    if (!shape) {
        const int w = width + 2, h = height + 2; // with a one-pixel ring of outside around the window
        std::vector<uint8_t> index(size_t(w * h), 0), outside(size_t(w * h), 0);
        for (int yy = 0; yy < height; ++yy)
            for (int xx = 0; xx < width; ++xx)
                index[size_t((yy + 1) * w + xx + 1)] = uint8_t(at(xx, yy));
        std::vector<int> todo;
        for (int i = 0; i < w * h; ++i)
            if (i % w == 0 || i % w == w - 1 || i / w == 0 || i / w == h - 1) {
                outside[size_t(i)] = 1;
                todo.push_back(i);
            }
        while (!todo.empty()) {
            const int i = todo.back();
            todo.pop_back();
            for (const int n : {i - 1, i + 1, i - w, i + w})
                if (n >= 0 && n < w * h && !outside[size_t(n)] && index[size_t(n)] != kStroke) {
                    outside[size_t(n)] = 1;
                    todo.push_back(n);
                }
        }
        // Distance inward from the line (4-neighbors), 0 on the line itself.
        std::vector<int> depth(size_t(w * h), -1);
        for (int i = 0; i < w * h; ++i)
            if (!outside[size_t(i)] && index[size_t(i)] == kStroke &&
                (outside[size_t(i - 1)] || outside[size_t(i + 1)] || outside[size_t(i - w)] ||
                 outside[size_t(i + w)])) {
                depth[size_t(i)] = 0;
                todo.push_back(i);
            }
        for (size_t k = 0; k < todo.size(); ++k) {
            const int i = todo[k];
            if (depth[size_t(i)] >= kThinBand)
                continue;
            for (const int n : {i - 1, i + 1, i - w, i + w})
                if (!outside[size_t(n)] && depth[size_t(n)] < 0) {
                    depth[size_t(n)] = depth[size_t(i)] + 1;
                    todo.push_back(n);
                }
        }
        Shape fresh{width, height, atlas, std::vector<uint8_t>(size_t(width * height))};
        for (int yy = 1; yy <= height; ++yy)
            for (int xx = 1; xx <= width; ++xx) {
                const int i = yy * w + xx;
                fresh.paint[size_t((yy - 1) * width + xx - 1)] = outside[size_t(i)] ? 0
                                                                 : depth[size_t(i)] < 0
                                                                     ? 4
                                                                     : uint8_t(depth[size_t(i)] + 1);
            }
        if (shapes.size() == 32)
            shapes.pop_front();
        shapes.push_back(std::move(fresh));
        shape = &shapes.back();
    }
    // Black-gray-black: the line, a band in the next gray down, the line again, then the
    // lightest gray fills.
    uint32_t light = 0, band = 0, line = rgb(kNormal, kStroke, false);
    window_greys(&light, &band);
    const auto *paint = shape->paint.data();
    for (int yy = 0; yy < height; ++yy)
        for (int xx = 0; xx < width; ++xx)
            if (const uint8_t kind = paint[yy * width + xx])
                pixel(x + xx, y + yy, kind == 1 || kind == 3 ? line : kind == 2 ? band : light);
}

std::u32string Chrome::item_run(int id, std::u32string_view name) {
    return std::u32string(1, kItemToken + (id >= 0 && id < 256 ? id : 256)) + std::u32string(name);
}
void Chrome::button_icon(const ButtonIcon &icon, int x, int y, Ink ink, Window style, bool with_shadow) {
    // the Icon Type's colors in the WSC picture (and so Enhanced); the GB picture keeps its greys
    const bool colored = !second_ && ink != Ink::TitleHint;
    const uint32_t stroke = icon.body && colored ? *icon.body : color(ink, style);
    const uint32_t shadow = icon.shadow && colored
                                ? *icon.shadow
                                : color(ink == Ink::TitleHint ? Ink::TitleHintShadow : Ink::Shadow, style);
    const bool symbol = icon.symbol && colored;
    // a stick's arc: its own color, and in the GB picture the windows' band gray
    uint32_t light = 0, arc = 0;
    if (icon.motion && colored)
        arc = *icon.motion;
    else if (!colored)
        window_greys(&light, &arc);
    if (ink == Ink::TitleHint)
        arc = title_hint_faded(arc);                       // the key's face fades out with its label
    for (int pass = with_shadow ? 0 : 1; pass < 2; ++pass) // the shadow down-right first, then the ink
        for (int r = 0; r < icon.h; ++r)
            for (int c = 0; c < icon.w; ++c) {
                const uint8_t v = icon.at(c, r);
                if (v == ButtonIcon::Clear || (v == ButtonIcon::Symbol && (pass == 0 || !symbol)) ||
                    (pass == 0 && v == ButtonIcon::InkNoShadow))
                    continue;
                pixel(x + icon.dx + c + (pass ? 0 : 1), y + icon.dy + r + (pass ? 0 : 1),
                      pass == 0                                                   ? shadow
                      : v == ButtonIcon::Symbol                                   ? *icon.symbol
                      : v == ButtonIcon::InkNoShadow && (icon.motion || !colored) ? arc
                                                                                  : stroke);
            }
}

int Chrome::text_width(std::u32string_view s) {
    if (s.find(U'{') != std::u32string_view::npos) {
        std::u32string plain; // as text(): with no icons, the names written out
        bool icons = false;
        for (size_t i = 0; i < s.size(); ++i) {
            TextToken token;
            if (text_token_at(s.substr(i), &token)) {
                icons = icons || !text_token_icon(token).empty();
                const std::string name = text_token_text(token);
                plain.append(name.begin(), name.end());
                i += token.len - 1;
            } else {
                plain += s[i];
            }
        }
        if (!icons && plain != s) // a brace that is no token (e.g. {n}) would recurse forever
            return text_width(plain);
    }
    if (s.find(U'{') != std::u32string_view::npos) {
        int w = 0;
        size_t begin = 0;
        bool any = false;
        for (size_t i = 0; i <= s.size(); ++i) {
            TextToken token;
            const bool is_token = i < s.size() && text_token_at(s.substr(i), &token);
            if (i < s.size() && !is_token)
                continue;
            if (!any && i == s.size())
                break;
            any = true;
            if (const auto run = s.substr(begin, i - begin); !run.empty())
                w += text_width(run) - 2;
            if (is_token) {
                const ButtonIcon icon = text_token_icon(token);
                const std::string name = text_token_text(token);
                w += icon.empty() ? text_width(std::u32string(name.begin(), name.end())) - 2 : icon.w + 1;
                i += token.len - 1;
                begin = i + 1;
            }
        }
        if (any)
            return w + 2;
    }
    auto plain = normalized(s);
    int cells = 0;
    std::erase_if(plain, [&](char32_t ch) {
        if (!is_item_token(ch))
            return false;
        ++cells;
        return true;
    });
    return cells * kItemAdvance + sawgod::game::text_width(plain);
}
void Chrome::item(int id, std::u32string_view name, int x, int y, Ink ink, int total_width, Window style) {
    text(item_run(id, name), x, y, ink, total_width, style);
}

// GB style item icons, one per WSC item symbol (9000:6EE0), rows of eight: '#' black, '+' dark gray,
// '-' light gray, '.' paper: Final Fantasy Legend II's icons where it has one, the Japanese GB game's heart,
// circle and cross, the rest drawn for this port in FFL2's scheme or traced from the WSC icon.
constexpr const char *kGbItemIcons[32] = {
    "....##....####...####+..+-###+...+-#.#+...+...#+.......#........", //  0 hammers and axes (FFL2 axe)
    "++......#.+......#.+...#..#.+.##...#.##.....###....#####..##..##", //  1 swords (FFL2 sword)
    ".....#......+.#..#.+..#.#+######.#+---#-...+..#.....+.#......#..", //  2 bows (FFL2 bow)
    "..###....#...#...#....#...#..+#...#.+#...+..++#......#-#......##", //  3 whips (FFL2 whip)
    "........##.......#+++++#.#######.##.#...#-##....#+#.....###.....", //  4 guns (FFL2 gun)
    "...##.....#.+#..#.####.##.#.+#.###++++####++++###.####.##..#...#", //  5 launchers (FFL2 cannon)
    "#--+....#---+...#.---+...#.---+...#.---+...#.--#....####....####", //  6 saw (new: two-edged handsaw,
                                                                        //  traced from the WSC icon)
    ".######.#-#-#--##-#-#--########.#-++++#..#---+#...####..........", //  7 martial arts (new: fist)
    "###..####.####-##--.---#.#-.--#..##--##.#.-##--##.#.-#-###.##.##", //  8 mail (FFL2 armor)
    ".#######.#..+++#.#.++++#.##+++##.#.###+#..#.#+#....#+#......#...", //  9 shields (FFL2 shield)
    "+......+#+.##.+##.#.+#.#.#+.++#..#.##+#.#+####+##+####+#.##..##.", // 10 helms (FFL2 helm)
    "+......+#+.##.+##.#.+#.#.#+.++#..#.##+#.#+####+##+####+#.##..##.", // 11 headband (FFL2 helm)
    ".........##+##..#..#..##.##+.+.##...#+.#.##++++##+..#++#.##+++..", // 12 gauntlets (FFL2 glove)
    "...####....#.-#....#--#..##-#--##.-#---##.---#-#.####.#-........", // 13 shoes (FFL2 boot)
    "###..####.####-##--.---#.#-.--#..##--##.#.-##--##.#.-#-###.##.##", // 14 suits (FFL2 armor)
    "..++++#...+..+#....+.#....+..+#..+.###+#.+.###+#..+.#+#....++#..", // 15 potions and remedies (FFL2
                                                                        // potion)
    "..........##.##..#######.#######.#######..#####....###......#...", // 16 Heart (JP GB heart)
    ".........######.#-#--#-#######-#.#-##-#...#--#.....##...........", // 17 PhiloStone (new: faceted gem)
    ".######..#----#..#-++-#..#-++-#..#---##..#-++-#..#-++-#..######.", // 18 Door (new: door with panels)
    "...##.....#+-#...#+++-#.#+++++-####++###..#++#....#++#....####..", // 19 Str/Agil-Up (new: arrow)
    "...##.....#-.#...#---.#.#-----.####--###..#--#....#--#....####..", // 20 HP200-600 (new: light arrow)
    ".++++++#.+.++++#.#.+...#.#.+##+#.#.++++#.#.++++#.#.++++#.+######", // 21 books (FFL2 book)
    ".#++....+++#....#.##.....#++#.....##+#......#+#......#+#......##", // 22 staves (FFL2 staff)
    ".........#+#+#+.+######+#++##.###-+#####.#-+###...#-+#.....##...", // 23 Flirt (WSC icon traced)
    "..----....#-#-....---+.....-+.....----...-.-+.-...-..-....-..-..", // 24 single-target abilities (WSC
                                                                        // icon traced)
    "..---+....#-#+....---+....+++...---.---+#-#.#-#+---.---++++.+++.", // 25 multi-target abilities (WSC icon
                                                                        // traced)
    ".#....#.#..##..#...##......##..-+-....-+.+-##-+.#.+--+.#.#.++.#.", // 26 active abilities (WSC icon
                                                                        // traced)
    "..####....#..#..###..####......##......####..###..#..#....####..", // 27 healing abilities (new: hollow
                                                                        // cross)
    "..........###....#---#..#-----#.#----+#.#--#++#..#####..........", // 28 Rock (new: rock)
    ".######.#------##-####-##------##########--##--##------#.######.", // 29 key items (FFL2 field chest)
    "...........###....#...#..#.....#.#.....#.#.....#..#...#....###..", // 30 resists (JP GB circle)
    ".........#.....#..#...#....#.#......#......#.#....#...#..#.....#", // 31 weak to (JP GB cross)
};
const char *Chrome::gb_item_icon(int symbol) {
    return symbol >= 0 && symbol < 32 ? kGbItemIcons[symbol] : nullptr;
}
void Chrome::item_icon(int id, int x, int y) {
    Machine m(w_);
    const int symbol = id < 0 ? 255 : m.rd8(0x9000, uint16_t(0x6EE0 + id));
    const bool present = symbol != 255;
    std::vector<uint32_t> pixels;
    if (present) {
        const uint16_t entry = m.rd16(0x4000, uint16_t(0xC0E7 + symbol * 2));
        // Menu tiles 80..9F are reused by battle graphics. Read their original source, so the icon
        // is also correct in battle and field messages, where the native menu atlas isn't loaded.
        const int tile = (entry & 0x1FF) - 0x80, palette = entry >> 9 & 15;
        const char *gb = second_ ? gb_item_icon(symbol) : nullptr;
        for (int yy = 0; yy < 8; ++yy)
            for (int xx = 0; xx < 8; ++xx) {
                uint32_t color;
                if (gb) {
                    const char c = gb[yy * 8 + xx];
                    if (c == '.')
                        continue;
                    color = shown(c == '#' ? 0 : c == '+' ? 0x555 : 0xAAA);
                } else {
                    const int tx = entry & 0x4000 ? 7 - xx : xx, ty = entry & 0x8000 ? 7 - yy : yy;
                    const uint8_t b = m.rd8(0x9000, uint16_t(0x2480 + tile * 32 + ty * 4 + tx / 2));
                    const int v = tx & 1 ? b & 15 : b >> 4;
                    if (!v)
                        continue;
                    if (!second_) {
                        icon_pixel(x + xx, y + yy, palette, v, false);
                        if (x + xx >= clip_left_ && x + xx < clip_right_ && y + yy >= clip_top_ &&
                            y + yy < clip_bottom_)
                            pixels.push_back(uint32_t((y + yy) * kWidth + x + xx));
                        continue;
                    }
                    color = rgb(palette, v, false);
                    if (second_) {
                        const int lum = luma(color);
                        color = shown(lum >= 221 ? 0xFFF : lum >= 136 ? 0xAAA : lum >= 51 ? 0x555 : 0);
                    }
                }
                pixel(x + xx, y + yy, color);
                if (x + xx >= clip_left_ && x + xx < clip_right_ && y + yy >= clip_top_ &&
                    y + yy < clip_bottom_)
                    pixels.push_back(uint32_t((y + yy) * kWidth + x + xx));
            }
    }
    if (observe) {
        Draw draw{Draw::Kind::ItemIcon, x, y, 8, 8, 8, false, {}};
        draw.ink = std::move(pixels);
        draw.item = id;
        draw.has_icon = present;
        observe(draw);
    }
}

bool Chrome::can_draw(char32_t c) {
    if (font_has(c))
        return true;
    const char32_t n = normalized(std::u32string(1, c))[0];
    return font_has(n) || (n >= kWscGlyph && n < kWscGlyph + 0x100);
}

void Chrome::text(std::u32string_view s, int x, int y, Ink ink, int max_width, Window style,
                  bool cast_shadow) {
    if (!ready_)
        return;
    // Button tokens ({B} Back: engine/button_icons.h) draw as the icon of what the player presses, the runs
    // between them as text.
    if (s.find(U'{') != std::u32string_view::npos) {
        // with no icon for any of them, the names in place: the same text as written out ("B Back")
        std::u32string plain;
        bool icons = false;
        for (size_t i = 0; i < s.size(); ++i) {
            TextToken token;
            if (text_token_at(s.substr(i), &token)) {
                icons = icons || !text_token_icon(token).empty();
                const std::string name = text_token_text(token);
                plain.append(name.begin(), name.end());
                i += token.len - 1;
            } else {
                plain += s[i];
            }
        }
        if (!icons && plain != s) {
            text(plain, x, y, ink, max_width, style, cast_shadow);
            return;
        }
    }
    if (s.find(U'{') != std::u32string_view::npos) {
        size_t begin = 0;
        int pen = 0;
        bool any = false;
        for (size_t i = 0; i <= s.size(); ++i) {
            TextToken token;
            const bool is_token = i < s.size() && text_token_at(s.substr(i), &token);
            if (i < s.size() && !is_token)
                continue;
            if (!any && i == s.size())
                break; // no tokens after all: plain text below
            any = true;
            const auto run = s.substr(begin, i - begin);
            if (!run.empty()) {
                text(run, x + pen, y, ink, std::max(0, max_width - pen), style, cast_shadow);
                pen += text_width(run) - 2; // shared text metrics include the pen inset and end shadow
            }
            if (is_token) {
                if (const ButtonIcon icon = text_token_icon(token); icon.empty()) {
                    const std::string name = text_token_text(token);
                    const std::u32string name32(name.begin(), name.end());
                    text(name32, x + pen, y, ink, std::max(0, max_width - pen), style, cast_shadow);
                    pen += text_width(name32) - 2;
                } else if (pen + icon.w <= max_width) {
                    button_icon(icon, x + pen + kM5x7Left, y - 1, ink, style,
                                (ink == Ink::TitleHint ? !second_ : shadow_) && cast_shadow);
                    pen += icon.w + 1; // its shadow; the next run's pen inset is the gap
                }
                i += token.len - 1;
                begin = i + 1;
            }
        }
        if (any)
            return;
    }
    // Item-name arguments carry a private presentation token. Expand at the pen, before drawing
    // the following name, so translated templates can reorder/repeat arguments without string edits.
    if (std::any_of(s.begin(), s.end(), is_item_token)) {
        size_t begin = 0;
        int pen = 0;
        for (size_t i = 0; i <= s.size(); ++i) {
            if (i != s.size() && !is_item_token(s[i]))
                continue;
            const auto run = s.substr(begin, i - begin);
            text(run, x + pen, y, ink, std::max(0, max_width - pen), style, cast_shadow);
            const int run_width = text_width(run);
            pen +=
                run.empty() ? 0 : run_width - 2; // shared text metrics include the pen inset and end shadow
            if (i < s.size()) {
                if (observe) {
                    Draw draw{Draw::Kind::ItemRun, x + pen, y, kItemAdvance, 8, max_width - pen, false, {}};
                    draw.item = s[i] == kItemToken + 256 ? -1 : int(s[i] - kItemToken);
                    observe(draw);
                }
                if (pen + kItemAdvance <= max_width)
                    item_icon(s[i] == kItemToken + 256 ? -1 : int(s[i] - kItemToken), x + pen, y);
                pen += kItemAdvance;
            }
            begin = i + 1;
        }
        return;
    }
    const bool with_shadow =
        (ink == Ink::Title || ink == Ink::TitleHint || ink == Ink::TitleVersion || ink == Ink::IntroPrologue
             ? !second_
             : shadow_) &&
        cast_shadow;
    const auto text = normalized(s);
    // Stroke and shadow masks, as text_render: m5x7 strokes cast a shadow right and down; WSC glyphs keep
    // their own. Ten rows: a descender's shadow hangs one row under the nine-row glyph.
    const int width = std::min(max_width, kWidth - x) + 1;
    if (width <= 1)
        return;
    std::vector<uint8_t> mask(size_t(width * 10), 0);
    int pen = 0;
    for (char32_t c : text) {
        const bool wsc = c >= kWscGlyph && c < kWscGlyph + 0x100;
        const FontGlyph g = wsc ? FontGlyph{8, {}} : font_glyph(c);
        if (pen + g.advance > width - 1)
            break;
        for (int r = 0; r < 9; ++r)
            for (int b = 0; b < 8; ++b) {
                const int at = r * width + pen + b + kM5x7Left;
                if (pen + b + kM5x7Left >= width)
                    continue;
                if (wsc) {
                    // The WSC font in both pictures (the GB font's kana are drawn
                    // differently); the GB picture keeps its no-shadow look.
                    const int t = kFontTile + int(c - kWscGlyph);
                    const uint8_t byte = r < 8 ? w_.ram[uint16_t(0x4000 + t * 32 + r * 4 + b / 2)] : 0;
                    const uint8_t i = b & 1 ? byte & 15 : byte >> 4;
                    if (i == kStroke || (i == kShadow && with_shadow))
                        mask[size_t(at)] = uint8_t(0x20 | i); // flagged: casts no m5x7 shadow
                } else if (g.rows[size_t(r)] >> (7 - b) & 1) {
                    mask[size_t(at)] = kStroke;
                }
            }
        pen += g.advance;
    }
    for (int r = 9; r >= 0 && with_shadow; --r)
        for (int c = width - 1; c >= 0; --c)
            if (!mask[size_t(r * width + c)] && ((c && mask[size_t(r * width + c - 1)] == kStroke) ||
                                                 (r && mask[size_t((r - 1) * width + c)] == kStroke)))
                mask[size_t(r * width + c)] = kShadow + 0x10; // marked: not a stroke source for the scan
    const uint32_t stroke = color(ink, style),
                   shadow = color(ink == Ink::Title || ink == Ink::TitleVersion ? Ink::TitleShadow
                                  : ink == Ink::TitleHint                       ? Ink::TitleHintShadow
                                  : ink == Ink::IntroPrologue                   ? Ink::IntroEdge
                                                                                : Ink::Shadow,
                                  style);
    int left = kWidth, top = emu::Wsc::kHeight, right = 0, bottom = 0;
    std::vector<uint32_t> pixels, strokes;
    uint16_t cut = 0; // bit per glyph row with ink inside the clip's columns but outside its rows
    for (int r = 0; r < 10; ++r)
        for (int c = 0; c < width; ++c)
            if (const uint8_t v = mask[size_t(r * width + c)] & 15) {
                text_pixel(x + c, y + r, v == kStroke ? stroke : shadow, v != kStroke, with_shadow);
                if (x + c >= clip_left_ && x + c < clip_right_ &&
                    (y + r < clip_top_ || y + r >= clip_bottom_))
                    cut |= uint16_t(1u << r);
                if (observe && x + c >= clip_left_ && x + c < clip_right_ && y + r >= clip_top_ &&
                    y + r < clip_bottom_) {
                    left = std::min(left, x + c);
                    top = std::min(top, y + r);
                    right = std::max(right, x + c + 1);
                    bottom = std::max(bottom, y + r + 1);
                    if (x + c >= 0 && x + c < kWidth && y + r >= 0 && y + r < emu::Wsc::kHeight) {
                        pixels.push_back(uint32_t((y + r) * kWidth + x + c));
                        if (v == kStroke)
                            strokes.push_back(pixels.back());
                    }
                }
            }
    if (observe && !s.empty()) {
        Draw draw{Draw::Kind::Text,
                  x,
                  y,
                  text_width(s),
                  8,
                  max_width,
                  clip_left_ != 0 || clip_top_ != 0 || clip_right_ != kWidth ||
                      clip_bottom_ != emu::Wsc::kHeight,
                  std::u32string(s),
                  left,
                  top,
                  right,
                  bottom,
                  std::move(pixels),
                  std::move(strokes),
                  cut_ok_};
        for (int r = 0; r < 10; ++r)
            draw.cut_rows += cut >> r & 1;
        draw.marquee = marquee_;
        observe(draw);
    }
}

void Chrome::numerals(std::string_view s, int x, int y, Ink ink) {
    if (ready_)
        numerals(s, x, y, color(ink), shadow_ ? color(Ink::Shadow) : ~0u);
}

void Chrome::numerals(std::string_view s, int x, int y, uint32_t stroke, uint32_t shadow) {
    if (observe && !s.empty()) {
        const int width = numerals_width(s), height = shadow == ~0u ? 6 : 7;
        observe({Draw::Kind::Text, x, y, width, height, width, false, std::u32string(s.begin(), s.end()), x,
                 y, x + width, y + height});
    }
    static constexpr uint8_t kDigits[11][6] = {
        {6, 9, 9, 9, 9, 6},   {2, 6, 2, 2, 2, 7},   {6, 9, 1, 2, 4, 15}, {14, 1, 6, 1, 9, 6},
        {2, 6, 10, 15, 2, 2}, {15, 8, 14, 1, 9, 6}, {6, 8, 14, 9, 9, 6}, {15, 1, 2, 2, 4, 4},
        {6, 9, 6, 9, 9, 6},   {6, 9, 9, 7, 1, 6},   {1, 1, 1, 2, 2, 2}};
    // Two passes keep a later glyph's shadow from covering an earlier glyph.
    for (int pass = shadow == ~0u ? 0 : 1; pass >= 0; --pass) {
        int xx = x;
        for (char ch : s) {
            if (ch != '/' && (ch < '0' || ch > '9'))
                continue;
            const int g = ch == '/' ? 10 : ch - '0', gw = ch == '/' ? 2 : 4;
            for (int r = 0; r < 6; ++r)
                for (int c = 0; c < gw; ++c)
                    if (kDigits[g][r] & (1 << (gw - 1 - c)))
                        text_pixel(xx + c + pass, y + r + pass, pass ? shadow : stroke, pass != 0,
                                   shadow != ~0u);
            xx += gw + 1;
        }
    }
}

int Chrome::numerals_width(std::string_view s) {
    int n = 0;
    for (char ch : s)
        n += ch == '/' ? 3 : 5;
    return n ? n - 1 : 0;
}

void Chrome::bar(int x, int y, int width, int height, int value, int maximum, std::string_view label,
                 const BarColors &colors) {
    if (!ready_)
        return;
    // Native black fades subtract one step from every palette channel. The window's
    // lightest channels carry that same level for these host-defined meter colors.
    int peak = 0;
    for (int i = 0; i < 16; ++i) {
        const auto c = w_.presentation_palette_color(kNormal, i, false, false);
        for (int shift : {0, 4, 8})
            peak = std::max(peak, c >> shift & 15);
    }
    if (second_) { // the GB picture: no bar, the label alone
        // Black ink cannot darken. Fade its coverage into the battlefield/window beneath it.
        const int previous_opacity = opacity_;
        opacity_ = previous_opacity * peak / 15;
        if (kGbBarFont) {
            std::u32string text(label.begin(), label.end());
            this->text(text, x + (width - text_width(text)) / 2, y + (height - 9) / 2);
        } else {
            numerals(label, x + (width - numerals_width(label)) / 2, y + (height - 6) / 2,
                     Ink::Text); // as WSC
        }
        opacity_ = previous_opacity;
        return;
    }
    if (width <= 0 || height <= 0)
        return;
    auto *bars = w_.ui_text ? &w_.ui_text->bars : nullptr;
    std::vector<std::pair<size_t, uint32_t>> underlays;
    const auto hard_shadow = [&](int xx, int yy) {
        if (xx < clip_left_ || xx >= clip_right_ || yy < clip_top_ || yy >= clip_bottom_)
            return;
        const size_t at = size_t(yy * kWidth + xx);
        const auto beneath = px_[at];
        const auto owner = w_.ui_fill ? w_.ui_fill->owner[at] : 0;
        pixel(xx, yy, color(Ink::Shadow));
        if (bars) {
            underlays.emplace_back(at, beneath);
            if (w_.ui_fill)
                w_.ui_fill->owner[at] = owner;
        }
    };
    for (int yy = 1; yy <= height; ++yy)
        hard_shadow(x + width, y + yy);
    for (int xx = 1; xx < width; ++xx)
        hard_shadow(x + xx, y + height);
    const int filled = maximum > 0 ? std::clamp((value * width + maximum / 2) / maximum, 0, width) : 0;
    const auto faded = [cut = 17 * (15 - peak)](uint32_t color) {
        uint32_t out = 0;
        for (int shift : {0, 8, 16})
            out |= uint32_t(std::max(0, int(color >> shift & 255) - cut)) << shift;
        return out;
    };
    const uint32_t empty = faded(colors.empty), full = faded(colors.full);
    for (int yy = 0; yy < height; ++yy)
        for (int xx = 0; xx < width; ++xx)
            pixel(x + xx, y + yy, xx < filled ? full : empty);
    if (!label.empty()) {
        // Keep text legible if a mod changes the window palette's text ink.
        uint32_t stroke = color(Ink::Text), shadow = faded(colors.shadow);
        if (std::min(std::abs(luma(stroke) - luma(empty)), std::abs(luma(stroke) - luma(full))) < kContrast)
            std::swap(stroke, shadow);
        const auto previous_shadow = soft_shadow_;
        soft_shadow_ = shadow;
        numerals(label, x + (width - numerals_width(label)) / 2, y + (height - 6) / 2, stroke, shadow);
        soft_shadow_ = previous_shadow;
    }
    if (bars) {
        bars->rectangles.push_back({x, y, width, height, float(peak) / 15});
        const auto owner = uint32_t(bars->rectangles.size());
        for (int yy = std::max(clip_top_, y - 3); yy < std::min(clip_bottom_, y + height + 3); ++yy)
            for (int xx = std::max(clip_left_, x - 3); xx < std::min(clip_right_, x + width + 3); ++xx) {
                const size_t at = size_t(yy * kWidth + xx);
                bars->pixels[at] = {owner, px_[at], px_[at], false};
            }
        for (const auto &[at, beneath] : underlays)
            bars->pixels[at].beneath = beneath, bars->pixels[at].shadow = true;
    }
}

std::vector<uint32_t> Chrome::sprite_ink(int tile, int x, int y) const {
    std::vector<uint32_t> ink;
    for (int yy = 0; yy < 8; ++yy)
        for (int xx = 0; xx < 8; ++xx)
            if (index(tile, xx, yy) && x + xx >= 0 && x + xx < kWidth && y + yy >= 0 &&
                y + yy < emu::Wsc::kHeight)
                ink.push_back(uint32_t((y + yy) * kWidth + x + xx));
    return ink;
}

// The GB picture's arrow and scroll markers: the GB has no arrow, so ours in GB shades
// ('#' black, 'L' light grey, 'D' dark grey): the anchor arrow, solid black with the WSC arrow's two-row tip
// (rows 3-4) so the pointer rules align it the same way, and plain black triangles (8 x 4) for more
// above/below.
constexpr const char *kGbArrow[8] = {"....#...", "....##..", ".######.", ".#######",
                                     ".#######", ".######.", "....##..", "....#..."};
constexpr const char *kGbMoreBelow[4] = {"#######.", ".#####..", "..###...", "...#...."};

void Chrome::gb_pattern(const char *const *rows, int height, int x, int y, bool flip, Draw &draw) {
    for (int yy = 0; yy < height; ++yy)
        for (int xx = 0; xx < 8; ++xx) {
            const char c = rows[flip ? height - 1 - yy : yy][xx];
            if (c == '.')
                continue;
            pixel(x + xx, y + yy,
                  c == 'L'   ? 0xAAAAAA
                  : c == 'D' ? 0x555555
                             : color(Ink::Text)); // GB text: black
            if (x + xx >= 0 && x + xx < kWidth && y + yy >= 0 && y + yy < emu::Wsc::kHeight)
                draw.ink.push_back(uint32_t((y + yy) * kWidth + x + xx));
        }
}

void Chrome::cursor(int x, int y, bool pointer, int target_height) {
    if (!ready_)
        return;
    if (pointer)
        y += target_height / 2 - 4;
    if (second_) {
        Draw draw{Draw::Kind::Cursor, x, y, 8, 8, 8, false, {}};
        draw.pointer = pointer;
        gb_pattern(kGbArrow, 8, x, y, false, draw);
        if (observe)
            observe(draw);
        return;
    }
    ++y; // WSC/Enhanced arrow art sits one pixel below the shared target anchor.
    if (observe) {
        Draw draw{Draw::Kind::Cursor, x, y, 8, 8, 8, false, {}};
        draw.ink = sprite_ink(kArrow, x, y);
        draw.pointer = pointer;
        observe(draw);
    }
    tiles_observed_ = false;
    tile(uint16_t(kArrow | (kNormal - 8) << 9), x, y, false, true);
    tiles_observed_ = true;
}

Chrome::ArrowPixels Chrome::arrow() const {
    ArrowPixels pixels{};
    for (int yy = 0; yy < 8; ++yy)
        for (int xx = 0; xx < 8; ++xx)
            pixels[size_t(yy * 8 + xx)] = index(kArrow, xx, yy);
    return pixels;
}

void Chrome::cursor(int x, int y, const ArrowPixels &arrow, int target_height) {
    if (second_) { // the GB arrow is the host's own pattern
        cursor(x, y, true, target_height);
        return;
    }
    if (!ready_)
        return;
    y += target_height / 2 - 4 + 1;
    Draw draw{Draw::Kind::Cursor, x, y, 8, 8, 8, false, {}};
    draw.pointer = true;
    for (int yy = 0; yy < 8; ++yy)
        for (int xx = 0; xx < 8; ++xx)
            if (const int i = arrow[size_t(yy * 8 + xx)]) {
                draw.ink.push_back(uint32_t((y + yy) * kWidth + x + xx));
                icon_pixel(x + xx, y + yy, kNormal, i, true);
            }
    if (observe)
        observe(draw);
}

void Chrome::more_below(int x, int y) { scroll_marker(x, y, false); }
void Chrome::more_above(int x, int y) { scroll_marker(x, y, true); }
void Chrome::trend_mark(int x, int y, bool higher) {
    scroll_marker(x, y, higher, color(higher ? Ink::Higher : Ink::Lower), true);
}
void Chrome::scroll_marker(int x, int y, bool up, uint32_t tint, bool tinted) {
    if (!ready_)
        return;
    Draw draw{Draw::Kind::ScrollMarker, x, y, 8, 4, 8, false, {}};
    draw.pointer = false;
    if (second_) {
        gb_pattern(kGbMoreBelow, 4, x, y, up, draw);
        if (observe)
            observe(draw);
        return;
    }
    // Rotate the arrow clockwise (down) or counterclockwise (up), shortening its body to four rows to fit
    // the gutter. Every nontransparent pixel retains its native shade; no layout owns colors or sprite data.
    static constexpr int columns[] = {0, 2, 4, 7};
    int darkest = -1; // a tinted mark keeps its darkest shade
    if (tinted)
        for (int yy = 0; yy < 4; ++yy)
            for (int xx = 0; xx < 8; ++xx)
                if (const int i = index(kArrow, columns[yy], 7 - xx))
                    if (darkest < 0 || luma(rgb(kNormal, i, true)) < luma(rgb(kNormal, darkest, true)))
                        darkest = i;
    for (int yy = 0; yy < 4; ++yy)
        for (int xx = 0; xx < 8; ++xx)
            if (const int i = index(kArrow, columns[up ? 3 - yy : yy], 7 - xx)) {
                pixel(x + xx, y + yy, tinted && i != darkest ? tint : rgb(kNormal, i, true));
                if (x + xx >= 0 && x + xx < kWidth && y + yy >= 0 && y + yy < emu::Wsc::kHeight)
                    draw.ink.push_back(uint32_t((y + yy) * kWidth + x + xx));
            }
    if (observe)
        observe(draw);
}
int Chrome::tile_ink_top(uint16_t entry, bool sprite) const {
    const int t = (entry & 0x1FF) | (entry & 0x2000 && !sprite ? 0x200 : 0);
    for (int yy = 0; yy < 8; ++yy)
        for (int xx = 0; xx < 8; ++xx)
            if (index(t, entry & 0x4000 ? 7 - xx : xx, entry & 0x8000 ? 7 - yy : yy))
                return yy;
    return 8;
}

void Chrome::hand(int x, int y) {
    if (!ready_)
        return;
    if (observe) {
        Draw draw{Draw::Kind::Cursor, x, y, 16, 16, 16, false, {}};
        for (int i = 0; i < 4; ++i) {
            const auto part = sprite_ink(kHand + i, x + (i & 1) * 8, y + (i >> 1) * 8);
            draw.ink.insert(draw.ink.end(), part.begin(), part.end());
        }
        std::sort(draw.ink.begin(), draw.ink.end());
        observe(draw);
    }
    tiles_observed_ = false;
    for (int i = 0; i < 4; ++i)
        tile(uint16_t((kHand + i) | (kNormal - 8) << 9), x + (i & 1) * 8, y + (i >> 1) * 8, false, true);
    tiles_observed_ = true;
}

} // namespace sawgod::game
