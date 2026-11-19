// Scheduler regressions use synthetic ROM/RAM and optional ROM fixtures.
// Every driver has a step bound so a parked CPU or a frozen-time wait fails instead of hanging the runner.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

#include "app/host_settings.h"
#include "game/battle_ui.h"

#include "game/bank34.h"
#include "game/bank35_field.h"
#include "game/fiber.h"
#include "game/session.h"
#include "tools/difftest_clock.h"
#include "tools/difftest_inputs.h"

using namespace sawgod;

TEST_CASE("indexed movie inputs preserve linear lookup, directive order and snapshot replay") {
    const std::vector<std::pair<int, uint16_t>> keys = {{2, 1}, {5, 2}, {5, 4}, {10, 0}, {1000, 8}};
    const tools::MovieInputs::Holds holds = {{0xC881, 1}, {0xC881, 0}, {0xD000, 3}};
    // Deliberately unordered frames, duplicate writes, and a poke that overrides a held address.
    const std::vector<tools::MovieInputs::Poke> pokes = {
        {12, 0xD000, 6}, {5, 0xC881, 7}, {5, 0xD000, 8}, {5, 0xD000, 9}, {0, 0xC881, 4}};
    const tools::MovieInputs inputs(keys, holds, pokes);
    const tools::MovieInputs empty({}, {}, {});
    std::array<uint8_t, 0x10000> expected{}, actual{};
    // Nonmonotonic access represents original/native runs and repeated restores of the entry snapshot.
    for (uint64_t frame : {0u, 1u, 2u, 5u, 6u, 10u, 12u, 1000u, 1000000u, 5u, 0u, 12u}) {
        CAPTURE(frame);
        uint16_t key = 0;
        for (const auto &[at, value] : keys)
            if (uint64_t(at) <= frame)
                key = value;
        CHECK(inputs.keys_at(frame) == key);
        CHECK(empty.keys_at(frame) == 0);
        expected.fill(0xA5);
        actual = expected;
        for (const auto &[addr, value] : holds)
            expected[addr] = value;
        for (const auto &p : pokes)
            if (uint64_t(p.frame) == frame)
                expected[p.addr] = p.value;
        inputs.ram_at(actual, frame);
        CHECK(actual == expected);
        empty.ram_at(actual, frame);
        CHECK(actual == expected);
    }
}

namespace {
struct FrameMachine {
    emu::Wsc w{std::vector<uint8_t>(4 * 1024 * 1024), 0};
    int line_irqs = 0, vblank_irqs = 0, fetches = 0;

    FrameMachine(const char *native) {
        w.io[0x60] = 0x80;
        w.io[0xB0] = 0x20;
        w.io[0xB2] = 0x50;
        w.io[0x03] = 143;
        w.scanline = 142;
        w.line_cycle = 0;
        w.cpu.s.seg[emu::SS] = 0;
        w.cpu.s.r[emu::SP] = 0x2000;
        w.cpu.s.flags = emu::IF | emu::DF;
        game::Machine m(w);
        m.wr16(0, 0x90, 0x1000);
        m.wr16(0, 0x92, 0x4000);
        m.wr16(0, 0x98, 0x1010);
        m.wr16(0, 0x9A, 0x4000);
        game::install(w, native);
        w.set_native(0x341000, [this](emu::Wsc &hw) {
            ++line_irqs;
            game::Machine irq(hw);
            irq.out8(0xB6, 0x10);
            irq.iret();
            return 16;
        });
        w.set_native(0x341010, [this](emu::Wsc &hw) {
            ++vblank_irqs;
            game::Machine irq(hw);
            irq.out8(0xB6, 0x40);
            irq.wr8(0, 0xC80E, uint8_t(irq.rd8(0, 0xC80E) + 1));
            irq.iret();
            return 16;
        });
        w.on_bus = [this](char kind, uint32_t) { fetches += kind == 'F'; };
        enter(0x0009);
    }

