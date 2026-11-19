// CPU-only, opt-in presentation diagnostic. Never changes the machine or native renderer.
#pragma once
#include "emu/png.h"
#include "game/battle_ui.h"
#include "game/dialogue_ui.h"
#include "game/host_picture.h"
#include "game/mode7.h"
#include "game/service_ui.h"
#include "game/session.h"
#include "game/terrain_veil.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>

namespace sawgod::tools {
class EnhancedOracle {
  public:
    explicit EnhancedOracle(const std::string &directory, bool terrain_veil)
        : directory_(directory), allow_veil_(terrain_veil) {
        std::filesystem::create_directories(directory_);
        trace_.open(directory_ / "frames.tsv");
        if (!trace_)
            throw std::runtime_error("cannot create oracle trace");
        trace_ << "frame\tchannel\tkind\tmap\towner\theld\tcompared\traw\tterrain_"
                  "veil\tdifferent\tx0\ty0\tx1\ty1\tcause\tsources\ttriple\n";
    }
    void install(emu::Wsc &w, game::HostPicture::Screens screens) {
        picture_.install(w, screens);
        observer_.install(w);
        observer_.battle_animations({});
        w.capture_layers(true, false);
        screens_ = screens;
    }
    void frame(emu::Wsc &w, int frame) {
        const auto generation = game::Session::of(w).generation();
        if (generation != generation_) {
            world_ = {};
            held_field_ = {};
            generation_ = generation;
        }
        picture_.frame(w);
        coverage_.fill(0);
        auto *previous = w.ui_coverage;
        w.ui_coverage = &coverage_;
        auto composed = picture_.picture(w, emu::Wsc::ColorMode::Raw, false, false);
        const bool battle = screens_.battle->active();
        const bool service = screens_.service->active() && picture_.shown() == 4;
        const bool host_full = picture_.full_ui() && !service;
        const bool full = picture_.native_full_screen(w) || host_full;
        const bool windows = screens_.dialogue->active() || service;
        if (!picture_.holding()) {
            scene_ = observer_.describe(w, battle, full, windows || battle);
            if (service && scene_.kind != game::PresentationScene::Kind::Field &&
                held_field_.kind == game::PresentationScene::Kind::Field && held_field_.map == scene_.map &&
                !game::recognized_field(w)) {
                scene_ = held_field_;
                scene_.perspective_allowed = false;
            } else if (!service) {
                held_field_ = scene_;
            }
            if (scene_.kind == game::PresentationScene::Kind::Field && !battle)
                game::blend_terrain_veil(w, composed, &coverage_);
            const bool field = scene_.kind == game::PresentationScene::Kind::Field;
            const bool layout = full || battle || game::presented_battle_entry_wipe(w);
            reconstruct_ = game::field_zoom_allowed(scene_.map, FidelityMode::Enhanced, field, layout) ||
                           game::mode7_allowed(scene_.map, FidelityMode::Enhanced, field, layout);
            if (reconstruct_)
                reconstruct_ = world_.update(w, scene_, observer_.light_tags(), observer_.landmark_tags());
            mask_ = ui_cache_.get(w, battle, windows || battle, service && scene_.map == held_field_.map,
                                  reconstruct_);
        }
        if (picture_.holding() && !composed.empty() &&
            std::all_of(composed.begin(), composed.end(), [&](uint32_t c) { return c == composed.front(); }))
            scene_.hold_uniform_picture();
        if (battle && scene_.kind == game::PresentationScene::Kind::Battle)
            mask_ = scene_.mask;
        // A held presentation can still show a menu while the native machine already
        // draws its successor. Exclude UI on either endpoint, using the same ownership
        // rule on the current native raster rather than a stale retained scene mask.
        reference_mask_ = game::mode7_ui_mask(w, battle, false);
        if (!picture_.holding())
            held_full_ui_ = host_full;
        if (host_full || (picture_.holding() && held_full_ui_))
            coverage_.fill(1);
        const auto native = w.framebuffer_rgb(emu::Wsc::ColorMode::Raw);
        auto veiled = native;
        if (scene_.kind == game::PresentationScene::Kind::Field)
            game::blend_terrain_veil(
                w, veiled, &coverage_,
                reconstruct_ && !picture_.holding() ? &game::Session::of(w).raster_field(w) : nullptr);
        std::vector<uint32_t> sources(native.size(), 2); // 2 = composed HostPicture
        if (reconstruct_ && !scene_.field_cleared) {
            auto neutral = game::mode7_neutral_picture(world_.surface(), world_.sprites(), world_.scroll());
            if (neutral.pixels.size() != native.size())
                throw std::runtime_error("missing neutral reconstruction");
            // Exercise the same production camera and metadata projection consumed by effects.
            const game::Mode7Camera camera{0, 24, 0, 1};
            projected_ = world_.project(camera, true, w.frame_count);
            for (size_t at = 0; at < composed.size(); ++at)
                if (!(mask_[at] & 0xFF0000)) {
                    composed[at] = neutral.pixels[at];
                    sources[at] = neutral.sources[at];
                }
        }
        compare(w, frame, "scene", native, composed, veiled, sources, false);
        if (scene_.background.size() == native.size()) {
            auto expected = native;
            const auto *layers = w.frame_layers();
            if (!layers)
                throw std::runtime_error("missing native battle layers");
            for (size_t at = 0; at < expected.size(); ++at) {
                // Under a native enemy portrait the reference is the native SCR1 raster,
                // never the clean-scene underlay being tested.
                const auto &enemy = layers->planes[emu::FrameLayers::Screen2][at];
                const bool portrait = (enemy.flags & emu::RasterPixel::Present) && enemy.palette >= 4 &&
                                      enemy.palette <= 6 && enemy.tile >= 0x300 && enemy.tile < 0x378;
                const bool animation = (scene_.mask[at] & 0x00FF00) == 0x00FE00;
                if (portrait || animation) {
                    const auto &ground = layers->planes[emu::FrameLayers::Screen1][at];
                    expected[at] = color((ground.flags & emu::RasterPixel::Present)
                                             ? ground.color
                                             : layers->planes[emu::FrameLayers::Backdrop][at].color);
                }
            }
            std::fill(sources.begin(), sources.end(), 3); // scene background
            compare(w, frame, "background", expected, scene_.background, expected, sources, true);
        } else
            spans_[1] = {};
        w.ui_coverage = previous;
    }
    void finish() {
        trace_.flush();
        if (!trace_)
            throw std::runtime_error("oracle trace write failed");
    }

