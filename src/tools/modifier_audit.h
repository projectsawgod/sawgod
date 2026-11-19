// Read-only runtime assertions for the modifier soak (a developer tool). Observe bus writes as well as hooks:
// an effect that forgot a hook must not silently pass. Movie pokes bypass the bus intentionally.
#pragma once

#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "game/cheats.h"
#include "game/machine.h"

namespace sawgod::tools {

class ModifierAudit {
  public:
    explicit ModifierAudit(emu::Wsc &w) : w_(w) {}

    void before_cheats() {
        using namespace game::hooks;
        auto &h = game::Session::of(w_).hooks();
        for (Value kind : {Value::BattleMeterChange, Value::StatGain, Value::BattleGold, Value::FieldDamage})
            subs_.push_back(h.subscribe(kind, [this, kind](auto &, int32_t v, const Args &) {
                raw_[int(kind)] = v;
                return v;
            }));
        for (VB kind :
             {VB::SpendUse, VB::SpendHeart, VB::InflictStatus, VB::RandomEncounter, VB::HarmBattleActor})
            subs_.push_back(h.subscribe(kind, [this, kind](auto &, bool v, const Args &) {
                raw_[100 + int(kind)] = v;
                return v;
            }));
    }

    bool start(game::CheatFlags flags, const std::string &route_in, const std::string &route_out) {
        using namespace game::hooks;
        flags_ = flags;
        if (!route_in.empty()) {
            route_in_.open(route_in);
            if (!route_in_)
                return false;
        }
        if (!route_out.empty()) {
            route_out_.open(route_out);
            if (!route_out_)
                return false;
        }
        auto &h = game::Session::of(w_).hooks();
        for (Value kind : {Value::BattleMeterChange, Value::StatGain, Value::BattleGold})
            subs_.push_back(h.subscribe(kind, [this, kind](auto &m, int32_t v, const Args &a) {
                const int32_t raw = raw_[int(kind)];
                if (kind == Value::BattleMeterChange) {
                    ++counts_["meter"];
                    const int damage = int16_t(uint16_t(raw));
                    int expected = raw;
                    if (damage > 0) {
                        if (a.a < 4 && flags_.no_damage)
                            expected = 0;
                        else if (a.a >= 4 && flags_.one_hit_kills)
                            expected = std::max(raw, a.b);
                        else if (a.a >= 4 && a.c < 4 && flags_.damage != 1)
                            expected = std::min(damage * flags_.damage, 9999);
                    }
                    if (v != expected)
                        fail("meter_filter", "actor " + std::to_string(a.a) + " raw " + std::to_string(raw) +
                                                 " expected " + std::to_string(expected) + " got " +
                                                 std::to_string(v));
                    expected_hp_[a.a] =
                        std::clamp(a.b - int16_t(uint16_t(v)), 0, int(word(actors + a.a * 32 + 5)));
                    meter_pending_[a.a] = true;
                    if (damage > 0 && a.a >= 4 && a.c < 4)
                        ++counts_["party_hits"];
                    if (a.a >= 4)
                        ++counts_["hit_type_" + std::to_string(w_.ram[actors + a.a * 32 + 31])];
                } else {
                    const int factor = kind == Value::StatGain ? flags_.stat_growth : flags_.gold;
                    const int expected =
                        kind == Value::StatGain && raw <= 0 ? raw : int((int64_t(raw) * factor + 50) / 100);
                    ++counts_[kind == Value::StatGain ? "stat_gains" : "gold_rewards"];
                    if (v != expected)
                        fail(kind == Value::StatGain ? "stat_filter" : "gold_filter",
                             "raw " + std::to_string(raw) + " expected " + std::to_string(expected) +
                                 " got " + std::to_string(v));
                    if (kind == Value::StatGain) {
                        check_stats();
                        const int addr = a.b == 0 ? m.si() : m.di() + a.a;
                        const int cap = a.a == 8 ? 999 : 99;
                        const int before = cap == 999 ? word(addr) : w_.ram[addr];
                        int after = std::min(before + std::clamp(v, 0, cap), cap);
                        if (a.b == 0 && cap == 99 && v == raw && before + raw > cap)
                            after = before; // unmodified mutant overflow is dropped by the ROM
                        stat_pending_ = {addr, after, cap};
                    } else {
                        gold_expected_ = std::min<uint32_t>(money() + std::clamp(v, 0, 65535), 999999);
                        gold_pending_ = true;
                    }
                }
                return v;
            }));
        // God Mode zeroes field HP losses (damage floors, scripted losses); nothing else changes them.
        subs_.push_back(h.subscribe(Value::FieldDamage, [this](auto &, int32_t v, const Args &a) {
            const int32_t raw = raw_[int(Value::FieldDamage)];
            ++counts_[a.a == 0 ? "floor_damage_hooks" : "script_damage_hooks"];
            const int32_t expected = flags_.no_damage ? 0 : raw;
            if (v != expected)
                fail("field_damage_filter", "source " + std::to_string(a.a) + " raw " + std::to_string(raw) +
                                                " expected " + std::to_string(expected) + " got " +
                                                std::to_string(v));
            return v;
        }));
        for (VB kind :
             {VB::SpendUse, VB::SpendHeart, VB::InflictStatus, VB::RandomEncounter, VB::HarmBattleActor})
            subs_.push_back(h.subscribe(kind, [this, kind](auto &m, bool v, const Args &a) {
                const bool raw = raw_[100 + int(kind)] != 0;
                bool expected = raw;
                if (kind == VB::HarmBattleActor) {
                    ++counts_["harm_gate_checks"];
                    if (flags_.no_damage && a.a < 4)
                        expected = false;
                    if (a.a < 4 && !v)
                        ++counts_["party_harm_blocked"];
                } else if (kind == VB::SpendUse) {
                    ++counts_[a.a == 0 ? "battle_use_hooks" : "field_use_hooks"];
                    if (flags_.unbreakable_gear && !(a.a == 0 && a.b >= 4))
                        expected = false;
                    if (a.a != 0)
                        field_use_ = true;
                } else if (kind == VB::SpendHeart) {
                    ++counts_[a.a == 0 ? "item_revives" : "paid_revives"];
                    if (flags_.unlimited_hearts)
                        expected = false;
                    const int addr = (a.a == 0 ? m.di() : m.si()) + 10;
                    heart_pending_ = {addr, (w_.ram[addr] & 15) - (v ? 1 : 0)};
                    if (a.a != 0) {
                        ++counts_["paid_charges"];
                        if (money_before_write_ != money() + 100)
                            fail("revive_price", "paid revival did not cost 100 gold");
                    }
                } else if (kind == VB::InflictStatus) {
                    ++counts_["status_hooks"];
                    if (flags_.no_damage && a.a < 4)
                        expected = false;
                } else {
                    ++counts_["encounter_rolls"];
                    if (flags_.encounters == 2)
                        expected = false;
                    else if (flags_.encounters == 1) {
                        skip_next_ = !skip_next_;
                        expected = raw && !skip_next_;
                    }
                    if (raw && !v)
                        ++counts_["skipped_encounters"];
                    random_pending_ = v;
                }
                if (v != expected)
                    fail("decision_filter", "hook " + std::to_string(int(kind)));
                return v;
            }));
        auto event = [&](Event e, auto fn) { subs_.push_back(h.subscribe(e, fn)); };
        event(Event::ItemDialog, [this](auto &, const Args &a) {
            // Watch the first field use even if the effect omitted SpendUse entirely.
            if (a.a != 0)
                field_use_ = true;
        });
        event(Event::BattleStart, [this](auto &, const Args &) {
            flush();
            phase_ = 1;
            round_ = false;
            ++starts_;
            ++counts_["battles"];
            if (random_pending_)
                ++counts_["random_battles"];
            else if (w_.ram[0xC826] == 8)
                ++counts_["scripted_battles"];
            random_pending_ = false;
        });
        event(Event::BattleRoundStart, [this](auto &, const Args &) {
            flush();
            round_ = true;
            ++counts_["rounds"];
            meter_pending_.fill(false);
        });
        event(Event::BattleRoundEnd, [this](auto &, const Args &) {
            flush();
            round_ = false;
        });
        event(Event::BattleRewards, [this](auto &, const Args &) {
            flush();
            round_ = false;
            phase_ = 2;
            if (w_.ram[0xD70E] & 4)
                ++counts_["escape_rewards"];
            else if (w_.ram[0xDCC2] & 2)
                ++counts_["victory_rewards"];
        });
        event(Event::BattleEnd, [this](auto &, const Args &) {
            flush();
            round_ = false;
            phase_ = 0;
            ++ends_;
            if (w_.ram[0xD70E] & 4)
                ++counts_["escapes"];
            else if (w_.ram[0xDCC2] & 2)
                ++counts_["victories"];
        });
        w_.on_bus = [this, old = w_.on_bus](char kind, uint32_t addr) {
            // Complete the previous write before examining the next bus access. HP words are checked
            // after their high byte; a low-byte carry/borrow is not a transient HP loss.
            flush();
            if (old)
                old(kind, addr);
            if (kind == 'F') {
                ++counts_["interpreted_fetches"];
                fail("interpreter", "fetch at " + std::to_string(addr) + " PC " +
                                        std::to_string(w_.cpu.pc()) + " ROM " +
                                        std::to_string(w_.rom_offset_of(w_.cpu.pc())));
            }
            if (kind != 'W' || addr >= 65536)
                return;
            if (addr == 0xD0A4)
                money_before_write_ = money();
            if (gold_pending_ && addr == 0xD0A3)
                pending_ = {6, 0, 0, 0};
            const int off = int(addr) - actors;
            if (round_ && off >= 0 && off < 31 * 32) {
                const int actor = off / 32, field = off % 32;
                if (field == 4) {
                    pending_ = {1, actor, word(actors + actor * 32 + 3), 0};
                    // The low byte has already been written. Use the last stable HP when available.
                    pending_[2] = hp_valid_[actor] ? last_hp_[actor] : pending_[2];
                } else if (field == 3) {
                    last_hp_[actor] = word(actors + actor * 32 + 3);
                    hp_valid_[actor] = true;
                } else if (field == 2) {
                    pending_ = {2, actor, w_.ram[addr], 0};
                } else if (field >= 15 && (field & 1)) {
                    pending_ = {3, int(addr), w_.ram[addr], w_.ram[addr - 1]};
                }
            }
            if (loaded_ && phase_ == 0 && field_use_ && field_count_address(int(addr)))
                pending_ = {5, int(addr), w_.ram[addr], w_.ram[addr - 1]};
        };
        return true;
    }