    void enter(uint16_t ip) {
        w.cpu.s.seg[emu::CS] = 0x4000;
        w.cpu.s.ip = ip;
        w.cpu.s.r[emu::SP] = 0x2000;
        game::Machine m(w);
        m.wr16(0, 0x2000, 0x2222);
        m.wr16(0, 0x2002, 0x5000);
    }
    bool returned() const { return w.cpu.s.seg[emu::CS] == 0x5000 && w.cpu.s.ip == 0x2222; }
    void tick(bool advance = true) {
        if (w.tick(true, advance)) {
            ++w.frame_count;
            w.set_keys(emu::KEY_A);
            w.begin_frame();
        }
    }
    void finish(bool advance = true) {
        for (int i = 0; !returned() && i < 2000; ++i)
            tick(advance);
        REQUIRE(returned());
        CHECK(w.cpu.s.r[emu::SP] == 0x2004);
    }
};
} // namespace

TEST_CASE("native VBlank wait wakes on IRQ and re-parks after a raster IRQ") {
    FrameMachine f("wait_vblank");
    const auto regs = f.w.cpu.s.r;
    f.w.ram[0xC80E] = 7; // must clear a previous completion, not return immediately
    f.tick();
    REQUIRE(f.w.cpu.s.halted);
    CHECK(f.w.ram[0xC80E] == 0);
    f.finish();
    CHECK(f.line_irqs == 1);
    CHECK(f.vblank_irqs == 1);
    CHECK(f.fetches == 0);
    for (int i = 0; i < 8; ++i)
        if (i != emu::SP)
            CHECK(f.w.cpu.s.r[i] == regs[i]);
    CHECK((f.w.cpu.s.flags & (emu::IF | emu::DF)) == (emu::IF | emu::DF));
}

TEST_CASE("native waits advance a frozen clock and a scanline wait needs no IRQ") {
    SUBCASE("VBlank") {
        FrameMachine f("wait_vblank");
        f.w.irq_gate = [] { return true; };
        f.finish(false);
        CHECK(f.vblank_irqs == 1);
        CHECK(f.fetches == 0);
    }
    SUBCASE("visible raster with interrupts disabled") {
        FrameMachine f("wait_visible_scanline");
        f.enter(0x0051);
        f.w.cpu.s.flags = emu::DF;
        f.w.cpu.s.r[emu::AX] = 0xABCD;
        f.tick(false);
        REQUIRE(f.w.line_parked());
        const auto parked = f.w.snapshot();
        f.w.park_until_next_line();
        f.w.restore(parked);
        CHECK(f.w.line_parked());
        f.finish(false);
        CHECK(f.w.scanline < 141);
        CHECK(f.w.cpu.s.r[emu::AX] == 0xABCD);
        CHECK((f.w.cpu.s.flags & emu::IF) == 0);
        CHECK(f.fetches == 0);
    }
    SUBCASE("already visible") {
        FrameMachine f("wait_visible_scanline");
        f.enter(0x0051);
        f.w.scanline = 140;
        f.tick(false);
        CHECK(f.returned());
        CHECK_FALSE(f.w.line_parked());
    }
}

TEST_CASE("native caller fibers and interpreted CALL FAR can both reach native frame waits") {
    SUBCASE("native caller") {
        FrameMachine f("wait_frames");
        // The whole registry supplies a separate fiber for wait_vblank, plus its trampoline.
        game::install(f.w);
        f.enter(0);
        f.w.cpu.s.r[emu::CX] = 2;
        f.finish();
        CHECK(f.w.cpu.s.r[emu::CX] == 0);
        CHECK(f.vblank_irqs == 2);
        CHECK(f.fetches == 0);
    }
    SUBCASE("interpreted caller") {
        FrameMachine f("wait_vblank");
        // A synthetic far call at 4000:9000, with no game ROM code or data.
        const uint8_t call[] = {0x9A, 0x09, 0x00, 0x00, 0x40};
        std::copy(std::begin(call), std::end(call), f.w.rom.begin() + 0x349000);
        f.w.cpu.s.ip = 0x9000;
        f.tick();
        for (int i = 0; f.w.cpu.s.ip != 0x9005 && i < 2000; ++i)
            f.tick();
        CHECK(f.w.cpu.s.seg[emu::CS] == 0x4000);
        CHECK(f.w.cpu.s.ip == 0x9005);
        CHECK(f.w.cpu.s.r[emu::SP] == 0x2000);
        CHECK(f.vblank_irqs == 1);
    }
}

