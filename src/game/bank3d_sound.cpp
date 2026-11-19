#include "game/computed_flags.h"
#include "game/computed_handlers.h"
// Decompiled routines from bank 0x3D (sound driver). See asm/bank_3D.s and bank3d_sound.h.
//
// sound_tick is the once-per-VBlank sound-engine service. It toggles a couple of output-control bits on
// port 0x91 change, then walks 8 logical "channels" (structs of 0x32 bytes each, at DS:0x62C..0x7AB; DS = 0
// = internal RAM at these call sites -- the numeric addresses below that look like ROM labels, e.g. "0x62C"
// itself, are literal IRAM addresses that happen to share digits with an unrelated same-named ROM data
// blob, per the established pattern from bank34.cpp's ES=D000/DS=0 dual addressing). Each logical channel
// maps onto one of the WSC's 4 hardware PSG channels (channel struct offset +2, low 2 bits); a logical
// channel's compiled note-sequence bytecode lives in the bank's own D000 segment (ES=D000=CS while this
// bank runs) at a pointer cached in the channel struct, and sound_decode_track_commands is the bytecode
// interpreter that walks it: note bytes (0x00-0x9F) look up a base frequency table and write the WSC's
// pitch/volume/sweep/noise ports (0x80-0x8F); bytes 0xA0-0xAF are one-parameter "meta" commands
// (instrument/portamento/noise config); 0xB0-0xBF are section repeats (loop counters + saved read positions);
// 0xC0-0xCF configure tuplet/tie timing; 0xD0-0xEF set a signed vibrato/pitch-bend depth; 0xFD stores a loop
// anchor; 0xFF ends the channel.
//
// Channel struct fields used here (offset from the channel base, DS:0x62C + n*0x32):
//   +0x00 (word) sequence step counter: 2 at channel start (skips a 4-byte header), incremented once per
//                opcode byte the dispatch loop reads; combined with +0x03 to recompute the bytecode read
//                pointer at the start of every call/restart of sound_decode_track_commands (bx = step*2 +
//                track_ptr) -- reused as -1 when the channel is inactive, and overwritten by the loop-repeat
//                opcode (0xB1-0xBF) and the tie opcode (0x7F) to redirect the pointer.
//   +0x02 hardware-channel index (0-3, low 2 bits) and other flag bits (upper nibble: portamento target).
//   +0x03 (word) bytecode track pointer (offset within the D000 segment).
//   +0x06 pitch-table/LFO flags: bit 0x10 selects the "half-rate" note frequency sub-table.
//   +0x07 vibrato/LFO phase accumulator (low nibble of +0x06 added each frame, wraps at 0x10).
//   +0x08 current note pitch nibble pair (envelope table index), +0x0A channel-3 sweep step selector.
//   +0x0B PCM/noise-mode flag (channel 1 8-bit-PCM or channel 3 noise, meaning depends on hw index).
//   +0x0C instrument/noise parameter byte, +0x0D/+0x0E/+0x0F tuplet timing (numerator/denominator/counter).
//   +0x10 (word) last frequency/period word written to the pitch port (0 = silent).
//   +0x12/+0x15 duty/volume-ceiling nibbles (current, reload); +0x13/+0x14 tie length (set value, counter).
//   +0x16.. loop-repeat counter/anchor table (3 bytes per loop id: counter byte, 2-byte saved position).
//   +0x22/+0x23/+0x25 vibrato/pitch-bend depth, reload word, countdown word (set by opcodes 0xD0-0xEF).
//   +0x27 instrument id, +0x28 current envelope/volume nibble pair written to port 0x88+hwidx.
//   +0x2A/+0x2B tie fallback absolute pitch (low/high byte of the tie's two embedded data bytes).
//   +0x2C last raw instrument id byte (0xA0 opcode), +0x2D portamento-linked pitch offset target.
//
// Globals (DS:0x600-0x62B, plus 0x978 bit 1):
//   0x600 per-hw-channel retrigger mask this tick (OR of 0x603 for every logical channel already handled).
//   0x601 per-hw-channel output/gate bits folded into port 0x90 at the end of the tick.
//   0x602 current hw-channel index (0-3) for the logical channel being processed.
//   0x603 that index's two-bit-per-nibble selector mask (0x11/0x22/0x44/0x88), used to touch both the
//         "retrigger" and "gate" masks together.
//   0x604 free-running frame counter (vibrato/LFO phase source), 0x605 current logical-channel index (0-7).
//   0x608/0x609/0x60A fade-out bit-scan state for sound_apply_pending_duty_overrides (per-channel duty
//   override on channel mute). 0x60B source for 0x605's reload (active-channel count), 0x60D cached port-0x91
//   value. 0x619 bitmask of currently-active logical channels; 0x61A+hwidx last instrument id per hw channel.
//   0x624/0x625 portamento glide mode/progress used by sound_advance_shared_portamento.
#include "game/bank3d_sound.h"
#include "game/hooks.h"

#include "emu/wsc.h"

