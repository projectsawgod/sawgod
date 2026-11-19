#include "game/gb_intro.h"
#include "emu/wsc.h"
#include "game/machine.h"
#include "game/session.h"
#include "game/text.h"
#include "game/text_render.h"
#include "game/ui_chrome.h"
#include <algorithm>
namespace sawgod::game {
namespace {
// The opening reuses the font's VRAM for illustrations. Read the same native kana
// strokes from their immutable source instead; neither picture's RAM is changed.
FontGlyph opening_glyph(const emu::Wsc &w, char32_t c) {
    if (c < 0xE000 || c >= 0xE100)
        return font_glyph(c);
    FontGlyph g{8, {}};
    const size_t at = 0x390080 + size_t(c - 0xE000) * 32;
    if (at + 32 > w.rom.size())
        return g;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            const auto b = w.rom[at + size_t(y * 4 + x / 2)];
            if ((x & 1 ? b & 15 : b >> 4) == 2)
                g.rows[size_t(y)] |= uint8_t(0x80 >> x);
        }
    return g;
}
int mix(int a, int b, int age, int duration) { return a + (b - a) * age / duration; }
constexpr int kNoRepeat = -(1 << 20);
} // namespace
std::vector<std::u32string> GbIntro::pages(std::u32string_view paragraph) {
    const auto split = paragraph.find(U"{n}{n}");
    if (split == paragraph.npos)
        return {std::u32string(paragraph), {}};
    return {std::u32string(paragraph.substr(0, split)), std::u32string(paragraph.substr(split + 6))};
}
int GbIntro::caption_width(int id) {
    // Authored bank-3C strip widths, in pixels (descriptors 46C0..46F8).
    constexpr int widths[] = {160, 192, 192, 176, 128, 208, 160, 96, 160};
    return id >= 1 && id <= 8 ? widths[id] : kCaptionWidth;
}
WrapResult GbIntro::layout(std::u32string_view text, bool prologue, int caption) {
    return wrap_text(text, prologue ? kPrologueWidth : caption_width(caption));
}
void GbIntro::attach(emu::Wsc &w) {
    if (owner_ == &w && subscription_.active())
        return;
    subscription_.reset();
    owner_ = &w;
    generation_ = Session::of(w).generation();
    stage_ = IntroStage::Off;
    subscription_ = Session::of(w).hooks().subscribe(
        hooks::Event::IntroPresentation, [this](Machine &m, const hooks::Args &a) { observe(m.wsc(), a); });
}
void GbIntro::page(emu::Wsc &w, std::u32string text, int budget, bool prologue, int caption) {
    lines_ = layout(text, prologue, caption);
    letters_ = 0;
    for (const auto &line : lines_.lines)
        letters_ += line.text.size();
    budget_ = std::max(1, budget);
    start_ = w.frame_count;
    complete_ = false;
}
void GbIntro::observe(emu::Wsc &w, const hooks::Args &a) {
    generation_ = Session::of(w).generation();
    const auto next = IntroStage(a.a);
    switch (next) {
    case IntroStage::WorldCard:
        in_world_card_ = true;
        in_prologue_ = false;
        fade_ = 0;
        page(w, host_text(w, "world_card_" + std::to_string(a.b)), 524, false);
        break;
    case IntroStage::WorldCardContent:
        start_ = w.frame_count;
        break;
    case IntroStage::Caption:
        page(w, host_text(w, "intro_caption_" + std::to_string(a.b)), a.c, false, a.b);
        break;
    case IntroStage::Splash:
        page(w, host_text(w, "intro_copyright"), 250, false);
        break;
    case IntroStage::Prologue:
        in_prologue_ = true;
        prologue_ = pages(host_text(w, "intro_prologue"));
        page_ = 0;
        fade_ = 0;
        page(w, prologue_[0], kPageFrames, true);
        break;
    case IntroStage::Content:
        content_start_ = w.frame_count;
        page(w, prologue_[0], kPageFrames, true);
        break;
    case IntroStage::Hold:
        if (page_ == 0) {
            page_ = 1;
            page(w, prologue_[1], 300, true);
        }
        break;
    case IntroStage::WorldCardRow:
        return; // WSC lettering observes this; GB keeps its existing typed-page state.
    case IntroStage::Fade:
        fade_ = a.b;
        return;
    case IntroStage::White:
        lines_ = {};
        letters_ = 0;
        in_prologue_ = false;
        break;
    case IntroStage::Off:
        in_world_card_ = false;
        in_prologue_ = false;
        break;
    default:
        break;
    }
    if (next == IntroStage::Off || next == IntroStage::Field)
        w.second_scene_image.clear();
    w.second_scene_above_host = next != IntroStage::Off && next != IntroStage::Field;
    stage_ = next;
    if (next == IntroStage::Pans)
        start_ = w.frame_count;
    if (w.second())
        frame(w, true);
}
void GbIntro::sync_page(emu::Wsc &w) {
    if (stage_ == IntroStage::Content && page_ == 0 && w.frame_count - content_start_ >= kPageFrames) {
        page_ = 1;
        page(w, prologue_[1], kPageFrames, true);
        // Retain the exact boundary even when frame() is first called after it.
        start_ = content_start_ + kPageFrames;
    }
}
bool GbIntro::typing(const emu::Wsc &w) const {
    const bool text_page = stage_ == IntroStage::Caption || stage_ == IntroStage::Splash ||
                           stage_ == IntroStage::Content || stage_ == IntroStage::Hold ||
                           stage_ == IntroStage::WorldCardContent;
    if (!text_page || complete_ || lines_.lines.empty())
        return false;
    const auto age = w.frame_count - start_;
    return age * std::max(size_t(budget_), letters_ * 2) < letters_ * size_t(budget_) * 2;
}
uint16_t GbIntro::keys(emu::Wsc &w, uint16_t held, bool selected) {
    sync_page(w);
    // A press completes the typing page and still reaches the native skip/advance unchanged.
    const uint16_t mask = in_prologue_ ? emu::KEY_A : emu::KEY_A | emu::KEY_B | emu::KEY_START;
    if (selected && (held & ~previous_ & mask) && typing(w)) {
        complete_ = true;
        frame(w, true);
    }
    previous_ = held;
    return held;
}
void GbIntro::reset(emu::Wsc &w) {
    stage_ = IntroStage::Off;
    in_prologue_ = in_world_card_ = false;
    previous_ = 0;
    w.second_scene_above_host = false;
}
void GbIntro::frame(emu::Wsc &w, bool enabled) {
    if (Session::of(w).generation() != generation_) {
        stage_ = IntroStage::Off;
        w.second_scene_above_host = false;
        in_prologue_ = false;
        in_world_card_ = false;
        previous_ = 0;
        generation_ = Session::of(w).generation();
    }
    if (!enabled || !active())
        return;
    sync_page(w);
    w.second_scene_above_host = true;
    auto &image = w.second_scene_image;
    image.assign(224 * 144, 0xFFF);
    if (stage_ == IntroStage::Pans) {
        // The native core owns 3*18*16 + 64 = 928 frames. Use all of them,
        // including its final fade interval, split 310/309/309.
        const int age = std::min(927, int(w.frame_count - start_));
        if (draw_map) {
            if (age < 310) // in toward the tower, then a one-second rest on it
                draw_map(88, mix(316, 12 * 16 + 8, std::min(age, 234), 234),
                         mix(336, 11 * 16 + 8, std::min(age, 234), 234), image, kNoRepeat);
            else if (age < 619) {
                // One straight northwest move from the town's southeast corner to the tower base, continuing
                // pan 1's direction.
                const int t = age - 310;
                draw_map(1, mix(33 * 16, 24 * 16 + 8, t, 308), mix(34 * 16 + 8, 20 * 16, t, 308), image,
                         kNoRepeat);
            } else {
                // Up the tower at 320 px per 308 frames, accelerating to about twice the height; the brick
                // column repeats above the map's top row (-6), so the tower goes on.
                const double t = age - 619, v0 = 320.0 / 308, a = 2 * (640 - v0 * 308) / (308.0 * 308);
                draw_map(1, 24 * 16 + 8, 20 * 16 - int(v0 * t + a * t * t / 2), image, -6 * 16);
            }
        }
        return;
    }
    if (stage_ == IntroStage::White)
        return;
    size_t shown =
        complete_ ? letters_
                  : std::min(letters_, size_t(w.frame_count - start_) *
                                           std::max(size_t(budget_), letters_ * 2) / (size_t(budget_) * 2));
    if (stage_ == IntroStage::Prologue || stage_ == IntroStage::WorldCard)
        shown = 0; // the native fade-in precedes typing
    // Center each completed line; its position does not shift during typing.
    int y = (144 - int(lines_.lines.size()) * 12) / 2 + 1;
    for (const auto &line : lines_.lines) {
        int x = (224 - Chrome::text_width(line.text)) / 2 + 1;
        for (char32_t c : line.text) {
            if (!shown--)
                goto finished;
            const auto g = opening_glyph(w, c);
            for (int row = 0; row < 9; ++row)
                for (int col = 0; col < 8; ++col)
                    if ((g.rows[size_t(row)] & (0x80 >> col)) && x + col >= 0 && x + col < 224 &&
                        y + row >= 0 && y + row < 144)
                        image[size_t((y + row) * 224 + x + col)] = 0;
            x += g.advance;
        }
        y += 12;
    }
finished:
    if (in_prologue_ || in_world_card_) {
        const uint16_t white = uint16_t(std::clamp((fade_ + 2) / 5, 0, 3) * 0x555);
        for (auto &pixel : image)
            if (pixel)
                pixel = white;
    }
}
} // namespace sawgod::game