TEST_CASE("installed keypad polls yield for fresh input and retain the scan bookkeeping") {
    FrameMachine f("read_keypad");
    f.enter(0x005C);
    f.finish();
    CHECK(f.w.ram[0xC804] == 0);
    f.enter(0x005C);
    f.tick();
    REQUIRE(f.w.line_parked());
    f.finish();
    CHECK(f.w.frame_count == 1);
    CHECK(f.w.ram[0xC800] == emu::KEY_A);
    CHECK(f.w.ram[0xC804] == emu::KEY_A);
    CHECK(f.w.ram[0xC808] == 0x28);
    CHECK(f.fetches == 0);
    const auto scanned = f.w.snapshot();
    CHECK_FALSE(f.w.begin_keypad_scan());
    f.w.restore(scanned);
    CHECK_FALSE(f.w.begin_keypad_scan());
}

TEST_CASE("two machines own their suspended native frame fibers independently") {
    FrameMachine a("wait_vblank"), b("wait_vblank");
    a.w.cpu.s.r[emu::AX] = 0x1234;
    b.w.cpu.s.r[emu::AX] = 0x5678;
    a.tick();
    b.tick();
    REQUIRE(a.w.cpu.s.halted);
    REQUIRE(b.w.cpu.s.halted);
    a.finish();
    b.finish();
    CHECK(a.w.cpu.s.r[emu::AX] == 0x1234);
    CHECK(b.w.cpu.s.r[emu::AX] == 0x5678);
    CHECK(a.fetches == 0);
    CHECK(b.fetches == 0);
}

TEST_CASE("a cancelled fiber unwinds its stack and runs nothing after its yield") {
    struct Guard {
        std::vector<int> &log;
        int id;
        ~Guard() { log.push_back(id); }
    };
    std::vector<int> log;
    bool after = false;
    {
        game::Fiber f([&] {
            Guard outer{log, 1};
            [&] {
                Guard inner{log, 2};
                game::Fiber::yield();
                after = true;
            }();
        });
        CHECK_FALSE(f.resume());
        CHECK(log.empty());
        f.cancel();
        CHECK(f.finished());
        CHECK(log == std::vector<int>{2, 1});
        CHECK_FALSE(after);
    }
    log.clear();
    {
        game::Fiber g([&] {
            Guard only{log, 3};
            game::Fiber::yield();
        });
        g.resume();
    } // destroyed unfinished: unwound too
    CHECK(log == std::vector<int>{3});
    game::Fiber never([&] { after = true; });
    never.cancel(); // never started: just finished
    CHECK(never.finished());
    CHECK_FALSE(after);
}

TEST_CASE("a new Session generation cancels the suspended natives and the machine runs on") {
    FrameMachine f("wait_frames");
    game::install(f.w); // wait_frames suspends in its call-out to wait_vblank, which parks on its own fiber
    game::Session &s = game::Session::of(f.w);
    const uint64_t gen = s.generation(), abandoned = s.abandoned();
    f.enter(0);
    f.w.cpu.s.r[emu::CX] = 3;
    for (int i = 0; s.suspended() < 2 && i < 10; ++i) // wait_frames' call-out, then wait_vblank's park
        f.tick();
    REQUIRE(s.suspended() == 2);
    s.new_generation(); // as on entry to main after the reset keys / a party wipeout / the ending
    CHECK(s.suspended() == 0);
    CHECK(s.abandoned() == abandoned + 2);
    CHECK(s.generation() == gen + 1);
    // the abandoned frames are gone; a fresh call works normally
    f.enter(0);
    f.w.cpu.s.r[emu::CX] = 2;
    f.finish();
    CHECK(f.w.cpu.s.r[emu::CX] == 0);
    CHECK(s.suspended() == 0);
}

