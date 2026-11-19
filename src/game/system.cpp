// Decompiled system code: power-on (reset_vector, reset), the main loop and the interrupt handlers. See
// asm/bank_34.s (main, irq_vblank), asm/bank_3C.s (irq_line_compare) and asm/bank_3F.s (reset). The field
// main loop game_main (0x350564) is in bank35_world.cpp; it is registered here with the other loop.
//
// These call many routines that are registered natives without a *_core helper in a header. They are
// called through call_native_far/call_native_near below: the same CALL frame as a call-out (return address
// at the caller's trampoline), but the native body runs directly in this fiber. This retains the original
// return slots where title/intro aborts restore an earlier stack. Title cleanup itself is fully native.
#include "game/system.h"

#include "game/gb_intro.h"
#include "game/hooks.h"

#include "game/bank34.h"
#include "game/bank34_misc.h"
#include "game/bank35_field.h"
#include "game/bank35_world.h"
#include "game/bank3c_title.h"
#include "game/bank3d_sound.h"
#include "game/computed_handlers.h"

namespace sawgod::game {

namespace {
constexpr uint16_t kBattleDebugEnabled = 0xC884;
constexpr uint16_t kIram = 0x0000;
// RAM (symbols/ram.toml names where they exist)
constexpr uint16_t kRandomSeed = 0xC80C;        // random_seed: +1 every VBlank
constexpr uint16_t kVblankDone = 0xC80E;        // vblank_done: set (incremented) every VBlank
constexpr uint16_t kFieldFrameWaiting = 0xC813; // 1 while the field frame waits for its VBlank upload
constexpr uint16_t kSceneIndex = 0xC81A;        // scene_index (main stores wait_frames' offset, 0, here)
constexpr uint16_t kIntroStrip = 0xC81C;        // intro strip / title-menu state word
constexpr uint16_t kMusicRequest = 0xC820;      // music_request
constexpr uint16_t kSfxRequest = 0xC821;        // sfx_request
constexpr uint16_t kGameState = 0xD000;         // game_state: byte 0 = FFh when no game was set up
constexpr uint16_t kPlayTimeFrames = 0xD0DF;    // play_time_frames (32-bit)
constexpr uint16_t kBattleWipeScroll = 0xD509;  // four scanline scroll pairs
constexpr uint16_t kLineScrollTable = 0xD6FE;   // per-line window X for raster lines 38h..57h
constexpr uint16_t kVblankVector = 0x98;        // IVT entry 26h (base 20h + VBlank 6)
// I/O ports
constexpr uint8_t kPortLineCurrent = 0x02, kPortLineCompare = 0x03, kPortScr2WinX0 = 0x08;
constexpr uint8_t kPortIntBase = 0xB0, kPortIntEnable = 0xB2, kPortIntAck = 0xB6;
constexpr uint8_t kIntVblank = 0x40, kIntLineCompare = 0x10;

void push16(Machine &m, uint16_t value) {
    m.sp() = uint16_t(m.sp() - 2);
    m.wr16(m.ss(), m.sp(), value);
}
uint16_t pop16(Machine &m) {
    const uint16_t value = m.rd16(m.ss(), m.sp());
    m.sp() = uint16_t(m.sp() + 2);
    return value;
}
void out16(Machine &m, uint8_t port, uint16_t value) { // `out port, ax`: low byte first
    m.out8(port, uint8_t(value));
    m.out8(uint8_t(port + 1), uint8_t(value >> 8));
}

// After a native callee: if it didn't return to its caller's frame (it ended in a jump into original code,
// a nonlocal exit), point the frame's return address at the caller's call-out trampoline and
// let the interpreter run until that code returns there. Otherwise the stack holds the original's own return
// address, as the CALL left it.
void finish_call(Machine &m, uint16_t caller_cs, uint16_t ret_sp, uint16_t slot) {
    if (m.cs() == caller_cs && m.sp() == ret_sp)
        return;
    m.wr16(m.ss(), slot, Machine::kTrampolineIp);
    Machine::pending_callout() = {caller_cs, ret_sp};
    Fiber::yield();
}
// `lcall seg:fn` to a registered native (fn ends with RETF); ret_ip = the IP after the original's lcall.
void call_native_far(Machine &m, uint16_t seg, uint16_t ret_ip, void (*fn)(Machine &)) {
    const uint16_t caller_cs = m.cs();
    push16(m, caller_cs);
    push16(m, ret_ip);
    const uint16_t slot = m.sp(), ret_sp = uint16_t(slot + 4);
    m.cs() = seg;
    fn(m);
    finish_call(m, caller_cs, ret_sp, slot);
}
// `call fn` to a registered near native in the current segment (fn ends with RET).
void call_native_near(Machine &m, uint16_t ret_ip, void (*fn)(Machine &)) {
    push16(m, ret_ip);
    const uint16_t caller_cs = m.cs(), slot = m.sp(), ret_sp = uint16_t(slot + 2);
    fn(m);
    finish_call(m, caller_cs, ret_sp, slot);
}
// A far routine's *_core (no return) run in its own segment, as `lcall seg:...` would.
void with_cs(Machine &m, uint16_t seg, void (*core)(Machine &)) {
    const uint16_t caller_cs = m.cs();
    m.cs() = seg;
    core(m);
    m.cs() = caller_cs;
}

// `pushaw; push ds; push es; push ss` and the matching pops.
void push_all(Machine &m) {
    const uint16_t sp = m.sp();
    for (uint16_t register_value :
         {m.ax(), m.cx(), m.dx(), m.bx(), sp, m.bp(), m.si(), m.di(), m.ds(), m.es(), m.ss()})
        push16(m, register_value);
}
void pop_all(Machine &m) {
    m.ss() = pop16(m);
    m.es() = pop16(m);
    m.ds() = pop16(m);
    m.di() = pop16(m);
    m.si() = pop16(m);
    m.bp() = pop16(m);
    pop16(m); // popaw skips SP
    m.bx() = pop16(m);
    m.dx() = pop16(m);
    m.cx() = pop16(m);
    m.ax() = pop16(m);
}
} // namespace

// rom: 0x340EE0
// Main entry after reset (and soft reset / game over, which jump here): hardware init, then the title loop.
// Disables interrupts, sets the interrupt base (20h), colour mode, stack 0000:FE00, display/scroll/window
// registers; clears tiles and palettes, loads the font tiles and palettes from bank 0x39, clears the debug
// and sound-request bytes, starts the sound driver, installs irq_vblank and enables the VBlank interrupt.
// Loop: opening, clear_game_state, title_screen, then title_menu: 1 = new game (prologue, new_game_setup,
// game_main), 2 = load (load_menu; a loaded game goes through the party menu into game_main), 3 =
// ORIJINARU (jumps to F000:0040, out of scope: original code), anything else (timeout) back to the opening.
// Never returns. The arithmetic flags of the `xor`/`cmp` instructions are not reproduced (main has no
// caller and nothing reads them before they are overwritten).
void main(Machine &m) {
    m.flags() &= uint16_t(~emu::IF); // cli
    m.set_al(0);
    m.out8(kPortIntEnable, 0);
    m.set_al(0x20);
    m.out8(kPortIntBase, 0x20);
    m.set_al(0xF0);
    m.out8(kPortIntAck, 0xF0);
    m.set_al(0xE0);
    call_native_far(m, 0x4000, 0x0EF4, set_color_hardware_mode);
    m.ax() = 0;
    m.ss() = 0;
    m.sp() = 0xFE00;
    m.ax() = 0xF100;
    out16(m, 0x00, m.ax()); // display control
    m.set_al(1);
    m.out8(0x04, 1); // sprite table base
    m.set_al(0);
    m.out8(0x05, 0); // first sprite
    m.out8(0x06, 0); // sprite count
    m.set_al(0x32);
    m.out8(0x07, 0x32); // screen map bases
    m.ax() = 0;
    for (uint8_t port :
         {uint8_t(0x10), uint8_t(0x12), uint8_t(0x08), uint8_t(0x0A), uint8_t(0x0C), uint8_t(0x0E)})
        out16(m, port, 0); // scroll and window registers
    call_native_far(m, 0x4000, 0x0F34, clear_tiles);
    call_native_far(m, 0x4000, 0x0F39, clear_hardware_palettes);
    m.ds() = 0x9000;
    m.ax() = 0x100;
    m.cx() = 0x100;
    m.si() = 0x0080; // D_390080: font tiles
    copy_tiles_core(m);
    m.ax() = 0xF;
    m.cx() = 4;
    m.si() = 0x0000; // D_390000: palettes
    m.di() = 0xC600;
    copy_palettes_core(m);
    m.ax() = 0;
    for (uint16_t off : {uint16_t(0xC880), uint16_t(0xC881), uint16_t(0xC883)})
        m.wr8(m.ss(), off, 0);
    m.set_al(1);
    m.wr8(m.ss(), kBattleDebugEnabled, 1); // battle debug auto-win allowed
    m.ax() = 0;
    for (uint16_t off : {uint16_t(0xC823), kMusicRequest, kSfxRequest, uint16_t(0xC824)})
        m.wr8(m.ss(), off, 0);
    m.set_al(0xFF);
    m.wr8(m.ss(), kMusicRequest, 0xFF);
    m.ax() = 0;
    m.ds() = 0;
    call_native_far(m, 0xD000, 0x0F92, sound_init);
    m.ax() = 0;
    m.wr16(m.ss(), kRandomSeed, 0);
    m.wr8(m.ss(), kFieldFrameWaiting, 0);
    m.ax() = 0;
    m.ds() = 0;
    m.bx() = kVblankVector;
    m.wr16(kIram, kVblankVector, 0x114E); // irq_vblank
    m.wr16(kIram, kVblankVector + 2, 0x4000);
    m.set_al(kIntVblank);
    m.out8(kPortIntEnable, kIntVblank);
    m.flags() |= emu::IF; // sti

    for (;;) { // L_340FB2
        const bool intro = hooks::should(hooks::VB::PlayIntro, m);
        if (intro)
            call_native_far(m, 0xC000, 0x0FB7, opening);
        clear_game_state_core(m);
        m.wr16(m.ss(), kSceneIndex, 0x0000); // offset wait_frames
        if (intro)
            call_native_far(m, 0xC000, 0x0FC6, title_screen);
        m.wr16(m.ss(), kIntroStrip, 0x0000); // offset wait_frames
        for (;;) {                           // L_340FCD
            call_native_far(m, 0xC000, 0x0FD2, title_menu);
            m.wr16(m.ss(), kIntroStrip, 1);
            const uint16_t choice = m.ax();
            bool new_game = false;
            if (choice == 1) { // new game
                call_native_near(m, 0x1020, fade_menu_palettes);
                new_game = true;
            } else if (choice == 2) { // load
                call_native_near(m, 0x1043, fade_menu_palettes);
                clear_game_state_core(m);
                load_menu_core(m);
                if (!(m.flags() & emu::CF)) {
                    new_game = true; // no save loaded: start a new game
                } else {
                    m.ax() = 0;
                    m.bx() = 8;
                    open_party_menu_core(m);
                    const uint16_t ax = m.ax();
                    with_cs(m, 0x5000, fade_palettes_16_core);
                    m.ax() = ax;
                    if (m.al() == 0) {
                        hooks::emit(hooks::Event::SaveLoaded, m, {});
                        call_native_far(m, 0x5000, 0x106A, game_main);
                    }
                    continue;
                }
            } else { // 3: ORIJINARU; else (title timeout): fade out and replay the opening
                m.ax() = 2;
                m.cx() = 0x0101;
                const bool orijinaru = choice == 3;
                call_native_far(m, 0xD000, orijinaru ? 0x100D : 0x0FF4, sound_channel_reset);
                call_native_near(m, orijinaru ? 0x1010 : 0x0FF7, fade_menu_palettes);
                m.cx() = 0x1E;
                wait_frames_core(m);
                if (orijinaru) {
                    m.jmp_far(0xF000, 0x0040); // orijinaru_main (out of scope: original code)
                    return;
                }
                break;
            }
            if (new_game) { // L_341020
                call_native_far(m, 0xC000, 0x1025, prologue);
                clear_game_state_core(m);
                m.set_al(0);
                m.bx() &= 0xFF00; // mov bl, 0
                new_game_setup_core(m);
                if (m.rd8(m.ss(), kGameState) != 0xFF)
                    call_native_far(m, 0x5000, 0x103E, game_main);
            }
        }
    }
}

// rom: 0x34114E
// VBlank interrupt handler (vector 26h). Acknowledges the interrupt; if a field frame is waiting
// (kFieldFrameWaiting) uploads it (upload_field_display: display registers, palettes, map effect); runs the
// sound driver (sound_tick, DS = DX = 0); increments random_seed, vblank_done and the 32-bit play time.
// All registers preserved (pushaw/popaw and the segment pushes); IRET.
void irq_vblank(Machine &m) {
    push_all(m);
    m.set_al(kIntVblank);
    m.out8(kPortIntAck, kIntVblank);
    m.set_al(m.rd8(m.ss(), kFieldFrameWaiting));
    if (m.al())
        call_native_far(m, 0x5000, 0x1163, upload_field_display);
    m.ax() = 0;
    m.ds() = 0;
    m.dx() = 0;
    if (m.wsc().audio_clock_enabled)
        call_native_far(m, 0xD000, 0x116E, sound_tick);
    m.wr16(m.ss(), kRandomSeed, uint16_t(m.rd16(m.ss(), kRandomSeed) + 1));
    m.wr8(m.ss(), kVblankDone, uint8_t(m.rd8(m.ss(), kVblankDone) + 1));
    const uint32_t t =
        (m.rd16(m.ss(), kPlayTimeFrames) | uint32_t(m.rd16(m.ss(), kPlayTimeFrames + 2)) << 16) + 1;
    m.wr16(m.ss(), kPlayTimeFrames, uint16_t(t));
    m.wr16(m.ss(), kPlayTimeFrames + 2, uint16_t(t >> 16));
    pop_all(m);
    m.iret();
}

// rom: 0x3C4EA0
// Line-compare interrupt handler (vector 24h), installed by the title/intro scenes: a raster effect. For
// lines below 58h it writes the byte for this line from kLineScrollTable (indexed by line - 38h, 8-bit) to
// SCR2 window X0 (port 08h) and sets the next line compare to line + 1; from line 58h on it rearms the
// compare at line 38h. AX and BX preserved; IRET.
void irq_line_compare(Machine &m) {
    const uint16_t ax = m.ax(), bx = m.bx();
    m.out8(kPortIntAck, kIntLineCompare);
    const uint8_t line = m.in8(kPortLineCurrent);
    if (line < 0x58) {
        const uint16_t entry = uint16_t(kLineScrollTable + uint8_t(line - 0x38)); // 8-bit `sub al, 38h`
        m.out8(kPortScr2WinX0, m.rd8(m.ss(), entry));
        m.out8(kPortLineCompare, uint8_t(m.in8(kPortLineCurrent) + 1));
    } else {
        m.out8(kPortLineCompare, 0x38);
    }
    m.ax() = ax;
    m.bx() = bx;
    m.iret();
}

// rom: 0x350122
// Field dialogue overlay IRQ (line compare): from this line on, SCR2 shows the dialogue box -- its map base
// (port 07h high nibble) becomes 4 (internal RAM 0x2000) with no scroll (ports 12h/13h = 0). Acknowledges
// the line-compare interrupt. Every register preserved (pushaw/push ds-es-ss in the original); IRET.
void irq_field_dialogue_split(Machine &m) {
    m.out8(0x07, uint8_t((m.in8(0x07) & 0x0F) | 0x40));
    m.out8(0x12, 0); // out 12h, ax: SCR2 scroll X and Y
    m.out8(0x13, 0);
    m.out8(kPortIntAck, kIntLineCompare);
    m.iret();
}

// rom: 0x3C3C94
// Achievement window IRQ: stream 32 horizontal edges at lines 30h..4Fh, then rearm at 30h.
void irq_achievement_window(Machine &m) {
    const uint16_t ax = m.ax(), bx = m.bx();
    m.out8(kPortIntAck, kIntLineCompare);
    const uint8_t line = m.in8(kPortLineCurrent);
    if (line < 0x50) {
        const uint16_t entry = uint16_t(kLineScrollTable + uint8_t(line - 0x30));
        m.out8(kPortScr2WinX0, m.rd8(m.ss(), entry));
        hooks::emit(hooks::Event::IntroPresentation, m,
                    {int(IntroStage::WorldCardRow), line - 0x30, m.rd8(m.ss(), entry)});
        m.out8(kPortLineCompare, uint8_t(m.in8(kPortLineCurrent) + 1));
    } else {
        m.out8(kPortLineCompare, 0x30);
    }
    m.ax() = ax;
    m.bx() = bx;
    m.iret();
}

// rom: 0x351310
// Battle-entry raster IRQ: cycle the four scroll pairs and advance the compare through line 8Fh.
void irq_battle_entry_scroll(Machine &m) {
    const uint16_t ax = m.ax(), bx = m.bx();
    const uint16_t pair = uint16_t((m.in8(kPortLineCompare) & 3) * 2);
    out16(m, 0x10, m.rd16(m.ss(), uint16_t(kBattleWipeScroll + pair)));
    uint8_t next = uint8_t(m.in8(kPortLineCompare) + 1);
    if (next >= 0x90)
        next = 0;
    m.out8(kPortLineCompare, next);
    m.out8(kPortIntAck, kIntLineCompare);
    m.ax() = ax;
    m.bx() = bx;
    m.iret();
}

// rom: 0x35BC4D
// Travel-item warp IRQ: stream the current row's distortion to both horizontal scroll registers,
// schedule the next line (wrapping at 144), and acknowledge line compare. AX/BX and flags preserved.
void irq_destination_warp(Machine &m) {
    const uint16_t ax = m.ax(), bx = m.bx();
    constexpr uint16_t kEffectDistortion = 0xDCFE;
    m.out8(kPortIntAck, kIntLineCompare);
    const uint8_t offset = m.rd8(m.ss(), uint16_t(kEffectDistortion + m.in8(kPortLineCurrent)));
    m.out8(0x10, offset);
    m.out8(0x12, offset);
    const uint8_t next = uint8_t(m.in8(kPortLineCurrent) + 1);
    m.out8(kPortLineCompare, next < 0x90 ? next : 0);
    m.ax() = ax;
    m.bx() = bx;
    m.iret();
}

// rom: 0x36C7C2
// Boss-defeat IRQ: stream a shutter row's scroll and window edges below line 78; otherwise reset the
// window/scroll and rearm at line zero. AX/BX and flags preserved.
void irq_battle_raster_shutter(Machine &m) {
    const uint16_t ax = m.ax(), bx = m.bx();
    constexpr uint16_t kTransitionRows = 0xC885;
    const uint8_t line = m.in8(kPortLineCurrent);
    if (line < 0x4E) {
        const uint16_t row = uint16_t(kTransitionRows + line * 4);
        m.out8(0x12, m.rd8(m.ss(), row));
        m.out8(0x08, m.rd8(m.ss(), uint16_t(row + 2)));
        m.out8(0x0A, m.rd8(m.ss(), uint16_t(row + 3)));
        m.out8(kPortLineCompare, uint8_t(m.in8(kPortLineCurrent) + 1));
    } else {
        out16(m, 0x08, 0);
        out16(m, 0x0A, 0x8FDF);
        m.out8(0x12, 0);
        m.out8(kPortLineCompare, 0);
    }
    m.out8(kPortIntAck, kIntLineCompare);
    m.ax() = ax;
    m.bx() = bx;
    m.iret();
}

// rom: 0x36C681
// God's defeat raster IRQ: apply a row's opposing scroll and window edges below line 78, or restore
// the fixed display below it. Original quirk: the computed next line is discarded; acknowledge HTimer
// (80h), with no write to the line-compare port. AX/BX and flags preserved.
void irq_battle_raster_sweep(Machine &m) {
    const uint16_t ax = m.ax(), bx = m.bx();
    constexpr uint16_t kTransitionRows = 0xC885;
    const uint8_t line = m.in8(kPortLineCurrent);
    if (line < 0x4E) {
        const uint16_t row = uint16_t(kTransitionRows + line * 4);
        const uint8_t scroll = m.rd8(m.ss(), row);
        out16(m, 0x10, uint16_t(0xFC00 | scroll));
        out16(m, 0x12, uint16_t(0x0400 | uint8_t(-scroll)));
        m.out8(0x08, m.rd8(m.ss(), uint16_t(row + 2)));
        m.out8(0x0A, m.rd8(m.ss(), uint16_t(row + 3)));
        (void)m.in8(kPortLineCurrent);
    } else {
        out16(m, 0x08, 0);
        out16(m, 0x0A, 0x8FDF);
        out16(m, 0x10, 8);
        out16(m, 0x12, 0x400);
    }
    m.out8(kPortIntAck, 0x80);
    m.ax() = ax;
    m.bx() = bx;
    m.iret();
}

namespace {
constexpr uint16_t kRasterWaveOffset = 0xD6B5, kRasterWavePhase = 0xD6B6, kRasterWaveSource = 0xD6B7;

// Both wave IRQs shift each byte independently: carries never propagate between X and Y scroll.
void raster_wave_row(Machine &m, uint16_t segment, uint8_t line_port, uint8_t rows) {
    const uint16_t scroll = m.rd16(segment, kRasterWaveSource);
    out16(m, 0x10, uint16_t((scroll & 0xFF00) | uint8_t(scroll + 8)));
    out16(m, 0x12, uint16_t((uint16_t(uint8_t((scroll >> 8) + 4)) << 8) | uint8_t(scroll)));
    const uint8_t line = m.in8(line_port);
    m.set_al(uint8_t((line << 4) + m.rd8(segment, kRasterWaveOffset)));
    with_cs(m, 0x4000, lookup_wave_sample_core);
    const int16_t product = int16_t(int8_t(m.al())) * int16_t(m.rd8(segment, kRasterWavePhase) >> 1);
    // IMUL's high byte, then SAR AL,3: arithmetic division rounds toward negative infinity.
    const int8_t displacement = int8_t(int8_t(uint16_t(product) >> 8) >> 3);
    const uint8_t displaced_line = uint8_t(line + displacement);
    const uint8_t vertical = displaced_line < rows ? uint8_t(displacement) : 0;
    m.wr16(segment, kRasterWaveSource, uint16_t((uint16_t(vertical) << 8) | uint8_t(displacement)));
    m.out8(kPortLineCompare, uint8_t(m.in8(line_port) + 1));
}
} // namespace

// rom: 0x364CBC
// Decaying battle-wave IRQ, indexed by the programmed compare line (not the current raster). Wave
// state is in segment zero. The middle region advances four rows at a time; all registers preserved.
void irq_battle_raster_wave(Machine &m) {
    const auto registers = m.wsc().cpu.s;
    const uint8_t line = m.in8(kPortLineCompare);
    if (line < 0x4F) {
        raster_wave_row(m, 0, kPortLineCompare, 0x4E);
    } else {
        out16(m, 0x10, 8);
        out16(m, 0x12, 0x400);
        m.out8(kPortLineCompare, line < 0x8F ? uint8_t(m.in8(kPortLineCurrent) + 4) : 0);
    }
    m.out8(kPortIntAck, kIntLineCompare);
    m.wsc().cpu.s = registers;
    m.iret();
}

// rom: 0x377EFA
// Nuclear flash IRQ: scroll the upper region, roll a new signed X/Y pair in the middle, reset below
// line 140. RNG counter writes are gameplay state; all registers are preserved.
void irq_nuclear_flash(Machine &m) {
    const auto registers = m.wsc().cpu.s;
    m.out8(kPortIntAck, kIntLineCompare);
    const uint8_t line = m.in8(kPortLineCurrent);
    if (line < 0x4E) {
        out16(m, 0x12, m.rd16(m.ss(), kRasterWaveSource));
        m.out8(kPortLineCompare, 0x4E);
    } else {
        if (line < 0x8C) {
            random_byte_call_core(m);
            const uint8_t x = uint8_t((m.al() & 7) - 4);
            random_byte_call_core(m);
            const uint8_t y = uint8_t((m.al() & 7) - 4);
            m.wr16(m.ss(), kRasterWaveSource, uint16_t((uint16_t(y) << 8) | x));
        }
        out16(m, 0x10, 8);
        out16(m, 0x12, 0x400);
        m.out8(kPortLineCompare, 0);
    }
    m.wsc().cpu.s = registers;
    m.iret();
}

// rom: 0x377F50
// Nuclear decay-wave IRQ: the current raster indexes SS-relative wave state, with a 70-row vertical
// bound. Restore fixed scroll below the upper region; all registers preserved.
void irq_nuclear_wave(Machine &m) {
    const auto registers = m.wsc().cpu.s;
    m.out8(kPortIntAck, kIntLineCompare);
    if (m.in8(kPortLineCurrent) < 0x4F) {
        raster_wave_row(m, m.ss(), kPortLineCurrent, 0x46);
    } else {
        out16(m, 0x10, 8);
        out16(m, 0x12, 0x400);
        m.out8(kPortLineCompare, 0);
    }
    m.wsc().cpu.s = registers;
    m.iret();
}

namespace {
// Shared body of ROM 0x3FC7B0: `in al, 0A0h; test al, 2` (exact flags); true on a WonderSwan Color.
bool boot_color_check_core(Machine &m) {
    m.set_al(m.in8(0xA0));
    const uint8_t r = m.al() & 2;
    m.flags() &= uint16_t(~(emu::CF | emu::OF | emu::AF | emu::SF | emu::ZF | emu::PF));
    if (!r)
        m.flags() |= emu::ZF | emu::PF; // 0: even parity
    return r != 0;                      // 2: one bit set, odd parity
}
// The IP of a linear address in the current code segment: bank 0x3F code runs as FC00:0xxx after power-on
// (reset_vector's jump) but as F000:Cxxx when called through the usual bank mapping.
uint16_t ip_in_cs(Machine &m, uint32_t linear) { return uint16_t(linear - (uint32_t(m.cs()) << 4)); }
} // namespace

// rom: 0x3FC000
// Power-on hardware init, reached from reset_vector. Interrupts off, all registers 0, stack 0000:2000;
// display, sprite, scroll, window, LCD (1Ch/1Eh: 8ACFh, 0246h), palette (20h-3Eh), sound (80h-91h) ports
// cleared; the colour-hardware boot check (boot_color_check); SRAM bank 0 (port C1h); jumps to main. On a
// mono WonderSwan the check never returns (its "colour only" screen is left to the original code).
void reset(Machine &m) {
    m.flags() &= uint16_t(~emu::IF); // cli
    m.ax() = m.bx() = m.cx() = m.dx() = 0;
    m.ds() = m.es() = m.ss() = 0;
    m.bp() = 0;
    m.sp() = 0x2000;
    m.out8(kPortIntEnable, 0);
    out16(m, 0x00, 0);
    for (uint8_t port : {uint8_t(0x04), uint8_t(0x05), uint8_t(0x06)})
        m.out8(port, 0);
    m.set_al(0x32);
    m.out8(0x07, 0x32);
    for (uint8_t port :
         {uint8_t(0x10), uint8_t(0x12), uint8_t(0x08), uint8_t(0x0A), uint8_t(0x0C), uint8_t(0x0E)})
        out16(m, port, 0);
    out16(m, 0x1C, 0x8ACF); // LCD shade levels
    out16(m, 0x1E, 0x0246);
    for (unsigned port = 0x20; port <= 0x3E; port += 2) // mono palettes
        out16(m, uint8_t(port), 0);
    m.out8(0x90, 0); // sound control
    m.out8(0x91, 0); // sound output
    for (uint8_t port :
         {uint8_t(0x88), uint8_t(0x89), uint8_t(0x8A), uint8_t(0x8B), uint8_t(0x80), uint8_t(0x82),
          uint8_t(0x84), uint8_t(0x86)}) // channel volumes (overlapping word writes), frequencies
        out16(m, port, 0);
    m.ax() = 0;
    push_all(m);
    push16(m, ip_in_cs(m, 0xFC0DC)); // call boot_color_check
    if (!boot_color_check_core(m)) {
        m.jmp_far(m.cs(), ip_in_cs(m, 0xFC7B7)); // mono WonderSwan: the "colour only" screen (original code)
        return;
    }
    pop16(m); // ret
    pop_all(m);
    m.set_al(0);
    m.out8(0xC1, 0);           // SRAM bank 0
    m.jmp_far(0x4000, 0x0EE0); // main
}

// rom: 0x3FC7B0 (near)
// Colour-hardware boot check (from reset): returns if port A0h bit 1 (WonderSwan Color) is set, with the
// flags of `test al, 2` and AL = port A0h; otherwise shows the "colour only" screen forever (left to the
// original code: it can't run on the WonderSwan Color this port targets).
void boot_color_check(Machine &m) {
    if (!boot_color_check_core(m)) {
        m.jmp_far(m.cs(), ip_in_cs(m, 0xFC7B7));
        return;
    }
    m.ret();
}

// rom: 0x3FFFF0
// Power-on entry: jmp FC00:0000 (reset).
void reset_vector(Machine &m) { m.jmp_far(0xFC00, 0x0000); }

} // namespace sawgod::game
