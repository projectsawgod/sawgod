#include "game/computed_flags.h"
#include "game/computed_handlers.h"
#include "game/text_render.h"
// Decompiled routines from bank 0x34, menu/popup area (ROM >= 0x34A000). See asm/bank_34.s.
#include "game/bank34.h"
#include "game/session.h"

namespace sawgod::game {

namespace {
constexpr uint16_t kDialogueSpeed = 0xD0E6;      // dialogue_speed
constexpr uint16_t kIconQueue = 0xD55A;          // icon_queue
constexpr uint16_t kStatusWindowTokens = 0xD596; // status_window_tokens
constexpr uint16_t kPopupState = 0xD60E;         // status_window_state
constexpr uint16_t kTextColorFlag = 0xDD77;      // text_color_flag
constexpr uint16_t kIram = 0x0000;               // internal RAM segment

} // namespace

// Appends up to AX words from the 0xFFFF-terminated array at DS:[SI] into the 25-entry, 0xFFFF-terminated
// word queue icon_queue (0xD55A): BX==0xFF finds the existing terminator (capping the search at 25
// entries) and appends after it; any other BX overwrites the queue from the start. Stops early on a
// 0xFFFF word in the source. All registers preserved (pushaw + ds/es/ss bracket the whole body); only
// icon_queue's memory changes. By far the most-called unnamed function (167 sites); best guess is it
// queues icon/token codes for the status popup window (open_status_popup / draw_status_icons).
void append_word_queue_core(Machine &m) {
    m.watch_text("append_word_queue");
    Session *session = Session::find(m.wsc());
    if (session) // battle-popup message templates (text.h)
        session->message_part(m);
    uint16_t count = m.ax();
    uint16_t di = kIconQueue;
    if (m.bx() == 0xFF) {
        uint16_t searched_entries = 0;
        for (;;) {
            if (m.rd16(kIram, di) == 0xFFFF)
                break;
            di = uint16_t(di + 2);
            ++searched_entries;
            if (searched_entries == 0x19)
                break;
        }
    }
    uint16_t si = m.si();
    while (count != 0) {
        if (const auto *table = m.text_table();
            table && !m.original_popup_text() && di >= kIconQueue + 25 * 2)
            break;
        uint16_t queued_word = m.rd16(m.ds(), si);
        if (queued_word == 0xFFFF)
            break;
        m.wr16(kIram, di, queued_word);
        si = uint16_t(si + 2);
        di = uint16_t(di + 2);
        --count;
    }
    m.wr16(kIram, di, 0xFFFF);
}
// rom: 0x34CA20
// Appends up to AX words from the 0xFFFF-terminated array at DS:[SI] into the 25-entry, 0xFFFF-terminated
void append_word_queue(Machine &m) {
    append_word_queue_core(m);
    m.retf();
}

// For each word entry in the 0xFFFF-terminated buffer at ES:SI, looks up a tile pattern via table word
// 0x2080+entry (DS-relative, same base table as draw_glyph_tile) and draws it with draw_tilemap_offset
// (a 1x1 copy, attribute 0x0F00 or 0x1F00 depending on text_color_flag at 0xDD77) at row = entry CX's low
// byte, column = entry BX's low byte + the 0-based icon index (BX/CX are restored after each icon --
// only visible to draw_tilemap_offset transiently). AX/SI/ES preserved; DX ends up holding the index of
// the terminator (the count of icons drawn); DI ends up pointing at the terminator word (just past the
// last entry checked). Renders the icon list built by append_word_queue/open_status_popup.
void draw_status_icons_core(Machine &m) {
    uint16_t entry_ax = m.ax();
    uint16_t si = m.si();
    uint16_t es = m.es();
    uint16_t outer_bx = m.bx();
    uint16_t outer_cx = m.cx();
    uint8_t row = uint8_t(outer_cx);
    uint16_t dx = 0;
    uint16_t di;
    // a loaded language's text words: the whole line in our font (text_render.h); registers as the loop
    // below leaves them
    if (draw_popup_line(m, es, si, row, outer_bx, m.rd8(m.ss(), kTextColorFlag) != 0 ? 0x0F00 : 0x1F00)) {
        while (m.rd16(es, uint16_t(si + dx * 2 + 2)) != 0xFFFF)
            ++dx;
        m.ax() = entry_ax;
        m.si() = si;
        m.es() = es;
        m.dx() = uint16_t(dx + 1);
        m.di() = uint16_t(si + (dx + 1) * 2);
        return;
    }
    for (;;) {
        di = uint16_t(si + dx * 2);
        uint16_t icon = m.rd16(es, di);
        uint16_t col_bx = uint16_t((outer_bx & 0xFF00) | uint8_t(uint8_t(outer_bx) + uint8_t(dx)));
        uint16_t attr = m.rd8(m.ss(), kTextColorFlag) != 0 ? 0x0F00 : 0x1F00;

        m.set_al(row);
        m.bx() = col_bx;
        m.cx() = 0x0101;
        m.dx() = attr;
        m.si() = uint16_t(0x2080 + icon);
        draw_tilemap_offset_core(m);

        m.bx() = outer_bx;
        m.cx() = outer_cx;

        dx = uint16_t(dx + 1);
        di = uint16_t(si + dx * 2);
        if (m.rd16(es, di) == 0xFFFF)
            break;
    }
    m.ax() = entry_ax;
    m.si() = si;
    m.es() = es;
    m.dx() = dx;
    m.di() = di;
}
// rom: 0x34CB44
// For each word entry in the 0xFFFF-terminated buffer at ES:SI, looks up a tile pattern via table word
void draw_status_icons(Machine &m) {
    draw_status_icons_core(m);
    m.retf();
}

// Shared body of ROM 0x34CB98
// Draw an icon-word message progressively, waiting between icons; finish with the configured delay.
void status_popup_wait_core(Machine &m, uint16_t timing_words) {
    uint16_t saved_ax = m.ax(), saved_si = m.si(), saved_es = m.es();
    uint16_t remaining_waits = timing_words ? uint16_t(timing_words - 1) : 0;
    const auto word_wait = [&] {
        uint16_t cx = m.cx(), dx = m.dx();
        m.cx() = 0;
        m.dx() = uint8_t(m.rd8(m.ss(), kDialogueSpeed) + 1);
        for (;;) {
            m.set_al(0);
            m.out8(5, 0);
            m.out8(6, 0);
            dma_sprite_table_core(m);
            m.call_far(0x4000, 9);
            m.call_far(0x4000, 9);
            m.call_far(0x4000, 0x5c);
            m.ax() &= 4;
            if (m.ax()) {
                m.ax() = 0;
                break;
            }
            if (++m.cx() == m.dx())
                break;
        }
        m.dx() = dx;
        m.cx() = cx;
    };
    m.dx() = 0;
    for (;;) {
        m.ax() = 0;
        m.di() = uint16_t(m.si() + m.dx() * 2);
        m.ax() = m.rd16(m.es(), m.di());
        uint16_t cx = m.cx(), dx = m.dx(), bx = m.bx(), di = m.di(), si = m.si();
        // a loaded language's text words: the line so far, laid out in our font (text_render.h)
        if (!draw_popup_line(m, m.es(), si, uint8_t(cx), bx, 0x1F00, dx + 1)) {
            m.si() = uint16_t(0x2080 + m.ax());
            m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(uint8_t(m.bx()) + uint8_t(m.dx())));
            m.ax() = m.cx();
            m.cx() = 0x101;
            m.dx() = 0x1F00;
            draw_tilemap_offset_core(m);
        }
        m.si() = si;
        m.di() = di;
        m.bx() = bx;
        m.dx() = dx;
        m.cx() = cx;
        ++m.dx();
        m.di() = uint16_t(m.si() + m.dx() * 2);
        m.ax() = m.rd16(m.es(), m.di());
        if (m.ax() == 0xFFFF)
            break;
        if (!timing_words || remaining_waits) {
            word_wait();
            if (remaining_waits)
                --remaining_waits;
        }
    }
    // English battle text can occupy fewer (or more) words than the Japanese queue. Keep its
    // drawing unchanged, but poll A once per original inter-word wait, never as one padded delay.
    while (remaining_waits) {
        word_wait();
        --remaining_waits;
    }
    m.cx() = uint16_t(uint8_t(m.rd8(m.ss(), kDialogueSpeed) + 2) * 2);
    wait_frames_core(m);
    m.es() = saved_es;
    m.si() = saved_si;
    m.ax() = saved_ax;
    return;
}

