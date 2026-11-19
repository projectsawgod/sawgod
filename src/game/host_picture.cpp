// Composes host-rendered menus and dialogue over the current game picture.
#include "game/host_picture.h"
#include "engine/cpu_scope.h"
#include "engine/presentation_diagnostics.h"
#include "game/battle_ui.h"
#include "game/bestiary_ui.h"
#include "game/creation_ui.h"
#include "game/dialogue_ui.h"
#include "game/info.h"
#include "game/inventory_ui.h"
#include "game/item_dialog_ui.h"
#include "game/party_ui.h"
#include "game/presentation_scene.h"
#include "game/save_ui.h"
#include "game/service_ui.h"
#include "game/session.h"
#include "game/text.h"
#include <algorithm>
#include <type_traits>

namespace sawgod::game {
namespace {
// Every channel loses one native level per darken_palette_step. The brightest channel
// therefore measures the remaining fade level, including black and the subsequent fade in.
int palette_peak(const emu::Wsc &w) {
    int peak = 0;
    for (size_t at = 0xFE00; at < 0x10000; at += 2) {
        peak = std::max({peak, int(w.ram[at] & 15), int(w.ram[at] >> 4), int(w.ram[at + 1] & 15)});
    }
    return peak;
}
} // namespace

// Shade copies, never the retained endpoint: repeated draws and hidden-mode refreshes
// must not accumulate the fade. Enhanced fills, text, corners and bars carry the same colors.
std::vector<uint32_t> HostPicture::fade_picture(emu::Wsc &w, std::vector<uint32_t> px, bool host) {
    if (host && warps_)
        warp_picture(w, px);
    if (!host || !fade_peak_)
        return px;
    const int level = std::min(fade_peak_, palette_peak(w));
    if (level == fade_peak_)
        return px;
    const auto shade = [&](uint32_t color) {
        uint32_t result = color & 0xFF000000;
        for (int shift = 0; shift < 24; shift += 8)
            result |= uint32_t(int(color >> shift & 255) * level / fade_peak_) << shift;
        return result;
    };
    for (auto &pixel : px)
        pixel = shade(pixel);
    if (w.ui_fill) {
        for (auto &window : w.ui_fill->windows) {
            window.light = shade(window.light);
            window.dark = shade(window.dark);
        }
        for (auto *colors : {&w.ui_fill->color, &w.ui_fill->beneath})
            for (auto &color : *colors)
                color = shade(color);
    }
    if (w.ui_text) {
        for (auto &pixel : w.ui_text->pixels) {
            pixel.color = shade(pixel.color);
            pixel.beneath = shade(pixel.beneath);
            pixel.shadow_color = shade(pixel.shadow_color);
        }
        for (auto &pixel : w.ui_text->bars.pixels) {
            pixel.color = shade(pixel.color);
            pixel.beneath = shade(pixel.beneath);
        }
        for (auto &corner : w.ui_text->corners) {
            corner.light = shade(corner.light);
            corner.dark = shade(corner.dark);
            corner.stroke = shade(corner.stroke);
            corner.bevel = shade(corner.bevel);
            corner.shadow = shade(corner.shadow);
        }
    }
    return px;
}

// The travel warp's line interrupt scrolls both planes by its row's distortion byte. Shift the host
// picture and its Enhanced records the same way, so the held menu ripples like the native screen; columns
// scrolled in from outside the picture are black, like the native plane's unused columns.
void HostPicture::warp_picture(emu::Wsc &w, std::vector<uint32_t> &px) {
    constexpr uint16_t kEffectDistortion = 0xDCFE;
    constexpr int width = emu::Wsc::kWidth, height = emu::Wsc::kHeight;
    if (px.size() != size_t(width * height))
        return;
    const auto offset = [&](int y) { return int(int8_t(w.ram[kEffectDistortion + y])); };
    const auto shift = [&](auto &pixels) {
        if (pixels.size() != px.size())
            return;
        for (int y = 0; y < height; ++y) {
            const int by = offset(y);
            if (!by)
                continue;
            const auto row = pixels.begin() + y * width;
            using Pixel = std::decay_t<decltype(pixels[0])>;
            const std::vector<Pixel> before(row, row + width);
            for (int x = 0; x < width; ++x)
                row[x] = x + by >= 0 && x + by < width ? before[size_t(x + by)] : Pixel{};
        }
    };
    shift(px);
    if (w.ui_coverage)
        shift(*w.ui_coverage);
    if (w.ui_fill) {
        shift(w.ui_fill->owner);
        shift(w.ui_fill->color);
        shift(w.ui_fill->beneath);
    }
    if (w.ui_text) {
        shift(w.ui_text->pixels);
        shift(w.ui_text->bars.pixels);
        for (auto &rect : w.ui_text->bars.rectangles)
            rect.x -= offset(std::clamp(rect.y, 0, height - 1));
        for (auto &corner : w.ui_text->corners)
            corner.x -= offset(std::clamp(corner.y, 0, height - 1));
    }
}

int HostPicture::owner() const {
    const auto &s = screens_;
    return s.item && s.item->active()             ? 2
           : s.save && s.save->active()           ? 3
           : s.service && s.service->active()     ? 4
           : s.dialogue && s.dialogue->active()   ? 5
           : s.creation && s.creation->active()   ? 6
           : s.bestiary && s.bestiary->active()   ? 7
           : s.party && s.party->active()         ? 8
           : s.inventory && s.inventory->active() ? 9
           : s.battle && s.battle->active()       ? 1
                                                  : 0;
}
void HostPicture::invalidate(emu::Wsc &w, bool transition) {
    sync_generation(w);
    if (!transition && !armed_ && owner() == 0)
        return;
    // Leaving the plain picture: the one not on screen has nothing retained to hold, so keep it now.
    if (!armed_ && shown_ == 0 && w.second())
        for (size_t slot : {size_t(1), size_t(3)})
            if (composed_[slot] < composed_[slot ^ 1]) {
                w.refresh_second();
                last_[slot] =
                    w.framebuffer_rgb(slot >= 2 ? emu::Wsc::ColorMode::Lcd : emu::Wsc::ColorMode::Raw, true);
                composed_[slot] = composed_[slot ^ 1];
                masks_[slot] = {};
                fills_[slot].clear();
                texts_[slot].clear();
            }
    armed_ = true;
    ++w.presentation_epoch;
    w.presentation_ready = 0;
    native_ready_ = false;
}
void HostPicture::install(emu::Wsc &w, Screens screens) {
    subscriptions_.clear();
    world_cards_.attach(w);
    title_menu_.attach(w);
    ending_notice_.attach(w);
    intro_.attach(w);
    screens_ = screens;
    generation_ = Session::of(w).generation();
    scenes_.fill(-1);
    for (size_t slot = 0; slot < last_.size(); ++slot)
        last_[slot] = w.framebuffer_rgb(slot >= 2 ? emu::Wsc::ColorMode::Lcd : emu::Wsc::ColorMode::Raw,
                                        bool(slot & 1));
    composed_ = {};
    masks_ = {};
    for (auto &fill : fills_)
        fill.clear();
    for (auto &text : texts_)
        text.clear();
    armed_ = holding_ = native_ready_ = false;
    fades_ = shown_ = fade_peak_ = defeat_effects_ = warps_ = 0;
    w.presentation_epoch = w.presentation_ready = w.presentation_shadow = w.presentation_oam = 0;
    auto &h = Session::of(w).hooks();
    subscriptions_.push_back(h.subscribe(hooks::Event::BattleDefeatEffect, [this](Machine &m, const auto &a) {
        sync_generation(m.wsc());
        if (a.a <= 1)
            defeat_effects_ = std::max(0, defeat_effects_ + (a.a ? 1 : -1));
    }));
    subscriptions_.push_back(h.subscribe(hooks::Event::TravelWarpEffect, [this](Machine &m, const auto &a) {
        sync_generation(m.wsc());
        warps_ = std::max(0, warps_ + (a.a ? 1 : -1));
    }));
    for (auto e :
         {hooks::Event::InventoryMenu, hooks::Event::PartyMenu, hooks::Event::ItemDialog,
          hooks::Event::ItemMessage, hooks::Event::CreationMenu, hooks::Event::NameEntry,
          hooks::Event::Bestiary, hooks::Event::Dialogue, hooks::Event::ServiceMenu, hooks::Event::SaveMenu,
          hooks::Event::BattleMenu, hooks::Event::MeatMenu, hooks::Event::BattleStart,
          hooks::Event::BattleEnd, hooks::Event::BattleRewards, hooks::Event::BattleRoundStart}) {
        subscriptions_.push_back(h.subscribe(e, [this, e](Machine &m, const auto &a) {
            if (e == hooks::Event::Dialogue && a.a > 1)
                return; // glyph pacing does not transfer ownership
            sync_generation(m.wsc());
            auto &scene = scenes_[size_t(e)];
            if (scene != a.a || a.b != 0 || e == hooks::Event::BattleEnd ||
                e == hooks::Event::BattleRoundStart) {
                scene = a.a;
                invalidate(m.wsc(), true);
            }
        }));
    }
    subscriptions_.push_back(
        h.subscribe(hooks::Event::PresentationPicture, [this](Machine &m, const auto &a) {
            auto &w = m.wsc();
            sync_generation(w);
            if (a.a == 3) {
                if (!fades_ && (shown_ > 0 || owner() > 0)) {
                    fade_peak_ = std::max(1, palette_peak(w));
                    fade_epoch_ = w.presentation_epoch;
                }
                ++fades_;
                return;
            }
            if (a.a == 4) {
                fades_ = std::max(0, fades_ - 1);
                return;
            }
            if (a.a == 0) {
                if (!fades_)
                    invalidate(w);
            } else if (armed_ && (a.a == 2 || !fades_)) {
                w.presentation_ready = w.presentation_epoch;
                native_ready_ = a.a == 2;
            }
        }));
}
void HostPicture::sync_generation(emu::Wsc &w) {
    if (generation_ != Session::of(w).generation()) {
        generation_ = Session::of(w).generation();
        // Native main can emit input observations before the app's per-frame screen updates. Retire
        // the previous run's owners now, or those observations rearm a hold over the new native run.
        if (screens_.battle)
            screens_.battle->frame(w);
        if (screens_.inventory)
            screens_.inventory->frame(w);
        if (screens_.item)
            screens_.item->frame(w);
        if (screens_.party)
            screens_.party->frame(w);
        if (screens_.creation)
            screens_.creation->frame(w);
        if (screens_.bestiary)
            screens_.bestiary->frame(w);
        if (screens_.dialogue)
            screens_.dialogue->frame(w);
        if (screens_.service)
            screens_.service->frame(w);
        if (screens_.save)
            screens_.save->frame(w);
        for (size_t slot = 0; slot < last_.size(); ++slot)
            last_[slot] = w.framebuffer_rgb(slot >= 2 ? emu::Wsc::ColorMode::Lcd : emu::Wsc::ColorMode::Raw,
                                            bool(slot & 1));
        composed_ = {};
        masks_ = {};
        for (auto &fill : fills_)
            fill.clear();
        for (auto &text : texts_)
            text.clear();
        scenes_.fill(-1);
        armed_ = holding_ = native_ready_ = false;
        fades_ = shown_ = fade_peak_ = defeat_effects_ = warps_ = 0;
        w.presentation_epoch = w.presentation_ready = w.presentation_shadow = w.presentation_oam = 0;
        w.presentation_raster.fill(0);
    }
}
void HostPicture::frame(emu::Wsc &w) {
    sync_generation(w);
    w.presentation_shadow = w.presentation_epoch;
    if (!fades_ && fade_peak_ && palette_peak(w) >= fade_peak_)
        fade_peak_ = 0;
}
std::vector<uint32_t> HostPicture::picture(emu::Wsc &w, emu::Wsc::ColorMode mode, bool second,
                                           bool lettering) {
    return picture_impl(w, mode, second, lettering, nullptr);
}
// A handoff holds layout and host chrome. Palette-only native updates still belong to that
// battlefield. Require identical raster provenance everywhere outside host UI before recoloring;
// a partial upload or a moving sprite must wait for the normal complete-picture submission.
// A native defeat effect explicitly owns each raster, including changed geometry. Publish its
// scene pixels on that clock while retaining the host UI and its independently drawn overlays.
void HostPicture::refresh_battle_scene(emu::Wsc &w, emu::Wsc::ColorMode mode, size_t slot) {
    // Raster defeats repurpose SCR2 for scene rows. The window-free pass deliberately omits
    // that plane, so only the ordinary completed raster contains the whole moving picture.
    const bool scene = !defeat_effects_;
    const auto *layers = w.frame_layers(bool(slot & 1), scene);
    // The app captures only WSC provenance. Explicit raster ownership needs its completed rows,
    // not an unchanged GB layout, so it can publish the GB endpoint without a second capture pass.
    if (defeat_effects_ && !layers)
        layers = w.frame_layers();
    auto &source = native_scene_[slot];
    auto &colors = native_colors_[slot];
    if (shown_ != 1 || owner() != 1 || !screens_.battle || !screens_.battle->live_palette(w) || !layers ||
        colors.size() != last_[slot].size() || (!defeat_effects_ && source.size() != last_[slot].size()))
        return;
    for (size_t at = 0; at < colors.size(); ++at) {
        if (masks_[slot][at])
            continue;
        if (!layers->valid[at / 224])
            return;
        if (!defeat_effects_) {
            const auto &a = source[at], &b = layers->top(at);
            if (a.tile != b.tile || a.cell != b.cell || a.palette != b.palette || a.index != b.index ||
                a.sprite != b.sprite || a.flags != b.flags || a.x != b.x || a.y != b.y)
                return;
        }
    }
    const auto current = w.framebuffer_rgb(mode, bool(slot & 1), scene);
    bool changed = false;
    for (size_t at = 0; at < colors.size(); ++at) {
        if (!masks_[slot][at] && last_[slot][at] == colors[at] && current[at] != colors[at]) {
            last_[slot][at] = current[at];
            changed = true;
        }
        colors[at] = current[at];
    }
    // Scene consumers must observe the newly published palette, including its restoration.
    if (changed || defeat_effects_)
        holding_ = false;
}
std::vector<uint32_t> HostPicture::picture_impl(emu::Wsc &w, emu::Wsc::ColorMode mode, bool second,
                                                bool lettering, std::array<bool, 4> *retained) {
    // Host decisions are complete pictures even though the machine is parked inside a frame.
    if (w.host_paused && screens_.battle && Session::of(w).esper_change()) {
        // Keep it as this endpoint's last complete picture: after the answer the native side takes a
        // few frames to publish again, and the held picture must be this prompt, never the native
        // screen underneath (a 1-2 frame flash).
        const size_t slot = (mode == emu::Wsc::ColorMode::Lcd ? 2 : 0) + size_t(second);
        auto *coverage = w.ui_coverage;
        auto *fill = w.ui_fill;
        auto *text = w.ui_text;
        masks_[slot] = {};
        fills_[slot].clear();
        texts_[slot].clear();
        w.ui_coverage = &masks_[slot];
        w.ui_fill = &fills_[slot];
        w.ui_text = &texts_[slot];
        last_[slot] = screens_.battle->picture(w, mode, second);
        composed_[slot] = w.frame_count;
        w.ui_coverage = coverage;
        w.ui_fill = fill;
        w.ui_text = text;
        if (coverage)
            *coverage = masks_[slot];
        if (fill)
            *fill = fills_[slot];
        if (text)
            *text = texts_[slot];
        return last_[slot];
    }
    frame(w);
    if (second)
        w.refresh_second(); // not drawn while nothing showed it (emu::Wsc::second_visible)
    // A native world card owns the whole picture, including a pending Continue
    // handoff. English replaces SCR2 kanji with the PNG; Japanese keeps the native picture.
    if (!second && world_cards_.active(w)) {
        holding_ = false;
        if (w.ui_coverage)
            *w.ui_coverage = {};
        if (w.ui_fill)
            w.ui_fill->clear();
        if (w.ui_text)
            w.ui_text->clear();
        auto px = w.framebuffer_rgb(mode, false, false, Session::of(w).text().proportional());
        if (lettering)
            world_cards_.composite(w, px);
        return px;
    }
    // The ending's notice owns the whole picture on the native clock, like the opening (no held frame).
    if (ending_notice_.active(w)) {
        holding_ = false;
        auto px = w.framebuffer_rgb(mode, second);
        ending_notice_.composite(w, px, mode, second);
        return px;
    }
    if (!second && intro_.active(w)) {
        holding_ = false;
        auto px = w.framebuffer_rgb(mode, false, false, true);
        intro_.composite(w, px, mode);
        return px;
    }
    const int next = owner();
    const size_t slot = (mode == emu::Wsc::ColorMode::Lcd ? 2 : 0) + size_t(second);
    // GB opening/world-entry pages are complete pictures on the native clock.
    // A pending menu handoff must not retain old pixels or paint chrome over them.
    if (second && w.second_scene_above_host && !w.second_scene_image.empty()) {
        holding_ = false;
        auto px = w.framebuffer_rgb(mode, true);
        last_[slot] = px;
        composed_[slot] = w.frame_count;
        masks_[slot] = {};
        fills_[slot].clear();
        texts_[slot].clear();
        if (w.ui_coverage)
            *w.ui_coverage = {};
        if (w.ui_fill)
            w.ui_fill->clear();
        if (w.ui_text)
            w.ui_text->clear();
        return px;
    }
    // clear_field_overlay may arm a dialogue-close handoff even on the first battle after a load.
    // The following native wipe publishes each raster without keypad polls; retaining the field hides
    // the entire transition. Release that handoff and retain the wipe frames for the battle's own open.
    if (battle_entry_wipe(w)) {
        armed_ = native_ready_ = false;
        w.presentation_epoch = w.presentation_ready = 0;
    }
    const bool complete =
        !armed_ || (w.presentation_ready == w.presentation_epoch &&
                    std::all_of(w.presentation_raster.begin(), w.presentation_raster.end(),
                                [&](uint64_t epoch) { return epoch == w.presentation_epoch; }) &&
                    (!native_ready_ || next == 0 || next == 5));
    holding_ = !complete && !last_[slot].empty();
    // Native full-page owners may have published only one endpoint. Keep their last complete picture
    // until the native handoff finishes; host-owned screens retain both endpoints together below.
    const size_t other = slot ^ 1;
    if (holding_ && composed_[other] > composed_[slot] && !last_[other].empty()) {
        if (w.ui_coverage)
            *w.ui_coverage = masks_[other];
        if (w.ui_fill)
            *w.ui_fill = fills_[other];
        if (w.ui_text)
            *w.ui_text = texts_[other];
        return fade_picture(w, last_[other], shown_ > 0);
    }
    if (holding_) {
        refresh_battle_scene(w, mode, slot);
        if (w.ui_coverage)
            *w.ui_coverage = masks_[slot];
        if (w.ui_fill)
            *w.ui_fill = fills_[slot];
        if (w.ui_text)
            *w.ui_text = texts_[slot];
        auto px = last_[slot];
        if (second && shown_ == 1)
            w.move_second_picture(px);
        return fade_picture(w, std::move(px), shown_ > 0);
    }
    // A first picture has no predecessor; keep the native picture until a host commit.
    if (!complete)
        return w.framebuffer_rgb(mode, second);
    // A committed destination ends the previous screen's black wait. Until then the
    // palette remains authoritative even after PresentationPicture 4 ends the fade scope.
    if (!fades_ && (w.presentation_epoch != fade_epoch_ || next != shown_))
        fade_peak_ = 0;
    // Retain both endpoints while the same host state still owns the picture. After a close
    // notification that state is gone; replaying the shown endpoint's pixels cannot recover its chrome.
    auto *coverage = w.ui_coverage;
    auto *fill = w.ui_fill;
    auto *text = w.ui_text;
    const auto retain = [&](size_t endpoint) {
        if (!presentation_diagnostics::fresh() && retained && (*retained)[endpoint])
            return;
        CpuScope cpu_scope(endpoint & 1 ? "retain GB" : "retain WSC", w.frame_count);
        masks_[endpoint] = {};
        fills_[endpoint].clear();
        texts_[endpoint].clear();
        w.ui_coverage = &masks_[endpoint];
        w.ui_fill = &fills_[endpoint];
        w.ui_text = &texts_[endpoint];
        const bool gb = bool(endpoint & 1);
        if (gb)
            w.refresh_second();
        last_[endpoint] = compose(w, mode, gb, next);
        native_scene_[endpoint].clear();
        if (next == 1) {
            native_colors_[endpoint] = w.framebuffer_rgb(mode, gb, true);
            if (const auto *layers = w.frame_layers(gb, true)) {
                auto &source = native_scene_[endpoint];
                source.resize(last_[endpoint].size());
                for (size_t at = 0; at < source.size(); ++at)
                    source[at] = layers->top(at);
            }
        }
        composed_[endpoint] = w.frame_count;
        if (retained)
            (*retained)[endpoint] = true;
    };
    if (next > 0 && w.second())
        retain(other);
    retain(slot);
    w.ui_coverage = coverage;
    w.ui_fill = fill;
    w.ui_text = text;
    if (coverage)
        *coverage = masks_[slot];
    if (fill)
        *fill = fills_[slot];
    if (text) {
        CpuScope cpu_scope("copy retained text", w.frame_count);
        *text = texts_[slot];
    }
    auto px = last_[slot];
    shown_ = next;
    if (next == 0) {
        armed_ = false;
        w.presentation_epoch = w.presentation_ready = 0;
    }
    if (second && next == 1)
        w.move_second_picture(px);
    return fade_picture(w, std::move(px), next > 0);
}
std::vector<uint32_t> HostPicture::compose(emu::Wsc &w, emu::Wsc::ColorMode mode, bool second, int next) {
    CpuScope cpu_scope(next ? (second ? "compose host GB" : "compose host WSC")
                            : (second ? "compose native GB" : "compose native WSC"),
                       w.frame_count);
    const auto &s = screens_;
    auto px = next == 2   ? s.item->picture(w, mode, second)
              : next == 3 ? s.save->picture(w, mode, second)
              : next == 4 ? s.service->picture(w, mode, second)
              : next == 5 ? s.dialogue->picture(w, mode, second)
              : next == 6 ? s.creation->picture(w, mode, second)
              : next == 7 ? s.bestiary->picture(w, mode, second)
              : next == 8 ? s.party->picture(w, mode, second)
              : next == 9 ? s.inventory->picture(w, mode, second)
              : next == 1 ? s.battle->picture(w, mode, second)
                          : w.framebuffer_rgb(mode, second);
    draw_enemy_hp_boxes(w, px, mode, second);
    title_menu_.composite(w, px, mode, second,
                          title_step_hint_ ? title_step_hint_() : TitleMenuLettering::StepHint{});
    return px;
}

} // namespace sawgod::game