    void before_frame() {
        flush();
        hp_valid_.fill(false); // movie writes are external fixtures, never game effects
    }

    bool frame(int done) {
        flush();
        check_stats();
        if (heart_pending_[0]) {
            ++counts_["heart_commits"];
            if ((w_.ram[heart_pending_[0]] & 15) != heart_pending_[1])
                fail("heart_commit", "address " + std::to_string(heart_pending_[0]));
            heart_pending_ = {};
        }
        frames_ = done;
        loaded_ |= w_.ram[0xD0DC] != 0 && done >= 2200;
        const auto generation = game::Session::of(w_).generation();
        if (generation != generation_) {
            phase_ = 0;
            round_ = false;
            loaded_ = false;
            field_use_ = false;
            random_pending_ = false;
            generation_ = generation;
        }
        const std::string route = std::to_string(phase_) + " " + std::to_string(starts_) + " " +
                                  std::to_string(ends_) + " " + std::to_string(generation) + " " +
                                  std::to_string(w_.ram[0xD0DC]);
        if (route_out_)
            route_out_ << route << '\n';
        if (route_in_.is_open()) {
            std::string baseline;
            if (!std::getline(route_in_, baseline)) {
                fail("baseline", "route ended before recording");
                return false;
            }
            if (route != baseline) {
                divergence_ = "battle/reward/field boundary: baseline " + baseline + "; actual " + route;
                return false;
            }
        }
        return counts_["interpreted_fetches"] == 0;
    }

