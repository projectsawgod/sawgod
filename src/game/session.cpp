// Owns native execution, resources, save services, and text hooks for one machine.
#include "game/session.h"

namespace sawgod::game {

Session::~Session() {
    host_fiber_.reset();
    // unwind any natives still suspended (e.g. at the end of a recording), innermost first
    while (!suspended_.empty())
        suspended_.pop_back();
}

Session &Session::of(emu::Wsc &w) {
    if (!w.game_session) {
        w.game_session = std::make_shared<Session>();
        w.on_scene_frame = [](emu::Wsc &machine) { Session::of(machine).apply_scene_settings(machine); };
    }
    return *static_cast<Session *>(w.game_session.get());
}

Session *Session::find(emu::Wsc &w) { return static_cast<Session *>(w.game_session.get()); }

void Session::apply_scene_settings(emu::Wsc &w) {
    if (w.frame_layers()) {
        if (!raster_field_) {
            raster_field_ = std::make_unique<emu::Wsc>(w.rom, 0);
            // Nonowning: this Session owns the presentation copy. Resource lookups must use
            // the live tables, while renderer reads use the retained memory and OAM.
            raster_field_->game_session = std::shared_ptr<void>(this, [](void *) {});
        }
        w.copy_presentation_to(*raster_field_);
        if (raster_world_words_hook)
            raster_field_words_ = raster_world_words_hook(w);
        else
            raster_field_words_.clear();
    }
    scene_requests_ = {};
    Machine m(w);
    hooks_.emit(hooks::Event::SceneFrame, m, {});
    const auto settings = clean_scene_settings(scene_requests_);
    if (w.scene_window_palettes != settings.windows || w.scene_window_tile_min != settings.window_tile_min)
        w.reset_scene_underlay();
    w.scene_hidden_palettes = settings.palettes;
    w.scene_hidden_sprite_prefix = settings.sprite_prefix;
    w.scene_hidden_sprite_min_y = settings.sprite_min_y;
    w.scene_hidden_sprite_palettes = settings.sprite_palettes;
    w.scene_keep_field_npcs = settings.keep_field_npcs;
    w.scene_window_palettes = settings.windows;
    w.scene_window_tile_min = settings.window_tile_min;
}

JoinedBattlePopup::JoinedBattlePopup(Machine &m, std::string_view routine)
    : machine_(m), original_text_(m.original_popup_text()),
      session_(routine.empty() ? nullptr : Session::find(m.wsc())), routine_(routine) {
    if (!routine.empty())
        machine_.set_original_popup_text(true);
    if (session_) {
        outer_ = session_->joined_popup_;
        session_->joined_popup_ = this;
    }
}

JoinedBattlePopup::~JoinedBattlePopup() {
    machine_.set_original_popup_text(original_text_);
    if (session_)
        session_->joined_popup_ = outer_;
}

const std::u32string *Session::joined_battle_popup_text() const {
    return joined_popup_ ? &joined_popup_->text_ : nullptr;
}

void Session::new_generation() {
    if (host_fiber_) {
        host_fiber_->cancel();
        host_fiber_.reset();
        ++abandoned_;
    }
    if (host_machine_)
        host_machine_->host_paused = false;
    host_machine_ = nullptr;
    raster_field_.reset();
    raster_field_words_.clear();
    party_load_pending = false;
    esper_pending_ = false;
    esper_declined = false;
    // Innermost first: each cancelled fiber unwinds only its own stack; the outer ones still hold theirs.
    while (!suspended_.empty()) {
        std::unique_ptr<Fiber> f = std::move(suspended_.back().fiber);
        suspended_.pop_back();
        f->cancel();
        ++abandoned_;
    }
    enemy_hp_boxes_.clear();
    ++generation_;
    player_names_.reset();
}

int Session::continue_fiber(std::unique_ptr<Fiber> f, int cycles) {
    if (!f->resume()) {
        if (esper_pending_)
            host_fiber_ = std::move(f);
        else
            suspended_.push_back({std::move(f), Machine::pending_callout()});
    }
    return cycles;
}

bool Session::allow_esper_change(Machine &m, int actor, int old_ability, int new_ability) {
    if (!confirm_esper_changes)
        return true;
    esper_change_ = {actor, old_ability, new_ability, true};
    esper_pending_ = true;
    host_machine_ = &m.wsc();
    esper_held_ = 0xFFFF; // release the key that dismissed the previous battle message first
    esper_hold_ = kEsperInputHold;
    m.wsc().host_paused = true;
    Fiber::yield();
    return esper_answer_;
}

void Session::esper_input(emu::Wsc &w, uint16_t keys) {
    if (!esper_pending_)
        return;
    if (esper_hold_ > 0) { // ignored: a key still down when the hold ends must be released and pressed again
        --esper_hold_;
        esper_held_ = keys;
        return;
    }
    const uint16_t pressed = keys & ~esper_held_;
    esper_held_ = keys;
    if (pressed & (emu::KEY_X1 | emu::KEY_X2 | emu::KEY_X3 | emu::KEY_X4))
        esper_change_.yes = !esper_change_.yes;
    if (!(pressed & (emu::KEY_A | emu::KEY_B)))
        return;
    esper_answer_ = !(pressed & emu::KEY_B) && esper_change_.yes;
    esper_pending_ = false;
    w.host_paused = false;
    continue_fiber(std::move(host_fiber_), 0);
}

std::unique_ptr<Fiber> Session::take_returned(uint16_t cs, uint16_t sp) {
    if (suspended_.empty() || cs != suspended_.back().at.cs || sp != suspended_.back().at.sp)
        return nullptr;
    std::unique_ptr<Fiber> f = std::move(suspended_.back().fiber);
    suspended_.pop_back();
    return f;
}

} // namespace sawgod::game
