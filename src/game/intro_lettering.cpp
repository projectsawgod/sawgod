#include "game/intro_lettering.h"
#include "game/machine.h"
#include "game/session.h"
#include "game/text.h"
#include "game/ui_chrome.h"
#include <algorithm>

namespace sawgod::game {
void IntroLettering::attach(emu::Wsc &w) {
    generation_ = Session::of(w).generation();
    captions_ = {};
    prologue_ = false;
    scenes_ = Session::of(w).hooks().subscribe(
        hooks::Event::IntroPresentation, [this](Machine &m, const hooks::Args &a) {
            auto &w = m.wsc();
            if (generation_ != Session::of(w).generation()) {
                generation_ = Session::of(w).generation();
                captions_ = {};
                prologue_ = false;
            }
            switch (IntroStage(a.a)) {
            case IntroStage::Caption:
                if (a.b >= 1 && a.b <= 8) {
                    auto &caption = captions_[size_t(a.b - 1)];
                    caption = {wrap_text(host_text(w, "intro_caption_" + std::to_string(a.b)), 192), 0,
                               m.rd8(m.cs(), uint16_t(m.bp() + 4)) * 4};
                }
                break;
            case IntroStage::Prologue:
                captions_ = {};
                prologue_ = true;
                break;
            case IntroStage::Off:
            case IntroStage::White:
            case IntroStage::Field:
            case IntroStage::Pans:
                captions_ = {};
                prologue_ = false;
                break;
            default:
                break;
            }
        });
    strips_ = Session::of(w).hooks().subscribe(hooks::Event::IntroCaptionStrip,
                                               [this](Machine &, const hooks::Args &a) {
                                                   if (a.a >= 1 && a.a <= 8)
                                                       captions_[size_t(a.a - 1)].columns = a.b;
                                               });
}
bool IntroLettering::active(emu::Wsc &w) const {
    return generation_ == Session::of(w).generation() && Session::of(w).text().proportional() &&
           (prologue_ || std::any_of(captions_.begin(), captions_.end(),
                                     [](const auto &c) { return !c.lines.lines.empty(); }));
}
void IntroLettering::composite(emu::Wsc &w, std::vector<uint32_t> &pixels, emu::Wsc::ColorMode mode) const {
    if (!active(w))
        return;
    Chrome chrome(w, pixels, mode, false);
    if (prologue_) {
        const auto lines = wrap_text(host_text(w, "intro_prologue"), 176);
        // The original prologue opens SCR2's window one scanline per native step.
        // Observe its scroll and window too: no estimated time, replacement waits, or input policy.
        const bool window = (w.io[0] & 0x20) != 0;
        const int top = window ? w.io[9] : 0, bottom = window ? std::min(144, int(w.io[11]) + 1) : 144;
        chrome.clip(24, top, 176, std::max(0, bottom - top), [&] {
            int y = 9 - w.io[0x13]; // layout's one-pixel lowering, applied once
            for (const auto &line : lines.lines) {
                const int x =
                    24 + (176 - Chrome::text_width(line.text)) / 2 - 1; // centered, as the original's lines
                chrome.text(line.text, x - w.io[0x12], y, Chrome::Ink::IntroPrologue, 176);
                y += 12;
            }
        });
        return;
    }
    for (size_t i = 0; i < captions_.size(); ++i) {
        const auto &c = captions_[i];
        int y = i == 3 || i == 5 ? 57 : i == 4 || i == 6 ? 81 : 65;
        if (c.lines.lines.size() > 1)
            y -= 5;
        for (const auto &line : c.lines.lines) {
            const int width = Chrome::text_width(line.text);
            const int x = (224 - width) / 2 - 1;
            const int edge = width * c.columns / std::max(1, c.total);
            chrome.clip(x, y, edge + (edge > 0 ? 1 : 0), 10, [&] {
                chrome.text(line.text, x, y, i == 7 ? Chrome::Ink::IntroClosing : Chrome::Ink::IntroCaption,
                            192, Chrome::Window::Normal, false);
            });
            y += 10;
        }
    }
}
} // namespace sawgod::game