    bool finish(const std::string &path) {
        flush();
        check_stats();
        std::ofstream out(path);
        if (!out)
            return false;
        out << "{\"frames\":" << frames_ << ",\"divergence\":" << quote(divergence_)
            << ",\"flags\":{\"god_mode\":" << flags_.no_damage
            << ",\"one_hit_kills\":" << flags_.one_hit_kills
            << ",\"unbreakable_gear\":" << flags_.unbreakable_gear
            << ",\"unlimited_revives\":" << flags_.unlimited_hearts
            << ",\"stat_boost\":" << flags_.stat_growth << ",\"gold\":" << flags_.gold
            << ",\"encounters\":" << flags_.encounters << ",\"damage\":" << flags_.damage << "},\"counts\":{";
        bool comma = false;
        for (const auto &[key, value] : counts_) {
            out << (comma ? "," : "") << quote(key) << ':' << value;
            comma = true;
        }
        out << "},\"failures\":[";
        comma = false;
        for (const auto &failure : failures_) {
            out << (comma ? "," : "") << quote(failure);
            comma = true;
        }
        out << "]}\n";
        return bool(out);
    }

    bool healthy() const { return failures_.empty(); }
    uint64_t count(const std::string &name) const {
        const auto found = counts_.find(name);
        return found == counts_.end() ? 0 : found->second;
    }

