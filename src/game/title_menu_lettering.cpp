#include "game/title_menu_lettering.h"
#include "engine/version.h"
#include "game/machine.h"
#include "game/session.h"
#include "game/text.h"
#include "game/text_render.h"
#include "game/ui_chrome.h"
#include <algorithm>

namespace sawgod::game {
void TitleMenuLettering::attach(emu::Wsc &w) {
    subscription_.reset();
    rows_ = 0;
    prompt_ = false;
    generation_ = Session::of(w).generation();
    subscription_ =
        Session::of(w).hooks().subscribe(hooks::Event::TitleMenu, [this](Machine &m, const hooks::Args &a) {
            generation_ = Session::of(m.wsc()).generation();
            rows_ = std::clamp(a.a, 0, 3);
            prompt_ = a.b != 0;
            if (prompt_)
                pulse_from_ = m.wsc().frame_count;
        });
}
bool TitleMenuLettering::active(emu::Wsc &w) const {
    return (rows_ || prompt_) && generation_ == Session::of(w).generation();
}
void TitleMenuLettering::composite(emu::Wsc &w, std::vector<uint32_t> &pixels, emu::Wsc::ColorMode mode,
                                   bool second, const StepHint &hint) const {
    if ((!rows_ && !prompt_) || generation_ != Session::of(w).generation())
        return;
    Chrome chrome(w, pixels, mode, second);
    if (rows_ && Session::of(w).text().proportional()) {
        const auto underlay = w.framebuffer_rgb(mode, second, false, true);
        // Only the menu words occupy SCR2 here. Preserve the subtitle, copyright, and native hand.
        for (int y = 96; y < 96 + 8 * rows_; ++y)
            std::copy_n(underlay.begin() + y * emu::Wsc::kWidth + 96, emu::Wsc::kWidth - 96,
                        pixels.begin() + y * emu::Wsc::kWidth + 96);
        constexpr const char *keys[] = {"title_new_game", "title_continue", "title_original"};
        for (int row = 0; row < rows_; ++row)
            // The native rows are eight pixels apart; apply the layout's one-pixel lowering once.
            chrome.text(host_text(w, keys[row]), 95, 97 + row * 8, Chrome::Ink::Title, 127);
    }
    if (!hint.mode || *hint.mode == FidelityMode::Swap)
        return;
    // One line half a text line above the copyright notice (its first line starts at 128).
    constexpr int y = 115;
    constexpr int edge = 4, side = 72, gap = 2;
    // Optically center the version on the title, steady in its own gray ink and palette fades.
    // The third menu row (ORIGINAL) occupies this line's middle, so it yields there.
    if (rows_ < 3) {
        const auto version = utf8_to_u32(kVersionString);
        const int width = std::min(Chrome::text_width(version), side);
        chrome.text(version, (emu::Wsc::kWidth - width) / 2 + 9, y, Chrome::Ink::TitleVersion, side);
    }
    // In step and in tone with PUSH ANY BUTTON's palette pulse (advance_title_palette_pulse): while it waits,
    // its own palette's level (0 = fully out, 15 = full white); on the menu after it, the same rhythm from
    // the frame it was drawn (64 frames up and 64 down in 16 steps, out when it was drawn).
    int pulse = 0;
    if (prompt_) {
        // The brightest of palette 13's colors against its pulse target (step_title_palette_core).
        constexpr int kPulse = 0xC5A0, kTarget = 0xC7A0; // 0RGB words, as kTitlePulsePalette / Target
        const auto brightest = [&](int base) {
            int most = 0;
            for (int i = 0; i < 32; ++i) // each byte: two nibbles (B and G, then R and 0)
                most = std::max({most, w.ram[size_t(base + i)] & 15, w.ram[size_t(base + i)] >> 4});
            return most;
        };
        const int now = brightest(kPulse), full = brightest(kTarget);
        pulse = full ? std::min(15, now * 15 / full) : 0;
    } else {
        const int phase = int((w.frame_count - pulse_from_) % 128);
        pulse = std::min(15, (phase < 64 ? phase : 128 - phase) / 4);
    }
    chrome.title_hint_strength(pulse * 100 / 15);
    if (chrome.title_hint_out())
        return; // nothing to draw: the labels are the page's own color
    // Native rows remain the authority, including a disabled Continue and the AUTO-only case.
    constexpr std::u32string_view names[] = {U"GB", U"WSC", U"Enhanced"};
    const int level = int(*hint.mode);
    auto draw = [&](std::u32string_view name, const std::string &binding, bool right) {
        const std::u32string control(binding.begin(), binding.end());
        const int name_width = Chrome::text_width(name), control_width = Chrome::text_width(control);
        const int x = right ? emu::Wsc::kWidth - edge - name_width - gap - control_width : edge;
        // Keep the line in the empty sides, clear of the hand, logo, and menu.
        chrome.clip(right ? emu::Wsc::kWidth - edge - side : edge, 112, side, 16, [&] {
            chrome.text(name, right ? x : x + control_width + gap, y, Chrome::Ink::TitleHint, side);
            chrome.text(control, right ? emu::Wsc::kWidth - edge - control_width : x, y,
                        Chrome::Ink::TitleHint, side);
        });
    };
    if (level > 0)
        draw(names[level - 1], hint.down, false);
    if (level < 2)
        draw(names[level + 1], hint.up, true);
}
} // namespace sawgod::game
