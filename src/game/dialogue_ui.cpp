// Host dialogue layout and presentation driven by native dialogue events.
#include "game/dialogue_ui.h"
#include "game/player_names.h"
#include "game/session.h"
#include "game/text_render.h"
#include "game/text_tokens.h"
#include "game/text_wrap.h"
#include "game/ui_items.h"
#include <algorithm>
#include <array>

namespace sawgod::game {
void DialogueUi::clear() {
    seal_ = -1;
    lines_.assign(1, {});
    speaker_.clear();
    wrap_line_.clear();
    projected_tail_.clear();
    waiting_ = wrap_wait_ = skip_glyph_ = speaker_decided_ = false;
}
void DialogueUi::glyph(char32_t code, bool speaker_colon) {
    // Speakers introduce quoted speech: Japanese uses 「, English uses a colon then an opening quote.
    // Only a first-line prefix can become a tab; later punctuation stays in the body.
    if (!speaker_decided_ && lines_.size() == 1 && !lines_[0].empty() && (speaker_colon || code == U'「')) {
        speaker_ = lines_[0];
        lines_[0].clear();
        speaker_decided_ = true;
        return;
    }
    // A predicted speaker (Japanese) is already on the plate: its name glyphs are not body text.
    if (capacity_ != 3 && !speaker_decided_ && !speaker_.empty() && lines_.size() == 1 && lines_[0].empty()) {
        if (code == U'「')
            speaker_decided_ = true;
        return;
    }
    if (speaker_decided_ && lines_.back().empty() && code == U' ')
        return;
    lines_.back() += code;
}
void DialogueUi::next_line(Machine &m) {
    speaker_decided_ = true;
    if (int(lines_.size()) == capacity_) {
        if (capacity_ == 3 && Fiber::current()) {
            // This hook runs on the native dialogue fiber. Park it in precisely the native
            // {wait} routine: its field updates/key polling continue, no script byte is read.
            // Preserve the interrupted token's CPU contract (including arithmetic flags).
            std::array<uint16_t, 8> registers;
            std::copy_n(m.wsc().cpu.s.r, 8, registers.begin());
            const auto flags = m.flags();
            const auto cs = m.cs();
            wrap_wait_ = true;
            m.call_far(0x5000, 0x49B1);
            wrap_wait_ = false;
            m.cs() = cs;
            std::copy(registers.begin(), registers.end(), m.wsc().cpu.s.r);
            m.flags() = flags;
        }
        lines_.erase(lines_.begin());
    }
    lines_.emplace_back();
    if (seal_ >= 0)
        lines_.back() += kSealInk + seal_;
    wrap_line_.clear();
}
void DialogueUi::typed(Machine &m, char32_t code, std::u32string_view tail) {
    if (capacity_ != 3) {
        glyph(code);
        return;
    }
    auto text = wrap_line_ + code + std::u32string(tail);
    // A speaker tab consumes no body width. It is still recognized by glyph(), at its native pace.
    const auto colon =
        !speaker_decided_ && lines_.size() == 1 ? text.find_first_of(U":「") : std::u32string::npos;
    const auto hard_break = text.find(U"{n}");
    const auto quote = colon == std::u32string::npos ? colon : text.find_first_not_of(U" ", colon + 1);
    const bool speaker = colon != std::u32string::npos &&
                         (text[colon] == U'「' ||
                          (quote != std::u32string::npos && (text[quote] == U'"' || text[quote] == U'“')));
    if (speaker && (hard_break == std::u32string::npos || colon < hard_break)) {
        // The whole name is known from the line's first glyph: the tab opens with it, not letter by letter.
        const auto name = text.substr(0, colon);
        if (!speaker_decided_ && lines_.size() == 1 && lines_[0].empty() &&
            name.find(U'{') == std::u32string::npos) {
            if (speaker_.empty())
                speaker_ = name;
            if (code == U':' || code == U'「') {
                speaker_decided_ = true;
                wrap_line_.clear();
            }
            return;
        }
        glyph(code, code == U':');
        if (code == U':' || code == U'「')
            wrap_line_.clear();
        else
            wrap_line_ += code;
        return;
    }
    const auto wrapped = wrap_text(text, 214);
    if (!lines_.back().empty() && wrapped.lines.size() > 1 && wrapped.lines[1].begin < hard_break &&
        wrapped.lines[0].end <= wrap_line_.size()) {
        while (!lines_.back().empty() && lines_.back().back() == U' ')
            lines_.back().pop_back();
        next_line(m);
    }
    if ((code == U' ' || code == U'\n') &&
        (lines_.back().empty() || (!wrap_line_.empty() && wrap_line_.back() == U' ')))
        return;
    if (code >= kInlineSeal && code < kInlineSeal + 5)
        seal_ = int(code - kInlineSeal);
    else if (code == kSealEnd)
        seal_ = -1;
    wrap_line_ += code;
    glyph(code == U'~' || code == U'\n' ? U' ' : code);
}
void DialogueUi::install(emu::Wsc &w) {
    subscriptions_.clear();
    wsc_ = &w;
    Session::of(w).player_names().enabled = true;
    generation_ = Session::of(w).generation();
    visible_ = false;
    clear();
    subscriptions_.push_back(
        Session::of(w).hooks().subscribe(hooks::Event::SceneFrame, [this](Machine &m, const auto &) {
            Session::of(m.wsc()).scene_requests().dialogue =
                active() && generation_ == Session::of(m.wsc()).generation();
        }));
    subscriptions_.push_back(Session::of(w).hooks().subscribe(
        hooks::Event::Dialogue, [this, &w](Machine &m, const hooks::Args &a) {
            frame(w);
            capacity_ = Session::of(w).text().proportional() ? 3 : 4;
            switch (a.a) {
            case 0:
                visible_ = false;
                clear();
                break;
            case 1:
                visible_ = true;
                clear();
                box_reads_ = Session::of(w).dialogue_reads(); // the cursor still points at the last box
                break;
            case 2:
                waiting_ = false;
                if (a.b == 0x0E || a.b == 0x0C) {
                    clear();
                    box_reads_ = Session::of(w).dialogue_reads() - 1; // the clear token was this box's read
                } else if (a.b == 0x0A || a.b == 0x0D) {
                    next_line(m);
                } else if (skip_glyph_)
                    skip_glyph_ = false;
                else {
                    typed(m, char32_t(a.c), projected_tail_ + Session::of(w).dialogue_tail(m));
                    projected_tail_.clear();
                }
                break;
            case 3:
                waiting_ = a.b != 0;
                break;
            case 5:
                clear();
                speaker_decided_ = true;
                lines_[0] = a.b ? host_text(w, "ui_dialogue_inventory_full")
                                : host_text(w, "ui_dialogue_item_obtained",
                                            Chrome::item_run(uint8_t(a.c), item_name(w, uint8_t(a.c))));
                break;
            case 4:
                if (a.b < 0 || a.b > 3)
                    break;
                projected_tail_.clear();
                if (Session::of(w).player_names().rendered(size_t(a.b)).empty()) {
                    const auto name = Session::of(w).dialogue_name(m, a.b);
                    if (size_t(a.c + 1) < name.size())
                        projected_tail_ = name.substr(size_t(a.c + 1));
                }
                skip_glyph_ = true;
                if (a.c == 0) {
                    auto name = Session::of(w).player_names().rendered(size_t(a.b));
                    if (!name.empty())
                        for (size_t i = 0; i < name.size(); ++i)
                            typed(m, name[i], name.substr(i + 1) + Session::of(w).dialogue_tail(m));
                    else
                        skip_glyph_ = false;
                } else if (Session::of(w).player_names().rendered(size_t(a.b)).empty())
                    skip_glyph_ = false;
                break;
            }
        }));
}
// The native delay types a box's first glyph a few frames after the box opens or clears. A speaker whose
// name is already read joins the box on its first frame, not as a tab appearing (or reappearing) later.
void DialogueUi::predict_speaker(emu::Wsc &w) {
    auto &session = Session::of(w);
    if (speaker_decided_ || !speaker_.empty() || lines_.size() != 1 || !lines_[0].empty() ||
        session.dialogue_reads() == box_reads_)
        return;
    Machine m(w);
    const auto text = session.dialogue_pending(m);
    const auto colon = text.find_first_of(capacity_ == 3 ? U":「" : U"「");
    if (colon == std::u32string::npos || colon == 0)
        return;
    const auto hard_break = text.find(U"{n}");
    const auto quote = text.find_first_not_of(U" ", colon + 1);
    const bool speaker = text[colon] == U'「' ||
                         (quote != std::u32string::npos && (text[quote] == U'"' || text[quote] == U'“'));
    const auto name = text.substr(0, colon);
    if (speaker && (hard_break == std::u32string::npos || colon < hard_break) &&
        name.find(U'{') == std::u32string::npos)
        speaker_ = name; // typed() keeps it and decides the tab as the name's glyphs arrive
}
void DialogueUi::frame(emu::Wsc &w) {
    if (generation_ != Session::of(w).generation()) {
        visible_ = false;
        clear();
        generation_ = Session::of(w).generation();
    }
}
std::vector<uint32_t> DialogueUi::picture(emu::Wsc &w, emu::Wsc::ColorMode mode, bool second) {
    frame(w);
    if (!active())
        return w.framebuffer_rgb(mode, second);
    predict_speaker(w);
    auto px = w.framebuffer_rgb(mode, second, true);
    Chrome c(w, px, mode, second);
    const int height = capacity_ == 4 ? 64 : 52, y = 144 - height;
    const size_t first_fill = w.ui_fill ? w.ui_fill->windows.size() : 0;
    if (speaker_.empty())
        c.window(0, y, 224, height);
    else {
        c.speaker_window(0, y - 16, 224, height + 16, Chrome::text_width(speaker_) + 8);
        c.text(speaker_, 5, y - 10, Chrome::Ink::Text, 204);
    }
    // The tab joins a body whose position and gradient were fixed when the box opened.
    // Extending the gradient upward with the new tab makes the whole body appear to jump.
    if (w.ui_fill && !second)
        for (size_t i = first_fill; i < w.ui_fill->windows.size(); ++i) {
            w.ui_fill->windows[i].top = y;
            w.ui_fill->windows[i].bottom = 144;
        }
    for (size_t i = 0; i < lines_.size(); ++i)
        c.text(lines_[i], 5, y + 5 + int(i) * 12, Chrome::Ink::Text, 214);
    if (waiting_) {
        if (wrap_wait_)
            c.more_below(212, 133);
        else
            c.cursor(212, 133, false);
    }
    return px;
}
} // namespace sawgod::game
