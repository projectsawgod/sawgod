#pragma once

#include "emu/wsc.h"

namespace sawgod::tools {

inline bool tick_parked_wait(emu::Wsc &w) {
    const bool waiting = w.cpu.s.halted || w.line_parked();
    w.irq_gate = [waiting] { return waiting; };
    const bool frame_done = w.tick(false, false);
    w.irq_gate = nullptr;
    return frame_done;
}

// Difftest's virtual clock, not the runtime scheduler. Native work takes no hardware time, so the
// original VBlank wait must spend time only in its idle poll, with interrupts admitted at that same
// point. The poll reads vblank_done at SS:C80E and has no side effects while it remains zero. Parking
// it at a line boundary matches Machine::halt_and_wait without replacing the original's setup, sound
// requests, IRQ handlers, final flag test, or return with native code.
inline bool tick_vblank_wait(emu::Wsc &w) {
    constexpr uint32_t kVblankPoll = 0x340044;
    constexpr uint16_t kVblankDone = 0xC80E;
    if (w.rom_offset_of(w.cpu.pc()) == kVblankPoll &&
        w.read8((uint32_t(w.cpu.s.seg[emu::SS]) << 4) + kVblankDone) == 0)
        w.park_until_next_line();
    return tick_parked_wait(w);
}

// The raster wait polls IN AL,02 at 340053 until line < 141. Its pushes, final comparison,
// pops/return and any IRQ handler consume no time, just like the native wait_next_line path.
// In particular, a line-0 IRQ stays pending when the wait returns at the visible-frame boundary.
inline bool tick_scanline_wait(emu::Wsc &w) {
    constexpr uint32_t kScanlinePoll = 0x340053;
    if (w.rom_offset_of(w.cpu.pc()) == kScanlinePoll && w.scanline >= 141)
        w.park_until_next_line();
    return tick_parked_wait(w);
}

} // namespace sawgod::tools
