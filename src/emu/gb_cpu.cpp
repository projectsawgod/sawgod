#include "emu/gb_cpu.h"

namespace sawgod::emu {

namespace {
constexpr uint16_t kReturnSentinel = 0xFEFF; // unusable memory: no code returns here by accident
} // namespace

// Register index as in the opcode: 0 b, 1 c, 2 d, 3 e, 4 h, 5 l, 6 (hl), 7 a.
uint8_t GbCpu::get_r(int i) {
    switch (i) {
    case 0:
        return r.b;
    case 1:
        return r.c;
    case 2:
        return r.d;
    case 3:
        return r.e;
    case 4:
        return r.h;
    case 5:
        return r.l;
    case 6:
        return rd(hl());
    default:
        return r.a;
    }
}

void GbCpu::set_r(int i, uint8_t v) {
    switch (i) {
    case 0:
        r.b = v;
        break;
    case 1:
        r.c = v;
        break;
    case 2:
        r.d = v;
        break;
    case 3:
        r.e = v;
        break;
    case 4:
        r.h = v;
        break;
    case 5:
        r.l = v;
        break;
    case 6:
        wr(hl(), v);
        break;
    default:
        r.a = v;
        break;
    }
}

// 0 bc, 1 de, 2 hl, 3 sp
uint16_t GbCpu::get_rp(int i) const {
    switch (i) {
    case 0:
        return bc();
    case 1:
        return de();
    case 2:
        return hl();
    default:
        return r.sp;
    }
}

void GbCpu::set_rp(int i, uint16_t v) {
    switch (i) {
    case 0:
        set_bc(v);
        break;
    case 1:
        set_de(v);
        break;
    case 2:
        set_hl(v);
        break;
    default:
        r.sp = v;
        break;
    }
}

// 0 nz, 1 z, 2 nc, 3 c
bool GbCpu::cond(int i) const {
    switch (i) {
    case 0:
        return !(r.f & FZ);
    case 1:
        return (r.f & FZ) != 0;
    case 2:
        return !(r.f & FC);
    default:
        return (r.f & FC) != 0;
    }
}

// 0 add, 1 adc, 2 sub, 3 sbc, 4 and, 5 xor, 6 or, 7 cp
void GbCpu::alu(int op, uint8_t v) {
    const int a = r.a;
    const int carry = (r.f & FC) ? 1 : 0;
    int res = 0;
    uint8_t f = 0;
    switch (op) {
    case 0:
    case 1: {
        const int c = op == 1 ? carry : 0;
        res = a + v + c;
        if (((a & 0xF) + (v & 0xF) + c) > 0xF)
            f |= FH;
        if (res > 0xFF)
            f |= FC;
        break;
    }
    case 2:
    case 3:
    case 7: {
        const int c = op == 3 ? carry : 0;
        res = a - v - c;
        f |= FN;
        if (((a & 0xF) - (v & 0xF) - c) < 0)
            f |= FH;
        if (res < 0)
            f |= FC;
        break;
    }
    case 4:
        res = a & v;
        f |= FH;
        break;
    case 5:
        res = a ^ v;
        break;
    default:
        res = a | v;
        break;
    }
    if ((res & 0xFF) == 0)
        f |= FZ;
    r.f = f;
    if (op != 7)
        r.a = uint8_t(res);
}

// CB-prefix rotates and shifts: 0 rlc, 1 rrc, 2 rl, 3 rr, 4 sla, 5 sra, 6 swap, 7 srl.
uint8_t GbCpu::cb_rot(int op, uint8_t v) {
    const int carry_in = (r.f & FC) ? 1 : 0;
    int res = 0, carry = 0;
    switch (op) {
    case 0:
        carry = v >> 7, res = (v << 1) | carry;
        break;
    case 1:
        carry = v & 1, res = (v >> 1) | (carry << 7);
        break;
    case 2:
        carry = v >> 7, res = (v << 1) | carry_in;
        break;
    case 3:
        carry = v & 1, res = (v >> 1) | (carry_in << 7);
        break;
    case 4:
        carry = v >> 7, res = v << 1;
        break;
    case 5:
        carry = v & 1, res = (v >> 1) | (v & 0x80);
        break;
    case 6:
        res = (v >> 4) | (v << 4);
        break;
    default:
        carry = v & 1, res = v >> 1;
        break;
    }
    res &= 0xFF;
    r.f = uint8_t((res == 0 ? FZ : 0) | (carry ? FC : 0));
    return uint8_t(res);
}

int GbCpu::step_cb() {
    const uint8_t op = imm8();
    const int x = op >> 6, y = (op >> 3) & 7, z = op & 7;
    const bool mem = z == 6;
    const uint8_t v = get_r(z);
    switch (x) {
    case 0:
        set_r(z, cb_rot(y, v));
        return mem ? 16 : 8;
    case 1: // bit
        r.f = uint8_t((r.f & FC) | FH | ((v >> y) & 1 ? 0 : FZ));
        return mem ? 12 : 8;
    case 2:
        set_r(z, uint8_t(v & ~(1 << y)));
        return mem ? 16 : 8;
    default:
        set_r(z, uint8_t(v | (1 << y)));
        return mem ? 16 : 8;
    }
}

int GbCpu::step() {
    if (ei_pending_ && --ei_pending_ == 0)
        ime = true;
    if (halted) {
        cycles += 4;
        return 4;
    }
    const uint8_t op = imm8();
    const int x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = y >> 1, q = y & 1;
    int t = 4;
    switch (x) {
    case 0:
        switch (z) {
        case 0:
            if (y == 0) {        // nop
            } else if (y == 1) { // ld (nn),sp
                const uint16_t a = imm16();
                wr(a, uint8_t(r.sp));
                wr(uint16_t(a + 1), uint8_t(r.sp >> 8));
                t = 20;
            } else if (y == 2) { // stop (treated as a no-op that skips its operand)
                ++r.pc;
            } else {
                const int8_t d = int8_t(imm8());
                t = 8;
                if (y == 3 || cond(y - 4)) {
                    r.pc = uint16_t(r.pc + d);
                    t = 12;
                }
            }
            break;
        case 1:
            if (q == 0) {
                set_rp(p, imm16());
                t = 12;
            } else { // add hl,rp
                const int a = hl(), b = get_rp(p), s = a + b;
                r.f = uint8_t((r.f & FZ) | (((a & 0xFFF) + (b & 0xFFF)) > 0xFFF ? FH : 0) |
                              (s > 0xFFFF ? FC : 0));
                set_hl(uint16_t(s));
                t = 8;
            }
            break;
        case 2: {
            uint16_t a = 0;
            switch (p) {
            case 0:
                a = bc();
                break;
            case 1:
                a = de();
                break;
            default:
                a = hl();
                set_hl(uint16_t(p == 2 ? a + 1 : a - 1));
                break;
            }
            if (q == 0)
                wr(a, r.a);
            else
                r.a = rd(a);
            t = 8;
            break;
        }
        case 3:
            set_rp(p, uint16_t(get_rp(p) + (q == 0 ? 1 : -1)));
            t = 8;
            break;
        case 4: { // inc r
            const uint8_t v = uint8_t(get_r(y) + 1);
            set_r(y, v);
            r.f = uint8_t((r.f & FC) | (v == 0 ? FZ : 0) | ((v & 0xF) == 0 ? FH : 0));
            t = y == 6 ? 12 : 4;
            break;
        }
        case 5: { // dec r
            const uint8_t v = uint8_t(get_r(y) - 1);
            set_r(y, v);
            r.f = uint8_t((r.f & FC) | FN | (v == 0 ? FZ : 0) | ((v & 0xF) == 0xF ? FH : 0));
            t = y == 6 ? 12 : 4;
            break;
        }
        case 6:
            set_r(y, imm8());
            t = y == 6 ? 12 : 8;
            break;
        default:
            switch (y) {
            case 0: { // rlca
                const int c = r.a >> 7;
                r.a = uint8_t(r.a << 1 | c);
                r.f = c ? FC : 0;
                break;
            }
            case 1: { // rrca
                const int c = r.a & 1;
                r.a = uint8_t(r.a >> 1 | c << 7);
                r.f = c ? FC : 0;
                break;
            }
            case 2: { // rla
                const int c = r.a >> 7;
                r.a = uint8_t(r.a << 1 | ((r.f & FC) ? 1 : 0));
                r.f = c ? FC : 0;
                break;
            }
            case 3: { // rra
                const int c = r.a & 1;
                r.a = uint8_t(r.a >> 1 | ((r.f & FC) ? 0x80 : 0));
                r.f = c ? FC : 0;
                break;
            }
            case 4: { // daa
                int a = r.a;
                uint8_t f = r.f & (FN | FC);
                if (!(r.f & FN)) {
                    if ((r.f & FC) || a > 0x99) {
                        a += 0x60;
                        f |= FC;
                    }
                    if ((r.f & FH) || (a & 0x0F) > 0x09)
                        a += 0x06;
                } else {
                    if (r.f & FC)
                        a -= 0x60;
                    if (r.f & FH)
                        a -= 0x06;
                }
                r.a = uint8_t(a);
                r.f = uint8_t(f | (r.a == 0 ? FZ : 0));
                break;
            }
            case 5: // cpl
                r.a = uint8_t(~r.a);
                r.f |= FN | FH;
                break;
            case 6: // scf
                r.f = uint8_t((r.f & FZ) | FC);
                break;
            default: // ccf
                r.f = uint8_t((r.f & FZ) | ((r.f & FC) ? 0 : FC));
                break;
            }
            break;
        }
        break;
    case 1:
        if (op == 0x76) {
            halted = true;
        } else {
            set_r(y, get_r(z));
            t = (y == 6 || z == 6) ? 8 : 4;
        }
        break;
    case 2:
        alu(y, get_r(z));
        t = z == 6 ? 8 : 4;
        break;
    default:
        switch (z) {
        case 0:
            if (y < 4) { // ret cc
                t = 8;
                if (cond(y)) {
                    r.pc = pop();
                    t = 20;
                }
            } else if (y == 4) {
                wr(uint16_t(0xFF00 | imm8()), r.a);
                t = 12;
            } else if (y == 6) {
                r.a = rd(uint16_t(0xFF00 | imm8()));
                t = 12;
            } else { // add sp,e (y 5) / ld hl,sp+e (y 7): flags from the low byte
                const int8_t d = int8_t(imm8());
                const int sp = r.sp, u = uint8_t(d);
                r.f =
                    uint8_t((((sp & 0xF) + (u & 0xF)) > 0xF ? FH : 0) | (((sp & 0xFF) + u) > 0xFF ? FC : 0));
                const uint16_t res = uint16_t(sp + d);
                if (y == 5) {
                    r.sp = res;
                    t = 16;
                } else {
                    set_hl(res);
                    t = 12;
                }
            }
            break;
        case 1:
            if (q == 0) {
                const uint16_t v = pop();
                switch (p) {
                case 0:
                    set_bc(v);
                    break;
                case 1:
                    set_de(v);
                    break;
                case 2:
                    set_hl(v);
                    break;
                default:
                    set_af(v);
                    break;
                }
                t = 12;
            } else if (p == 0 || p == 1) { // ret / reti
                r.pc = pop();
                if (p == 1)
                    ime = true;
                t = 16;
            } else if (p == 2) { // jp hl
                r.pc = hl();
            } else { // ld sp,hl
                r.sp = hl();
                t = 8;
            }
            break;
        case 2:
            if (y < 4) {
                const uint16_t a = imm16();
                t = 12;
                if (cond(y)) {
                    r.pc = a;
                    t = 16;
                }
            } else if (y == 4) {
                wr(uint16_t(0xFF00 | r.c), r.a);
                t = 8;
            } else if (y == 5) {
                wr(imm16(), r.a);
                t = 16;
            } else if (y == 6) {
                r.a = rd(uint16_t(0xFF00 | r.c));
                t = 8;
            } else {
                r.a = rd(imm16());
                t = 16;
            }
            break;
        case 3:
            if (y == 0) {
                r.pc = imm16();
                t = 16;
            } else if (y == 1) {
                t = step_cb();
            } else if (y == 6) {
                ime = false;
                ei_pending_ = 0;
            } else if (y == 7) {
                ei_pending_ = 2; // takes effect after the next instruction
            } else {
                ++bad_opcodes;
            }
            break;
        case 4:
            if (y < 4) {
                const uint16_t a = imm16();
                t = 12;
                if (cond(y)) {
                    push(r.pc);
                    r.pc = a;
                    t = 24;
                }
            } else {
                ++bad_opcodes;
            }
            break;
        case 5:
            if (q == 0) {
                switch (p) {
                case 0:
                    push(bc());
                    break;
                case 1:
                    push(de());
                    break;
                case 2:
                    push(hl());
                    break;
                default:
                    push(af());
                    break;
                }
                t = 16;
            } else if (p == 0) {
                const uint16_t a = imm16();
                push(r.pc);
                r.pc = a;
                t = 24;
            } else {
                ++bad_opcodes;
            }
            break;
        case 6:
            alu(y, imm8());
            t = 8;
            break;
        default: // rst
            push(r.pc);
            r.pc = uint16_t(y * 8);
            t = 16;
            break;
        }
        break;
    }
    cycles += uint64_t(t);
    return t;
}

bool GbCpu::call(uint16_t addr, uint64_t max_cycles) {
    const uint16_t sp = r.sp;
    push(kReturnSentinel);
    r.pc = addr;
    halted = false;
    const uint64_t end = cycles + max_cycles;
    while (!(r.pc == kReturnSentinel && r.sp == sp)) {
        if (cycles >= end || halted)
            return false;
        if (r.pc == watch_pc && on_watch)
            on_watch();
        step();
    }
    return true;
}

} // namespace sawgod::emu
