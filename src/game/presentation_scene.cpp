// Derives field and battle presentation layers from observed native game events.
#include "game/presentation_scene.h"
#include "engine/palette_fade.h"
#include "game/game_config.h"
#include "game/machine.h"
#include "game/maps.h"
#include "game/presentation_ownership.h"
#include "game/session.h"
#include "game/terrain_veil.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string_view>

namespace sawgod::game {
namespace {
struct LitAnimation {
    int rom;            // bank-37 routine entry
    int peak;           // brightness normalization in effect pixels (shared magic stages use 650)
    bool settle;        // skip the first raster after starting: it can still hold casting/opening sprites
    uint32_t color = 0; // fixed RGB; zero samples the effect's bright pixels
    float gain = 1;
    float reach = 4;             // outer support radius from native emitting pixel centers
    int palette = -1;            // DC12 variant; -1 matches every palette
    bool subtractive = false;    // dim toward color instead of emitting light
    bool delivery_color = false; // brightness-weight all nonblack covered pixels in the live palette
};
// Reach is the exact outer support radius: zero at/beyond this distance from emitting pixel centers.
// Shared effect 7 is gated by palette, so enabling sleep cannot enhance death or unrelated statuses.
constexpr LitAnimation kLitAnimations[] = {
    {0x379090, 4000, false, 0, 1, 8}, // live fire landing: one tile
    {0x373370, 4400, true},           // ice wall: light only, preserve native holes and every portrait pixel
    {0x378AA0, 850, true, 0xFFF080, 1.25f},          // brighter, warmer yellow thunder
    {0x3786A0, 350, true},                           // poison bubbles
    {0x375F40, 2300, true, 0, 1, 4, 1},              // sleep's gold cloud only
    {0x375F40, 2300, true, 0x201828, 1, 4, 4, true}, // death: sprite-shaped gloom
    {0x376A80, 1250, true, 0x201828, 1, 4, 4, true}, // native dark follow-up
    {0x378130, 650, true, 0xFFFFFF, 1, 8},           // shared casting, every palette/action
    {0x378EA0, 650, true, 0, 1, 8, -1, false, true}, // fire delivery
    {0x3788B0, 650, true, 0, 1, 8, -1, false, true}, // thunder delivery
    {0x3784B0, 650, true, 0, 1, 8, -1, false, true}, // cloud delivery
};
constexpr int kPatch = 8; // the glow follows the effect's own shape in 8x8-pixel patches
} // namespace
// Native sprites above the panels light (type 6) or dim (type 7) the scene; never UI or the effect art.
// Each occupied 8x8 patch carries its exact 64-bit pixel coverage. The GPU measures distance to the
// nearest emitting pixel, not a bounding rectangle or centroid; sparse patches cannot broaden the reach.
// Lights combine by maximum, with one sampled or fixed color for the whole effect.
// Brightness rises and falls with the effect's pixel count against its busiest frame, smoothed: up fast, down
// a little slower.
std::vector<PointLight> SceneObserver::animation_lights(const emu::FrameLayers &layers,
                                                        uint64_t frame) const {
    const auto *lit =
        std::find_if(std::begin(kLitAnimations), std::end(kLitAnimations), [&](const LitAnimation &a) {
            return a.rom == animation_ && (a.palette < 0 || a.palette == animation_palette_);
        });
    if (!animation_enabled(animation_, animation_palette_) || lit == std::end(kLitAnimations) ||
        (lit->settle && frame <= animation_start_frame_ + 1)) {
        level_ = 0;
        return {};
    }
    struct Patch {
        int n = 0, x0 = 224, y0 = 80, x1 = -1, y1 = -1;
        std::array<uint32_t, 2> pixels{};
    };
    std::array<Patch, (224 / kPatch) * (80 / kPatch)> patches{};
    double r = 0, g = 0, b = 0, bright = 0;
    int total = 0;
    for (int y = 0; y < 80; ++y) {
        if (!layers.valid[size_t(y)])
            continue;
        for (int x = 0; x < 224; ++x) {
            const size_t at = size_t(y * 224 + x);
            for (const auto layer : {emu::FrameLayers::SpriteFront, emu::FrameLayers::SpriteBehind}) {
                const auto &p = layers.planes[size_t(layer)][at];
                if (!(p.flags & emu::RasterPixel::Present))
                    continue;
                auto &patch = patches[size_t(y / kPatch * (224 / kPatch) + x / kPatch)];
                ++patch.n;
                const int bit = y % kPatch * kPatch + x % kPatch;
                patch.pixels[size_t(bit / 32)] |= uint32_t(1) << (bit % 32);
                patch.x0 = std::min(patch.x0, x);
                patch.x1 = std::max(patch.x1, x);
                patch.y0 = std::min(patch.y0, y);
                patch.y1 = std::max(patch.y1, y);
                ++total;
                const int cr = p.color >> 8 & 15, cg = p.color >> 4 & 15, cb = p.color & 15;
                const int brightness = cr * 3 + cg * 6 + cb;
                if (lit->delivery_color) {
                    // Mean weighted by native RGB brightness (3:6:1). Black contributes zero.
                    // Sampling raster colors, rather than palette IDs, follows any caller/recolor.
                    r += cr * brightness;
                    g += cg * brightness;
                    b += cb * brightness;
                    bright += brightness;
                } else if (brightness >= 70) { // keep the existing phase-3 bright-pixel rule
                    r += cr;
                    g += cg;
                    b += cb;
                    ++bright;
                }
                break;
            }
        }
    }
    const bool new_frame = frame != level_frame_;
    if (new_frame) {
        const float target = std::min(1.0f, float(total) / float(lit->peak));
        level_ += (target - level_) * (target > level_ ? 0.6f : 0.25f);
        level_frame_ = frame;
    }
    std::vector<PointLight> lights;
    if ((!lit->color && bright < (lit->delivery_color ? 1 : 4)) || level_ < 0.01f)
        return lights;
    std::array<float, 3> color;
    if (lit->color) {
        color = {float(lit->color >> 16 & 255) / 255, float(lit->color >> 8 & 255) / 255,
                 float(lit->color & 255) / 255};
    } else {
        // Dividing the weighted mean by its largest channel cancels the common weight sum.
        // Preserve hue/saturation, normalize peak RGB to white's 1.0; keep intensity unchanged.
        const double peak = std::max({r, g, b});
        color = {float(r / peak), float(g / peak), float(b / peak)};
    }
    if (lit->delivery_color && new_frame) {
        const char *trace = std::getenv("SAWGOD_BATTLE_TRACE");
        if (trace && std::string_view(trace) == "1")
            std::fprintf(stderr, "Battle delivery glow: frame=%llu rom=%06X rgb=#%02X%02X%02X\n",
                         (unsigned long long)frame, animation_, unsigned(std::lround(color[0] * 255)),
                         unsigned(std::lround(color[1] * 255)), unsigned(std::lround(color[2] * 255)));
    }
    for (const auto &patch : patches) {
        if (patch.n == 0)
            continue;
        PointLight l;
        l.type = lit->subtractive ? 7 : 6;
        l.position = {float(patch.x0 + patch.x1 + 1) / 2, float(patch.y0 + patch.y1 + 1) / 2, lit->reach, 0};
        l.extent = {float(patch.x1 - patch.x0 + 1) / 2 - 0.5f, float(patch.y1 - patch.y0 + 1) / 2 - 0.5f};
        // twice a torch (0.18) at the effect's peak; a sparse patch glows less than a full one
        const float fill = std::min(1.0f, float(patch.n) / float(kPatch * kPatch / 2));
        l.emission = {color[0], color[1], color[2], 0.36f * lit->gain * level_ * fill};
        l.source_pixels = patch.pixels;
        l.beam = {0, 0, 0, float(total)}; // debug: effect pixels this frame
        lights.push_back(l);
    }
    return lights;
}
BattleAnimationOptions BattleAnimationOptions::environment() {
    // All on; "0" turns one off for a before/after capture.
    const auto enabled = [](const char *key) {
        const char *value = std::getenv(key);
        return !value || std::string_view(value) != "0";
    };
    return {enabled("SAWGOD_BATTLE_ICE"),    enabled("SAWGOD_BATTLE_THUNDER"),
            enabled("SAWGOD_BATTLE_POISON"), enabled("SAWGOD_BATTLE_SLEEP"),
            enabled("SAWGOD_BATTLE_DEATH"),  enabled("SAWGOD_BATTLE_CAST_GLOW")};
}
bool SceneObserver::animation_enabled(int rom, int palette) const {
    switch (rom) {
    case 0x378130:
    case 0x378EA0:
    case 0x3788B0:
    case 0x3784B0:
        return battle_options_.cast_glow;
    case 0x379090:
        return true;
    case 0x373370:
        return battle_options_.ice;
    case 0x378AA0:
        return battle_options_.thunder;
    case 0x3786A0:
        return battle_options_.poison;
    case 0x375F40:
        return palette == 1 ? battle_options_.sleep : palette == 4 && battle_options_.death;
    case 0x376A80:
        return palette == 4 && battle_options_.death;
    default:
        return false;
    }
}
void SceneObserver::install(emu::Wsc &w) {
    battle_animations(BattleAnimationOptions::environment());
    subscriptions_.clear();
    generation_ = Session::of(w).generation();
    battle_ = false;
    field_map_ = -1;
    confirmed_ = PresentationScene::Kind::Unknown;
    picture_generation_ = ~0ull;
    confirmed_map_ = -1;
    entering_battle_ = false;
    overlay_ = false;
    animation_ = 0;
    auto &r = Session::of(w).hooks();
    // Opt-in diagnostics observe native resolution boundaries, without touching game state.
    const char *trace_value = std::getenv("SAWGOD_BATTLE_TRACE");
    const bool trace = trace_value && std::string_view(trace_value) == "1";
    if (trace)
        subscriptions_.push_back(
            r.subscribe(hooks::Event::BattleHpChange, [](Machine &m, const hooks::Args &a) {
                std::fprintf(stderr, "Battle HP: frame=%llu actor=%d change=%d previous=%d\n",
                             (unsigned long long)m.wsc().frame_count, a.a, a.b, a.c);
            }));
    subscriptions_.push_back(
        r.subscribe(hooks::Event::PresentationField, [this](Machine &m, const hooks::Args &) {
            generation_ = Session::of(m.wsc()).generation();
            field_map_ = m.wsc().ram[0xD0DC];
            battle_ = false;
        }));
    subscriptions_.push_back(
        r.subscribe(hooks::Event::ScriptedFieldOverlay, [this](Machine &m, const hooks::Args &a) {
            overlay_ = a.a != 0;
            overlay_generation_ = Session::of(m.wsc()).generation();
        }));
    subscriptions_.push_back(r.subscribe(hooks::Event::BattleStart, [this](Machine &m, const hooks::Args &) {
        if (generation_ != Session::of(m.wsc()).generation())
            field_map_ = -1;
        generation_ = Session::of(m.wsc()).generation();
        battle_ = true;
    }));
    subscriptions_.push_back(
        r.subscribe(hooks::Event::BattleEnd, [this](Machine &, const hooks::Args &) { battle_ = false; }));
    subscriptions_.push_back(
        r.subscribe(hooks::Event::BattleAnimation, [this](Machine &m, const hooks::Args &a) {
            level_ = 0;
            level_frame_ = ~0ull;
            animation_ = a.a;
            animation_palette_ = a.b;
            animation_start_frame_ = m.wsc().frame_count;
        }));
    subscriptions_.push_back(
        r.subscribe(hooks::Event::BattleAnimationEnd, [this, trace](Machine &m, const hooks::Args &a) {
            if (trace)
                std::fprintf(stderr, "Battle animation end: frame=%llu rom=%06X\n",
                             (unsigned long long)m.wsc().frame_count, a.a);
            if (animation_ == a.a)
                animation_ = 0;
        }));
}
namespace {
bool field_descriptor(emu::Wsc &w, int map) {
    // The field remains loaded between its waits, including the generic VBlank wait
    // at each tile boundary. C813 is producer scheduling, not field identity.
    if (map <= 0 || map >= kMapCount || (w.io[7] & 15) != 2 || (w.io[0x60] & 0xE0) != 0xE0)
        return false;
    const auto *row = Session::of(w).tables().row(Table::maps, unsigned(map));
    if (!row && w.rom.size() != 0x400000)
        return false;
    if (!row)
        row = w.rom.data() + 0x35A830 + unsigned(map) * 18;
    // load_field_map copies the bank and four source pointers here. A destination ID written
    // during a warp does not qualify until that destination's actual field has been loaded.
    return row[1] && std::equal(row + 1, row + 10, w.ram.begin() + 0xD52D);
}
} // namespace
bool recognized_field(emu::Wsc &w) { return field_descriptor(w, w.ram[0xD0DC]); }
bool battle_entry_wipe(const emu::Wsc &w) {
    return (w.io[0xB2] & 0x10) && w.ram[0x90] == 0x10 && w.ram[0x91] == 0x13 && w.ram[0x92] == 0 &&
           w.ram[0x93] == 0x50;
}
bool presented_battle_entry_wipe(const emu::Wsc &live) {
    // VBlank can arm or disarm the handler after the displayed field was rasterized.
    // Scene classification, camera steps and the retained wipe must use the same picture boundary.
    const auto *session = static_cast<const Session *>(live.game_session.get());
    return battle_entry_wipe(session ? session->raster_field(live) : live);
}
bool SceneObserver::field_published(emu::Wsc &w) const {
    return generation_ == Session::of(w).generation() && field_map_ >= 0;
}
PresentationScene SceneObserver::describe(emu::Wsc &w, bool battle, bool full_ui, bool scene) const {
    PresentationScene result;
    result.mask.fill(0xFF0000);
    const auto *layers = w.frame_layers(false, scene);
    auto &inputs = Session::of(w).raster_field(w);
    result.map = inputs.ram[0xD0DC];
    const auto generation = Session::of(w).generation();
    if (picture_generation_ != generation) {
        picture_generation_ = generation;
        confirmed_ = PresentationScene::Kind::Unknown;
        confirmed_map_ = -1;
        entering_battle_ = false;
    }
    const auto transition = [&](PresentationScene::Transition reason) {
        result.kind = PresentationScene::Kind::Transition;
        result.previous = confirmed_;
        result.transition = reason;
        result.field_cleared = true;
        result.perspective_allowed = result.camera_field();
        if (result.camera_field())
            result.map = confirmed_map_;
        return result;
    };
    // The native wipe owns its raster, and ends field ownership before the black battle setup.
    if (presented_battle_entry_wipe(w)) {
        entering_battle_ = true;
        confirmed_ = PresentationScene::Kind::Unknown;
        return transition(PresentationScene::Transition::BattleWipe);
    }
    const bool published = field_published(w);
    if (!recognized_field(inputs) && published && field_descriptor(inputs, field_map_))
        result.map = field_map_;
    const bool field_picture = field_descriptor(inputs, result.map);
    const bool field_owned = published || inputs.ram[0xD519] == 1 || GameConfig::shown_world(result.map);
    if (full_ui)
        return result;
    // A scripted overlay draws the whole picture over the field (the story lightning): show the native
    // raster, keep the camera family.
    if (overlay_ && overlay_generation_ == generation && (field_picture && field_owned))
        return transition(PresentationScene::Transition::ScriptedOverlay);
    // Evidence is the completed ordinary raster, never the clean-scene underlay or end-of-frame RAM.
    // Transparent blank cells produce no terrain pixels. They are inconclusive during map loads,
    // not a new scene: keep the camera family but let the native picture show through.
    const auto *native = w.frame_layers();
    bool raster = false, terrain = false, portrait = false, uniform = true;
    uint16_t first = 0;
    if (native)
        for (size_t at = 0; at < result.mask.size(); ++at) {
            if (!native->valid[at / 224])
                continue;
            if (uniform) {
                const auto color = native->color(at);
                if (!raster)
                    first = color;
                uniform = color == first;
            }
            raster = true;
            if (!terrain) {
                const auto &ground = native->planes[emu::FrameLayers::Screen1][at];
                terrain = (ground.flags & emu::RasterPixel::Present) &&
                          !UiOwnership{true, false}.owns(ground, emu::FrameLayers::Screen1);
            }
            if (!portrait) {
                const auto &enemy = native->planes[emu::FrameLayers::Screen2][at];
                portrait = (enemy.flags & emu::RasterPixel::Present) && enemy.palette >= 4 &&
                           enemy.palette <= 6 && enemy.tile >= 0x300 && enemy.tile < 0x378;
            }
            if (!uniform && terrain && portrait)
                break;
        }
    if (!layers || !raster) {
        if (confirmed_ != PresentationScene::Kind::Unknown || (field_picture && field_owned) ||
            entering_battle_)
            return transition(PresentationScene::Transition::RasterUnavailable);
        return result;
    }
    // Battle UI may open after the first portrait, and close before the last reward picture.
    // Require picture evidence at entry; retain the family until field terrain is confirmed again.
    const bool battle_context = battle_ && generation_ == generation;
    const bool battlefield = portrait && (battle || battle_context || entering_battle_ ||
                                          confirmed_ == PresentationScene::Kind::Battle);
    if (battlefield || (battle && (battle_context || confirmed_ == PresentationScene::Kind::Battle))) {
        result.kind = PresentationScene::Kind::Battle;
        entering_battle_ = false;
    } else if (uniform && (confirmed_ != PresentationScene::Kind::Unknown || (field_picture && field_owned) ||
                           entering_battle_)) {
        return transition(PresentationScene::Transition::Uniform);
    } else if (field_picture && field_owned && terrain && !battle) {
        result.kind = PresentationScene::Kind::Field;
    } else if (confirmed_ != PresentationScene::Kind::Unknown || (field_picture && field_owned) ||
               entering_battle_) {
        return transition(PresentationScene::Transition::TerrainAbsent);
    } else {
        return result;
    }
    confirmed_ = result.kind;
    confirmed_map_ = result.map;
    const bool field = result.kind == PresentationScene::Kind::Field;
    if (field) {
        generation_ = generation;
        field_map_ = result.map;
    }
    result.perspective_allowed = field;
    if (!field)
        result.background.resize(result.mask.size());
    const UiOwnership ownership{field, !field, animation_enabled(animation_, animation_palette_)};
    PaletteFadeFit fade;
    bool scene_color = false, all_black = true, all_white = true;
    std::array<int, 256> fade_colors;
    fade_colors.fill(-1);
    std::map<int, std::array<float, 4>> actors;
    std::array<int, 3> left{224, 224, 224}, right{-1, -1, -1}, bottom{-1, -1, -1};
    std::array<int, 3> picture_top{144, 144, 144}, picture_bottom{-1, -1, -1};
    // Each enemy group draws with palette 4 + group, but a boss (one group) spreads its large picture over
    // palettes 4-6 (Genbu: 4 and 5). Palettes past the formation's group count (D6D4h) belong to its last
    // group, so the boss's target outline, light and shadow cover the whole picture.
    const int formation_groups = inputs.ram[0xD6D4];
    const size_t last_group =
        formation_groups >= 1 && formation_groups <= 3 ? size_t(formation_groups - 1) : 2;
    for (size_t at = 0; at < result.mask.size(); ++at) {
        const int y = int(at / 224), x = int(at % 224);
        if (!layers->valid[size_t(y)]) {
            result.mask[at] = 0xFF0000;
            continue;
        }
        bool ui = w.ui_coverage && (*w.ui_coverage)[at];
        bool occlusion = false;
        bool enemy = false;
        // Ownership is game-specific, evaluated from every covered layer, even if a
        // foreground actor happens to cover an equal-colored window pixel.
        for (int layer = 1; layer < emu::FrameLayers::Count; ++layer) {
            const auto &p = layers->planes[size_t(layer)][at];
            if (!(p.flags & emu::RasterPixel::Present))
                continue;
            const bool sprite =
                layer == emu::FrameLayers::SpriteBehind || layer == emu::FrameLayers::SpriteFront;
            const bool native_ui = ownership.owns(p, layer);
            ui |= native_ui;
            // In battle, screen 2 also draws the floor: only enemy pictures (marked below) block shadows
            // there.
            occlusion |= sprite || (field && layer == emu::FrameLayers::Screen2 && !native_ui);
            if (field && sprite && !native_ui && p.tile >= 0x10 && p.tile < 0x98) {
                const int bank = p.tile / 8, q = p.tile & 3;
                // Both OAM writers place tile + 1 to the right and tile + 2 below.
                const int dx = q % 2;
                // The last covered raster row is the drawn foot, including flipped/walking art.
                // p.y is the tile origin: the 16x16 cell can have transparent bottom padding.
                actors[bank] = {float(p.x - dx * 8 + 8), float(y) - 0.5f, 6.6f, 2.2f};
            }
            if (!field && layer == emu::FrameLayers::Screen2 && p.palette >= 4 && p.palette <= 6 &&
                p.tile >= 0x300 && p.tile < 0x378) {
                enemy = true;
                const size_t group = std::min(size_t(p.palette - 4), last_group);
                picture_top[group] = std::min(picture_top[group], y);
                picture_bottom[group] = std::max(picture_bottom[group], y);
                // Anchor at the bottom of the latched portrait cells, rather than
                // moving the feet with the silhouette/dissolve's last opaque pixel.
                const auto &regs = layers->registers[size_t(y)];
                const int cell_x = x - ((x + regs[0x12]) & 7);
                const int cell_y = y - ((y + regs[0x13]) & 7);
                left[group] = std::min(left[group], cell_x);
                right[group] = std::max(right[group], cell_x + 7);
                bottom[group] = std::max(bottom[group], cell_y + 7);
            }
        }
        // Native battle panels, party portraits and badges are UI, not standing actors.
        if (ownership.panel(y))
            ui = true;
        // G 0xFF: an enemy picture is the visible pixel; 0xFE: an enemy under a sprite (an effect drawn over
        // it). Both stay protected; only visible enemy pixels receive light.
        // Native spell animations replace OAM, including entries 0-26 normally used by UI.
        // Restore their visible pixels exactly, without the UI filter margin; only surrounding scene glows.
        // Remove them from ambient bloom extraction too, so no second glow exceeds their reach.
        const auto &top = layers->top(at);
        const bool source_top = !field && animation_enabled(animation_, animation_palette_) &&
                                (&top == &layers->planes[emu::FrameLayers::SpriteFront][at] ||
                                 &top == &layers->planes[emu::FrameLayers::SpriteBehind][at]);
        const bool enemy_top = enemy && &layers->top(at) == &layers->planes[emu::FrameLayers::Screen2][at];
        result.mask[at] = (ui ? 0xFF0000u : 0u) | (enemy       ? (enemy_top ? 0x00FF00u : 0x00FE00u)
                                                   : occlusion ? 0x004000u
                                                               : 0u);
        if (source_top && !ui)
            result.mask[at] = 0x00FE00;
        if (!ui) {
            scene_color = true;
            all_black &= top.color == 0;
            all_white &= top.color == 0xFFF;
            // Read the completed raster, never the next frame's live palette RAM. The native target
            // table is shared by fields, worlds and battles; transparent entries are not evidence.
            const auto &p = layers->planes[emu::FrameLayers::Screen1][at];
            if (p.flags & emu::RasterPixel::Present) {
                const size_t key = size_t(p.palette * 16 + p.index);
                if (fade_colors[key] != p.color) {
                    const size_t target = 0xC600 + key * 2;
                    fade.add(uint16_t(inputs.ram[target] | inputs.ram[target + 1] << 8), p.color);
                    fade_colors[key] = p.color;
                }
            }
        }
        if (!field) {
            // Bloom sees the actual background beneath the portrait, never its
            // colors or a black silhouette hole. Foreground UI is restored later.
            const auto &p = enemy || source_top ? layers->planes[emu::FrameLayers::Screen1][at] : top;
            const uint16_t c = (p.flags & emu::RasterPixel::Present)
                                   ? p.color
                                   : layers->planes[emu::FrameLayers::Backdrop][at].color;
            result.background[at] =
                uint32_t((c >> 8 & 15) * 17 << 16 | (c >> 4 & 15) * 17 << 8 | (c & 15) * 17);
        }
    }
    result.palette_range = fade.range();
    if (scene_color && (all_black || all_white))
        result.palette_range = all_white ? std::array<float, 2>{1, 1} : std::array<float, 2>{0, 0};
    // The flat veil draws the lower body even on native OAM's hidden half-frames. Use the same
    // reconstructed card, so its contact does not jump up to the torso on alternate frames.
    if (field) {
        for (const auto &card : veiled_terrain_cards(inputs)) {
            const int bank = (card.words[0] & 511) / 8;
            if (auto actor = actors.find(bank); actor != actors.end())
                for (int q = 0; q < 4; ++q) {
                    const int word = card.words[size_t(q)];
                    for (int y = 0; y < 8; ++y)
                        for (int x = 0; x < 8; ++x) {
                            const int sx = word & 0x4000 ? 7 - x : x, sy = word & 0x8000 ? 7 - y : y;
                            const int byte = inputs.ram[size_t(0x4000 + (word & 511) * 32 + sy * 4 + sx / 2)];
                            if ((sx & 1 ? byte & 15 : byte >> 4) != 0)
                                actor->second[1] =
                                    std::max(actor->second[1], float(card.y + q / 2 * 8 + y) - 0.5f);
                        }
                }
        }
    }
    for (const auto &[id, pose] : actors) {
        (void)id;
        if (pose[0] >= -6 && pose[0] <= 230 && pose[1] >= 0 && pose[1] < 144)
            result.actors.push_back(pose);
    }
    // Both boss-defeat IRQs slide portrait rows apart: the Creator's sweep (HBlank, 6000:C681) and every
    // other boss's shutter (line compare, 6000:C7C2). The pieces retain enemy ownership, but their moving
    // cell bounds no longer describe feet.
    auto installed = [&](uint8_t enable, uint16_t vector, uint16_t offset) {
        return (w.io[0xB2] & enable) && inputs.ram[vector] == uint8_t(offset) &&
               inputs.ram[vector + 1] == uint8_t(offset >> 8) && inputs.ram[vector + 2] == 0 &&
               inputs.ram[vector + 3] == 0x60;
    };
    const bool defeat_raster = installed(0x80, 0x9C, 0xC681) || installed(0x10, 0x90, 0xC7C2);
    for (size_t g = 0; !field && g < 3; ++g)
        if (right[g] >= left[g]) {
            result.battle_groups[g] = {float(left[g] + right[g] + 1) / 2,
                                       float(picture_top[g] + picture_bottom[g]) / 2,
                                       float(picture_bottom[g]), 1};
            // Enemy ground shadows sized to the sprite: as wide as the group's picture,
            // a flat ellipse centered on its feet so the lower half reads on the floor
            if (!defeat_raster)
                result.actors.push_back(
                    {float(left[g] + right[g] + 1) / 2, float(bottom[g] - 2), float(right[g] - left[g] + 1),
                     -std::clamp(float(right[g] - left[g] + 1) * 0.2f, 4.0f, 10.0f)}); // < 0: solid
        }
    if (!field) {
        const auto lights = animation_lights(*layers, w.frame_count);
        result.lights.insert(result.lights.end(), lights.begin(), lights.end());
    }
    if (field) {
        const auto *row = Session::of(w).tables().row(Table::maps, unsigned(result.map));
        if (row) {
            std::erase_if(result.actors, [&](const auto &pose) {
                return light_tags_.water_at(*layers, int(std::floor(pose[0])), int(std::floor(pose[1])),
                                            row[0]);
            });
            result.water = light_tags_.water_pixels(*layers, result.mask.data(), row[0]);
            result.lights = light_tags_.visible(*layers, result.mask.data(), row[0], w.frame_count);
        }
    }
    return result;
}
} // namespace sawgod::game
