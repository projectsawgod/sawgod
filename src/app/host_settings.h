// How the app turns the Options settings into what the game and the frame loop use: the game's own Config
// bytes (game/game_config.h), the Cheats (game/cheats.h), the game frames run per scheduled tick and the
// floors Teleport offers, the fidelity buttons and levels into the picture and music mix, and the span Battle
// Speed covers. Shared by main.cpp and the host settings and fidelity UI tests, so the
// tests observe the app's own mapping instead of a copy of it.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "app/sound_mix.h"
#include "emu/wsc.h"
#include "engine/fidelity.h"
#include "engine/settings.h"
#include "game/cheats.h"
#include "game/game_config.h"
#include "game/session.h"

namespace sawgod::app {

inline void apply_esper_confirmation(emu::Wsc &w, const Settings &s) {
    game::Session::of(w).confirm_esper_changes = s.confirm_esper_changes->on();
}

// Options > Gameplay: the values written into the game's Config bytes before every frame.
inline game::GameConfigValues game_config_values(const Settings &s) {
    game::GameConfigValues v;
    v.b_runs = s.b_button->get() == 1;
    v.auto_target = s.auto_target->on();
    v.meat_preview = s.meat_preview->on();
    v.message_speed = s.message_speed->get();
    for (int i = 0; i < 4; ++i)
        v.y[size_t(i)] = Settings::kYMenuValues[s.y_button[i]->get()];
    return v;
}

// Options > Cheats (and Gameplay > Skip Intro): the hook subscriptions' flags.
inline game::CheatFlags cheat_flags(const Settings &s) {
    return {s.no_damage->on(),        s.one_hit_kills->on(), s.unbreakable_gear->on(),
            s.unlimited_hearts->on(), s.stat_growth->get(),  s.encounters->get(),
            s.gold_percent(),         s.skip_intro->on(),    s.damage_factor()};
}

// Game frames per scheduled 75.47 Hz tick: Fast-Forward held wins over Battle Speed; the open menu pauses
// both. A tick that starts `late` (the loop is over two ticks behind) runs only Battle Speed's sound frame:
// its extra frames give way instead of the clock reset that would drop sound.
struct FrameSpeed {
    int speed = 1;
    bool fast_forwarding = false;
    int steps(bool late = false) const { return late && !fast_forwarding ? 1 : speed; }
    bool audio_tick(int step, bool late = false) const { return fast_forwarding || step == steps(late) - 1; }
    int audio_speed() const { return fast_forwarding ? speed : 1; }
};
inline FrameSpeed frame_speed(const Settings &s, bool menu_open, bool fast_forward_held, bool in_battle) {
    if (menu_open)
        return {};
    if (fast_forward_held)
        return {s.fast_forward_speed(), true};
    return {in_battle ? s.battle_speed_factor() : 1, false};
}

// Run one game frame, with one audio frame only on the scheduled tick. A host decision holds the
// partial game frame, but keeps the same music clock as Options (including Battle Speed's extra steps).
inline void run_game_frame(emu::Wsc &w, bool audio_tick = true) {
    if (w.host_paused) {
        if (audio_tick)
            paused_tick(w);
        return;
    }
    const bool previous = w.audio_clock_enabled;
    w.audio_clock_enabled = audio_tick;
    w.run_frame();
    w.audio_clock_enabled = previous;
}

// Cheats > Teleport: the highest floor it offers (the save's 0xD0DA, at most 23), or 0 before a game is
// loaded (no session, or no map yet).
inline int teleport_max(emu::Wsc *wsc) {
    if (!wsc || !game::Session::find(*wsc) || wsc->ram[0xD0DC] == 0)
        return 0;
    return std::min<int>(wsc->ram[0xD0DA], 23);
}

// Mode Down/Up (the fidelity level): each press (not a hold) steps the Swap tracks; both pressed together
// cancel out. Ignored while Options is open; a button held across the menu's closing does not step.
struct FidelityButtons {
    bool down_held = false, up_held = false;
    int press(bool options_open, bool down, bool up) {
        const int direction = options_open ? 0 : int(up && !up_held) - int(down && !down_held);
        down_held = down;
        up_held = up;
        return direction;
    }
};

// The graphics weights (GB, WSC, Enhanced) into the picture: the GB picture's share of 256 over the rest, and
// Enhanced's share of what is not GB (the shader mixes WSC/Enhanced first, then GB).
struct PictureMix {
    uint32_t gb = 0;
    float enhanced = 0;
};
inline PictureMix picture_mix(const Fidelity::Weights &w) {
    return {uint32_t(std::clamp(w[0] * 256, 0.0f, 256.0f)), w[0] < 1 ? w[2] / (1 - w[0]) : 0};
}

// Blends the GB picture `b` into `a`: gb of 256 parts, per color channel.
inline void blend_gb_picture(std::vector<uint32_t> &a, const std::vector<uint32_t> &b, uint32_t gb) {
    for (size_t i = 0; i < a.size(); ++i) {
        uint32_t out = 0;
        for (int sh = 0; sh < 24; sh += 8)
            out |= (((a[i] >> sh & 255) * (256 - gb) + (b[i] >> sh & 255) * gb) >> 8) << sh;
        a[i] = out;
    }
}

// The music weights into the three drivers: the GB music (when loaded), the Enhanced arrangement (when it
// plays), and the WSC music's share of the Music volume (what the other two leave).
struct MusicMix {
    float gb = 0, enhanced = 0, wsc = 1;
};
inline MusicMix music_mix(const Fidelity::Weights &w, bool gb_loaded, bool gb_playing,
                          bool enhanced_playing) {
    MusicMix m;
    m.gb = gb_loaded ? w[0] : 0.0f;
    m.enhanced = enhanced_playing ? w[2] : 0.0f;
    m.wsc = 1 - (gb_playing ? m.gb : 0) - m.enhanced;
    return m;
}

// Screen Mode as shown: the setting, whatever the graphics level (Screen Mode and Graphics are
// independent; any graphics level with any screen mode), except the Enhanced title screen, which shows none.
inline ScreenMode effective_screen(FidelityMode graphics, ScreenMode setting, bool title_screen = false) {
    return title_screen && graphics == FidelityMode::Enhanced ? ScreenMode::Off : setting;
}

// Battle Speed's span: the game's BattleStart .. BattleEnd. A wipe-out restarts the game instead (a new
// Session generation), which ends it too.
class BattleSpan {
  public:
    void install(emu::Wsc &w) {
        auto &hooks = game::Session::of(w).hooks();
        start_ = hooks.subscribe(game::hooks::Event::BattleStart,
                                 [this](game::Machine &m, const game::hooks::Args &) {
                                     in_battle_ = true;
                                     generation_ = game::Session::of(m.wsc()).generation();
                                 });
        end_ = hooks.subscribe(game::hooks::Event::BattleEnd,
                               [this](game::Machine &, const game::hooks::Args &) { in_battle_ = false; });
    }
    // In a battle now (checks the generation first).
    bool active(emu::Wsc &w) {
        if (in_battle_ && game::Session::find(w) && game::Session::of(w).generation() != generation_)
            in_battle_ = false; // the game restarted (a wipe-out) mid-battle
        return in_battle_;
    }

  private:
    bool in_battle_ = false;
    uint64_t generation_ = 0;
    game::hooks::Subscription start_, end_;
};

} // namespace sawgod::app
