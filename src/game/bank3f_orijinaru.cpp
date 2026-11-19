// Decompiled routines from bank 0x3F (ORIJINARU engine). See asm/bank_3F.s.
// ARCHIVED: ORIJINARU is out of scope (docs/ORIJINARU.md). Kept building and verified; no new work.
//
// ORIJINARU is a separate, self-contained engine that main jumps into (orijinaru_main, 0x3F0040) and never
// returns from. It runs entirely at CS=SS=F000h and addresses all of its own working RAM SS-relative (no
// DS/ES juggling for globals the way bank 0x34 or 0x3D do); DS is instead repointed per sub-mode to select
// which ROM bank a script/tile-data cursor reads from (0x2000 or 0x3000 seen so far). Structure:
//   - A keypad scanner (orij_scan_keypad, near) that mirrors bank34.cpp's read_keypad almost exactly (same
//     3-nibble bit-banged port 0xB5 scan, same auto-repeat state machine) but additionally remaps the raw
//     12-bit reading into a Game-Boy-style button bit layout (orij_map_keys) for the rest of this engine.
//   - A GDMA-based sprite flush (orij_dma_sprite_table / orij_dma_sprite_table_from) that copies a 0x200-
//     byte staging buffer at SS:0x7000 out to the hardware sprite table (0x0200), and a sprite-position
//     helper (orij_sprite_quad_positions) that lays out a 2x2 grid of 8x8 sub-sprites -- the ORIJINARU
//     equivalent of bank34.cpp's set_sprite_16x16, just split into separate near helpers for tile number,
//     attribute and position (only the position one, L_3F0CD0, is here).
//   - orij_random / orij_random_range (already named in symbols/functions.toml): orij_random picks one of
//     128 per-call-site counters from the sum of its own return-address bytes, then feeds the counter into
//     a 256-byte SS:0xCB00 table as the "random" byte orij_random_range scales into [low, high].
//   - orij_read_script_byte (L_3F1120), a trivial "read next byte at DS:[the SS:0xF099 cursor], advance
//     it" helper, and L_3F1130 (NOT implemented -- see below), the opcode dispatcher that uses it: reads
//     one opcode byte and, depending on its range, either indexes one of several byte tables (D_3F1771,
//     D_3F1821, D_3F17EF, D_3F1817) to remap it into a tile-placement write, or falls into a long chain of
//     labelled opcode handlers (L_3F1223, L_3F124D, L_3F129C, L_3F12B6, L_3F12C9, L_3F12E9, L_3F1309,
//     L_3F134D, L_3F1355, ... continuing well past L_3F1668) that between them implement what looks like
//     the whole ORIJINARU script/text bytecode language (cursor moves, number formatting via a call into
//     bank34's number_to_digits, screen-buffer writes). That is a full subsystem on the order of the sound
//     driver's sound_decode_track_commands bytecode interpreter, not a single routine; it needs its own
//     dedicated effort. L_3F1120 is implemented since it's a real, independently
//     near-callable routine that happens to also be this dispatcher's opcode-byte reader.
#include "game/bank3f_orijinaru.h"