namespace sawgod::game {

namespace {
constexpr uint16_t kBank = 0xD000; // bank 0x3D's own segment (CS while this bank runs; also ES for its data)

constexpr uint16_t kChBase = 0x062C; // channel struct array base (DS:kChBase + n*0x32, 8 channels)
constexpr uint16_t kChStride = 0x32;

// ROM data tables (offsets within the bank's own D000 segment).
constexpr uint16_t kD3D01F0 = 0x01F0; // envelope-index -> raw table (used by
                                      // sound_update_channel_volume/sound_compute_tied_note_volume)
constexpr uint16_t kD3DCF36 = 0xCF36; // envelope-index -> volume-port value
constexpr uint16_t kD3DCF06 = 0xCF06; // note (0-0x2E, doubled) -> base period word
constexpr uint16_t kD3DD036 = 0xD036; // (channel-3 sweep param - 1) -> sweep-step port value
constexpr uint16_t kD3D00F0 = 0x00F0; // instrument id -> 8-word envelope record (ROM)
constexpr uint16_t kD3D00C0 = 0x00C0; // per-hw-channel envelope RAM slot base (IRAM address, 16 bytes each)

// Globals, all DS-relative (internal RAM, DS = 0 at these call sites).
constexpr uint16_t kRetrigMask = 0x600;
constexpr uint16_t kGateBits = 0x601;
constexpr uint16_t kHwIndex = 0x602;
constexpr uint16_t kHwPairMask = 0x603;
constexpr uint16_t kFrameCounter = 0x604;
constexpr uint16_t kChIndex = 0x605;
constexpr uint16_t kFadeMask = 0x608;
constexpr uint16_t kFadeNibble = 0x609;
constexpr uint16_t kFadeScratch = 0x60A;
constexpr uint16_t kChCountReload = 0x60B;
constexpr uint16_t kPort91Cache = 0x60D;
constexpr uint16_t kActiveMask = 0x619;
constexpr uint16_t kLastInstrBase = 0x61A; // +hwidx
constexpr uint16_t kPortaMode = 0x624;
constexpr uint16_t kPortaProgress = 0x625;
constexpr uint16_t kVoiceSlot = 0x60E; // portamento "owner" id compared by sound_advance_shared_portamento
constexpr uint16_t kOutFlags1 = 0x978;
constexpr uint16_t kPort90Backup = 0x61E; // port 0x90 stashed by sound_mute_output/sound_unmute_output
constexpr uint16_t kReqFlag = 0x627; // 0 = queued request (sound_request), 1 = immediate (play_sound_effect)

// More ROM data tables (offsets within the bank's own D000 segment), used by the request/effect dispatch.
constexpr uint16_t kD3D02E0 = 0x02E0; // sound_request's table: 3-byte records (word, type byte) per song id
constexpr uint16_t kD3D0316 = 0x0316; // play_sound_effect's table: same shape, per effect id
constexpr uint16_t kD3D0500 = 0x0500; // sound_dispatch_record's per-logical-channel table: 6-byte records

uint8_t lo(uint16_t value) { return uint8_t(value); }
uint8_t hi(uint16_t value) { return uint8_t(value >> 8); }
void set_lo(uint16_t &register_value, uint8_t value) {
    register_value = uint16_t((register_value & 0xFF00) | value);
}

// Where an instrument's 16-byte waveform is copied: slot hwidx of the wavetable at 00C0h. The original
// computes it as `shl ax,4` (AX = instrument) ... `mov al,[0x602]; shl al,4; add di,ax`, so AH still holds
// the high byte of instrument*16: instruments 16 and up land 256 bytes further per 16 ids (never in play).
uint16_t waveform_slot(uint8_t instr, uint8_t hwidx) {
    return uint16_t(kD3D00C0 + ((uint16_t(instr) << 4) & 0xFF00) + uint8_t(hwidx << 4));
}
void set_hi(uint16_t &register_value, uint8_t value) {
    register_value = uint16_t((register_value & 0x00FF) | (uint16_t(value) << 8));
}

// `out port, ax` / `in ax, port`: low byte first (matches V30MZ::out16/in16 in src/emu/v30mz.cpp).
void out16(Machine &m, uint8_t port, uint16_t value) {
    m.out8(port, lo(value));
    m.out8(uint8_t(port + 1), hi(value));
}
uint16_t in16(Machine &m, uint8_t port) {
    uint16_t value = m.in8(port);
    value = uint16_t(value | (uint16_t(m.in8(uint8_t(port + 1))) << 8));
    return value;
}

// -------------------------------------------------------------------------------------------------
// rom: 0x3DCEF4 (near helper, not independently registered: no movie calls it on its own, only ever
// inlined via a real `call` from within sound_decode_track_commands's 0xFF/terminate handling). Zeroes
// kChStride (0x32) bytes starting at `base`. BX/CX preserved (pushed/popped in the original).
void l3dcef4_clear(Machine &m, uint16_t base) {
    for (uint16_t i = 0; i < kChStride; ++i)
        m.wr8(m.ds(), uint16_t(base + i), 0);
}

// rom: 0x3DCE0D (near helper). If this logical channel's hw pair-mask bit is already set in the
// retrigger mask (0x600), the original does `pop ax; ret`: it discards its own return address and
// returns to *its caller's* caller as well, i.e. it also aborts the rest of L_3DCDE3 below. AX ends up
// holding the low 16 bits of the address right after L_3DCDE3's `call L_3DCE0D` (0x3DCDE3 + 3 = IP
// 0xCDE6) -- reproduced as that literal leftover value. Returns true if this "double return" happened
// (caller must skip the rest of L_3DCDE3's body).
bool l3dce0d_double_return(Machine &m) {
    if ((m.rd8(m.ds(), kHwPairMask) & m.rd8(m.ds(), kRetrigMask)) != 0) {
        m.ax() = 0xCDE6;
        return true;
    }
    return false;
}

// rom: 0x3DCDE3 (near helper). Clears this channel's gate bit in 0x601, silences its volume port
// (0x88+hwidx = 0), forcing the "headphone" bits (0x22) back in when [0x60F] is set. Skipped entirely
// (see l3dce0d_double_return) when this hw channel was already retriggered by another logical channel
// this tick.
void l3dcde3(Machine &m) {
    if (l3dce0d_double_return(m))
        return;
    uint16_t ds = m.ds();
    uint8_t mask = m.rd8(ds, kHwPairMask);
    uint8_t keep = uint8_t(~mask);
    uint8_t ah = uint8_t(keep | 0xF0);
    uint8_t al = uint8_t(m.in8(0x90) & ah);
    if (m.rd8(ds, 0x60F) != 0)
        al = uint8_t(al | 0x22);
    m.out8(0x90, al);
    m.wr8(ds, kGateBits, uint8_t(m.rd8(ds, kGateBits) & keep));
    uint16_t dx = 0x88;
    set_lo(dx, uint8_t(lo(dx) + m.rd8(ds, kHwIndex)));
    m.out8(lo(dx), 0);
    m.set_ah(ah);
    m.set_al(0);
    m.dx() = dx;
}

// rom: 0x3DCE3C (near helper): if this is hw channel 3 (the noise channel) and it is now silent
// (channel-7 slot's +0xB flag byte, the noise-mode flag, is 0), clears the "unmute" bit (0x80) of port
// 0x90; otherwise sets it. All registers preserved.
void l3dce3c(Machine &m) {
    if ((m.rd8(m.ds(), kActiveMask) >> 7) == 0)
        return;
    uint16_t ch7 = uint16_t(kChBase + 7 * kChStride);
    uint8_t al = m.rd8(m.ds(), uint16_t(ch7 + 0xB));
    uint8_t channel_control = m.in8(0x90);
    channel_control = al != 0 ? uint8_t(channel_control | 0x80) : uint8_t(channel_control & 0x7F);
    m.out8(0x90, channel_control);
}

// rom: 0x3DCE19 (near helper): clears this logical channel's bit in the active-channel mask (0x619);
// if this was the last (index 3, i.e. logical channel 3) also refreshes the noise-channel mute bit via
// l3dce3c. CX preserved (pushed/popped in the original).
void l3dce19(Machine &m) {
    uint16_t ds = m.ds();
    uint8_t idx = m.rd8(ds, kChIndex);
    uint8_t bit = 0x80;
    for (uint8_t i = 0; i <= idx; ++i)
        bit = uint8_t((bit << 1) | (bit >> 7)); // rol al,1
    uint8_t inv = uint8_t(~bit);
    m.wr8(ds, kActiveMask, uint8_t(m.rd8(ds, kActiveMask) & inv));
    m.ax() = inv; // `xor ax,ax` then `not al` -- AH stays 0
    if (idx == 3)
        l3dce3c(m); // preserves AX (pushed/popped)
}

// -------------------------------------------------------------------------------------------------
// rom: 0x3DC5AC (far)
// Add the channel's vibrato-table offset to cached pitch and write its hardware frequency.
// Channel-3 (noise) pitch-sweep-table hit: if this hw channel hasn't already been
// retriggered this tick and the noise flag (+0x14, tested via +2's high nibble) is nonzero, looks up a
// signed offset from D_3D0000 (indexed by (+2's high nibble - 1) | low nibble of the current tick's
// vibrato-depth nibble at 0x604), adds it to the cached pitch word (+0x10) and writes the result to the
// pitch port (0x80 + 2*hwidx). No pushaw: AX/BX/CX(low byte)/DX end as scratch leftovers of whichever exit
// path was taken (see each `mov al,...`/`mov bx,...` below); AH/CH/DI/SI are never touched.
void sound_update_channel_vibrato_core(Machine &m) {
    uint16_t ds = m.ds();
    uint16_t si = m.si();

    uint8_t al = uint8_t(m.rd8(ds, kHwPairMask) & m.rd8(ds, kRetrigMask));
    bool skip = al != 0;
    if (!skip) {
        // `test byte ptr [si+0x14],0xff` only sets flags -- AL keeps the (zero) AND result above.
        skip = m.rd8(ds, uint16_t(si + 0x14)) != 0;
    }
    if (!skip) {
        al = uint8_t(m.rd8(ds, uint16_t(si + 2)) & 0xF0);
        if (al == 0) {
            skip = true;
        } else {
            uint8_t ah = m.rd8(ds, 0x604);
            uint8_t idx = uint8_t(al - 0x10);
            idx = uint8_t(idx | uint8_t(ah & 0xF));
            al = idx;
            uint16_t bx = uint16_t(0x0000 + al); // D_3D0000, cs-relative
            m.bx() = bx;
            int8_t delta = int8_t(m.rd8(kBank, bx));
            uint16_t dx = m.rd16(ds, uint16_t(si + 0x10));
            uint16_t ax = uint16_t(uint16_t(int16_t(delta)) + dx); // cbw sign-extends delta, then add
            m.ax() = ax;
            dx = 0x80;
            uint8_t cl = uint8_t(m.rd8(ds, kHwIndex) * 2);
            set_lo(dx, uint8_t(lo(dx) + cl));
            m.dx() = dx;
            out16(m, lo(dx), ax);
            set_lo(m.cx(), cl);
            return;
        }
    }
    m.set_al(al);
}

// rom: 0x3DC466 (far)
// Compute the envelope, slew duty nibbles and write the gated hardware volume.
// Per-frame envelope/duty step for the current channel: recomputes the tuplet
// timing fraction (+0xD/+0xE/+0xF) into a lookup index, reads two chained tables (D_3D01F0 then
// D_3DCF36) to get the target volume/envelope byte (+0x28), silencing it when the cached pitch (+0x10)
// is 0; then (unless this hw channel already fired this tick, or it's channel 2 in "voice" mode with
// [0x60F] set) slews the duty nibble pair (+0x12) toward its reload (+0x15) or, in +0x12/+0x15 tracking
// mode, snaps the two nibbles to match each other, clamps +0x28's nibbles against +0x12, and writes the
// combined byte to the volume port (0x88 + hwidx).
void sound_update_channel_volume_core(Machine &m) {
    uint16_t ds = m.ds();
    uint16_t si = m.si();
    // AX/CX/DX/DI are genuine scratch here (no pushaw): start from the caller's values and only change
    // where the original does, so whichever return path is taken leaves the exact same leftovers.
    uint8_t al = m.al(), ah = m.ah();
    uint8_t cl = lo(m.cx()), ch = hi(m.cx());
    uint16_t dx = m.dx();
    bool touched_di = false;
    uint16_t di = m.di();

    al = m.rd8(ds, kHwPairMask);
    bool retriggered = (al & m.rd8(ds, kRetrigMask)) != 0;
    if (!retriggered) {
        bool pitch_nonzero = m.rd16(ds, uint16_t(si + 0x10)) != 0;
        if (!pitch_nonzero) {
            al = m.rd8(ds, kHwPairMask);
            if ((al & m.rd8(ds, kRetrigMask)) == 0) {
                al = 0;
                m.wr8(ds, uint16_t(si + 0x28), al);
            }
        }
        // L_3DC484
        if (m.rd8(ds, uint16_t(si + 0xD)) != 0) {
            ah = 0;
            al = m.rd8(ds, uint16_t(si + 0xE));
            if (al != 0) {
                cl = al; // mov cx,ax (ah already 0): clears ch too
                ch = 0;
                ah = 0;
                al = m.rd8(ds, uint16_t(si + 0xF));
                uint16_t ax16 = al;
                uint16_t cx16 = uint16_t(uint16_t(ch) << 8 | cl);
                // quotient bits are rotated into DH (`rcl dh,1`): its old low nibble ends up on top
                uint8_t dh = uint8_t(dx >> 8);
                for (int i = 0; i < 4; ++i) {
                    ax16 = uint16_t(ax16 + ax16);
                    bool borrow = ax16 < cx16;
                    if (!borrow)
                        ax16 = uint16_t(ax16 - cx16);
                    dh = uint8_t((dh << 1) | (borrow ? 0 : 1));
                }
                dx = uint16_t(uint16_t(dh) << 8); // DL (the loop counter) always ends at 0
                al = m.rd8(ds, uint16_t(si + 0xD));
                al = uint8_t(al | dh);
                ah = 0;
                di = uint16_t(kD3D01F0 - 0x10 + al);
                touched_di = true;
                ah = m.rd8(ds, uint16_t(si + 8));
                al = m.rd8(kBank, di);
                al = uint8_t((al & 0xF0) | ah);
                di = kD3DCF36;
                ah = 0;
                di = uint16_t(di + al);
                al = m.rd8(kBank, di);
                if (m.rd16(ds, uint16_t(si + 0x10)) == 0)
                    al = 0;
                m.wr8(ds, uint16_t(si + 0x28), al);
            }
        }
    }

    // L_3DC4DB
    al = m.rd8(ds, kHwPairMask);
    bool skip_all = (al & m.rd8(ds, kRetrigMask)) != 0;
    if (!skip_all && m.rd8(ds, kHwIndex) == 1 && m.rd8(ds, 0x60F) != 0)
        skip_all = true;

    if (!skip_all) {
        cl = m.rd8(ds, kOutFlags1); // `test cl,3` right after is dead: `cmp cl,3` overwrites its flags
        if (cl == 3) {
            // je L_3DC506: nibble-sync branch (touches CH).
            cl = m.rd8(ds, uint16_t(si + 0x12));
            ch = cl;
            cl = uint8_t(cl & 0xF);
            ch = uint8_t(ch >> 4);
            if (cl != ch) {
                if (cl < ch) {
                    uint8_t ch_old = ch;
                    ch = uint8_t(ch_old << 4);
                    cl = uint8_t(ch_old | ch);
                } else {
                    uint8_t cl_old = cl;
                    ch = uint8_t(cl_old << 4);
                    cl = uint8_t(cl_old | ch);
                }
                m.wr8(ds, uint16_t(si + 0x12), cl);
            }
        } else {
            // fallthrough (cl != 3): take the reload target directly, no nibble-sync (CH untouched).
            cl = m.rd8(ds, uint16_t(si + 0x15));
            m.wr8(ds, uint16_t(si + 0x12), cl);
        }
        // L_3DC52F: al/ah become the envelope/duty clamp; cl gets reloaded twice more from +0x12 (final
        // leftover = +0x12 & 0xF), ch is untouched here (keeps whatever the duty step above left it as).
        al = m.rd8(ds, uint16_t(si + 0x28));
        ah = al;
        al = uint8_t(al >> 4);
        cl = uint8_t(m.rd8(ds, uint16_t(si + 0x12)) & 0xF0);
        if (ah >= cl)
            ah = cl;
        cl = uint8_t(m.rd8(ds, uint16_t(si + 0x12)) & 0xF);
        if (al >= cl)
            al = cl;
        al = uint8_t(al | ah);
        dx = 0x88;
        set_lo(dx, uint8_t(lo(dx) + m.rd8(ds, kHwIndex)));
        m.out8(lo(dx), al);
    }

    m.set_al(al);
    m.set_ah(ah);
    set_lo(m.cx(), cl);
    set_hi(m.cx(), ch);
    m.dx() = dx;
    if (touched_di)
        m.di() = di;
}

// rom: 0x3DC55A (far)
// Compute and store the envelope volume unconditionally for a tied note.
// Same pitch-table lookup as the front half of sound_update_channel_volume (tuplet-fraction index into
// D_3D01F0 then D_3DCF36), but unconditional (no hw-retrigger gating) and always stores the result to
// +0x28. Used when (re)starting a tied note that begins mid-tuplet. Registers preserved.
void sound_compute_tied_note_volume_core(Machine &m) {
    uint16_t ds = m.ds();
    uint16_t si = m.si();
    // No pushaw: AX/CX/DX/DI end as scratch leftovers (same shape as sound_update_channel_volume's front
    // half).
    if (m.rd8(ds, uint16_t(si + 0xE)) == 0) {
        m.ax() = 0; // `xor ax,ax` runs before the check either way
        return;
    }
    uint16_t cx = m.rd8(ds, uint16_t(si + 0xE));
    uint16_t ax = m.rd8(ds, uint16_t(si + 0xF));
    uint8_t dh = uint8_t(m.dx() >> 8); // `rcl dh,1`: the old low nibble ends up on top
    for (int i = 0; i < 4; ++i) {
        ax = uint16_t(ax + ax);
        bool borrow = ax < cx;
        if (!borrow)
            ax = uint16_t(ax - cx);
        dh = uint8_t((dh << 1) | (borrow ? 0 : 1));
    }
    uint8_t idx = uint8_t(m.rd8(ds, uint16_t(si + 0xD)) | dh);
    uint8_t t1 = m.rd8(kBank, uint16_t(kD3D01F0 - 0x10 + idx));
    uint8_t combined = uint8_t((t1 & 0xF0) | m.rd8(ds, uint16_t(si + 8)));
    uint16_t di = kD3DCF36;
    di = uint16_t(di + combined);
    uint8_t vol = m.rd8(kBank, di);
    if (m.rd16(ds, uint16_t(si + 0x10)) == 0)
        vol = 0;
    m.wr8(ds, uint16_t(si + 0x28), vol);
    m.ax() = vol;
    m.cx() = cx;
    m.dx() = uint16_t(uint16_t(dh) << 8); // DL always ends at 0 (the loop counter runs out)
    m.di() = di;
}

// Per-logical-channel-group (4-7) pitch-sweep-depth block and its backup slot, shared by
// sound_channel_reset's AL=0 (restore) and AL>=2 (back up) paths.
constexpr uint16_t kSweepBase[4] = {uint16_t(kChBase + 0xEA), uint16_t(kChBase + 0x11C),
                                    uint16_t(kChBase + 0x14E), uint16_t(kChBase + 0x180)};
constexpr uint16_t kSweepBackup[4] = {uint16_t(kChBase + 0xD0), uint16_t(kChBase + 0x102),
                                      uint16_t(kChBase + 0x134), uint16_t(kChBase + 0x166)};

// -------------------------------------------------------------------------------------------------
// Shared body of ROM 0x3DD0DB, AL=0 path only (sound_channel_reset): inlined here (see next_random_byte /
// draw_ tilemap_offset_core in bank34.cpp for the established pattern -- sound_advance_shared_portamento
// reaches it via a real `lcall` with no return address left to unwind by a native routine, since we replace
// the whole call chain). Zeroes the pitch-sweep depth block (+0x22 byte, +0x23/+0x25 words) for logical
// channels 4-7 and restores each one's backed-up pitch base (+0xD0 <- +0xF4, etc.), clearing the backup. Also
// clears the portamento mode/progress globals. The original brackets its whole body in cli/sti; since sti
// always re-enables interrupts regardless of the entry state, IF ends up set.
void sound_channel_reset_al0(Machine &m) {
    uint16_t ds = m.ds();
    for (int i = 0; i < 4; ++i) {
        m.wr8(ds, kSweepBase[i], 0);
        m.wr16(ds, uint16_t(kSweepBase[i] + 1), 0);
        m.wr16(ds, uint16_t(kSweepBase[i] + 3), 0);
        uint8_t backed_up = m.rd8(ds, uint16_t(kSweepBackup[i] + 0x24)); // +0xF4 relative to +0xD0
        m.wr8(ds, kSweepBackup[i], backed_up);
        m.wr8(ds, uint16_t(kSweepBackup[i] + 0x25), 0); // +0xF5 relative to +0xD0
    }
    m.wr8(ds, kPortaMode, 0);
    m.wr8(ds, kPortaProgress, 0);
    m.flags() = uint16_t(m.flags() | emu::IF);
}

// Shared body of ROM 0x3DD0DB, AL!=0 path (sound_channel_reset): stores the caller's CL/CH into each of the
// four channel-4-7 groups' pitch-sweep-depth fields (+0xEA set/+0xEB reload/+0xED countdown, CH zero-extended
// to a word), sets the portamento mode global from AL and clears its progress counter, then -- unless AL is
// exactly 1 -- backs up each group's current pitch base (+0xD0 etc.) into its backup slot (+0xF4 etc.),
// clamping it down by the caller's AH (not CH -- a genuinely separate input; 0 treated as 0xF, computed
// once and reused for all four groups: the original never resets AH between them, so only the first group
// can trigger the AH==0 default). See sound_channel_reset_al0 for the AL=0 (restore) counterpart; cli/sti
// bracket both (see there).
void sound_channel_reset_alN(Machine &m, uint8_t al, uint8_t cl, uint8_t ch, uint8_t ah) {
    uint16_t ds = m.ds();
    uint16_t chw = ch;
    for (int i = 0; i < 4; ++i) {
        m.wr8(ds, kSweepBase[i], cl);
        m.wr16(ds, uint16_t(kSweepBase[i] + 1), chw);
        m.wr16(ds, uint16_t(kSweepBase[i] + 3), chw);
    }
    m.wr8(ds, kPortaMode, al);
    m.wr8(ds, kPortaProgress, 0);
    if (al != 1) {
        uint8_t eff_ah = ah == 0 ? 0xF : ah;
        for (int i = 0; i < 4; ++i) {
            uint8_t cur = m.rd8(ds, kSweepBackup[i]);
            m.wr8(ds, uint16_t(kSweepBackup[i] + 0x24), cur);
            uint8_t clamped_counter = cur >= eff_ah ? uint8_t(cur - eff_ah) : 0;
            m.wr8(ds, uint16_t(kSweepBackup[i] + 0x25), clamped_counter);
        }
    }
    m.flags() = uint16_t(m.flags() | emu::IF);
}

// rom: 0x3DCCFE (far)
// Advance melody-channel pitch glides and reset shared state when all owners finish.
// Portamento/glide stepper: while the current voice-slot ([0x60E]) is the "owner"
// of the active glide (compared against 0x625's matching nibble), each call counts down +0x25 and, on
// reaching 0, advances +0x8 (the current pitch nibble) one step toward +0x2D (mode 2, upward-biased) or
// +0x2C (mode 1, capped) and marks this logical channel's ownership bit in 0x625 -- ending the glide
// (resetting the shared channel-envelope state via sound_channel_reset) once every channel using this
// glide has claimed ownership. Only acts when [0x605] (logical channel index) is 0-3 is false, i.e. for
// logical channels 4-7 (the "melody" set is 0-3; see the sound_channel_reset comment above).
// L_3DCDD4: shared store/silence tail for both glide modes. Mode 2 leaves the step size in AH; mode 1
// replaces AH with the pitch cap and, when claiming ownership, leaves accumulated progress in CH.
void l3dccfe_store(Machine &m, uint8_t al) {
    uint16_t ds = m.ds();
    uint16_t si = m.si();
    m.wr8(ds, uint16_t(si + 8), al);
    if (al == 0)
        m.wr16(ds, uint16_t(si + 0x10), 0);
    m.set_al(al);
}

void sound_advance_shared_portamento_core(Machine &m) {
    uint16_t ds = m.ds();
    uint16_t si = m.si();
    uint8_t idx = m.rd8(ds, kChIndex);
    if (idx <= 3) {
        m.set_al(idx);
        return;
    }
    uint8_t mode = m.rd8(ds, kPortaMode);
    if (mode == 2) {
        uint8_t al = uint8_t(m.rd8(ds, kPortaProgress) & 0xF);
        m.set_al(al);
        if (al == m.rd8(ds, kVoiceSlot))
            return;
        uint16_t cnt = uint16_t(m.rd16(ds, uint16_t(si + 0x25)) - 1);
        m.wr16(ds, uint16_t(si + 0x25), cnt);
        if (cnt != 0)
            return;
        m.wr16(ds, uint16_t(si + 0x25), m.rd16(ds, uint16_t(si + 0x23)));
        uint8_t ah = m.rd8(ds, uint16_t(si + 0x22));
        m.set_ah(ah);
        uint8_t base = m.rd8(ds, uint16_t(si + 8));
        al = base >= ah ? uint8_t(base - ah) : 0;
        uint8_t target = m.rd8(ds, uint16_t(si + 0x2D));
        if (al > target) {
            l3dccfe_store(m, al);
            return;
        }
        // al == target, or al < target: always reload al = target and mark this channel's claim bit.
        al = target;
        uint8_t ch = 1;
        for (uint8_t i = 0; i < idx; ++i)
            ch = uint8_t(ch << 1);
        ch = uint8_t(ch >> 4);
        m.wr8(ds, kPortaProgress, uint8_t(m.rd8(ds, kPortaProgress) | ch));
        m.cx() = uint16_t(uint16_t(ch) << 8); // CL (the loop counter) always ends at 0
        l3dccfe_store(m, al);
        return;
    }
    if (mode == 1) {
        uint8_t al = uint8_t(m.rd8(ds, kPortaProgress) >> 4);
        m.set_al(al);
        if (al == m.rd8(ds, kVoiceSlot))
            return;
        uint16_t cnt = uint16_t(m.rd16(ds, uint16_t(si + 0x25)) - 1);
        m.wr16(ds, uint16_t(si + 0x25), cnt);
        if (cnt != 0)
            return;
        m.wr16(ds, uint16_t(si + 0x25), m.rd16(ds, uint16_t(si + 0x23)));
        uint8_t ah = m.rd8(ds, uint16_t(si + 0x22));
        m.set_ah(ah);
        al = uint8_t(m.rd8(ds, uint16_t(si + 8)) + ah);
        uint8_t cap = m.rd8(ds, uint16_t(si + 0x2C));
        m.set_ah(cap);
        if (al < cap) {
            l3dccfe_store(m, al);
            return;
        }
        al = cap;
        uint8_t ch = 1;
        for (uint8_t i = 0; i < idx; ++i)
            ch = uint8_t(ch << 1);
        ch = uint8_t(ch & 0xF0);
        m.wr8(ds, kPortaProgress, uint8_t(m.rd8(ds, kPortaProgress) | ch));
        ch = uint8_t(m.rd8(ds, kPortaProgress) >> 4);
        m.cx() = uint16_t(uint16_t(ch) << 8); // mov ch,[0x625]; shr ch,4; CL ends at 0
        if (ch == m.rd8(ds, kVoiceSlot)) {
            m.ax() = 0; // mov ax,0 before the inlined sound_channel_reset call
            sound_channel_reset_al0(m);
            return;
        }
        l3dccfe_store(m, al);
        return;
    }
    m.set_al(idx); // neither mode 1 nor 2: falls straight through to the shared return, AL unchanged
}
// rom: 0x3DC400 (near)
// Clear the pending duty-override mask while preserving AX and DS.
// Clears bit 1 of [0x978]; the game_state read/test it does (bit 0x10 of
// game_state+0xE7) doesn't actually change the outcome (both branches converge), so it's dead code with
// no further effect. AX/DS preserved (pushed/popped in the original).
void sound_reset_pending_duty_overrides_core(Machine &m) {
    m.wr8(m.ds(), kOutFlags1, uint8_t(m.rd8(m.ds(), kOutFlags1) & 0xFD));
}

// rom: 0x3DC41A (far)
// Mark the current logical channel as having ended its note.
// ORs bit (1 << [0x605]) into the fade-scratch byte [0x60A] -- marks this logical
// channel's slot as "note ended" for sound_apply_pending_duty_overrides to notice. Registers preserved.
void sound_mark_note_ended_core(Machine &m) {
    uint8_t idx = m.rd8(m.ds(), kChIndex);
    uint8_t bit = 1;
    for (uint8_t i = 0; i < idx; ++i)
        bit = uint8_t(bit << 1);
    m.wr8(m.ds(), kFadeScratch, uint8_t(m.rd8(m.ds(), kFadeScratch) | bit));
    m.ax() = bit; // the countdown loop always leaves AH = 0, AL = the computed bit
}

// rom: 0x3DC42F (far)
// Apply the duty override to eligible channels and clear pending and ended-note masks.
// For each bit set in [0x60A]&[0x608] (channels whose note just ended, from
// sound_mark_note_ended, that were also pending a duty override), walks channel structs at +6 starting from
// channel 0 applying [0x609]'s low nibble to any channel whose corresponding [0x608] bit is clear, then
// clears [0x608]/[0x60A]. Registers preserved (pushaw brackets the whole body).
void sound_apply_pending_duty_overrides_core(Machine &m) {
    uint16_t ds = m.ds();
    uint8_t gate = uint8_t(m.rd8(ds, kFadeScratch) & m.rd8(ds, kFadeMask));
    if (gate == 0) {
        m.wr8(ds, kFadeScratch, 0);
        m.set_al(0); // AH left as whatever the caller had; AL always ends at 0 (see below)
        return;
    }
    uint16_t addr = uint16_t(kChBase + 6);
    uint8_t nib = uint8_t(m.rd8(ds, kFadeNibble) & 0xF);
    m.set_ah(nib);
    uint8_t al = m.rd8(ds, kFadeMask);
    for (;;) {
        bool bit0 = (al & 1) != 0;
        al = uint8_t(al >> 1);
        m.wr8(ds, kFadeScratch, al);
        if (bit0) {
            uint8_t field = m.rd8(ds, addr);
            m.wr8(ds, addr, uint8_t((field & 0xF0) | nib));
        }
        addr = uint16_t(addr + kChStride);
        if (al == 0)
            break;
    }
    m.wr8(ds, kFadeMask, 0);
    m.wr8(ds, kFadeScratch, 0);
    m.set_al(0); // the loop's shr always leaves AL = 0 on exit
}

// rom: 0x3DCA75 (near)
// Consume track commands until a note, rest, duration or end marker is reached.
// See the file-level comment for the bytecode format. Reads and dispatches opcode
// bytes from the current channel's track until it either plays a note (or a rest, or the 0xA7 short-form
// duration-only command) or hits the 0xFF end marker; loop-repeat (0xB0-0xBF) restarts the whole function
// from its entry (recomputing the read pointer from the channel's step counter), everything else loops
// back to the byte-fetch point. BX/CX/DX/DI end as scratch leftovers of whichever opcode path was taken
// last (not restored by the original); SI is the channel pointer throughout (never reassigned
// permanently); ES ends at the bank's own segment (0xD000, set unconditionally on entry).
void sound_decode_track_commands_core(Machine &m) {
    const uint16_t ds = m.ds();
    const uint16_t si = m.si();

restart:
    m.bx() = uint16_t((m.rd16(ds, si) << 1) + m.rd16(ds, uint16_t(si + 3)));

fetch:
    m.es() = kBank;
    m.wr16(ds, si, uint16_t(m.rd16(ds, si) + 1));
    {
        uint8_t al = m.rd8(kBank, m.bx());
        m.bx() = uint16_t(m.bx() + 1);
        m.set_al(al);
        m.set_ah(0);

        if (al < 0xB0) {
            if (al < 0xA0) {
                // --- note / rest / tie (0x00-0x9F) ---
                set_lo(m.dx(), al);                 // mov dl,al -- unconditional, DH untouched
                uint8_t dur = m.rd8(kBank, m.bx()); // peeked; bx is NOT advanced past it
                m.wr8(ds, uint16_t(si + 9), dur);
                m.set_ah(dur); // mov ah, byte ptr es:[bx] -- AH stays this unless overwritten below

                if (m.rd8(ds, uint16_t(si + 0xB)) != 0 && m.rd8(ds, kHwIndex) == 1) {
                    // L_3DCA6B: hw channel 1 (8-bit PCM voice) with the PCM flag set -- short exit, no
                    // pitch/volume issued.
                    m.cx() = al; // mov cl,al; xor ch,ch
                    m.ax() = m.rd8(ds, uint16_t(si + 0xC));
                    set_lo(m.dx(), al);
                    return;
                }

                uint16_t pitch;
                if (al == 0x7F) {
                    // tie: the two bytes after the duration are the new pitch (high byte first)
                    m.bx() = uint16_t(m.bx() + 1);
                    uint8_t b1 = m.rd8(kBank, m.bx());
                    m.wr8(ds, uint16_t(si + 0x2B), b1);
                    m.bx() = uint16_t(m.bx() + 1);
                    uint8_t b2 = m.rd8(kBank, m.bx());
                    m.wr8(ds, uint16_t(si + 0x2A), b2);
                    m.bx() = uint16_t(m.bx() + 1);
                    m.wr16(ds, si, uint16_t(m.rd16(ds, si) + 1));
                    pitch = m.rd16(ds, uint16_t(si + 0x2A));
                } else {
                    uint8_t nib = uint8_t(al & 0xF);
                    if (nib >= 0xC) {
                        m.wr16(ds, uint16_t(si + 0x10), 0);
                        uint8_t rmask = uint8_t(m.rd8(ds, kHwPairMask) & m.rd8(ds, kRetrigMask));
                        if (rmask == 0)
                            m.wr8(ds, uint16_t(si + 0x28), 0);
                        m.set_al(rmask); // AH stays `dur` (never touched on this path)
                        return;
                    }
                    uint8_t tidx = uint8_t(nib << 1);
                    if (m.rd8(ds, uint16_t(si + 6)) & 0x10)
                        tidx = uint8_t(tidx + 0x18);
                    uint16_t base_freq = m.rd16(kBank, uint16_t(kD3DCF06 + tidx));
                    uint8_t octave = uint8_t(al >> 4);
                    for (uint8_t i = 0; i < octave; ++i)
                        base_freq = uint16_t(base_freq >> 1);
                    pitch = uint16_t(0x800 - base_freq);
                    m.set_ah(0); // xor ah,ah before the table lookup; the octave loop ends AH at 0 too
                }

                m.wr8(ds, uint16_t(si + 0xF), 0);
                {
                    uint8_t rmask = uint8_t(m.rd8(ds, kHwPairMask) & m.rd8(ds, kRetrigMask));
                    if (rmask != 0) {
                        m.bx() = pitch;
                        m.set_al(rmask);
                        return;
                    }
                }

                uint8_t hwidx = m.rd8(ds, kHwIndex);
                uint8_t instr = m.rd8(ds, uint16_t(si + 0x27));
                m.wr8(ds, uint16_t(kLastInstrBase + hwidx), instr);

                uint16_t src = uint16_t(kD3D00F0 + instr * 16);
                uint16_t dst = waveform_slot(instr, hwidx);
                for (int i = 0; i < 8; ++i)
                    m.wr16(ds, uint16_t(dst + i * 2), m.rd16(kBank, uint16_t(src + i * 2)));
                m.cx() = 0;                  // the `loop` counter always reaches 0
                m.di() = uint16_t(dst + 16); // DI is left past the copy (SI is popped)

                if (m.rd8(ds, uint16_t(si + 0xD)) != 0)
                    sound_compute_tied_note_volume_core(m);

                uint8_t volume = m.rd8(ds, uint16_t(si + 8));
                volume = uint8_t((volume >> 4) | (volume << 4)); // ror al,4
                m.wr8(ds, uint16_t(si + 0x28), volume);

                if (hwidx == 3 && m.rd8(ds, uint16_t(si + 0xB)) != 0)
                    m.out8(0x8E, uint8_t(m.rd8(ds, uint16_t(si + 0xC)) | 0x18));

                uint8_t port = uint8_t(0x80 + 2 * hwidx);
                out16(m, port, pitch);
                m.dx() = port; // mov dx,0x80; add dl,al -- DH stays 0
                m.wr16(ds, uint16_t(si + 0x10), pitch);
                m.bx() = pitch;

                if (hwidx == 2) {
                    uint8_t a = m.rd8(ds, uint16_t(si + 0xA));
                    if (a != 0) {
                        m.out8(0x8D, uint8_t(a >> 4));
                        // 16-bit index (`and ax, 0Fh; dec ax`): speed 0 reads the byte before the table.
                        // The original reuses SI as the table pointer and never restores it.
                        uint16_t table = uint16_t(kD3DD036 + uint16_t((a & 0xF) - 1));
                        uint8_t step = m.rd8(kBank, table);
                        m.si() = table;
                        m.out8(0x8C, step);
                        m.set_al(step);
                    } else {
                        m.out8(0x8C, a);
                        m.set_al(a);
                    }
                }
                uint8_t mask = m.rd8(ds, kHwPairMask);
                m.wr8(ds, kGateBits, uint8_t((m.rd8(ds, kGateBits) & ~mask) | mask));
                m.ax() = uint16_t((uint16_t(uint8_t(~mask)) << 8) | mask); // mov al,mask; mov ah,al; not ah
                return;
            }
            // --- 0xA0-0xAF meta commands ---
            switch (al) {
            case 0xA0: {
                uint8_t volume = m.rd8(kBank, m.bx());
                m.bx() = uint16_t(m.bx() + 1);
                uint8_t ch_idx = m.rd8(ds, kChIndex);
                // si+8/si+0x28 are set unless this is logical channel 4-7 *and* the portamento-mode global
                // is nonzero (matches the original's `cmp ah,3/2/1/0; je ...; cmp [0x624],0; jne skip`).
                bool skip = (ch_idx >= 4) && (m.rd8(ds, kPortaMode) != 0);
                // si+0x2c always gets AL's value at that point: `volume` if skipped, else the retrigger-mask
                // byte (AL gets overwritten by `mov al,[0x603]; and al,[0x600]` before falling through).
                uint8_t final_al = volume;
                if (!skip) {
                    m.wr8(ds, uint16_t(si + 8), volume);
                    final_al = uint8_t(m.rd8(ds, kHwPairMask) & m.rd8(ds, kRetrigMask));
                    if (final_al == 0)
                        m.wr8(ds, uint16_t(si + 0x28), final_al);
                }
                m.wr8(ds, uint16_t(si + 0x2C), final_al);
                goto fetch;
            }
            case 0xA1: {
                uint8_t sweep_control = m.rd8(kBank, m.bx());
                m.bx() = uint16_t(m.bx() + 1);
                m.wr8(ds, uint16_t(si + 0xA), sweep_control);
                if (sweep_control == 0)
                    m.out8(0x8C, 0);
                goto fetch;
            }
            case 0xA2: {
                uint8_t instrument_or_noise = m.rd8(kBank, m.bx());
                m.bx() = uint16_t(m.bx() + 1);
                bool to_c = m.rd8(ds, uint16_t(si + 0xB)) != 0 &&
                            ((m.rd8(ds, kHwIndex) & 1) != 0 || (m.rd8(ds, kHwIndex) & 3) != 0);
                m.wr8(ds, uint16_t(si + (to_c ? 0xC : 0x27)), instrument_or_noise);
                goto fetch;
            }
            case 0xA3: {
                uint8_t tie_parameter = m.rd8(kBank, m.bx());
                m.bx() = uint16_t(m.bx() + 1);
                if (tie_parameter >= 0xF0) {
                    m.wr8(ds, uint16_t(si + 2), uint8_t(m.rd8(ds, uint16_t(si + 2)) & 0xF));
                } else {
                    uint8_t ah = tie_parameter;
                    uint8_t tie = uint8_t(uint8_t(tie_parameter & 0xF) << 1);
                    m.wr8(ds, uint16_t(si + 0x13), tie);
                    m.wr8(ds, uint16_t(si + 0x14), tie);
                    ah = uint8_t((ah + 0x10) & 0xF0);
                    m.wr8(ds, uint16_t(si + 2), uint8_t((m.rd8(ds, uint16_t(si + 2)) & 0xF) | ah));
                }
                goto fetch;
            }
            case 0xA4: {
                uint8_t noise_enabled = m.rd8(kBank, m.bx());
                m.bx() = uint16_t(m.bx() + 1);
                m.wr8(ds, uint16_t(si + 0xB), noise_enabled);
                if (m.rd8(ds, kHwIndex) == 3) {
                    uint8_t p = m.in8(0x90);
                    p = noise_enabled != 0 ? uint8_t(p | 0x80) : uint8_t(p & 0x7F);
                    m.out8(0x90, p);
                }
                goto fetch;
            }
            case 0xA5: {
                uint8_t v = m.rd8(kBank, m.bx());
                m.bx() = uint16_t(m.bx() + 1);
                m.wr8(ds, uint16_t(si + 0x12), v);
                m.wr8(ds, uint16_t(si + 0x15), v);
                goto fetch;
            }
            case 0xA6:
                m.bx() = uint16_t(m.bx() + 1);
                goto fetch;
            case 0xA7: {
                uint8_t v = m.rd8(kBank, m.bx()); // NOT advanced (matches the original's missing inc bx)
                m.wr8(ds, uint16_t(si + 9), v);
                uint8_t mask = m.rd8(ds, kHwPairMask);
                m.wr8(ds, kGateBits, uint8_t((m.rd8(ds, kGateBits) & ~mask) | mask));
                m.ax() = uint16_t((uint16_t(uint8_t(~mask)) << 8) | mask);
                return;
            }
            case 0xAE: {
                uint8_t pitch_mode = uint8_t(m.rd8(kBank, m.bx()) & 0x10);
                m.bx() = uint16_t(m.bx() + 1);
                m.wr8(ds, uint16_t(si + 6), uint8_t((m.rd8(ds, uint16_t(si + 6)) & 0xEF) | pitch_mode));
                goto fetch;
            }
            case 0xAF: {
                uint8_t v = uint8_t(m.rd8(kBank, m.bx()) & 0xF);
                m.bx() = uint16_t(m.bx() + 1);
                m.wr8(ds, uint16_t(si + 7), v);
                m.wr8(ds, uint16_t(si + 6), uint8_t((m.rd8(ds, uint16_t(si + 6)) & 0xF0) | v));
                goto fetch;
            }
            default: // 0xA8-0xAD and any other unused value in range: skip one parameter byte
                m.bx() = uint16_t(m.bx() + 1);
                goto fetch;
            }
        }

        if (al < 0xD0) {
            if (al < 0xC0) {
                // --- 0xB0-0xBF: section repeat ---
                uint8_t nib = uint8_t(al & 0xF);
                if (nib == 0) {
                    // L_3DCB3C: 0xB0 takes an extended parameter byte instead of a plain 1-15 loop id.
                    uint8_t param = m.rd8(kBank, m.bx()); // not yet advanced
                    if ((param & 0xF0) == 0xF0) {
                        uint8_t id2 = uint8_t(param & 0xF);
                        uint16_t di2 = uint16_t(si + 0x16 + id2 * 3);
                        m.di() = di2;
                        m.wr16(ds, si, m.rd16(ds, uint16_t(di2 + 1)));
                        goto restart;
                    }
                    uint8_t hi = uint8_t(param >> 4);
                    uint8_t idx2 = uint8_t(hi * 2 + param); // 8-bit: `add al,al; add al,ah`
                    uint16_t di2 = uint16_t(si + 0x16 + idx2);
                    m.di() = di2;
                    uint8_t stored = uint8_t(param & 0xF);
                    m.wr8(ds, di2, stored);
                    set_lo(m.cx(), stored); // mov cl,al;...;and cl,0xf (CH untouched)
                    m.bx() = uint16_t(m.bx() + 1);
                    goto fetch;
                }
                uint8_t id = nib;
                uint8_t count = uint8_t(m.rd8(kBank, m.bx()) & 0xF); // NOT advanced (bx unchanged)
                m.cx() = uint16_t(count * 3); // mov cl,ah;add cl,cl;add cl,ah;xor ch,ch
                uint16_t entry = uint16_t(si + 0x16 + count * 3);
                m.di() = entry;
                uint8_t counter = m.rd8(ds, entry);
                uint8_t remaining_loops;
                if (counter != 0) {
                    remaining_loops = counter;
                } else {
                    id = uint8_t(id + 1);
                    remaining_loops = id;
                }
                set_lo(m.dx(), id); // `mov dl,al` (and `inc dl` when a new loop starts)
                remaining_loops = uint8_t(remaining_loops - 1);
                m.wr8(ds, entry, remaining_loops);
                if (remaining_loops != 0)
                    m.wr16(ds, si, m.rd16(ds, uint16_t(entry + 1)));
                goto restart;
            }
            // --- 0xC0-0xCF: tuplet timing ---
            uint8_t nib = uint8_t(al & 0xF);
            uint8_t param = m.rd8(kBank, m.bx());
            m.wr8(ds, uint16_t(si + 0xE), param);
            m.wr8(ds, uint16_t(si + 0xD), uint8_t((nib >> 4) | (nib << 4))); // ror ah,4 (nib in ah, top=0)
            m.bx() = uint16_t(m.bx() + 1);
            goto fetch;
        }

        if (al < 0xF0) {
            // --- 0xD0-0xEF: signed vibrato/pitch-bend depth ---
            uint8_t bend_depth = uint8_t(al & 0xF);
            if (al >= 0xE0)
                bend_depth = uint8_t(uint8_t(0) - bend_depth);
            m.wr8(ds, uint16_t(si + 0x22), bend_depth);
            m.wr8(ds, uint16_t(si + 0x23), bend_depth);
            m.wr8(ds, uint16_t(si + 0x25), bend_depth);
            m.bx() = uint16_t(m.bx() + 1);
            goto fetch;
        }
        if (al == 0xFD) {
            uint8_t id = uint8_t(m.rd8(kBank, m.bx()) & 0xF);
            uint16_t di = uint16_t(si + 0x16 + id * 3 + 1);
            m.di() = di;
            m.wr16(ds, di, m.rd16(ds, si));
            m.bx() = uint16_t(m.bx() + 1);
            goto fetch;
        }
        if (al == 0xFF) {
            l3dcef4_clear(m, si);
            m.wr16(ds, si, 0xFFFF);
            l3dcde3(m);
            l3dce19(m);
            return;
        }
        // 0xF0-0xFC, 0xFE: unused/reserved, skip one parameter byte.
        m.bx() = uint16_t(m.bx() + 1);
        goto fetch;
    }
}

// rom: 0x3DC749 (near helper, not independently registered): starts a fresh note sequence for a channel
// whose step counter is exactly 0 (reached from sound_tick when a channel hasn't advanced at all yet).
// Loads the track's 4-byte header (instrument-ish byte 0, two per-hw-channel-kind config bytes, a flag
// byte), copies the channel's initial instrument envelope, primes pitch/duty state, then rejoins
// sound_tick's shared per-channel tail (tie-length reload + sound_decode_track_commands) via the caller.
void l3dc749_core(Machine &m) {
    uint16_t ds = m.ds();
    uint16_t si = m.si();
    uint16_t bx = m.rd16(ds, uint16_t(si + 3));

    m.wr8(ds, uint16_t(si + 7), 0);
    uint8_t header0 = m.rd8(kBank, bx);
    m.wr8(ds, uint16_t(si + 6), header0);

    uint8_t hwidx = m.rd8(ds, kHwIndex);
    if (m.rd8(kBank, uint16_t(bx + 3)) == 1 && hwidx == 1) {
        m.wr8(ds, uint16_t(si + 0xC), m.rd8(kBank, uint16_t(bx + 1)));
    } else if (hwidx == 3) {
        m.wr8(ds, uint16_t(si + 0xC), m.rd8(kBank, uint16_t(bx + 1)));
    } else {
        uint8_t instrument = m.rd8(kBank, uint16_t(bx + 1));
        m.wr8(ds, uint16_t(si + 0x27), instrument);
        m.wr8(ds, uint16_t(kLastInstrBase + hwidx), instrument);
    }

    uint8_t instr = m.rd8(kBank, uint16_t(bx + 1)); // AL after any of the branches above
    uint16_t src = uint16_t(kD3D00F0 + instr * 16);
    uint16_t dst = waveform_slot(instr, hwidx);
    for (int i = 0; i < 8; ++i)
        m.wr16(ds, uint16_t(dst + i * 2), m.rd16(kBank, uint16_t(src + i * 2)));
    m.cx() = 0;                  // the `loop` counter always reaches 0
    m.di() = uint16_t(dst + 16); // DI is never restored after this copy (unlike SI, which is popped)

    uint8_t al = m.rd8(kBank, uint16_t(bx + 2));
    uint8_t ah = al;
    if (m.rd8(ds, kPortaMode) == 1) {
        al = 0;
        m.wr16(ds, uint16_t(si + 0x10), 0);
    }
    m.wr8(ds, uint16_t(si + 8), al);
    m.wr8(ds, uint16_t(si + 0x2C), ah);

    uint8_t bx3 = m.rd8(kBank, uint16_t(bx + 3));
    if ((hwidx & 1) == 0) {
        m.wr8(ds, uint16_t(si + 0xA), bx3);
    } else {
        m.wr8(ds, uint16_t(si + 0xB), bx3);
        if (bx3 != 0 && hwidx == 3) {
            uint8_t p = m.in8(0x90);
            m.out8(0x90, uint8_t(p | 0x80));
        }
    }

    m.wr8(ds, uint16_t(si + 0xD), 0);
    m.wr8(ds, uint16_t(si + 0x12), 0xFF);
    m.wr8(ds, uint16_t(si + 0x15), 0xFF);
    m.wr16(ds, si, 2);
    m.wr16(ds, 0x61F, 0xFFFF);
}

// rom: 0x3DC825 (far). Per-frame sound-engine service, called once from irq_vblank. See the file-level
// comment for the sequencer/hardware-channel model. Walks all 8 logical channels: skips inactive ones
// (step counter -1); for a fresh channel (step counter 0) runs l3dc749_core to start its first note;
// otherwise steps the pitch-sweep/envelope helpers, advances the vibrato/LFO phase (holding off the rest
// of this frame's processing if it just wrapped), steps the portamento glide, and counts down the current
// note's duration, re-entering the bytecode interpreter (sound_decode_track_commands) once it reaches 0.
// Channel 2 gets an extra check against the noise-LFSR readback (port 0x84) to silence it once the LFSR has
// run far enough. Finishes by folding the accumulated gate bits into port 0x90 and running the fade-out step
// (sound_apply_pending_duty_overrides).
void sound_tick_core(Machine &m) {
    uint16_t ds = m.ds();

    uint8_t port91 = m.in8(0x91);
    if (port91 != m.rd8(ds, kPort91Cache)) {
        m.wr8(ds, kPort91Cache, port91);
        if (port91 & 0x80) {
            port91 = uint8_t((port91 | 8) & 0xFE);
            m.wr8(ds, kOutFlags1, uint8_t(m.rd8(ds, kOutFlags1) | 1));
        } else {
            port91 = uint8_t((port91 | 1) & 0xF7);
            m.wr8(ds, kOutFlags1, uint8_t(m.rd8(ds, kOutFlags1) & 0xFE));
        }
        m.out8(0x91, port91);
    }

    sound_reset_pending_duty_overrides_core(m);
    m.wr8(ds, kChIndex, m.rd8(ds, kChCountReload));
    m.wr8(ds, kRetrigMask, 0);
    m.wr8(ds, kFrameCounter, uint8_t(m.rd8(ds, kFrameCounter) + 1));

    uint16_t si = kChBase;
    m.si() = si;
    for (;;) {
        if (m.rd16(ds, si) == 0xFFFF)
            goto loop_tail;
        {
            uint8_t idx = m.rd8(ds, kChIndex);
            uint8_t bit_active = 0x80;
            for (uint8_t i = 0; i <= idx; ++i)
                bit_active = uint8_t((bit_active << 1) | (bit_active >> 7));
            m.wr8(ds, kActiveMask, uint8_t(m.rd8(ds, kActiveMask) | bit_active));

            uint8_t hwidx = uint8_t(m.rd8(ds, uint16_t(si + 2)) & 3);
            m.wr8(ds, kHwIndex, hwidx);
            uint8_t bit_pair = 0x88;
            for (uint8_t i = 0; i <= hwidx; ++i)
                bit_pair = uint8_t((bit_pair << 1) | (bit_pair >> 7));
            m.wr8(ds, kHwPairMask, bit_pair);
            set_lo(m.cx(), 0);  // both `dec cl; jne` loops end with CL = 0
            m.set_al(bit_pair); // AL = the last mask computed

            if (m.rd16(ds, si) == 0) {
                l3dc749_core(m);
                goto shared_tail;
            }

            sound_update_channel_vibrato_core(m);
            sound_update_channel_volume_core(m);

            uint8_t tuplet_den = m.rd8(ds, uint16_t(si + 0xE));
            uint8_t tuplet_cnt = uint8_t(m.rd8(ds, uint16_t(si + 0xF)) + 1);
            if (tuplet_cnt >= tuplet_den)
                tuplet_cnt = tuplet_den;
            m.wr8(ds, uint16_t(si + 0xF), tuplet_cnt);

            uint8_t lfo_rate = uint8_t(m.rd8(ds, uint16_t(si + 6)) & 0xF);
            uint8_t phase = uint8_t(lfo_rate + m.rd8(ds, uint16_t(si + 7)));
            if (phase >= 0x10) {
                phase = uint8_t(phase - 0x10);
                m.wr8(ds, uint16_t(si + 7), phase);
                goto loop_tail_after_mask;
            }
            m.wr8(ds, uint16_t(si + 7), phase);

            sound_advance_shared_portamento_core(m);
            uint8_t tie_left = m.rd8(ds, uint16_t(si + 0x14));
            if (tie_left != 0)
                m.wr8(ds, uint16_t(si + 0x14), uint8_t(tie_left - 1));

            uint8_t dur = uint8_t(m.rd8(ds, uint16_t(si + 9)) - 1);
            m.wr8(ds, uint16_t(si + 9), dur);
            if (dur != 0)
                goto loop_tail_after_mask;
            sound_mark_note_ended_core(m);
        }
    shared_tail: // L_3DC8E2
        m.wr8(ds, uint16_t(si + 0x14), m.rd8(ds, uint16_t(si + 0x13)));
        sound_decode_track_commands_core(m);
    loop_tail_after_mask: // L_3DC8EB
    {
        uint8_t mask = m.rd8(ds, kHwPairMask);
        m.wr8(ds, kRetrigMask, uint8_t(m.rd8(ds, kRetrigMask) | mask));
        if (m.rd8(ds, kHwIndex) == 2) {
            uint16_t lfsr = in16(m, 0x84);
            if (lfsr < 0x96) {
                m.wr16(ds, uint16_t(si + 0x10), 0);
                out16(m, 0x84, 0);
            }
        }
    }
    loop_tail: // L_3DC900
        si = uint16_t(si + kChStride);
        m.si() = si;
        {
            uint8_t idx = uint8_t(m.rd8(ds, kChIndex) + 1);
            m.wr8(ds, kChIndex, idx);
            if (idx >= 8)
                break;
        }
    }

    // L_3DC912
    {
        uint8_t al = m.in8(0x90);
        uint8_t gate = uint8_t(m.rd8(ds, kGateBits) & 0xF);
        al = uint8_t(al | gate);
        if (m.rd8(ds, 0x60F) != 0)
            al = uint8_t(al | 0x22);
        m.out8(0x90, al);
        m.set_al(al);
        m.set_ah(gate);
    }
    sound_apply_pending_duty_overrides_core(m);
}

// -------------------------------------------------------------------------------------------------
// Request/effect dispatch: sound_request (music/background SFX, queued once per frame) and
// play_sound_effect (immediate SFX) both funnel through sound_dispatch_record, which reserves 1-4 hw
// channels via sound_reserve_channels_N and wires up new logical-channel slots from a small ROM table.

// rom: 0x3DCE73 (near). Zeroes the whole sound-driver state block DS:0x600-0x62B plus 0x978-0x97C -- except
// 0x61F/0x620 (word), left at 0xFFFF (the same "inactive" sentinel used for a channel's own step counter),
// and 0x626, which the original's unrolled zeroing skips entirely (every other byte in the range is
// covered; this one alone isn't -- reproduced as found). AX ends at 0 (`xor ax,ax`, never touched again).
void sound_reset_driver_state_core(Machine &m) {
    uint16_t ds = m.ds();
    for (uint16_t a = 0x600; a <= 0x62B; ++a) {
        if (a == 0x626)
            continue;
        m.wr8(ds, a, 0);
    }
    for (uint16_t a = 0x978; a <= 0x97C; ++a)
        m.wr8(ds, a, 0);
    m.wr16(ds, 0x61F, 0xFFFF);
}

// rom: 0x3DC5FE (far). Sets the fade-out bit-scan state used by sound_apply_pending_duty_overrides:
// [0x608]=CH, [0x609]=CL, [0x60A]=0. Only ever called from sound_init with CX=0 (so, in practice, just zeroes
// all three).
void sound_init_fade_state_core(Machine &m, uint8_t cl, uint8_t ch) {
    m.wr8(m.ds(), kFadeMask, ch);
    m.wr8(m.ds(), kFadeNibble, cl);
    m.wr8(m.ds(), kFadeScratch, 0);
}

// rom: 0x3DC5F5 (far). [0x60B]=4 (the active-channel-count reload sound_tick starts each frame from),
// [0x601]=AL (the caller's gate-bits seed -- always 0 at sound_request's one call site, right after
// sound_mute_output has forced AL to 0). Paired with sound_request_end at the end of a request.
void sound_request_begin_core(Machine &m, uint8_t al) {
    m.wr8(m.ds(), kChCountReload, 4);
    m.wr8(m.ds(), kGateBits, al);
}

// rom: 0x3DC5EA (far). [0x60B]=0, [0x601]=0: the reload/gate-bits reset that closes out a sound_request.
void sound_request_end_core(Machine &m) {
    m.wr8(m.ds(), kChCountReload, 0);
    m.wr8(m.ds(), kGateBits, 0);
}

// rom: 0x3DD050 (far). Saves port 0x90 to [0x61E] and writes 0 (silences output while a request is being
// wired up). AL always ends 0 (`and al,0`); AH is never touched.
void sound_mute_output_core(Machine &m) {
    uint8_t channel_control = m.in8(0x90);
    m.wr8(m.ds(), kPort90Backup, channel_control);
    m.out8(0x90, 0);
}

// rom: 0x3DD05A (far). Restores port 0x90 from [0x61E] (the sound_mute_output backup).
void sound_unmute_output_core(Machine &m) { m.out8(0x90, m.rd8(m.ds(), kPort90Backup)); }

// rom: 0x3DCEF4 (near). Zeroes kChStride (0x32) bytes starting at `base` -- a whole logical-channel slot.
// BX/CX preserved (pushed/popped in the original; already the shape of the existing l3dcef4_clear helper).

// rom: 0x3DC6E8/0x3DC6EB (far, shared tail of sound_reserve_channels_1..4, reached by falling through
// rather than a `call`). Walks `count` records of the per-logical-channel table D_3D0500 (6 bytes each:
// word channel-struct offset, byte hw-index/flags [stored from only the low byte of a table word], word
// track pointer), starting at record index [0x606] (seeded from the entry AX -- the "unused" word
// sound_dispatch_record loaded from the dispatch record). For each one: if the logical-channel slot it
// points at is still active (step counter != 0xFFFF), clears that hw channel's gate bits (a 2-bit-per-index
// mask picked by the slot's current hw-index/flags byte) so a channel being reassigned doesn't keep
// sounding; then clears the whole slot (sound_clear_channel_slot) and re-primes it from the table's
// hw-index/flags and track-pointer fields. The whole body is bracketed by pushaw/push es/push ds ...
// popaw/pop ds/pop es in the original, so every GPR and DS/ES comes back exactly as it went in; this helper
// therefore never touches the Machine's registers, only RAM. Original quirk (only reachable with a garbage
// record index -- never from a real dispatch, where it's always small): the loop counter isn't kept in a
// register across sound_clear_channel_slot's `call` -- each iteration re-reads it from [0x606] and adds 1
// (`mov ax, word ptr [0x606]; inc ax`) -- so if the channel-struct offset this iteration read from the ROM
// table is small/wrapped enough that sound_clear_channel_slot's 0x32-byte clear lands on [0x606] itself,
// the next iteration's index is whatever that clear left (0), not the correctly incremented one. Reproduced
// by re-reading RAM here too instead of tracking the index in a local.
void sound_reserve_tail_core(Machine &m, uint8_t count) {
    uint16_t ds = m.ds();
    uint16_t idx = m.ax();
    for (uint8_t iter = 0; iter < count; ++iter) {
        m.wr16(ds, 0x606, idx);
        uint16_t di = uint16_t(kD3D0500 + idx * 6);
        uint16_t bx = uint16_t(kChBase + m.rd16(kBank, di));
        di = uint16_t(di + 2);
        if (m.rd16(ds, bx) != 0xFFFF) {
            uint8_t flags = m.rd8(ds, uint16_t(bx + 2));
            uint8_t cl;
            switch (flags & 3) {
            case 0:
                cl = 0xEE;
                break;
            case 1:
                cl = 0xDD;
                break;
            case 2:
                cl = 0xBB;
                break;
            default:
                cl = 0x77;
                break;
            }
            m.wr8(ds, kGateBits, uint8_t(m.rd8(ds, kGateBits) & cl));
        }
        l3dcef4_clear(m, bx);
        m.wr8(ds, uint16_t(bx + 2), lo(m.rd16(kBank, di)));
        di = uint16_t(di + 2);
        m.wr16(ds, uint16_t(bx + 3), m.rd16(kBank, di));
        // ADD DI,2 and INC AX leave live flags for computed menu-choice callers.
        (void)computed::add<uint16_t>(m, uint16_t(di - 2), 2);
        idx = computed::increment<uint16_t>(m, m.rd16(ds, 0x606)); // reload after the clear
    }
}

// rom: 0x3DC682 (far) and its near-identical siblings 0x3DC69F/0x3DC6BA/0x3DC6D3 (3/2/1 channels): reserves
// `count` hw channels for a new sound_request (music) call -- records the voice mask in [0x60E] and
// silences hw volume ports 0x88..0x88+count-1 -- then falls into the shared per-record loop above. Skipped
// for play_sound_effect calls ([0x627]!=0): those never reset a hw channel that may still be mid-note.
// CX ends at `count` (set here, never touched again -- the tail's pushaw/popaw restores every other GPR).
void sound_reserve_channels_core(Machine &m, uint8_t count, uint8_t voice_mask) {
    m.cx() = count;
    if (m.rd8(m.ds(), kReqFlag) == 0) {
        m.wr8(m.ds(), kVoiceSlot, voice_mask);
        for (uint8_t i = 0; i < count; ++i)
            m.out8(uint8_t(0x88 + i), 0);
    }
    sound_reserve_tail_core(m, count);
}

// rom: 0x3DD45F (near). Shared dispatcher for sound_request/play_sound_effect: DI = pointer (within this
// bank's own D000 segment) to the request/effect table, DL = request/effect id (only the low byte of DX
// matters). Reads the 3-byte record at DI + id*3 (a word field that's loaded into AX but never used beyond
// that -- reproduced as a leftover -- and a type byte 1-4 selecting how many hw channels the new note
// needs) and dispatches to the matching sound_reserve_channels_N; type 0 does nothing. DI ends at
// table + id*3 + 2 (pointing at the type byte); CH=0, CL = the type byte; DX ends at id*3 (`mov dx,ax`
// after the `mul ah` that computes the record offset -- never restored, unlike DI/AX).
void sound_dispatch_record_core(Machine &m) {
    uint8_t id = lo(m.dx());
    const bool immediate = m.di() == kD3D0316;
    uint16_t offset = uint16_t(uint16_t(id) * 3);
    m.dx() = offset;
    uint16_t di = uint16_t(m.di() + offset);
    uint16_t ax = m.rd16(kBank, di);
    di = uint16_t(di + 2);
    uint8_t type = m.rd8(kBank, di);
    m.ax() = ax;
    m.di() = di;
    set_lo(m.cx(), type);
    set_hi(m.cx(), 0);
    (void)computed::logic<uint8_t>(m, type);
    if (type > 4)
        (void)computed::subtract<uint8_t>(m, type, 1);
    switch (type) {
    case 4:
        sound_reserve_channels_core(m, 4, 0xF);
        break;
    case 3:
        sound_reserve_channels_core(m, 3, 7);
        break;
    case 2:
        sound_reserve_channels_core(m, 2, 3);
        break;
    case 1:
        sound_reserve_channels_core(m, 1, 1);
        break;
    default:
        break;
    }
    if (type >= 1 && type <= 4) {
        uint8_t slots = 0;
        for (int i = 0; i < type; ++i) {
            const uint16_t offset = m.rd16(kBank, uint16_t(kD3D0500 + (ax + i) * 6));
            if (offset % kChStride == 0 && offset / kChStride < 8)
                slots |= uint8_t(1 << (offset / kChStride));
        }
        hooks::emit(hooks::Event::SoundRequest, m, {id, immediate, slots});
    }
}

// Shared body of ROM 0x3DD0DB (far). AL = mode: 0 restores each channel-4-7 group's pitch base from its
// backup slot (sound_channel_reset_al0); nonzero instead (re)seeds the group's pitch-sweep-depth fields from
// CL/CH and (unless AL==1) backs up their current pitch base, clamped by AH (sound_channel_reset_alN). See
// both for detail; cli/sti bracket the whole routine (sti unconditionally sets IF).
void sound_channel_reset_core(Machine &m, uint8_t al, uint8_t cl, uint8_t ch, uint8_t ah) {
    if (al == 0)
        sound_channel_reset_al0(m);
    else
        sound_channel_reset_alN(m, al, cl, ch, ah);
}

// Shared body of ROM 0x3DD434 (far). AL = effect id (the entry AX is carried through as DX for
// sound_dispatch_record, which only reads DL). Sets [0x627]=1 (an "immediate effect", as opposed to
// sound_request's queued request) and dispatches through sound_dispatch_record with the effect table
// D_3D0316. The original brackets the whole body in a full register save/restore (no cli/sti, unlike
// sound_request) -- so, from the caller's side, every GPR, DS and DS/ES/SS comes back exactly as it went in;
// only RAM/IO and whatever flags sound_dispatch_record leaves are observable (no pushf either, so flags pass
// through unrestored). DS is forced to 0 for the body (`mov ax,0; mov ds,ax`, unlike the per-tick helpers
// this file's header notes already run with DS=0 -- this is a public entry point callable from any bank) and
// restored before retf.
void play_sound_effect_core_impl(Machine &m, uint8_t id) {
    uint16_t ds0 = m.ds();
    uint16_t ax0 = m.ax(), bx0 = m.bx(), cx0 = m.cx(), dx0 = m.dx();
    uint16_t si0 = m.si(), di0 = m.di(), bp0 = m.bp();

    m.ds() = 0;
    m.wr8(0, kReqFlag, 1);
    m.di() = kD3D0316;
    m.dx() = id;
    sound_dispatch_record_core(m);

    m.ax() = ax0;
    m.bx() = bx0;
    m.cx() = cx0;
    m.dx() = dx0;
    m.si() = si0;
    m.di() = di0;
    m.bp() = bp0;
    m.ds() = ds0;
}

// Shared body of ROM 0x3DD36F (far). Sound driver entry for music/background-SFX requests (called once per
// frame, via wait_frames/wait_vblank's forwarding). AL = request id (as play_sound_effect, carried through as
// DX). Brackets the work in cli/sti and a full register save/restore, like play_sound_effect (DS is likewise
// forced to 0 for the body and restored before retf -- see its comment), plus an extra bracket
// play_sound_effect skips: sound_mute_output/sound_unmute_output (silence port 0x90 while the request is
// wired up), sound_request_begin/sound_request_end (reload the channel count / seed and clear the gate
// bits), and sound_channel_reset(AL=0) (restore/back the channel-4-7 envelopes for a fresh request) around
// the sound_dispatch_record call, using the music/request table D_3D02E0.
void sound_request_impl(Machine &m) {
    uint16_t ds0 = m.ds();
    uint8_t id = m.al();
    hooks::emit(hooks::Event::MusicRequest, m, {id}); // a notification only (the audio swap follows songs)
    uint16_t ax0 = m.ax(), bx0 = m.bx(), cx0 = m.cx(), dx0 = m.dx();
    uint16_t si0 = m.si(), di0 = m.di(), bp0 = m.bp();

    m.ds() = 0;
    m.wr8(0, kReqFlag, 0);
    sound_mute_output_core(m);      // AL is 0 after this (`and al,0`)
    sound_request_begin_core(m, 0); // AL is 0 here (from the ds=0 load at entry, still untouched)
    sound_channel_reset_al0(m);     // sound_channel_reset with AL=0
    m.di() = kD3D02E0;
    m.dx() = id;
    sound_dispatch_record_core(m);
    sound_request_end_core(m);
    sound_unmute_output_core(m);

    m.ax() = ax0;
    m.bx() = bx0;
    m.cx() = cx0;
    m.dx() = dx0;
    m.si() = si0;
    m.di() = di0;
    m.bp() = bp0;
    m.ds() = ds0;
    m.flags() = uint16_t(m.flags() | emu::IF);
}

// rom: 0x3DC60C (far). Hardware/state init for the sound driver: clears the fade-out bit-scan state
// (sound_init_fade_state with CX=0), silences all 4 PSG channels' volume/pitch/sweep/noise ports, sets up
// output-control port 0x91/0x90 per the headphone-jack-style flag [0x60F] (0x62 vs 0x40+enable-DAC-on-0x94),
// resets the whole driver state block (sound_reset_driver_state) and all 8 logical-channel slots
// (sound_clear_channel_slot), then marks every slot inactive (step counter = 0xFFFF). CX ends 0 (the
// per-channel `loop`), BX at DS:0x62C (the last explicit `mov bx,0x62c`), AX at 0xFFFF (the last literal
// load, overwriting the earlier `mov ax,3` used only for the 8-bit `out 0x8f,al`).
void sound_init_core(Machine &m) {
    uint16_t ds = m.ds();
    sound_init_fade_state_core(m, 0, 0);

    m.out8(0x88, 0);
    m.out8(0x8A, 0);
    m.out8(0x8B, 0);
    out16(m, 0x80, 0);
    out16(m, 0x82, 0);
    out16(m, 0x84, 0);
    out16(m, 0x86, 0);
    m.out8(0x8C, 0);
    m.out8(0x8D, 0);

    if (m.rd8(ds, 0x60F) == 0) {
        m.out8(0x89, 0);
        m.out8(0x90, 0x40);
        m.out8(0x94, 0);
    } else {
        m.out8(0x90, 0x62);
    }
    m.out8(0x91, 6);
    m.out8(0x8E, 0x18);
    m.out8(0x8F, 3);

    sound_reset_driver_state_core(m);
    uint16_t bx = kChBase;
    for (int i = 0; i < 8; ++i) {
        l3dcef4_clear(m, bx);
        bx = uint16_t(bx + kChStride);
    }
    for (int i = 0; i < 8; ++i)
        m.wr16(ds, uint16_t(kChBase + i * kChStride), 0xFFFF);

    m.cx() = 0;
    m.bx() = kChBase;
    m.ax() = 0xFFFF;
    hooks::emit(hooks::Event::SoundReset, m);
}
} // namespace

void sound_reset_pending_duty_overrides(Machine &m) {
    sound_reset_pending_duty_overrides_core(m);
    m.ret();
}

void sound_mark_note_ended(Machine &m) {
    sound_mark_note_ended_core(m);
    m.retf();
}

void sound_apply_pending_duty_overrides(Machine &m) {
    sound_apply_pending_duty_overrides_core(m);
    m.retf();
}

void sound_update_channel_volume(Machine &m) {
    sound_update_channel_volume_core(m);
    m.retf();
}

void sound_update_channel_vibrato(Machine &m) {
    sound_update_channel_vibrato_core(m);
    m.retf();
}

void sound_compute_tied_note_volume(Machine &m) {
    sound_compute_tied_note_volume_core(m);
    m.retf();
}

void sound_advance_shared_portamento(Machine &m) {
    sound_advance_shared_portamento_core(m);
    m.retf();
}

void sound_decode_track_commands(Machine &m) {
    sound_decode_track_commands_core(m);
    m.ret();
}

void sound_tick(Machine &m) {
    sound_tick_core(m);
    m.retf();
}

void sound_init_fade_state(Machine &m) {
    sound_init_fade_state_core(m, lo(m.cx()), hi(m.cx()));
    m.retf();
}

void sound_init(Machine &m) {
    sound_init_core(m);
    m.retf();
}

void sound_request_begin(Machine &m) {
    sound_request_begin_core(m, m.al());
    m.retf();
}

void sound_request_end(Machine &m) {
    sound_request_end_core(m);
    m.retf();
}

void sound_mute_output(Machine &m) {
    sound_mute_output_core(m);
    m.set_al(0);
    m.retf();
}

void sound_unmute_output(Machine &m) {
    uint8_t channel_control = m.rd8(m.ds(), kPort90Backup);
    sound_unmute_output_core(m);
    m.set_al(channel_control);
    m.retf();
}

void sound_reserve_channels_4(Machine &m) {
    sound_reserve_channels_core(m, 4, 0xF);
    m.retf();
}

void sound_reserve_channels_3(Machine &m) {
    sound_reserve_channels_core(m, 3, 7);
    m.retf();
}

void sound_reserve_channels_2(Machine &m) {
    sound_reserve_channels_core(m, 2, 3);
    m.retf();
}

void sound_reserve_channels_1(Machine &m) {
    sound_reserve_channels_core(m, 1, 1);
    m.retf();
}

void sound_dispatch_record(Machine &m) {
    sound_dispatch_record_core(m);
    m.ret();
}

void sound_reset_driver_state(Machine &m) {
    sound_reset_driver_state_core(m);
    m.ax() = 0;
    m.ret();
}

void sound_clear_channel_slot(Machine &m) {
    l3dcef4_clear(m, m.bx());
    m.ret();
}

void sound_silence_channel(Machine &m) {
    l3dcde3(m);
    m.ret();
}

void sound_deactivate_channel(Machine &m) {
    l3dce19(m);
    m.ret();
}

void sound_update_noise_gate(Machine &m) {
    l3dce3c(m);
    m.ret();
}

void sound_channel_reset_entry_core(Machine &m) {
    // DS (and ES, unused inside the body) are forced to 0 for the duration (`mov ax,0; mov ds,ax; mov
    // es,ax`) -- this is a public entry point, not one of the per-tick helpers already guaranteed DS=0 --
    // and restored via the original's pop ds before retf.
    uint16_t ds0 = m.ds();
    uint8_t al = m.al(), cl = lo(m.cx()), ch = hi(m.cx()), ah = m.ah();
    m.ds() = 0;
    sound_channel_reset_core(m, al, cl, ch, ah);
    m.ds() = ds0;
    return;
}

// rom: 0x3DD0DB
void sound_channel_reset(Machine &m) {
    sound_channel_reset_entry_core(m);
    m.retf();
}

void sound_request_core(Machine &m) {
    sound_request_impl(m);
    return;
}

// rom: 0x3DD36F
void sound_request(Machine &m) {
    sound_request_core(m);
    m.retf();
}

void play_sound_effect_entry_core(Machine &m) {
    play_sound_effect_core_impl(m, m.al());
    return;
}

// rom: 0x3DD434
void play_sound_effect(Machine &m) {
    play_sound_effect_entry_core(m);
    m.retf();
}

void play_sound_effect_core(Machine &m, uint8_t id) { play_sound_effect_core_impl(m, id); }

// rom: 0x3DCE0D (near)
// Test whether the current channel is reserved; if so discard this return and return from its caller.
// Unregistered: the original has a nonlocal return or exceeds the differential-test return limit.
void guard_reserved_sound_channel(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ds(), kHwPairMask) & m.rd8(m.ds(), kRetrigMask)));
    if (m.al()) {
        m.ax() = m.rd16(m.ss(), m.sp());
        m.sp() = uint16_t(m.sp() + 2);
    }
    m.ret();
}
} // namespace sawgod::game
