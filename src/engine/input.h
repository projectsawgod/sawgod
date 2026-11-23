// Keyboard and gamepad input for the front end: the binding table from WSC buttons to physical inputs
// (InputMap, rebindable), the open gamepads (SDL3 gamepad API with SDL's built-in
// community controller database), and the options overlay's menu actions.
#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "engine/button_icons.h"
#include "engine/config.h"
#include "engine/touch_controls.h"
#include <functional>

namespace sawgod::ui {

// The WSC's buttons, in sawgod::emu::Key bit order is not assumed: wsc_key_bit() maps them. Options is the
// port's own: the button that opens the options menu (no WSC key bit; a device's last binding for it can't
// be removed).
// FastForward is the port's too: held, the game runs at the Fast-Forward speed
// (Gameplay); Encounters switches random battles off and
// back on (Cheats > Encounters); FidelityDown/Up step the unlocked graphics and music tracks; Minimap
// cycles the world-map minimap Off -> Small -> Large (read only while the party walks a world map:
// app/minimap.h).
enum class WscButton {
    X1,
    X2,
    X3,
    X4,
    Y1,
    Y2,
    Y3,
    Y4,
    A,
    B,
    Start,
    Options,
    FastForward,
    Encounters,
    FidelityDown,
    FidelityUp,
    Minimap,
    Count
};
// Fidelity actions exist (and their bindings load and save) in every build, but are shown/bound only in
// builds with SAWGOD_GFX_SWAP (on by default).
#ifdef SAWGOD_GFX_SWAP
inline constexpr bool kGfxSwapBuild = true;
#else
inline constexpr bool kGfxSwapBuild = false;
#endif
inline constexpr int kWscButtonCount = int(WscButton::Count);
uint16_t wsc_key_bit(WscButton b); // the sawgod::emu::Key bit

struct Binding {
    enum class Kind {
        None,      // an empty slot
        Key,       // code = SDL_Scancode
        PadButton, // code = SDL_GamepadButton (by position: SDL_GAMEPAD_BUTTON_EAST is the right face button)
        PadAxis,   // code = SDL_GamepadAxis, dir = -1 or +1: past the deadzone in that direction
    };
    Kind kind = Kind::None;
    int code = 0;
    int dir = 0;
    bool empty() const { return kind == Kind::None; }
    bool is_pad() const { return kind == Kind::PadButton || kind == Kind::PadAxis; }
    bool operator==(const Binding &o) const {
        return kind == o.kind && (kind == Kind::None || (code == o.code && dir == o.dir));
    }
};

// A stick counts as a d-pad press past this much of full tilt.
inline constexpr int kStickDeadzone = 40; // percent

// Each WSC button has kBindSlots keyboard slots and kBindSlots gamepad slots (rebindable in the
// options menu's Keyboard and Gamepad pages, saved in sawgod.toml's [keyboard] and [gamepad] tables,
// docs/CONFIG.md).
inline constexpr int kBindSlots = 2;
using BindSlots = std::array<Binding, kBindSlots>;

// The WSC button's name in the file ("x1".."y4", "a", "b", "start"), in the menu ("X1", "Start") and its
// one-line description ("X pad up").
const char *wsc_button_id(WscButton b);
const char *wsc_button_label(WscButton b);
const char *wsc_button_help(WscButton b);

// Which physical inputs press each WSC button. defaults(): arrows / D-pad + left stick = X pad, WASD / right
// stick = Y pad, X / the pad's south face button = A, Z / the
// east face button = B (south accepts, east cancels, as players expect), Enter / Start or north = Start;
// Options: Esc and F1 / the pad's Select button, rebindable like the rest, except that each device always
// keeps at least one binding for it, so nobody is locked out of the menu. G / L1 = Mode Down; H / R1 = Mode
// Up; C / L3 = Encounters; M / R3 = Minimap; L2 is unbound. Menu directions and keyboard keys are fixed;
// gamepad Accept/Cancel follow the effective A/B slots (with recovery for unusable legacy slots).
struct InputMap {
    std::array<BindSlots, kWscButtonCount> keys{}, pad{};
    static InputMap defaults();

