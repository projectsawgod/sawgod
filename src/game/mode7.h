// Presentation-only world camera. No machine reads go through the emulated bus.
#pragma once
#include "engine/fidelity.h"
#include "game/presentation_scene.h"
#include <algorithm>
#include <map>
#include <memory>

namespace sawgod::game {
// The camera: fixed values, not settings. World tilt and lens width in
// degrees, the world camera's pull-back, and the flat field zoom.
struct Mode7Look {
    float tilt = 16, lens = 24, distance = 1.5f, field_zoom = 1.5f;
};
struct Mode7Camera {
    float tilt = 30, lens = 24;
    float amount = 1;   // shared Mode7Fade amount: ground art unfolds into standing geometry
    float distance = 1; // camera distance multiplier, animated with amount
    // Flat field camera: one WSC pixel occupies pixel_scale square output pixels: a whole number once
    // settled, fractional only inside a fidelity fade. Origin is relative to the game's scroll; zero
    // pixel_scale selects perspective.
    float pixel_scale = 0;
    std::array<float, 2> output_scale{1, 1}, origin{};
    std::array<float, 2> ground_at_height(float x, float y, float height) const;
    std::array<float, 3> elevated(float x, float y, float height) const;
    // Flat screen coordinates in/out; the player's ground point stays at (112,79).
    std::array<float, 2> ground(float x, float y) const;
    std::array<float, 3> project(float x, float y) const; // x, y, billboard scale
};
class Mode7Fade {
  public:
    Mode7Fade() = default;
    explicit Mode7Fade(uint64_t duration) : duration_(duration) {}
    float update(bool perspective, uint64_t now);
    float amount(uint64_t now) const;
    void settle(bool perspective); // jump to an endpoint; the caller must hide the cut

  private:
    uint64_t duration_ = Fidelity::kFadeNs;
    bool target_ = false;
    float from_ = 0;
    uint64_t start_ = 0;
};
// A new scene crosses neutral over 100 ms in each direction. Native-only transitions lower
// the envelope while the camera family holds; UI windows never change the scene key.
class SceneEffectFade {
  public:
    float update(const PresentationScene &, uint64_t now);
    bool fading() const { return fading_; }
    // The next scene starts at full strength: a new session or an independent still has no
    // visible predecessor to fade from (as its camera settles).
    void settle_next() {
        *this = {};
        settle_ = true;
    }

  private:
    Mode7Fade fade_{100'000'000};
    bool settle_ = false;
    PresentationScene::Kind kind_ = PresentationScene::Kind::Unknown;
    int map_ = -1;
    uint64_t rise_ = 0;
    bool fading_ = false;
};
// Explicit scene eligibility: field loop, supported world, Enhanced target, no screen-space layout.
bool mode7_allowed(int map, FidelityMode level, bool field, bool layout);
bool field_zoom_allowed(int map, FidelityMode level, bool field, bool layout);
// amount 1: the whole output pixels nearest `zoom`. Below 1 (a fidelity fade): one continuous zoom about
// the party anchor, from the ordinary picture's own scale and framing (amount 0) to that settled view.
Mode7Camera field_zoom_camera(float zoom, float amount, int width, int height, std::array<float, 2> scroll,
                              bool continuous = false);
// The travel crystal's door vignette: its scene command (field event kind 2, argument
// 43-47: ring, beam and door, with the white flash and fades around them) while its script runs. The
// native picture has no field terrain there; Enhanced draws it at the zoomed field's scale and framing.
bool crystal_door_vignette(const emu::Wsc &w);
// A hidden field arrival may start at its settled framing. Dialogue, small windows and Options keep
// the same field camera. Visible scene entries and fidelity changes animate from the current amount.
class FieldZoomEntry {
  public:
    enum class Step {
        Fade,
        Settle,
        Drop
    }; // animate with Mode7Fade / jump to zoomed / jump to the ordinary view
    // field: the frame shows `map`'s field (field_zoom_allowed at Enhanced). hidden: a full-screen layout
    // covers it. source_map / flat_source / amount: the camera's loaded map, whether it is a flat field,
    // and its present amount. hidden_entry permits endpoint cuts only while the picture hides them.
    Step step(int map, bool field, bool enhanced, bool hidden, int source_map, bool flat_source, float amount,
              bool hidden_entry = true);

  private:
    int shown_ = -1; // the map whose field the previous frame showed
};
// Hidden map entries may settle immediately; visible entries and fidelity changes fade.
class WorldTiltEntry {
  public:
    FieldZoomEntry::Step step(int map, bool field, bool enhanced, bool hidden_entry = true);

