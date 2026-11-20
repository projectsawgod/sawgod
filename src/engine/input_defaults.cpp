#include "emu/wsc.h"
#include "engine/input.h"
#include <algorithm>

namespace sawgod::ui {
uint16_t wsc_key_bit(WscButton b) {
    using namespace sawgod::emu;
    static constexpr uint16_t kBits[kWscButtonCount] = {KEY_X1, KEY_X2, KEY_X3, KEY_X4, KEY_Y1,    KEY_Y2,
                                                        KEY_Y3, KEY_Y4, KEY_A,  KEY_B,  KEY_START, 0,
                                                        0,      0,      0,      0,      0};
    return kBits[int(b)];
}

const char *wsc_button_id(WscButton b) {
    static constexpr const char *kIds[kWscButtonCount] = {
        "x1",          "x2",      "x3",           "x4",         "y1",
        "y2",          "y3",      "y4",           "a",          "b",
        "start",       "options", "fast_forward", "encounters", "fidelity_down",
        "fidelity_up", "minimap"};
    return kIds[int(b)];
}

const char *wsc_button_label(WscButton b) {
    static constexpr const char *kLabels[kWscButtonCount] = {"X1",
                                                             "X2",
                                                             "X3",
                                                             "X4",
                                                             "Y1",
                                                             "Y2",
                                                             "Y3",
                                                             "Y4",
                                                             "A",
                                                             "B",
                                                             "Start",
                                                             "Options",
                                                             "Fast Forward",
                                                             "Encounters On/Off",
                                                             "Mode Down",
                                                             "Mode Up",
                                                             "Toggle Minimap"};
    return kLabels[int(b)];
}

const char *wsc_button_help(WscButton b) {
    static constexpr const char *kHelp[kWscButtonCount] = {
        "X pad up",   "X pad right", "X pad down",    "X pad left",  "Y pad up",    "Y pad right",
        "Y pad down", "Y pad left",  "A button",      "B button",    "Start",       "Options menu",
        "Speed up",   "Encounters",  "Fidelity down", "Fidelity up", "Minimap size"};
    return kHelp[int(b)];
}

InputMap InputMap::defaults() {
    using K = Binding::Kind;
    InputMap m;
    auto set = [&](WscButton b, std::initializer_list<Binding> keys, std::initializer_list<Binding> pad) {
        std::copy(keys.begin(), keys.end(), m.keys[int(b)].begin());
        std::copy(pad.begin(), pad.end(), m.pad[int(b)].begin());
    };
    set(WscButton::X1, {{K::Key, SDL_SCANCODE_UP}},
        {{K::PadButton, SDL_GAMEPAD_BUTTON_DPAD_UP}, {K::PadAxis, SDL_GAMEPAD_AXIS_LEFTY, -1}});
    set(WscButton::X2, {{K::Key, SDL_SCANCODE_RIGHT}},
        {{K::PadButton, SDL_GAMEPAD_BUTTON_DPAD_RIGHT}, {K::PadAxis, SDL_GAMEPAD_AXIS_LEFTX, +1}});
    set(WscButton::X3, {{K::Key, SDL_SCANCODE_DOWN}},
        {{K::PadButton, SDL_GAMEPAD_BUTTON_DPAD_DOWN}, {K::PadAxis, SDL_GAMEPAD_AXIS_LEFTY, +1}});
    set(WscButton::X4, {{K::Key, SDL_SCANCODE_LEFT}},
        {{K::PadButton, SDL_GAMEPAD_BUTTON_DPAD_LEFT}, {K::PadAxis, SDL_GAMEPAD_AXIS_LEFTX, -1}});
    set(WscButton::Y1, {{K::Key, SDL_SCANCODE_W}}, {{K::PadAxis, SDL_GAMEPAD_AXIS_RIGHTY, -1}});
    set(WscButton::Y2, {{K::Key, SDL_SCANCODE_D}}, {{K::PadAxis, SDL_GAMEPAD_AXIS_RIGHTX, +1}});
    set(WscButton::Y3, {{K::Key, SDL_SCANCODE_S}}, {{K::PadAxis, SDL_GAMEPAD_AXIS_RIGHTY, +1}});
    set(WscButton::Y4, {{K::Key, SDL_SCANCODE_A}}, {{K::PadAxis, SDL_GAMEPAD_AXIS_RIGHTX, -1}});
    set(WscButton::A, {{K::Key, SDL_SCANCODE_X}}, {{K::PadButton, SDL_GAMEPAD_BUTTON_SOUTH}});
    set(WscButton::B, {{K::Key, SDL_SCANCODE_Z}}, {{K::PadButton, SDL_GAMEPAD_BUTTON_EAST}});
    set(WscButton::Start, {{K::Key, SDL_SCANCODE_RETURN}},
        {{K::PadButton, SDL_GAMEPAD_BUTTON_START}, {K::PadButton, SDL_GAMEPAD_BUTTON_NORTH}});
    set(WscButton::Options, {{K::Key, SDL_SCANCODE_ESCAPE}, {K::Key, SDL_SCANCODE_F1}},
        {{K::PadButton, SDL_GAMEPAD_BUTTON_BACK}});
    set(WscButton::FastForward, {{K::Key, SDL_SCANCODE_TAB}},
        {{K::PadAxis, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, +1}});
    // Stick clicks: Encounters on L3, Minimap on R3; west and L2 are free.
    set(WscButton::Encounters, {{K::Key, SDL_SCANCODE_C}}, {{K::PadButton, SDL_GAMEPAD_BUTTON_LEFT_STICK}});
    // Keyboard bindings stay unchanged.
    set(WscButton::Minimap, {{K::Key, SDL_SCANCODE_M}}, {{K::PadButton, SDL_GAMEPAD_BUTTON_RIGHT_STICK}});
    if (kGfxSwapBuild) {
        set(WscButton::FidelityDown, {{K::Key, SDL_SCANCODE_G}},
            {{K::PadButton, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER}});
        set(WscButton::FidelityUp, {{K::Key, SDL_SCANCODE_H}},
            {{K::PadButton, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER}});
    }
    return m;
}

} // namespace sawgod::ui
