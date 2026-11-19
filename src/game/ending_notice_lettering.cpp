#include "game/ending_notice_lettering.h"
#include "game/machine.h"
#include "game/session.h"
#include "game/text.h"
#include "game/text_wrap.h"
#include "game/ui_chrome.h"
#include <algorithm>
#include <cstdlib>

namespace sawgod::game {
namespace {
// The notice's lines occupy these rows; the blinking prompt sprite below them stays native.
constexpr int kTop = 16, kBottom = 122, kWidth = 192, kPitch = 14;
} // namespace
void EndingNoticeLettering::attach(emu::Wsc &w) {
    subscriptions_.clear();
    shown_ = false;
    generation_ = Session::of(w).generation();
    auto &hooks = Session::of(w).hooks();
    // From the notice's tilemap until the save screen replaces it. The colors come from the native picture
    // each frame (composite), so the English follows the fades whatever the picture's timing.
    subscriptions_.push_back(
        hooks.subscribe(hooks::Event::EndingNotice, [this](Machine &m, const hooks::Args &a) {
            if (a.a) {
                generation_ = Session::of(m.wsc()).generation();
                shown_ = true;
            }
        }));
    for (auto e : {hooks::Event::PartyMenu, hooks::Event::SaveMenu})
        subscriptions_.push_back(
            hooks.subscribe(e, [this](Machine &, const hooks::Args &) { shown_ = false; }));
}
bool EndingNoticeLettering::active(emu::Wsc &w) const {
    return shown_ && generation_ == Session::of(w).generation();
}
void EndingNoticeLettering::composite(emu::Wsc &w, std::vector<uint32_t> &pixels, emu::Wsc::ColorMode mode,
                                      bool second) const {
    if (!active(w) || !Session::of(w).text().proportional())
        return; // Japanese keeps every native pixel.
    // The native colors at this fade step: the background beside the notice, and the kana farthest from it.
    // Both modes (WSC, GB greys) and both fades follow without a separate clock.
    const auto native = w.framebuffer_rgb(mode, second);
    const auto at = [&](int x, int y) { return native[size_t(y * emu::Wsc::kWidth + x)]; };
    const uint32_t backdrop = at(0, kTop);
    const auto distance = [&](uint32_t p) {
        int d = 0;
        for (int shift : {16, 8, 0})
            d += std::abs(int(p >> shift & 255) - int(backdrop >> shift & 255));
        return d;
    };
    uint32_t ink = backdrop;
    for (int y = kTop; y < kBottom; ++y)
        for (int x = 0; x < emu::Wsc::kWidth; ++x)
            if (distance(at(x, y)) > distance(ink))
                ink = at(x, y);
    // Chrome's colors follow the faded palette, so mark the glyphs against a color the palette can't make.
    constexpr uint32_t kEmpty = 0xFF00FF;
    std::vector<uint32_t> mask(pixels.size(), kEmpty);
    Chrome chrome(w, mask, mode, false);
    const auto lines = wrap_text(host_text(w, "ui_ending_notice"), kWidth).lines;
    int y = (kTop + kBottom - int(lines.size()) * kPitch) / 2 + 1; // layout's one-pixel lowering, once
    for (const auto &line : lines) {
        if (!line.text.empty())
            chrome.text(line.text, (emu::Wsc::kWidth - Chrome::text_width(line.text)) / 2, y,
                        Chrome::Ink::IntroClosing, kWidth, Chrome::Window::Normal,
                        false); // glyphs only, no shadow
        y += kPitch;
    }
    for (int y = kTop; y < kBottom; ++y)
        for (int x = 0; x < emu::Wsc::kWidth; ++x) {
            const size_t i = size_t(y * emu::Wsc::kWidth + x);
            pixels[i] = mask[i] != kEmpty ? ink : backdrop;
        }
}
} // namespace sawgod::game
