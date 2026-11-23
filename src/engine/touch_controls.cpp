#include "engine/touch_controls.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace sawgod::ui {
bool TouchArea::contains(float px, float py) const {
    // Capsule expanded 20% in radius. Neighboring controls have disjoint hit areas.
    const float radius = hit_radius > 0 ? hit_radius : h * 0.6f;
    const float dx = std::max(0.0f, std::abs(px - x) - (w - h) * 0.5f);
    const float dy = py - y;
    return dx * dx + dy * dy <= radius * radius;
}
TouchLayout TouchLayout::make(float width, float height, float picture_width, SDL_FRect safe,
                              float minimum_scale) {
    TouchLayout l;
    l.scale = height / 1080.0f;
    const float s = l.scale;
    // Each side uses one reference scale, with physical floors for individual targets.
    // Top-row controls use 48 dp; gameplay targets use 57 dp (about 9 mm).
    const float minimum = minimum_scale > 0 ? minimum_scale : s * 57 / 120;
    const float top_minimum = minimum * 48 / 57;
    const float bar = std::max(0.0f, (width - picture_width) / 2);
    const float gap = 16 * s;
    const float left_width = std::max(0.f, bar - gap - safe.x);
    const float right_edge = safe.x + safe.w;
    const float right_width = std::max(0.f, right_edge - (width - bar + gap));
    auto pad_size = [&](float scale) { return std::max(320 * scale, 120 * minimum); };
    auto button_size = [&](float scale) { return std::max(172 * scale, 120 * minimum); };
    auto top_size = [&](float scale) { return std::max(120 * scale, 120 * top_minimum); };
    auto button_dx = [&](float scale) {
        return std::max(172 * scale, button_size(scale) * 1.2f * 172 / std::hypot(172.f, 170.f));
    };
    auto fit = [&](float available, auto required) {
        if (required(s) <= available)
            return s;
        if (required(0) > available)
            return 0.f; // Only minimum targets may cross the picture edge.
        float lo = 0, hi = s;
        for (int i = 0; i < 24; ++i) {
            const float mid = (lo + hi) / 2;
            (required(mid) <= available ? lo : hi) = mid;
        }
        return lo;
    };
    const float left_scale = fit(left_width, [&](float scale) {
        return std::max(pad_size(scale) * 1.2f,
                        top_size(scale) + std::max(160 * scale, top_size(scale)));
    });
    const float right_scale = fit(right_width, [&](float scale) {
        return std::max(button_size(scale) * 1.2f + button_dx(scale), top_size(scale) * 2.2f);
    });
    auto put = [&](TouchControl c, float x, float y, float w, float h, float radius = 0) {
        const float r = radius > 0 ? radius : h * .6f;
        const float bottom = std::max(safe.y + r, safe.y + safe.h - r);
        l.areas[size_t(c)] = {x, std::clamp(y, safe.y + r, bottom), w, h, radius};
    };
    // Prefer the reference centers, then translate the whole cluster into its usable bar.
    auto center = [](float preferred, float low, float high) {
        return std::clamp(preferred, low, std::max(low, high));
    };
    const float top = top_size(left_scale);
    const float spacing = std::max(160 * left_scale, top);
    // Sacrifice only the extra hit margin when two minimum top targets need the bar.
    const float top_radius =
        std::max(top / 2, std::min({top * .6f, spacing / 2, (left_width - spacing) / 2}));
    const float options_x = center(safe.x + 110 * left_scale, safe.x + top_radius,
                                  bar - gap - spacing - top_radius);
    const float top_y = safe.y + 130 * s;
    put(TouchControl::Options, options_x, top_y, top, top, top_radius);
    put(TouchControl::Mode, options_x + spacing, top_y, top, top, top_radius);
    const float start = top_size(right_scale);
    put(TouchControl::Start,
        center(right_edge - 180 * right_scale, width - bar + gap + start * 1.1f,
               right_edge - start * 1.1f), top_y, 2 * start, start);
    // The right edge wins when the minimum cluster is wider than its bar.
    auto &start_area = l.areas[size_t(TouchControl::Start)];
    start_area.x = std::min(start_area.x, right_edge - start * 1.1f);
    const float pad = pad_size(left_scale), pad_radius = pad * .6f;
    const float left = center(bar / 2, safe.x + pad_radius, bar - gap - pad_radius);
    float y_pad = safe.y + 415 * s;
    for (auto c : {TouchControl::Options, TouchControl::Mode}) {
        const auto a = l.areas[size_t(c)];
        const float dx = left - a.x, radii = pad_radius + a.hit_radius;
        if (std::abs(dx) < radii)
            y_pad = std::max(y_pad, a.y + std::sqrt(radii * radii - dx * dx));
    }
    put(TouchControl::Y, left, y_pad, pad, pad);
    put(TouchControl::X, left, std::max(safe.y + 800 * s, y_pad + 2 * pad_radius), pad, pad);
    const float button = button_size(right_scale), dx = button_dx(right_scale);
    const float right = std::min(right_edge - button * .6f,
                                std::max(width - bar / 2 + 92 * right_scale,
                                         width - bar + gap + dx + button * .6f));
    put(TouchControl::A, right, safe.y + 700 * s, button, button);
    put(TouchControl::B, right - dx, safe.y + 700 * s + std::max(170 * s, dx * 170 / 172),
        button, button);
    return l;
}
int TouchState::direction(float dx, float dy, float radius) {
    if (dx * dx + dy * dy < radius * radius * 0.18f * 0.18f)
        return -1;
    // 70-degree cardinal sectors, separated by 20-degree diagonal dead zones.
    if (std::abs(dx) < std::abs(dy) * 0.700208f)
        return dy < 0 ? 0 : 2;
    if (std::abs(dy) < std::abs(dx) * 0.700208f)
        return dx > 0 ? 1 : 3;
    return -1;
}
void TouchState::down(SDL_FingerID id, float x, float y, bool options_only) {
    if (fingers_.contains(id))
        return;
    for (size_t i = 0; i < layout.areas.size(); ++i)
        if ((!options_only || TouchControl(i) == TouchControl::Options) && layout.areas[i].contains(x, y)) {
            const auto c = TouchControl(i);
            fingers_[id] = {c, x, y};
            options_ |= c == TouchControl::Options;
            mode_ |= c == TouchControl::Mode;
            return;
        }
}
void TouchState::move(SDL_FingerID id, float x, float y) {
    if (auto it = fingers_.find(id); it != fingers_.end()) {
        it->second.x = x;
        it->second.y = y;
    }
}
bool TouchState::held(TouchControl c, int d) const {
    const auto &a = layout.areas[size_t(c)];
    for (const auto &[id, f] : fingers_) {
        (void)id;
        if (f.control == c &&
            ((c == TouchControl::X || c == TouchControl::Y) ? direction(f.x - a.x, f.y - a.y, a.h / 2) == d
                                                            : a.contains(f.x, f.y)))
            return true;
    }
    return false;
}
namespace {
const unsigned char kArt[] = {
#include "touch_art.inc"
};
} // namespace
bool TouchControls::init() {
    SDL_VirtualJoystickDesc desc{};
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.name = "SawGod touch controls";
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    id_ = SDL_AttachVirtualJoystick(&desc);
    if (id_)
        joystick_ = SDL_OpenJoystick(id_);
    return joystick_ != nullptr;
}
TouchControls::~TouchControls() { shutdown(); }
void TouchControls::shutdown() {
    show(false);
    release_renderer();
    if (joystick_)
        SDL_CloseJoystick(joystick_);
    if (id_)
        SDL_DetachVirtualJoystick(id_);
    joystick_ = nullptr;
    id_ = 0;
}
void TouchControls::release_renderer() {
    SDL_DestroyTexture(texture_);
    texture_ = nullptr;
}
void TouchControls::layout(SDL_Window *window, SDL_Renderer *renderer, float width, float height,
                           float picture_width) {
    width_ = width;
    height_ = height;
    SDL_Rect safe{};
    int ww = 1, wh = 1;
    SDL_GetWindowSize(window, &ww, &wh);
    if (!SDL_GetWindowSafeArea(window, &safe))
        safe = {0, 0, ww, wh};
    SDL_FRect area{float(safe.x) * width / float(ww), float(safe.y) * height / float(wh),
                   float(safe.w) * width / float(ww), float(safe.h) * height / float(wh)};
    float minimum = 0;
#ifdef __ANDROID__
    // 9 mm at Android's logical 160 dpi, converted to render-output pixels.
    minimum = 57 * SDL_GetWindowDisplayScale(window) * width / float(ww) / 120;
#elif !defined(NDEBUG)
    // Desktop reproduction of phone cutouts/density, in render-output pixels.
    if (const char *insets = std::getenv("SAWGOD_TOUCH_SAFE_INSETS")) {
        float left, top, right, bottom;
        if (std::sscanf(insets, "%f,%f,%f,%f", &left, &top, &right, &bottom) == 4 &&
            left >= 0 && top >= 0 && right >= 0 && bottom >= 0 &&
            left + right < width && top + bottom < height)
            area = {left, top, width - left - right, height - top - bottom};
    }
    if (const char *density = std::getenv("SAWGOD_TOUCH_DISPLAY_SCALE")) {
        const float scale = std::strtof(density, nullptr);
        if (std::isfinite(scale) && scale > 0)
            minimum = 57 * scale / 120;
    }
#endif
    state_.layout = TouchLayout::make(width, height, picture_width, area, minimum);
    (void)renderer;
}
void TouchControls::show(bool visible, bool options_only) {
    if (!visible || options_only_ != options_only) {
        state_.release();
        sync();
    }
    visible_ = visible;
    options_only_ = options_only;
}
void TouchControls::sync() {
    if (!joystick_)
        return;
    using C = TouchControl;
    const SDL_GamepadButton buttons[] = {SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
                                         SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT};
    for (int d = 0; d < 4; ++d)
        SDL_SetJoystickVirtualButton(joystick_, buttons[d], state_.held(C::X, d));
    SDL_SetJoystickVirtualAxis(joystick_, SDL_GAMEPAD_AXIS_RIGHTX,
                               Sint16((int(state_.held(C::Y, 1)) - int(state_.held(C::Y, 3))) * 32767));
    SDL_SetJoystickVirtualAxis(joystick_, SDL_GAMEPAD_AXIS_RIGHTY,
                               Sint16((int(state_.held(C::Y, 2)) - int(state_.held(C::Y, 0))) * 32767));
    SDL_SetJoystickVirtualButton(joystick_, SDL_GAMEPAD_BUTTON_SOUTH, state_.held(C::A));
    SDL_SetJoystickVirtualButton(joystick_, SDL_GAMEPAD_BUTTON_EAST, state_.held(C::B));
    SDL_SetJoystickVirtualButton(joystick_, SDL_GAMEPAD_BUTTON_START, state_.held(C::Start));
    SDL_UpdateJoysticks();
}
void TouchControls::event(const SDL_Event &e, SDL_Window *window, bool mouse) {
    if (!visible_)
        return;
    if (e.type == SDL_EVENT_FINGER_DOWN)
        state_.down(e.tfinger.fingerID, e.tfinger.x * width_, e.tfinger.y * height_, options_only_);
    else if (e.type == SDL_EVENT_FINGER_MOTION)
        state_.move(e.tfinger.fingerID, e.tfinger.x * width_, e.tfinger.y * height_);
    else if (e.type == SDL_EVENT_FINGER_UP || e.type == SDL_EVENT_FINGER_CANCELED)
        state_.up(e.tfinger.fingerID);
    else if (mouse && (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP ||
                       e.type == SDL_EVENT_MOUSE_MOTION)) {
        if ((e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.which : e.button.which) == SDL_TOUCH_MOUSEID)
            return;
        int ww = 1, wh = 1;
        SDL_GetWindowSize(window, &ww, &wh);
        const float x = (e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.x : e.button.x) * width_ / float(ww);
        const float y = (e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.y : e.button.y) * height_ / float(wh);
        constexpr SDL_FingerID mouse_id = -1;
        if (e.type == SDL_EVENT_MOUSE_MOTION)
            state_.move(mouse_id, x, y);
        else if (e.button.button == SDL_BUTTON_LEFT) {
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
                state_.down(mouse_id, x, y, options_only_);
            else
                state_.up(mouse_id);
        }
    }
    sync();
}
void TouchControls::render(SDL_Renderer *renderer) {
    if (!visible_)
        return;
    // One embedded atlas, generated once from the six licensed source sprites.
    if (!texture_) {
        SDL_Surface *surface = SDL_LoadPNG_IO(SDL_IOFromConstMem(kArt, sizeof kArt), true);
        if (!surface)
            return;
        texture_ = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_DestroySurface(surface);
        if (!texture_)
            return;
        SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
    }
    auto draw = [&](int art, float x, float y, float w, float h, float alpha) {
        SDL_FRect src{float((art % 4) * 512), float((art / 4) * 512), 512, 512};
        SDL_FRect dst{x - w / 2, y - h / 2, w, h};
        SDL_SetTextureAlphaModFloat(texture_, alpha);
        SDL_RenderTexture(renderer, texture_, &src, &dst);
    };
    for (size_t i = 0; i < state_.layout.areas.size(); ++i) {
        const auto c = TouchControl(i);
        if (!shown(c))
            continue;
        const auto &a = state_.layout.areas[i];
        const float opacity = c == TouchControl::Y ? 0.45f : 0.75f;
        const bool pad = c == TouchControl::X || c == TouchControl::Y;
        const float s = a.h / (pad ? 320 : c == TouchControl::A || c == TouchControl::B ? 172 : 120);
        draw(pad ? 0 : c == TouchControl::Start ? 2 : 1, a.x, a.y, a.w, a.h, opacity);
        if (pad)
            draw(c == TouchControl::Y ? 8 : 9, a.x - 110 * s, a.y - 110 * s, 80 * s, 80 * s, 215.0f / 255);
        else if (c == TouchControl::Start)
            draw(7, a.x, a.y + s, a.w, a.h, opacity);
        else {
            const int art = c == TouchControl::Options ? 3
                            : c == TouchControl::Mode  ? 6
                            : c == TouchControl::A     ? 4
                                                       : 5;
            const float icon_scale =
                a.h / (c == TouchControl::Options || c == TouchControl::Mode ? 120 : 172);
            const float size = (c == TouchControl::Options ? 56
                                : c == TouchControl::Mode  ? 66
                                                           : 70) *
                               icon_scale;
            draw(art, a.x, a.y - (c == TouchControl::Mode ? 2 * s : 0), size, size, opacity);
        }
    }
}
} // namespace sawgod::ui