TEST_CASE("reset_natives starts a new generation and keeps hook subscriptions") {
    FrameMachine f("wait_vblank");
    game::Session &s = game::Session::of(f.w);
    int calls = 0;
    game::hooks::Subscription sub = s.hooks().subscribe(
        game::hooks::Event::BattleStart, [&](game::Machine &, const game::hooks::Args &) { ++calls; });
    f.tick();
    REQUIRE(s.suspended() == 1);
    const uint64_t gen = s.generation();
    game::reset_natives(f.w, "wait_vblank");
    CHECK(s.suspended() == 0);
    CHECK(s.generation() > gen);
    CHECK(&game::Session::of(f.w) == &s);
    game::Machine m(f.w);
    game::hooks::emit(game::hooks::Event::BattleStart, m);
    CHECK(calls == 1);
}

TEST_CASE("function-only frames freeze LCD pixels without changing hardware or CPU timing") {
    emu::Wsc shown(std::vector<uint8_t>(0x400000, 0x90), 0x2000);
    emu::Wsc hidden(shown.rom, 0x2000);
    shown.io[0x60] = 0x80;
    shown.ram[0xFE00] = 0xFF;
    shown.ram[0xFE01] = 0x0F;
    hidden.restore(shown.snapshot());
    hidden.render_lcd = false;
    for (int i = 0; i < 3; ++i) {
        shown.run_frame();
        hidden.run_frame();
    }
    CHECK(shown.ram == hidden.ram);
    CHECK(shown.io == hidden.io);
    CHECK(shown.sram == hidden.sram);
    CHECK(shown.cpu.cycles == hidden.cpu.cycles);
    CHECK(shown.cpu.s.ip == hidden.cpu.s.ip);
    CHECK(shown.cpu.s.flags == hidden.cpu.s.flags);
    CHECK(std::equal(std::begin(shown.cpu.s.r), std::end(shown.cpu.s.r), std::begin(hidden.cpu.s.r)));
    CHECK(std::equal(std::begin(shown.cpu.s.seg), std::end(shown.cpu.s.seg), std::begin(hidden.cpu.s.seg)));
    CHECK(shown.frame_count == hidden.frame_count);
    CHECK(shown.scanline == hidden.scanline);
    CHECK(shown.line_cycle == hidden.line_cycle);
    CHECK(shown.framebuffer() != hidden.framebuffer());
}

TEST_CASE("GB palette binding follows mid-frame colors and mutable swap resources") {
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    w.cpu.s.halted = true;
    w.cpu.s.flags = 0;
    w.io[0x60] = 0x80;
    w.io[0x01] = 1; // backdrop: palette 0, color 1
    w.set_second(true);
    emu::Wsc::PaletteSwap swap;
    swap.from.fill(0x888);
    swap.to.fill(0x444);
    w.palette_swaps.push_back(swap);
    auto colors = [&](uint16_t color) {
        for (int c = 0; c < 16; ++c)
            w.shadow_write16(uint16_t(0xFE00 + c * 2), color);
    };
    colors(0x888);
    w.run_frame();
    w.run_frame(); // reset starts in VBlank; advance a full visible raster
    CHECK(w.framebuffer(true)[0] == 0x444);
    // No frame boundary between the palette writes and the second half of scanout.
    while (w.scanline != 72)
        w.tick();
    colors(0x999);
    while (w.scanline != 145)
        w.tick();
    CHECK(w.framebuffer(true)[71 * emu::Wsc::kWidth] == 0x444);
    CHECK(w.framebuffer(true)[72 * emu::Wsc::kWidth] == 0x555);
    // Replacing a public resource in place must invalidate binding, even at the same size.
    w.palette_swaps[0].from.fill(0x999);
    w.palette_swaps[0].to.fill(0x333);
    w.run_frame();
    CHECK(w.framebuffer(true)[0] == 0x333);
    w.palette_swaps[0].to.fill(0x222);
    w.run_frame();
    CHECK(w.framebuffer(true)[0] == 0x222);
    // A different fade target can bind a palette that does not equal the displayed colors.
    w.palette_swap_target = 0xC600;
    for (int c = 0; c < 16; ++c)
        w.shadow_write16(uint16_t(0xC600 + c * 2), 0xAAA);
    w.palette_swaps[0].from.fill(0xAAA);
    colors(0xBBB);
    w.run_frame();
    CHECK(w.framebuffer(true)[0] == 0x333);
}

