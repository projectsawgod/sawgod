#include "game/computed_handlers.h"
#include "game/text.h"
#include "game/text_render.h"
// Bank-34 character-creation/menu cursors, decimal/BCD text formatting, save verification and event flags.
// Shared cores retain the original near/far entry contracts without synthetic machine returns.
#include "game/bank34.h"
#include "game/bank3d_sound.h"
#include "game/save_slots.h"

namespace sawgod::game {

namespace {
constexpr uint16_t kMenuCursorPosition = 0x014A; // menu_cursor_position
constexpr uint16_t kPlayTimeFrames = 0xD0DF;     // play_time_frames
constexpr uint16_t kEventFlags = 0xD0EC;         // event_flags
constexpr uint16_t kMenuCursorAnchor = 0xD6FC;   // menu_cursor_anchor
constexpr uint16_t kPlayMinutesText = 0xD807;    // play_minutes_text
constexpr uint16_t kPlaySecondsText = 0xD80B;    // play_seconds_text
constexpr uint16_t kDigitBuffer = 0xD82B;        // digit_buffer
constexpr uint16_t kMenuSelection = 0xD841;      // race_select_index
constexpr uint16_t kSaveSlotSelection = 0xD842;  // class_select_index
constexpr uint16_t kSaveChoice = 0xD843;         // gender_select_index
constexpr uint16_t kCreationLeader = 0xD852;     // creation_leader
constexpr uint16_t kCreationRace = 0xD853;       // creation_race
constexpr uint16_t kPlayTimeScratch = 0xD862;    // play_time_scratch
constexpr uint16_t kIram = 0x0000;               // internal RAM segment

void clear_direction(Machine &m) { m.flags() = uint16_t(m.flags() & ~emu::DF); } // cld

// Parity of the low byte (x86 PF: 1 if the byte has an even number of set bits).
bool parity8(uint8_t value) {
    value = uint8_t(value ^ (value >> 4));
    value = uint8_t(value ^ (value >> 2));
    value = uint8_t(value ^ (value >> 1));
    return !(value & 1);
}

// SF/ZF/PF from an 8-bit ALU result (matches V30MZ::set_szp8).
void set_szp8_flags(Machine &m, uint8_t result) {
    uint16_t flags = uint16_t(m.flags() & ~(emu::SF | emu::ZF | emu::PF));
    if (result & 0x80)
        flags = uint16_t(flags | emu::SF);
    if (result == 0)
        flags = uint16_t(flags | emu::ZF);
    if (parity8(result))
        flags = uint16_t(flags | emu::PF);
    m.flags() = flags;
}

// SF/ZF/PF from a 16-bit ALU result (matches V30MZ::set_szp16: PF is still over the low BYTE only).
void set_szp16_flags(Machine &m, uint16_t result) {
    uint16_t flags = uint16_t(m.flags() & ~(emu::SF | emu::ZF | emu::PF));
    if (result & 0x8000)
        flags = uint16_t(flags | emu::SF);
    if (result == 0)
        flags = uint16_t(flags | emu::ZF);
    if (parity8(uint8_t(result)))
        flags = uint16_t(flags | emu::PF);
    m.flags() = flags;
}

// `cmp left, right` (8-bit): sets CF/OF/AF/SF/ZF/PF exactly like V30MZ::alu8's SUB/CMP case; doesn't store a
// result. Used by routines whose FLAGS output is read by a caller (clobbers_flags=false), so it has to be
// bit-exact rather than approximated.
void cmp8_flags(Machine &m, uint8_t left, uint8_t right) {
    unsigned difference = unsigned(left) - right;
    uint16_t flags = uint16_t(m.flags() & ~(emu::CF | emu::OF | emu::AF));
    if (difference > 0xFF)
        flags = uint16_t(flags | emu::CF);
    if ((left ^ right) & (left ^ uint8_t(difference)) & 0x80)
        flags = uint16_t(flags | emu::OF);
    if ((left ^ right ^ difference) & 0x10)
        flags = uint16_t(flags | emu::AF);
    m.flags() = flags;
    set_szp8_flags(m, uint8_t(difference));
}

// `cmp left, right` (16-bit): see cmp8_flags.
void cmp16_flags(Machine &m, uint16_t left, uint16_t right) {
    uint32_t difference = uint32_t(left) - right;
    uint16_t flags = uint16_t(m.flags() & ~(emu::CF | emu::OF | emu::AF));
    if (difference > 0xFFFF)
        flags = uint16_t(flags | emu::CF);
    if ((left ^ right) & (left ^ uint16_t(difference)) & 0x8000)
        flags = uint16_t(flags | emu::OF);
    if ((left ^ right ^ difference) & 0x10)
        flags = uint16_t(flags | emu::AF);
    m.flags() = flags;
    set_szp16_flags(m, uint16_t(difference));
}

// `test left, right` (8-bit AND without storing): CF/OF/AF always cleared, SF/ZF/PF from (left & right). See
// cmp8_flags.
void test8_flags(Machine &m, uint8_t left, uint8_t right) {
    m.flags() = uint16_t(m.flags() & ~(emu::CF | emu::OF | emu::AF));
    set_szp8_flags(m, uint8_t(left & right));
}

// Shared core of the "clamp N, then format it right-justified into (up to) 2 characters at DS:[di]"
// family (rom 0x347FEE / 0x348025): clamps n to clamp_max, converts it via number_to_digits_core into the
// scratch digit buffer at DS:0xD82B, then writes the tens digit (blanked with tile 0xD9 instead of shown,
// if it's a leading zero and blank_pad is set) and the ones digit -- each converted to its screen-tile code
// by +0xBB -- to DS:[di], followed by an 0xFF terminator. The final DI is always di_in + 3 and the final SI
// is always 0xD830 (one past the digit buffer), regardless of which path was taken -- reproduced via the
// returned pair so draw_play_time, which inlines this the same way, can pick them up too.
struct FormatResult {
    uint16_t di, si;
};
// Clamp a number, then write two decimal glyphs and FF; optionally blank the leading zero.
FormatResult format_2digit_core(Machine &m, uint16_t number, uint16_t clamp_max, bool blank_pad,
                                uint16_t di) {
    if (number >= clamp_max)
        number = clamp_max;
    uint16_t ds = m.ds();

    number_to_digits_core(m, number, ds, kDigitBuffer);

    uint16_t si = uint16_t(kDigitBuffer + 3); // the tens digit (n <= clamp_max fits in 2 digits)
    uint16_t count;
    if (blank_pad && m.rd8(ds, si) == 0) {
        m.wr8(ds, di, 0xD9);
        di = uint16_t(di + 1);
        si = uint16_t(si + 1);
        count = 1;
    } else {
        count = 2;
    }
    for (uint16_t i = 0; i < count; ++i) {
        m.wr8(ds, di, uint8_t(m.rd8(ds, si) + 0xBB));
        di = uint16_t(di + 1);
        si = uint16_t(si + 1);
    }
    m.wr8(ds, di, 0xFF);
    di = uint16_t(di + 1);
    return {di, si};
}

// Shared core of frames_to_minutes_seconds (rom 0x3480FC): DX:AX in as a 32-bit VBlank-frame count;
// clamps to 99:59 (0x63:0x3B) once the count reaches or passes 0x19BEB2Dh (DX left untouched on this path),
// otherwise divides by 4500 (0x1194, matching the original's `div bx` on the 32-bit value) for whole
// minutes -- leaving DX as that division's remainder, a ROM quirk (ordinary leftover junk from the 16-bit
// `div`) reproduced exactly -- then by 60 (`div bl`) for hours/minutes. Leaves AL = hours, AH = minutes;
// BX preserved (pushed/popped around the whole body in the original, never touched here anyway).
void frames_to_minutes_seconds_core(Machine &m) {
    uint16_t dx = m.dx();
    uint16_t ax = m.ax();
    uint8_t hours, minutes;
    if (dx > 0x19B || (dx == 0x19B && ax > 0xEB2C)) {
        hours = 0x63;
        minutes = 0x3B;
    } else {
        uint32_t frames = (uint32_t(dx) << 16) | ax;
        // 4500 VBlanks per displayed minute at 75 Hz; splitting that quotient by 60 gives hours:minutes.
        uint32_t total_minutes = frames / 0x1194;
        m.dx() = uint16_t(frames % 0x1194);
        hours = uint8_t(total_minutes / 60);
        minutes = uint8_t(total_minutes % 60);
    }
    m.ax() = uint16_t(hours | (uint16_t(minutes) << 8));
}
} // namespace

// AX in: a value clamped to <=99 (0x63), formatted right-justified into 2 characters at DS:[DI] -- a
// leading-zero tens digit is blanked (tile 0xD9) rather than shown -- followed by an 0xFF terminator (see
// format_2digit_core). Used both for stat-style displays (age/percentage-shaped fields in the 4 party
// records, e.g. ss:[0xD00C]/[0xD02B]/[0xD04A]/[0xD069] and friends) and, via draw_play_time, for a play
// time's minutes. Returns DI advanced past the terminator (di_in + 3), SI = 0xD830 (the shared digit
// scratch buffer, one past its last byte), CX = 0, AX = 0x00FF (AH cleared by the caller before the
// original's `lcall number_to_digits`, never touched again; AL = the terminator byte just written);
// BX/DX/segments preserved.
void format_stat_2digit_core(Machine &m) {
    FormatResult formatted = format_2digit_core(m, m.ax(), 0x63, true, m.di());
    m.di() = formatted.di;
    m.si() = formatted.si;
    m.cx() = 0;
    m.ax() = 0x00FF;
}
// rom: 0x347FEE (near)
// AX in: a value clamped to <=99 (0x63), formatted right-justified into 2 characters at DS:[DI] -- a
void format_stat_2digit(Machine &m) {
    format_stat_2digit_core(m);
    m.ret();
}

// Same shape as format_stat_2digit, but clamps to <=59 (0x3B) and always shows both digits (no
// leading-zero blanking) -- the "always two digits" minutes half of draw_play_time's hours:minutes line. Same
// final-register contract as format_stat_2digit.
void format_seconds_2digit_core(Machine &m) {
    FormatResult formatted = format_2digit_core(m, m.ax(), 0x3B, false, m.di());
    m.di() = formatted.di;
    m.si() = formatted.si;
    m.cx() = 0;
    m.ax() = 0x00FF;
}
// rom: 0x348025 (near)
// Same shape as format_stat_2digit, but clamps to <=59 (0x3B) and always shows both digits (no
void format_seconds_2digit(Machine &m) {
    format_seconds_2digit_core(m);
    m.ret();
}

// rom: 0x3480FC (near)
// See frames_to_minutes_seconds_core.
void frames_to_minutes_seconds(Machine &m) {
    frames_to_minutes_seconds_core(m);
    m.ret();
}

// DX:AX in: a 32-bit VBlank-frame count (a play-time total). Sets DS = SS (left changed, not restored --
// a ROM quirk, reproduced exactly) and writes wait_frames' offset (0x0000) to the play-time scratch word
// ss:[0xD862], written here but not read again on this path -- then converts the frame count to hours:minutes
// (inlines frames_to_minutes_seconds_core, reached via a real `call` from this near routine). Returns AX with
// AL = hours, AH = minutes (DX = frames_to_minutes_seconds_core's leftover, see its comment); BX preserved.
// The original's own body past this call is dead code on this path (an unconditional `jmp` skips over
// 0x23 undecoded bytes straight to a bare `ret`).
void play_time_to_minutes_seconds_core(Machine &m) {
    m.ds() = m.ss();
    m.wr16(m.ss(), kPlayTimeScratch, 0x0000); // offset of wait_frames (rom 0x340000)
    frames_to_minutes_seconds_core(m);
}
// rom: 0x3480CB (near)
// DX:AX in: a 32-bit VBlank-frame count (a play-time total). Sets DS = SS (left changed, not restored --
void play_time_to_minutes_seconds(Machine &m) {
    play_time_to_minutes_seconds_core(m);
    m.ret();
}

// Draws the "play time" HUD line. Reads the 32-bit VBlank-frame counter at ss:[0xD0DF]/ss:[0xD0E1],
// converts it to hours:minutes (inlines play_time_to_minutes_seconds's body -- DS ends up = SS mid-
// routine, and ss:[0xD862] gets wait_frames' offset written into it, same as that routine, reached here via
// a real `call` from this near routine), formats the hours (blank-padded, max 99) into scratch buffer
// 0xD807 and the minutes (always 2 digits, max 59) into 0xD80B (inlining format_stat_2digit/
// format_seconds_2digit the same way), then draws a label (D_34B981, DS = CS) and the two formatted buffers
// (DS = 0/kIram) with draw_text at fixed screen positions (rows/columns baked into the original). The
// entry DS is saved and restored at the end -- the only register the original explicitly preserves; every
// other register/segment ends up however the last inlined draw_text_core call left it.
void draw_play_time_core(Machine &m) {
    uint16_t entry_ds = m.ds();

    m.ds() = m.ss();
    m.ax() = m.rd16(m.ss(), kPlayTimeFrames);
    m.dx() = m.rd16(m.ss(), (kPlayTimeFrames + 0x2));
    m.wr16(m.ss(), kPlayTimeScratch,
           0x0000); // offset of wait_frames; part of play_time_to_minutes_seconds's body
    frames_to_minutes_seconds_core(m);
    uint8_t hours = m.al();
    uint8_t minutes = m.ah();

    format_2digit_core(m, hours, 0x63, true, kPlayMinutesText);
    FormatResult minute_format = format_2digit_core(m, minutes, 0x3B, false, kPlaySecondsText);
    m.di() = minute_format
                 .di; // format_seconds_2digit's own contract: DI = di_in + 3, left live for the caller here
    m.cx() = 0;       // both inlined format calls end CX = 0 (format_stat_2digit/format_seconds_2digit)

    {
        const TextPtr text_ptr = text(m, TextKey::draw_play_time_1);
        m.ds() = text_ptr.segment;
        m.ax() = 0;
        m.bx() = 0x0D13;
        m.si() = text_ptr.offset; // D_34B981 (a fixed label string, "play time"-shaped)
        draw_text_core(m);
        text_done(m, TextKey::draw_play_time_1);
    }

    m.ds() = kIram;
    m.ax() = 0;
    m.bx() = 0x0D16;
    m.si() = kPlayMinutesText;
    draw_text_core(m);

    m.ds() = kIram;
    m.ax() = 0;
    m.bx() = 0x0D19;
    m.si() = kPlaySecondsText;
    draw_text_core(m);

    m.ds() = entry_ds;
}
// Shared body of ROM 0x341F5A
// Draws the "play time" HUD line. Reads the 32-bit VBlank-frame counter at ss:[0xD0DF]/ss:[0xD0E1],

// rom: 0x341F5A (near)
void draw_play_time(Machine &m) {
    draw_play_time_core(m);
    m.ret();
}

// Copies CX bytes DS:SI -> ES:DI (a manual byte-at-a-time loop, not `rep movsb`; CX = 0 means 65536, the
// usual dec/jne idiom), then appends an 0xFF terminator right after the copied run (no further advance).
// SI/DI each end advanced by however many bytes were copied; CX ends 0; AH is left untouched from entry,
// AL ends 0xFF (the terminator byte); BX/DX/segments preserved.
void copy_bytes_terminated_core(Machine &m) {
    uint16_t cx = m.cx();
    uint16_t si = m.si();
    uint16_t di = m.di();
    uint16_t ds = m.ds(), es = m.es();
    do {
        m.wr8(es, di, m.rd8(ds, si));
        si = uint16_t(si + 1);
        di = uint16_t(di + 1);
        --cx;
    } while (cx != 0);
    m.wr8(es, di, 0xFF);
    m.si() = si;
    m.di() = di;
    m.cx() = cx;
    m.set_al(0xFF);
}
// rom: 0x347F06 (near)
// Copies CX bytes DS:SI -> ES:DI (a manual byte-at-a-time loop, not `rep movsb`; CX = 0 means 65536, the
void copy_bytes_terminated(Machine &m) {
    copy_bytes_terminated_core(m);
    m.ret();
}

// Draws up to CX characters (0xFF-terminated, whichever comes first) from DS:SI into the screen map at
// 0x1000 (ES forced to 0/kIram, restored after -- reproduced as a direct kIram write, same convention as
// draw_text), row AL / column BX (packed like draw_text/screen_map_offset), attribute 0x1F00 (a sibling
// right after it in ROM, L_347D3F, is identical but attribute 0x1E00; handled separately). DF is
// cleared; DI is restored to its entry value (pushed/popped around the whole body, unlike CX); CX ends at
// however many characters were left unused (0 if the string's own 0xFF terminator was hit first, since that
// path breaks out without decrementing CX further; CX = 0 in also wraps to 65536 like the copy loops
// elsewhere in this bank, reproduced by the natural uint16_t decrement below); BX ends with BL bumped by
// the count of characters actually written (8-bit, no carry into BH, same as draw_text); AX ends with
// AH = 0x1F and AL = the last character byte processed (whether that was the 0xFF terminator or, if CX ran
// out first, the last ordinary character).
void draw_text_n_white_core(Machine &m) {
    m.watch_text("draw_text_n_white");
    clear_direction(m);
    uint16_t entry_bx = m.bx();
    uint16_t di = screen_map_offset(m.al(), entry_bx);
    if (const std::u32string *text = proportional_text(m)) { // a loaded language's string: our font
        const int cells = draw_table_text(m, di, 0x1F00, *text, m.cx()); // at most CX of the original's cells
        skip_text(m);
        m.cx() = 0;
        m.ax() = 0x1FFF;
        m.bx() = uint16_t((entry_bx & 0xFF00) | uint8_t(uint8_t(entry_bx) + cells));
        return;
    }
    uint16_t si = m.si();
    uint8_t bl = uint8_t(entry_bx);
    uint16_t cx = m.cx();
    uint16_t ax = 0x1F00;
    for (;;) {
        uint8_t ch = m.rd8(m.ds(), si);
        ax = uint16_t(0x1F00 | ch);
        if (ch == 0xFF)
            break;
        ++si;
        ++bl;
        m.wr16(kIram, di, ax);
        di = uint16_t(di + 2);
        --cx;
        if (cx == 0)
            break;
    }
    m.si() = si;
    m.cx() = cx;
    m.ax() = ax;
    m.bx() = uint16_t((entry_bx & 0xFF00) | bl);
}
// Shared body of ROM 0x347D06
// Draws up to CX characters (0xFF-terminated, whichever comes first) from DS:SI into the screen map at

// rom: 0x347D06 (near)
void draw_text_n_white(Machine &m) {
    draw_text_n_white_core(m);
    m.ret();
}

// Shared body of ROM 0x341A80
// Looks up a 16-bit OAM position from DI's table (the caller-supplied base, e.g. the 7-entry D_341A71 used
// by the race-select screen at L_3419D1) indexed by the selection value at ss:[0xD841] (0-6); stashes it
// at ss:[0x014A] (scratch, not otherwise read on this path) and draws OAM slot 0 (a 16x16 cursor sprite,
// tile+attribute 0x3FFB) there via set_sprite_16x16 (inlined -- a real `lcall` from this near routine, same
// reasoning as draw_play_time). Ends with AX = 0, DX = 0x3FFB, BX = the looked-up position (set_sprite_
// 16x16 itself doesn't touch ax/bx/dx/di); DI is never written here either, so it stays whatever the
// caller's table-base value was.
void draw_race_select_cursor_core(Machine &m) {
    uint16_t index = uint16_t(uint16_t(m.rd8(m.ss(), kMenuSelection)) << 1);
    uint16_t cursor_position = m.rd16(m.cs(), uint16_t(index + m.di()));
    m.wr16(m.ss(), kMenuCursorPosition, cursor_position);
    m.ax() = 0;
    m.dx() = 0x3FFB;
    m.bx() = cursor_position;
    set_sprite_16x16_core(m);
    return;
}

// rom: 0x341A80 (near)
void draw_race_select_cursor(Machine &m) {
    draw_race_select_cursor_core(m);
    m.ret();
}

// rom: 0x3461C1 (near)
// Same shape as draw_race_select_cursor: selection index ss:[0xD842] (the class-select screen), OAM slot
// 0x18 instead of 0.
void draw_class_select_cursor_core(Machine &m) {
    uint16_t index = uint16_t(uint16_t(m.rd8(m.ss(), kSaveSlotSelection)) << 1);
    uint16_t cursor_position = m.rd16(m.cs(), uint16_t(index + m.di()));
    m.wr16(m.ss(), kMenuCursorPosition, cursor_position);
    m.ax() = 0x18;
    m.dx() = 0x3FFB;
    m.bx() = cursor_position;
    set_sprite_16x16_core(m);
    return;
}
void draw_class_select_cursor(Machine &m) {
    draw_class_select_cursor_core(m);
    m.ret();
}

// Shared body of ROM 0x3461E0
// Same shape again: selection index ss:[0xD843] (the gender-select screen), OAM slot 0x1C.
void draw_gender_select_cursor_core(Machine &m) {
    uint16_t index = uint16_t(uint16_t(m.rd8(m.ss(), kSaveChoice)) << 1);
    uint16_t cursor_position = m.rd16(m.cs(), uint16_t(index + m.di()));
    m.wr16(m.ss(), kMenuCursorPosition, cursor_position);
    m.ax() = 0x1C;
    m.dx() = 0x3FFB;
    m.bx() = cursor_position;
    set_sprite_16x16_core(m);
    return;
}

// rom: 0x3461E0 (near)
void draw_gender_select_cursor(Machine &m) {
    draw_gender_select_cursor_core(m);
    m.ret();
}

// Shared body of ROM 0x34AEB6
// Fixed-table twin of draw_race_select_cursor: the same ss:[0xD841] selection index, but the 7-entry
// position table (D_34AED2) is baked in rather than taken from DI, since this is reused on a review screen
// that always wants that one table (the screen itself, around L_34AD.., is handled separately).
// Doesn't write ss:[0x014A] (a ROM difference from draw_race_select_cursor, reproduced exactly).
void draw_race_icon_cursor_core(Machine &m) {
    uint16_t index = uint16_t(uint16_t(m.rd8(m.ss(), kMenuSelection)) << 1);
    uint16_t cursor_position = m.rd16(m.cs(), uint16_t(index + 0xAED2));
    m.ax() = 0;
    m.dx() = 0x3FFB;
    m.bx() = cursor_position;
    set_sprite_16x16_core(m);
    return;
}

// rom: 0x34AEB6 (near)
void draw_race_icon_cursor(Machine &m) {
    draw_race_icon_cursor_core(m);
    m.ret();
}

// Shared body of ROM 0x34B566
// Combines two independent fixed byte tables -- D_34B58C indexed by the class-select value ss:[0xD842],
// D_34B596 indexed by the gender-select value ss:[0xD843] -- into one 16-bit OAM position (AL from the
// first table, AH from the second) for the same review screen's cursor sprite (OAM slot 0, tile+attribute
// 0x3FFB).
void draw_class_gender_icon_cursor_core(Machine &m) {
    uint8_t class_idx = m.rd8(m.ss(), kSaveSlotSelection);
    uint8_t gender_idx = m.rd8(m.ss(), kSaveChoice);
    uint8_t x = m.rd8(m.cs(), uint16_t(0xB58C + class_idx));
    uint8_t y = m.rd8(m.cs(), uint16_t(0xB596 + gender_idx));
    m.ax() = 0;
    m.dx() = 0x3FFB;
    m.bx() = uint16_t(x | (uint16_t(y) << 8));
    set_sprite_16x16_core(m);
    return;
}

// rom: 0x34B566 (near)
void draw_class_gender_icon_cursor(Machine &m) {
    draw_class_gender_icon_cursor_core(m);
    m.ret();
}

namespace {
// Shared core of the pushaw-wrapped "index a fixed table by a scratch selection byte, stash the looked-up
// position at ss:0xD6FC, draw it as OAM slot 0" cursor family (0x34453B/0x344F15/0x3428E4; several
// other siblings, e.g. L_342941, share the same shape). The original loads SI with the table's base
// address *before* the `pushaw` that brackets everything else, so SI (never touched again until the
// matching `popaw`) ends up holding that table base, not the routine's true entry SI. The inlined
// set_sprite_16x16 call is inside the same pushaw/push-ds-es-ss bracket, so only the memory writes
// (mem_seg:0xD6FC and the 8 OAM bytes) besides SI are externally visible: AX and BX end up holding the
// looked-up position `pos` (it's moved into BX before the pushaw, so popaw's own AX/BX restore lands back
// on that same value, not the routine's true entry AX/BX), while CX/DX/DI and every segment register end
// back at their entry values.
void draw_indexed_cursor_core(Machine &m, uint16_t mem_seg, uint16_t table_base, uint16_t cursor_position) {
    uint16_t entry_cx = m.cx(), entry_dx = m.dx(), entry_di = m.di();
    m.wr16(mem_seg, kMenuCursorAnchor, cursor_position);
    m.ax() = 0;
    m.dx() = 0x3FFB;
    m.bx() = cursor_position;
    set_sprite_16x16_core(m);
    m.ax() = cursor_position;
    m.bx() = cursor_position;
    m.cx() = entry_cx;
    m.dx() = entry_dx;
    m.si() = table_base;
    m.di() = entry_di;
}
} // namespace

// rom: 0x34453B (near)
// Looks up a 16-bit OAM position from the fixed 4-entry table D_344564, indexed by a scratch selection
// byte at ss:[0xD853] (0-3; not race/class/gender_select_index -- those live at 0xD841-0xD843 -- so this is
// some other piece of character-creation UI state, e.g. a shared "current step" or "sub-icon" marker;
// called from both the race-select and gender-select screen setup blocks at rom 0x342D43/0x343BA7, so
// whatever it is, it's common to more than one of those screens). See draw_indexed_cursor_core.
void draw_creation_step_cursor_core(Machine &m) {
    uint16_t index = uint16_t(uint16_t(m.rd8(m.ss(), kCreationRace)) << 1);
    uint16_t cursor_position = m.rd16(m.cs(), uint16_t(index + 0x4564));
    draw_indexed_cursor_core(m, m.ss(), 0x4564, cursor_position);
    return;
}
void draw_creation_step_cursor(Machine &m) {
    draw_creation_step_cursor_core(m);
    m.ret();
}

// Shared body of ROM 0x344F15
// Same shape as draw_creation_step_cursor, but the table is D_344F3E (4 entries) indexed by
// class_select_index (ss:[0xD842]) -- the pushaw-wrapped sibling of draw_class_select_cursor (0x3461C1,
// which takes its table via DI and doesn't wrap pushaw), used by the class-select screen setup block at rom
// 0x344AB1. See draw_indexed_cursor_core.
void draw_class_menu_cursor_core(Machine &m) {
    uint16_t index = uint16_t(uint16_t(m.rd8(m.ss(), kSaveSlotSelection)) << 1);
    uint16_t cursor_position = m.rd16(m.cs(), uint16_t(index + 0x4F3E));
    draw_indexed_cursor_core(m, m.ss(), 0x4F3E, cursor_position);
    return;
}

// rom: 0x344F15 (near)
void draw_class_menu_cursor(Machine &m) {
    draw_class_menu_cursor_core(m);
    m.ret();
}

// Shared body of ROM 0x3428E4
// Same family again, but picks between two tables depending on a mode byte at [0xD852] (DS-relative, no
// segment override in the original -- every call site has DS=0/kIram here, same as SS): if [0xD852]==1,
// indexes the 9-entry D_34292F by gender_select_index ([0xD843]); otherwise indexes the 10-entry D_34291B
// by class_select_index ([0xD842]). See draw_indexed_cursor_core.
void draw_class_or_gender_cursor_core(Machine &m) {
    uint16_t ds = m.ds();
    uint16_t index, table;
    if (m.rd8(ds, kCreationLeader) == 1) {
        index = uint16_t(uint16_t(m.rd8(ds, kSaveChoice)) << 1);
        table = 0x292F;
    } else {
        index = uint16_t(uint16_t(m.rd8(ds, kSaveSlotSelection)) << 1);
        table = 0x291B;
    }
    uint16_t cursor_position = m.rd16(m.cs(), uint16_t(index + table));
    draw_indexed_cursor_core(m, ds, table, cursor_position);
    return;
}

// rom: 0x3428E4 (near)
void draw_class_or_gender_cursor(Machine &m) {
    draw_class_or_gender_cursor_core(m);
    m.ret();
}

// rom: 0x347204 (near)
// Same shape as draw_class_select_cursor (0x3461C1) -- selection index class_select_index (ss:[0xD842]),
// caller-supplied position table via DI, writes ss:[0x014A] -- but OAM slot 0 instead of 0x18: the field
// (in-game) pause menu's cursor over its own label list (see draw_field_menu_label, which highlights the
// matching text using the same index).
void draw_field_menu_cursor_core(Machine &m) {
    uint16_t index = uint16_t(uint16_t(m.rd8(m.ss(), kSaveSlotSelection)) << 1);
    uint16_t cursor_position = m.rd16(m.cs(), uint16_t(index + m.di()));
    m.wr16(m.ss(), kMenuCursorPosition, cursor_position);
    m.ax() = 0;
    m.dx() = 0x3FFB;
    m.bx() = cursor_position;
    set_sprite_16x16_core(m);
    return;
}
void draw_field_menu_cursor(Machine &m) {
    draw_field_menu_cursor_core(m);
    m.ret();
}

// Shared body of ROM 0x347261
// Draws the field pause menu's currently-selected label (one of 6 CS-relative strings at D_34BB0F and its
// siblings, indexed via the 6-entry word table D_347281) at a fixed row/column (AL=1, BX=0x1002), selection
// index class_select_index (ss:[0xD842]) -- the same index draw_field_menu_cursor uses. DS is saved and
// restored (an explicit push/pop bracketing the whole body); every other register ends up however
// draw_text_core (inlined via a real `lcall` from this near routine, same reasoning as draw_play_time) left
// it: AX = 0x1Fxx (AH=0x1F, AL = the label's terminator byte), BX = entry BX with BL bumped by the label's
// length, SI = just before the label's own terminator; CX/DX untouched.
void draw_field_menu_label_core(Machine &m) {
    uint16_t entry_ds = m.ds();
    const uint8_t index =
        m.rd8(m.ss(), kSaveSlotSelection); // D_347281[index]: the Config tab's help lines (6)
    const TextKey key = TextKey(uint16_t(TextKey::field_menu_label_0) + std::min<uint8_t>(index, 5));
    const TextPtr text_ptr =
        index < 6 ? text(m, key) : TextPtr{m.cs(), m.rd16(m.cs(), uint16_t(0x7281 + index * 2))};
    m.ds() = text_ptr.segment;
    m.ax() = 1;
    m.bx() = 0x1002;
    m.si() = text_ptr.offset;
    draw_text_core(m);
    text_done(m, key);
    m.ds() = entry_ds;
    return;
}

// rom: 0x347261 (near)
void draw_field_menu_label(Machine &m) {
    draw_field_menu_label_core(m);
    m.ret();
}

// Shared body of ROM 0x3454F0
// Same shape as draw_class_select_cursor (0x3461C1) -- selection index class_select_index (ss:[0xD842]),
// writes ss:[0x014A] like draw_race_select_cursor -- but the position table is the smaller, fixed 2-entry
// D_345511 rather than a caller-supplied one, for a review-style screen that always wants that table.
void draw_class_review_cursor_core(Machine &m) {
    uint16_t index = uint16_t(uint16_t(m.rd8(m.ss(), kSaveSlotSelection)) << 1);
    uint16_t cursor_position = m.rd16(m.cs(), uint16_t(index + 0x5511));
    m.wr16(m.ss(), kMenuCursorPosition, cursor_position);
    m.ax() = 0;
    m.dx() = 0x3FFB;
    m.bx() = cursor_position;
    set_sprite_16x16_core(m);
    return;
}

// rom: 0x3454F0 (near)
void draw_class_review_cursor(Machine &m) {
    draw_class_review_cursor_core(m);
    m.ret();
}

// Clamps AX to <=9, converts it via number_to_digits_core into the shared scratch digit buffer (DS:0xD82B),
// then writes just the ones digit (screen-tile code = digit + 0xBB) to DS:[DI], followed by an 0xFF
// terminator -- the single-digit member of the format_stat_2digit/format_seconds_2digit family, with no
// blank-padding since there's only ever one digit. DI ends advanced past the terminator (di_in + 2); SI
// ends at 0xD82B (the digit-buffer pointer passed to number_to_digits, never advanced -- unlike its 2/3-
// digit siblings, this one addresses the ones digit directly rather than walking SI up to it); AX = 0x00FF
// (AH stays 0 the whole way, since the clamp keeps the value to one digit; AL = the terminator byte just
// written); BX/CX/DX preserved.
void format_stat_1digit_core(Machine &m) {
    uint16_t number = m.ax();
    if (number >= 9)
        number = 9;
    uint16_t ds = m.ds();

    number_to_digits_core(m, number, ds, kDigitBuffer);
    uint8_t digit = m.rd8(ds, uint16_t(kDigitBuffer + 4));
    uint16_t di = m.di();
    m.wr8(ds, di, uint8_t(digit + 0xBB));
    di = uint16_t(di + 1);
    m.wr8(ds, di, 0xFF);
    di = uint16_t(di + 1);
    m.di() = di;
    m.si() = kDigitBuffer;
    m.ax() = 0x00FF;
}
// rom: 0x347F84 (near)
// Clamps AX to <=9, converts it via number_to_digits_core into the shared scratch digit buffer (DS:0xD82B),
void format_stat_1digit(Machine &m) {
    format_stat_1digit_core(m);
    m.ret();
}

// AL in: a party member's race-flags byte (0xFF means "empty slot", the same sentinel clear_game_state
// writes to the four party markers at 0xD000/0xD01F/0xD03E/0xD05D) -- otherwise treated as a one-hot bitmask
// over 8 races. Finds the lowest set bit (0-7, via `shr al,1`/`jb` per bit, BX counting how many zero bits
// were shifted out) and copies the matching fixed 5-character abbreviation (from one of 8 CS-relative
// strings via the word table D_347F74, indexed by BX*2) to DS:[DI], with no terminator (the caller already
// knows it's exactly 5 characters, e.g. as one field of a fixed-width record). AL==0xFF does nothing and is
// the only path that preserves BX/CX/SI at their entry values. Otherwise CX (a scratch loop counter, 8 for
// the bit scan or 5 for the copy) always ends 0. A wholly-zero bitmask shifts through all 8 bits without
// finding one, leaving BX=8, AL=0, SI untouched (never set) and nothing drawn. When a bit is found: BX ends
// bit*2 (the table byte offset used), SI/DI end advanced past the copied 5 characters, AL ends the last
// character copied (AH is never touched by either path). DS preserved throughout.
void write_race_abbreviation_core(Machine &m) {
    uint8_t race_flags = m.al();
    if (race_flags == 0xFF) {

        return;
    }
    uint8_t al = race_flags;
    uint16_t bx = 0;
    bool found = false;
    for (int i = 0; i < 8; ++i) {
        bool carry = al & 1;
        al = uint8_t(al >> 1);
        if (carry) {
            found = true;
            break;
        }
        bx = uint16_t(bx + 1);
    }
    if (!found) {
        m.set_al(al);
        m.bx() = bx;
        m.cx() = 0; // the bit-scan loop's own `loop` decrement runs it out to 0 either way

        return;
    }
    bx = uint16_t(bx << 1);
    uint16_t si = m.rd16(m.cs(), uint16_t(0x7F74 + bx));
    uint16_t di = m.di();
    uint16_t ds = m.ds();
    uint8_t ch = 0;
    for (int i = 0; i < 5; ++i) {
        ch = m.rd8(m.cs(), si);
        m.wr8(ds, di, ch);
        si = uint16_t(si + 1);
        di = uint16_t(di + 1);
    }
    m.set_al(ch);
    m.bx() = bx;
    m.cx() = 0;
    m.si() = si;
    m.di() = di;
}
// rom: 0x347F4D (near)
// AL in: a party member's race-flags byte (0xFF means "empty slot", the same sentinel clear_game_state
void write_race_abbreviation(Machine &m) {
    write_race_abbreviation_core(m);
    m.ret();
}

// Converts a decimal value in DX (clamped to 9999) into a packed-BCD word (one hex nibble per decimal
// digit, thousands..ones) via repeated subtraction against a fixed place-value table (D_347EF6: 1000/100/
// 10/1), accumulating the BCD contribution (D_347EFE: 0x1000/0x100/0x10/0x1) for each successful subtraction
// -- feeds draw_bcd_number and other BCD-digit renderers elsewhere in this bank. AX/BX/CX are pushed and
// popped around the whole body (restored to entry values, regardless of what the digit-extraction loop did
// to them internally); DX ends holding the packed-BCD result (or 0x9999 directly if the input was already
// >=10000, a shortcut since that's what full conversion of 9999 would yield anyway).
void decimal_to_bcd_clamped_core(Machine &m) {
    uint16_t entry_ax = m.ax(), entry_bx = m.bx(), entry_cx = m.cx();
    uint16_t value = m.dx();
    static constexpr uint16_t kPlaces[4] = {1000, 100, 10, 1};
    static constexpr uint16_t kBcdPlaces[4] = {0x1000, 0x100, 0x10, 0x1};
    uint16_t result;
    if (value >= 10000) {
        result = 0x9999;
    } else {
        result = 0;
        for (int i = 0; i < 4; ++i) {
            while (value >= kPlaces[i]) {
                value = uint16_t(value - kPlaces[i]);
                result = uint16_t(result + kBcdPlaces[i]);
            }
        }
    }
    m.ax() = entry_ax;
    m.bx() = entry_bx;
    m.cx() = entry_cx;
    m.dx() = result;
}
// rom: 0x347EBE (near)
// Converts a decimal value in DX (clamped to 9999) into a packed-BCD word (one hex nibble per decimal
void decimal_to_bcd_clamped(Machine &m) {
    decimal_to_bcd_clamped_core(m);
    m.ret();
}

// Draws a packed-BCD number (DX, as decimal_to_bcd_clamped produces) right-justified into up to CX digit
// positions in the screen map, ending at row AL / column BX (packed like draw_text -- the caller's column
// is where the ONES digit lands; the number grows leftward from there). If DX needs more significant
// nibbles than CX allows, it's saturated to 0x9999 first (so an over-wide value always displays as all 9s,
// not truncated/wrapped). Leading zero digits within the CX-wide field are blanked with tile 0x1FD9 (a
// plain space/blank, attribute 0x1F included) rather than drawn as "0"; at least one digit (the ones place)
// is always drawn even if the whole value is zero. SI/DI/BP/DS/ES are pushed and popped around the whole
// body (restored to entry values); AX ends 0x1FD9 if any padding was drawn, else the last digit's tile word
// (0x1F00 | (digit + 0xBB)); BX ends the raw significant-digit count from the width-limited scan (0 if the
// whole field was blank, even though at least 1 digit was still drawn -- a ROM quirk, reproduced exactly);
// CX ends 0; DX ends shifted right by 4 bits per digit actually drawn.
void draw_bcd_number_core(Machine &m) {
    uint16_t entry_si = m.si(), entry_di = m.di(), entry_bp = m.bp();
    uint16_t entry_ds = m.ds(), entry_es = m.es();

    uint16_t di = screen_map_offset(m.al(), m.bx());
    uint16_t dx = m.dx();
    uint16_t width = m.cx();

    uint16_t probe = dx;
    uint16_t lead_significant = 4;
    while (lead_significant != 0 && !(probe & 0xF000)) {
        probe = uint16_t(probe << 4);
        --lead_significant;
    }
    if (lead_significant > width)
        dx = 0x9999;

    // `shl ax, cl`: the V30MZ (like the 80186 it's compatible with) masks the shift count to 5 bits, so only
    // CL (the low byte of the 16-bit (4-width)*4) matters, and a masked count of 16-31 shifts all 16 bits
    // out (0), same as a plain 16-bit register would for any count >=16.
    uint8_t shift_amt = uint8_t(uint8_t((4 - width) * 4) & 0x1F);
    uint16_t shifted = shift_amt >= 16 ? uint16_t(0) : uint16_t(dx << shift_amt);
    uint16_t sig = width;
    for (uint16_t i = 0; i < width; ++i) {
        shifted = uint16_t((shifted << 4) | (shifted >> 12));
        if (shifted & 0xF)
            break;
        --sig;
    }
    uint16_t draw_count = sig == 0 ? 1 : sig;

    uint16_t es = kIram;
    uint16_t ax_out = 0x1F00;
    for (uint16_t i = 0; i < draw_count; ++i) {
        ax_out = uint16_t(0x1F00 | uint8_t(uint8_t(dx & 0xF) + 0xBB));
        m.wr16(es, di, ax_out);
        di = uint16_t(di - 2);
        dx = uint16_t(dx >> 4);
    }
    uint16_t pad = uint16_t(width - draw_count);
    if (pad != 0) {
        ax_out = 0x1FD9;
        for (uint16_t i = 0; i < pad; ++i) {
            m.wr16(es, di, ax_out);
            di = uint16_t(di - 2);
        }
    }

    m.ax() = ax_out;
    m.bx() = sig;
    m.cx() = 0;
    m.dx() = dx;
    m.si() = entry_si;
    m.di() = entry_di;
    m.bp() = entry_bp;
    m.ds() = entry_ds;
    m.es() = entry_es;
}
// rom: 0x347D78 (near)
// Draws a packed-BCD number (DX, as decimal_to_bcd_clamped produces) right-justified into up to CX digit
void draw_bcd_number(Machine &m) {
    draw_bcd_number_core(m);
    m.ret();
}

namespace {
// Shared core of copy_and_verify_save_block (0x3469E1): copies CX bytes DS:SI -> ES:DI while accumulating a
// 16-bit checksum (the sum of each zero-extended byte copied), then verifies it against the word right
// after the source data (DS:[SI], SI having advanced past the CX copied bytes) and two fixed "magic" bytes
// at SS:[entry_DI + 0x435] / SS:[entry_DI + 0x436] (0x1B, 0xE4). Sets FLAGS exactly like the original's CMP
// chain (the caller applies the final CF itself, since the original does that with an explicit `clc`/`stc`
// that doesn't touch the other flags); returns true if verified. AX ends (0, last byte copied); SI ends
// advanced past the checksum word; DI/CX end restored to their entry values; DX ends holding the checksum
// (not preserved -- a genuine output, unlike DI/CX). Internal linkage (both callers live in this file):
// load_save_slot (0x3469A4) reaches this via a real `call` from another near routine, so it inlines the
// core the same way other near routines in this file inline their callees.
bool copy_and_verify_save_block_core(Machine &m) {
    uint16_t entry_di = m.di();
    uint16_t entry_cx = m.cx();
    uint16_t si = m.si();
    uint16_t ds = m.ds(), es = m.es(), ss = m.ss();
    uint16_t di = entry_di;
    uint16_t cx = entry_cx;
    uint16_t checksum = 0;
    uint8_t last_byte = 0;
    do {
        last_byte = m.rd8(ds, si);
        m.wr8(es, di, last_byte);
        checksum = uint16_t(checksum + last_byte);
        si = uint16_t(si + 1);
        di = uint16_t(di + 1);
        --cx;
    } while (cx != 0);

    uint16_t stored = m.rd16(ds, si);
    cmp16_flags(m, checksum, stored);
    bool ok = checksum == stored;
    if (ok) {
        uint8_t magic1 = m.rd8(ss, uint16_t(entry_di + 0x435));
        cmp8_flags(m, magic1, 0x1B);
        ok = magic1 == 0x1B;
        if (ok) {
            uint8_t magic2 = m.rd8(ss, uint16_t(entry_di + 0x436));
            cmp8_flags(m, magic2, 0xE4);
            ok = magic2 == 0xE4;
        }
    }

    m.ax() = last_byte;
    m.si() = si;
    m.di() = entry_di;
    m.cx() = entry_cx;
    m.dx() = checksum;
    return ok;
}
} // namespace

// rom: 0x3469E1 (near)
// See copy_and_verify_save_block_core. CF is the routine's real result (clear = verified OK, set =
// mismatch); its callers (load_save_slot and a sibling) branch on it immediately with
// `jae`/`jne`, so clobbers_flags=false: every flag has to match, not just CF.
void copy_and_verify_save_block(Machine &m) {
    bool ok = copy_and_verify_save_block_core(m);
    m.flags() = uint16_t(ok ? (m.flags() & ~emu::CF) : (m.flags() | emu::CF));
    m.ret();
}

// Loads and verifies one SRAM save slot: AL = slot index, DI = destination (e.g. ss:0xD8FE, set by the
// caller) for copy_and_verify_save_block_core's copy. Reads from the SRAM segment (0x1000), slot stride
// 0x43B (0x439 data bytes + a 2-byte checksum) starting at offset 0x3301; if the primary copy fails
// verification, retries once from a backup copy 0xCB1 bytes further into the same segment (the classic
// "keep two copies" save-corruption safety net). Every register is preserved (pushaw + ds/es/ss bracket the
// whole body) except FLAGS: CF is the real result (clear = a copy verified, either the primary or the
// backup; set = both failed), read by callers immediately after the call, so clobbers_flags=false here too.
void load_save_slot_core(Machine &m) {
    uint16_t entry_ax = m.ax(), entry_bx = m.bx(), entry_cx = m.cx(), entry_dx = m.dx();
    uint16_t entry_si = m.si(), entry_di = m.di(), entry_bp = m.bp();
    uint16_t entry_ds = m.ds(), entry_es = m.es(), entry_ss = m.ss();

    uint8_t index = m.al();
    // The save store (save_slots.h): every slot when one is attached, else slots 3-8 (Save Slots 9); never
    // SRAM.
    if (SaveSlots *slots = save_store(m); slots && slots->holds(index)) {
        std::vector<uint8_t> block;
        const bool ok = slots->load(index, &block);
        if (ok)
            for (size_t i = 0; i < SaveSlots::kPayload; ++i)
                m.wr8(entry_ss, uint16_t(entry_di + i), block[i]);
        m.flags() = uint16_t(ok ? (m.flags() & ~emu::CF) : (m.flags() | emu::CF));
        return; // registers as on entry
    }
    m.ds() = 0x1000; // SRAM save-data segment
    m.es() = entry_ss;
    uint16_t si = uint16_t(0x3301 + index * 0x43B);
    m.si() = si;
    m.cx() = 0x0439;
    m.di() = entry_di;
    bool ok = copy_and_verify_save_block_core(m);
    if (!ok) {
        m.si() = uint16_t(si + 0xCB1);
        m.cx() = 0x0439;
        m.di() = entry_di;
        ok = copy_and_verify_save_block_core(m);
    }
    m.flags() = uint16_t(ok ? (m.flags() & ~emu::CF) : (m.flags() | emu::CF));

    m.ax() = entry_ax;
    m.bx() = entry_bx;
    m.cx() = entry_cx;
    m.dx() = entry_dx;
    m.si() = entry_si;
    m.di() = entry_di;
    m.bp() = entry_bp;
    m.ds() = entry_ds;
    m.es() = entry_es;
    m.ss() = entry_ss;
}
// Shared body of ROM 0x3469A4
// Loads and verifies one SRAM save slot: AL = slot index, DI = destination (e.g. ss:0xD8FE, set by the

// rom: 0x3469A4 (near)
void load_save_slot(Machine &m) {
    load_save_slot_core(m);
    m.ret();
}

namespace {
// Shared core of test_event_flag (0x345F29): tests story/event flag AL (0-0x95=149, a bit index into the
// 19-byte flag block at SS:[0xD0EC], zeroed by clear_game_state) without modifying it, via the same
// byte/bit split set_event_flag (0x345EF9) uses to set one: byte SS:[0xD0EC + AL>>3],
// bit mask D_345F51[AL&7] (0x80>>bit, the same 8-entry table set_event_flag's own D_345F21 duplicates).
// AL>0x95 is out of range and does a plain CMP AL,0x95 instead of the TEST, leaving FLAGS as that failed
// range check rather than a bit query -- reproduced exactly since it's externally visible (see
// test_event_flag). Internal linkage (both callers live in this file): draw_flag_gated_icon (0x345DB0)
// reaches this via a real `call` from another near routine, so it inlines the core the same way other near
// routines in this file inline their callees. Returns true if the bit was set.
bool test_event_flag_core(Machine &m, uint8_t flag_id) {
    if (flag_id > 0x95) {
        // Out of range: the original does nothing further either way, but leaves FLAGS as this CMP set
        // them -- and since flag_id != 0x95 always holds here, ZF always ends 0, so a caller reading ZF the
        // way draw_flag_gated_icon does always sees "bit set" for an out-of-range id (not a real bit test,
        // just how the leftover flags happen to read).
        cmp8_flags(m, flag_id, 0x95);
    } else {
        uint16_t ss = m.ss();
        uint8_t byte_val = m.rd8(ss, uint16_t(kEventFlags + (flag_id >> 3)));
        uint8_t mask = uint8_t(0x80 >> (flag_id & 7));
        test8_flags(m, byte_val, mask);
    }
    return !(m.flags() & emu::ZF); // matches a caller's `je`/`jne` on this routine's FLAGS output
}
} // namespace

// rom: 0x345F29 (near)
// See test_event_flag_core. Every register is preserved (pushaw + ds/es/ss bracket the whole body, both the
// in-range and out-of-range paths leave it untouched); FLAGS is the real output (clobbers_flags=false) --
// callers branch on ZF right after the call (a set bit clears ZF, the usual TEST convention), so every flag
// has to match whichever comparison (the TEST, or the out-of-range CMP) the original actually executed, not
// just ZF.
void test_event_flag_call_core(Machine &m) { test_event_flag_core(m, m.al()); }
void test_event_flag(Machine &m) {
    test_event_flag_call_core(m);
    m.ret();
}

// If event flag AL is set (test_event_flag_core, inlined the same way as this file's other near-to-near
// calls), draws a fixed 2x2 icon tile block (row 1, caller's column BX, one of 6 icons in the 0x30-byte
// table D_345DD5 selected by DX*8 -- best guess: a per-race/gender "unlocked emblem" marker in the
// character-creation/review flow, given the callers feed DX from ss:[0xD844] and AL from a flag id computed
// out of ss:[0xD852]/[0xD844]/[0xD843]) via draw_tilemap_core (inlined via a real `lcall` from this near
// routine, same reasoning as draw_play_time). AX/BX/CX/DX/DS are pushed and popped around the whole body
// (restored to entry values); nothing else is touched besides the screen-map memory when the flag is set.
void draw_flag_gated_icon_core(Machine &m) {
    uint16_t entry_ax = m.ax(), entry_bx = m.bx(), entry_cx = m.cx(), entry_dx = m.dx();
    uint16_t entry_ds = m.ds();

    if (test_event_flag_core(m, m.al())) {
        m.ds() = m.cs();
        uint16_t si = uint16_t(0x5DD5 + (entry_dx << 3));
        m.si() = si;
        m.cx() = 0x0202;
        m.ax() = 1;
        m.bx() = entry_bx;
        draw_tilemap_core(m);
    }

    m.ax() = entry_ax;
    m.bx() = entry_bx;
    m.cx() = entry_cx;
    m.dx() = entry_dx;
    m.ds() = entry_ds;
}
// rom: 0x345DB0 (near)
// If event flag AL is set (test_event_flag_core, inlined the same way as this file's other near-to-near
void draw_flag_gated_icon(Machine &m) {
    draw_flag_gated_icon_core(m);
    m.ret();
}

// Shared body of ROM 0x348518
// Wraps a genuine far call to play_sound_effect (bank 0x3D, segment 0xD000, offset 0xD434): AL/DX in are
// play_sound_effect's own effect-id argument, passed through unchanged. The original brackets the call
// with pushaw + push ds/es/ss, restored once play_sound_effect's `retf` returns -- but play_sound_effect
// is itself now a native routine (see bank3d_sound.cpp) whose whole visible effect is exactly that
// save/restore around play_sound_effect_core, so this just calls that core directly instead of
// reconstructing a real far call/return through the interpreter.
void trigger_sound_effect_core(Machine &m) {
    play_sound_effect_core(m, m.al());
    return;
}

// rom: 0x348518 (near)
void trigger_sound_effect(Machine &m) {
    trigger_sound_effect_core(m);
    m.ret();
}

} // namespace sawgod::game
