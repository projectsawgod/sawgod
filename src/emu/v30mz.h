// NEC V30MZ (80186-compatible) interpreter: the WonderSwan's CPU.
//
// Written from the WSdev wiki (ref/docs/wsdev-wiki: NEC_V30MZ_*.wiki) and the documented 80186 instruction
// set. One call to step() runs one instruction with its prefixes (or one further iteration of a REP string
// instruction, so interrupts can be taken between iterations, as on hardware) and returns the cycles it took.
#pragma once

#include <cstdint>

namespace sawgod::emu {

// Everything the CPU sees of the outside world: a 20-bit address space and a 16-bit I/O space.
class Bus {
  public:
    virtual ~Bus() = default;
    virtual uint8_t read8(uint32_t addr) = 0;
    virtual void write8(uint32_t addr, uint8_t value) = 0;
    virtual uint8_t in8(uint16_t port) = 0;
    virtual void out8(uint16_t port, uint8_t value) = 0;
};

enum Reg16 { AX, CX, DX, BX, SP, BP, SI, DI };
enum SegReg { ES, CS, SS, DS };

enum Flag : uint16_t {
    CF = 1 << 0,
    PF = 1 << 2,
    AF = 1 << 4,
    ZF = 1 << 6,
    SF = 1 << 7,
    TF = 1 << 8,
    IF = 1 << 9,
    DF = 1 << 10,
    OF = 1 << 11,
};

struct CpuState {
    uint16_t r[8] = {};   // AX CX DX BX SP BP SI DI
    uint16_t seg[4] = {}; // ES CS SS DS
    uint16_t ip = 0;
    uint16_t flags = 0; // the defined flag bits only; flags_word() adds the fixed ones
    bool halted = false;
};

class V30MZ {
  public:
    explicit V30MZ(Bus &bus) : bus_(bus) {}

    void reset();

    // Runs one instruction; returns the cycles taken. When halted, returns 1 (idle cycle).
    int step();

    // Takes a hardware interrupt now (the caller checks accepts_irq()). Returns the cycles taken.
    int interrupt(uint8_t vector);
    int nmi() { return interrupt(2); }

    // A maskable interrupt may be taken before the next instruction: IF set and not inhibited by the
    // previous instruction (STI/POPF that set IF, MOV/POP SS, prefixes).
    bool accepts_irq() const { return (s.flags & IF) && !irq_inhibit_; }
    void wake() { s.halted = false; }

    uint16_t flags_word() const { return s.flags | 0xF002; }
    void set_flags_word(uint16_t v) { s.flags = v & (CF | PF | AF | ZF | SF | TF | IF | DF | OF); }

    uint8_t reg8(int i) const { return i < 4 ? s.r[i] & 0xFF : s.r[i - 4] >> 8; }
    void set_reg8(int i, uint8_t v) {
        if (i < 4)
            s.r[i] = (s.r[i] & 0xFF00) | v;
        else
            s.r[i - 4] = (s.r[i - 4] & 0x00FF) | (v << 8);
    }

    uint32_t linear(uint16_t seg, uint16_t off) const { return ((uint32_t(seg) << 4) + off) & 0xFFFFF; }
    uint32_t pc() const { return linear(s.seg[CS], s.ip); }

    CpuState s;
    uint64_t cycles = 0; // total cycles executed

    // Everything that defines the CPU's future behaviour, for snapshots (differential tests).
    struct Snapshot {
        CpuState s;
        uint64_t cycles;
        int seg_override, rep;
        uint16_t start_ip;
        bool in_prefix, irq_inhibit;
        uint32_t q_head, q_fetch;
        bool after_branch;
    };
    Snapshot snapshot() const {
        return {s,          cycles,       seg_override_, rep_,     start_ip_,
                in_prefix_, irq_inhibit_, q_head_,       q_fetch_, after_branch_};
    }
    void restore(const Snapshot &p) {
        s = p.s;
        cycles = p.cycles;
        seg_override_ = p.seg_override;
        rep_ = p.rep;
        start_ip_ = p.start_ip;
        in_prefix_ = p.in_prefix;
        irq_inhibit_ = p.irq_inhibit;
        q_head_ = p.q_head;
        q_fetch_ = p.q_fetch;
        after_branch_ = p.after_branch;
    }
    // A native routine ran instead of the code here: the prefetch queue no longer holds it.
    void flush_queue() {
        q_head_ = ~0u;
        after_branch_ = true;
    }
    bool fetching = false; // true while a read is an instruction-byte fetch (for bus tracing)
    void clear_prefixes() { in_prefix_ = false; }

