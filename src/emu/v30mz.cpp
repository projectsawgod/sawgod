// NEC V30MZ interpreter. See v30mz.h.
//
// Timing: the per-form cycle counts in execute() are the WSdev wiki's figures and are only a fallback. The
// timing actually charged comes from a model fitted to external CPU traces (the fitted timing table):
// instruction bytes come through an 8-byte prefetch queue that fetches one aligned word per cycle, sharing
// the bus with data accesses (one cycle per access; unaligned words take two plus a penalty cycle);
// execution's internal cycles (kTimingTable, per instruction form) overlap with prefetching; a taken branch
// flushes the queue. Port accesses have per-port wait states; interrupts cost kIrqCycles.
#include "emu/v30mz.h"

#include <cstdio>
#include <cstdlib>

namespace sawgod::emu {

namespace {

struct TimingEntry {
    uint8_t prefix, op, modrm, taken, after_branch;
    int8_t cycles;
};
#include "emu/v30mz_timing.inc"

// opcodes that take a ModRM byte (as keyed in the timing table)
constexpr bool has_modrm_op(int op) {
    return (op < 0x40 && (op & 7) < 4) || (op >= 0x80 && op <= 0x8F) || op == 0x62 || op == 0x69 ||
           op == 0x6B || op == 0xC0 || op == 0xC1 || (op >= 0xC4 && op <= 0xC7) ||
           (op >= 0xD0 && op <= 0xD3) || op == 0xF6 || op == 0xF7 || op == 0xFE || op == 0xFF;
}

struct TimingLookup {
    int8_t t[4][256][17][2][2]; // [prefix][op][modrm][taken][after branch]
    TimingLookup() {
        for (auto &a : t)
            for (auto &b : a)
                for (auto &c : b)
                    for (auto &d : c)
                        d[0] = d[1] = -128;
        for (const TimingEntry &e : kTimingTable)
            t[e.prefix][e.op][e.modrm][e.taken][e.after_branch] = e.cycles;
    }
};
const TimingLookup kTiming;

// Port accesses take a bus cycle; reference engine adds 2 for the sound control ports 0x90-0x9F and 1 for the
// cartridge's ports (0xC0 and up).
int port_wait(uint16_t port) {
    uint16_t p = port & 0xFF;
    return (p >= 0x90 && p <= 0x9F) ? 2 : p >= 0xC0 ? 1 : 0;
}

bool parity(uint8_t v) {
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return !(v & 1);
}

} // namespace

void V30MZ::reset() {
    s = CpuState{};
    s.seg[CS] = 0xFFFF;
    s.ip = 0;
    seg_override_ = -1;
    rep_ = 0;
    in_prefix_ = false;
    irq_inhibit_ = false;
    q_head_ = ~0u;
    after_branch_ = true;
}

// ---------------------------------------------------------------------------------------------------------------
// memory helpers

uint8_t V30MZ::fetch8() {
    ++n_fetch_;
    fetching = true;
    uint8_t v = bus_.read8(linear(s.seg[CS], s.ip));
    fetching = false;
    s.ip++;
    return v;
}

uint16_t V30MZ::fetch16() {
    uint16_t lo = fetch8();
    return lo | (fetch8() << 8);
}

// A word access: one bus cycle on the 16-bit buses (internal RAM, ROM, I/O) when aligned; two plus a penalty
// cycle when not; two on the 8-bit SRAM bus.
void V30MZ::count_word(uint32_t addr, bool io) {
    if (io || addr < 0x10000 || addr >= 0x20000) {
        if (addr & 1) {
            n_bus_ += 2;
            ++n_penalty_;
        } else {
            n_bus_ += 1;
        }
    } else {
        n_bus_ += 2;
    }
}

uint16_t V30MZ::rd16(uint16_t seg, uint16_t off) {
    count_word(linear(seg, off), false);
    uint16_t lo = bus_.read8(linear(seg, off));
    return lo | (bus_.read8(linear(seg, uint16_t(off + 1))) << 8);
}

void V30MZ::wr16(uint16_t seg, uint16_t off, uint16_t v) {
    count_word(linear(seg, off), false);
    bus_.write8(linear(seg, off), v & 0xFF);
    bus_.write8(linear(seg, uint16_t(off + 1)), v >> 8);
}

uint16_t V30MZ::in16(uint16_t port) {
    count_word(port, true);
    n_bus_ += port_wait(port);
    uint16_t lo = bus_.in8(port);
    return lo | (bus_.in8(uint16_t(port + 1)) << 8);
}

void V30MZ::out16(uint16_t port, uint16_t v) {
    count_word(port, true);
    n_bus_ += port_wait(port);
    bus_.out8(port, v & 0xFF);
    bus_.out8(uint16_t(port + 1), v >> 8);
}

int V30MZ::internal_cycles(bool taken, bool repeat) const {
    int prefix = repeat ? 0 : (seg_override_ >= 0 ? 1 : 0) | (rep_ ? 2 : 0);
    int mc = 0;
    if (has_modrm_op(op_))
        mc = (modrm_ >> 6) == 3 ? 9 + ((modrm_ >> 3) & 7) : 1 + ((modrm_ >> 3) & 7);
    const auto &f = kTiming.t[prefix][op_][mc];
    // the form as seen, then the nearest seen variant
    const int order[4][2] = {
        {taken, after_branch_}, {taken, !after_branch_}, {!taken, after_branch_}, {!taken, !after_branch_}};
    for (const auto &o : order)
        if (f[o[0]][o[1]] != -128)
            return f[o[0]][o[1]] < 0 ? 0 : f[o[0]][o[1]];
    int v = clk_ - 1; // form never seen by the fit: the wiki figure, less the bus cycle the model adds
    return v < 0 ? 0 : v;
}

void V30MZ::push(uint16_t v) {
    s.r[SP] -= 2;
    wr16(s.seg[SS], s.r[SP], v);
}

uint16_t V30MZ::pop() {
    uint16_t v = rd16(s.seg[SS], s.r[SP]);
    s.r[SP] += 2;
    return v;
}

// ---------------------------------------------------------------------------------------------------------------
// ModRM

void V30MZ::decode_modrm() {
    modrm_ = fetch8();
    int mod = modrm_ >> 6, rm = modrm_ & 7;
    if (mod == 3) {
        ea_mem_ = false;
        return;
    }
    ea_mem_ = true;
    uint16_t off = 0;
    int seg = DS;
    switch (rm) {
    case 0:
        off = s.r[BX] + s.r[SI];
        break;
    case 1:
        off = s.r[BX] + s.r[DI];
        break;
    case 2:
        off = s.r[BP] + s.r[SI];
        seg = SS;
        break;
    case 3:
        off = s.r[BP] + s.r[DI];
        seg = SS;
        break;
    case 4:
        off = s.r[SI];
        break;
    case 5:
        off = s.r[DI];
        break;
    case 6:
        if (mod == 0)
            off = fetch16();
        else {
            off = s.r[BP];
            seg = SS;
        }
        break;
    case 7:
        off = s.r[BX];
        break;
    }
    if (mod == 1)
        off += int8_t(fetch8());
    else if (mod == 2)
        off += fetch16();
    ea_off_ = off;
    ea_seg_ = seg_override_ >= 0 ? s.seg[seg_override_] : s.seg[seg];
}

uint8_t V30MZ::rm8() { return ea_mem_ ? rd8(ea_seg_, ea_off_) : reg8(modrm_ & 7); }
uint16_t V30MZ::rm16() { return ea_mem_ ? rd16(ea_seg_, ea_off_) : s.r[modrm_ & 7]; }

void V30MZ::set_rm8(uint8_t v) {
    if (ea_mem_)
        wr8(ea_seg_, ea_off_, v);
    else
        set_reg8(modrm_ & 7, v);
}

void V30MZ::set_rm16(uint16_t v) {
    if (ea_mem_)
        wr16(ea_seg_, ea_off_, v);
    else
        s.r[modrm_ & 7] = v;
}

// ---------------------------------------------------------------------------------------------------------------
// ALU

void V30MZ::set_szp8(uint8_t v) {
    set_flag(SF, v & 0x80);
    set_flag(ZF, v == 0);
    set_flag(PF, parity(v));
}

void V30MZ::set_szp16(uint16_t v) {
    set_flag(SF, v & 0x8000);
    set_flag(ZF, v == 0);
    set_flag(PF, parity(v & 0xFF));
}

// op: 0 ADD, 1 OR, 2 ADC, 3 SBB, 4 AND, 5 SUB, 6 XOR, 7 CMP (the ModRM /r numbering of opcodes 80-83)
uint8_t V30MZ::alu8(int op, uint8_t a, uint8_t b) {
    unsigned r;
    switch (op) {
    case 0:
    case 2: {
        unsigned c = (op == 2 && flag(CF)) ? 1 : 0;
        r = a + b + c;
        set_flag(CF, r > 0xFF);
        set_flag(OF, (r ^ a) & (r ^ b) & 0x80);
        set_flag(AF, (a ^ b ^ r) & 0x10);
        break;
    }
    case 3:
    case 5:
    case 7: {
        unsigned c = (op == 3 && flag(CF)) ? 1 : 0;
        r = a - b - c;
        set_flag(CF, r > 0xFF);
        set_flag(OF, (a ^ b) & (a ^ r) & 0x80);
        set_flag(AF, (a ^ b ^ r) & 0x10);
        break;
    }
    default:
        r = op == 1 ? (a | b) : op == 4 ? (a & b) : (a ^ b);
        set_flag(CF, false);
        set_flag(OF, false);
        set_flag(AF, false);
        break;
    }
    set_szp8(uint8_t(r));
    return op == 7 ? a : uint8_t(r);
}

uint16_t V30MZ::alu16(int op, uint16_t a, uint16_t b) {
    uint32_t r;
    switch (op) {
    case 0:
    case 2: {
        uint32_t c = (op == 2 && flag(CF)) ? 1 : 0;
        r = uint32_t(a) + b + c;
        set_flag(CF, r > 0xFFFF);
        set_flag(OF, (r ^ a) & (r ^ b) & 0x8000);
        set_flag(AF, (a ^ b ^ r) & 0x10);
        break;
    }
    case 3:
    case 5:
    case 7: {
        uint32_t c = (op == 3 && flag(CF)) ? 1 : 0;
        r = uint32_t(a) - b - c;
        set_flag(CF, r > 0xFFFF);
        set_flag(OF, (a ^ b) & (a ^ r) & 0x8000);
        set_flag(AF, (a ^ b ^ r) & 0x10);
        break;
    }
    default:
        r = op == 1 ? (a | b) : op == 4 ? (a & b) : (a ^ b);
        set_flag(CF, false);
        set_flag(OF, false);
        set_flag(AF, false);
        break;
    }
    set_szp16(uint16_t(r));
    return op == 7 ? a : uint16_t(r);
}

uint8_t V30MZ::inc8(uint8_t v) {
    uint8_t r = v + 1;
    set_flag(OF, r == 0x80);
    set_flag(AF, (r & 0x0F) == 0);
    set_szp8(r);
    return r;
}

uint8_t V30MZ::dec8(uint8_t v) {
    uint8_t r = v - 1;
    set_flag(OF, r == 0x7F);
    set_flag(AF, (r & 0x0F) == 0x0F);
    set_szp8(r);
    return r;
}

uint16_t V30MZ::inc16(uint16_t v) {
    uint16_t r = v + 1;
    set_flag(OF, r == 0x8000);
    set_flag(AF, (r & 0x0F) == 0);
    set_szp16(r);
    return r;
}

uint16_t V30MZ::dec16(uint16_t v) {
    uint16_t r = v - 1;
    set_flag(OF, r == 0x7FFF);
    set_flag(AF, (r & 0x0F) == 0x0F);
    set_szp16(r);
    return r;
}

// op: 0 ROL, 1 ROR, 2 RCL, 3 RCR, 4 SHL, 5 SHR, 6 SHL (undocumented alias), 7 SAR.
// The count is masked to 5 bits (80186 behaviour); a zero count changes nothing.
uint8_t V30MZ::shift8(int op, uint8_t v, int count) {
    count &= 0x1F;
    if (count == 0)
        return v;
    unsigned r = v;
    bool cf = flag(CF);
    for (int i = 0; i < count; ++i) {
        switch (op) {
        case 0:
            cf = r & 0x80;
            r = ((r << 1) | cf) & 0xFF;
            break;
        case 1:
            cf = r & 1;
            r = (r >> 1) | (cf ? 0x80 : 0);
            break;
        case 2: {
            bool out = r & 0x80;
            r = ((r << 1) | cf) & 0xFF;
            cf = out;
            break;
        }
        case 3: {
            bool out = r & 1;
            r = (r >> 1) | (cf ? 0x80 : 0);
            cf = out;
            break;
        }
        case 4:
        case 6:
            cf = r & 0x80;
            r = (r << 1) & 0xFF;
            break;
        case 5:
            cf = r & 1;
            r >>= 1;
            break;
        case 7:
            cf = r & 1;
            r = (r >> 1) | (r & 0x80);
            break;
        }
    }
    set_flag(CF, cf);
    switch (op) {
    case 0:
    case 2:
    case 4:
    case 6:
        set_flag(OF, bool(r & 0x80) != cf);
        break;
    case 1:
    case 3:
        set_flag(OF, ((r >> 7) ^ (r >> 6)) & 1);
        break;
    case 5:
        set_flag(OF, count == 1 ? (v & 0x80) : false);
        break;
    case 7:
        set_flag(OF, false);
        break;
    }
    if (op >= 4) {
        set_szp8(uint8_t(r));
        set_flag(AF, false);
    }
    return uint8_t(r);
}

uint16_t V30MZ::shift16(int op, uint16_t v, int count) {
    count &= 0x1F;
    if (count == 0)
        return v;
    uint32_t r = v;
    bool cf = flag(CF);
    for (int i = 0; i < count; ++i) {
        switch (op) {
        case 0:
            cf = r & 0x8000;
            r = ((r << 1) | cf) & 0xFFFF;
            break;
        case 1:
            cf = r & 1;
            r = (r >> 1) | (cf ? 0x8000 : 0);
            break;
        case 2: {
            bool out = r & 0x8000;
            r = ((r << 1) | cf) & 0xFFFF;
            cf = out;
            break;
        }
        case 3: {
            bool out = r & 1;
            r = (r >> 1) | (cf ? 0x8000 : 0);
            cf = out;
            break;
        }
        case 4:
        case 6:
            cf = r & 0x8000;
            r = (r << 1) & 0xFFFF;
            break;
        case 5:
            cf = r & 1;
            r >>= 1;
            break;
        case 7:
            cf = r & 1;
            r = (r >> 1) | (r & 0x8000);
            break;
        }
    }
    set_flag(CF, cf);
    switch (op) {
    case 0:
    case 2:
    case 4:
    case 6:
        set_flag(OF, bool(r & 0x8000) != cf);
        break;
    case 1:
    case 3:
        set_flag(OF, ((r >> 15) ^ (r >> 14)) & 1);
        break;
    case 5:
        set_flag(OF, count == 1 ? (v & 0x8000) : false);
        break;
    case 7:
        set_flag(OF, false);
        break;
    }
    if (op >= 4) {
        set_szp16(uint16_t(r));
        set_flag(AF, false);
    }
    return uint16_t(r);
}

bool V30MZ::condition(int cc) const {
    bool r;
    switch (cc >> 1) {
    case 0:
        r = flag(OF);
        break;
    case 1:
        r = flag(CF);
        break;
    case 2:
        r = flag(ZF);
        break;
    case 3:
        r = flag(CF) || flag(ZF);
        break;
    case 4:
        r = flag(SF);
        break;
    case 5:
        r = flag(PF);
        break;
    case 6:
        r = flag(SF) != flag(OF);
        break;
    default:
        r = flag(ZF) || (flag(SF) != flag(OF));
        break;
    }
    return (cc & 1) ? !r : r;
}

// F6: TEST/NOT/NEG/MUL/IMUL/DIV/IDIV on a byte
void V30MZ::group3_8() {
    uint8_t v = rm8();
    switch (modrm_reg()) {
    case 0:
    case 1:
        alu8(4, v, fetch8());
        clk_ += mr(2, 1);
        break;
    case 2:
        set_rm8(~v);
        clk_ += mr(3, 1);
        break;
    case 3: {
        uint8_t r = alu8(5, 0, v);
        set_flag(CF, v != 0);
        set_rm8(r);
        clk_ += mr(3, 1);
        break;
    }
    case 4: {
        uint16_t r = uint16_t(reg8(0)) * v;
        s.r[AX] = r;
        set_flag(CF, r >> 8);
        set_flag(OF, r >> 8);
        clk_ += mr(4, 3);
        break;
    }
    case 5: {
        int16_t r = int16_t(int8_t(reg8(0))) * int8_t(v);
        s.r[AX] = uint16_t(r);
        bool ext = r != int8_t(r);
        set_flag(CF, ext);
        set_flag(OF, ext);
        clk_ += mr(4, 3);
        break;
    }
    case 6: {
        clk_ += mr(16, 15);
        if (v == 0) {
            raise(0);
            break;
        }
        unsigned q = s.r[AX] / v, rem = s.r[AX] % v;
        if (q > 0xFF) {
            raise(0);
            break;
        }
        s.r[AX] = uint16_t((rem << 8) | q);
        break;
    }
    case 7: {
        clk_ += mr(18, 17);
        if (v == 0) {
            raise(0);
            break;
        }
        int a = int16_t(s.r[AX]), d = int8_t(v);
        int q = a / d, rem = a % d;
        if (q > 127 || q < -128) {
            raise(0);
            break;
        }
        s.r[AX] = uint16_t(((rem & 0xFF) << 8) | (q & 0xFF));
        break;
    }
    }
}

void V30MZ::group3_16() {
    uint16_t v = rm16();
    switch (modrm_reg()) {
    case 0:
    case 1:
        alu16(4, v, fetch16());
        clk_ += mr(2, 1);
        break;
    case 2:
        set_rm16(~v);
        clk_ += mr(3, 1);
        break;
    case 3: {
        uint16_t r = alu16(5, 0, v);
        set_flag(CF, v != 0);
        set_rm16(r);
        clk_ += mr(3, 1);
        break;
    }
    case 4: {
        uint32_t r = uint32_t(s.r[AX]) * v;
        s.r[AX] = uint16_t(r);
        s.r[DX] = uint16_t(r >> 16);
        set_flag(CF, r >> 16);
        set_flag(OF, r >> 16);
        clk_ += mr(4, 3);
        break;
    }
    case 5: {
        int32_t r = int32_t(int16_t(s.r[AX])) * int16_t(v);
        s.r[AX] = uint16_t(r);
        s.r[DX] = uint16_t(uint32_t(r) >> 16);
        bool ext = r != int16_t(r);
        set_flag(CF, ext);
        set_flag(OF, ext);
        clk_ += mr(4, 3);
        break;
    }
    case 6: {
        clk_ += mr(24, 23);
        if (v == 0) {
            raise(0);
            break;
        }
        uint32_t a = (uint32_t(s.r[DX]) << 16) | s.r[AX];
        uint32_t q = a / v, rem = a % v;
        if (q > 0xFFFF) {
            raise(0);
            break;
        }
        s.r[AX] = uint16_t(q);
        s.r[DX] = uint16_t(rem);
        break;
    }
    case 7: {
        clk_ += mr(25, 24);
        if (v == 0) {
            raise(0);
            break;
        }
        int32_t a = int32_t((uint32_t(s.r[DX]) << 16) | s.r[AX]);
        int32_t d = int16_t(v);
        int32_t q = a / d, rem = a % d;
        if (q > 32767 || q < -32768) {
            raise(0);
            break;
        }
        s.r[AX] = uint16_t(q);
        s.r[DX] = uint16_t(rem);
        break;
    }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// interrupts

void V30MZ::do_interrupt(uint8_t vector) {
    push(flags_word());
    s.flags &= ~(IF | TF);
    push(s.seg[CS]);
    push(s.ip);
    s.ip = rd16(0, uint16_t(vector * 4));
    s.seg[CS] = rd16(0, uint16_t(vector * 4 + 2));
}

void V30MZ::raise(uint8_t vector) { do_interrupt(vector); }

uint8_t V30MZ::io_in8(uint16_t port) {
    n_bus_ += 1 + port_wait(port);
    return bus_.in8(port);
}

void V30MZ::io_out8(uint16_t port, uint8_t v) {
    n_bus_ += 1 + port_wait(port);
    bus_.out8(port, v);
}

int V30MZ::interrupt(uint8_t vector) {
    s.halted = false;
    if (in_prefix_) { // between iterations of a repeated string instruction
        s.ip = start_ip_;
        in_prefix_ = false;
    }
    do_interrupt(vector);
    irq_inhibit_ = false;
    q_head_ = ~0u;
    after_branch_ = true;
    cycles += kIrqCycles;
    return kIrqCycles;
}

// ---------------------------------------------------------------------------------------------------------------
// string instructions (one iteration per step when repeated)

void V30MZ::string_op(uint8_t op) {
    if (rep_ && s.r[CX] == 0) {
        clk_ += 1;
        return;
    }
    int delta = flag(DF) ? -1 : 1;
    bool word = op & 1;
    int step = word ? 2 * delta : delta;
    uint16_t src = data_seg();
    bool check_z = false;
    switch (op) {
    case 0x6C: // INSB/INSW
    case 0x6D:
        if (word)
            wr16(s.seg[ES], s.r[DI], in16(s.r[DX]));
        else
            wr8(s.seg[ES], s.r[DI], io_in8(s.r[DX]));
        s.r[DI] += step;
        clk_ += 6;
        break;
    case 0x6E: // OUTSB/OUTSW
    case 0x6F:
        if (word)
            out16(s.r[DX], rd16(src, s.r[SI]));
        else
            io_out8(s.r[DX], rd8(src, s.r[SI]));
        s.r[SI] += step;
        clk_ += 6;
        break;
    case 0xA4: // MOVSB/MOVSW
    case 0xA5:
        if (word)
            wr16(s.seg[ES], s.r[DI], rd16(src, s.r[SI]));
        else
            wr8(s.seg[ES], s.r[DI], rd8(src, s.r[SI]));
        s.r[SI] += step;
        s.r[DI] += step;
        clk_ += rep_ ? 7 : 5;
        break;
    case 0xA6: // CMPSB/CMPSW
    case 0xA7:
        if (word)
            alu16(7, rd16(src, s.r[SI]), rd16(s.seg[ES], s.r[DI]));
        else
            alu8(7, rd8(src, s.r[SI]), rd8(s.seg[ES], s.r[DI]));
        s.r[SI] += step;
        s.r[DI] += step;
        check_z = true;
        clk_ += 6;
        break;
    case 0xAA: // STOSB/STOSW
    case 0xAB:
        if (word)
            wr16(s.seg[ES], s.r[DI], s.r[AX]);
        else
            wr8(s.seg[ES], s.r[DI], reg8(0));
        s.r[DI] += step;
        clk_ += rep_ ? 6 : 3;
        break;
    case 0xAC: // LODSB/LODSW
    case 0xAD:
        if (word)
            s.r[AX] = rd16(src, s.r[SI]);
        else
            set_reg8(0, rd8(src, s.r[SI]));
        s.r[SI] += step;
        clk_ += 3;
        break;
    case 0xAE: // SCASB/SCASW
    case 0xAF:
        if (word)
            alu16(7, s.r[AX], rd16(s.seg[ES], s.r[DI]));
        else
            alu8(7, reg8(0), rd8(s.seg[ES], s.r[DI]));
        s.r[DI] += step;
        check_z = true;
        clk_ += 4;
        break;
    }
    if (!rep_)
        return;
    s.r[CX]--;
    if (s.r[CX] == 0)
        return;
    if (check_z && ((rep_ == 0xF3) != flag(ZF)))
        return; // REPE stops on ZF=0, REPNE on ZF=1
    // run the instruction again next step (from its opcode, prefixes kept); an interrupt in between returns
    // to the first prefix (see interrupt())
    s.ip--;
    in_prefix_ = true;
}

// ---------------------------------------------------------------------------------------------------------------

int V30MZ::step() {
    if (s.halted) {
        cycles += 1;
        return 1;
    }
#ifdef SAWGOD_NO_INTERPRETER
    std::fprintf(stderr,
                 "SAWGOD_NO_INTERPRETER: attempted to interpret an instruction at CS:IP %04X:%04X "
                 "(linear 0x%05X); run the WSC game with --native all\n",
                 s.seg[CS], s.ip, unsigned(pc()));
    std::abort();
#endif
    bool trap = flag(TF);
    irq_inhibit_ = false;
    clk_ = 0;
    const bool repeat = in_prefix_; // a further iteration of a REP instruction: timed without its prefixes
    if (!in_prefix_) {
        seg_override_ = -1;
        rep_ = 0;
        start_ip_ = s.ip;
    }
    const uint32_t pc = this->pc();
    int t = 0;
    if (pc != q_head_) { // queue flushed (reset, interrupt): refill from here and wait for the opcode byte
        q_fetch_ = pc & ~1u;
        while (q_fetch_ <= pc) {
            q_fetch_ += 2;
            ++t;
        }
    }
    n_fetch_ = n_bus_ = n_penalty_ = 0;
    modrm_ = 0;
    uint8_t op = fetch8();
    for (;;) {
        if (op == 0x26 || op == 0x2E || op == 0x36 || op == 0x3E)
            seg_override_ = (op >> 3) & 3;
        else if (op == 0xF2 || op == 0xF3)
            rep_ = op;
        else if (op != 0xF0)
            break;
        clk_ += 1;
        op = fetch8();
    }
    op_ = op;
    in_prefix_ = false;
    execute(op);

    const uint32_t end = pc + uint32_t(n_fetch_);
    while (q_fetch_ < end) { // the rest of the instruction's bytes
        q_fetch_ += 2;
        ++t;
    }
    const uint32_t next = this->pc();
    // prefetch while executing (optionally not in the cycle after a data access: kTurnaround)
    const int blocked = kTurnaround && n_bus_ ? 1 : 0;
    const int internal = internal_cycles(next != end, repeat) + n_penalty_;
    for (int i = 0; i < internal; ++i) {
        if (i >= blocked && q_fetch_ - end <= uint32_t(kQueueBytes - 2))
            q_fetch_ += 2;
        ++t;
    }
    t += n_bus_;
    // Wait for the next opcode byte now (a taken branch refills the queue from its target), so the step's
    // cycles end where the next instruction starts, as the reference engine counts them.
    if (next != end)
        q_fetch_ = next & ~1u;
    while (q_fetch_ <= next) {
        q_fetch_ += 2;
        ++t;
    }
    q_head_ = next;
    after_branch_ = next != end;

    if (trap && flag(TF) && !s.halted)
        do_interrupt(1);
    clk_ = t;
    cycles += clk_;
    return clk_;
}

void V30MZ::execute(uint8_t op) {
    switch (op) {
    // --- ALU r/m forms and accumulator-immediate forms: 00-3F except the x6/x7/xE/xF columns
    case 0x00:
    case 0x08:
    case 0x10:
    case 0x18:
    case 0x20:
    case 0x28:
    case 0x30:
    case 0x38: {
        decode_modrm();
        int alu = op >> 3;
        uint8_t r = alu8(alu, rm8(), reg8(modrm_reg()));
        if (alu != 7)
            set_rm8(r);
        clk_ += alu == 7 ? mr(2, 1) : mr(3, 1);
        break;
    }
    case 0x01:
    case 0x09:
    case 0x11:
    case 0x19:
    case 0x21:
    case 0x29:
    case 0x31:
    case 0x39: {
        decode_modrm();
        int alu = op >> 3;
        uint16_t r = alu16(alu, rm16(), s.r[modrm_reg()]);
        if (alu != 7)
            set_rm16(r);
        clk_ += alu == 7 ? mr(2, 1) : mr(3, 1);
        break;
    }
    case 0x02:
    case 0x0A:
    case 0x12:
    case 0x1A:
    case 0x22:
    case 0x2A:
    case 0x32:
    case 0x3A: {
        decode_modrm();
        int alu = op >> 3;
        uint8_t r = alu8(alu, reg8(modrm_reg()), rm8());
        if (alu != 7)
            set_reg8(modrm_reg(), r);
        clk_ += mr(2, 1);
        break;
    }
    case 0x03:
    case 0x0B:
    case 0x13:
    case 0x1B:
    case 0x23:
    case 0x2B:
    case 0x33:
    case 0x3B: {
        decode_modrm();
        int alu = op >> 3;
        uint16_t r = alu16(alu, s.r[modrm_reg()], rm16());
        if (alu != 7)
            s.r[modrm_reg()] = r;
        clk_ += mr(2, 1);
        break;
    }
    case 0x04:
    case 0x0C:
    case 0x14:
    case 0x1C:
    case 0x24:
    case 0x2C:
    case 0x34:
    case 0x3C:
        set_reg8(0, alu8(op >> 3, reg8(0), fetch8()));
        clk_ += 1;
        break;
    case 0x05:
    case 0x0D:
    case 0x15:
    case 0x1D:
    case 0x25:
    case 0x2D:
    case 0x35:
    case 0x3D:
        s.r[AX] = alu16(op >> 3, s.r[AX], fetch16());
        clk_ += 1;
        break;

    // --- segment push/pop
    case 0x06:
    case 0x0E:
    case 0x16:
    case 0x1E:
        push(s.seg[(op >> 3) & 3]);
        clk_ += 2;
        break;
    case 0x07:
    case 0x17:
    case 0x1F:
        s.seg[(op >> 3) & 3] = pop();
        if (op == 0x17)
            irq_inhibit_ = true;
        clk_ += 3;
        break;
    case 0x0F: // POP CS on the 8086; undefined (no operation) on the V30MZ
        clk_ += 1;
        break;

    // --- BCD
    case 0x27:
    case 0x2F: { // DAA / DAS
        uint8_t al = reg8(0);
        bool old_cf = flag(CF);
        bool sub = op == 0x2F;
        if ((al & 0x0F) > 9 || flag(AF)) {
            al = sub ? al - 6 : al + 6;
            set_flag(AF, true);
        } else {
            set_flag(AF, false);
        }
        if (reg8(0) > 0x99 || old_cf) {
            al = sub ? al - 0x60 : al + 0x60;
            set_flag(CF, true);
        } else {
            set_flag(CF, false);
        }
        set_reg8(0, al);
        set_szp8(al);
        clk_ += 10;
        break;
    }
    case 0x37:
    case 0x3F: { // AAA / AAS
        if ((reg8(0) & 0x0F) > 9 || flag(AF)) {
            int d = op == 0x37 ? 1 : -1;
            set_reg8(0, uint8_t(reg8(0) + 6 * d));
            set_reg8(4, uint8_t(reg8(4) + d));
            set_flag(AF, true);
            set_flag(CF, true);
        } else {
            set_flag(AF, false);
            set_flag(CF, false);
        }
        set_reg8(0, reg8(0) & 0x0F);
        clk_ += 9;
        break;
    }

    // --- INC/DEC/PUSH/POP r16
    case 0x40:
    case 0x41:
    case 0x42:
    case 0x43:
    case 0x44:
    case 0x45:
    case 0x46:
    case 0x47:
        s.r[op & 7] = inc16(s.r[op & 7]);
        clk_ += 1;
        break;
    case 0x48:
    case 0x49:
    case 0x4A:
    case 0x4B:
    case 0x4C:
    case 0x4D:
    case 0x4E:
    case 0x4F:
        s.r[op & 7] = dec16(s.r[op & 7]);
        clk_ += 1;
        break;
    case 0x50:
    case 0x51:
    case 0x52:
    case 0x53:
    case 0x54:
    case 0x55:
    case 0x56:
    case 0x57:
        if (op == 0x54) { // PUSH SP stores the decremented value (8086/80186 behaviour)
            s.r[SP] -= 2;
            wr16(s.seg[SS], s.r[SP], s.r[SP]);
        } else {
            push(s.r[op & 7]);
        }
        clk_ += 1;
        break;
    case 0x58:
    case 0x59:
    case 0x5A:
    case 0x5B:
    case 0x5C:
    case 0x5D:
    case 0x5E:
    case 0x5F:
        s.r[op & 7] = pop();
        clk_ += 1;
        break;

    // --- 80186 additions
    case 0x60: { // PUSHA
        uint16_t sp = s.r[SP];
        push(s.r[AX]);
        push(s.r[CX]);
        push(s.r[DX]);
        push(s.r[BX]);
        push(sp);
        push(s.r[BP]);
        push(s.r[SI]);
        push(s.r[DI]);
        clk_ += 9;
        break;
    }
    case 0x61: // POPA
        s.r[DI] = pop();
        s.r[SI] = pop();
        s.r[BP] = pop();
        pop();
        s.r[BX] = pop();
        s.r[DX] = pop();
        s.r[CX] = pop();
        s.r[AX] = pop();
        clk_ += 8;
        break;
    case 0x62: { // BOUND
        decode_modrm();
        int16_t idx = int16_t(s.r[modrm_reg()]);
        int16_t lo = int16_t(rd16(ea_seg_, ea_off_));
        int16_t hi = int16_t(rd16(ea_seg_, uint16_t(ea_off_ + 2)));
        clk_ += 13;
        if (idx < lo || idx > hi) {
            clk_ += 7;
            raise(5);
        }
        break;
    }
    case 0x68:
        push(fetch16());
        clk_ += 1;
        break;
    case 0x6A:
        push(uint16_t(int8_t(fetch8())));
        clk_ += 1;
        break;
    case 0x69:
    case 0x6B: { // IMUL reg16, r/m16, imm
        decode_modrm();
        int32_t a = int16_t(rm16());
        int32_t b = op == 0x69 ? int16_t(fetch16()) : int8_t(fetch8());
        int32_t r = a * b;
        s.r[modrm_reg()] = uint16_t(r);
        bool ext = r != int16_t(r);
        set_flag(CF, ext);
        set_flag(OF, ext);
        clk_ += mr(4, 3);
        break;
    }
    case 0x6C:
    case 0x6D:
    case 0x6E:
    case 0x6F:
        string_op(op);
        break;

    // --- conditional jumps
    case 0x70:
    case 0x71:
    case 0x72:
    case 0x73:
    case 0x74:
    case 0x75:
    case 0x76:
    case 0x77:
    case 0x78:
    case 0x79:
    case 0x7A:
    case 0x7B:
    case 0x7C:
    case 0x7D:
    case 0x7E:
    case 0x7F: {
        int8_t d = int8_t(fetch8());
        if (condition(op & 0x0F)) {
            s.ip += d;
            clk_ += 4;
        } else {
            clk_ += 1;
        }
        break;
    }

    // --- immediate group
    case 0x80:
    case 0x82: {
        decode_modrm();
        int alu = modrm_reg();
        uint8_t r = alu8(alu, rm8(), fetch8());
        if (alu != 7)
            set_rm8(r);
        clk_ += alu == 7 ? mr(2, 1) : mr(3, 1);
        break;
    }
    case 0x81:
    case 0x83: {
        decode_modrm();
        int alu = modrm_reg();
        uint16_t v = rm16();
        uint16_t imm = op == 0x81 ? fetch16() : uint16_t(int8_t(fetch8()));
        uint16_t r = alu16(alu, v, imm);
        if (alu != 7)
            set_rm16(r);
        clk_ += alu == 7 ? mr(2, 1) : mr(3, 1);
        break;
    }
    case 0x84:
        decode_modrm();
        alu8(4, rm8(), reg8(modrm_reg()));
        clk_ += mr(2, 1);
        break;
    case 0x85:
        decode_modrm();
        alu16(4, rm16(), s.r[modrm_reg()]);
        clk_ += mr(2, 1);
        break;
    case 0x86: {
        decode_modrm();
        uint8_t a = rm8();
        set_rm8(reg8(modrm_reg()));
        set_reg8(modrm_reg(), a);
        clk_ += mr(5, 3);
        break;
    }
    case 0x87: {
        decode_modrm();
        uint16_t a = rm16();
        set_rm16(s.r[modrm_reg()]);
        s.r[modrm_reg()] = a;
        clk_ += mr(5, 3);
        break;
    }

    // --- MOV
    case 0x88:
        decode_modrm();
        set_rm8(reg8(modrm_reg()));
        clk_ += 1;
        break;
    case 0x89:
        decode_modrm();
        set_rm16(s.r[modrm_reg()]);
        clk_ += 1;
        break;
    case 0x8A:
        decode_modrm();
        set_reg8(modrm_reg(), rm8());
        clk_ += 1;
        break;
    case 0x8B:
        decode_modrm();
        s.r[modrm_reg()] = rm16();
        clk_ += 1;
        break;
    case 0x8C:
        decode_modrm();
        set_rm16(s.seg[modrm_reg() & 3]);
        clk_ += mr(3, 1);
        break;
    case 0x8D:
        decode_modrm();
        s.r[modrm_reg()] = ea_off_;
        clk_ += 1;
        break;
    case 0x8E: {
        decode_modrm();
        int sr = modrm_reg() & 3;
        s.seg[sr] = rm16();
        if (sr == SS)
            irq_inhibit_ = true;
        clk_ += mr(3, 2);
        break;
    }
    case 0x8F: {
        decode_modrm();
        uint16_t v = pop();
        set_rm16(v);
        clk_ += mr(3, 1);
        break;
    }

    // --- XCHG AX, r16 / misc
    case 0x90:
        clk_ += 1;
        break;
    case 0x91:
    case 0x92:
    case 0x93:
    case 0x94:
    case 0x95:
    case 0x96:
    case 0x97: {
        uint16_t t = s.r[AX];
        s.r[AX] = s.r[op & 7];
        s.r[op & 7] = t;
        clk_ += 3;
        break;
    }
    case 0x98:
        s.r[AX] = uint16_t(int8_t(reg8(0)));
        clk_ += 1;
        break;
    case 0x99:
        s.r[DX] = (s.r[AX] & 0x8000) ? 0xFFFF : 0;
        clk_ += 1;
        break;
    case 0x9A: {
        uint16_t off = fetch16(), seg = fetch16();
        push(s.seg[CS]);
        push(s.ip);
        s.seg[CS] = seg;
        s.ip = off;
        clk_ += 10;
        break;
    }
    case 0x9B:
        clk_ += 9;
        break; // WAIT: POLL is always asserted on the WonderSwan
    case 0x9C:
        push(flags_word());
        clk_ += 2;
        break;
    case 0x9D: {
        bool had_if = flag(IF);
        set_flags_word(pop());
        if (!had_if && flag(IF))
            irq_inhibit_ = true;
        clk_ += 3;
        break;
    }
    case 0x9E:
        s.flags = (s.flags & 0xFF00) | (reg8(4) & (SF | ZF | AF | PF | CF));
        clk_ += 4;
        break;
    case 0x9F:
        set_reg8(4, uint8_t(flags_word()));
        clk_ += 2;
        break;

    // --- MOV accumulator <-> memory offset
    case 0xA0:
        set_reg8(0, rd8(data_seg(), fetch16()));
        clk_ += 1;
        break;
    case 0xA1:
        s.r[AX] = rd16(data_seg(), fetch16());
        clk_ += 1;
        break;
    case 0xA2:
        wr8(data_seg(), fetch16(), reg8(0));
        clk_ += 1;
        break;
    case 0xA3:
        wr16(data_seg(), fetch16(), s.r[AX]);
        clk_ += 1;
        break;
    case 0xA4:
    case 0xA5:
    case 0xA6:
    case 0xA7:
    case 0xAA:
    case 0xAB:
    case 0xAC:
    case 0xAD:
    case 0xAE:
    case 0xAF:
        string_op(op);
        break;
    case 0xA8:
        alu8(4, reg8(0), fetch8());
        clk_ += 1;
        break;
    case 0xA9:
        alu16(4, s.r[AX], fetch16());
        clk_ += 1;
        break;

    // --- MOV reg, imm
    case 0xB0:
    case 0xB1:
    case 0xB2:
    case 0xB3:
    case 0xB4:
    case 0xB5:
    case 0xB6:
    case 0xB7:
        set_reg8(op & 7, fetch8());
        clk_ += 1;
        break;
    case 0xB8:
    case 0xB9:
    case 0xBA:
    case 0xBB:
    case 0xBC:
    case 0xBD:
    case 0xBE:
    case 0xBF:
        s.r[op & 7] = fetch16();
        clk_ += 1;
        break;

    // --- shifts
    case 0xC0:
    case 0xD0:
    case 0xD2: {
        decode_modrm();
        uint8_t v = rm8();
        int count = op == 0xD0 ? 1 : op == 0xD2 ? reg8(1) : fetch8();
        set_rm8(shift8(modrm_reg(), v, count));
        clk_ += op == 0xD0 ? mr(3, 1) : mr(5, 3);
        break;
    }
    case 0xC1:
    case 0xD1:
    case 0xD3: {
        decode_modrm();
        uint16_t v = rm16();
        int count = op == 0xD1 ? 1 : op == 0xD3 ? reg8(1) : fetch8();
        set_rm16(shift16(modrm_reg(), v, count));
        clk_ += op == 0xD1 ? mr(3, 1) : mr(5, 3);
        break;
    }

    // --- returns
    case 0xC2: {
        uint16_t n = fetch16();
        s.ip = pop();
        s.r[SP] += n;
        clk_ += 6;
        break;
    }
    case 0xC3:
        s.ip = pop();
        clk_ += 6;
        break;
    case 0xCA: {
        uint16_t n = fetch16();
        s.ip = pop();
        s.seg[CS] = pop();
        s.r[SP] += n;
        clk_ += 9;
        break;
    }
    case 0xCB:
        s.ip = pop();
        s.seg[CS] = pop();
        clk_ += 8;
        break;
    case 0xCF:
        s.ip = pop();
        s.seg[CS] = pop();
        set_flags_word(pop());
        clk_ += 10;
        break;

    case 0xC4:
    case 0xC5: { // LES / LDS
        decode_modrm();
        s.r[modrm_reg()] = rd16(ea_seg_, ea_off_);
        s.seg[op == 0xC4 ? ES : DS] = rd16(ea_seg_, uint16_t(ea_off_ + 2));
        clk_ += 6;
        break;
    }
    case 0xC6:
        decode_modrm();
        set_rm8(fetch8());
        clk_ += 1;
        break;
    case 0xC7:
        decode_modrm();
        set_rm16(fetch16());
        clk_ += 1;
        break;
    case 0xC8: { // ENTER
        uint16_t size = fetch16();
        uint8_t level = fetch8() & 0x1F;
        push(s.r[BP]);
        uint16_t frame = s.r[SP];
        if (level > 0) {
            for (int i = 1; i < level; ++i) {
                s.r[BP] -= 2;
                push(rd16(s.seg[SS], s.r[BP]));
            }
            push(frame);
        }
        s.r[BP] = frame;
        s.r[SP] -= size;
        clk_ += level == 0 ? 8 : level == 1 ? 14 : 15 + level * 4;
        break;
    }
    case 0xC9:
        s.r[SP] = s.r[BP];
        s.r[BP] = pop();
        clk_ += 2;
        break;
    case 0xCC:
        clk_ += 9;
        raise(3);
        break;
    case 0xCD: {
        uint8_t v = fetch8();
        clk_ += 10;
        raise(v);
        break;
    }
    case 0xCE:
        if (flag(OF)) {
            clk_ += 13;
            raise(4);
        } else
            clk_ += 6;
        break;

    case 0xD4: { // AAM
        uint8_t base = fetch8();
        clk_ += 17;
        if (base == 0) {
            raise(0);
            break;
        }
        uint8_t al = reg8(0);
        set_reg8(4, al / base);
        set_reg8(0, al % base);
        set_szp8(reg8(0));
        break;
    }
    case 0xD5: { // AAD
        uint8_t base = fetch8();
        uint8_t al = uint8_t(reg8(0) + reg8(4) * base);
        s.r[AX] = al;
        set_szp8(al);
        clk_ += 6;
        break;
    }
    case 0xD6:
        set_reg8(0, flag(CF) ? 0xFF : 0);
        clk_ += 8;
        break; // SALC (undocumented)
    case 0xD7:
        set_reg8(0, rd8(data_seg(), uint16_t(s.r[BX] + reg8(0))));
        clk_ += 5;
        break;
    case 0xD8:
    case 0xD9:
    case 0xDA:
    case 0xDB:
    case 0xDC:
    case 0xDD:
    case 0xDE:
    case 0xDF:
        decode_modrm(); // ESC: no coprocessor; operand decoded and ignored
        clk_ += 1;
        break;

    // --- loops and jumps
    case 0xE0:
    case 0xE1: {
        int8_t d = int8_t(fetch8());
        s.r[CX]--;
        bool z = flag(ZF);
        if (s.r[CX] != 0 && (op == 0xE1 ? z : !z)) {
            s.ip += d;
            clk_ += 6;
        } else
            clk_ += 3;
        break;
    }
    case 0xE2: {
        int8_t d = int8_t(fetch8());
        if (--s.r[CX] != 0) {
            s.ip += d;
            clk_ += 5;
        } else
            clk_ += 2;
        break;
    }
    case 0xE3: {
        int8_t d = int8_t(fetch8());
        if (s.r[CX] == 0) {
            s.ip += d;
            clk_ += 4;
        } else
            clk_ += 1;
        break;
    }
    case 0xE4:
        set_reg8(0, io_in8(fetch8()));
        clk_ += 7;
        break;
    case 0xE5:
        s.r[AX] = in16(fetch8());
        clk_ += 7;
        break;
    case 0xE6:
        io_out8(fetch8(), reg8(0));
        clk_ += 7;
        break;
    case 0xE7:
        out16(fetch8(), s.r[AX]);
        clk_ += 7;
        break;
    case 0xE8: {
        uint16_t d = fetch16();
        push(s.ip);
        s.ip += d;
        clk_ += 5;
        break;
    }
    case 0xE9: {
        uint16_t d = fetch16();
        s.ip += d;
        clk_ += 4;
        break;
    }
    case 0xEA: {
        uint16_t off = fetch16(), seg = fetch16();
        s.ip = off;
        s.seg[CS] = seg;
        clk_ += 7;
        break;
    }
    case 0xEB: {
        int8_t d = int8_t(fetch8());
        s.ip += d;
        clk_ += 4;
        break;
    }
    case 0xEC:
        set_reg8(0, io_in8(s.r[DX]));
        clk_ += 6;
        break;
    case 0xED:
        s.r[AX] = in16(s.r[DX]);
        clk_ += 6;
        break;
    case 0xEE:
        io_out8(s.r[DX], reg8(0));
        clk_ += 6;
        break;
    case 0xEF:
        out16(s.r[DX], s.r[AX]);
        clk_ += 6;
        break;

    case 0xF4:
        s.halted = true;
        clk_ += 9;
        break;
    case 0xF5:
        s.flags ^= CF;
        clk_ += 4;
        break;
    case 0xF6:
        decode_modrm();
        group3_8();
        break;
    case 0xF7:
        decode_modrm();
        group3_16();
        break;
    case 0xF8:
        set_flag(CF, false);
        clk_ += 4;
        break;
    case 0xF9:
        set_flag(CF, true);
        clk_ += 4;
        break;
    case 0xFA:
        set_flag(IF, false);
        clk_ += 4;
        break;
    case 0xFB:
        if (!flag(IF))
            irq_inhibit_ = true;
        set_flag(IF, true);
        clk_ += 4;
        break;
    case 0xFC:
        set_flag(DF, false);
        clk_ += 4;
        break;
    case 0xFD:
        set_flag(DF, true);
        clk_ += 4;
        break;
    case 0xFE:
        decode_modrm();
        if (modrm_reg() == 0)
            set_rm8(inc8(rm8()));
        else if (modrm_reg() == 1)
            set_rm8(dec8(rm8()));
        clk_ += mr(3, 1);
        break;
    case 0xFF: {
        decode_modrm();
        switch (modrm_reg()) {
        case 0:
            set_rm16(inc16(rm16()));
            clk_ += mr(3, 1);
            break;
        case 1:
            set_rm16(dec16(rm16()));
            clk_ += mr(3, 1);
            break;
        case 2: {
            uint16_t t = rm16();
            push(s.ip);
            s.ip = t;
            clk_ += mr(6, 5);
            break;
        }
        case 3: {
            uint16_t off = rd16(ea_seg_, ea_off_), seg = rd16(ea_seg_, uint16_t(ea_off_ + 2));
            push(s.seg[CS]);
            push(s.ip);
            s.seg[CS] = seg;
            s.ip = off;
            clk_ += 12;
            break;
        }
        case 4:
            s.ip = rm16();
            clk_ += mr(5, 4);
            break;
        case 5:
            s.ip = rd16(ea_seg_, ea_off_);
            s.seg[CS] = rd16(ea_seg_, uint16_t(ea_off_ + 2));
            clk_ += 10;
            break;
        default:
            push(rm16());
            clk_ += mr(2, 1);
            break;
        }
        break;
    }

    default: // 0x63-0x67, 0xF1 and other undefined opcodes: no operation
        clk_ += 1;
        break;
    }
}

} // namespace sawgod::emu
