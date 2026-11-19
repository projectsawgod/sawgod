// Cheats (game/cheats.h): what each subscription does to its hook, and the field revive item with and
// without Unlimited Hearts (synthetic RAM, no game assets).
#include <doctest/doctest.h>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

#include "emu/wsc.h"
#include "game/bank35_world.h"
#include "game/bank36_battle.h"
#include "game/battle_actor_state.h"
#include "game/cheats.h"
#include "game/computed_handlers.h"
#include "game/hooks.h"
#include "game/machine.h"
#include "game/session.h"
#include "game/system.h"
#include "tools/modifier_audit.h"

using namespace sawgod::game;

namespace {
struct Rig {
    sawgod::emu::Wsc w{std::vector<uint8_t>(0x400000), 0x10000};
    Machine m{w};
    Cheats cheats;
    void set(CheatFlags f) { cheats.set(Session::of(w), f); }
};
int32_t meter(Rig &r, int actor, int32_t amount, int hp) {
    return hooks::filter(hooks::Value::BattleMeterChange, r.m, amount, {actor, hp});
}
} // namespace

TEST_CASE("cheats off: every hook keeps the game's own value") {
    Rig r;
    r.set({});
    CHECK(meter(r, 0, 12, 60) == 12);
    CHECK(meter(r, 4, 12, 40) == 12);
    CHECK(hooks::should(hooks::VB::SpendUse, r.m, true, {0, 0, 0x21}));
    CHECK(hooks::should(hooks::VB::SpendHeart, r.m, true, {1}));
    CHECK(hooks::should(hooks::VB::InflictStatus, r.m, true, {0, 0x02}));
}

TEST_CASE("No Damage: the party's damage is 0, heals and enemies untouched") {
    Rig r;
    r.set({.no_damage = true});
    CHECK(meter(r, 0, 12, 60) == 0);
    CHECK(meter(r, 3, 999, 60) == 0);
    CHECK(meter(r, 1, uint16_t(-20), 30) == uint16_t(-20)); // a heal
    CHECK(meter(r, 4, 12, 40) == 12);
    CHECK_FALSE(hooks::should(hooks::VB::InflictStatus, r.m, true, {0, 0x02})); // stone: no effect
    CHECK_FALSE(hooks::should(hooks::VB::InflictStatus, r.m, true, {3, 0x01})); // death
    CHECK_FALSE(hooks::should(hooks::VB::InflictStatus, r.m, true, {2, 0x10})); // poison
    CHECK(hooks::should(hooks::VB::InflictStatus, r.m, true, {4, 0x02}));       // on enemies as usual
}

TEST_CASE("One-Hit Kills: any damage to an enemy takes all its HP") {
    Rig r;
    r.set({.one_hit_kills = true});
    CHECK(meter(r, 4, 1, 40) == 40);
    CHECK(meter(r, 7, 500, 40) == 500);
    CHECK(meter(r, 5, 0, 40) == 0);   // a miss stays a miss
    CHECK(meter(r, 0, 12, 60) == 12); // the party as usual
}

TEST_CASE("Unbreakable Gear: the party never spends a use, enemies still do") {
    Rig r;
    r.set({.unbreakable_gear = true});
    CHECK_FALSE(hooks::should(hooks::VB::SpendUse, r.m, true, {0, 2, 0x21}));  // a party battle action
    CHECK_FALSE(hooks::should(hooks::VB::SpendUse, r.m, true, {1, -1, 0x05})); // the field menu
    CHECK_FALSE(hooks::should(hooks::VB::SpendUse, r.m, true, {2, -1, 0x0A})); // the refill item
    CHECK(hooks::should(hooks::VB::SpendUse, r.m, true, {0, 4, 0x80}));        // an enemy
}

