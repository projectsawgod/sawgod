#include "game/battle_rewards.h"
#include "game/hooks.h"
#include "game/session.h"
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

using namespace sawgod;
namespace {
std::vector<uint8_t> reward_test_rom() {
    const char *path = std::getenv("SAWGOD_WSC_ROM");
    if (!path) {
        MESSAGE("Set SAWGOD_WSC_ROM for reward oracle comparisons");
        return {};
    }
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    std::vector<uint8_t> rom{std::istreambuf_iterator<char>(input), {}};
    REQUIRE(rom.size() == 0x400000);
    return rom;
}

void seed_reward_entry(emu::Wsc &w, uint16_t entry, uint16_t ss) {
    w.io[0x60] = 0x80;
    game::Machine m(w);
    m.cs() = 0x6000;
    w.cpu.s.ip = entry;
    m.ss() = ss;
    m.ds() = 0x0123;
    m.es() = 0x0234;
    m.sp() = 0x3000;
    m.wr16(ss, m.sp(), 0x1234);
    m.ax() = 0xABCD;
    m.bx() = 0x2345;
    m.cx() = 0x6700;
    m.dx() = 0x89AB;
    m.si() = 0x4567;
    m.di() = 0x5678;
    m.bp() = 0x6789;
    m.flags() = emu::DF;
}

void reward_word32(game::Machine &m, uint16_t at, uint32_t value) {
    m.wr16(m.ss(), at, uint16_t(value));
    m.wr16(m.ss(), uint16_t(at + 2), uint16_t(value >> 16));
}

uint32_t reward_word32(game::Machine &m, uint16_t at) {
    return uint32_t(m.rd16(m.ss(), at)) | uint32_t(m.rd16(m.ss(), uint16_t(at + 2))) << 16;
}

void compare_reward_result(const emu::Wsc &original, const emu::Wsc &native, uint16_t ss) {
    CHECK(std::equal(std::begin(original.cpu.s.r), std::end(original.cpu.s.r), std::begin(native.cpu.s.r)));
    CHECK(std::equal(std::begin(original.cpu.s.seg), std::end(original.cpu.s.seg),
                     std::begin(native.cpu.s.seg)));
    CHECK(original.cpu.s.ip == native.cpu.s.ip);
    constexpr uint16_t control_flags = emu::DF | emu::IF | emu::TF;
    CHECK((original.cpu.s.flags & control_flags) == (native.cpu.s.flags & control_flags));
    CHECK(original.io == native.io);
    CHECK(original.sram == native.sram);
    const size_t stack = (size_t(ss) << 4) + 0x3000;
    for (size_t at = 0; at < original.ram.size(); ++at) {
        // The native ABI omits dead push/pop bytes, as in the recording difftests.
        if (at >= stack - 0x100 && at < stack)
            continue;
        if (original.ram[at] != native.ram[at]) {
            CAPTURE(at);
            CHECK(original.ram[at] == native.ram[at]);
            break;
        }
    }
}

void run_reward_original(emu::Wsc &w) {
    int steps = 0;
    while (w.cpu.s.ip != 0x1234 && steps++ < 100000)
        w.cpu.step();
    REQUIRE(w.cpu.s.ip == 0x1234);
}
} // namespace

TEST_CASE("battle rewards: money and total match ROM at caps, carries, overflow and nonzero SS") {
    const auto rom = reward_test_rom();
    if (rom.empty())
        return;
    struct Addition {
        uint32_t before, reward, sum, stored;
    };
    const Addition cases[] = {{0, 0, 0, 0},
                              {100, 25, 125, 125},
                              {999998, 1, 999999, 999999},
                              {999998, 2, 1000000, 999999},
                              {999999, 0, 999999, 999999},
                              {1000000, 0, 1000000, 999999},
                              {0xFFFF, 1, 0x10000, 0x10000},
                              {0xFFFFFFFF, 1, 0, 0},
                              {0xFFFFFFF0, 0x20, 0x10, 0x10},
                              {0x80000000, 0x80000000, 0, 0},
                              {0xFFFF, 0xFFFF, 0x1FFFE, 0x1FFFE},
                              {0, 0xFFFFFFFF, 0xFFFFFFFF, 999999}};
    for (const auto [entry, destination] : {std::pair{0x38F5, 0xD0A2}, std::pair{0x392C, 0xDD6F}}) {
        for (uint16_t ss : {0, 0x0100}) {
            for (const Addition c : cases) {
                CAPTURE(entry);
                CAPTURE(ss);
                CAPTURE(c.before);
                CAPTURE(c.reward);
                emu::Wsc original(rom, 0), native(rom, 0);
                seed_reward_entry(original, uint16_t(entry), ss);
                game::Machine m(original);
                reward_word32(m, uint16_t(destination), c.before);
                reward_word32(m, 0xDD6B, c.reward);
                native.restore(original.snapshot());
                run_reward_original(original);
                game::Machine n(native);
                if (entry == 0x38F5)
                    game::add_battle_reward_to_money(n);
                else
                    game::add_battle_reward_to_total(n);
                compare_reward_result(original, native, ss);
                CHECK(reward_word32(n, uint16_t(destination)) == c.stored);
                CHECK((uint32_t(n.dx()) << 16 | n.ax()) == c.sum);
                CHECK(reward_word32(n, 0xDD6B) == c.reward);
            }
        }
    }
}

