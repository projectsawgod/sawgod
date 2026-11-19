// Battle effect commits, including handlers that bypass the animated damage/status paths.
#pragma once

#include "game/hooks.h"
#include "game/machine.h"

namespace sawgod::game {

class BattleActorState {
  public:
    BattleActorState(Machine &m, uint16_t record) : m_(m), record_(record) {}

    void write_hp(uint16_t hp) {
        if (hp < m_.rd16(m_.ss(), uint16_t(record_ + kHp)) && !may_harm(kHp, hp))
            return;
        m_.wr16(m_.ss(), uint16_t(record_ + kHp), hp);
    }

    // Cures remain possible even if a combined replacement also tries to add a negative status.
    bool can_write_status(uint8_t status) {
        return !(status & ~m_.rd8(m_.ss(), uint16_t(record_ + kStatus))) || may_harm(kStatus, status);
    }

    bool write_status(uint8_t status) {
        const uint8_t previous = m_.rd8(m_.ss(), uint16_t(record_ + kStatus));
        const bool allowed = can_write_status(status);
        m_.wr8(m_.ss(), uint16_t(record_ + kStatus), allowed ? status : uint8_t(status & previous));
        return allowed;
    }

    bool write_stat(uint16_t field, uint8_t value) {
        if (value < m_.rd8(m_.ss(), uint16_t(record_ + field)) && !may_harm(field, value))
            return false;
        m_.wr8(m_.ss(), uint16_t(record_ + field), value);
        return true;
    }

  private:
    static constexpr uint16_t kActors = 0xD71A, kStride = 32, kStatus = 2, kHp = 3;
    bool may_harm(int field, int value) {
        return hooks::should(hooks::VB::HarmBattleActor, m_, true,
                             {(record_ - kActors) / kStride, field, value});
    }
    Machine &m_;
    uint16_t record_;
};

} // namespace sawgod::game
