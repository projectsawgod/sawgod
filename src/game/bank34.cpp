#include "game/computed_flags.h"
#include "game/computed_handlers.h"
#include "game/session.h"
#include "game/tables.h"
#include "game/text.h"
#include "game/text_render.h"
// Decompiled routines from bank 0x34 (main engine, segment 4000h). See asm/bank_34.s.
#include "game/bank34.h"
#include "game/bank3d_sound.h"

namespace sawgod::game {

namespace {
constexpr uint16_t kHardwareSprites = 0x0200; // hardware sprite/OAM table
constexpr uint16_t kSpriteBuffer = 0xC000; // sprite staging; also the base of the masked C000-FFFF RAM window
constexpr uint16_t kPaletteBuffer = 0xC400;      // palette_buffer
constexpr uint16_t kPaletteTarget = 0xC600;      // palette_target_buffer
constexpr uint16_t kKeysHeld = 0xC800;           // keys_held
constexpr uint16_t kKeysPrev = 0xC802;           // keys_prev
constexpr uint16_t kKeysPressed = 0xC804;        // keys_pressed
constexpr uint16_t kKeysRepeat = 0xC806;         // keys_repeat
constexpr uint16_t kKeyRepeatTimer = 0xC808;     // key_repeat_timer
constexpr uint16_t kRngCalls = 0xC80A;           // random_counter
constexpr uint16_t kFrames = 0xC80C;             // random_seed
constexpr uint16_t kVblankDone = 0xC80E;         // vblank_done
constexpr uint16_t kMusicRequest = 0xC820;       // music_request
constexpr uint16_t kSfxRequest = 0xC821;         // sfx_request
constexpr uint16_t kMusicRequestPrev = 0xC822;   // music_current
constexpr uint16_t kGameState = 0xD000;          // game_state
constexpr uint16_t kMember1Record = 0xD01F;      // member1_record
constexpr uint16_t kMember2Record = 0xD03E;      // member2_record
constexpr uint16_t kMember3Record = 0xD05D;      // member3_record
constexpr uint16_t kInventory = 0xD07C;          // inventory_slots
constexpr uint16_t kKeyItems = 0xD08C;           // key_items
constexpr uint16_t kVehicleLocations = 0xD0AF;   // vehicle_locations
constexpr uint16_t kPreviousMap = 0xD0D7;        // previous_map
constexpr uint16_t kPreviousPlayerX = 0xD0D8;    // previous_player_x
constexpr uint16_t kPreviousPlayerY = 0xD0D9;    // previous_player_y
constexpr uint16_t kTowerCounter = 0xD0DA;       // tower_counter
constexpr uint16_t kCurrentFloor = 0xD0DB;       // current_floor
constexpr uint16_t kMapId = 0xD0DC;              // current_map
constexpr uint16_t kPlayerCell = 0xD0DD;         // player_x
constexpr uint16_t kPlayerY = 0xD0DE;            // player_y
constexpr uint16_t kDialogueSpeed = 0xD0E6;      // dialogue_speed
constexpr uint16_t kOptions = 0xD0E7;            // field_options
constexpr uint16_t kPartyOrder = 0xD0E8;         // dialogue_party_order
constexpr uint16_t kPartyOrderLast = 0xD0EB;     // dialogue_party_order_last
constexpr uint16_t kEventFlags = 0xD0EC;         // event_flags
constexpr uint16_t kMemberQueues = 0xD105;       // member_queues
constexpr uint16_t kSecondMemberQueues = 0xD1D1; // second_member_queues
constexpr uint16_t kThirdMemberQueues = 0xD29D;  // third_member_queues
constexpr uint16_t kFourthMemberQueues = 0xD369; // fourth_member_queues
constexpr uint16_t kHardwarePalettes = 0xFE00;
constexpr uint16_t kIram = 0x0000;    // internal RAM segment
constexpr uint16_t kTileSeg = 0x0400; // 4000h: 4bpp tile data

constexpr uint16_t kRandomTable = 0x0BD7; // D_340BD7: 256 random bytes (in segment 4000h)
constexpr uint16_t kScreenMap = 0x1000;   // screen tile map base, within the SS-relative IRAM window

void clear_direction(Machine &m) { m.flags() = uint16_t(m.flags() & ~emu::DF); } // cld

// Shared core of random_byte: advances random_counter and indexes the fixed table. Returns the byte;
// callers that reproduce an inlined `call random_byte` (e.g. random_range) use this instead of a real
// far call, since there's no return address on the stack to unwind.
uint8_t next_random_byte(Machine &m) {
    // The counter increments before indexing; frame count perturbs the low-byte table index.
    uint16_t calls = uint16_t(m.rd16(m.ss(), kRngCalls) + 1);
    m.wr16(m.ss(), kRngCalls, calls);
    uint16_t random_index = uint16_t(calls + m.rd16(m.ss(), kFrames)) & 0xFF;
    // RNG table belongs to bank 34, including core calls.
    return m.rd8(0x4000, uint16_t(kRandomTable + random_index));
}

// out16(port, value): the original's `out port, ax` -- two byte writes, low byte to `port` then high byte to
// `port + 1` (matches V30MZ::out16 in src/emu/v30mz.cpp).
void out16(Machine &m, uint8_t port, uint16_t value) {
    m.out8(port, uint8_t(value));
    m.out8(uint8_t(port + 1), uint8_t(value >> 8));
}
} // namespace

// Shared body of ROM 0x340000
// Waits CX frames through the frame scheduler (4000:0009); CX = 0 waits 65536 frames. Leaves CX = 0.
void wait_frames_core(Machine &m) {
    do
        m.call_far(0x4000, 0x0009); // wait_vblank
    while (--m.cx() != 0);
    return;
}

// rom: 0x340000
void wait_frames(Machine &m) {
    wait_frames_core(m);
    m.retf();
}

// rom: 0x340009 (far)
// The port's one hardware frame point that waits on a flag: clears vblank_done (0xC80E), forwards a
// pending music/sfx request to the sound driver (sound_request, bank 0x3D) exactly as the original does --
// music_request (0xC820) only when it differs from music_request_prev (0xC822); sfx_request (0xC821)
// whenever nonzero, cleared after -- then waits for vblank_done to become nonzero again (set by
// irq_vblank). The original brackets the whole body in pushaw/push ds,es,ss .. popaw/pop ds,es,ss, so every
// register the sound-request calls might clobber comes back exactly as the caller passed it; SS is not
// expected to change, so (unlike the others) it is read fresh rather than saved/restored.
// Parks instead of spinning (Machine::halt_and_wait): Wsc::tick keeps the frame/line clock (and sound)
// running while halted, and the VBlank IRQ (already a native routine, irq_vblank) wakes the CPU and sets
// the flag; a pending interrupt can still be taken here, exactly as it could during the original's spin.
void wait_vblank(Machine &m) {
    const uint16_t ax = m.ax(), cx = m.cx(), dx = m.dx(), bx = m.bx();
    const uint16_t bp = m.bp(), si = m.si(), di = m.di();
    const uint16_t ds = m.ds(), es = m.es();

    m.wr8(m.ss(), kVblankDone, 0);
    const uint8_t music_req = m.rd8(m.ss(), kMusicRequest);
    if (music_req != m.rd8(m.ss(), kMusicRequestPrev)) {
        m.wr8(m.ss(), kMusicRequestPrev, music_req);
        m.ax() = music_req;
        const uint16_t cs = m.cs();
        m.cs() = 0xD000;
        sound_request_core(m);
        m.cs() = cs;
    }
    const uint8_t sfx_req = m.rd8(m.ss(), kSfxRequest);
    if (sfx_req != 0) {
        m.ax() = sfx_req;
        const uint16_t cs = m.cs();
        m.cs() = 0xD000;
        sound_request_core(m);
        m.cs() = cs;
        m.wr8(m.ss(), kSfxRequest, 0);
    }
    while (computed::logic<uint8_t>(m, m.rd8(m.ss(), kVblankDone)) == 0)
        m.halt_and_wait();

    m.ax() = ax;
    m.cx() = cx;
    m.dx() = dx;
    m.bx() = bx;
    m.bp() = bp;
    m.si() = si;
    m.di() = di;
    m.ds() = ds;
    m.es() = es;
    m.retf();
}

// rom: 0x340051 (far)
// The port's second hardware frame point: waits until the raster line counter (port 0x02) drops below
// 0x8D (141) -- the original busy-polls the port every cycle; used right after VBlank processing so a
// following raster-effect write (line-compare setup, window scroll) lands once the display is back in the
// visible area rather than in the brief 141-143 tail before the next VBlank. AX is pushed on entry and
// popped unchanged on exit in the original (the port reading is used only for the loop condition, never
// kept); DS is pushed/popped too but never written, so it needs no restore.
// Parks on the raster clock itself (Machine::wait_next_line) rather than a flag: Wsc::tick fast-forwards to
// the next line boundary while parked and re-dispatches here once one passes, without touching any
// hardware register (no line-compare interrupt is armed) -- a pending interrupt can still preempt this
// exactly as it could the original's loop.
void wait_visible_scanline(Machine &m) {
    const uint16_t ax = m.ax();
    for (;;) {
        const uint8_t line = m.in8(0x02);
        (void)computed::subtract<uint8_t>(m, line, 0x8D);
        if (line < 0x8D)
            break;
        m.wait_next_line();
    }
    m.ax() = ax;
    m.retf();
}

// rom: 0x34005C
// Scans the keypad matrix (port 0xB5, selecting each of the 3 nibble groups with NOP delays between
// select and read) into a 12-bit raw reading. Updates: keys_held (0xC800) = this reading; keys_prev
// (0xC802) = the previous reading; keys_pressed (0xC804) = bits newly pressed this frame; keys_repeat
// (0xC806) = the auto-repeat "fire" value (newly-pressed bits immediately, then the held bits again every
// 7 frames after an initial 0x28-frame delay, 0 otherwise); keys_repeat_timer (0xC808) tracks that delay.
// If all of the three middle bits (0xE) are set at once, jumps straight back into main (some kind of
// reset combo) instead of returning. Returns AX = the raw reading; BX and DX preserved.

void read_keypad(Machine &m) {
    hooks::emit(hooks::Event::PresentationPicture, m, {1});
    uint16_t entry_bx = m.bx();
    uint16_t entry_dx = m.dx();

    uint16_t prev = m.rd16(m.ss(), kKeysHeld);
    m.wr16(m.ss(), kKeysPrev, prev);

    uint16_t dx = 0;
    m.out8(0xB5, 0x10);
    m.out8(0xB5, 0x10);
    dx = uint16_t((dx | (m.in8(0xB5) & 0xF)) << 4);
    m.out8(0xB5, 0x20);
    dx = uint16_t((dx | (m.in8(0xB5) & 0xF)) << 4);
    m.out8(0xB5, 0x40);
    dx = uint16_t(dx | (m.in8(0xB5) & 0xF));

    m.wr16(m.ss(), kKeysHeld, dx);
    uint16_t pressed = uint16_t((dx ^ prev) & dx);
    m.wr16(m.ss(), kKeysPressed, pressed);

    if (prev == dx) {
        uint16_t timer = uint16_t(m.rd16(m.ss(), kKeyRepeatTimer) - 1);
        if (timer == 0) {
            m.wr16(m.ss(), kKeyRepeatTimer, 7);
            m.wr16(m.ss(), kKeysRepeat, dx);
        } else {
            m.wr16(m.ss(), kKeyRepeatTimer, timer);
            m.wr16(m.ss(), kKeysRepeat, 0);
        }
    } else {
        m.wr16(m.ss(), kKeysRepeat, pressed);
        m.wr16(m.ss(), kKeyRepeatTimer, 0x28);
    }

    if (m.rd16(m.ss(), kKeysRepeat))
        hooks::emit(hooks::Event::PresentationPicture, m, {0});
    if ((dx & 0xE) == 0xE) {
        // ljmp 0x4000, main (0x340EE0): the entry bx/dx pushed at the top are never popped, so they stay
        // on the stack (SP -= 4) as this abandoned frame's live top instead of being restored.
        m.sp() = uint16_t(m.sp() - 4);
        m.wr16(m.ss(), m.sp(), entry_dx);
        m.wr16(m.ss(), uint16_t(m.sp() + 2), entry_bx);
        m.jmp_far(0x4000, 0x0EE0);
        return;
    }
    m.ax() = dx;
    m.retf();
}

// rom: 0x3401BD
// AL = a pseudo-random byte: entry (calls + frames) & 0xFF of a fixed 256-byte table, where calls counts
// calls to this routine. AH = 0. Far routine; BX preserved.
void random_byte_call_core(Machine &m) { m.ax() = next_random_byte(m); }
void random_byte(Machine &m) {
    random_byte_call_core(m);
    m.retf();
}

// Scale a table byte into inclusive AL..AH, rounding the product; reversed bounds wrap the byte span.
void random_range_core(Machine &m) {
    uint8_t lo = m.al();
    uint8_t range = uint8_t(m.ah() - lo);
    uint8_t random_byte = next_random_byte(m);
    // Result = low + round(random_byte * uint8_t(high - low) / 256); the high endpoint is reachable.
    uint16_t product = uint16_t(uint16_t(random_byte) * range);
    uint8_t scaled = uint8_t(product >> 8);
    if (product & 0x80)
        scaled = uint8_t(scaled + 1);
    m.ax() = uint8_t(scaled + lo);
}
// rom: 0x3401D7
// AX in: AL = low bound, AH = high bound. range = AH - AL; draws a byte r from random_byte's generator
// (the inlined call reproduces its counter side effect, see next_random_byte); scales it by range/256,
// rounding up when the low byte of r*range has its top bit set. Returns AL = low + scaled in [low, high],
// AH = 0. DX preserved (pushed/popped around the whole body in the original).
void random_range(Machine &m) {
    random_range_core(m);
    m.retf();
}

// Copies CX 4bpp tiles (32 bytes each) from DS:SI to tile AX of the tile area (0400:AX*32). SI advances past
// the data; the original leaves AX = 0400h (the segment it loaded into ES) and CX = 0; DI is preserved.
void copy_tiles_core(Machine &m) {
    clear_direction(m);
    uint16_t dst = uint16_t(m.ax() << 5);
    uint16_t words = uint16_t(m.cx() << 4);
    for (uint16_t i = 0; i < words; ++i) {
        m.wr16(kTileSeg, dst, m.rd16(m.ds(), m.si()));
        dst = uint16_t(dst + 2);
        m.si() = uint16_t(m.si() + 2);
    }
    m.ax() = kTileSeg;
    m.cx() = 0;
}
// Shared body of ROM 0x34028E
// Copies CX 4bpp tiles (32 bytes each) from DS:SI to tile AX of the tile area (0400:AX*32). SI advances past

// rom: 0x34028E
void copy_tiles(Machine &m) {
    copy_tiles_core(m);
    m.retf();
}

// rom: 0x3402A3
// Clears the 4bpp tile area (0400:0000-7FFF). Registers preserved.
void clear_tiles(Machine &m) {
    clear_direction(m);
    for (uint32_t off = 0; off < 0x8000; off += 2)
        m.wr16(kTileSeg, uint16_t(off), 0);
    m.retf();
}

// Fills a CH (height) x CL (width) tile rectangle of a screen map with the constant tile word DX. AX =
// screen page ((AX+2)*0x800, e.g. page 0 -> 0x1000, page 1 -> 0x1800); BH = row, BL = column (col*2, an
// 8-bit `shl bl,1` -- no carry into BH, and BX is left doubled like this afterward, a dead leftover); CL =
// width (<=0x20), CH = height (both use the usual "0 means 256" dec/jne idiom, CL reloaded each row); row
// stride is 0x40 bytes (32 columns). DS is forced to 0 (kIram) for the writes and DI is preserved
// (explicit push/pop); AX ends up holding the per-row skip constant (0x0040 - 2*CL, low byte only, AH=0);
// CX ends CH=0, CL=entry CL (reloaded by the last row).
void fill_screen_rect_core(Machine &m) {
    uint16_t page = uint16_t((m.ax() + 2) << 11);
    uint16_t bx = m.bx();
    uint16_t row_part = uint16_t(uint16_t(uint8_t(bx >> 8)) << 8) >> 2; // xor al,al; shr ax,2
    uint8_t col_x2 = uint8_t(uint8_t(bx) << 1);                         // shl bl,1
    m.bx() = uint16_t((bx & 0xFF00) | col_x2);                          // leftover: bl doubled
    uint16_t di = uint16_t(page + uint16_t((row_part & 0xFF00) | uint8_t(uint8_t(row_part) + col_x2)));

    uint8_t width = uint8_t(m.cx());
    uint8_t height = uint8_t(m.cx() >> 8);
    uint16_t row_skip = uint8_t(uint8_t(0x40) - width - width); // ah = 0
    uint16_t dx = m.dx();

    uint8_t ch = height;
    bool row_carry = false;
    do {
        uint8_t cl = width;
        do {
            m.wr16(kIram, di, dx);
            di = uint16_t(di + 2);
        } while (--cl != 0);
        row_carry = unsigned(di) + row_skip > 0xFFFF;
        di = uint16_t(di + row_skip);
    } while (--ch != 0);

    // The final DEC CH (1 to 0) preserves carry from ADD DI,AX.
    m.flags() = uint16_t((m.flags() & ~(emu::PF | emu::AF | emu::ZF | emu::SF | emu::OF | emu::CF)) |
                         emu::PF | emu::ZF | (row_carry ? emu::CF : 0));

    m.ax() = row_skip;
    m.cx() = width;
}
// rom: 0x3402BC
// Fills a CH (height) x CL (width) tile rectangle of a screen map with the constant tile word DX. AX =
void fill_screen_rect(Machine &m) {
    fill_screen_rect_core(m);
    m.retf();
}

// Row/column packing shared by draw_text, draw_window(_alt) and draw_tilemap(_offset): DI = kScreenMap +
// row*0x800 + col*2, but the column add is an 8-bit `add al,bl` -- only the low byte, no carry into the
// high byte -- reproduced bit-for-bit including that truncation (harmless since row*0x800's low byte is
// always 0, so it doesn't matter whether kScreenMap is folded in before or after). Exported (external
// linkage): bank34_engine.cpp's near text/cursor routines reach draw_text via real `lcall`s from other near
// routines, so they inline this the same way next_random_byte and draw_tilemap_offset_core are inlined
// elsewhere in this file.
uint16_t screen_map_offset(uint8_t row, uint16_t bx) {
    uint16_t ax = uint16_t(uint16_t(row) << 8); // mov ah,al; xor al,al
    ax = uint16_t(ax << 3);
    uint16_t bx_masked = uint16_t(bx & 0xFF00);                   // xor bl,bl
    ax = uint16_t(ax + (bx_masked >> 2));                         // shr bx,2; add ax,bx
    uint8_t col_x2 = uint8_t((bx & 0xFF) << 1);                   // shl bl,1
    ax = uint16_t((ax & 0xFF00) | uint8_t(uint8_t(ax) + col_x2)); // add al,bl
    return uint16_t(ax + kScreenMap);
}

// Core of draw_tilemap_offset (no register/flag side effects besides memory): factored out (external
// linkage, declared in bank34_menus.cpp) so draw_status_icons (0x34CB44), which calls draw_tilemap_offset
// internally, can reuse it without a real far call (there's no return address on the stack to unwind, same
// reasoning as next_random_byte above).
void draw_tilemap_offset_core(Machine &m) {
    uint16_t row_di = screen_map_offset(m.al(), m.bx());

    uint16_t si = m.si();
    uint16_t dx = m.dx();
    uint16_t ds = m.ds(), ss = m.ss();
    int height = uint8_t(m.cx() >> 8) == 0 ? 256 : uint8_t(m.cx() >> 8); // CH
    int width = uint8_t(m.cx()) == 0 ? 256 : uint8_t(m.cx());            // CL

    for (int row = 0; row < height; ++row) {
        uint16_t di = row_di;
        for (int column = 0; column < width; ++column) {
            m.wr16(ss, di, uint16_t(m.rd16(ds, si) + dx));
            si = uint16_t(si + 2);
            di = uint16_t(di + 2);
        }
        row_di = uint16_t(row_di + 0x40);
    }
}

// Core of draw_tilemap (no register/flag side effects besides memory): factored out (external linkage,
// declared in bank34.h) so bank34_engine.cpp's near routines that reach draw_tilemap via a real `lcall` can
// reuse it without a genuine machine call (same reasoning as draw_tilemap_offset_core above).
void draw_tilemap_core(Machine &m) {
    uint16_t di = screen_map_offset(m.al(), m.bx());
    uint16_t si = m.si();
    uint16_t ds = m.ds(), ss = m.ss();
    int height = uint8_t(m.cx() >> 8) == 0 ? 256 : uint8_t(m.cx() >> 8);
    uint8_t width = uint8_t(m.cx());
    uint16_t row_skip = uint16_t(uint8_t(0x20 - width) * 2);
    int16_t step = (m.flags() & emu::DF) ? -2 : 2;
    for (int row = 0; row < height; ++row) {
        for (uint8_t column = 0; column < width; ++column) {
            m.wr16(ss, di, m.rd16(ds, si));
            si = uint16_t(si + step);
            di = uint16_t(di + step);
        }
        di = uint16_t(di + row_skip);
    }
}

// rom: 0x34032D
// Copies a CH x CL tile map (rows x columns) from DS:SI into the screen map at 0x1000, row AL / column BL
// (packed like draw_window/draw_tilemap_offset); each row's copy is a real `rep movsw` (unlike
// draw_tilemap_offset's hand-written loop) and -- unlike every sibling in this file -- draw_tilemap never
// clears DF, so it runs SI/DI backward through the row when the caller left DF set. CH counts down to 0 (0
// means 256, the usual dec/jne idiom); CL is a plain rep count (0 copies nothing, no 256 fallback) and is
// reloaded each row. The inter-row skip (0x20-CL, doubled) is a plain `add di,ax`, always forward
// regardless of DF. Same preserve-everything convention as draw_tilemap_offset (pushaw + ds/es/ss bracket
// the whole body): only the screen-map memory changes.
void draw_tilemap(Machine &m) {
    draw_tilemap_core(m);
    m.retf();
}

// Shared body of ROM 0x340369
// Like draw_tilemap (0x34032D), but adds DX to every source tile word before storing it (reusing one map
// layout against different tile banks/attribute offsets). Copies a CH x CL tile map (rows x columns) from
// DS:SI into the screen map at 0x1000, row AL / column BL of it (BH folds in as an extra 64-word offset,
// same row/column packing as draw_window). Screen rows are 0x40 bytes (32 tile words) apart. CH/CL count
// down to 0 (a count of 0 means 256, the classic `dec/jne` idiom) and are reloaded to the original width
// each row. Every register and segment is preserved (pushaw and ds/es/ss are pushed and popped around the
// whole body); only the screen-map memory is written.

// rom: 0x340369
void draw_tilemap_offset(Machine &m) {
    draw_tilemap_offset_core(m);
    m.retf();
}

namespace {
// Shared body of draw_window / draw_window_alt: draws a 9-tile window frame (tiles[0..8] = top-left,
// top edge, top-right, left edge, interior fill, right edge, bottom-left, bottom edge, bottom-right) of
// CL x CH tiles into the screen map, row AL / column BL (packed like draw_tilemap_offset). Leaves
// AX = tiles[8] (the last tile written), BX = entry BX with BL doubled (an 8-bit `shl bl,1`, no carry
// into BH -- the same dead leftover as fill_screen_rect/draw_tilemap_offset), CX = 0; DI/ES/DS untouched
// (the originals preserve them via push/pop around the whole body); DF cleared.
void draw_window_frame(Machine &m, uint16_t seg, const uint16_t tiles[9]) {
    clear_direction(m);
    uint16_t bx = m.bx();
    uint16_t di = screen_map_offset(m.al(), bx);
    uint8_t col_x2 = uint8_t(uint8_t(bx) << 1);
    m.bx() = uint16_t((bx & 0xFF00) | col_x2);

    uint8_t width = uint8_t(m.cx());
    uint8_t height = uint8_t(m.cx() >> 8);
    uint8_t mid = uint8_t(width - 2);

    uint16_t row_start = di;
    auto draw_row = [&](uint16_t left, uint16_t fill, uint16_t right) {
        di = row_start;
        m.wr16(seg, di, left);
        di = uint16_t(di + 2);
        for (uint8_t i = 0; i < mid; ++i) {
            m.wr16(seg, di, fill);
            di = uint16_t(di + 2);
        }
        m.wr16(seg, di, right);
    };

    draw_row(tiles[0], tiles[1], tiles[2]);
    row_start = uint16_t(row_start + 0x40); // the original restores DI from the stack, then steps 0x40
    uint8_t middle_rows = uint8_t(height - 2);
    for (uint8_t row = 0; row < middle_rows; ++row) {
        draw_row(tiles[3], tiles[4], tiles[5]);
        row_start = uint16_t(row_start + 0x40);
    }

    draw_row(tiles[6], tiles[7], tiles[8]);
    m.ax() = tiles[8];
    m.cx() = 0;
}
} // namespace

// Shared body of ROM 0x34047F
// Second window-frame border, same layout/parameters as draw_window (AL=row, BL=col, CL=width, CH=height)
// but a different tile set: corners/edges 0x1FE6/0x1FE9/0x1FEB, written to ES=0 (kIram) rather than ES=SS.
// A genuine ROM quirk: the middle rows' interior fill tile is 0x1FD9 (draw_window's own interior tile,
// not an alt one) and the right edge is 0x1FEA (one past the left edge 0x1FE9, i.e. as if the fill slot
// were skipped) -- reproduced exactly, not "fixed". Called from the status/icon popup code (open_status_
// popup) with a fixed row/col/size, so likely the "message box" skin as opposed to draw_window's normal
// skin.
void draw_window_alt_core(Machine &m) {
    static constexpr uint16_t kTiles[9] = {0x1FE6, 0x1FE7, 0x1FE8, 0x1FE9, 0x1FD9,
                                           0x1FEA, 0x1FEB, 0x1FEC, 0x1FED};
    draw_window_frame(m, kIram, kTiles);
    return;
}

// rom: 0x34047F
void draw_window_alt(Machine &m) {
    draw_window_alt_core(m);
    m.retf();
}

// Draws a window frame (tiles 0x1FD5-0x1FDD, consecutive: TL/top/TR/left/fill/right/BL/bottom/BR) into the
// screen map at 0x1000, ES=SS. AL = row, BL = column, CL = width, CH = height.
void draw_window_core(Machine &m) {
    static constexpr uint16_t kTiles[9] = {0x1FD5, 0x1FD6, 0x1FD7, 0x1FD8, 0x1FD9,
                                           0x1FDA, 0x1FDB, 0x1FDC, 0x1FDD};
    draw_window_frame(m, m.ss(), kTiles);
}
// rom: 0x340503
// Draws a window frame (tiles 0x1FD5-0x1FDD, consecutive: TL/top/TR/left/fill/right/BL/bottom/BR) into the
void draw_window(Machine &m) {
    draw_window_core(m);
    m.retf();
}

// Copies CX palettes (16 colors, 32 bytes each) from DS:SI to RAM ((DI + AX*32) & 3FFFh) + C000h -- with
// DI = C600h as every caller passes, palette AX of the color palette area at FE00h. SI and DI advance past
// the data; AX = 0; CX preserved.
void copy_palettes_core(Machine &m) {
    clear_direction(m);
    uint16_t di = uint16_t(((m.di() + (m.ax() << 5)) & 0x3FFF) + kSpriteBuffer);
    uint16_t words = uint16_t(m.cx() << 4);
    for (uint16_t i = 0; i < words; ++i) {
        m.wr16(kIram, di, m.rd16(m.ds(), m.si()));
        di = uint16_t(di + 2);
        m.si() = uint16_t(m.si() + 2);
    }
    m.di() = di;
    m.ax() = 0;
}
// Shared body of ROM 0x340582
// Copies CX palettes (16 colors, 32 bytes each) from DS:SI to RAM ((DI + AX*32) & 3FFFh) + C000h -- with

// rom: 0x340582
void copy_palettes(Machine &m) {
    copy_palettes_core(m);
    m.retf();
}

// rom: 0x34059F
// Fills CX palettes (16 colors/32 bytes each) in the palette_buffer window with the constant color word
// DX, at ((DI + (AX & 0xF)*32) & 3FFFh) + C000h -- same addressing as copy_palettes, just a memset instead
// of a copy. All registers preserved (pushaw and ds/es/ss bracket the whole body); only memory is written.
void fill_palette_core(Machine &m) {
    clear_direction(m);
    uint16_t di = uint16_t(((m.di() + ((m.ax() & 0xF) << 5)) & 0x3FFF) + kSpriteBuffer);
    uint16_t words = uint16_t(m.cx() << 4);
    uint16_t color = m.dx();
    for (uint16_t i = 0; i < words; ++i) {
        m.wr16(kIram, di, color);
        di = uint16_t(di + 2);
    }
}
void fill_palette(Machine &m) {
    fill_palette_core(m);
    m.retf();
}

namespace {
uint8_t inc_nibble_sat(uint8_t nibble) { return nibble == 0xF ? nibble : uint8_t(nibble + 1); }
uint8_t dec_nibble_sat(uint8_t nibble) { return nibble == 0 ? nibble : uint8_t(nibble - 1); }
uint8_t nibble_toward(uint8_t cur, uint8_t tgt) {
    if (cur == tgt)
        return cur;
    return cur < tgt ? uint8_t(cur + 1) : uint8_t(cur - 1);
}
} // namespace

// rom: 0x3405C5
// For CX palettes (masked to 0x1F; a no-op if 0) starting at palette (AX & 0xF), increments each of the 3
// nibbles (R = bits8-11, G = bits4-7, B = bits0-3) of every color word in palette_buffer (0xC400),
// saturating at 0xF; the top nibble of the high byte is always zeroed (an unconditional `and bh,0xf` in
// the original, harmless since WSC colors are 12-bit). One-step "fade toward white" helper, called
// repeatedly with dma_palettes/wait_vblank in between for a smooth fade. All registers preserved (pushaw
// brackets the whole body); only palette_buffer memory changes.
void brighten_palette_step_core(Machine &m) {
    uint16_t count = uint16_t(m.cx() & 0x1F);
    if (count == 0) {
        return;
    }
    uint16_t si = uint16_t(kPaletteBuffer + ((m.ax() & 0xF) << 5));
    uint16_t words = uint16_t(count << 4);
    for (uint16_t i = 0; i < words; ++i) {
        uint16_t color = m.rd16(m.ss(), si);
        uint8_t blue = inc_nibble_sat(uint8_t(color & 0xF));
        uint8_t green = inc_nibble_sat(uint8_t((color >> 4) & 0xF));
        uint8_t red = inc_nibble_sat(uint8_t((color >> 8) & 0xF));
        m.wr16(m.ss(), si, uint16_t((uint16_t(red) << 8) | (uint16_t(green) << 4) | blue));
        si = uint16_t(si + 2);
    }
}
void brighten_palette_step(Machine &m) {
    brighten_palette_step_core(m);
    m.retf();
}

// rom: 0x340607
// Same as brighten_palette_step but decrements each nibble, saturating at 0. This is the per-frame step
// used by fade_palettes_16 (0x3503BE) to fade all palettes to black over 16 frames.
void darken_palette_step_core(Machine &m) {
    uint16_t count = uint16_t(m.cx() & 0x1F);
    if (count == 0) {
        return;
    }
    uint16_t si = uint16_t(kPaletteBuffer + ((m.ax() & 0xF) << 5));
    uint16_t words = uint16_t(count << 4);
    for (uint16_t i = 0; i < words; ++i) {
        uint16_t color = m.rd16(m.ss(), si);
        uint8_t blue = dec_nibble_sat(uint8_t(color & 0xF));
        uint8_t green = dec_nibble_sat(uint8_t((color >> 4) & 0xF));
        uint8_t red = dec_nibble_sat(uint8_t((color >> 8) & 0xF));
        m.wr16(m.ss(), si, uint16_t((uint16_t(red) << 8) | (uint16_t(green) << 4) | blue));
        si = uint16_t(si + 2);
    }
}
void darken_palette_step(Machine &m) {
    darken_palette_step_core(m);
    m.retf();
}

// rom: 0x34063D
// For CX palettes (masked 0x1F) from palette (AX & 0xF): nudges each nibble of palette_buffer (0xC400) up
// by 1 toward the matching nibble of palette_target_buffer (0xC600), one-directional (never down).
// Original-game bug kept: blue/red skip the step once they reach or pass the target
// (`jge`), but the green nibble only skips on an exact match (`je`) -- if it overshoots the target it keeps
// climbing (and wraps mod 16, carrying nothing into the adjacent nibble, when it was already 0xF). All
// registers preserved (pushaw brackets the whole body).
void palette_step_up_toward_core(Machine &m) {
    uint16_t count = uint16_t(m.cx() & 0x1F);
    if (count == 0) {
        return;
    }
    uint16_t off = uint16_t((m.ax() & 0xF) << 5);
    uint16_t si = uint16_t(kPaletteTarget + off);
    uint16_t di = uint16_t(kPaletteBuffer + off);
    uint16_t words = uint16_t(count << 4);
    for (uint16_t i = 0; i < words; ++i) {
        uint16_t cur = m.rd16(m.ss(), di);
        uint16_t tgt = m.rd16(m.ss(), si);
        uint8_t bl = uint8_t(cur), bh = uint8_t(cur >> 8);
        if (!(uint8_t(bl & 0xF) >= uint8_t(tgt & 0xF)))
            bl = uint8_t(bl + 1);
        if (uint8_t(bl & 0xF0) != uint8_t(tgt & 0xF0))
            bl = uint8_t(bl + 0x10);
        bh = uint8_t(bh & 0xF);
        if (!(bh >= uint8_t((tgt >> 8) & 0xF)))
            bh = uint8_t(bh + 1);
        m.wr16(m.ss(), di, uint16_t(bl | (uint16_t(bh) << 8)));
        si = uint16_t(si + 2);
        di = uint16_t(di + 2);
    }
}
void palette_step_up_toward(Machine &m) {
    palette_step_up_toward_core(m);
    m.retf();
}

// rom: 0x3406EF
// Bidirectional version of palette_step_up_toward: for CX palettes from palette (AX & 0xF), nudges each
// nibble of palette_buffer (0xC400) by +-1 per call toward the matching nibble of palette_target_buffer
// (0xC600) (no change if already equal); unlike palette_step_up_toward, all three nibbles use the same
// correct >=/<= comparisons, so this one doesn't overshoot. Used in several 16-frame crossfade loops
// together with dma_palettes/wait_vblank. All registers preserved (pushaw brackets the whole body).
void palette_step_toward_core(Machine &m) {
    uint16_t count = uint16_t(m.cx() & 0x1F);
    if (count == 0) {
        return;
    }
    uint16_t off = uint16_t((m.ax() & 0xF) << 5);
    uint16_t si = uint16_t(kPaletteTarget + off);
    uint16_t di = uint16_t(kPaletteBuffer + off);
    uint16_t words = uint16_t(count << 4);
    for (uint16_t i = 0; i < words; ++i) {
        uint16_t cur = m.rd16(m.ss(), di);
        uint16_t tgt = m.rd16(m.ss(), si);
        uint8_t blue = nibble_toward(uint8_t(cur & 0xF), uint8_t(tgt & 0xF));
        uint8_t green = nibble_toward(uint8_t((cur >> 4) & 0xF), uint8_t((tgt >> 4) & 0xF));
        uint8_t red = nibble_toward(uint8_t((cur >> 8) & 0xF), uint8_t((tgt >> 8) & 0xF));
        m.wr16(m.ss(), di, uint16_t((uint16_t(red) << 8) | (uint16_t(green) << 4) | blue));
        si = uint16_t(si + 2);
        di = uint16_t(di + 2);
    }
}
void palette_step_toward(Machine &m) {
    palette_step_toward_core(m);
    m.retf();
}

// rom: 0x34075B
// Triggers a GDMA transfer of 0x200 (512) bytes from sprite_buffer (RAM 0xC000) to OAM (0x0200): the
// standard "flip the sprite table" call, done once per frame after building sprites in RAM. Leaves
// AX = 0x0280 (AL from the final `mov al,0x80`, AH left over from the previous `mov ax,0x200`); every
// other register is untouched.
void dma_sprite_table_core(Machine &m) {
    out16(m, 0x40, kSpriteBuffer);    // GDMA source = 0xC000 (sprite_buffer, IRAM)
    out16(m, 0x42, 0x0000);           // source bank/high bits = 0 (IRAM)
    out16(m, 0x44, kHardwareSprites); // GDMA destination = 0x0200 (OAM)
    out16(m, 0x46, 0x0200);           // length = 0x200 bytes
    m.out8(0x48, 0x80);               // start, increment addresses
    m.wsc().publish_terrain_cards();  // presentation provenance follows the OAM upload
    m.ax() = 0x0280;
}
void dma_sprite_table(Machine &m) {
    dma_sprite_table_core(m);
    m.retf();
}

// rom: 0x34078A
// Triggers a GDMA transfer of 0x200 (512) bytes from palette_buffer (RAM 0xC400) to palette RAM (0xFE00):
// the "flip the palette buffer" call. Leaves AX = 0x0280, same as dma_sprite_table; every other register
// is untouched.
void dma_palettes_core(Machine &m) {
    out16(m, 0x40, kPaletteBuffer);    // GDMA source = 0xC400 (palette_buffer, IRAM)
    out16(m, 0x42, 0x0000);            // source bank/high bits = 0 (IRAM)
    out16(m, 0x44, kHardwarePalettes); // GDMA destination = 0xFE00 (palette RAM)
    out16(m, 0x46, 0x0200);            // length = 0x200 bytes
    m.out8(0x48, 0x80);                // start, increment addresses
    m.ax() = 0x0280;
}
void dma_palettes(Machine &m) {
    dma_palettes_core(m);
    m.retf();
}

// Writes 4 consecutive 8x8 OAM entries (sprite_buffer at 0xC000, 4 bytes each) starting at slot AX,
// forming a 16x16 metasprite: tile+attribute word DX (auto-incremented for each of the other 3 quadrants),
// position word packed from BX with the two bytes swapped (`xchg bl,bh`), offset by +8 in each axis in
// turn for the other three quadrants ((0,0), (0,+8), (+8,0), (+8,+8) in whichever axis each byte is).
// All registers preserved (ax/bx/dx/di pushed and popped around the whole body); only the 16 bytes of
// OAM data are written. Exported (external linkage): bank34_engine.cpp's cursor-sprite routines
// (draw_race_select_cursor and its siblings) reach this via real `lcall`s from other near routines, so they
// inline it the same way next_random_byte is inlined elsewhere in this file. Doesn't touch ax/bx/dx/di
// itself (only reads them), so preserving them is automatic for any caller that doesn't either.
void set_sprite_16x16_core(Machine &m) {
    uint16_t di = uint16_t((m.ax() << 2) + kSpriteBuffer);
    uint16_t dx = m.dx();
    uint8_t lo = uint8_t(m.bx() >> 8);   // xchg bl,bh: new bl = old bh
    uint8_t hi = uint8_t(m.bx() & 0xFF); // xchg bl,bh: new bh = old bl
    auto pos = [&] { return uint16_t(lo | (hi << 8)); };

    m.wr16(m.ss(), di, dx);
    m.wr16(m.ss(), uint16_t(di + 2), pos());

    hi = computed::add<uint8_t>(m, hi, 8);
    dx = computed::increment<uint16_t>(m, dx);
    m.wr16(m.ss(), uint16_t(di + 4), dx);
    m.wr16(m.ss(), uint16_t(di + 6), pos());

    hi = computed::subtract<uint8_t>(m, hi, 8);
    lo = computed::add<uint8_t>(m, lo, 8);
    dx = computed::increment<uint16_t>(m, dx);
    m.wr16(m.ss(), uint16_t(di + 8), dx);
    m.wr16(m.ss(), uint16_t(di + 0xA), pos());

    hi = computed::add<uint8_t>(m, hi, 8);
    dx = computed::increment<uint16_t>(m, dx);
    m.wr16(m.ss(), uint16_t(di + 0xC), dx);
    m.wr16(m.ss(), uint16_t(di + 0xE), pos());
}

// rom: 0x340839
// Writes 4 consecutive 8x8 OAM entries (sprite_buffer at 0xC000, 4 bytes each) starting at slot AX,
void set_sprite_16x16(Machine &m) {
    set_sprite_16x16_core(m);
    m.retf();
}

// Writes an 0xFF-terminated string at DS:SI into the screen map at 0x1000 (tile word = 0x1F00 | character
// byte), AL = row, BL = column (packed like draw_window). Advances SI past each character written,
// stopping with SI pointing AT the terminator (not past it); returns AX = 0x1FFF (AH stuck at 0x1F, AL =
// the terminator byte). BX ends up with BL += the number of characters written (an 8-bit `inc bl` per
// char, no carry into BH -- the column computed for drawing is never itself written back). DI/ES/DS/CX/DX
// preserved (DI/ES via push/pop, the rest simply untouched); DF cleared. Exported (external linkage):
// bank34_engine.cpp's draw_play_time reaches this via a real `lcall` from another near routine, so it
// inlines the core the same way next_random_byte is inlined elsewhere in this file.
void draw_text_core(Machine &m) {
    m.watch_text("draw_text");
    clear_direction(m);
    uint16_t entry_bx = m.bx();
    uint16_t di = screen_map_offset(m.al(), entry_bx);
    if (const std::u32string *text = proportional_text(m)) { // a loaded language's string: our font
        const int cells = draw_table_text(m, di, 0x1F00, *text);
        skip_text(m);
        m.ax() = 0x1FFF;
        m.bx() = uint16_t((entry_bx & 0xFF00) | uint8_t(uint8_t(entry_bx) + cells));
        (void)computed::subtract<uint8_t>(m, 0xFF, 0xFF);
        return;
    }
    uint16_t si = m.si();
    uint8_t bl = uint8_t(entry_bx);
    uint16_t ax = 0x1F00;
    for (;;) {
        uint8_t ch = m.rd8(m.ds(), si);
        ax = uint16_t((ax & 0xFF00) | ch);
        if (ch == 0xFF)
            break;
        ++si;
        ++bl;
        m.wr16(kIram, di, ax);
        di = uint16_t(di + 2);
    }
    m.si() = si;
    m.ax() = ax;
    m.bx() = uint16_t((entry_bx & 0xFF00) | bl);
    (void)computed::subtract<uint8_t>(m, 0xFF, 0xFF); // final terminator comparison
}

// Shared body of ROM 0x34091D
// Writes an 0xFF-terminated string at DS:SI into the screen map at 0x1000 (tile word = 0x1F00 | character

// rom: 0x34091D
void draw_text(Machine &m) {
    draw_text_core(m);
    m.retf();
}

// Shared body of ROM 0x340B73
// Converts AX (0-65535) into 5 decimal digits at DS:SI[0..4] (ten-thousands..ones), by successively
// subtracting 10000/1000/100/10 (borrow-free) and counting how many times each succeeds before writing
// the final remainder to SI[4]. SI's 5-byte buffer is zeroed by the routine itself. Feeds the on-screen
// number renderers (format_number, draw_numeral_string). Returns AX = the final remainder (0-9, same as
// SI[4]); BX/CX/DX preserved. Exported (external linkage): bank34_engine.cpp's play-time/stat formatters
// (format_stat_2digit and its siblings) reach this via real `lcall`s from other near routines, so they
// inline the core the same way next_random_byte is inlined elsewhere in this file.
uint16_t number_to_digits_core(Machine &m, uint16_t number, uint16_t ds, uint16_t si) {
    static constexpr uint16_t kPlaces[4] = {10000, 1000, 100, 10};
    m.wr16(ds, si, 0);
    m.wr16(ds, uint16_t(si + 2), 0);
    m.wr8(ds, uint16_t(si + 4), 0);

    for (int digit = 0; digit < 4; ++digit) {
        uint16_t place = kPlaces[digit];
        uint8_t count = 0;
        while (number >= place) {
            number = uint16_t(number - place);
            ++count;
        }
        m.wr8(ds, uint16_t(si + digit), count);
    }
    m.wr8(ds, uint16_t(si + 4), uint8_t(number));
    return number;
}

void number_to_digits_entry_core(Machine &m) {
    m.ax() = number_to_digits_core(m, m.ax(), m.ds(), m.si());
    return;
}

// rom: 0x340B73
void number_to_digits(Machine &m) {
    number_to_digits_entry_core(m);
    m.retf();
}

// Initialize the starting map/party, empty inventories, dialogue settings and per-member history records.
void clear_game_state_core(Machine &m) {
    if (auto *session = Session::find(m.wsc()))
        session->player_names().reset();
    uint16_t ss = m.ss();
    m.es() = ss;

    for (uint16_t state_address = kGameState; state_address < kGameState + 0x438;
         state_address = uint16_t(state_address + 2))
        m.wr16(ss, state_address, 0);

    m.wr8(ss, kMapId, 1);
    m.wr8(ss, kPreviousMap, 1);
    m.wr8(ss, kPlayerCell, 0x11);
    m.wr8(ss, kPreviousPlayerX, 0x11);
    m.wr8(ss, kPlayerY, 0x22);
    m.wr8(ss, kPreviousPlayerY, 0x22);
    m.wr8(ss, kTowerCounter, 1);
    m.wr8(ss, kCurrentFloor, 1);

    m.wr8(ss, kGameState, 0xFF);
    m.wr8(ss, kMember1Record, 0xFF);
    m.wr8(ss, kMember2Record, 0xFF);
    m.wr8(ss, kMember3Record, 0xFF);

    m.wr8(ss, kDialogueSpeed, 4); // inlined L_347722:
    m.wr8(ss, kDialogueSpeed, 4);
    // Dialogue-order defaults are packed category assignments (3, 1, 8, 2), not member indices.
    m.wr8(ss, kPartyOrder, 3);
    m.wr8(ss, (kPartyOrder + 0x1), 1);
    m.wr8(ss, (kPartyOrder + 0x2), 8);
    m.wr8(ss, kPartyOrderLast, 2);
    m.wr8(ss, kOptions, 0x1F);

    for (uint16_t i = 0; i < 0x10; ++i)
        m.wr8(ss, uint16_t(kInventory + i), 0xFF);
    for (uint16_t i = 0; i < 0x12; ++i)
        m.wr8(ss, uint16_t(kKeyItems + i), 0xFF);

    uint16_t cs = m.cs();
    m.ds() = cs;
    constexpr uint16_t kD3410F0 = 0x10F0;
    for (uint16_t i = 0; i < 0xC; ++i)
        m.wr8(ss, uint16_t(kVehicleLocations + i), m.rd8(cs, uint16_t(kD3410F0 + i)));

    for (uint16_t i = 0; i < 0x13; ++i) // inlined L_345E5A
        m.wr8(ss, uint16_t(kEventFlags + i), 0);

    static constexpr uint16_t kPartyRecord[4] = {kMemberQueues, kSecondMemberQueues, kThirdMemberQueues,
                                                 kFourthMemberQueues}; // D_34B92D, inlined
    uint16_t di_end = 0;                                               // L_345E78/L_345E8D
    for (uint16_t base : kPartyRecord) {
        m.wr16(ss, base, 0x0000); // offset of wait_frames (rom 0x340000)
        m.wr16(ss, uint16_t(base + 2), 0x63);
        for (uint16_t i = 0; i < 200; ++i)
            m.wr8(ss, uint16_t(base + 4 + i), 0xFF);
        di_end = uint16_t(base + 4 + 200);
    }

    m.ax() = 0x4003;
    m.cx() = 0;
    m.si() = uint16_t(kD3410F0 + 0xC);
    m.di() = di_end;
}
// Shared body of ROM 0x34106D
// Zeroes game_state (0xD000-0xD437, 0x21C words) and sets its defaults: current_map/player_x/player_y

// rom: 0x34106D (near)
void clear_game_state(Machine &m) {
    clear_game_state_core(m);
    m.ret();
}

// AL = a character/monster type byte; switches DS to bank 0x9000 and looks up a word in a 256-entry table
// D_3976E8 indexed by AL. Result: AX = the looked-up word; BX/DS preserved (pushed and popped).
void char_type_to_gfx_index_core(Machine &m) {
    constexpr uint16_t kD3976E8 = 0x76E8;
    uint16_t index = uint16_t(uint8_t(m.al()));
    m.ax() = table_at16(m, Table::monster_type_graphics, 0x9000, uint16_t(kD3976E8 + index * 2));
}
// rom: 0x34129A
// AL = a character/monster type byte; switches DS to bank 0x9000 and looks up a word in a 256-entry table
void char_type_to_gfx_index(Machine &m) {
    char_type_to_gfx_index_core(m);
    m.retf();
}

// AL = index; switches DS to bank 0x9000 (not restored) and computes SI = 0x6FE0 + AL*9 (a fixed-width,
// 9-byte-per-entry table at ROM bank 0x39 offset 0x6FE0). AX ends up equal to SI too (an 8-bit `mul cl`
// that overwrites all of AX with the AL*9 product before 0x6FE0 is added); BX/DX/CX preserved (CX is
// pushed/popped despite being pure scratch here).
void lookup_name_record_core(Machine &m) {
    const TextPtr rec =
        name_record(m, NameTable::Monster, m.al()); // a loaded language's (text.h) or the ROM's
    m.ds() = rec.segment;
    m.ax() = rec.offset;
    m.si() = rec.offset;
}
// rom: 0x3412F6
// AL = index; switches DS to bank 0x9000 (not restored) and computes SI = 0x6FE0 + AL*9 (a fixed-width,
void lookup_name_record(Machine &m) {
    lookup_name_record_core(m);
    m.retf();
}

} // namespace sawgod::game