    BindSlots &slots(WscButton b, bool gamepad) { return (gamepad ? pad : keys)[int(b)]; }
    const BindSlots &slots(WscButton b, bool gamepad) const { return (gamepad ? pad : keys)[int(b)]; }
    // Resolve the gamepad swap without rewriting either stored slot. Keyboard slots never swap.
    WscButton effective_button(WscButton b, bool gamepad) const;
    const BindSlots &effective_slots(WscButton b, bool gamepad) const {
        return slots(effective_button(b, gamepad), gamepad);
    }
    // Options requires distinct, non-directional buttons for A, B and Open Options. Unusable legacy
    // slots keep their gameplay meaning; these menu-only controls recover deterministically without
    // overwriting them. Directions remain available for reaching Reset Bindings.
    BindSlots menu_slots(WscButton b) const;
    bool has_menu_button(WscButton b) const;
    // Puts `bind` in that slot. A conflict -- another slot of that device already has it -- is a swap:
    // that slot gets this slot's old binding (maybe empty). Returns the button that gave it up, or Count.
    // Refused (nothing changes, *refused set): a stick or trigger for Options, or a change that would
    // leave that device without Options, or remove its last usable gamepad A/B menu button.
    WscButton assign(WscButton b, int slot, Binding bind, bool *refused = nullptr);
    // Empties the slot; false for the last Options key or usable gamepad A/B/Options button.
    bool clear(WscButton b, bool gamepad, int slot);
    // Whether that device has a binding for Options (always true for a map made by these functions).
    bool opens_options(bool gamepad) const;
    void reset(bool gamepad); // that device's bindings back to the defaults
    bool operator==(const InputMap &o) const { return keys == o.keys && pad == o.pad; }

    // sawgod.toml: [keyboard] / [gamepad] tables, `x1 = ["Up"]`; a button not listed keeps its default
    // and only buttons that differ from the defaults are written. Bad names are reported and skipped.
    void load(Config &config, std::vector<std::string> *warnings = nullptr);
    void store(Config &config) const;

  private:
    const CVar *swap_ab_ = nullptr; // Config owns it; load links the live setting (absent means Off).
    bool menu_edit_valid(const InputMap &before) const;
};

// A binding's name in the file: the SDL scancode name for keys ("Up", "Return"), our own position names for
// the pad ("east", "dpad_up", "lb", "lstick_left", "lt"), and back.
std::string binding_id(const Binding &b);
std::optional<Binding> binding_from_id(const std::string &id, bool gamepad);
// A binding's short name in the menu (<= 8 characters where possible), with this kind of pad's own names.
std::string binding_label(const Binding &b, SDL_GamepadType type);
// A binding's icon (engine/button_icons.h): a key as its keycap, a pad button or stick in the Icon Type's
// family (input.pad_icons resolved: 1 Xbox, 2 PS, 3 Switch), by position like the bindings themselves. Empty
// when there is none (guide, paddles, touchpad, the extra buttons, keys outside the lettering): the label
// shows instead.
ButtonIcon binding_icon(const Binding &b, int pad_icons);
// The Icon Type to draw: the setting (1 Xbox, 2 PS, 3 Switch), or for Auto (0) the connected pad's family, X
// without one.
int resolve_pad_icons(int setting, SDL_GamepadType type);
// The same as a name for an {icon:NAME} text token ("pad A", "key:W"), or "".
std::string binding_icon_name(const Binding &b, int pad_icons);
// First bound control on this device as an inline icon token, or its label when no icon exists.
// Shared by Options help and the title's step hint. No cross-device fallback.
std::string bound_button_text(const InputMap &map, WscButton button, bool pad, int pad_icons,
                              SDL_GamepadType type);
// The WSC's own button as its badge ("WSC X1"); nullptr for the port's buttons (Options, Fast-Forward...).
const ButtonIcon *wsc_button_icon(WscButton b);

// One menu action per press (with key repeat for the directions).
struct MenuActions {
    bool up = false, down = false, left = false, right = false;
    bool accept = false, back = false;
    bool prev_page = false, next_page = false;
    bool clear = false;  // clear a binding slot (Delete, the pad's north face button)
    bool toggle = false; // open/close the overlay (Options: Esc, the pad's Select button, or its 2nd slot)
};

class Input {
  public:
#ifdef __ANDROID__
    static constexpr bool kAndroid = true;
#else
    static constexpr bool kAndroid = false;
#endif
    explicit Input(SDL_JoystickID virtual_id = 0);
    ~Input();
    bool real_controller_connected() const;
    // Opens/closes gamepads as they come and go; collects menu actions from key and button presses.
    void handle_event(const SDL_Event &e);
    // Fixed host controls (touch Options and Android Back) share the binding's capture cancellation.
    void press_options();
    // The WSC buttons held now, as sawgod::emu::Key bits.
    uint16_t held(const InputMap &map) const;
    // Whether one button (e.g. FastForward, which has no WSC key bit) is held now.
    bool button_held(const InputMap &map, WscButton b) const;
    // Menu actions since the last call (events plus held-direction repeat from pads), then cleared.
    MenuActions take_menu_actions(double now_s);