  private:
    static constexpr int actors = 0xD71A;
    emu::Wsc &w_;
    game::CheatFlags flags_;
    std::vector<game::hooks::Subscription> subs_;
    std::map<int, int32_t> raw_;
    std::map<std::string, uint64_t> counts_;
    std::vector<std::string> failures_;
    std::ifstream route_in_;
    std::ofstream route_out_;
    int phase_ = 0, starts_ = 0, ends_ = 0, frames_ = 0;
    uint64_t generation_ = 0;
    bool round_ = false, loaded_ = false, skip_next_ = false, field_use_ = false, random_pending_ = false;
    std::array<int, 4> pending_{};
    std::array<int, 3> stat_pending_{};
    std::array<int, 2> heart_pending_{};
    uint32_t gold_expected_ = 0, money_before_write_ = 0;
    bool gold_pending_ = false;
    std::array<int, 31> last_hp_{}, expected_hp_{};
    std::array<bool, 31> hp_valid_{}, meter_pending_{};
    std::string divergence_;

    int word(int addr) const { return w_.ram[addr] | (w_.ram[addr + 1] << 8); }
    uint32_t money() const { return uint32_t(word(0xD0A2)) | uint32_t(word(0xD0A4)) << 16; }
    void check_stats() {
        if (!stat_pending_[0])
            return;
        const int actual = stat_pending_[2] == 999 ? word(stat_pending_[0]) : w_.ram[stat_pending_[0]];
        ++counts_["stat_commits"];
        if (actual != stat_pending_[1])
            fail("stat_commit", "address " + std::to_string(stat_pending_[0]) + " expected " +
                                    std::to_string(stat_pending_[1]) + " got " + std::to_string(actual));
        stat_pending_ = {};
    }
    static bool field_count_address(int addr) {
        if (addr >= 0xD07D && addr < 0xD08C)
            return (addr & 1) != 0;
        if (addr >= 0xD00F && addr < 0xD07C) {
            const int off = (addr - 0xD000) % 31;
            return off >= 16 && (off & 1) == 0;
        }
        return false;
    }
    static std::string quote(const std::string &s) {
        std::string out = "\"";
        for (char c : s) {
            if (c == '\\' || c == '"')
                out += '\\';
            out += c == '\n' ? ' ' : c;
        }
        return out + '"';
    }
    void fail(const std::string &key, const std::string &detail) {
        ++counts_["failure_" + key];
        if (counts_["failure_" + key] <= 4)
            failures_.push_back(key + " frame " + std::to_string(w_.frame_count) + " action " +
                                std::to_string(w_.ram[0xDBFC]) + " source " + std::to_string(w_.ram[0xDBFE]) +
                                ": " + detail);
    }
    void flush() {
        const auto p = pending_;
        pending_ = {};
        if (!p[0])
            return;
        if (p[0] == 1) {
            const int actor = p[1], hp = word(actors + actor * 32 + 3);
            ++counts_["hp_writes"];
            if (actor < 4)
                ++counts_["party_hp_checks"];
            if (actor >= 4 && hp == 0 && p[2] > 0)
                ++counts_["knockout_type_" + std::to_string(w_.ram[actors + actor * 32 + 31])];
            if (flags_.no_damage && actor < 4 && w_.ram[actors + actor * 32 + 2] != 255 && hp < p[2])
                fail("god_hp", "actor " + std::to_string(actor) + " HP " + std::to_string(p[2]) + " -> " +
                                   std::to_string(hp));
            if (meter_pending_[actor]) {
                ++counts_["meter_commits"];
                if (hp != expected_hp_[actor])
                    fail("meter_commit", "actor " + std::to_string(actor) + " expected " +
                                             std::to_string(expected_hp_[actor]) + " got " +
                                             std::to_string(hp));
                meter_pending_[actor] = false;
            } else if (actor >= 4 && hp < p[2] && w_.ram[0xDBFE] < 4 &&
                       !(w_.ram[actors + actor * 32 + 2] & 3)) {
                if (flags_.one_hit_kills && hp != 0)
                    fail("one_hit_bypass",
                         "actor " + std::to_string(actor) + " survived at " + std::to_string(hp));
                if (flags_.damage != 1)
                    fail("damage_bypass", "actor " + std::to_string(actor) + " unfiltered HP loss " +
                                              std::to_string(p[2] - hp));
            }
            last_hp_[actor] = hp;
            hp_valid_[actor] = true;
        } else if (p[0] == 2) {
            const int status = w_.ram[actors + p[1] * 32 + 2];
            ++counts_["status_writes"];
            if (p[1] < 4)
                ++counts_["party_status_checks"];
            if (flags_.no_damage && p[1] < 4 && p[2] != 255 && (status & ~p[2]))
                fail("god_status", "actor " + std::to_string(p[1]) + " status " + std::to_string(p[2]) +
                                       " -> " + std::to_string(status));
        } else if (p[0] == 3 || p[0] == 5) {
            const int now = w_.ram[p[1]];
            if (w_.ram[p[1] - 1] != p[3] || p[2] == 0 || p[2] >= 254 || now == p[2])
                return;
            if (now != p[2] - 1 && now != 255)
                return;
            const bool enemy = p[0] == 3 && (p[1] - actors) / 32 >= 4;
            ++counts_[enemy ? "enemy_spends" : p[0] == 5 ? "field_spends" : "party_spends"];
            if (!enemy && flags_.unbreakable_gear)
                fail("gear_spent", "address " + std::to_string(p[1]) + " uses " + std::to_string(p[2]) +
                                       " -> " + std::to_string(now));
        } else if (p[0] == 6) {
            ++counts_["gold_commits"];
            if (money() != gold_expected_)
                fail("gold_commit",
                     "expected " + std::to_string(gold_expected_) + " got " + std::to_string(money()));
            gold_pending_ = false;
        }
    }
};
} // namespace sawgod::tools