TEST_CASE("God Mode gates direct battle commits, including self costs and status replacements") {
    for (bool god : {false, true}) {
        Rig r;
        r.w.io[0x60] = 0x80;
        r.set({.no_damage = god});
        for (int actor = 0; actor < 31; ++actor) {
            const uint16_t record = uint16_t(0xD71A + actor * 32);
            BattleActorState state(r.m, record);
            const bool protected_party = god && actor < 4;
            CAPTURE(god);
            CAPTURE(actor);
            r.m.wr16(0, uint16_t(record + 3), 999);
            state.write_hp(0); // instant kill and scripted/self damage bypass the animated meter
            CHECK(r.m.rd16(0, uint16_t(record + 3)) == (protected_party ? 999 : 0));
            r.m.wr16(0, uint16_t(record + 3), 100);
            state.write_hp(99);
            CHECK(r.m.rd16(0, uint16_t(record + 3)) == (protected_party ? 100 : 99));
            state.write_hp(200); // healing still works
            CHECK(r.m.rd16(0, uint16_t(record + 3)) == 200);
            for (int bit = 1; bit <= 128; bit *= 2) {
                r.w.ram[record + 2] = 0;
                CHECK(state.write_status(uint8_t(bit)) == !protected_party);
                CHECK(r.w.ram[record + 2] == (protected_party ? 0 : bit));
                r.w.ram[record + 2] = uint8_t(bit);
                CHECK(state.write_status(0));
                CHECK(r.w.ram[record + 2] == 0);
            }
            r.w.ram[record + 2] = 0x10;
            state.write_status(0x08); // clear poison while attempting to add blindness
            CHECK(r.w.ram[record + 2] == (protected_party ? 0 : 8));
            r.w.ram[record + 7] = 99;
            CHECK(state.write_stat(7, 20) == !protected_party);
            CHECK(r.w.ram[record + 7] == (protected_party ? 99 : 20));
            CHECK(state.write_stat(7, 100));
            CHECK(r.w.ram[record + 7] == 100);
        }
    }
}

TEST_CASE("Unbreakable Gear gates the travel handler's early inventory and equipment charge") {
    for (bool unbreakable : {false, true})
        for (uint16_t item : {uint16_t(0xD07C), uint16_t(0xD00F)})
            for (int uses : {1, 20, 254}) {
                Rig r;
                r.w.io[0x60] = 0x80;
                r.set({.unbreakable_gear = unbreakable});
                r.m.cs() = 0x5000;
                r.m.si() = item;
                r.w.ram[item] = 17;
                r.w.ram[item + 1] = uint8_t(uses);
                use_destination_travel_item_core(r.m); // no destination: isolate the charge commit
                CAPTURE(unbreakable);
                CAPTURE(item);
                CAPTURE(uses);
                CHECK(r.w.ram[item + 1] == (unbreakable ? uses : uses == 1 ? 255 : uses - 1));
                CHECK(r.w.ram[item] == (!unbreakable && uses == 1 ? 255 : 17));
                CHECK((r.m.flags() & sawgod::emu::CF) == 0);
            }
}

