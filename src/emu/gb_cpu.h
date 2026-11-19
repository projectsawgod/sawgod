// Sharp SM83 (the Game Boy's CPU), small and self-contained: our own core (CC0), written from the
// documented instruction set (Pan Docs, gbdev.io opcode tables). It exists to run a Game Boy game's sound
// driver headless (emu/gb_song.h), so it has what that needs: every documented instruction with its flags
// and machine-cycle count, IME/EI/DI/RETI, HALT. No interrupt dispatch timing, no STOP, no illegal-opcode
// lockup (an undefined opcode is reported and treated as a 4-cycle no-op).
#pragma once

#include <cstdint>
#include <functional>

namespace sawgod::emu {

// The CPU's view of the 16-bit address space.
class GbBus {
  public:
    virtual ~GbBus() = default;
    virtual uint8_t read(uint16_t addr) = 0;
    virtual void write(uint16_t addr, uint8_t value) = 0;
};

class GbCpu {
  public:
    enum : uint8_t { FZ = 0x80, FN = 0x40, FH = 0x20, FC = 0x10 };

    struct Regs {
        uint8_t a = 0x01, f = 0xB0, b = 0x00, c = 0x13, d = 0x00, e = 0xD8, h = 0x01, l = 0x4D;
        uint16_t sp = 0xFFFE, pc = 0x0100;
    };

    explicit GbCpu(GbBus &bus) : bus_(bus) {}

    Regs r;
    bool ime = false;
    bool halted = false;
    uint64_t cycles = 0;      // T-cycles (4.194304 MHz) run so far
    uint32_t bad_opcodes = 0; // undefined opcodes met (each run as a no-op)

    // Runs one instruction; returns its T-cycles.
    int step();

    // Calls the routine at addr as `call addr` would, and runs until it returns to the caller (a return
    // address no code lives at). Returns false if it ran past max_cycles without returning.
    bool call(uint16_t addr, uint64_t max_cycles = 10'000'000);
    // While call() runs: on_watch() each time an instruction is about to run at watch_pc.
    uint16_t watch_pc = 0xFFFF;
    std::function<void()> on_watch;

    uint16_t af() const { return uint16_t(r.a << 8 | (r.f & 0xF0)); }
    uint16_t bc() const { return uint16_t(r.b << 8 | r.c); }
    uint16_t de() const { return uint16_t(r.d << 8 | r.e); }
    uint16_t hl() const { return uint16_t(r.h << 8 | r.l); }
    void set_af(uint16_t v) { r.a = uint8_t(v >> 8), r.f = uint8_t(v & 0xF0); }
    void set_bc(uint16_t v) { r.b = uint8_t(v >> 8), r.c = uint8_t(v); }
    void set_de(uint16_t v) { r.d = uint8_t(v >> 8), r.e = uint8_t(v); }
    void set_hl(uint16_t v) { r.h = uint8_t(v >> 8), r.l = uint8_t(v); }

  private:
    GbBus &bus_;
    int ei_pending_ = 0;

    uint8_t rd(uint16_t a) { return bus_.read(a); }
    void wr(uint16_t a, uint8_t v) { bus_.write(a, v); }
    uint8_t imm8() { return rd(r.pc++); }
    uint16_t imm16() {
        const uint8_t lo = imm8();
        return uint16_t(lo | imm8() << 8);
    }
    void push(uint16_t v) {
        wr(--r.sp, uint8_t(v >> 8));
        wr(--r.sp, uint8_t(v));
    }
    uint16_t pop() {
        const uint8_t lo = rd(r.sp++);
        return uint16_t(lo | rd(r.sp++) << 8);
    }
    uint8_t get_r(int i);
    void set_r(int i, uint8_t v);
    uint16_t get_rp(int i) const;
    void set_rp(int i, uint16_t v);
    bool cond(int i) const;
    void alu(int op, uint8_t v);
    uint8_t cb_rot(int op, uint8_t v);
    int step_cb();
};

} // namespace sawgod::emu