TEST_CASE("an undrawn GB picture keeps its palette tracking and draws exactly as one drawn all along") {
    // A: the second picture drawn every frame. B: not drawn (Graphics showed no GB) through a palette fade
    // whose later steps only bind through the previous line's binding, then shown again.
    emu::Wsc a(std::vector<uint8_t>(0x400000), 0), b(std::vector<uint8_t>(0x400000), 0);
    for (auto &w : {&a, &b}) {
        w->cpu.s.halted = true;
        w->cpu.s.flags = 0;
        w->io[0x60] = 0x80;
        w->io[0x01] = 1;
        w->set_second(true);
        emu::Wsc::PaletteSwap swap;
        for (int c = 0; c < 16; ++c)
            swap.from[size_t(c)] = uint16_t(0x111 * (c % 8 + 4));
        swap.to.fill(0x444);
        w->palette_swaps.push_back(swap);
    }
    const auto colors = [&](int shift) {
        for (auto *w : {&a, &b})
            for (int c = 0; c < 16; ++c) {
                const int v = std::clamp(c % 8 + 4 + shift, 0, 15);
                w->shadow_write16(uint16_t(0xFE00 + c * 2), uint16_t(0x111 * v));
            }
    };
    colors(0);
    for (auto *w : {&a, &b}) {
        w->run_frame();
        w->run_frame();
    }
    CHECK(b.framebuffer(true) == a.framebuffer(true));
    b.second_visible = false;
    for (int step = -1; step >= -15; --step) { // down to black: later steps match the swap only as a fade
        colors(step);
        a.run_frame();
        b.run_frame();
    }
    CHECK(b.framebuffer(true) != a.framebuffer(true)); // not drawn meanwhile
    b.refresh_second();                                // on demand: drawn from the current state
    CHECK(b.framebuffer(true) == a.framebuffer(true));
    b.refresh_second(); // already current: nothing to do
    CHECK(b.framebuffer(true) == a.framebuffer(true));
    b.second_visible = false;
    for (int step = -14; step <= 0; ++step) { // back up, still undrawn
        colors(step);
        a.run_frame();
        b.run_frame();
    }
    b.second_visible = true;
    a.run_frame();
    b.run_frame(); // drawn line by line again, with the tracking kept current all along
    CHECK(b.framebuffer(true) == a.framebuffer(true));
    CHECK(b.framebuffer_rgb(emu::Wsc::ColorMode::Raw, true, true) ==
          a.framebuffer_rgb(emu::Wsc::ColorMode::Raw, true, true));
    CHECK(b.framebuffer() == a.framebuffer());
    CHECK(b.ram == a.ram);
}

