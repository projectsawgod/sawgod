// SaGa ownership and actor semantics for presentation; never writes machine state.
#pragma once
#include "emu/wsc.h"
#include "game/hooks.h"
#include "game/landmark_tags.h"
#include "game/light_tags.h"

namespace sawgod::game {
// Loaded field resource identity; SceneObserver separately confirms what the raster actually shows.
bool recognized_field(emu::Wsc &w);
// Live native handler state, also used to release pending host-picture handoffs.
bool battle_entry_wipe(const emu::Wsc &w);
// Whether the wipe owns the displayed raster, before BattleStart is emitted.
// Uses the session's latched raster inputs, falling back to live state when no latch exists.
bool presented_battle_entry_wipe(const emu::Wsc &w);
struct PresentationScene {
    enum class Kind { Unknown, Field, Battle, Transition } kind = Kind::Unknown;
    // Transition publishes the native raster while retaining the last confirmed camera family.
    Kind previous = Kind::Unknown;
    enum class Transition { None, RasterUnavailable, Uniform, TerrainAbsent, BattleWipe, ScriptedOverlay };
    Transition transition = Transition::None;
    bool camera_field() const {
        return kind == Kind::Field || (kind == Kind::Transition && previous == Kind::Field);
    }
    void hold_uniform_picture() {
        if (kind != Kind::Transition)
            previous = kind;
        kind = Kind::Transition;
        transition = Transition::Uniform;
        field_cleared = true;
        perspective_allowed = camera_field();
    }
    // R: UI; G: 64 = foreground shadow occlusion, 128 = exact source overlay,
    // 254 = exact sprite overlay, 255 = visible protected enemy.
    std::array<uint32_t, emu::FrameLayers::Pixels> mask{};
    std::vector<uint32_t> background;         // battle picture with enemy layer removed, Raw colors
    std::vector<std::array<float, 4>> actors; // foot x/y, ellipse radii
    std::array<std::array<float, 4>, 3> battle_groups{}; // center x/y, picture bottom, visible
    std::vector<PointLight> lights;
    std::vector<uint32_t> water;
    std::array<float, 2> palette_range{0, 1}; // native fade's black/white endpoints
    bool field_cleared = false; // native transition raster owns the picture; no reconstruction or effects
    bool perspective_allowed = false; // confirmed field camera, including windows and transition holds
    int map = -1;
};
// Per-family switches for the enhanced battle animations (fire is always on). Struct defaults are off;
// environment() turns each on unless its SAWGOD_BATTLE_* variable is "0".
struct BattleAnimationOptions {
    bool ice = false, thunder = false, poison = false, sleep = false, death = false, cast_glow = false;
    static BattleAnimationOptions environment();
};
class SceneObserver {
  public:
    void install(emu::Wsc &w);
    void battle_animations(BattleAnimationOptions options) {
        battle_options_ = options;
        level_ = 0;
        level_frame_ = ~0ull;
    }
    LandmarkTags &landmark_tags() { return landmark_tags_; }
    LightTags &light_tags() { return light_tags_; }
    PresentationScene describe(emu::Wsc &w, bool battle, bool full_ui, bool scene) const;
    // Gameplay's field producer has published this session; raster confirmation is separate.
    bool field_published(emu::Wsc &w) const;

  private:
    BattleAnimationOptions battle_options_;
    bool animation_enabled(int rom, int palette) const;
    LightTags light_tags_;
    LandmarkTags landmark_tags_;
    std::vector<hooks::Subscription> subscriptions_;
    mutable uint64_t generation_ = 0;
    bool battle_ = false;
    mutable PresentationScene::Kind confirmed_ = PresentationScene::Kind::Unknown;
    mutable uint64_t picture_generation_ = ~0ull;
    mutable int confirmed_map_ = -1;
    mutable bool entering_battle_ = false;
    bool overlay_ = false; // a scripted full-width field overlay is running
    uint64_t overlay_generation_ = 0;
    mutable int field_map_ = -1; // last field picture, including destinations drawn inside a script
    int animation_ = 0;          // the native battle animation now playing (its bank-37 ROM entry), 0 = none
    int animation_palette_ = 0;  // DC12 at the animation-start hook
    uint64_t animation_start_frame_ = 0; // first raster after the hook can still contain the preceding effect
    std::vector<PointLight> animation_lights(const emu::FrameLayers &layers, uint64_t frame) const;
    mutable float level_ = 0; // smoothed brightness of the current animation's light
    mutable uint64_t level_frame_ = ~0ull;
};
} // namespace sawgod::game