  private:
    Bus &bus_;

    // decode state for the current instruction
    int seg_override_ = -1;
    int rep_ = 0;           // 0, 0xF2 (REPNE) or 0xF3 (REP/REPE)
    uint16_t start_ip_ = 0; // address of the current instruction's first prefix
    bool in_prefix_ =
        false; // a REP iteration is done: the next step repeats the opcode with the same prefixes
    bool irq_inhibit_ = false;
    int clk_ = 0;

    // ModRM
    uint8_t modrm_ = 0;
    bool ea_mem_ = false;
    uint16_t ea_seg_ = 0;
    uint16_t ea_off_ = 0;

    // Timing model (see step()): a prefetch queue sharing one bus with data accesses.
    static constexpr int kQueueBytes = 8;
    static constexpr bool kTurnaround = false; // no prefetch in the cycle after a data access
    static constexpr int kIrqCycles = 33; // interrupt acknowledge and entry (measured: 34-35 with the refill)
    uint32_t q_head_ = ~0u;               // linear address of the next queued byte (~0: queue flushed)
    uint32_t q_fetch_ = 0;                // linear address of the next word the prefetcher reads
    int n_fetch_ = 0;                     // instruction bytes consumed by this step
    int n_bus_ = 0;                       // data bus cycles used by this step
    int n_penalty_ = 0;                   // extra cycles for unaligned word accesses
    int op_ = 0;                          // opcode of this step (for the timing table)
    bool after_branch_ = true; // this step follows a taken branch or an interrupt (queue just refilled)

    void count_word(uint32_t addr, bool io);
    int internal_cycles(bool taken, bool repeat) const;

    uint8_t fetch8();
    uint16_t fetch16();
    uint8_t rd8(uint16_t seg, uint16_t off) {
        ++n_bus_;
        return bus_.read8(linear(seg, off));
    }
    uint16_t rd16(uint16_t seg, uint16_t off);
    void wr8(uint16_t seg, uint16_t off, uint8_t v) {
        ++n_bus_;
        bus_.write8(linear(seg, off), v);
    }
    void wr16(uint16_t seg, uint16_t off, uint16_t v);
    uint8_t io_in8(uint16_t port);
    void io_out8(uint16_t port, uint8_t v);
    uint16_t in16(uint16_t port);
    void out16(uint16_t port, uint16_t v);
    void push(uint16_t v);
    uint16_t pop();
    uint16_t data_seg() const { return seg_override_ >= 0 ? s.seg[seg_override_] : s.seg[DS]; }

    void decode_modrm();
    int modrm_reg() const { return (modrm_ >> 3) & 7; }
    uint8_t rm8();
    uint16_t rm16();
    void set_rm8(uint8_t v);
    void set_rm16(uint16_t v);
    // memory-operand cycle adjustment: mem forms cost `m`, register forms `r`
    int mr(int m, int r) const { return ea_mem_ ? m : r; }

    // ALU
    void set_szp8(uint8_t v);
    void set_szp16(uint16_t v);
    void set_flag(uint16_t f, bool on) { s.flags = on ? (s.flags | f) : (s.flags & ~f); }
    bool flag(uint16_t f) const { return (s.flags & f) != 0; }
    uint8_t alu8(int op, uint8_t a, uint8_t b);
    uint16_t alu16(int op, uint16_t a, uint16_t b);
    uint8_t inc8(uint8_t v);
    uint8_t dec8(uint8_t v);
    uint16_t inc16(uint16_t v);
    uint16_t dec16(uint16_t v);
    uint8_t shift8(int op, uint8_t v, int count);
    uint16_t shift16(int op, uint16_t v, int count);
    void group3_8();
    void group3_16();
    bool condition(int cc) const;

    void string_op(uint8_t op);
    void raise(uint8_t vector); // software/CPU exception during an instruction
    void do_interrupt(uint8_t vector);
    void execute(uint8_t op);
};

} // namespace sawgod::emu