TEST_CASE("difftest VBlank clock isolates the prologue and IRQ return from raster boundaries") {
    // Synthetic original: save registers/segments, clear vblank_done, jump to its real poll address;
    // finish with the original flag test and register restore. No game ROM or data is used.
    const uint8_t setup[] = {0x60, 0x1E, 0x06, 0x16, 0x31, 0xC0, 0x36, 0xA2, 0x0E, 0xC8, 0xEB, 0x2F};
    const uint8_t poll[] = {0x36, 0xA0, 0x0E, 0xC8, 0x08, 0xC0, 0x74, 0xF8, 0x17, 0x07, 0x1F, 0x61, 0xCB};
    // VBlank: preserve AX, acknowledge, delay, increment the flag and a sound-tick surrogate, IRET.
    // The long handler would cross the frame wrap in the old clock and admit an extra raster IRQ
    // during the original's epilogue, while the native has already returned.
    std::vector<uint8_t> irq = {0x50, 0xB0, 0x40, 0xE6, 0xB6};
    irq.insert(irq.end(), 3800, 0x90);
    const uint8_t irq_end[] = {0xFE, 0x06, 0x0E, 0xC8, 0xFE, 0x06, 0x04, 0x06, 0x58, 0xCF};
    irq.insert(irq.end(), std::begin(irq_end), std::end(irq_end));
    const uint8_t raster[] = {0x50, 0xB0, 0x10, 0xE6, 0xB6, 0xE4, 0x02, 0xE6, 0x10, 0x58, 0xCF};

    auto run = [&](bool native, int line, int cycle) {
        FrameMachine f("wait_vblank");
        auto &w = f.w;
        std::copy(std::begin(setup), std::end(setup), w.rom.begin() + 0x340009);
        std::copy(std::begin(poll), std::end(poll), w.rom.begin() + 0x340044);
        std::copy(irq.begin(), irq.end(), w.rom.begin() + 0x341010);
        std::copy(std::begin(raster), std::end(raster), w.rom.begin() + 0x341000);
        w.cpu.s.seg[emu::DS] = 0;
        w.scanline = line;
        w.line_cycle = cycle;
        w.io[0x03] = 0;
        w.ram[0xC80E] = 2;
        w.ram[0x604] = 0x75;
        int vblanks = 0;
        w.on_irq = [&](const emu::Wsc &x) { vblanks += x.scanline == 144; };
        std::unique_ptr<game::Fiber> fiber;
        bool done = false;
        if (native) {
            fiber = game::start_yielding(w, game::wait_vblank);
            done = fiber->resume();
            w.discard_stall();
        }
        for (int steps = 0; !f.returned() && steps < 20000; ++steps) {
            if (tools::tick_vblank_wait(w)) {
                ++w.frame_count;
                w.begin_frame();
            }
            if (native && !done && !w.cpu.s.halted && !w.line_parked() &&
                w.cpu.s.ip == game::Machine::kTrampolineIp) {
                done = fiber->resume();
                w.discard_stall();
            }
        }
        CAPTURE(native);
        CAPTURE(w.cpu.s.ip);
        CAPTURE(w.cpu.s.seg[emu::CS]);
        CAPTURE(w.cpu.s.r[emu::SP]);
        CAPTURE(w.ram[0xC80E]);
        CAPTURE(w.scanline);
        CAPTURE(vblanks);
        REQUIRE(f.returned());
        CHECK(vblanks == 1);
        CHECK(w.ram[0x604] == 0x76);
        CHECK(w.ram[0xC80E] == 1);
        return w.snapshot();
    };
    for (const auto &[line, cycle] : {std::pair{143, 244}, std::pair{154, 228}}) {
        CAPTURE(line);
        CAPTURE(cycle);
        const auto original = run(false, line, cycle), native = run(true, line, cycle);
        CHECK(original.io == native.io);
        CHECK(std::equal(std::begin(original.cpu.s.r), std::end(original.cpu.s.r), native.cpu.s.r));
        CHECK(std::equal(std::begin(original.cpu.s.seg), std::end(original.cpu.s.seg), native.cpu.s.seg));
        CHECK(original.cpu.s.ip == native.cpu.s.ip);
        CHECK(original.scanline == native.scanline);
        CHECK(original.line_cycle == native.line_cycle);
        CHECK(original.frame_count == native.frame_count);
    }
}