TEST_CASE("battle rewards: formatter matches ROM including zero and full-width totals") {
    const auto rom = reward_test_rom();
    if (rom.empty())
        return;
    for (uint16_t ss : {0, 0x0100}) {
        for (uint32_t total : {0u, 1u, 0xFFFFu, 0x10000u, 999999u, 0xFFFFFFFFu}) {
            CAPTURE(ss);
            CAPTURE(total);
            emu::Wsc original(rom, 0), native(rom, 0);
            seed_reward_entry(original, 0x38A6, ss);
            game::Machine m(original);
            reward_word32(m, 0xDD6F, total);
            m.wr8(ss, 0xDC95, 7); // byte after the ten digits: the original zero-total quirk
            native.restore(original.snapshot());
            run_reward_original(original);
            game::Machine n(native);
            game::format_battle_reward_total(n);
            compare_reward_result(original, native, ss);
        }
    }
}

TEST_CASE("battle rewards: empty slots, zero-gold monsters and ordered value-changing hooks match ROM") {
    const auto rom = reward_test_rom();
    if (rom.empty())
        return;
    for (int scenario : {0, 1, 2}) {
        CAPTURE(scenario);
        emu::Wsc original(rom, 0), native(rom, 0);
        seed_reward_entry(original, 0x3963, 0);
        game::Machine m(original);
        reward_word32(m, 0xD0A2, 999990);
        reward_word32(m, 0xDD6B, 0xFFFFFFFF);
        reward_word32(m, 0xDD6F, 0xFFFFFFFF);
        for (int actor = 4; actor < 31; ++actor)
            m.wr8(0, uint16_t(0xD71A + actor * 32 + 2), 0xFF);
        if (scenario != 0) {
            for (int actor : {4, 7, 30}) {
                const uint16_t record = uint16_t(0xD71A + actor * 32);
                m.wr8(0, record + 2, 0);
                m.wr8(0, record + 31, scenario == 1 ? 172 : 0);
            }
        }
        native.restore(original.snapshot());
        std::vector<int> original_visits, native_visits;
        std::vector<game::hooks::Subscription> subscriptions;
        const auto subscribe = [&](emu::Wsc &w, std::vector<int> &visits) {
            auto &hooks = game::Session::of(w).hooks();
            subscriptions.push_back(
                hooks.subscribe(game::hooks::Value::BattleGold,
                                [&visits](game::Machine &state, int32_t gold, const auto &args) {
                                    CHECK(gold == 40);
                                    CHECK(state.ax() == 40);
                                    CHECK(reward_word32(state, 0xDD6F) == 0);
                                    CHECK(reward_word32(state, 0xD0A2) == 999990);
                                    visits.push_back(args.a);
                                    return gold * 2;
                                }));
            subscriptions.push_back(hooks.subscribe(
                game::hooks::Value::BattleGold, [&visits](game::Machine &, int32_t gold, const auto &args) {
                    CHECK(gold == 80);
                    visits.push_back(-args.a);
                    return -1; // The original bridge clamps the changed value to zero.
                }));
        };
        if (scenario == 2) {
            subscribe(original, original_visits);
            subscribe(native, native_visits);
        }
        int steps = 0;
        while (original.cpu.s.ip != 0x1234 && steps++ < 100000) {
            // The ROM has no mod hook: inject the same external input just after its gold load.
            if (scenario == 2 && m.cs() == 0x6000 && original.cpu.s.ip == 0x3993)
                m.ax() = uint16_t(std::clamp<int32_t>(
                    game::hooks::filter(game::hooks::Value::BattleGold, m, m.ax(), {uint8_t(m.cx())}), 0,
                    0xFFFF));
            original.cpu.step();
        }
        REQUIRE(original.cpu.s.ip == 0x1234);
        game::Machine n(native);
        game::award_battle_gold(n);
        compare_reward_result(original, native, 0);
        CHECK(reward_word32(n, 0xD0A2) == 999990);
        CHECK(reward_word32(n, 0xDD6B) == 0);
        CHECK(reward_word32(n, 0xDD6F) == 0);
        if (scenario == 2) {
            const std::vector<int> expected{4, -4, 7, -7, 30, -30};
            CHECK(original_visits == expected);
            CHECK(native_visits == expected);
        }
    }
}
