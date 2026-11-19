#include "game/battle_rewards.h"
#include "game/bank34_misc.h"
#include "game/bank36_misc.h"
#include "game/bank3d_sound.h"
#include "game/hooks.h"
#include "game/tables.h"

#include <algorithm>

namespace sawgod::game {
void show_gold_reward_message_core(Machine &m);

namespace {
constexpr uint16_t kGold = 0xD0A2;
constexpr uint16_t kGoldHigh = 0xD0A4;
constexpr uint16_t kBattleReward = 0xDD6B;
constexpr uint16_t kBattleRewardTotal = 0xDD6F;
constexpr uint16_t kRewardDigits = 0xDC8B;
constexpr uint16_t kBattleRewardText = 0xDC9B; // battle_reward_text
constexpr uint16_t kKeysRepeat = 0xC806;

void set_cl(Machine &m, uint8_t v) { m.cx() = uint16_t((m.cx() & 0xFF00) | v); }

constexpr uint32_t kGoldLimit = 999999;
constexpr uint8_t kFirstEnemy = 4;
constexpr uint8_t kEnemyEnd = 0x1F;
constexpr uint8_t kEmptyEnemySlot = 0xFF;

struct GoldAddition {
    uint32_t sum; // Wrapped, uncapped sum returned to original callers.
    uint32_t stored;
};

GoldAddition add_gold_reward(uint32_t gold, uint32_t reward) {
    const uint32_t sum = gold + reward;
    return {sum, std::min(sum, kGoldLimit)};
}

uint16_t enemy_gold_reward(int32_t filtered_gold) {
    return uint16_t(std::clamp<int32_t>(filtered_gold, 0, 0xFFFF));
}

// Machine-backed state remains authoritative; free accessors keep segment-audit discovery intact.
namespace reward_state {
uint32_t party_money(Machine &m) {
    const uint32_t high = m.rd16(m.ss(), kGoldHigh);
    return (high << 16) | m.rd16(m.ss(), kGold);
}

uint32_t pending_gold(Machine &m) {
    const uint32_t high = m.rd16(m.ss(), kBattleReward + 2);
    return (high << 16) | m.rd16(m.ss(), kBattleReward);
}

uint32_t reward_total(Machine &m) {
    const uint32_t high = m.rd16(m.ss(), kBattleRewardTotal + 2);
    return (high << 16) | m.rd16(m.ss(), kBattleRewardTotal);
}

void store_party_money(Machine &m, uint32_t gold) {
    m.wr16(m.ss(), kGoldHigh, uint16_t(gold >> 16));
    m.wr16(m.ss(), kGold, uint16_t(gold));
}

void store_reward_total(Machine &m, uint32_t gold) {
    m.wr16(m.ss(), kBattleRewardTotal + 2, uint16_t(gold >> 16));
    m.wr16(m.ss(), kBattleRewardTotal, uint16_t(gold));
}

void clear_gold_rewards(Machine &m) {
    m.wr16(m.ss(), kBattleReward, 0);
    m.wr16(m.ss(), kBattleReward + 2, 0);
    m.wr16(m.ss(), kBattleRewardTotal, 0);
    m.wr16(m.ss(), kBattleRewardTotal + 2, 0);
}

void store_enemy_gold(Machine &m, uint16_t gold) { m.wr16(m.ss(), kBattleReward, gold); }
} // namespace reward_state

// Load this enemy's monster gold while keeping the original pointer/register results.
bool load_enemy_gold_core(Machine &m) {
    m.set_al(uint8_t(m.cx()));
    battle_actor_pointer_core(m);
    m.set_al(m.rd8(m.ss(), uint16_t(m.si() + 2)));
    if (m.al() == kEmptyEnemySlot)
        return false;
    m.set_al(m.rd8(m.ss(), uint16_t(m.si() + 0x1F)));
    battle_table_pointer_core(m);
    m.ax() = table_at16(m, Table::monsters, m.es(), uint16_t(m.di() + 9));
    return true;
}
} // namespace

// rom: 0x3638F5 (near)
// Add the 32-bit reward to party gold, capped at 999999.
void add_battle_reward_to_money_core(Machine &m) {
    const uint32_t gold = reward_state::party_money(m);
    const uint32_t reward = reward_state::pending_gold(m);
    const GoldAddition result = add_gold_reward(gold, reward);
    m.dx() = uint16_t(result.sum >> 16);
    m.ax() = uint16_t(result.sum);
    reward_state::store_party_money(m, result.stored);
}

void add_battle_reward_to_money(Machine &m) {
    add_battle_reward_to_money_core(m);
    m.ret();
}

// rom: 0x36392C (near)
// Accumulate the 32-bit reward total, capped at 999999.
void add_battle_reward_to_total_core(Machine &m) {
    const uint32_t gold = reward_state::reward_total(m);
    const uint32_t reward = reward_state::pending_gold(m);
    const GoldAddition result = add_gold_reward(gold, reward);
    m.dx() = uint16_t(result.sum >> 16);
    m.ax() = uint16_t(result.sum);
    reward_state::store_reward_total(m, result.stored);
}

void add_battle_reward_to_total(Machine &m) {
    add_battle_reward_to_total_core(m);
    m.ret();
}

// rom: 0x3638A6 (near)
// Formats the 32-bit total at 0xDD6F as a tile-word string at 0xDC9B: ten decimal digits into 0xDC8B
// (number32_to_digits), leading zeros skipped (at least one digit; with all ten zero, the byte after the
// digits is used as the last one -- an original quirk), each digit d as tile word (d + BBh) * 2, then FFFFh.
// AX = FFFFh, CX = DX = 0; DS/ES/SI/DI preserved.
void format_battle_reward_total_core(Machine &m) {
    const uint16_t ds = m.ds(), es = m.es(), si = m.si(), di = m.di();
    m.ds() = 0;
    m.ax() = m.rd16(m.ss(), kBattleRewardTotal);
    m.dx() = m.rd16(m.ss(), (kBattleRewardTotal + 0x2));
    m.si() = kRewardDigits;
    number32_to_digits_core(m);
    m.si() = kRewardDigits;
    m.di() = kBattleRewardText;
    m.cx() = 0xA;
    do {
        m.set_al(m.rd8(m.ss(), m.si()));
        if (m.al() != 0)
            break;
        m.si() = uint16_t(m.si() + 1);
    } while (--m.cx() != 0);
    m.cx() = uint16_t(m.cx() + 1);
    for (;;) {
        m.ax() = uint16_t(uint8_t(m.rd8(m.ss(), m.si()) + 0xBB) << 1);
        m.wr16(m.ss(), m.di(), m.ax());
        if (--m.cx() == 0)
            break;
        m.di() = uint16_t(m.di() + 2);
        m.si() = uint16_t(m.si() + 1);
    }
    m.ax() = 0xFFFF;
    m.wr16(m.ss(), m.di(), m.ax());
    m.ds() = ds;
    m.es() = es;
    m.si() = si;
    m.di() = di;
}

void format_battle_reward_total(Machine &m) {
    format_battle_reward_total_core(m);
    m.ret();
}

// rom: 0x363963 (near)
// Sum each present enemy's gold reward, credit party gold, and display the capped total.
void award_battle_gold_core(Machine &m) {
    m.ax() = 0;
    reward_state::clear_gold_rewards(m);
    set_cl(m, kFirstEnemy);
    m.dx() = 0;
    for (; uint8_t(m.cx()) != kEnemyEnd; set_cl(m, uint8_t(uint8_t(m.cx()) + 1))) {
        if (!load_enemy_gold_core(m))
            continue;
        m.ax() = enemy_gold_reward(hooks::filter(hooks::Value::BattleGold, m, m.ax(), {uint8_t(m.cx())}));
        reward_state::store_enemy_gold(m, m.ax());
        add_battle_reward_to_total_core(m);
        add_battle_reward_to_money_core(m);
    }
    format_battle_reward_total_core(m);
    m.ax() = m.rd16(m.ss(), kBattleRewardTotal);
    if (m.ax() != 0) {
        show_gold_reward_message_core(m);
        do {
            m.call_far(0x4000, 0x0009); // wait_vblank
            m.call_far(0x4000, 0x005C); // read_keypad: no core
            m.ax() = uint16_t(m.rd16(m.ss(), kKeysRepeat) & 4);
        } while (m.ax() == 0);
    }
    m.ax() = 0x93;
    play_sound_effect_core(m, 0x93);
}

void award_battle_gold(Machine &m) {
    award_battle_gold_core(m);
    m.ret();
}

} // namespace sawgod::game
