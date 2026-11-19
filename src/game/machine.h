// The machine as decompiled game code sees it while the rest of the game still runs in the interpreter:
// the V30MZ registers and the WonderSwan address space (internal RAM, SRAM, ROM through the mapper).
//
// Native routines are written against this, one original routine each, keeping the original's name,
// ROM address (a `// rom: 0xXXXXXX` tag) and observable behaviour: the same
// registers out, the same memory and I/O writes, the same return. sawgod-headless --difftest proves it.
#pragma once

#include "game/fiber.h"

#include <cstdint>
#include <memory>

#include "emu/wsc.h"
#include "game/text.h"

namespace sawgod::game {

// The machine's loaded language (its Session's TextTable), or nullptr (text.cpp).
const TextTable *text_table(emu::Wsc &w);

class Machine {
  public:
    explicit Machine(emu::Wsc &w) : w_(w), r_(w.cpu.s), text_(::sawgod::game::text_table(w)) {}
    emu::Wsc &wsc() { return w_; }
    // The loaded language (text.h), or nullptr.
    const TextTable *text_table() const { return text_; }
    bool original_popup_text() const { return original_popup_text_; }
    void set_original_popup_text(bool value) { original_popup_text_ = value; }

    // 16-bit registers
    uint16_t &ax() { return r_.r[emu::AX]; }
    uint16_t &bx() { return r_.r[emu::BX]; }
    uint16_t &cx() { return r_.r[emu::CX]; }
    uint16_t &dx() { return r_.r[emu::DX]; }
    uint16_t &sp() { return r_.r[emu::SP]; }
    uint16_t &bp() { return r_.r[emu::BP]; }
    uint16_t &si() { return r_.r[emu::SI]; }
    uint16_t &di() { return r_.r[emu::DI]; }
    uint16_t &cs() { return r_.seg[emu::CS]; }
    uint16_t &ds() { return r_.seg[emu::DS]; }
    uint16_t &es() { return r_.seg[emu::ES]; }
    uint16_t &ss() { return r_.seg[emu::SS]; }
    uint16_t &flags() { return r_.flags; }
    // 8-bit halves
    uint8_t al() const { return r_.r[emu::AX] & 0xFF; }
    uint8_t ah() const { return r_.r[emu::AX] >> 8; }
    void set_al(uint8_t v) { r_.r[emu::AX] = (r_.r[emu::AX] & 0xFF00) | v; }
    void set_ah(uint8_t v) { r_.r[emu::AX] = uint16_t((r_.r[emu::AX] & 0x00FF) | (v << 8)); }

