// Our Game Boy core for rendering GB music (emu/gb_cpu.h, emu/gb_apu.h): instruction results, flags and
// cycle counts against the documented behavior (Pan Docs / gbdev opcode tables), and sound-chip sanity.
#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "emu/gb_apu.h"
#include "emu/gb_cpu.h"

using namespace sawgod::emu;

namespace {

struct FlatBus : GbBus {
    std::vector<uint8_t> mem = std::vector<uint8_t>(0x10000);
    uint8_t read(uint16_t a) override { return mem[a]; }
    void write(uint16_t a, uint8_t v) override { mem[a] = v; }
};

// Runs `code` at 0x0100 until it executes HALT; returns the CPU.
struct Run {
    FlatBus bus;
    GbCpu cpu{bus};
    explicit Run(std::vector<uint8_t> code, uint8_t f = 0) {
        for (size_t i = 0; i < code.size(); ++i)
            bus.mem[0x100 + i] = code[i];
        cpu.r.f = f;
        for (int i = 0; i < 1000 && !cpu.halted; ++i)
            cpu.step();
    }
};

constexpr uint8_t Z = GbCpu::FZ, N = GbCpu::FN, H = GbCpu::FH, C = GbCpu::FC;
constexpr uint8_t HALT = 0x76;

} // namespace

TEST_CASE("gb cpu: 8-bit arithmetic flags") {
    { // ld a,$3a; add a,$c6 -> 0, Z H C
        Run r({0x3E, 0x3A, 0xC6, 0xC6, HALT});
        CHECK(r.cpu.r.a == 0x00);
        CHECK(r.cpu.r.f == (Z | H | C));
    }
    { // ld a,$3e; sub $3e -> 0, Z N
        Run r({0x3E, 0x3E, 0xD6, 0x3E, HALT});
        CHECK(r.cpu.r.a == 0);
        CHECK(r.cpu.r.f == (Z | N));
    }
    { // ld a,$3e; sub $40 -> $fe, N C
        Run r({0x3E, 0x3E, 0xD6, 0x40, HALT});
        CHECK(r.cpu.r.a == 0xFE);
        CHECK(r.cpu.r.f == (N | C));
    }
    { // ld a,$3b; scf; sbc a,$2a -> $10, N
        Run r({0x3E, 0x3B, 0x37, 0xDE, 0x2A, HALT});
        CHECK(r.cpu.r.a == 0x10);
        CHECK(r.cpu.r.f == N);
    }
    { // ld a,$e1; scf; adc a,$0f -> $f1, H
        Run r({0x3E, 0xE1, 0x37, 0xCE, 0x0F, HALT});
        CHECK(r.cpu.r.a == 0xF1);
        CHECK(r.cpu.r.f == H);
    }
    { // ld a,$5a; and $3f -> $1a, H; or / xor clear H; cp leaves A
        Run r({0x3E, 0x5A, 0xE6, 0x3F, HALT});
        CHECK(r.cpu.r.a == 0x1A);
        CHECK(r.cpu.r.f == H);
        Run x({0x3E, 0xFF, 0xEE, 0xFF, HALT});
        CHECK(x.cpu.r.a == 0);
        CHECK(x.cpu.r.f == Z);
        Run c({0x3E, 0x3C, 0xFE, 0x40, HALT});
        CHECK(c.cpu.r.a == 0x3C);
        CHECK(c.cpu.r.f == (N | C));
    }
    { // inc/dec keep C: scf; ld b,$0f; inc b -> $10 H; ld c,$10; dec c -> $0f N H; ld d,1; dec d -> Z N
        Run r({0x37, 0x06, 0x0F, 0x04, HALT});
        CHECK(r.cpu.r.b == 0x10);
        CHECK(r.cpu.r.f == (H | C));
        Run d({0x37, 0x0E, 0x10, 0x0D, HALT});
        CHECK(d.cpu.r.c == 0x0F);
        CHECK(d.cpu.r.f == (N | H | C));
        Run e({0x16, 0x01, 0x15, HALT});
        CHECK(e.cpu.r.f == (Z | N));
    }
}