  private:
    int shown_ = -1;
};
// Actual UI ownership, also available when scene effects deliberately fall back.
// `host_only`: only the host's own windows count as UI (the native windows they replace do not), for the
// service menus over the zoomed field. `world_sprites`: the caller draws Mode7World's sprites, which hold
// every field sprite, so none of them is UI (false keeps the sprites no field record owns as native pixels
// at their screen place, for pictures drawn without that world).
std::array<uint32_t, emu::FrameLayers::Pixels>
mode7_ui_mask(const emu::Wsc &w, bool battle, bool scene, bool host_only = false, bool world_sprites = false);
// A latched sprite entry no field record owns belongs to the field (not to the screen's UI) unless it uses
// a UI palette. Sprite palette 15 is also a field object's when its tile is in the NPC buffers.
bool field_script_sprite(int word);
// Caller-owned: no global state or lifetime assumptions about the machine or its frame-layer allocation.
// The app's mask: Mode7World draws the field's sprites, script-only ones included. `world_sprites` false
// on a tick the world was not updated (held, cleared or not a field): its sprites are not this picture's,
// so a sprite no field record owns stays native pixels instead of vanishing.
class Mode7UiMask {
  public:
    const std::array<uint32_t, emu::FrameLayers::Pixels> &
    get(const emu::Wsc &w, bool battle, bool scene, bool host_only = false, bool world_sprites = true);
    uint64_t rebuilds = 0;

  private:
    std::array<uint32_t, emu::FrameLayers::Pixels> mask_{};
};
struct Mode7Picture {
    std::vector<uint32_t> pixels;
    PresentationScene scene;
};
// GPU tile-map inputs. Animation changes indices/palettes, never repaints world cells.
struct Mode7Surface {
    uint64_t revision = 0, generation = 0, tile_revision = 0, palette_revision = 0;
    // 160x160 quadrant words: R=low byte, G=high byte, B=foreground coverage.
    std::vector<uint32_t> words;
    // Packed 32 KiB tile RAM (128x64 RGBA), plus reviewed water kind per source texel.
    std::vector<uint32_t> indices, water;
    std::array<uint32_t, 256> palettes{};
    std::array<uint64_t, 6400> changed{};
    std::vector<int> dirty;
};
struct Mode7Sprite {
    uint64_t id = 0;     // art cache key
    uint32_t record = 0; // stable motion identity: native record address, never OAM slot/art hash
    float x = 0, y = 0;  // world-space foot
    int width = 0, height = 0, bottom = 0, extra = 0, shaft_tip = 0;
    bool landmark = false, front = true;
    bool veil = false; // see-through terrain: pixels with half alpha are blended (game/terrain_veil.h)
    // A script-only sprite (scene effects, bosses: native sprites no field record owns), kept at its place
    // on the field: 1 is drawn over every actor, -1 (after every actor in the native sprite table) under
    // them. Among themselves they keep the table's order: the lower first entry (`order`) is in front.
    int layer = 0, order = 0;
    std::shared_ptr<const std::vector<LandmarkCell>> footprint;
    std::shared_ptr<const std::vector<uint32_t>> pixels; // cached ARGB cutout
    // Per pixel of a native sprite's cutout: its sprite palette (0-7) << 4 | color index, 0 where clear. A
    // held world takes the live sprite colors from it (Mode7World::refresh_palettes).
    std::shared_ptr<const std::vector<uint8_t>> paint;
    // Offset from the unchanged card pivot to just above its lowest drawn row.
    float shadow_offset() const {
        if (pixels)
            for (int y = height - 1; y >= 0; --y)
                for (int x = 0; x < width; ++x)
                    if ((*pixels)[size_t(y * width + x)] >> 24)
                        return float(y - bottom) - 0.5f;
        return -0.5f;
    }
};
// Diagnostic-only identity resolve of cached GPU inputs (no effects). Source zero is terrain,
// one is a landmark, and other values are the native object record that painted the pixel.
struct Mode7NeutralPicture {
    std::vector<uint32_t> pixels, sources;
};
Mode7NeutralPicture mode7_neutral_picture(const Mode7Surface &surface,
                                          const std::vector<Mode7Sprite> &sprites,
                                          std::array<float, 2> scroll);
// Earlier native OAM wins an overlap. The party is emitted before NPC slots; equal feet
// must therefore paint higher-address records first, independently of collection order.
inline bool mode7_depth_less(const Mode7Sprite &a, const Mode7Sprite &b) {
    if (a.layer != b.layer)
        return a.layer < b.layer;
    if (a.layer && a.order != b.order)
        return a.order > b.order;
    if (a.y != b.y)
        return a.y < b.y;
    if (a.landmark != b.landmark)
        return a.landmark;
    return a.record > b.record;
}
// Convert the displayed SCR1 scroll back to world coordinates. Native RAM can have
// prepared the next pixel step before upload_field_display commits its shadow registers.
std::array<float, 2> field_raster_scroll(const emu::Wsc &);
// A half pixel belongs to the displayed OAM only while its whole coordinate still agrees.
inline float field_subpixel_position(float displayed, uint16_t prepared, float anchor) {
    return displayed == float(prepared >> 1) + anchor ? displayed + float(prepared & 1) * 0.5f : displayed;
}
// Live native objects already simulate off-screen. Decode their current art without allocating OAM.
std::vector<Mode7Sprite> field_npc_sprites(emu::Wsc &w);
class Mode7World {
  public:
    bool update(emu::Wsc &, const PresentationScene &, const LightTags &, const LandmarkTags &,
                bool extend_npcs = true);
    // A held world (a service screen is open over the field) keeps its layout, scroll and sprites but takes
    // the live colors: the Inn's fade to black and back runs under the menu. Not an update: it reads no new
    // sprites, so the UI mask still leaves sprites no record owns to the native picture.
    void refresh_palettes(emu::Wsc &);
    // Light-tile pixels re-extracted per update (0: no limit). Fades and animated light tiles make every
    // nearby lit chunk stale at once; with a budget the oldest catch up over the next frames instead.
    size_t light_pixel_budget = 0;
    PresentationScene project(const Mode7Camera &, bool snap, uint64_t frame) const;
    // Presentation overrides are restored before the next completed-state collection.
    void set_motion(std::array<float, 2> scroll, const std::map<uint32_t, std::array<float, 2>> &positions) {
        scroll_ = scroll;
        for (auto &sprite : sprites_)
            if (auto i = positions.find(sprite.record); sprite.record && i != positions.end()) {
                sprite.x = i->second[0];
                sprite.y = i->second[1];
            }
        std::stable_sort(sprites_.begin(), sprites_.end(), mode7_depth_less);
    }
    int map() const { return map_; }
    const Mode7Surface &surface() const { return surface_; }
    bool water_at(float x, float y) const;
    const std::vector<Mode7Sprite> &sprites() const { return sprites_; }
    std::array<float, 2> scroll() const { return scroll_; }