  private:
    struct Span {
        bool active = false;
        int map = -1, kind = -1;
        std::string cause;
    };
    std::array<Span, 2> spans_{};
    std::filesystem::path directory_;
    std::ofstream trace_;
    game::HostPicture picture_;
    game::HostPicture::Screens screens_;
    game::SceneObserver observer_;
    game::Mode7World world_;
    game::Mode7UiMask ui_cache_;
    game::PresentationScene scene_, held_field_, projected_;
    std::array<uint8_t, emu::FrameLayers::Pixels> coverage_{};
    std::array<uint32_t, emu::FrameLayers::Pixels> mask_{}, reference_mask_{};
    uint64_t generation_ = ~0ull;
    bool reconstruct_ = false, allow_veil_ = false, held_full_ui_ = false;
    static uint32_t color(uint16_t c) {
        return uint32_t((c >> 8 & 15) * 17 << 16 | (c >> 4 & 15) * 17 << 8 | (c & 15) * 17);
    }
    void png(const std::string &stem, const std::vector<uint32_t> &pixels) {
        const auto bytes = emu::encode_png(pixels.data(), 224, 144);
        std::ofstream output(directory_ / (stem + ".png"), std::ios::binary);
        output.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
        if (!output)
            throw std::runtime_error("oracle PNG write failed");
    }
    void compare(emu::Wsc &w, int frame, const char *channel, const std::vector<uint32_t> &native,
                 const std::vector<uint32_t> &neutral, const std::vector<uint32_t> &veiled,
                 const std::vector<uint32_t> &sources, bool background) {
        int compared = 0, raw = 0, allowed = 0, different = 0;
        int x0 = 224, x1 = -1, y0 = 144, y1 = -1;
        bool terrain = false, actor = false, host = false, landmark = false;
        std::map<uint32_t, int> counts;
        std::vector<uint32_t> diff(native.size(), 0);
        const auto *layers = w.frame_layers();
        for (size_t at = 0; at < native.size(); ++at) {
            // Actual production UI mask plus host coverage; Unknown's all-red scene mask
            // must not exempt its entire native scene from the assertion.
            if (coverage_[at] || ((mask_[at] | reference_mask_[at]) & 0xFF0000))
                continue;
            ++compared;
            if ((native[at] & 0xFFFFFF) == (neutral[at] & 0xFFFFFF))
                continue;
            ++raw;
            if (allow_veil_ && !background && veiled[at] != native[at] &&
                (neutral[at] & 0xFFFFFF) == (veiled[at] & 0xFFFFFF)) {
                ++allowed;
                diff[at] = 0x0080FF;
                continue;
            }
            ++different;
            diff[at] = 0xFF3030;
            x0 = std::min(x0, int(at % 224));
            x1 = std::max(x1, int(at % 224));
            y0 = std::min(y0, int(at / 224));
            y1 = std::max(y1, int(at / 224));
            const auto source = sources[at];
            ++counts[source];
            terrain |= source == 0;
            landmark |= source == 1;
            host |= source == 2;
            actor |= source > 3;
        }
        std::string cause = background ? "battle-background"
                            : terrain  ? "terrain-reconstruction"
                            : actor    ? "actor-reconstruction"
                            : landmark ? "landmark"
                            : host     ? "host-picture"
                                       : "equal";
        std::string source_list;
        for (const auto &[source, count] : counts) {
            char item[96];
            std::snprintf(item, sizeof item, "%s%X:%d", source_list.empty() ? "" : ",", source, count);
            source_list += item;
        }
        // Attach native top-layer provenance at the first differing pixel, for triage.
        if (different && layers) {
            for (size_t at = 0; at < diff.size(); ++at)
                if (diff[at] == 0xFF3030) {
                    const auto &p = layers->top(at);
                    char item[128];
                    std::snprintf(item, sizeof item, ";native-tile=%X,palette=%u,sprite=%u", p.tile,
                                  p.palette, p.sprite);
                    source_list += item;
                    break;
                }
        }
        auto &span = spans_[background ? 1 : 0];
        const int kind = int(scene_.kind);
        std::string triple = "-";
        if (different &&
            (!span.active || span.map != scene_.map || span.kind != kind || span.cause != cause)) {
            triple = std::string(channel) + "-" + std::to_string(frame);
            png(triple + "-native", native);
            png(triple + "-neutral", neutral);
            png(triple + "-diff", diff);
        }
        span = {different != 0, scene_.map, kind, cause};
        trace_ << frame << '\t' << channel << '\t' << kind << '\t' << scene_.map << '\t' << picture_.shown()
               << '\t' << picture_.holding() << '\t' << compared << '\t' << raw << '\t' << allowed << '\t'
               << different << '\t' << (different ? x0 : -1) << '\t' << (different ? y0 : -1) << '\t' << x1
               << '\t' << y1 << '\t' << cause << '\t' << (source_list.empty() ? "-" : source_list) << '\t'
               << triple << '\n';
        if (!trace_)
            throw std::runtime_error("oracle trace write failed");
    }
};
} // namespace sawgod::tools