TEST_CASE("gb cpu: daa after add and sub") {
    // ld a,$45; add a,$38; daa -> $83
    Run a({0x3E, 0x45, 0xC6, 0x38, 0x27, HALT});
    CHECK(a.cpu.r.a == 0x83);
    CHECK(a.cpu.r.f == 0);
    // ld a,$83; sub $38; daa -> $45 N
    Run s({0x3E, 0x83, 0xD6, 0x38, 0x27, HALT});
    CHECK(s.cpu.r.a == 0x45);
    CHECK(s.cpu.r.f == N);
    // ld a,$99; add a,$01; daa -> $00 Z C
    Run o({0x3E, 0x99, 0xC6, 0x01, 0x27, HALT});
    CHECK(o.cpu.r.a == 0x00);
    CHECK(o.cpu.r.f == (Z | C));
}

TEST_CASE("gb cpu: 16-bit arithmetic, sp offsets, pop af") {
    // ld hl,$8a23; ld bc,$0605; add hl,bc -> $9028, H (Z kept)
    Run a({0x21, 0x23, 0x8A, 0x01, 0x05, 0x06, 0x09, HALT}, Z);
    CHECK(a.cpu.hl() == 0x9028);
    CHECK(a.cpu.r.f == (Z | H));
    // ld hl,$8a23; add hl,hl -> $1446, H C
    Run b({0x21, 0x23, 0x8A, 0x29, HALT});
    CHECK(b.cpu.hl() == 0x1446);
    CHECK(b.cpu.r.f == (H | C));
    // ld sp,$fff8; add sp,2 -> $fffa, flags from the low byte: none
    Run c({0x31, 0xF8, 0xFF, 0xE8, 0x02, HALT});
    CHECK(c.cpu.r.sp == 0xFFFA);
    CHECK(c.cpu.r.f == 0);
    // ld sp,$fff8; ld hl,sp-1 -> $fff7; $f8 + $ff carries out of both nibbles: H C
    Run d({0x31, 0xF8, 0xFF, 0xF8, 0xFF, HALT});
    CHECK(d.cpu.hl() == 0xFFF7);
    CHECK(d.cpu.r.f == (H | C));
    // ld sp,$d000; ld bc,$12ff; push bc; pop af -> A $12, F $f0 (low nibble always 0)
    Run e({0x31, 0x00, 0xD0, 0x01, 0xFF, 0x12, 0xC5, 0xF1, HALT});
    CHECK(e.cpu.r.a == 0x12);
    CHECK(e.cpu.r.f == 0xF0);
    // ld (nn),sp
    Run f({0x31, 0x34, 0x12, 0x08, 0x00, 0xC0, HALT});
    CHECK(f.bus.mem[0xC000] == 0x34);
    CHECK(f.bus.mem[0xC001] == 0x12);
}

TEST_CASE("gb cpu: rotates, cb ops") {
    // ld a,$80; rlca -> $01 C (Z always clear for the A rotates)
    Run a({0x3E, 0x80, 0x07, HALT});
    CHECK(a.cpu.r.a == 0x01);
    CHECK(a.cpu.r.f == C);
    // xor a; rla (no carry) -> 0, Z clear
    Run b({0xAF, 0x17, HALT});
    CHECK(b.cpu.r.f == 0);
    // ld b,$f1; swap b -> $1f; ld c,$81; sra c -> $c0 C; ld d,$80; rl d (no carry) -> 0 Z C
    Run c({0x06, 0xF1, 0xCB, 0x30, HALT});
    CHECK(c.cpu.r.b == 0x1F);
    CHECK(c.cpu.r.f == 0);
    Run d({0x0E, 0x81, 0xCB, 0x29, HALT});
    CHECK(d.cpu.r.c == 0xC0);
    CHECK(d.cpu.r.f == C);
    Run e({0xA7, 0x16, 0x80, 0xCB, 0x12, HALT}); // and a clears C first
    CHECK(e.cpu.r.d == 0);
    CHECK(e.cpu.r.f == (Z | C));
    // scf; ld a,$10; bit 4,a -> H C (Z clear); bit 3,a -> Z H C
    Run f({0x37, 0x3E, 0x10, 0xCB, 0x67, HALT});
    CHECK(f.cpu.r.f == (H | C));
    Run g({0x37, 0x3E, 0x10, 0xCB, 0x5F, HALT});
    CHECK(g.cpu.r.f == (Z | H | C));
    // ld hl,$c000; ld (hl),$0f; set 7,(hl); res 0,(hl); srl (hl) -> $c7 >> 1 ... $8e -> $47
    Run h({0x21, 0x00, 0xC0, 0x36, 0x0F, 0xCB, 0xFE, 0xCB, 0x86, 0xCB, 0x3E, HALT});
    CHECK(h.bus.mem[0xC000] == 0x47);
}