TEST_CASE("[ROM] modifier soak raster handlers match the original at every scanline") {
    using namespace sawgod;
    const char *path = std::getenv("SAWGOD_WSC_ROM");
    if (!path)
        return;
    std::ifstream input(path, std::ios::binary);
    const std::vector<uint8_t> rom{std::istreambuf_iterator<char>(input), {}};
    REQUIRE(rom.size() == 0x400000);
    emu::Wsc original(rom, 0), native(rom, 0);
    for (const auto &[address, handler] :
         {std::pair{0x35BC4Du, &irq_destination_warp}, std::pair{0x36C7C2u, &irq_battle_raster_shutter},
          std::pair{0x36C681u, &irq_battle_raster_sweep}, std::pair{0x364CBCu, &irq_battle_raster_wave},
          std::pair{0x377EFAu, &irq_nuclear_flash}, std::pair{0x377F50u, &irq_nuclear_wave}}) {
        for (int line = 0; line < 159; ++line) {
            CAPTURE(address);
            CAPTURE(line);
            for (size_t i = 0; i < native.ram.size(); ++i)
                native.ram[i] = uint8_t(i * 73 + line * 11);
            native.io.fill(0);
            native.io[0x60] = 0x80;
            native.io[0xC0] = 0xFF;
            native.io[0x03] = uint8_t(line);
            native.scanline = original.scanline = line;
            native.cpu.reset();
            native.cpu.s = {};
            native.cpu.s.seg[emu::CS] = uint16_t((address - 0x300000) >> 4) & 0xF000;
            native.cpu.s.seg[emu::DS] = 0x1234;
            native.cpu.s.seg[emu::ES] = 0x4321;
            native.cpu.s.ip = uint16_t(address);
            for (int reg = 0; reg < 8; ++reg)
                native.cpu.s.r[reg] = uint16_t(0x12AB + reg * 913 + line);
            native.cpu.s.r[emu::SP] = 0x3000;
            Machine m(native);
            m.wr16(0, 0x3000, 0x2222);
            m.wr16(0, 0x3002, 0x4000);
            m.wr16(0, 0x3004, emu::CF | emu::IF | emu::DF | emu::OF);
            original.ram = native.ram;
            original.io = native.io;
            original.cpu.reset();
            original.cpu.s = native.cpu.s;
            std::vector<std::pair<char, uint32_t>> original_io, native_io;
            auto observe = [](auto &sequence) {
                return [&sequence](char kind, uint32_t port) {
                    if (kind == 'I' || kind == 'O')
                        sequence.emplace_back(kind, port);
                };
            };
            original.on_bus = observe(original_io);
            native.on_bus = observe(native_io);
            int steps = 0;
            uint16_t temporary_bottom = 0x3000;
            do {
                original.cpu.step();
                temporary_bottom = std::min(temporary_bottom, original.cpu.s.r[emu::SP]);
            } while (original.cpu.pc() != 0x42222 && ++steps < 100);
            REQUIRE(steps < 100);
            handler(m);
            CHECK(native_io == original_io);
            CHECK(native.io == original.io);
            for (int reg = 0; reg < 8; ++reg)
                CHECK(native.cpu.s.r[reg] == original.cpu.s.r[reg]);
            for (int reg = 0; reg < 4; ++reg)
                CHECK(native.cpu.s.seg[reg] == original.cpu.s.seg[reg]);
            CHECK(native.cpu.s.ip == original.cpu.s.ip);
            CHECK(native.cpu.s.flags == original.cpu.s.flags);
            // Ignore only the original's actual temporary register/call stack, as in difftests.
            std::copy(native.ram.begin() + temporary_bottom, native.ram.begin() + 0x3000,
                      original.ram.begin() + temporary_bottom);
            const auto mismatch = std::mismatch(native.ram.begin(), native.ram.end(), original.ram.begin());
            CHECK_MESSAGE(bool(native.ram == original.ram),
                          "first different RAM byte: ", mismatch.first - native.ram.begin());
            original.on_bus = native.on_bus = nullptr;
        }
    }
}

TEST_CASE("Unlimited Hearts: the field revive item revives without spending a heart") {
    for (const bool cheat : {false, true}) {
        Rig r;
        r.set({.unlimited_hearts = cheat});
        r.w.out8(0x60, 0x80); // color mode: all 64 KB of RAM
        r.m.ds() = r.m.es() = 0;
        r.m.di() = 0xD000; // the member's record: dead (bit 0), 3 hearts (+0Ah low nibble)
        r.m.bx() = 0xC000; // the item's record: status mask at +4
        r.w.ram[0xD000] = 0x01;
        r.w.ram[0xD00A] = 0x83;
        r.w.ram[0xC004] = 0xFE;
        r.m.cs() = 0x5000;
        use_field_revive_item_core(r.m);
        CHECK((r.w.ram[0xD000] & 1) == 0);                 // revived
        CHECK(r.w.ram[0xD006] == 1);                       // at 1 HP
        CHECK((r.w.ram[0xD00A] & 0xF) == (cheat ? 3 : 2)); // hearts
        CHECK((r.w.ram[0xD00A] & 0xF0) == 0x80);
    }
}

