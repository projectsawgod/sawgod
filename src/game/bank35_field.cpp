#include "game/computed_handlers.h"
// Decompiled routines from bank 0x35 (field/map/event engine, segment 5000h). See asm/bank_35.s.
//
// Bank 0x35 is the field engine: game_main (0x350000, not decompiled) runs a loop that scans input, steps
// the player and up to 16 NPCs across an 80x80-ish tile map, and dispatches into a huge per-map handler
// table (L_3593B0 / L_35A2D4, both hundreds of KB of per-map callback code) for map-specific scripted
// events. This file covers the pieces of that loop that are tractable in isolation: reading a packed-map
// bitstream, looking up tile ids/flags from the decompressed map, and animating/drawing the player and NPC
// sprites. The frame-pump routines that tie them together (L_3505A6, L_35062F, L_350904, L_35098B, L_350AC3)
// all transitively call wait_vblank (which spins on an interrupt-set flag and can't be sandboxed, same as
// bank34's wait_vblank/orij_wait_vblank) or dispatch into the per-map handler tables, so they're not
// implemented here either.
#include "game/bank35_field.h"
#include <algorithm>

namespace sawgod::game {

namespace {
// RAM (all SS-relative unless noted; SS is preserved/untouched by every routine here, just like the
// bank34.cpp/bank3f_orijinaru.cpp routines it mirrors).
constexpr uint16_t kIram = 0x0000;   // internal RAM segment (used where the original explicitly reads `ds:`
                                     // with DS forced to 0, as opposed to the ambient SS the rest of the
                                     // NPC-table code implicitly addresses through BP)
constexpr uint16_t kMapSeg = 0x1000; // decompressed field-map working segment
constexpr uint16_t kMapAttrOffset = 0x2100; // second grid within kMapSeg: per-cell "tile attribute id" byte
constexpr uint16_t kTileFlagsWordTable =
    0xD53B; // 256 words, indexed by attribute id (map_tile_flags_word_at)
constexpr uint16_t kTileFlagsByteTable =
    0xD57B; // 256 bytes, indexed by attribute id (map_tile_flags_byte_at)

constexpr uint16_t kBitstreamBytesLeft = 0xD6F8; // word: bytes remaining in the packed bitstream
constexpr uint16_t kBitstreamBitTimer = 0xD6FA;  // byte: bits left in the buffered byte (0 => refill)
constexpr uint16_t kBitstreamByte = 0xD6FB;      // byte: current buffered byte, shifted out MSB-first

constexpr uint16_t kOamCount = 0xC80F;       // running count of OAM "sprite speed units" written this frame
constexpr uint16_t kBlinkEnabled = 0xC811;   // nonzero: run the player's two idle/blink toggles below
constexpr uint16_t kWalkToggle = 0xD500;     // player walk-cycle toggle, 0/1
constexpr uint16_t kSpriteSetIndex = 0xD520; // player sprite-set index into D_3940B0/D_393F70 (bank 0x39)
constexpr uint16_t kFacingByte = 0xD521; // player facing/animation byte: low 6 bits + 0x40 "use blink A" flag
constexpr uint16_t kOamBaseY = 0xD522;   // player OAM Y baseline
constexpr uint16_t kOamBaseX = 0xD524;   // player OAM X baseline
constexpr uint16_t kBlinkToggleA = 0xD526; // idle animation toggle A, 0/1
constexpr uint16_t kBlinkTimerA = 0xD527;  // countdown for toggle A (decremented by kBlinkPeriodA each call)
constexpr uint16_t kBlinkPeriodA = 0xD528; // per-call decrement for kBlinkTimerA
constexpr uint16_t kBlinkToggleB = 0xD5A3; // idle animation toggle B, 0/1
constexpr uint16_t kBlinkTimerB = 0xD5A4;  // countdown for toggle B (decremented by 1 each call)

constexpr uint16_t kNpcDirTileset =
    0xD5A6;                            // 8 bytes: facing (0-7) -> sprite-set index, mirrors kSpriteSetIndex
constexpr uint16_t kNpcTable = 0xD5AE; // 16 entries x 0x14 bytes (ends 0xD6EE)
constexpr int kNpcCount = 16;
constexpr uint16_t kNpcLastMatch = 0xD5A1;   // last hit found by npc_table_find_core, 0 if none
constexpr uint16_t kNpcPickCounter = 0xD5A5; // round-robins which NPC slot an idle AI trigger picks
constexpr uint16_t kCamX = 0xD59D;      // camera/scroll accumulator X (fixed point, matches NPC pos fields)
constexpr uint16_t kCamY = 0xD59F;      // camera/scroll accumulator Y
constexpr uint16_t kPlayerPos = 0xD0DD; // player_x/player_y packed (see symbols/ram.toml)
constexpr uint16_t kFrameCounter = 0xC80C; // random_seed: incremented every VBlank

// NPC record layout (0x14 bytes, all SS-relative off `bp`):
//   +0x00 byte  flags: 0xFF = slot unused; bit 0x80 = currently moving; low 6 bits (when moving) | 0xC0 =
//               tile-set id source for the sprite drawn this frame
//   +0x02 byte  grid_x  (becomes the destination cell as soon as a move starts)
//   +0x03 byte  grid_y
//   +0x04 byte  prev_grid_x (the cell being departed; still "occupied" for collision until the move lands)
//   +0x05 byte  prev_grid_y
//   +0x06 word  pos_x (11.5 fixed point pixel accumulator; grid_x = pos_x >> 5)
//   +0x08 word  pos_y
//   +0x0A word  vel_x (added to pos_x every frame while moving)
//   +0x0C word  vel_y
//   +0x0E byte  move_step counter (decremented once pos_x/pos_y both re-align to a cell boundary)
//   +0x11 byte  facing/behavior flags: low 3 bits = facing (0-7); 0x08 = hidden; 0x10 = no terrain-speed
//               check; 0x20 = flicker with frame-counter bit 3; 0x40 = "use blink A" like the player;
//               0x80 = AI-controlled (eligible for npc_ai_trigger_move)
//   +0x12 byte  idle timer (frames since the last AI move attempt; also reset when a move starts)

// D_35291D: 4 direction descriptors (8 bytes each), picked by npc_ai_trigger_move's `random_byte & 3`:
//   word tile_id_or, word vel_x, word vel_y, byte dx, byte dy
constexpr uint16_t kNpcDirTable = 0x291D;
constexpr uint16_t kNpcDirTableEntrySize = 8;

// out16(port, value): the original's `out port, ax` -- two byte writes, low byte then high byte (matches
// V30MZ::out16 in src/emu/v30mz.cpp). Duplicated locally like bank3f_orijinaru.cpp's copy: a real `lcall`
// to bank34's dma_sprite_table can't be reproduced here (there's no return address to unwind), and inlining
// its own tiny body is simpler than exposing a `_core` helper for what's a one-off GDMA kick.
void out16(Machine &m, uint8_t port, uint16_t value) {
    m.out8(port, uint8_t(value));
    m.out8(uint8_t(port + 1), uint8_t(value >> 8));
}

// Shared core of random_byte (rom 0x3401BD, bank 0x34): advances random_counter and indexes the fixed
// 256-byte table at bank-0x34 rom 0x340BD7. A local reimplementation rather than a shared helper: bank34's
// own `next_random_byte` reads the table via the CALLER's current CS, which is correct there because a real
// `lcall 0x4000, random_byte` has just set CS = 0x4000; called from bank 0x35 code CS is 0x5000, so reusing
// that helper as-is would read the wrong bank. Here the table's segment is hardcoded to 0x4000 instead,
// matching what the original's far call actually reaches.
uint8_t next_random_byte(Machine &m) {
    constexpr uint16_t kBank34Seg = 0x4000;
    constexpr uint16_t kRngCalls = 0xC80A;
    constexpr uint16_t kRandomTable = 0x0BD7; // D_340BD7
    uint16_t calls = uint16_t(m.rd16(m.ss(), kRngCalls) + 1);
    m.wr16(m.ss(), kRngCalls, calls);
    uint16_t index = uint16_t(calls + m.rd16(m.ss(), kFrameCounter)) & 0xFF;
    return m.rd8(kBank34Seg, uint16_t(kRandomTable + index));
}

// Shared core of the packed-map bitstream reader (rom 0x35180E, registered separately as
// bitstream_read_bit since it's also independently near-callable). Reads one bit MSB-first from DS:SI
// (caller-selected bank), refilling the byte buffer from *SI++ (and decrementing the
// kBitstreamBytesLeft counter) whenever the 8-bit timer runs out; once the stream is exhausted (counter
// already 0), returns 0 without touching SI or the byte buffer. Only AX (bit or 0), and on a refill
// SI/kBitstreamByte/kBitstreamBytesLeft, change.
uint8_t bitstream_read_bit_core(Machine &m) {
    uint8_t timer = uint8_t(m.rd8(m.ss(), kBitstreamBitTimer) - 1);
    m.wr8(m.ss(), kBitstreamBitTimer, timer);
    if (timer == 0) {
        uint16_t left = m.rd16(m.ss(), kBitstreamBytesLeft);
        if (left == 0)
            return 0;
        m.wr16(m.ss(), kBitstreamBytesLeft, uint16_t(left - 1));
        m.wr8(m.ss(), kBitstreamBitTimer, 8);
        uint8_t b = m.rd8(m.ds(), m.si());
        m.wr8(m.ss(), kBitstreamByte, b);
        m.si() = uint16_t(m.si() + 1);
    }
    uint8_t cur = m.rd8(m.ss(), kBitstreamByte);
    uint8_t bit = uint8_t(cur >> 7);
    m.wr8(m.ss(), kBitstreamByte, uint8_t(cur << 1));
    return bit;
}

// Shared core of the NPC-table lookup (rom 0x35293D, near, not independently registered: every caller in
// this file needs its result to keep going, the same reasoning as draw_tilemap_offset_core). Scans the
// 16-entry NPC table at kNpcTable for an active slot (byte 0 != 0xFF) whose current cell (bytes 2/3) or
// departure cell (bytes 4/5) equals (col, row); records the match's address at kNpcLastMatch (0 if none)
// and returns whether one was found. BP/CX are scratch in the original (pushed/popped) so nothing else
// changes.
bool npc_table_find_core(Machine &m, uint8_t col, uint8_t row) {
    uint16_t bp = kNpcTable;
    for (int i = 0; i < kNpcCount; ++i, bp = uint16_t(bp + 0x14)) {
        if (m.rd8(m.ss(), bp) == 0xFF)
            continue;
        uint8_t c1 = m.rd8(m.ss(), uint16_t(bp + 2)), r1 = m.rd8(m.ss(), uint16_t(bp + 3));
        uint8_t c2 = m.rd8(m.ss(), uint16_t(bp + 4)), r2 = m.rd8(m.ss(), uint16_t(bp + 5));
        if ((col == c1 && row == r1) || (col == c2 && row == r2)) {
            m.wr16(m.ss(), kNpcLastMatch, bp);
            return true;
        }
    }
    m.wr16(m.ss(), kNpcLastMatch, 0);
    return false;
}

// Shared bounds+attribute-id lookup behind map_tile_flags_word_at/map_tile_flags_byte_at (rom 0x351A41 /
// 0x351A71): both routines do the identical bounds check and attribute-id fetch and differ only in which
// RAM table they then index and the out-of-bounds default, so callers that need the result without a real
// far call (draw_player_sprite, npc_ai_trigger_move) go through here instead.
bool map_attr_id_at(Machine &m, uint8_t col, uint8_t row, uint8_t &attr_id) {
    if (!(col < 0x40 && row != 0 && row < 0x3F))
        return false;
    uint16_t off = uint16_t(uint16_t(row) * 0x40 + col + kMapAttrOffset);
    attr_id = m.rd8(kMapSeg, off);
    return true;
}
uint16_t map_tile_flags_word_core(Machine &m, uint8_t col, uint8_t row) {
    uint8_t attr;
    if (!map_attr_id_at(m, col, row, attr))
        return 0x000F;
    return m.rd16(m.ss(), uint16_t(kTileFlagsWordTable + attr * 2));
}
uint8_t map_tile_flags_byte_core(Machine &m, uint8_t col, uint8_t row) {
    uint8_t attr;
    if (!map_attr_id_at(m, col, row, attr))
        return 0;
    return m.rd8(m.ss(), uint16_t(kTileFlagsByteTable + attr));
}
} // namespace

// rom: 0x35180E (near)
// See bitstream_read_bit_core.
void bitstream_read_bit(Machine &m) {
    m.ax() = uint16_t(bitstream_read_bit_core(m) * 0x0101); // `shl ax,1; mov al,ah`: the bit in AH and AL
    m.ret();
}

// rom: 0x351801 (near)
// Reads CX bits (0 means 65536, the usual dec/jne idiom) MSB-first via bitstream_read_bit_core, packing
// them into DX starting from bit 0 -- a ROM quirk, reproduced exactly: `shl dx,1` shifts the full 16-bit DX
// but the newly read bit is only ORed into DL, so once more than 8 bits have been read DH keeps shifting
// left on its own with nothing new ORed into it. AX ends with the last bit read in both AH and AL; CX ends 0.
void bitstream_read_bits(Machine &m) {
    uint16_t dx = 0;
    uint16_t cx = m.cx();
    uint8_t last_bit = 0;
    do {
        dx = uint16_t(dx << 1);
        last_bit = bitstream_read_bit_core(m);
        dx = uint16_t((dx & 0xFF00) | uint8_t(uint8_t(dx) | last_bit));
        --cx;
    } while (cx != 0);
    m.dx() = dx;
    m.cx() = cx;
    m.ax() = uint16_t(last_bit * 0x0101); // the last bit, in AH and AL
    m.ret();
}

// rom: 0x351A18
// BX in: BL = column, BH = row (map-local, i.e. already relative to the current map's origin). Adds an
// 8-cell border offset to both and, if the result stays within an 0x50 x 0x50 grid, returns AL = the byte
// at kMapSeg:[row*0x50 + col] (the decompressed map's raw per-cell id grid); otherwise AL = 0 (out of
// bounds/border). AH = 0. BX, DS preserved (pushed/popped around the whole body).
void map_cell_id_at(Machine &m) {
    uint8_t col = uint8_t(uint8_t(m.bx()) + 8);
    uint8_t row = uint8_t(uint8_t(m.bx() >> 8) + 8);
    uint16_t result = 0;
    if (col < 0x50 && row < 0x50)
        result = m.rd8(kMapSeg, uint16_t(uint16_t(row) * 0x50 + col));
    m.ax() = result;
    m.retf();
}

// rom: 0x351A41
// BX in: BL = column, BH = row, no border offset this time. If BL < 0x40 and 0 < BH < 0x3F, looks up the
// per-cell attribute id at kMapSeg:[row*0x40 + col + kMapAttrOffset], then AX = the word at
// kTileFlagsWordTable[attribute id]; otherwise AX = 0x000F (the out-of-bounds default). BX, DS preserved.
void map_tile_flags_word_at_core(Machine &m) {
    m.ax() = map_tile_flags_word_core(m, uint8_t(m.bx()), uint8_t(m.bx() >> 8));
    return;
}

void map_tile_flags_word_at(Machine &m) {
    map_tile_flags_word_at_core(m);
    m.retf();
}

// rom: 0x351A71
// Same bounds/attribute-id lookup as map_tile_flags_word_at, but AL = the byte at
// kTileFlagsByteTable[attribute id] (AH = 0), or AX = 0 out of bounds. This is the one callers test with
// `test al, 0x60`/`test al, 0x20` for terrain-based walk speed and blocking. BX, DS preserved.
void map_tile_flags_byte_at(Machine &m) {
    m.ax() = map_tile_flags_byte_core(m, uint8_t(m.bx()), uint8_t(m.bx() >> 8));
    m.retf();
}

// rom: 0x350163
// Per-frame player-sprite housekeeping. Flips the sprite table (inlined dma_sprite_table body) and reports
// this frame's running OAM/walk-speed tally (kOamCount) to ports 5/6, then resets it to 0. Advances the
// walk-cycle toggle (kWalkToggle) every call. If idle/blink animation is enabled (kBlinkEnabled != 0):
// counts kBlinkTimerA down by kBlinkPeriodA each call, and once it underflows, flips kBlinkToggleA and
// reloads kBlinkTimerA = 0x20; separately counts kBlinkTimerB down by 1 each call, and once it hits 0,
// flips kBlinkToggleB and reloads kBlinkTimerB = 0x20. AH ends 0x02 (leftover from the inlined
// dma_sprite_table call's own AX = 0x0280, never touched again); AL ends whatever the last 8-bit `mov al`
// on the path taken left (0 if blink animation is disabled, else the blink-timer B path's result -- see the
// body). BX/CX/DX/SI/DI/segments untouched; only these RAM locations, ports 5/6 and the GDMA ports
// dma_sprite_table itself writes change.
void advance_player_animation_timers_core(Machine &m) {
    // Inlined dma_sprite_table body (bank34.cpp, rom 0x34075B): the original reaches it via a real far
    // call, so AX ends 0x0280 exactly as dma_sprite_table itself leaves it; nothing after this touches AH,
    // so it stays 0x02 for the rest of the routine below (every later instruction here is an 8-bit `mov al`
    // / `and al` / `inc al`).
    constexpr uint16_t kSpriteBuffer = 0xC000;
    out16(m, 0x40, kSpriteBuffer);
    out16(m, 0x42, 0x0000);
    out16(m, 0x44, 0x0200);
    out16(m, 0x46, 0x0200);
    m.out8(0x48, 0x80);
    m.wsc().publish_terrain_cards(); // presentation provenance follows this inlined OAM upload
    uint8_t ah = 0x02;
    uint8_t al = 0x80;

    al = 0;
    m.out8(5, al);
    al = m.rd8(m.ss(), kOamCount);
    m.out8(6, al);
    al = 0;
    m.wr8(m.ss(), kOamCount, al);

    al = m.rd8(m.ss(), kWalkToggle);
    al = uint8_t((al + 1) & 1);
    m.wr8(m.ss(), kWalkToggle, al);

    al = m.rd8(m.ss(), kBlinkEnabled);
    if (al != 0) {
        uint8_t timerA = m.rd8(m.ss(), kBlinkTimerA);
        uint8_t periodA = m.rd8(m.ss(), kBlinkPeriodA);
        al = uint8_t(timerA - periodA);
        if (timerA < periodA) {
            al = m.rd8(m.ss(), kBlinkToggleA);
            al = uint8_t((al + 1) & 1);
            m.wr8(m.ss(), kBlinkToggleA, al);
            al = 0x20;
        }
        m.wr8(m.ss(), kBlinkTimerA, al);

        uint8_t timerB = m.rd8(m.ss(), kBlinkTimerB);
        al = uint8_t(timerB - 1);
        if (al == 0) {
            al = m.rd8(m.ss(), kBlinkToggleB);
            al = uint8_t((al + 1) & 1);
            m.wr8(m.ss(), kBlinkToggleB, al);
            al = 0x20;
        }
        m.wr8(m.ss(), kBlinkTimerB, al);
    }

    m.ax() = uint16_t(al | (uint16_t(ah) << 8));
    return;
}

void advance_player_animation_timers(Machine &m) {
    advance_player_animation_timers_core(m);
    m.retf();
}

// rom: 0x3501C4
// Refreshes the player's on-screen sprite: copies the current walk-frame's 4bpp graphics (a fixed
// 2-tile-slot destination selected by kWalkToggle, source picked by kSpriteSetIndex/kFacingByte/
// kBlinkToggleA from the bank-0x39 tables at D_3940B0/D_393F70) into tile RAM, then writes a 4-quadrant
// 16x16 OAM entry for it at kOamCount's slot (tile ids kWalkToggle*4+0x10 .. +3, positions from
// kOamBaseY/kOamBaseX +/-8), and finally adds 2 or 4 to kOamCount depending on the terrain flags
// (map_tile_flags_byte_at) at the player's tile: +4 normally, +2 with only bit 0x40 set, and on tiles
// with bit 0x20 set an every-other-frame (by kFrameCounter parity) +4/+2 alternation. Far routine;
// preserves everything else (pushaw + ds/es/ss bracket the whole body).
void draw_player_sprite_core(Machine &m) {
    uint16_t di = uint16_t((uint16_t(m.rd8(m.ss(), kWalkToggle)) * 4 + 0x10) << 5);

    uint8_t facing = m.rd8(m.ss(), kFacingByte);
    uint8_t set_sel = uint8_t(facing & 0x3F);
    if (facing & 0x40)
        set_sel = uint8_t(set_sel + m.rd8(m.ss(), kBlinkToggleA));
    uint16_t si = uint16_t(uint16_t(set_sel) << 7);

    uint8_t table_idx = uint8_t(m.rd8(m.ss(), kSpriteSetIndex) * 4);
    uint16_t src_off = m.rd16(0x9000, uint16_t(0x40B0 + table_idx)); // D_3940B0
    uint16_t src_seg = m.rd16(0x9000, uint16_t(0x3F70 + table_idx)); // D_393F70
    si = uint16_t(si + src_off);

    for (int i = 0; i < 0x40; ++i) {
        m.wr16(0x0400, uint16_t(di + i * 2), m.rd16(src_seg, uint16_t(si + i * 2)));
    }

    uint16_t oam_di = uint16_t((uint16_t(m.rd8(m.ss(), kOamCount)) << 2) + 0xC000);
    uint8_t tile_base = uint8_t(uint8_t(m.rd8(m.ss(), kWalkToggle) * 4) + 0x10);
    uint16_t tile = tile_base;
    m.wr16(m.ss(), oam_di, tile);
    tile = uint16_t(tile + 1); // `inc ax`: 16-bit, the attribute bits carry over
    m.wr16(m.ss(), uint16_t(oam_di + 4), tile);
    tile = uint16_t(tile + 1); // `inc ax`: 16-bit, the attribute bits carry over
    m.wr16(m.ss(), uint16_t(oam_di + 8), tile);
    tile = uint16_t(tile + 1); // `inc ax`: 16-bit, the attribute bits carry over
    m.wr16(m.ss(), uint16_t(oam_di + 0xC), tile);

    uint8_t y = m.rd8(m.ss(), kOamBaseY);
    uint8_t x = m.rd8(m.ss(), kOamBaseX);
    auto pos = [](uint8_t yy, uint8_t xx) { return uint16_t(xx | (uint16_t(yy) << 8)); };
    m.wr16(m.ss(), uint16_t(oam_di + 2), pos(y, x));
    y = uint8_t(y + 8);
    m.wr16(m.ss(), uint16_t(oam_di + 6), pos(y, x));
    x = uint8_t(x + 8);
    m.wr16(m.ss(), uint16_t(oam_di + 0xE), pos(y, x));
    y = uint8_t(y - 8);
    m.wr16(m.ss(), uint16_t(oam_di + 0xA), pos(y, x));
    if (Machine::player_sprite_hook) [[unlikely]]
        Machine::player_sprite_hook(m, oam_di);

    uint16_t pos_word = m.rd16(m.ss(), kPlayerPos);
    uint8_t col = uint8_t(pos_word);
    uint8_t row = uint8_t(pos_word >> 8);
    uint8_t flags = map_tile_flags_byte_core(m, col, row);
    m.wsc().terrain_cards[0] = {int((oam_di - 0xC000) / 4), tile_base, (flags & 0x20) != 0};
    // Presentation only (game/terrain_veil.h); native OAM/count writes below stay untouched.

    uint8_t speed;
    if ((flags & 0x60) == 0) {
        speed = 4;
    } else if ((flags & 0x20) == 0) {
        speed = 2;
    } else {
        speed = (m.rd16(m.ss(), kFrameCounter) & 1) == 0 ? 4 : 2;
    }
    m.wr8(m.ss(), kOamCount, uint8_t(m.rd8(m.ss(), kOamCount) + speed));

    return;
}

void draw_player_sprite(Machine &m) {
    draw_player_sprite_core(m);
    m.retf();
}

// rom: 0x35285A
// Idle-NPC AI trigger, called once per frame (from L_3505A6, not decompiled). Scans the 16 NPC slots
// starting from kNpcTable looking for one whose idle timer (+0x12) is >= 0x1E; if none is, instead rolls a
// random_byte and, ~87.5% of the time (< 0x1F out of 256 -- a ROM quirk read backward from the obvious
// "usually pick a slot" intent, reproduced exactly), picks slot (kNpcPickCounter & 0xF) instead (the other
// ~12.5% of the time the call does nothing). For the chosen slot: resets its idle timer to 0; bails if the
// slot is unused (byte 0 == 0xFF), already moving (bit 0x80 set) or not AI-controlled (bit 0x80 of +0x11
// clear); otherwise picks one of 4 directions at random (D_35291D, indexed by random_byte & 3), and if the
// resulting cell stays on the map, isn't the player's cell, isn't occupied by another NPC
// (npc_table_find_core) and isn't blocked by terrain (map_tile_flags_word_at bits 0xC0, or bit 0x30 without
// clear passage when bit 0x30 is itself clear), starts the move: grid_x/grid_y become the destination cell,
// +0 = (direction's low tile-id byte) | 0xC0, +0xE = 1 (move-step counter) and +0xA/+0xC = the direction's
// velocity words. Far routine; preserves everything besides the NPC table and kNpcPickCounter/kNpcLastMatch.
void npc_ai_trigger_move_core(Machine &m) {
    uint16_t bp = kNpcTable;
    bool found = false;
    for (int i = 0; i < kNpcCount; ++i, bp = uint16_t(bp + 0x14)) {
        if (m.rd8(m.ss(), uint16_t(bp + 0x12)) >= 0x1E) {
            found = true;
            break;
        }
    }
    if (!found) {
        uint8_t pick_counter = uint8_t(m.rd8(m.ss(), kNpcPickCounter) + 1);
        m.wr8(m.ss(), kNpcPickCounter, pick_counter);
        uint8_t roll = next_random_byte(m);
        if (roll >= 0x1F) {
            return;
        }
        bp = uint16_t(uint16_t(pick_counter & 0xF) * 0x14 + kNpcTable);
    }

    m.wr8(m.ss(), uint16_t(bp + 0x12), 0);
    uint8_t flags0 = m.rd8(m.ss(), bp);
    if (flags0 == 0xFF) {
        return;
    }
    if (flags0 & 0x80) {
        return;
    }
    uint8_t flags11 = m.rd8(m.ss(), uint16_t(bp + 0x11));
    if (!(flags11 & 0x80)) {
        return;
    }

    uint8_t col = m.rd8(m.ss(), uint16_t(bp + 2));
    uint8_t row = m.rd8(m.ss(), uint16_t(bp + 3));
    uint8_t dir = uint8_t(next_random_byte(m) & 3);
    uint16_t si = uint16_t(kNpcDirTable + dir * kNpcDirTableEntrySize);

    col = uint8_t(col + m.rd8(m.cs(), uint16_t(si + 6)));
    if (col >= 0x40) {
        return;
    }
    row = uint8_t(row + m.rd8(m.cs(), uint16_t(si + 7)));
    if (row >= 0x40) {
        return;
    }
    uint16_t candidate = uint16_t(col | (uint16_t(row) << 8));
    if (candidate == m.rd16(m.ss(), kPlayerPos)) {
        return;
    }
    if (npc_table_find_core(m, col, row)) {
        return;
    }

    uint16_t flags_word = map_tile_flags_word_core(m, col, row);
    uint8_t fw_hi = uint8_t(flags_word >> 8);
    uint8_t fw_lo = uint8_t(flags_word);
    if (fw_hi & 0xC0) {
        return;
    }
    if (!(fw_hi & 0x30) && (fw_lo & 1)) {
        return;
    }

    m.wr8(m.ss(), uint16_t(bp + 2), col);
    m.wr8(m.ss(), uint16_t(bp + 3), row);
    uint16_t tile_word = m.rd16(m.cs(), si);
    m.wr8(m.ss(), bp, uint8_t(uint8_t(tile_word) | 0xC0));
    m.wr8(m.ss(), uint16_t(bp + 0xE), 1);
    m.wr16(m.ss(), uint16_t(bp + 0xA), m.rd16(m.cs(), uint16_t(si + 2)));
    m.wr16(m.ss(), uint16_t(bp + 0xC), m.rd16(m.cs(), uint16_t(si + 4)));
    return;
}

void npc_ai_trigger_move(Machine &m) {
    npc_ai_trigger_move_core(m);
    m.retf();
}

// rom: 0x35297E
// Per-frame NPC step: advances position/animation and redraws all 16 NPC slots (kNpcTable), called once
// per frame (from L_3505A6, not decompiled) right after the player. For each active slot (byte 0 != 0xFF):
// if it's moving (bit 0x80 of byte 0), adds velocity (+0xA/+0xC) to the fixed-point position (+6/+8); once
// both coordinates re-align to a cell boundary (low 5 bits 0) it counts down the move-step counter (+0xE),
// and when that reaches 0 snaps grid_x/grid_y and prev_grid_x/prev_grid_y (+2..+5) to the new cell, zeroes
// velocity and the idle timer (+0x12), and clears the "moving" bit. If the slot is hidden (+0x11 bit 0x08)
// or off-screen (the pixel position, minus the camera accumulators kCamX/kCamY, outside the visible window)
// it's skipped from here on. Otherwise: writes a 4-quadrant 16x16 OAM entry at kOamCount's slot (tile ids
// from a per-active-NPC tile bank that starts at 0x18 and advances by 8 for every NPC actually drawn, plus
// kWalkToggle*4, plus the low 3 bits of +0x11 shifted into the attribute bits), GDMA-copies that NPC's
// current-facing graphics tile (bank 0x39 tables D_3940B0/D_393F72, selected by kNpcDirTileset[facing] and
// the tile-id's own top bits when bit 0x40 of byte 0 is set, +kBlinkToggleB) into tile RAM, then adds 2 or 4
// to kOamCount based on this NPC's terrain, as draw_player_sprite does. Bit 0x10 of +0x11 skips the terrain
// check and adds 4 unconditionally. Bit 0x20 skips the count update when kFrameCounter bit 3 is set,
// letting the next NPC overwrite this one's OAM (a flicker effect). Far routine; preserves everything
// besides the NPC table, kOamCount, tile RAM, OAM, and the GDMA ports.
void advance_and_draw_npcs_core(Machine &m) {
    // Clear skipped/hidden NPCs too; no record may survive a slot being removed or overwritten.
    std::fill(m.wsc().terrain_cards.begin() + 1, m.wsc().terrain_cards.end(), emu::Wsc::TerrainCard{});
    uint16_t bp = kNpcTable;
    uint8_t tile_bank = 0x18; // ROM: DL, constant 0x18 at entry, +8 per NPC actually drawn below
    for (int slot = 0; slot < kNpcCount; ++slot, bp = uint16_t(bp + 0x14)) {
        uint8_t flags0 = m.rd8(m.ss(), bp);
        if (flags0 == 0xFF)
            continue;

        if (flags0 & 0x80) {
            uint16_t pos_x = uint16_t(m.rd16(m.ss(), uint16_t(bp + 6)) + m.rd16(m.ss(), uint16_t(bp + 0xA)));
            m.wr16(m.ss(), uint16_t(bp + 6), pos_x);
            uint16_t pos_y = uint16_t(m.rd16(m.ss(), uint16_t(bp + 8)) + m.rd16(m.ss(), uint16_t(bp + 0xC)));
            m.wr16(m.ss(), uint16_t(bp + 8), pos_y);
            if ((pos_x & 0x1F) == 0 && (pos_y & 0x1F) == 0) {
                uint8_t step = uint8_t(m.rd8(m.ss(), uint16_t(bp + 0xE)) - 1);
                m.wr8(m.ss(), uint16_t(bp + 0xE), step);
                if (step == 0) {
                    uint8_t gx = uint8_t(pos_x >> 5);
                    uint8_t gy = uint8_t(pos_y >> 5);
                    m.wr8(m.ss(), uint16_t(bp + 2), gx);
                    m.wr8(m.ss(), uint16_t(bp + 4), gx);
                    m.wr8(m.ss(), uint16_t(bp + 3), gy);
                    m.wr8(m.ss(), uint16_t(bp + 5), gy);
                    m.wr16(m.ss(), uint16_t(bp + 0xA), 0);
                    m.wr16(m.ss(), uint16_t(bp + 0xC), 0);
                    m.wr8(m.ss(), uint16_t(bp + 0x12), 0);
                    flags0 = uint8_t(flags0 & 0x7F);
                    m.wr8(m.ss(), bp, flags0);
                }
            }
        }

        uint8_t flags11 = m.rd8(m.ss(), uint16_t(bp + 0x11));
        if (flags11 & 0x08)
            continue; // hidden

        uint16_t sx = uint16_t(m.rd16(m.ss(), uint16_t(bp + 6)) >> 1);
        sx = uint16_t(uint16_t(sx - m.rd16(m.ss(), kCamX)) + 0x10);
        if (sx >= 0x100)
            continue; // off-screen (unsigned 16-bit compare, matches `cmp ax, 0x100; jb`)
        uint8_t screen_x_px = uint8_t(sx - 0x10);

        // The original reads this one through DS (forced to 0/kIram at routine entry) rather than the
        // ambient SS every other +8 access in this routine uses -- reproduced exactly even though the two
        // usually coincide.
        uint16_t sy = uint16_t(m.rd16(kIram, uint16_t(bp + 8)) >> 1);
        sy = uint16_t(uint16_t(sy - m.rd16(kIram, kCamY)) + 0x10);
        if (sy >= 0xB0)
            continue;
        uint8_t screen_y_px = uint8_t(sy - 0x10);

        uint16_t oam_di = uint16_t((uint16_t(m.rd8(m.ss(), kOamCount)) << 2) + 0xC000);
        uint16_t tile_id = uint16_t(uint8_t(uint8_t(m.rd8(m.ss(), kWalkToggle) * 4) + tile_bank));
        uint16_t attr = uint16_t(uint16_t(flags11 & 7) << 9);
        uint16_t tile = uint16_t(attr | tile_id);
        m.wr16(m.ss(), oam_di, tile);
        tile = uint16_t(tile + 1); // `inc ax`: 16-bit, the attribute bits carry over
        m.wr16(m.ss(), uint16_t(oam_di + 4), tile);
        tile = uint16_t(tile + 1); // `inc ax`: 16-bit, the attribute bits carry over
        m.wr16(m.ss(), uint16_t(oam_di + 8), tile);
        tile = uint16_t(tile + 1); // `inc ax`: 16-bit, the attribute bits carry over
        m.wr16(m.ss(), uint16_t(oam_di + 0xC), tile);

        uint8_t x = screen_x_px, y = screen_y_px;
        m.wr8(m.ss(), uint16_t(oam_di + 3), x);
        m.wr8(m.ss(), uint16_t(oam_di + 0xB), x);
        x = uint8_t(x + 8);
        m.wr8(m.ss(), uint16_t(oam_di + 7), x);
        m.wr8(m.ss(), uint16_t(oam_di + 0xF), x);
        m.wr8(m.ss(), uint16_t(oam_di + 2), y);
        m.wr8(m.ss(), uint16_t(oam_di + 6), y);
        y = uint8_t(y + 8);
        m.wr8(m.ss(), uint16_t(oam_di + 0xA), y);
        m.wr8(m.ss(), uint16_t(oam_di + 0xE), y);

        out16(m, 0x44, uint16_t((tile_id << 5) + 0x4000));

        uint8_t tile0 = flags0;
        uint8_t dl = tile0;
        if (tile0 & 0x40)
            dl = uint8_t(dl + m.rd8(m.ss(), kBlinkToggleB));
        uint16_t gdma_src_off = uint16_t((uint16_t(dl & 0x3F)) << 7);

        uint8_t dir_set = m.rd8(m.ss(), uint16_t(kNpcDirTileset + (flags11 & 7)));
        uint16_t table_idx = uint16_t(uint16_t(dir_set) * 4);
        uint16_t src_off = m.rd16(0x9000, uint16_t(0x40B0 + table_idx)); // D_3940B0
        out16(m, 0x40, uint16_t(src_off + gdma_src_off));
        uint16_t bank_sel = m.rd16(0x9000, uint16_t(0x3F72 + table_idx)); // D_393F72
        out16(m, 0x42, uint16_t(bank_sel & 0xF));
        out16(m, 0x46, 0x0080);
        m.out8(0x48, 0x80);
        if (Machine::npc_sprite_hook) [[unlikely]]
            Machine::npc_sprite_hook(m, bp, oam_di, uint8_t(dl & 0x3F));

        tile_bank = uint8_t(tile_bank + 8);

        if (flags11 & 0x20) {
            if ((m.rd16(m.ss(), kFrameCounter) & 8) != 0)
                continue;
        }

        bool veiled = false; // presentation only; 0x10 and the 0x20 NPC blink retain their native behavior
        uint8_t speed = 4;   // flag 0x10 skips the terrain check, but still reserves all four OAM entries
        if ((flags11 & 0x10) == 0) {
            uint8_t gx = uint8_t(m.rd16(m.ss(), uint16_t(bp + 6)) >> 5);
            uint8_t gy = uint8_t(m.rd16(m.ss(), uint16_t(bp + 8)) >> 5);
            uint8_t terrain = map_tile_flags_byte_core(m, gx, gy);
            veiled = (terrain & 0x20) != 0;
            if ((terrain & 0x60) != 0) {
                if ((terrain & 0x20) == 0)
                    speed = 2;
                else
                    speed = (m.rd16(m.ss(), kFrameCounter) & 1) == 0 ? 4 : 2;
            }
        }
        m.wsc().terrain_cards[size_t(slot + 1)] = {int((oam_di - 0xC000) / 4), uint16_t(attr | tile_id),
                                                   veiled};
        m.wr8(m.ss(), kOamCount, uint8_t(m.rd8(m.ss(), kOamCount) + speed));
    }
    return;
}

void advance_and_draw_npcs(Machine &m) {
    advance_and_draw_npcs_core(m);
    m.retf();
}

} // namespace sawgod::game