TEST_CASE("gb cpu: loads, jumps, calls, cycle counts") {
    // ld hl,$c000; ld a,$12; ld (hl+),a; ld (hl-),a; ldh ($80),a; ld a,($ff00+c) with c=$80
    Run a({0x21, 0x00, 0xC0, 0x3E, 0x12, 0x22, 0x32, 0xE0, 0x80, 0x0E, 0x80, 0xAF, 0xF2, HALT});
    CHECK(a.bus.mem[0xC000] == 0x12);
    CHECK(a.bus.mem[0xC001] == 0x12);
    CHECK(a.cpu.hl() == 0xC000);
    CHECK(a.cpu.r.a == 0x12);

    // a loop: ld b,3; dec b; jr nz,-3; halt -> b 0
    Run l({0x06, 0x03, 0x05, 0x20, 0xFD, HALT});
    CHECK(l.cpu.r.b == 0);
    // 8 + 3 dec (4) + 2 taken jr (12) + 1 not taken (8) + halt 4
    CHECK(l.cpu.cycles == 8 + 12 + 24 + 8 + 4);

    // call() runs a routine to its ret: ld a,$42; ret
    FlatBus bus;
    GbCpu cpu(bus);
    bus.mem[0x4000] = 0x3E, bus.mem[0x4001] = 0x42, bus.mem[0x4002] = 0xC9;
    cpu.r.sp = 0xDFFF;
    CHECK(cpu.call(0x4000));
    CHECK(cpu.r.a == 0x42);
    CHECK(cpu.r.sp == 0xDFFF);
    // nested call, conditional returns, rst: call $4010 (xor a; ret nz; ret z), rst $08 (ld b,9; ret)
    bus.mem[0x4003] = 0xCD, bus.mem[0x4004] = 0x10, bus.mem[0x4005] = 0x40, bus.mem[0x4006] = 0xCF;
    bus.mem[0x4007] = 0xC9;
    bus.mem[0x4010] = 0xAF, bus.mem[0x4011] = 0xC0, bus.mem[0x4012] = 0xC8;
    bus.mem[0x0008] = 0x06, bus.mem[0x0009] = 0x09, bus.mem[0x000A] = 0xC9;
    const uint64_t before = cpu.cycles;
    CHECK(cpu.call(0x4003));
    CHECK(cpu.r.b == 9);
    // call 24 + xor 4 + ret nz (not taken) 8 + ret z 20 + rst 16 + ld b 8 + ret 16 + ret 16
    CHECK(cpu.cycles - before == 24 + 4 + 8 + 20 + 16 + 8 + 16 + 16);
}

namespace {
// Counts rising zero crossings of the left channel.
int crossings(const std::vector<int16_t> &s) {
    int n = 0;
    for (size_t i = 2; i < s.size(); i += 2)
        if (s[i - 2] < 0 && s[i] >= 0)
            ++n;
    return n;
}
} // namespace