TEST_CASE("difftest scanline clock leaves the first visible raster IRQ pending on both sides") {
    const uint8_t poll[] = {0x50, 0x1E, 0xE4, 0x02, 0x3C, 0x8D, 0x73, 0xFA, 0x1F, 0x58, 0xCB};
    // A line-0 handler that changes the next compare and an effect counter. The original used to
    // take it after its poll succeeded, while the native had already returned with the IRQ pending.
    const uint8_t raster[] = {0x50, 0xB0, 0x10, 0xE6, 0xB6, 0xB0, 0x01, 0xE6,
                              0x03, 0xFE, 0x06, 0xB7, 0xD6, 0x58, 0xCF};
    auto run = [&](bool native, int line, int cycle) {
        FrameMachine f("wait_visible_scanline");
        auto &w = f.w;
        f.enter(0x0051);
        std::copy(std::begin(poll), std::end(poll), w.rom.begin() + 0x340051);
        std::copy(std::begin(raster), std::end(raster), w.rom.begin() + 0x341000);
        w.cpu.s.seg[emu::DS] = 0;
        w.cpu.s.r[emu::AX] = 0xABCD;
        w.io[0xB2] = 0x10;
        w.io[0xB4] = 0;
        w.io[0x03] = 0;
        w.scanline = line;
        w.line_cycle = cycle;
        w.ram[0xD6B7] = 7;
        std::unique_ptr<game::Fiber> fiber;
        bool done = false;
        if (native) {
            fiber = game::start_yielding(w, game::wait_visible_scanline);
            done = fiber->resume();
            w.discard_stall();
        }
        for (int steps = 0; !f.returned() && steps < 10000; ++steps) {
            if (tools::tick_scanline_wait(w)) {
                ++w.frame_count;
                w.begin_frame();
            }
            if (native && !done && !w.cpu.s.halted && !w.line_parked() &&
                w.cpu.s.ip == game::Machine::kTrampolineIp) {
                done = fiber->resume();
                w.discard_stall();
            }
        }
        REQUIRE(f.returned());
        CHECK(w.ram[0xD6B7] == 7);
        CHECK(w.io[0x03] == 0);
        CHECK(w.io[0xB4] == (line < 141 ? 0 : 0x10));
        CHECK(w.scanline == (line < 141 ? line : 0));
        CHECK(w.cpu.s.r[emu::AX] == 0xABCD);
        return w.snapshot();
    };
    for (const auto &[line, cycle] : {std::pair{140, 252}, std::pair{143, 252}, std::pair{158, 252}}) {
        CAPTURE(line);
        CAPTURE(cycle);
        const auto original = run(false, line, cycle), native = run(true, line, cycle);
        CHECK(original.io == native.io);
        CHECK(original.scanline == native.scanline);
        CHECK(original.line_cycle == native.line_cycle);
        CHECK(original.frame_count == native.frame_count);
        CHECK(original.cpu.s.flags == native.cpu.s.flags);
    }
}

TEST_CASE("NPC terrain exemption still reserves all four OAM entries") {
    emu::Wsc w(std::vector<uint8_t>(4 * 1024 * 1024), 0);
    w.ram.fill(0);
    w.io[0x60] = 0x80; // WSC RAM above 3FFFh holds NPC records and OAM staging
    game::Machine m(w);
    m.ss() = 0;
    for (int slot = 0; slot < 16; ++slot)
        m.wr8(0, uint16_t(0xD5AE + slot * 0x14), 0xFF);
    // Two visible, terrain-exempt NPCs must occupy distinct four-sprite blocks.
    for (int slot = 0; slot < 2; ++slot) {
        const uint16_t npc = uint16_t(0xD5AE + slot * 0x14);
        m.wr8(0, npc, 0);
        m.wr8(0, uint16_t(npc + 0x11), 0x10);
        m.wr16(0, uint16_t(npc + 6), uint16_t(32 + slot * 32));
        m.wr16(0, uint16_t(npc + 8), 32);
    }
    game::advance_and_draw_npcs(m);
    CHECK(w.ram[0xC80F] == 8);
    CHECK(m.rd16(0, 0xC000) == 0x18);
    CHECK(m.rd16(0, 0xC010) == 0x20);
    CHECK(w.ram[0xC003] == 16);
    CHECK(w.ram[0xC013] == 32);
}