    // memory, segment:offset (offsets wrap within the segment, as on the CPU)
    // The host text segment (text.h, kTextSegment = 0x0FFF) reads the loaded language's strings.
    uint8_t rd8(uint16_t seg, uint16_t off) {
        if (seg == 0x0FFF && text_ && !text_->empty()) [[unlikely]]
            return text_->byte(off);
        if (seg == 0x9000 && text_ && text_->has_records()) [[unlikely]] {
            const int b = original_popup_text_ ? text_->original_record_byte(off) : text_->record_byte(off);
            if (b >= 0)
                return uint8_t(b);
        }
        if (read_watch) [[unlikely]]
            read_watch(seg, off);
        return w_.read8(linear(seg, off));
    }
    // Debug: called on every rd8 that reaches the machine's memory (sawgod-headless --tables-poison reports
    // the data-table reads that bypass the store this way). Null in normal runs.
    static inline void (*read_watch)(uint16_t seg, uint16_t off) = nullptr;
    // Debug: called by the text routines (draw_text family, append_word_queue) with the string they get at
    // DS:SI (sawgod-headless --text-poison reports strings that don't come from the text resource).
    static inline void (*text_watch)(Machine &m, const char *routine) = nullptr;
    void watch_text(const char *routine) {
        if (text_watch) [[unlikely]]
            text_watch(*this, routine);
    }
    // Presentation: called by draw_player_sprite after it has put the player's tiles and sprites in place
    // (the GFX swap draws the GB sprite over them, game/gfx_swap.h). `oam` is the first of its four sprites.
    static inline void (*player_sprite_hook)(Machine &m, uint16_t oam) = nullptr;
    // After native NPC DMA, with its record, first OAM entry and decoded pose. Read-only observers.
    static inline void (*npc_sprite_hook)(Machine &m, uint16_t record, uint16_t oam, uint8_t pose) = nullptr;
    // Presentation only: identify a field metatile after its four screen words are drawn.
    // `at` is the screen-1 byte offset; type -1 denotes the map's outside border.
    // Signed map coordinates include the eight-cell border; destination is a circular screen address.
    static inline void (*field_cell_hook)(Machine &m, uint16_t at, uint8_t id, int type, int x,
                                          int y) = nullptr;
    // Presentation: called by draw_enemy_group_portrait after it has drawn enemy group `group`'s picture
    // with its top left at screen-2 cell `at` (BX: row << 8 | column), and by dissolve_enemy_group after each
    // pass (`step`: the pass's mask is E000:BC18 + step).
    static inline void (*enemy_portrait_hook)(Machine &m, int group, uint16_t at) = nullptr;
    static inline void (*enemy_dissolve_hook)(Machine &m, int group, int step) = nullptr;
    uint16_t rd16(uint16_t seg, uint16_t off) {
        return uint16_t(rd8(seg, off) | rd8(seg, uint16_t(off + 1)) << 8);
    }
    void wr8(uint16_t seg, uint16_t off, uint8_t v) { w_.write8(linear(seg, off), v); }
    void wr16(uint16_t seg, uint16_t off, uint16_t v) {
        wr8(seg, off, uint8_t(v));
        wr8(seg, uint16_t(off + 1), uint8_t(v >> 8));
    }
    uint8_t in8(uint8_t port) { return w_.in8(port); }
    void out8(uint8_t port, uint8_t v) { w_.out8(port, v); }

    // returns: pop the return address pushed by the caller's CALL / CALL FAR
    void ret() { ip() = pop(); }
    void iret() {
        ip() = pop();
        cs() = pop();
        flags() = uint16_t(pop() & (emu::CF | emu::PF | emu::AF | emu::ZF | emu::SF | emu::TF | emu::IF |
                                    emu::DF | emu::OF)); // as the CPU core stores FLAGS
    }
    void retf() {
        ip() = pop();
        cs() = pop();
    }
    // Dispatch a callee through the machine and continue when it returns. Frame points use this to
    // reach the scheduler; without its native installed the oracle interprets the original. Only in routines
    // registered with `yields` (they run on a Fiber): the call pushes a return address pointing at the
    // trampoline (caller's CS, IP kTrampolineIp), hands control to the scheduler and resumes here once the
    // callee has returned there (CS:IP = trampoline, SP back where it was).
    static constexpr uint16_t kTrampolineIp = 0xFFFF;
    void call_far(uint16_t seg, uint16_t off) {
        push(cs());
        push(kTrampolineIp);
        const uint16_t caller_cs = cs();
        cs() = seg;
        ip() = off;
        callout(caller_cs, uint16_t(sp() + 4));
    }
    void call_near(uint16_t off) {
        push(kTrampolineIp);
        ip() = off;
        callout(cs(), uint16_t(sp() + 2));
    }
    // Frame points (wait_vblank, wait_visible_scanline): park instead of spinning on a RAM flag or an I/O
    // port the original polls every cycle. Both leave CS:IP at this routine's own trampoline (no stack
    // push -- this isn't a call) and resume here, via the same suspended-fiber/trampoline machinery as
    // a call-out, once the hardware has moved on. Interrupts can still preempt either kind of park and run
    // their native handlers in between, exactly as they could preempt the original's loop.
    //
    // Halts the CPU (as HLT would), preserving IF: Wsc::tick keeps advancing lines/frames/sound
    // while halted, and a hardware interrupt wakes it, runs its native handler (which IRETs back to this
    // trampoline) and resumes here -- for wait_vblank, which waits on a flag the VBlank IRQ sets.
    void halt_and_wait() {
        ip() = kTrampolineIp;
        w_.cpu.s.halted = true;
        callout(cs(), sp());
    }
    // Parks until the next raster line boundary without halting or touching any hardware register: for
    // wait_visible_scanline, which polls the scanline I/O port directly rather than a flag an interrupt
    // sets, so there's no RAM write or register programming to reproduce.
    void wait_next_line() {
        ip() = kTrampolineIp;
        w_.park_until_next_line();
        callout(cs(), sp());
    }
    // A pending call-out of the running fiber: where the interpreter must stop and resume it.
    struct Callout {
        uint16_t cs = 0, sp = 0;
    };
    static Callout &pending_callout() {
        static Callout c;
        return c;
    }

