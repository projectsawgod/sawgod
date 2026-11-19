// Autosave (whenever you change fields, into its own slot AUTO at the top
// of the save and load lists; Options > Gameplay > Autosave). The first time the party stands still in the
// field on a map other than the last one autosaved (hooks::Event::FieldIdle), the game state block -- what
// the game's own save writes -- goes into SaveSlots::kAuto (save_slots.h; the store writes auto.toml). The
// game's memory is untouched and the last-used slot isn't changed.
#pragma once

#include <cstdint>
#include <vector>

#include "game/hooks.h"
#include "game/machine.h"
#include "game/save_slots.h"
#include "game/session.h"

namespace sawgod::game {

class Autosave {
  public:
    void set(Session &s, bool on) {
        s.save_slots().request_auto(on);
        if (on == on_ && (sub_.active() || !on))
            return;
        on_ = on;
        sub_.reset();
        last_map_ = -1;
        if (on)
            sub_ = s.hooks().subscribe(hooks::Event::FieldIdle, [this](Machine &m, const hooks::Args &) {
                constexpr uint16_t kMapId = 0xD0DC, kGameState = 0xD000;
                const int map = m.rd8(m.ss(), kMapId);
                if (map == last_map_)
                    return;
                last_map_ = map;
                Session *session = Session::find(m.wsc());
                if (!session || !session->save_slots().store_attached())
                    return;
                std::vector<uint8_t> payload(SaveSlots::kPayload);
                for (size_t i = 0; i < payload.size(); ++i)
                    payload[i] = m.rd8(m.ss(), uint16_t(kGameState + i));
                session->save_slots().store(SaveSlots::kAuto, payload, session->player_names().values);
            });
    }

  private:
    bool on_ = false;
    int last_map_ = -1;
    hooks::Subscription sub_;
};

} // namespace sawgod::game