// rom: 0x34CB98
void status_popup_wait(Machine &m) {
    status_popup_wait_core(m);
    m.retf();
}
// Shared body of ROM 0x34CA6E
void open_status_popup_core(Machine &m) { open_status_popup_core(m, 0); }

void open_status_popup_core(Machine &m, uint16_t timing_words) {
    Session *session = Session::find(m.wsc());
    const bool joined = session && m.original_popup_text() && session->joining_battle_popup();
    if (!joined && session && m.es() == 0 && m.si() == kIconQueue)
        timing_words = session->apply_message_template(m);
    if (!joined || session->present_joined_battle_popup(m))
        hooks::emit(hooks::Event::StatusPopup, m, {m.es(), m.si(), m.rd8(0, kPopupState)});
    uint16_t saved_ax = m.ax(), saved_ds = m.ds();
    m.ax() = 0;
    m.ds() = 0;
    m.set_al(m.rd8(0, kPopupState));
    uint8_t row = m.al();
    if (row != 1) {
        uint16_t ax = m.ax(), bx = m.bx(), cx = m.cx(), dx = m.dx(), bp = m.bp(), si = m.si(), di = m.di(),
                 ds = m.ds(), es = m.es(), ss = m.ss();
        m.ax() = 0x8000;
        m.ds() = m.ax();
        m.ax() = 0;
        m.bx() = 0x0A01;
        m.cx() = 0x041C;
        draw_window_alt_core(m);
        m.ax() = ax;
        m.bx() = bx;
        m.cx() = cx;
        m.dx() = dx;
        m.bp() = bp;
        m.si() = si;
        m.di() = di;
        m.ds() = ds;
        m.es() = es;
        m.ss() = ss;
    }
    if (row == 0)
        m.bx() = 0x0B04;
    else {
        if (row != 1) {
            uint16_t si = m.si(), es = m.es();
            m.ax() = 0;
            m.es() = 0;
            m.si() = kStatusWindowTokens;
            m.ax() = 0x9000;
            m.ds() = m.ax();
            m.cx() = 0;
            m.bx() = 0x0B04;
            draw_status_icons_core(m);
            m.es() = es;
            m.si() = si;
        }
        m.bx() = 0x0C04;
        uint16_t si = m.si(), bx = m.bx();
        m.ax() = 0;
        m.ds() = 0;
        m.bx() = 0;
        for (;;) {
            m.ax() = m.rd16(m.es(), m.si());
            m.wr16(m.ds(), uint16_t(m.bx() + kStatusWindowTokens), m.ax());
            if (m.ax() == 0xFFFF)
                break;
            m.si() = uint16_t(m.si() + 2);
            m.bx() = uint16_t(m.bx() + 2);
        }
        m.bx() = bx;
        m.si() = si;
    }
    m.ax() = 0x9000;
    m.ds() = m.ax();
    m.cx() = 0;
    status_popup_wait_core(m, timing_words);
    m.ax() = 0;
    m.ds() = 0;
    m.set_al(computed::add<uint8_t>(m, m.rd8(0, kPopupState), 1));
    m.wr8(0, kPopupState, m.al());
    m.ds() = saved_ds;
    m.ax() = saved_ax;
    return;
}

// rom: 0x34CA6E
// Draw/append the status-popup icon list and advance its row counter, preserving AX and DS.
void open_status_popup(Machine &m) {
    open_status_popup_core(m);
    m.retf();
}
} // namespace sawgod::game