TEST_CASE("gb apu: a pulse tone's pitch, length counter, envelope, sweep, NR52") {
    GbApu apu;
    apu.write(0xFF26, 0x80);
    apu.write(0xFF24, 0x77);
    apu.write(0xFF25, 0xFF);
    // pulse 2, 50% duty, volume 15, f = 1792: 131072 / (2048 - 1792) = 512 Hz
    apu.write(0xFF16, 0x80);
    apu.write(0xFF17, 0xF0);
    apu.write(0xFF18, 0x00);
    apu.write(0xFF19, 0x87);
    CHECK((apu.read(0xFF26) & 0x02) != 0);
    apu.run(GbApu::kClock); // one second
    const std::vector<int16_t> s = apu.take();
    CHECK(s.size() == size_t(2 * GbApu::kSampleRate));
    const int n = crossings(s);
    CHECK(n >= 510);
    CHECK(n <= 514);

    // length: 64 - 62 = 2 steps of 1/256 s with length enabled -> off within 10 ms
    apu.write(0xFF16, 0x80 | 62);
    apu.write(0xFF19, 0xC7);
    CHECK(apu.channel_on(1));
    apu.run(GbApu::kClock / 100);
    CHECK_FALSE(apu.channel_on(1));
    CHECK((apu.read(0xFF26) & 0x02) == 0);

    // envelope: volume 2, decreasing, period 1 (64 Hz): 0 after about 2/64 s; the channel stays on
    apu.write(0xFF17, 0x21);
    apu.write(0xFF19, 0x87);
    CHECK(apu.volume(1) == 2);
    apu.run(GbApu::kClock / 16);
    CHECK(apu.volume(1) == 0);
    CHECK(apu.channel_on(1));

    // a DAC off (NRx2 top 5 bits 0) turns the channel off
    apu.write(0xFF17, 0x00);
    CHECK_FALSE(apu.channel_on(1));

    // sweep up with shift 1 from f = 1500: 1500 + 750 > 2047 overflows at trigger -> off
    apu.write(0xFF10, 0x11);
    apu.write(0xFF12, 0xF0);
    apu.write(0xFF13, 1500 & 0xFF);
    apu.write(0xFF14, 0x80 | (1500 >> 8));
    CHECK_FALSE(apu.channel_on(0));
    // from f = 1000: 1500 after the first sweep clock (period 1 = 128 Hz)
    apu.write(0xFF13, 1000 & 0xFF);
    apu.write(0xFF14, 0x80 | (1000 >> 8));
    CHECK(apu.channel_on(0));
    apu.run(GbApu::kClock / 100);
    CHECK(apu.frequency(0) == 1500);

    // power off: channels stop, registers clear
    apu.write(0xFF26, 0x00);
    CHECK((apu.read(0xFF26) & 0x8F) == 0);
    CHECK(apu.read(0xFF12) == 0x00);
}

TEST_CASE("gb apu: wave channel plays wave RAM, noise is noisy") {
    GbApu apu;
    apu.write(0xFF26, 0x80);
    apu.write(0xFF24, 0x77);
    apu.write(0xFF25, 0x44);     // wave only, both sides
    for (int i = 0; i < 16; ++i) // a square: 16 samples of 15, 16 of 0
        apu.write(uint16_t(0xFF30 + i), i < 8 ? 0xFF : 0x00);
    apu.write(0xFF1A, 0x80);
    apu.write(0xFF1C, 0x20); // 100%
    // f = 1536: 65536 / (2048 - 1536) = 128 Hz
    apu.write(0xFF1D, 0x00);
    apu.write(0xFF1E, 0x86);
    CHECK(apu.channel_on(2));
    apu.run(GbApu::kClock);
    const int n = crossings(apu.take());
    CHECK(n >= 127);
    CHECK(n <= 129);

    apu.write(0xFF25, 0x88); // noise only
    apu.write(0xFF1A, 0x00);
    apu.write(0xFF21, 0xF0);
    apu.write(0xFF22, 0x00);
    apu.write(0xFF23, 0x80);
    apu.run(GbApu::kClock / 10);
    const std::vector<int16_t> s = apu.take();
    CHECK(crossings(s) > 200); // white noise at 524 kHz steps: many crossings at 48 kHz
}