  private:
    int map_ = -1, set_ = -1;
    uint64_t landmark_hash_ = 0, card_revision_ = 0;
    Mode7Surface surface_;
    std::array<uint8_t, 0x3100> topology_{};
    std::array<uint8_t, 32> foreground_flags_{};
    std::array<uint64_t, 1024> tile_hashes_{};
    std::array<uint16_t, 1024> tile_colors_{};
    std::array<bool, 1024> ground_tiles_{};
    std::array<bool, 16> ground_palettes_{};
    uint64_t topology_revision_ = 0, table_revision_ = 0;
    bool palettes_refreshed_ = false; // landmark cards keep the colors of the last update
    std::array<bool, 1024> landmark_tiles_{};
    std::array<uint16_t, 16> landmark_palettes_{};
    std::array<std::vector<uint16_t>, 54> light_words_{};
    std::map<int, std::array<uint16_t, 4>> words_;
    std::map<int, int> repair_;
    std::vector<Mode7Sprite> landmarks_, sprites_;
    std::map<uint64_t, Mode7Sprite> actor_cache_;
    struct LightChunk {
        uint64_t hash = 0;   // the art and colors of the chunk's tagged words
        uint64_t layout = 0; // topology_revision_ + 1 when pixels were collected
        struct Pixel {
            uint8_t x, y;
            uint16_t tile, address; // address: the tile-RAM byte holding the pixel's index
            uint8_t palette, mirror;
            bool low; // the index is that byte's low nibble
        };
        std::vector<Pixel> pixels; // every tagged light-tile pixel; index and color are read live
        std::vector<PointLight> lights;
        uint64_t stale_since = 0; // light_clock_ when the chunk's inputs last diverged from its lights
    };
    std::array<LightChunk, 54> light_chunks_{};
    uint64_t light_clock_ = 0;
    PresentationScene scene_;
    std::array<float, 2> scroll_{};
};
// Samples the loaded MapData grid/metatile atlas and live palette/tile RAM. Border clamps, no wrap.
Mode7Picture mode7_picture(emu::Wsc &w, const PresentationScene &flat, const LightTags &tags,
                           const Mode7Camera &camera, bool snap, const LandmarkTags *landmarks = nullptr);
// A presentation-only copy of the last eligible world; menus/warps cannot replace its atlas mid-tilt.
class Mode7Source {
  public:
    void latch(emu::Wsc &w, const PresentationScene &scene);
    Mode7Picture picture(const LightTags &tags, const Mode7Camera &camera, bool snap,
                         const LandmarkTags *landmarks) const;
    int map() const { return scene_.map; }

  private:
    std::unique_ptr<emu::Wsc> machine_;
    PresentationScene scene_;
    std::map<int, std::array<uint16_t, 4>> words_;
    int tileset_ = 0;
};
} // namespace sawgod::game
