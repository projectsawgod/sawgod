// Cheats (Options > Modifiers, all off by default): each is a hook subscription (hooks.h), so the
// decompiled code stays the original and, off, the game is exactly the WSC's.
//
// - God Mode: the party takes no damage and no negative effects in battle
// (BattleMeterChange: a party
//   actor's positive amount -> 0; HarmBattleActor guards direct HP/status/stat commits; InflictStatus
//   rejects normal status hits -- stone, death, poison, ...:
//   "no effect") and no damage in the field (FieldDamage: damage floors, scripted HP losses -> 0).
//   Heals still work.
// - One-Hit Kills: any damage to an enemy takes all its HP (BattleMeterChange: an enemy's positive amount ->
//   its current HP).
// - Unbreakable Gear: the party's weapons, items and abilities never spend a use, in battle or from the field
//   menu (SpendUse).
// - Unlimited Revives: reviving a member never costs a heart (SpendHeart); the
// gold the paid revive costs is
//   still paid.
// - Stat Boost (1.00-2.00): every stat gain -- a mutant's growth after a battle, a
// human's stat or HP item --
//   times the factor, rounded half up (StatGain); the game's caps (99, 999) still apply.
// Boosts (Options > Modifiers, before the cheats; the WSC's by default):
// - Encounters: normal, half (every second random encounter skipped), off (RandomEncounter).
// - Damage Boost: party damage to enemies times 1, 2 or 4, capped at 9999 (BattleMeterChange).
// - Gold: each enemy's gold reward times None, 0.5, 1, 2 or 4 (a percent; rounded half up; BattleGold; the
// game's
//   999999 caps stay).
// - Teleport: request_teleport(N) takes the party to tower floor N (up to the highest reached) the
//   next time it stands still in the field, as the Door does (FieldTeleport).
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "game/hooks.h"
#include "game/session.h"

namespace sawgod::game {

struct CheatFlags {
    bool no_damage = false, one_hit_kills = false, unbreakable_gear = false, unlimited_hearts = false;
    int stat_growth = 100; // percent
    int encounters = 0;    // 0 normal, 1 half, 2 off
    int gold = 100;        // percent
    bool skip_intro = false;
    int damage = 1; // multiplier for party damage dealt to enemies
    bool operator==(const CheatFlags &) const = default;
};

// The live subscriptions for a set of cheats; set() re-subscribes when the flags change.
class Cheats {
  public:
    void set(Session &s, const CheatFlags &flags) {
        if (flags == flags_ && subscribed_)
            return;
        flags_ = flags;
        subscribed_ = true;
        subs_.clear();
        auto &h = s.hooks();
        const bool modifiers = flags.no_damage || flags.one_hit_kills || flags.unbreakable_gear ||
                               flags.unlimited_hearts || flags.stat_growth != 100 || flags.encounters != 0 ||
                               flags.gold != 100 || flags.damage != 1;
        subs_.push_back(
            h.subscribe(hooks::Value::FieldTeleport, [this](Machine &, int32_t, const hooks::Args &) {
                const int floor = teleport_;
                teleport_ = 0;
                return int32_t(floor);
            }));
        subs_.push_back(
            h.subscribe(hooks::Value::PartyMenuKeys, [this](Machine &, int32_t keys, const hooks::Args &) {
                return teleport_ > 0 ? int32_t(emu::KEY_B) : keys;
            }));
        if (modifiers)
            subs_.push_back(
                h.subscribe(hooks::Value::BattleMeterChange, [f = flags](Machine &, int32_t amount,
                                                                         const hooks::Args &a) {
                    const int16_t damage = int16_t(uint16_t(amount)); // HP becomes HP - amount
                    if (damage <= 0)
                        return amount; // a heal (or nothing)
                    if (a.a < 4)
                        return f.no_damage ? int32_t(0) : amount;
                    if (f.one_hit_kills)
                        return std::max<int32_t>(amount, a.b);
                    return a.c < 4 && f.damage != 1 ? std::min<int32_t>(damage * f.damage, 9999) : amount;
                }));
        if (flags.no_damage) {
            subs_.push_back(
                h.subscribe(hooks::VB::HarmBattleActor, [](Machine &, bool allow, const hooks::Args &a) {
                    return a.a < 4 ? false : allow;
                }));
            subs_.push_back(
                h.subscribe(hooks::VB::InflictStatus, [](Machine &, bool lands, const hooks::Args &a) {
                    return a.a < 4 ? false : lands; // the party: no effect
                }));
            subs_.push_back(h.subscribe(hooks::Value::FieldDamage,
                                        [](Machine &, int32_t, const hooks::Args &) { return int32_t(0); }));
        }
        if (flags.unbreakable_gear)
            subs_.push_back(h.subscribe(hooks::VB::SpendUse, [](Machine &, bool spend, const hooks::Args &a) {
                return a.a == 0 && a.b >= 4 ? spend : false; // the party's (enemies spend theirs elsewhere)
            }));
        if (flags.stat_growth != 100)
            subs_.push_back(
                h.subscribe(hooks::Value::StatGain,
                            [pct = flags.stat_growth](Machine &, int32_t gain, const hooks::Args &) {
                                return gain <= 0 ? gain : (gain * pct + 50) / 100;
                            }));
        if (flags.encounters == 2)
            subs_.push_back(h.subscribe(hooks::VB::RandomEncounter,
                                        [](Machine &, bool, const hooks::Args &) { return false; }));
        else if (flags.encounters == 1)
            subs_.push_back(
                h.subscribe(hooks::VB::RandomEncounter, [this](Machine &, bool start, const hooks::Args &) {
                    skip_next_ = !skip_next_; // every second one
                    return start && !skip_next_;
                }));
        if (flags.gold != 100)
            subs_.push_back(h.subscribe(hooks::Value::BattleGold,
                                        [g = flags.gold](Machine &, int32_t gold, const hooks::Args &) {
                                            return int32_t((int64_t(gold) * g + 50) / 100);
                                        }));
        if (flags.skip_intro)
            subs_.push_back(h.subscribe(hooks::VB::PlayIntro,
                                        [](Machine &, bool, const hooks::Args &) { return false; }));
        if (flags.unlimited_hearts)
            subs_.push_back(h.subscribe(hooks::VB::SpendHeart,
                                        [](Machine &, bool, const hooks::Args &) { return false; }));
    }
    // Teleport to Floor: floor N the next time the party stands still in the field (0 cancels).
    void request_teleport(int floor) { teleport_ = floor; }
    void clear() {
        subs_.clear();
        subscribed_ = false;
    }

  private:
    CheatFlags flags_;
    bool subscribed_ = false;
    int teleport_ = 0;
    bool skip_next_ = false; // Encounters: half
    std::vector<hooks::Subscription> subs_;
};

} // namespace sawgod::game