TEST_CASE("God Mode: no field damage (damage floors, scripted HP losses); heals unchanged") {
    for (const bool god : {false, true}) {
        Rig r;
        r.set({.no_damage = god});
        CHECK(hooks::filter(hooks::Value::FieldDamage, r.m, 3, {0}) == (god ? 0 : 3)); // a damage floor
        const auto adjust = [&](uint8_t amount) {
            r.w.out8(0x60, 0x80); // color mode: all 64 KB of RAM
            r.m.ss() = 0;
            r.m.sp() = 0xE000;
            r.m.bx() = 0xD000; // the member's record: alive, HP at +6, max HP at +8
            r.w.ram[0xD000] = 0x00;
            r.w.ram[0xD006] = 50;
            r.w.ram[0xD007] = 0;
            r.w.ram[0xD008] = 100;
            r.w.ram[0xD009] = 0;
            r.m.ax() = amount; // the script's signed byte
            r.m.cs() = 0x5000;
            dialogue_adjust_member_hp_core(r.m);
            return r.w.ram[0xD006] | r.w.ram[0xD007] << 8;
        };
        CHECK(adjust(uint8_t(-10)) == (god ? 50 : 40)); // a scripted loss
        CHECK(adjust(uint8_t(-80)) == (god ? 50 : 1));  // never below 1 either way
        CHECK(adjust(5) == 55);                         // a heal
    }
}

TEST_CASE("Stat Growth: every stat gain times the factor, rounded half up") {
    Rig r;
    r.set({.stat_growth = 150});
    CHECK(hooks::filter(hooks::Value::StatGain, r.m, 1, {0xB, 0}) == 2);
    CHECK(hooks::filter(hooks::Value::StatGain, r.m, 5, {0xE, 0}) == 8);
    CHECK(hooks::filter(hooks::Value::StatGain, r.m, 12, {8, 1}) == 18);
    r.set({.stat_growth = 125});
    CHECK(hooks::filter(hooks::Value::StatGain, r.m, 4, {0xD, 1}) == 5);
    r.set({});
    CHECK(hooks::filter(hooks::Value::StatGain, r.m, 4, {0xD, 1}) == 4);
}

TEST_CASE("Stat Growth: an HP item's gain doubled at 2.00") {
    for (const int pct : {100, 200}) {
        Rig r;
        r.set({.stat_growth = pct});
        r.w.out8(0x60, 0x80);
        r.m.ds() = 0;
        r.m.di() = 0xD000;
        r.w.ram[0xD008] = 100; // max HP 100
        r.m.ax() = 5;          // the item's base gain
        r.m.cx() = 200;        // its threshold (above the max HP: the full gain)
        r.m.bx() = 0;          // its random bonus: none
        r.m.cs() = 0x5000;
        add_field_maximum_hp_core(r.m);
        CHECK((r.w.ram[0xD008] | r.w.ram[0xD009] << 8) == (pct == 100 ? 105 : 110));
    }
}

TEST_CASE("Encounters: off never starts one, half skips every second one, normal keeps the roll") {
    Rig r;
    r.set({.encounters = 2});
    CHECK_FALSE(hooks::should(hooks::VB::RandomEncounter, r.m, true, {0}));
    r.set({.encounters = 1});
    int started = 0;
    for (int i = 0; i < 10; ++i)
        started += hooks::should(hooks::VB::RandomEncounter, r.m, true, {0});
    CHECK(started == 5);
    r.set({});
    CHECK(hooks::should(hooks::VB::RandomEncounter, r.m, true, {0}));
}

