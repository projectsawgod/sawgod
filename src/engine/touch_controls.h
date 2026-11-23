// Fixed host touch chrome and finger ownership; no game memory or binding shortcuts.
#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <cstdint>
#include <map>

namespace sawgod::ui {
enum class TouchControl { Options, Mode, Start, Y, X, A, B, Count };
constexpr bool touch_menu_control(TouchControl c) {
    return c == TouchControl::Options || c == TouchControl::X || c == TouchControl::A || c == TouchControl::B;
}
struct TouchArea {
    float x = 0, y = 0, w = 0, h = 0;
    float hit_radius = 0; // Zero uses the usual 20% radius extension.
    bool contains(float px, float py) const;
};
struct TouchLayout {
    std::array<TouchArea, size_t(TouchControl::Count)> areas{};
    float scale = 1;
    static TouchLayout make(float width, float height, float picture_width, SDL_FRect safe,
                            float minimum_scale = 0);
};
class TouchState {
  public:
    TouchLayout layout;
    // -1: center or diagonal dead zone; 0 up, 1 right, 2 down, 3 left.
    static int direction(float dx, float dy, float radius);
    void down(SDL_FingerID id, float x, float y, bool menu_open = false);
    void move(SDL_FingerID id, float x, float y);
    void up(SDL_FingerID id) { fingers_.erase(id); }
    bool owns(SDL_FingerID id) const { return fingers_.contains(id); }
    void release() {
        fingers_.clear();
        options_ = mode_ = false;
    }
    bool held(TouchControl c, int direction = -1) const;
    bool take_options() {
        bool v = options_;
        options_ = false;
        return v;
    }
    bool take_mode() {
        bool v = mode_;
        mode_ = false;
        return v;
    }

  private:
    struct Finger {
        TouchControl control;
        float x, y;
    };
    std::map<SDL_FingerID, Finger> fingers_;
    bool options_ = false, mode_ = false;
};

class TouchControls {
  public:
    ~TouchControls();
    bool init();
    void shutdown();
    SDL_JoystickID id() const { return id_; }
    void release_renderer();
    void layout(SDL_Window *window, SDL_Renderer *renderer, float width, float height, float picture_width);
    void show(bool visible, bool menu_open = false);
    bool shown(TouchControl c) const { return visible_ && (!menu_open_ || touch_menu_control(c)); }
    // True when the overlay owns this pointer event; do not also tap the menu behind it.
    bool event(const SDL_Event &event, SDL_Window *window, bool mouse);
    void render(SDL_Renderer *renderer);
    bool take_options() { return state_.take_options(); }
    bool take_mode() { return state_.take_mode(); }

  private:
    void sync();
    TouchState state_;
    SDL_JoystickID id_ = 0;
    SDL_Joystick *joystick_ = nullptr;
    bool visible_ = false;
    bool menu_open_ = false;
    float width_ = 1, height_ = 1;
    SDL_Texture *texture_ = nullptr;
};
} // namespace sawgod::ui