    int gamepad_count() const;
    bool has_gamepad(SDL_JoystickID id) const;
    std::string gamepad_name() const;     // preferred real pad's name, or ""
    SDL_GamepadType gamepad_type() const; // preferred real pad's type, or SDL_GAMEPAD_TYPE_UNKNOWN
    // Whether the player last pressed something on a gamepad (else the keyboard): the device whose buttons
    // the text's button icons show.
    bool last_used_pad() const { return last_pad_; }

    // Rebinding: the next key (or pad button / stick or trigger push) becomes a binding instead of a menu
    // action. Whatever opens Options (on either device) cancels it.
    void start_capture(bool gamepad);
    // The bindings (for Options' rebindable slot); without one, the defaults.
    void set_map(const InputMap *map) { map_ = map; }
    // Whether the options menu is open (the overlay says): then its own keys win over an Options binding.
    void set_menu_open(bool open) { menu_open_ = open; }
    // Mapping changes take effect on the next press, after controls already held are released.
    void suppress_gamepad_until_release();
    void stop_capture() { capture_ = Capture::Off; }
    bool capturing() const { return capture_ != Capture::Off; }
    // The captured binding once there is one (capture ends), or nullopt; *cancelled when it was cancelled.
    std::optional<Binding> take_captured(bool *cancelled);

  private:
    SDL_JoystickID virtual_id_ = 0;
    SDL_JoystickID last_real_pad_ = 0;
    SDL_Gamepad *prompt_pad() const;
    std::vector<SDL_Gamepad *> pads_;
    const InputMap *map_ = nullptr;
    bool last_pad_ = false;
    MenuActions pending_;
    bool menu_open_ = false;
    bool is_options(const Binding &b) const;
    bool is_bound(WscButton button, const Binding &b) const;
    // gamepad direction repeat (keyboard repeat comes from the OS)
    int held_dir_ = -1; // 0 up 1 down 2 left 3 right
    double next_repeat_ = 0;

    enum class Capture { Off, Keys, Pad } capture_ = Capture::Off;
    std::optional<Binding> captured_;
    bool capture_cancelled_ = false;
    std::array<bool, SDL_GAMEPAD_AXIS_COUNT> axis_armed_{}; // an axis held at capture start must return first
    struct SuppressedInput {
        SDL_JoystickID pad;
        Binding control;
    };
    std::vector<SuppressedInput> suppressed_;
    bool pad_suppressed(SDL_JoystickID pad, const Binding &b) const;
    bool dir_latched_ = false; // after a capture: no pad direction repeat until released

    bool pad_pressed(const Binding &b) const;
    void end_capture(std::optional<Binding> b, bool cancelled);
};

// A gamepad button's name as printed on that kind of pad ("B" / "Circle" / "A" for the east face button on
// Xbox / PlayStation / Nintendo pads; "Options", "Share", "+"...), never a direction. Unknown or no pad:
// Xbox names.
std::string pad_button_name(SDL_GamepadType type, SDL_GamepadButton button);

} // namespace sawgod::ui