TEST_CASE("Gold: each enemy's reward times the multiplier") {
    Rig r;
    r.set({.gold = 200});
    CHECK(hooks::filter(hooks::Value::BattleGold, r.m, 40, {4}) == 80);
    r.set({.gold = 50});
    CHECK(hooks::filter(hooks::Value::BattleGold, r.m, 41, {4}) == 21);
    r.set({.gold = 0});
    CHECK(hooks::filter(hooks::Value::BattleGold, r.m, 40, {4}) == 0);
    r.set({});
    CHECK(hooks::filter(hooks::Value::BattleGold, r.m, 40, {4}) == 40);
}

TEST_CASE("Damage Boost: party hits scale once, cap, and leave other HP changes alone") {
    Rig r;
    for (const int factor : {1, 2, 4}) {
        CAPTURE(factor);
        r.set({.damage = factor});
        for (int source = 0; source < 4; ++source)
            for (int target = 4; target < 8; ++target) {
                CHECK(hooks::filter(hooks::Value::BattleMeterChange, r.m, 123, {target, 20000, source}) ==
                      123 * factor);
                CHECK(hooks::filter(hooks::Value::BattleMeterChange, r.m, 5000, {target, 20000, source}) ==
                      (factor == 1 ? 5000 : 9999));
                CHECK(hooks::filter(hooks::Value::BattleMeterChange, r.m, 0, {target, 20000, source}) == 0);
                CHECK(hooks::filter(hooks::Value::BattleMeterChange, r.m, uint16_t(-20),
                                    {target, 20000, source}) == uint16_t(-20));
            }
        CHECK(hooks::filter(hooks::Value::BattleMeterChange, r.m, 123, {0, 999, 4}) == 123);
        CHECK(hooks::filter(hooks::Value::BattleMeterChange, r.m, 123, {4, 999, 5}) == 123);
        CHECK(hooks::filter(hooks::Value::BattleMeterChange, r.m, 123, {0, 999, 1}) == 123);
    }
    r.set({.one_hit_kills = true, .damage = 4});
    CHECK(hooks::filter(hooks::Value::BattleMeterChange, r.m, 1, {4, 20000, 0}) == 20000);
    r.set({});
    CHECK(meter(r, 4, 123, 999) == 123);
}

TEST_CASE("teleport leaves the party menu and preserves its request for the field") {
    Rig r;
    r.set({});
    CHECK(hooks::filter(hooks::Value::PartyMenuKeys, r.m, 0) == 0);
    r.cheats.request_teleport(3);
    CHECK(hooks::filter(hooks::Value::PartyMenuKeys, r.m, 0) == sawgod::emu::KEY_B);
    CHECK(hooks::filter(hooks::Value::FieldTeleport, r.m, 0) == 3);
    CHECK(hooks::filter(hooks::Value::PartyMenuKeys, r.m, 0) == 0);
    CHECK(hooks::filter(hooks::Value::FieldTeleport, r.m, 0) == 0);
}

TEST_CASE("no enemy is exempt from offensive modifiers, the roaming Suzaku and the bosses included") {
    Rig r;
    r.w.io[0x60] = 0x80;
    for (const CheatFlags flags :
         {CheatFlags{.no_damage = true}, CheatFlags{.one_hit_kills = true}, CheatFlags{.damage = 2},
          CheatFlags{.damage = 4}, CheatFlags{true, true, true, true, 400, 2, 400, false, 4}}) {
        r.set(flags);
        for (int actor = 4; actor < 31; ++actor)
            for (int type : {192, 196, 197, 199}) {
                r.w.ram[0xD71A + actor * 32 + 31] = uint8_t(type);
                const int expected = flags.one_hit_kills ? 5000 : 123 * flags.damage;
                CHECK(hooks::filter(hooks::Value::BattleMeterChange, r.m, 123, {actor, 5000, 0}) == expected);
                CHECK(meter(r, actor, uint16_t(-20), 3000) == uint16_t(-20));
            }
    }
}