    // an unconditional far jump (e.g. `ljmp seg, label`) instead of a return: the caller's stack frame is
    // abandoned as-is (nothing is popped), execution resumes at seg:off.
    void jmp_far(uint16_t seg, uint16_t off) {
        cs() = seg;
        ip() = off;
    }

  private:
    bool original_popup_text_ = false;
    emu::Wsc &w_;
    emu::CpuState &r_;
    const TextTable *text_;
    uint16_t &ip() { return r_.ip; }
    void push(uint16_t v) {
        sp() = uint16_t(sp() - 2);
        wr16(ss(), sp(), v);
    }
    void callout(uint16_t ret_cs, uint16_t ret_sp) {
        pending_callout() = {ret_cs, ret_sp};
        Fiber::yield();       // the scheduler runs the callee/wait; back here at the trampoline
        ip() = kTrampolineIp; // (already so)
    }
    uint16_t pop() {
        uint16_t v = rd16(ss(), sp());
        sp() = uint16_t(sp() + 2);
        return v;
    }
    static uint32_t linear(uint16_t seg, uint16_t off) { return ((uint32_t(seg) << 4) + off) & 0xFFFFF; }
};

// A decompiled routine. `clobbers_flags`: the original leaves FLAGS in a state no caller reads (checked by
// reading the call sites), so the native version needn't reproduce it and the differential test ignores it.
struct Routine {
    const char *name;
    uint32_t rom;
    void (*fn)(Machine &);
    bool clobbers_flags;
    bool near = false;   // called with CALL/RET (2-byte return address) rather than CALL FAR/RETF
    bool yields = false; // calls original code (Machine::call_far/call_near): runs on a Fiber
    bool irq = false;    // an interrupt handler: entered by the interrupt, returns with IRET (Machine::iret)
    bool loop = false;   // never returns (a main loop): the difftest compares at its 3rd wait_vblank entry
};

// Every decompiled routine, in ROM order.
const Routine *routines(size_t *count);
// Per-bank registries in their own files (routines_bankXX.cpp), merged into routines().
const Routine *routines_bank34(size_t *count);
const Routine *routines_bank35(size_t *count);
const Routine *routines_bank36(size_t *count);
const Routine *routines_bank36x(size_t *count);
const Routine *routines_bank37(size_t *count);
const Routine *routines_bank3c(size_t *count);
const Routine *routines_system(size_t *count);

// A Fiber running a yielding routine (Routine::yields) from the machine's current state: resume() it, and
// while it hasn't finished, interpret until CS:IP = (Machine::pending_callout().cs, kTrampolineIp) with
// SP = pending_callout().sp, then resume it again (install() does this in the game; the difftest by hand).
std::unique_ptr<Fiber> start_yielding(emu::Wsc &w, void (*fn)(Machine &));

// After Wsc::reset(): a new Session generation (natives suspended in call-outs are cancelled) and a reinstall
// (Reset Game). Hook subscriptions stay.
void reset_natives(emu::Wsc &w, const char *only = nullptr);

// Install all (or the named) routines as natives in the interpreter, scheduled by the machine's Session
// (session.h, created here if needed).
void install(emu::Wsc &w, const char *only = nullptr);

} // namespace sawgod::game