TEST_CASE(
    "host Esper decision freezes a partial frame, resumes without dispatch cost, and cancels on reset") {
    auto make = [](bool enabled) {
        auto w = std::make_unique<emu::Wsc>(std::vector<uint8_t>(0x400000), 0);
        w->rom[0x340200] = 0xF4; // HLT after the native returns
        w->io[0x60] = 0x80;
        w->cpu.s.seg[emu::CS] = 0x4000;
        w->cpu.s.seg[emu::SS] = 0;
        w->cpu.s.r[emu::SP] = 0x2000;
        w->cpu.s.flags = 0;
        w->cpu.s.ip = 0x100;
        w->ram[0x2000] = 0;
        w->ram[0x2001] = 2;
        auto &session = game::Session::of(*w);
        session.confirm_esper_changes = enabled;
        w->set_native(0x340100, [&session](emu::Wsc &hw) {
            return session.continue_fiber(
                game::start_yielding(hw,
                                     [](game::Machine &m) {
                                         if (game::Session::of(m.wsc()).allow_esper_change(m, 0, 128, 129)) {
                                             m.wr8(0, 0xD00F, 129);
                                             m.wr8(0, 0xD010, 25);
                                         }
                                         m.ret();
                                     }),
                16);
        });
        return w;
    };
    auto original = make(false), yes = make(true), no = make(true);
    original->run_frame();
    for (auto *w : {yes.get(), no.get()}) {
        auto &s = game::Session::of(*w);
        w->run_frame();
        REQUIRE(w->host_paused);
        REQUIRE(s.suspended() == 1);
        const auto frozen = w->snapshot();
        for (int frame = 0; frame < 300; ++frame)
            w->run_frame();
        const auto after = w->snapshot();
        CHECK(frozen.ram == after.ram);
        CHECK(frozen.io == after.io);
        CHECK(frozen.cpu.cycles == after.cpu.cycles);
        CHECK(frozen.frame_count == after.frame_count);
        CHECK(frozen.scanline == after.scanline);
        CHECK(frozen.line_cycle == after.line_cycle);
        for (int hold = 0; hold < game::Session::kEsperInputHold; ++hold)
            s.esper_input(*w, 0);
        s.esper_input(*w, 0);
        s.esper_input(*w, w == yes.get() ? emu::KEY_A : emu::KEY_B);
        CHECK_FALSE(w->host_paused);
        CHECK(s.suspended() == 0);
        w->run_frame();
        CHECK(w->cpu.cycles == original->cpu.cycles);
        CHECK(w->frame_count == original->frame_count);
        CHECK(w->scanline == original->scanline);
        CHECK(w->line_cycle == original->line_cycle);
        CHECK(w->io == original->io);
    }
    CHECK(yes->ram == original->ram);
    CHECK(no->ram[0xD00F] == 0);
    CHECK(no->ram[0xD010] == 0);
    no->ram[0xD00F] = original->ram[0xD00F];
    no->ram[0xD010] = original->ram[0xD010];
    CHECK(no->ram == original->ram);
    auto reset = make(true);
    reset->run_frame();
    REQUIRE(reset->host_paused);
    game::reset_natives(*reset, "random_byte");
    CHECK_FALSE(reset->host_paused);
    CHECK_FALSE(game::Session::of(*reset).esper_change());
    CHECK(game::Session::of(*reset).suspended() == 0);
}