TEST_CASE("[ROM] the roaming Suzaku's monster data, and One-Hit Kills commit through the real meter") {
    const char *path = std::getenv("SAWGOD_WSC_ROM");
    if (!path) {
        MESSAGE("Set SAWGOD_WSC_ROM for monster data and native battle checks");
        return;
    }
    std::ifstream input(path, std::ios::binary);
    const std::vector<uint8_t> rom{std::istreambuf_iterator<char>(input), {}};
    REQUIRE(rom.size() == 0x400000);
    auto word = [&](size_t at) { return int(rom[at] | rom[at + 1] << 8); };
    std::vector<int> maximum_defense, resist_everything;
    for (int type = 0; type < 200; ++type) {
        const size_t at = 0x360000 + word(0x36D1FB + type * 2);
        if (rom[at + 6] == 255)
            maximum_defense.push_back(type);
        for (int slot = 0; slot < rom[at + 2]; ++slot)
            if (rom[at + 11 + slot] == 222)
                resist_everything.push_back(type);
    }
    CHECK(maximum_defense == std::vector<int>{197});
    CHECK(resist_everything == std::vector<int>{197, 199}); // God is protected from magic, still winnable
    for (int type : {192, 197, 199}) {
        sawgod::emu::Wsc w(rom, 0);
        Machine m(w);
        w.io[0x60] = 0x80;
        m.ss() = m.ds() = 0;
        m.es() = m.cs() = 0x6000;
        m.sp() = 0x3000;
        m.si() = 0xD6D5;
        m.cx() = 4; // first enemy actor; SI points to its type/count pair
        w.ram[0xD6D5] = uint8_t(type);
        w.ram[0xD6D6] = 1;
        init_enemy_group_actors(m);
        CHECK(w.ram[0xD71A + 4 * 32 + 31] == type);
        if (type == 197) {
            CHECK(m.rd16(0, 0xD71A + 4 * 32 + 3) == 5000);
            CHECK(w.ram[0xD71A + 4 * 32 + 8] == 255);
            CHECK(w.ram[0xD71A + 4 * 32 + 11] == 255);
        }
        Cheats cheats;
        cheats.set(Session::of(w), {true, true, true, true, 400, 2, 400, false, 4});
        w.ram[0xDBFE] = 0;
        w.ram[0xDBFF] = 4;
        m.dx() = 123;
        set_battle_meter_target(m);
        CHECK(m.rd16(0, 0xD71A + 4 * 32 + 3) == 0);
    }
    // The actual ERASER99 field handler queues dialogue 17 at the generator, not a battle cheat.
    sawgod::emu::Wsc w(rom, 0);
    Machine m(w);
    w.io[0x60] = 0x80;
    m.cs() = 0x5000;
    m.ss() = m.ds() = 0;
    m.sp() = 0x3000;
    w.ram[0xD0DC] = 196;
    use_special_progression_item_core(m);
    CHECK(w.ram[0xC825] == 1);
    CHECK(w.ram[0xC826] == 9);
    CHECK(w.ram[0xC827] == 17);
    CHECK(w.ram[0xD0DD] == 4);
    CHECK(w.ram[0xD0DE] == 8);
}