namespace sawgod::game {

namespace {
// Working RAM, all SS-relative (internal RAM; SS holds the same value throughout ORIJINARU mode).
constexpr uint16_t kKeysRaw = 0x7402;          // this frame's raw 12-bit key-matrix reading
constexpr uint16_t kKeysPrevRaw = 0x7404;      // previous frame's raw reading
constexpr uint16_t kKeysPressedRaw = 0x7406;   // bits newly pressed this frame (raw layout)
constexpr uint16_t kKeysRepeatRaw = 0x7408;    // auto-repeat "fire" value (raw layout)
constexpr uint16_t kKeyRepeatTimer = 0x740A;   // frames left before the next auto-repeat
constexpr uint16_t kKeysMapped = 0xF08D;       // kKeysRaw remapped to GB-style button bits
constexpr uint16_t kKeysRepeatMapped = 0xF08E; // kKeysRepeatRaw remapped the same way
constexpr uint16_t kCursorRow = 0xF092;        // menu-cursor row, in tiles
constexpr uint16_t kCursorCol = 0xF093;        // menu-cursor column, in tiles
constexpr uint16_t kCursorObj = 0x7090;        // menu-cursor's fixed 16x16 sprite-object slot
constexpr uint16_t kScriptCursor = 0xF099;     // DS:[here] read pointer consumed by orij_read_script_byte
constexpr uint16_t kSpriteStaging = 0x7000;    // this engine's own sprite-object staging buffer
constexpr uint16_t kRandomSlots = 0xC300;      // 128 per-call-site counters (indexed by return-address sum)
constexpr uint16_t kRandomTable = 0xCB00;      // 256-byte "random" source table, indexed by those counters

// `out port, ax`: low byte to `port`, high byte to `port + 1` (matches V30MZ::out16 in src/emu/v30mz.cpp).
void out16(Machine &m, uint8_t port, uint16_t value) {
    m.out8(port, uint8_t(value));
    m.out8(uint8_t(port + 1), uint8_t(value >> 8));
}

// Shared core of orij_map_keys: ORs the GB-style button bits for AX's raw 12-bit reading into DL (DH and
// AX are left untouched, matching the original's `test ax, mask` / `or dl, bit` chain).
void orij_map_keys_core(Machine &m) {
    uint16_t ax = m.ax();
    uint8_t dl = uint8_t(m.dx());
    if (ax & 0x0040)
        dl = uint8_t(dl | 0x80);
    if (ax & 0x0010)
        dl = uint8_t(dl | 0x40);
    if (ax & 0x0080)
        dl = uint8_t(dl | 0x20);
    if (ax & 0x0020)
        dl = uint8_t(dl | 0x10);
    if (ax & 0x0002)
        dl = uint8_t(dl | 0x08);
    if (ax & 0x0400)
        dl = uint8_t(dl | 0x04);
    if (ax & 0x0008)
        dl = uint8_t(dl | 0x02);
    if (ax & 0x0004)
        dl = uint8_t(dl | 0x01);
    m.dx() = uint16_t((m.dx() & 0xFF00) | dl);
}

// Shared core of orij_sprite_quad_positions: writes the 4 position words of a 2x2 grid of 8x8 sub-sprites
// (object array at DI, tile-number/attribute words at DI+0/1, DI+4/5, DI+8/9, DI+0xC/0xD already filled in
// by the caller) at DI+2, DI+6, DI+0xA, DI+0xE. DX in: DH = Y baseline, DL = X baseline (before the
// original's `xchg dl, dh`); DX out = DX in (pushed/popped around the whole body in the original, so the
// mid-routine `xchg` never escapes). DI, BX, AX untouched.
void orij_sprite_quad_positions_core(Machine &m) {
    uint16_t di = m.di();
    uint16_t entry_dx = m.dx();
    uint8_t y = uint8_t(entry_dx >> 8);            // old DH
    uint8_t x = uint8_t(uint8_t(entry_dx) + 0x20); // old DL + the hardware sprite-coordinate origin
    auto word = [](uint8_t lo, uint8_t hi) { return uint16_t(lo | (uint16_t(hi) << 8)); };
    m.wr16(m.ss(), uint16_t(di + 2), word(y, x)); // top-left
    x = uint8_t(x + 8);
    m.wr16(m.ss(), uint16_t(di + 6), word(y, x)); // top-right
    y = uint8_t(y + 8);
    m.wr16(m.ss(), uint16_t(di + 0xE), word(y, x)); // bottom-right
    x = uint8_t(x - 8);
    m.wr16(m.ss(), uint16_t(di + 0xA), word(y, x)); // bottom-left
    m.dx() = entry_dx;
}

// Shared core of orij_random_range: AL = low bound, AH = high bound, SI = pointer to the "random" source
// byte (SS-relative; the caller picks which byte to use -- orij_random points it at its own per-call-site
// table entry, but other callers can point it anywhere). Returns AL = scaled value in [low, high], AH = 0,
// except AL = 0xFF when AL was already 0xFF, and AX = 0 when AH = 0, both special-cased before the SI byte
// is even read. BX, DX, SI untouched (the original's push/pop bx/dx around the general case is redundant
// with never touching them at all here).
void orij_random_range_core(Machine &m) {
    uint8_t lo = m.al();
    uint8_t hi = m.ah();
    if (lo == 0xFF) {
        m.ax() = 0xFF;
        return;
    }
    if (hi == 0) {
        m.ax() = 0;
        return;
    }
    if (lo == hi) {
        m.set_ah(0); // AL keeps its (only valid) value
        return;
    }
    uint8_t range = uint8_t(hi - lo);
    uint8_t r = m.rd8(m.ss(), m.si());
    // `mul dx` with AX = range (AH = 0) and DX = r:0x80: a 16x16 multiply whose high word (DX) is the
    // scaled-and-rounded result (multiplying by r*256+0x80 before truncating rounds to nearest). The
    // following `and ax, 0x80` / `je` in the original tests the low word of the same product and always
    // falls through to the very next instruction either way -- dead code, not reproduced.
    uint32_t product = uint32_t(range) * uint32_t((uint16_t(r) << 8) | 0x80);
    uint8_t scaled = uint8_t(product >> 16);
    m.set_al(uint8_t(scaled + lo));
    m.set_ah(0);
}
} // namespace

// rom: 0x3F0E1D (near)
// Fills CL bytes (0 means 256, the usual dec/jne idiom) with AL starting at SS:[DI], advancing DI past the
// last byte written. AL, BX, CH untouched; CL ends at 0.
void orij_fill_bytes8(Machine &m) {
    uint8_t cl = uint8_t(m.cx());
    uint16_t di = m.di();
    do {
        m.wr8(m.ss(), di, m.al());
        di = uint16_t(di + 1);
    } while (--cl != 0);
    m.di() = di;
    m.cx() = uint16_t(m.cx() & 0xFF00);
    m.ret();
}

// rom: 0x3F0E26 (near)
// Same as orij_fill_bytes8 but with a 16-bit count in CX (0 means 65536). AL, BX untouched; CX ends at 0.
void orij_fill_bytes16(Machine &m) {
    uint16_t cx = m.cx();
    uint16_t di = m.di();
    uint8_t al = m.al();
    uint32_t n = cx == 0 ? 0x10000u : cx;
    for (uint32_t i = 0; i < n; ++i) {
        m.wr8(m.ss(), di, al);
        di = uint16_t(di + 1);
    }
    m.di() = di;
    m.cx() = 0;
    m.ret();
}

// rom: 0x3F0E4F (near)
// ORIJINARU's per-frame keypad scan: bit-bangs the same 3-nibble port-0xB5 read as bank34.cpp's
// read_keypad into a 12-bit raw reading (kKeysRaw), updates kKeysPrevRaw/kKeysPressedRaw and the
// auto-repeat state (kKeysRepeatRaw/kKeyRepeatTimer) with the identical state machine, then additionally
// remaps kKeysRaw and kKeysRepeatRaw into GB-style button bits (via orij_map_keys) at kKeysMapped/
// kKeysRepeatMapped. Two reset combos abandon the call entirely (the BX/DX pushed at entry are never
// popped, left as dead stack below the new SP -- matches read_keypad's main-jump case): all of bits
// 0,1,2,3 (0x0E) held at once jumps (far) to main (0x340EE0); bits 2,3,9,10 (0x060C) jumps (near, same
// segment) to L_3F018F, a from-scratch hardware reset. Returns AX = the raw reading; BX, DX preserved
// (restored from entry on the normal path; on the reset paths AX = DX = the raw reading, matching the
// original's `mov ax, dx` immediately before each combo test).
void orij_scan_keypad(Machine &m) {
    uint16_t entry_dx = m.dx();

    uint16_t prev = m.rd16(m.ss(), kKeysRaw);
    m.wr16(m.ss(), kKeysPrevRaw, prev);

    uint16_t dx = 0;
    m.out8(0xB5, 0x10);
    m.out8(0xB5, 0x10);
    dx = uint16_t((dx | (m.in8(0xB5) & 0xF)) << 4);
    m.out8(0xB5, 0x20);
    dx = uint16_t((dx | (m.in8(0xB5) & 0xF)) << 4);
    m.out8(0xB5, 0x40);
    dx = uint16_t(dx | (m.in8(0xB5) & 0xF));

    m.wr16(m.ss(), kKeysRaw, dx);
    uint16_t pressed = uint16_t((dx ^ prev) & dx);
    m.wr16(m.ss(), kKeysPressedRaw, pressed);

    if (prev == dx) {
        uint16_t timer = uint16_t(m.rd16(m.ss(), kKeyRepeatTimer) - 1);
        if (timer == 0) {
            m.wr16(m.ss(), kKeyRepeatTimer, 7);
            m.wr16(m.ss(), kKeysRepeatRaw, dx);
        } else {
            m.wr16(m.ss(), kKeyRepeatTimer, timer);
            m.wr16(m.ss(), kKeysRepeatRaw, 0);
        }
    } else {
        m.wr16(m.ss(), kKeysRepeatRaw, pressed);
        m.wr16(m.ss(), kKeyRepeatTimer, 0x28);
    }

    auto abandon = [&](uint16_t seg, uint16_t off) {
        m.ax() = dx;
        m.dx() = dx;
        m.sp() = uint16_t(m.sp() - 4);
        m.wr16(m.ss(), m.sp(), dx);                   // where the original's `push dx` ended up
        m.wr16(m.ss(), uint16_t(m.sp() + 2), m.bx()); // where `push bx` ended up (BX itself untouched)
        m.jmp_far(seg, off);
    };
    if ((dx & 0x000E) == 0x000E) {
        abandon(0x4000, 0x0EE0); // ljmp 0x4000, main
        return;
    }
    if ((dx & 0x060C) == 0x060C) {
        abandon(m.cs(), 0x018F); // jmp L_3F018F (near, same segment): hard reset
        return;
    }

    m.dx() = 0;
    m.ax() = m.rd16(m.ss(), kKeysRaw);
    orij_map_keys_core(m);
    m.wr8(m.ss(), kKeysMapped, uint8_t(m.dx()));

    m.dx() = 0;
    m.ax() = m.rd16(m.ss(), kKeysRepeatRaw);
    orij_map_keys_core(m);
    m.wr8(m.ss(), kKeysRepeatMapped, uint8_t(m.dx()));

    m.ax() = dx;
    m.dx() = entry_dx;
    m.ret();
}

// rom: 0x3F0FBE (near)
// See orij_map_keys_core. AX, BX untouched.
void orij_map_keys(Machine &m) {
    orij_map_keys_core(m);
    m.ret();
}

// rom: 0x3F0C85 (near)
// GDMA-copies the fixed 0x200-byte sprite-object staging buffer at SS:0x7000 to the hardware sprite table
// (port-mapped GDMA: src bank 0, src 0x7000, dst 0x0200, len 0x200, start/no-decrement). AX ends at 0x0280
// (the last two `mov ax, imm16` / `mov al, imm8` the original leaves behind); BX/CX/DX untouched.
void orij_dma_sprite_table(Machine &m) {
    m.ax() = 0;
    out16(m, 0x42, m.ax());
    m.ax() = kSpriteStaging;
    out16(m, 0x40, m.ax());
    m.ax() = 0x0200;
    out16(m, 0x44, m.ax());
    m.ax() = 0x0200;
    out16(m, 0x46, m.ax());
    m.set_al(0x80);
    m.out8(0x48, m.al());
    m.ret();
}

// rom: 0x3F0C9E (near)
// Same GDMA flush as orij_dma_sprite_table, but AX in selects the source address (bank forced to 0) instead
// of the fixed staging buffer. AX ends at 0x0280, same as orij_dma_sprite_table; BX/CX/DX untouched.
void orij_dma_sprite_table_from(Machine &m) {
    out16(m, 0x40, m.ax());
    m.ax() = 0;
    out16(m, 0x42, m.ax());
    m.ax() = 0x0200;
    out16(m, 0x44, m.ax());
    m.ax() = 0x0200;
    out16(m, 0x46, m.ax());
    m.set_al(0x80);
    m.out8(0x48, m.al());
    m.ret();
}

// rom: 0x3F0CD0 (near)
// See orij_sprite_quad_positions_core.
void orij_sprite_quad_positions(Machine &m) {
    orij_sprite_quad_positions_core(m);
    m.ret();
}

// rom: 0x3F0D44 (near)
// Refreshes the menu cursor's on-screen position from its saved tile row/column (kCursorRow/kCursorCol,
// each * 8 for pixels) by tail-jumping into orij_sprite_quad_positions_core with DI fixed at the cursor's
// object slot (kCursorObj); the tail jump means it's this call's own return address that gets popped, so
// from the caller's point of view this is an ordinary near call/return. DX ends at the pixel row/column
// pair just computed (not the entry DX -- the original overwrites DX outright before the tail jump, it
// never preserves its own caller's DX here). AX, BX untouched.
void orij_refresh_cursor_position(Machine &m) {
    uint8_t row = m.rd8(m.ss(), kCursorRow);
    uint8_t col = m.rd8(m.ss(), kCursorCol);
    m.di() = kCursorObj;
    uint8_t dl = uint8_t(col << 3);
    uint8_t dh = uint8_t(row << 3);
    m.dx() = uint16_t(dl | (uint16_t(dh) << 8));
    orij_sprite_quad_positions_core(m);
    m.ret();
}

// rom: 0x3F0D70 (near)
// See symbols/functions.toml: picks one of 128 per-call-site counters (SS:0xC300 + the low 7 bits of the
// sum of its own near return address's two bytes), increments it, and uses that counter as the index into
// a 256-byte SS:0xCB00 "random" table, feeding the resulting byte to orij_random_range_core as its SI
// source. AX in/out = orij_random_range's low/high bound and result; BX, DX, SI preserved.
void orij_random(Machine &m) {
    uint16_t entry_bx = m.bx();
    uint16_t entry_dx = m.dx();
    uint16_t entry_si = m.si();

    uint16_t ret_addr = m.rd16(m.ss(), m.sp()); // the near return address this call just pushed
    uint8_t sum = uint8_t(uint8_t(ret_addr) + uint8_t(ret_addr >> 8));
    uint16_t slot = uint16_t(kRandomSlots + (sum & 0x7F));
    uint8_t counter = uint8_t(m.rd8(m.ss(), slot) + 1);
    m.wr8(m.ss(), slot, counter);
    m.si() = uint16_t(kRandomTable + counter);

    orij_random_range_core(m);

    m.bx() = entry_bx;
    m.dx() = entry_dx;
    m.si() = entry_si;
    m.ret();
}

// rom: 0x3F0D9A (near)
// See orij_random_range_core.
void orij_random_range(Machine &m) {
    orij_random_range_core(m);
    m.ret();
}

// rom: 0x3F1120 (near)
// Reads the byte at DS:[the SS:0xF099 script/data cursor] into AL, advances the cursor past it. DS is set
// up by the caller (0x2000 or 0x3000 seen so far, selecting which data bank the cursor reads); BX, AH, DI
// untouched.
void orij_read_script_byte(Machine &m) {
    uint16_t si = m.rd16(m.ss(), kScriptCursor);
    m.set_al(m.rd8(m.ds(), si));
    m.wr16(m.ss(), kScriptCursor, uint16_t(si + 1));
    m.ret();
}

// rom: 0x3F23A3 (near)
// Byte-deinterleaving copy: CH times (0 means 256), reads SS:[BX], writes it to SS:[DI]++, then toggles
// BX's low bit (`xor bl, 1`, i.e. alternates BX between two adjacent SS addresses -- BX and BX^1 -- rather
// than walking a single pointer), so it interleaves two adjacent source bytes' worth of data into a
// contiguous run at DI. CL untouched, CH ends at 0; AL ends at the last byte read; BX ends toggled from
// entry by CH's parity (unchanged if CH's original value was even).
void orij_copy_alternating(Machine &m) {
    uint16_t bx = m.bx();
    uint16_t di = m.di();
    uint8_t ch = uint8_t(m.cx() >> 8);
    uint8_t al = m.al();
    do {
        al = m.rd8(m.ss(), bx);
        m.wr8(m.ss(), di, al);
        di = uint16_t(di + 1);
        bx = uint16_t(bx ^ 1);
    } while (--ch != 0);
    m.bx() = bx;
    m.di() = di;
    m.set_al(al);
    m.cx() = uint16_t(m.cx() & 0x00FF);
    m.ret();
}

} // namespace sawgod::game