TEST_CASE("modifier audit detects effects that bypass hooks and ignores external movie fixtures") {
    for (int effect = 0; effect < 5; ++effect) {
        CAPTURE(effect);
        Rig r;
        r.w.io[0x60] = 0x80;
        sawgod::tools::ModifierAudit audit(r.w);
        audit.before_cheats();
        CheatFlags flags{.no_damage = true, .unbreakable_gear = true};
        r.set(flags);
        REQUIRE(audit.start(flags, {}, {}));
        hooks::emit(hooks::Event::BattleStart, r.m);
        hooks::emit(hooks::Event::BattleRoundStart, r.m);
        constexpr int actor = 0xD71A;
        r.w.ram[actor + 3] = 100;
        r.w.ram[actor + 14] = 33;
        r.w.ram[actor + 15] = 5;
        audit.before_frame();
        if (effect == 0)
            r.w.ram[actor + 3] = 20; // the movie's direct RAM fixtures are explicitly outside the audit
        else if (effect == 1)
            r.m.wr16(0, actor + 3, 20); // bypass BattleMeterChange
        else if (effect == 2)
            r.m.wr8(0, actor + 2, 8); // blindness, bypass InflictStatus
        else if (effect == 3)
            r.m.wr8(0, actor + 15, 4); // bypass SpendUse
        else {
            r.w.cpu.fetching = true;
            r.w.read8(0x40000); // interpreter fallback is fatal, never a route divergence
            r.w.cpu.fetching = false;
        }
        audit.frame(1);
        CHECK(audit.healthy() == (effect == 0));
    }
}

TEST_CASE("modifier audit validates both revival heart paths and paid cost after two-phase stores") {
    for (bool unlimited : {false, true}) {
        for (bool paid : {false, true}) {
            Rig r;
            r.w.io[0x60] = 0x80;
            r.w.ram[0xD00A] = 0x83;
            r.w.ram[0xD0A2] = 0xD2; // 1234 gold
            r.w.ram[0xD0A3] = 4;
            sawgod::tools::ModifierAudit audit(r.w);
            audit.before_cheats();
            CheatFlags flags{.unlimited_hearts = unlimited};
            r.set(flags);
            REQUIRE(audit.start(flags, {}, {}));
            r.m.si() = r.m.di() = 0xD000;
            if (paid) {
                r.m.wr16(0, 0xD0A4, 0);
                r.m.wr16(0, 0xD0A2, 1134);
            }
            const bool spend = hooks::should(hooks::VB::SpendHeart, r.m, true, {paid ? 1 : 0});
            r.m.wr8(0, 0xD00A, 0x80);
            r.m.wr8(0, 0xD00A, uint8_t(0x80 | (spend ? 2 : 3)));
            audit.frame(1);
            CHECK(audit.healthy());
        }
    }
}

TEST_CASE("modifier audit checks God Mode's field damage on both sources and catches a wrong value") {
    for (bool god : {false, true}) {
        for (bool rogue : {false, true}) {
            CAPTURE(god);
            CAPTURE(rogue);
            Rig r;
            r.w.io[0x60] = 0x80;
            sawgod::tools::ModifierAudit audit(r.w);
            audit.before_cheats();
            CheatFlags flags{.no_damage = god};
            r.set(flags);
            std::vector<hooks::Subscription> extra;
            if (rogue) // an effect that changes field damage outside God Mode's rule
                extra.push_back(Session::of(r.w).hooks().subscribe(
                    hooks::Value::FieldDamage,
                    [](Machine &, int32_t v, const hooks::Args &) { return v + 1; }));
            REQUIRE(audit.start(flags, {}, {}));
            CHECK(hooks::filter(hooks::Value::FieldDamage, r.m, 3, {0}) == (god ? 0 : 3) + (rogue ? 1 : 0));
            CHECK(hooks::filter(hooks::Value::FieldDamage, r.m, 40, {1, 0xD000}) ==
                  (god ? 0 : 40) + (rogue ? 1 : 0));
            CHECK(audit.count("floor_damage_hooks") == 1);
            CHECK(audit.count("script_damage_hooks") == 1);
            CHECK(audit.healthy() == !rogue);
        }
    }
}

TEST_CASE("modifier audit separates random rolls from scripted fights sharing field event kind 8") {
    for (int mode : {0, 1, 2}) {
        Rig r;
        r.w.io[0x60] = 0x80;
        r.w.ram[0xC826] = 8;
        sawgod::tools::ModifierAudit audit(r.w);
        audit.before_cheats();
        CheatFlags flags{.encounters = mode};
        r.set(flags);
        REQUIRE(audit.start(flags, {}, {}));
        for (int roll = 0; roll < 2; ++roll)
            if (hooks::should(hooks::VB::RandomEncounter, r.m, true)) {
                hooks::emit(hooks::Event::BattleStart, r.m);
                hooks::emit(hooks::Event::BattleEnd, r.m);
            }
        hooks::emit(hooks::Event::BattleStart, r.m); // an independent scripted request with the same kind
        CHECK(audit.count("random_battles") == (mode == 0 ? 2 : mode == 1 ? 1 : 0));
        CHECK(audit.count("scripted_battles") == 1);
        CHECK(audit.count("skipped_encounters") == uint64_t(mode));
        CHECK(audit.healthy());
    }
}

TEST_CASE("[ROM] every modifier leaves the real chainsaw dispatcher's direct kill as the original") {
    const char *path = std::getenv("SAWGOD_WSC_ROM");
    if (!path)
        return;
    std::ifstream input(path, std::ios::binary);
    const std::vector<uint8_t> rom{std::istreambuf_iterator<char>(input), {}};
    REQUIRE(rom.size() == 0x400000);
    for (const CheatFlags flags :
         {CheatFlags{}, CheatFlags{.no_damage = true}, CheatFlags{.one_hit_kills = true},
          CheatFlags{.unbreakable_gear = true}, CheatFlags{.unlimited_hearts = true},
          CheatFlags{.stat_growth = 200}, CheatFlags{.stat_growth = 400}, CheatFlags{.encounters = 1},
          CheatFlags{.encounters = 2}, CheatFlags{.gold = 200}, CheatFlags{.gold = 400},
          CheatFlags{.damage = 2}, CheatFlags{.damage = 4},
          CheatFlags{true, true, true, true, 400, 2, 400, false, 4}}) {
        sawgod::emu::Wsc w(rom, 0);
        Machine m(w);
        w.io[0x60] = 0x80;
        m.cs() = 0x6000;
        m.ds() = m.ss() = 0;
        m.sp() = 0xFD00;
        m.di() = 0xDB76;
        constexpr int actor = 0xD71A, target = actor + 4 * 32;
        for (int slot = 1; slot < 31; ++slot)
            if (slot != 4)
                w.ram[actor + slot * 32 + 2] = 255;
        w.ram[0xD6D1] = 1;
        m.wr16(0, actor + 3, 999);
        m.wr16(0, actor + 5, 999);
        w.ram[actor + 7] = 99;
        w.ram[actor + 14] = 66; // actual chainsaw item
        w.ram[actor + 15] = 99;
        w.ram[actor + 31] = 177;
        m.wr16(0, target + 3, 5000);
        m.wr16(0, target + 5, 5000);
        w.ram[target + 8] = w.ram[target + 11] = 255;
        w.ram[target + 31] = w.ram[0xD6D5] = 197;
        w.ram[0xD6D6] = w.ram[0xD6D4] = 1;
        w.ram[0xDB86] = 255; // target's counteraction
        w.ram[0xDB89] = 4;
        w.ram[0xDBFC] = 66;
        w.ram[0xDBFF] = 4;
        Cheats cheats;
        cheats.set(Session::of(w), flags);
        Fiber fiber([&] { execute_battle_action(m); });
        for (int step = 0; step < 4096; ++step) {
            if (fiber.resume())
                break;
            if (m.rd16(0, target + 3) == 0)
                break; // the chainsaw's direct kill, before the defeat presentation
            REQUIRE(m.cs() == 0x4000);
            REQUIRE((w.cpu.s.ip == 9 || w.cpu.s.ip == 0x5C || w.cpu.s.ip == 0x51));
            if (w.cpu.s.ip == 0x5C)
                m.ax() = sawgod::emu::KEY_A;
            m.retf();
        }
        CHECK(m.rd16(0, target + 3) == 0);
        CHECK((w.ram[target + 2] & 1) != 0);
    }
}
