#include "engine/input.h"

#include <algorithm>
#include <cstdlib>

#include "emu/wsc.h" // sawgod::emu::Key (constants only)
#include "engine/debug_log.h"

namespace sawgod::ui {

namespace {
bool menu_button(const Binding &b) {
    return b.kind == Binding::Kind::PadButton && b.code != SDL_GAMEPAD_BUTTON_GUIDE &&
           !(b.code >= SDL_GAMEPAD_BUTTON_DPAD_UP && b.code <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
}
constexpr WscButton kMenuButtons[] = {WscButton::A, WscButton::B, WscButton::Options};
} // namespace

WscButton InputMap::effective_button(WscButton b, bool gamepad) const {
    if (gamepad && swap_ab_ && swap_ab_->on()) {
        if (b == WscButton::A)
            return WscButton::B;
        if (b == WscButton::B)
            return WscButton::A;
    }
    return b;
}

bool InputMap::has_menu_button(WscButton b) const {
    const auto &s = effective_slots(b, true);
    return std::any_of(s.begin(), s.end(), menu_button);
}

BindSlots InputMap::menu_slots(WscButton b) const {
    std::array<BindSlots, 3> resolved{};
    for (size_t i = 0; i < resolved.size(); ++i) {
        resolved[i] = effective_slots(kMenuButtons[i], true);
        for (Binding &s : resolved[i])
            if (!menu_button(s))
                s = {};
    }
    // Keep all working custom controls, then reserve a separate recovery control for each missing
    // action. Gameplay bindings and their empty slot positions stay intact, even after saving.
    constexpr SDL_GamepadButton candidates[] = {
        SDL_GAMEPAD_BUTTON_SOUTH,         SDL_GAMEPAD_BUTTON_EAST,          SDL_GAMEPAD_BUTTON_BACK,
        SDL_GAMEPAD_BUTTON_START,         SDL_GAMEPAD_BUTTON_WEST,          SDL_GAMEPAD_BUTTON_NORTH,
        SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER};
    for (auto &s : resolved) {
        if (std::any_of(s.begin(), s.end(), menu_button))
            continue;
        for (auto candidate : candidates) {
            const Binding control{Binding::Kind::PadButton, candidate};
            const bool used = std::any_of(resolved.begin(), resolved.end(), [&](const auto &other) {
                return std::find(other.begin(), other.end(), control) != other.end();
            });
            if (!used) {
                s[0] = control;
                break;
            }
        }
    }
    for (size_t i = 0; i < resolved.size(); ++i)
        if (b == kMenuButtons[i])
            return resolved[i];
    return effective_slots(b, true);
}

bool InputMap::menu_edit_valid(const InputMap &before) const {
    for (auto b : kMenuButtons)
        if (before.has_menu_button(b) && !has_menu_button(b))
            return false;
    return true;
}

std::string bound_button_text(const InputMap &map, WscButton button, bool pad, int pad_icons,
                              SDL_GamepadType type) {
    for (const Binding &binding : map.effective_slots(button, pad))
        if (!binding.empty()) {
            const std::string name = binding_icon_name(binding, pad_icons);
            return name.empty() ? binding_label(binding, type) : "{icon:" + name + "}";
        }
    return "-";
}

bool InputMap::opens_options(bool gamepad) const {
    for (const Binding &o : gamepad ? menu_slots(WscButton::Options) : slots(WscButton::Options, false))
        if (!o.empty())
            return true;
    return false;
}

bool InputMap::clear(WscButton b, bool gamepad, int slot) {
    if (slot < 0 || slot >= kBindSlots)
        return false;
    const InputMap before = *this;
    const Binding old = slots(b, gamepad)[slot];
    slots(b, gamepad)[slot] = {};
    if ((gamepad && !menu_edit_valid(before)) ||
        (!gamepad && b == WscButton::Options && !opens_options(false))) {
        slots(b, gamepad)[slot] = old; // the last way into the menu stays
        return false;
    }
    return true;
}

WscButton InputMap::assign(WscButton b, int slot, Binding bind, bool *refused) {
    if (refused)
        *refused = false;
    if (bind.empty() || slot < 0 || slot >= kBindSlots ||
        (b == WscButton::Options && bind.kind == Binding::Kind::PadAxis)) {
        if (refused)
            *refused = true;
        return WscButton::Count;
    }
    auto &table = bind.is_pad() ? pad : keys;
    const InputMap before = *this;
    const Binding old = table[int(b)][slot];
    WscButton other = WscButton::Count;
    for (int i = 0; i < kWscButtonCount; ++i)
        for (int k = 0; k < kBindSlots; ++k)
            if (table[i][k] == bind && !(i == int(b) && k == slot)) {
                // the swap; within the same button that just moves it (no duplicate left behind); Options
                // can't take a stick or trigger, so it's left empty instead
                const bool fits = !(i == int(WscButton::Options) && old.kind == Binding::Kind::PadAxis);
                table[i][k] = i == int(b) || !fits || other != WscButton::Count ? Binding{} : old;
                if (i != int(b))
                    other = WscButton(i);
            }
    table[int(b)][slot] = bind;
    if ((bind.is_pad() && !menu_edit_valid(before)) || (!bind.is_pad() && !opens_options(false))) {
        table = bind.is_pad() ? before.pad : before.keys;
        if (refused)
            *refused = true;
        return WscButton::Count;
    }
    return other;
}

void InputMap::reset(bool gamepad) {
    const InputMap d = defaults();
    if (gamepad)
        pad = d.pad;
    else
        keys = d.keys;
}

namespace {

// Our names for the pad's inputs in sawgod.toml: by position, like SDL's button enum (SDL's own mapping
// names "a"/"b" are Xbox labels, which are the other way round on a Nintendo pad).
constexpr const char *kPadButtonIds[SDL_GAMEPAD_BUTTON_COUNT] = {
    "south",   "east",    "west",     "north",     "back",      "guide",      "start", "lstick",  "rstick",
    "lb",      "rb",      "dpad_up",  "dpad_down", "dpad_left", "dpad_right", "misc1", "paddle1", "paddle2",
    "paddle3", "paddle4", "touchpad", "misc2",     "misc3",     "misc4",      "misc5", "misc6"};
struct AxisId {
    int axis, dir;
    const char *id;
};
constexpr AxisId kAxisIds[] = {
    {SDL_GAMEPAD_AXIS_LEFTX, -1, "lstick_left"},  {SDL_GAMEPAD_AXIS_LEFTX, +1, "lstick_right"},
    {SDL_GAMEPAD_AXIS_LEFTY, -1, "lstick_up"},    {SDL_GAMEPAD_AXIS_LEFTY, +1, "lstick_down"},
    {SDL_GAMEPAD_AXIS_RIGHTX, -1, "rstick_left"}, {SDL_GAMEPAD_AXIS_RIGHTX, +1, "rstick_right"},
    {SDL_GAMEPAD_AXIS_RIGHTY, -1, "rstick_up"},   {SDL_GAMEPAD_AXIS_RIGHTY, +1, "rstick_down"},
    {SDL_GAMEPAD_AXIS_LEFT_TRIGGER, +1, "lt"},    {SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, +1, "rt"},
};

std::string quote(const std::string &s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out + "\"";
}

// A one-line TOML array of strings: ["a", "b"]. nullopt if it isn't one.
std::optional<std::vector<std::string>> parse_string_array(const std::string &raw) {
    std::vector<std::string> out;
    size_t i = 0;
    auto skip = [&] {
        while (i < raw.size() && (raw[i] == ' ' || raw[i] == '\t'))
            ++i;
    };
    skip();
    if (i >= raw.size() || raw[i] != '[')
        return std::nullopt;
    ++i;
    skip();
    if (i < raw.size() && raw[i] == ']')
        return out;
    for (;;) {
        skip();
        if (i >= raw.size() || raw[i] != '"')
            return std::nullopt;
        std::string s;
        for (++i; i < raw.size() && raw[i] != '"'; ++i) {
            if (raw[i] == '\\' && i + 1 < raw.size())
                ++i;
            s += raw[i];
        }
        if (i >= raw.size())
            return std::nullopt;
        ++i;
        out.push_back(s);
        skip();
        if (i < raw.size() && raw[i] == ',') {
            ++i;
            skip();
            if (i < raw.size() && raw[i] == ']') // trailing comma
                return out;
            continue;
        }
        if (i < raw.size() && raw[i] == ']')
            return out;
        return std::nullopt;
    }
}

} // namespace

std::string binding_id(const Binding &b) {
    switch (b.kind) {
    case Binding::Kind::Key: {
        const char *name = SDL_GetScancodeName(SDL_Scancode(b.code));
        return name && *name ? std::string(name) : "scancode_" + std::to_string(b.code);
    }
    case Binding::Kind::PadButton:
        return b.code >= 0 && b.code < SDL_GAMEPAD_BUTTON_COUNT ? kPadButtonIds[b.code] : "";
    case Binding::Kind::PadAxis:
        for (const AxisId &a : kAxisIds)
            if (a.axis == b.code && a.dir == b.dir)
                return a.id;
        return "";
    case Binding::Kind::None:
        break;
    }
    return "";
}

std::optional<Binding> binding_from_id(const std::string &id, bool gamepad) {
    if (id.empty())
        return Binding{}; // explicit empty slot preserves the following binding's position
    if (gamepad) {
        for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i)
            if (id == kPadButtonIds[i])
                return Binding{Binding::Kind::PadButton, i, 0};
        for (const AxisId &a : kAxisIds)
            if (id == a.id)
                return Binding{Binding::Kind::PadAxis, a.axis, a.dir};
        return std::nullopt;
    }
    if (id.rfind("scancode_", 0) == 0) {
        const int code = std::atoi(id.c_str() + 9);
        if (code > 0 && code < SDL_SCANCODE_COUNT)
            return Binding{Binding::Kind::Key, code, 0};
        return std::nullopt;
    }
    const SDL_Scancode sc = SDL_GetScancodeFromName(id.c_str());
    if (sc == SDL_SCANCODE_UNKNOWN)
        return std::nullopt;
    return Binding{Binding::Kind::Key, int(sc), 0};
}

namespace {
std::string table_key(bool gamepad, int button) {
    return std::string(gamepad ? "gamepad." : "keyboard.") + wsc_button_id(WscButton(button));
}
} // namespace

void InputMap::load(Config &config, std::vector<std::string> *warnings) {
    swap_ab_ = config.find("input.swap_ab");
    auto warn = [&](const std::string &w) {
        if (warnings)
            warnings->push_back(w);
    };
    for (int gp = 0; gp < 2; ++gp) {
        auto &table = gp ? pad : keys;
        for (int i = 0; i < kWscButtonCount; ++i) {
            const std::string name = table_key(gp, i);
            const std::optional<std::string> raw = config.raw(name);
            if (!raw)
                continue;
            const auto ids = parse_string_array(*raw);
            if (!ids) {
                warn(name + ": expected a list of names like [\"X\"]");
                continue;
            }
            BindSlots slots{};
            int n = 0;
            for (const std::string &id : *ids) {
                const std::optional<Binding> b = binding_from_id(id, gp);
                if (!b)
                    warn(name + ": unknown " + (gp ? "gamepad input " : "key ") + quote(id));
                else if (n == kBindSlots)
                    warn(name + ": more than " + std::to_string(kBindSlots) + " bindings");
                else
                    slots[n++] = *b;
            }
            table[i] = slots;
        }
        // one input on two buttons (a hand edit): a button listed in the file takes it from a default; of
        // two listed buttons the later one keeps it
        for (int i = 0; i < kWscButtonCount; ++i) {
            if (!config.raw(table_key(gp, i)))
                continue;
            for (const Binding &b : table[i]) {
                if (b.empty())
                    continue;
                for (int j = 0; j < kWscButtonCount; ++j) {
                    if (j == i)
                        continue;
                    const bool listed = config.raw(table_key(gp, j)).has_value();
                    if (listed && j > i)
                        continue; // (handled when the loop gets to j)
                    for (Binding &o : table[j])
                        if (o == b) {
                            o = {};
                            if (listed)
                                warn(table_key(gp, i) + ": " + quote(binding_id(b)) + " also on " +
                                     wsc_button_id(WscButton(j)) + "; kept here");
                        }
                }
            }
        }
        if (gp) {
            for (auto b : kMenuButtons)
                if (!has_menu_button(b))
                    warn(table_key(true, int(b)) + ": using recovery buttons in Options");
            continue; // Preserve legacy empty, axis-only and directional slots for gameplay.
        }
        // Options: no stick or trigger, and never without a binding on a device -- a file that leaves
        // it empty gets the default back (taken from whatever else held it).
        BindSlots &opt = table[int(WscButton::Options)];
        const BindSlots def = (gp ? defaults().pad : defaults().keys)[int(WscButton::Options)];
        for (int k = 0; k < kBindSlots; ++k)
            if (opt[k].kind == Binding::Kind::PadAxis)
                opt[k] = {};
        if (!opens_options(gp != 0)) {
            for (int i = 0; i < kWscButtonCount; ++i)
                for (int k = 0; k < kBindSlots; ++k)
                    if (table[i][k] == def[0])
                        table[i][k] = {};
            opt[0] = def[0];
            warn(table_key(gp, int(WscButton::Options)) + ": nothing opens Options; " +
                 quote(binding_id(def[0])) + " restored");
        }
    }
}

void InputMap::store(Config &config) const {
    const InputMap d = defaults();
    for (int gp = 0; gp < 2; ++gp)
        for (int i = 0; i < kWscButtonCount; ++i) {
            const std::string name = table_key(gp, i);
            const BindSlots &mine = (gp ? pad : keys)[i];
            if (mine == (gp ? d.pad : d.keys)[i]) {
                config.erase_raw(name);
                continue;
            }
            std::string list;
            int count = kBindSlots;
            while (count > 0 && mine[size_t(count - 1)].empty())
                --count;
            for (int slot = 0; slot < count; ++slot)
                list += (slot ? ", " : "") + quote(binding_id(mine[size_t(slot)]));
            config.set_raw(name, "[" + list + "]");
        }
}

std::string binding_label(const Binding &b, SDL_GamepadType type) {
    const bool playstation =
        type == SDL_GAMEPAD_TYPE_PS3 || type == SDL_GAMEPAD_TYPE_PS4 || type == SDL_GAMEPAD_TYPE_PS5;
    const bool nintendo = type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO ||
                          type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT ||
                          type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT ||
                          type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR;
    switch (b.kind) {
    case Binding::Kind::None:
        return "-";
    case Binding::Kind::Key: {
        std::string n = binding_id(b);
        auto shorten = [&](const std::string &from, const std::string &to) {
            if (n.rfind(from, 0) == 0 && n.size() > from.size())
                n = to + n.substr(from.size());
        };
        shorten("Keypad ", "KP");
        shorten("Left ", "L");
        shorten("Right ", "R");
        if (n == "Return")
            n = "Enter";
        // the few names wider than a binding slot even at the small size
        if (n == "ThousandsSeparator")
            n = "1000Sep";
        if (n == "MediaTrackPrevious")
            n = "PrevTrk";
        if (n == "MediaTrackNext")
            n = "NextTrk";
        if (n == "MediaFastForward")
            n = "MediaFF";
        shorten("International ", "Intl ");
        shorten("Language ", "Lang ");
        static const std::pair<const char *, const char *> short_names[] = {
            {"DecimalSeparator", "Dec Sep"}, {"CurrencySubUnit", "Cur Sub"}, {"CurrencyUnit", "CurUnit"},
            {"MediaPlayPause", "Play/Ps"},   {"Application", "App"},         {"NonUSBackslash", "NonUSBk"},
            {"KPMemSubtract", "KPMem-"},     {"KPMemMultiply", "KPMem*"},    {"KPHexadecimal", "KPHex"},
            {"AC Properties", "AC Prop"},    {"AC Bookmarks", "ACMark"},     {"Clear / Again", "Clr/Agn"},
            {"KPClearEntry", "KPClrEn"},     {"ChannelDown", "ChanDn"},      {"MediaRewind", "Rewind"},
            {"KPMemDivide", "KPMem/"},       {"MediaRecord", "Record"},      {"KPMemRecall", "KPMemRc"},
            {"KPBackspace", "KPBksp"},       {"KPMemStore", "KPMemSt"}};
        for (auto [from, to] : short_names)
            if (n == from)
                n = to;
        return n;
    }
    case Binding::Kind::PadButton:
        switch (SDL_GamepadButton(b.code)) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
            return "D-Up";
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            return "D-Down";
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
            return "D-Left";
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            return "D-Right";
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:
            return playstation ? "L3" : "LS Push";
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
            return playstation ? "R3" : "RS Push";
        case SDL_GAMEPAD_BUTTON_GUIDE:
            return "Guide";
        case SDL_GAMEPAD_BUTTON_TOUCHPAD:
            return "Touchpad";
        case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1:
        case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1:
        case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2:
        case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2:
            return "Paddle " + std::to_string(b.code - SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1 + 1);
        default: {
            const std::string n = pad_button_name(type, SDL_GamepadButton(b.code));
            return n == "?" ? binding_id(b) : n;
        }
        }
    case Binding::Kind::PadAxis: {
        const char *dir = "";
        switch (SDL_GamepadAxis(b.code)) {
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
            return playstation ? "L2" : nintendo ? "ZL" : "LT";
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
            return playstation ? "R2" : nintendo ? "ZR" : "RT";
        case SDL_GAMEPAD_AXIS_LEFTX:
        case SDL_GAMEPAD_AXIS_RIGHTX:
            dir = b.dir < 0 ? " Left" : " Right";
            break;
        default:
            dir = b.dir < 0 ? " Up" : " Down";
            break;
        }
        const bool left = b.code == SDL_GAMEPAD_AXIS_LEFTX || b.code == SDL_GAMEPAD_AXIS_LEFTY;
        return std::string(left ? "LS" : "RS") + dir;
    }
    }
    return "?";
}

std::string binding_icon_name(const Binding &b, int pad_icons) {
    const int family =
        pad_icons < 1 || pad_icons > 3 ? 1 : pad_icons; // Xbox, PS, Switch (Auto resolved by the caller)
    auto pick = [&](const std::string &a, const std::string &ps, const std::string &c) {
        return family == 2 ? ps : family == 3 ? c : a;
    };
    // a pad's icon with its family, for the Icon Type's button colors (engine/button_icons.h)
    auto pad = [&](const std::string &a, const std::string &ps, const std::string &c) {
        return std::string(1, " xpn"[family]) + ":" + pick(a, ps, c);
    };
    switch (b.kind) {
    case Binding::Kind::None:
        return "";
    case Binding::Kind::Key: {
        switch (SDL_Scancode(b.code)) {
        case SDL_SCANCODE_UP:
            return pick("key Up", "key Up", "key Up");
        case SDL_SCANCODE_DOWN:
            return pick("key Down", "key Down", "key Down");
        case SDL_SCANCODE_LEFT:
            return pick("key Left", "key Left", "key Left");
        case SDL_SCANCODE_RIGHT:
            return pick("key Right", "key Right", "key Right");
        default:
            break;
        }
        // the set's short names for the long ones; the rest as their label
        static constexpr std::pair<const char *, const char *> kShort[] = {
            {"Return", "ENTER"},      {"Escape", "ESC"},
            {"Backspace", "BKSP"},    {"Delete", "DEL"},
            {"Insert", "INS"},        {"PageUp", "PGUP"},
            {"PageDown", "PGDN"},     {"CapsLock", "CAPS"},
            {"PrintScreen", "PRTSC"}, {"ScrollLock", "SCRLK"},
            {"Left Shift", "LSHIFT"}, {"Right Shift", "RSHIFT"},
            {"Left Ctrl", "LCTRL"},   {"Right Ctrl", "RCTRL"},
            {"Left Alt", "LALT"},     {"Right Alt", "RALT"},
            {"Left GUI", "LGUI"},     {"Right GUI", "RGUI"},
            {"Space", "SPACE"},       {"Tab", "TAB"}};
        const std::string id = binding_id(b);
        std::string label = id;
        for (const auto &[from, to] : kShort)
            if (id == from)
                label = to;
        if (label.rfind("Keypad ", 0) == 0)
            label = "KP" + label.substr(7);
        if (label.size() > 6 || keycap_icon(label).empty()) // wider than a slot, or not in the lettering
            return "";
        return "key:" + label;
    }
    case Binding::Kind::PadButton:
        switch (SDL_GamepadButton(b.code)) {
        case SDL_GAMEPAD_BUTTON_SOUTH:
            return pad("pad A", "PS cross", "pad B");
        case SDL_GAMEPAD_BUTTON_EAST:
            return pad("pad B", "PS circle", "pad A");
        case SDL_GAMEPAD_BUTTON_WEST:
            return pad("pad X", "PS square", "pad Y");
        case SDL_GAMEPAD_BUTTON_NORTH:
            return pad("pad Y", "PS triangle", "pad X");
        case SDL_GAMEPAD_BUTTON_BACK:
            return pad("Select", "Select", "Switch -");
        case SDL_GAMEPAD_BUTTON_START:
            return pad("Start", "Start", "Switch +");
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:
            return pad("L stick", "L3", "L stick");
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
            return pad("R stick", "R3", "R stick");
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
            return pad("LB", "L1", "Switch L");
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
            return pad("RB", "R1", "Switch R");
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
            return pad("d-pad up", "d-pad up", "d-pad up");
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            return pad("d-pad down", "d-pad down", "d-pad down");
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
            return pad("d-pad left", "d-pad left", "d-pad left");
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            return pad("d-pad right", "d-pad right", "d-pad right");
        default: // guide, paddles, touchpad, the extra buttons: their names
            return "";
        }
    case Binding::Kind::PadAxis: {
        if (b.code == SDL_GAMEPAD_AXIS_LEFT_TRIGGER)
            return pad("LT", "L2", "ZL");
        if (b.code == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
            return pad("RT", "R2", "ZR");
        const bool left = b.code == SDL_GAMEPAD_AXIS_LEFTX || b.code == SDL_GAMEPAD_AXIS_LEFTY;
        const bool x = b.code == SDL_GAMEPAD_AXIS_LEFTX || b.code == SDL_GAMEPAD_AXIS_RIGHTX;
        const std::string name = std::string(left ? "L stick " : "R stick ") +
                                 (x ? (b.dir < 0 ? "left" : "right") : (b.dir < 0 ? "up" : "down"));
        return pad(name, name, name);
    }
    }
    return "";
}

int resolve_pad_icons(int setting, SDL_GamepadType type) {
    if (setting >= 1 && setting <= 3)
        return setting;
    switch (type) { // Auto: the connected pad's own family
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5:
        return 2;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return 3;
    default: // Xbox, other pads, none
        return 1;
    }
}

ButtonIcon binding_icon(const Binding &b, int pad_icons) {
    return named_icon(binding_icon_name(b, pad_icons));
}

const ButtonIcon *wsc_button_icon(WscButton b) {
    if (int(b) >= kButtonTokenCount)
        return nullptr;
    const std::string n = button_token_name(int(b));
    return button_icon("WSC " + (b == WscButton::Start ? std::string("START") : n));
}

std::string pad_button_name(SDL_GamepadType type, SDL_GamepadButton button) {
    const bool playstation =
        type == SDL_GAMEPAD_TYPE_PS3 || type == SDL_GAMEPAD_TYPE_PS4 || type == SDL_GAMEPAD_TYPE_PS5;
    const bool nintendo = type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO ||
                          type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT ||
                          type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT ||
                          type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR;
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH:
    case SDL_GAMEPAD_BUTTON_EAST:
    case SDL_GAMEPAD_BUTTON_WEST:
    case SDL_GAMEPAD_BUTTON_NORTH: {
        SDL_GamepadButtonLabel label = SDL_GetGamepadButtonLabelForType(type, button);
        if (label == SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN)
            label = SDL_GetGamepadButtonLabelForType(SDL_GAMEPAD_TYPE_XBOXONE, button);
        switch (label) {
        case SDL_GAMEPAD_BUTTON_LABEL_A:
            return "A";
        case SDL_GAMEPAD_BUTTON_LABEL_B:
            return "B";
        case SDL_GAMEPAD_BUTTON_LABEL_X:
            return "X";
        case SDL_GAMEPAD_BUTTON_LABEL_Y:
            return "Y";
        case SDL_GAMEPAD_BUTTON_LABEL_CROSS:
            return "Cross";
        case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE:
            return "Circle";
        case SDL_GAMEPAD_BUTTON_LABEL_SQUARE:
            return "Square";
        case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE:
            return "Triangle";
        default:
            return "?";
        }
    }
    case SDL_GAMEPAD_BUTTON_START:
        if (playstation)
            return type == SDL_GAMEPAD_TYPE_PS3 ? "Start" : "Options";
        if (nintendo)
            return "+";
        return type == SDL_GAMEPAD_TYPE_XBOXONE ? "Menu" : "Start";
    case SDL_GAMEPAD_BUTTON_BACK:
        if (playstation)
            return type == SDL_GAMEPAD_TYPE_PS5   ? "Create"
                   : type == SDL_GAMEPAD_TYPE_PS4 ? "Share"
                                                  : "Select";
        if (nintendo)
            return "-";
        if (type == SDL_GAMEPAD_TYPE_XBOXONE)
            return "View";
        return type == SDL_GAMEPAD_TYPE_XBOX360 || type == SDL_GAMEPAD_TYPE_UNKNOWN ||
                       type == SDL_GAMEPAD_TYPE_STANDARD
                   ? "Back"
                   : "Select";
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        return "D-Pad";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        return playstation ? "L1" : nintendo ? "L" : "LB";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
        return playstation ? "R1" : nintendo ? "R" : "RB";
    default:
        return "?";
    }
}

Input::~Input() {
    for (SDL_Gamepad *p : pads_)
        SDL_CloseGamepad(p);
}

bool Input::pad_pressed(const Binding &b) const {
    const int threshold = 32767 * kStickDeadzone / 100;
    for (SDL_Gamepad *pad : pads_) {
        if (pad_suppressed(SDL_GetGamepadID(pad), b))
            continue;
        switch (b.kind) {
        case Binding::Kind::PadButton:
            if (SDL_GetGamepadButton(pad, SDL_GamepadButton(b.code)))
                return true;
            break;
        case Binding::Kind::PadAxis: {
            int v = SDL_GetGamepadAxis(pad, SDL_GamepadAxis(b.code));
            if (b.dir < 0 ? v < -threshold : v > threshold)
                return true;
            break;
        }
        case Binding::Kind::Key:
        case Binding::Kind::None:
            break;
        }
    }
    return false;
}

uint16_t Input::held(const InputMap &map) const {
    const bool *keys = SDL_GetKeyboardState(nullptr);
    uint16_t out = 0;
    for (int i = 0; i < kWscButtonCount; ++i) {
        bool on = false;
        for (const Binding &b : map.keys[i])
            on = on || (b.kind == Binding::Kind::Key && keys[b.code]);
        for (const Binding &b : map.effective_slots(WscButton(i), true))
            on = on || pad_pressed(b);
        if (on)
            out |= wsc_key_bit(WscButton(i));
    }
    return out;
}

bool Input::button_held(const InputMap &map, WscButton b) const {
    const bool *keys = SDL_GetKeyboardState(nullptr);
    for (const Binding &k : map.keys[int(b)])
        if (k.kind == Binding::Kind::Key && keys[k.code])
            return true;
    for (const Binding &k : map.effective_slots(b, true))
        if (pad_pressed(k))
            return true;
    return false;
}

bool Input::pad_suppressed(SDL_JoystickID pad, const Binding &b) const {
    return std::any_of(suppressed_.begin(), suppressed_.end(), [&](const auto &s) {
        return s.pad == pad && s.control.kind == b.kind && s.control.code == b.code;
    });
}

void Input::suppress_gamepad_until_release() {
    suppressed_.clear();
    for (SDL_Gamepad *pad : pads_) {
        const auto id = SDL_GetGamepadID(pad);
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b)
            if (SDL_GetGamepadButton(pad, SDL_GamepadButton(b)))
                suppressed_.push_back({id, {Binding::Kind::PadButton, b}});
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a)
            if (std::abs(int(SDL_GetGamepadAxis(pad, SDL_GamepadAxis(a)))) > 32767 * kStickDeadzone / 100)
                suppressed_.push_back({id, {Binding::Kind::PadAxis, a}});
    }
    pending_ = {};
    dir_latched_ = true;
}

void Input::handle_event(const SDL_Event &e) {
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_UP)
        std::erase_if(suppressed_, [&](const auto &s) {
            return s.pad == e.gbutton.which && s.control.kind == Binding::Kind::PadButton &&
                   s.control.code == e.gbutton.button;
        });
    if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
        std::abs(int(e.gaxis.value)) <= 32767 * kStickDeadzone / 100)
        std::erase_if(suppressed_, [&](const auto &s) {
            return s.pad == e.gaxis.which && s.control.kind == Binding::Kind::PadAxis &&
                   s.control.code == e.gaxis.axis;
        });
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
        pad_suppressed(e.gbutton.which, {Binding::Kind::PadButton, int(e.gbutton.button)}))
        return;
    switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
        last_pad_ = false;
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        last_pad_ = true;
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (std::abs(int(e.gaxis.value)) > 32767 * kStickDeadzone / 100)
            last_pad_ = true;
        break;
    case SDL_EVENT_JOYSTICK_ADDED:
        // A controller SDL has no gamepad mapping for never becomes a gamepad, so it can't be used.
        if (!SDL_IsGamepad(e.jdevice.which)) {
            const char *name = SDL_GetJoystickNameForID(e.jdevice.which);
            sawgod::debug_log::warnf("input",
                                     "Controller \"%s\" connected but is not a recognized gamepad; "
                                     "try its XInput mode",
                                     name ? name : "unknown");
        }
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
        if (SDL_Gamepad *p = SDL_OpenGamepad(e.gdevice.which))
            pads_.push_back(p);
        else
            sawgod::debug_log::errorf("input", "Gamepad connected but could not be opened: %s",
                                      SDL_GetError());
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        std::erase_if(suppressed_, [&](const auto &s) { return s.pad == e.gdevice.which; });
        for (auto it = pads_.begin(); it != pads_.end(); ++it)
            if (SDL_GetGamepadID(*it) == e.gdevice.which) {
                SDL_CloseGamepad(*it);
                pads_.erase(it);
                break;
            }
        break;
    default:
        break;
    }
    if (capture_ != Capture::Off) {
        const int threshold = 32767 * 60 / 100; // a firm push, more than the play deadzone
        switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
            if (e.key.repeat)
                break;
            if (is_options(Binding{Binding::Kind::Key, int(e.key.scancode), 0}))
                end_capture(std::nullopt, true);
            else if (capture_ == Capture::Keys)
                end_capture(Binding{Binding::Kind::Key, int(e.key.scancode), 0}, false);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            if (is_options(Binding{Binding::Kind::PadButton, int(e.gbutton.button), 0}))
                end_capture(std::nullopt, true);
            else if (capture_ == Capture::Pad && e.gbutton.button != SDL_GAMEPAD_BUTTON_GUIDE)
                end_capture(Binding{Binding::Kind::PadButton, int(e.gbutton.button), 0}, false);
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
            const int axis = e.gaxis.axis;
            if (capture_ != Capture::Pad || axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT)
                break;
            const int v = e.gaxis.value;
            if (std::abs(v) < threshold / 2)
                axis_armed_[axis] = true; // back near the middle: may be captured now
            else if (axis_armed_[axis] && std::abs(v) > threshold)
                end_capture(Binding{Binding::Kind::PadAxis, axis, v < 0 ? -1 : +1}, false);
            break;
        }
        default:
            break;
        }
        return; // no menu actions while capturing
    }
    // Resolve one gamepad press once. Active Accept/Cancel win over clear/page shortcuts, and
    // directions stay dedicated to navigation, including for unusable legacy directional bindings.
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        const Binding b{Binding::Kind::PadButton, int(e.gbutton.button)};
        if (menu_open_ && is_bound(WscButton::A, b)) {
            pending_.accept = true;
            return;
        }
        if (menu_open_ && is_bound(WscButton::B, b)) {
            pending_.back = true;
            return;
        }
        if (is_options(b)) {
            pending_.toggle = true;
            return;
        }
    }
    // Options toggles the menu. While the menu is open its own keys come first, so binding Options to one
    // of them (X, the pad's south button...) can't take the menu's navigation away; B still closes it.
    const bool options = (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
                          is_options(Binding{Binding::Kind::Key, int(e.key.scancode), 0})) ||
                         (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
                          is_options(Binding{Binding::Kind::PadButton, int(e.gbutton.button), 0}));
    if (options && !menu_open_) {
        pending_.toggle = true;
        return;
    }
    const MenuActions was = pending_;
    const auto toggle_unless_menu_key = [&] {
        const MenuActions &p = pending_;
        const bool menu_key =
            p.up != was.up || p.down != was.down || p.left != was.left || p.right != was.right ||
            p.accept != was.accept || p.back != was.back || p.prev_page != was.prev_page ||
            p.next_page != was.next_page || p.clear != was.clear ||
            // (the pad's directions are polled, not set here)
            (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP ||
                                                         e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN ||
                                                         e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT ||
                                                         e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT));
        if (options && !menu_key)
            pending_.toggle = true;
    };
    struct AtExit {
        const decltype(toggle_unless_menu_key) &f;
        ~AtExit() { f(); }
    } at_exit{toggle_unless_menu_key};
    switch (e.type) {
    case SDL_EVENT_KEY_DOWN: {
        const SDL_Keymod mod = e.key.mod;
        const bool repeat = e.key.repeat;
        switch (e.key.scancode) {
        case SDL_SCANCODE_UP:
            pending_.up = true;
            break;
        case SDL_SCANCODE_DOWN:
            pending_.down = true;
            break;
        case SDL_SCANCODE_LEFT:
            pending_.left = true;
            break;
        case SDL_SCANCODE_RIGHT:
            pending_.right = true;
            break;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
            if (!repeat && !(mod & SDL_KMOD_ALT))
                pending_.accept = true;
            break;
        case SDL_SCANCODE_SPACE:
        case SDL_SCANCODE_X:
            if (!repeat)
                pending_.accept = true;
            break;
        case SDL_SCANCODE_Z:
        case SDL_SCANCODE_BACKSPACE:
            if (!repeat)
                pending_.back = true;
            break;
        case SDL_SCANCODE_DELETE:
            if (!repeat)
                pending_.clear = true;
            break;
        case SDL_SCANCODE_TAB:
            if (mod & SDL_KMOD_SHIFT)
                pending_.prev_page = true;
            else
                pending_.next_page = true;
            break;
        case SDL_SCANCODE_PAGEUP:
        case SDL_SCANCODE_Q:
            pending_.prev_page = true;
            break;
        case SDL_SCANCODE_PAGEDOWN:
        case SDL_SCANCODE_E:
            pending_.next_page = true;
            break;
        default:
            break;
        }
        break;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        switch (SDL_GamepadButton(e.gbutton.button)) {
        case SDL_GAMEPAD_BUTTON_NORTH:
            pending_.clear = true;
            break;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
            pending_.prev_page = true;
            break;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
            pending_.next_page = true;
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
}

bool Input::is_bound(WscButton button, const Binding &b) const {
    static const InputMap kDefaults = InputMap::defaults();
    const InputMap &m = map_ ? *map_ : kDefaults;
    for (const Binding &o : b.is_pad() ? m.menu_slots(button) : m.slots(button, false))
        if (!o.empty() && o == b)
            return true;
    return false;
}

bool Input::is_options(const Binding &b) const { return is_bound(WscButton::Options, b); }

void Input::start_capture(bool gamepad) {
    capture_ = gamepad ? Capture::Pad : Capture::Keys;
    captured_.reset();
    capture_cancelled_ = false;
    pending_ = {};
    // an axis already pushed (a trigger resting off-center, a stick being held) must come back first
    const int threshold = 32767 * 30 / 100;
    axis_armed_.fill(true);
    for (SDL_Gamepad *pad : pads_)
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a)
            if (std::abs(int(SDL_GetGamepadAxis(pad, SDL_GamepadAxis(a)))) > threshold)
                axis_armed_[a] = false;
}

void Input::end_capture(std::optional<Binding> b, bool cancelled) {
    suppress_gamepad_until_release();
    capture_ = Capture::Off;
    captured_ = b;
    capture_cancelled_ = cancelled;
    dir_latched_ = true;
}

std::optional<Binding> Input::take_captured(bool *cancelled) {
    if (cancelled)
        *cancelled = capture_cancelled_;
    capture_cancelled_ = false;
    std::optional<Binding> out = captured_;
    captured_.reset();
    return out;
}

MenuActions Input::take_menu_actions(double now_s) {
    if (capture_ != Capture::Off) {
        pending_ = {};
        return {};
    }
    // gamepad directions (D-pad or left stick), with repeat
    const int threshold = 32767 * kStickDeadzone / 100;
    int dir = -1;
    for (SDL_Gamepad *pad : pads_) {
        const int x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
        const int y = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
        if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP) ||
            (y < -threshold && std::abs(y) >= std::abs(x)))
            dir = 0;
        else if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN) ||
                 (y > threshold && std::abs(y) >= std::abs(x)))
            dir = 1;
        else if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT) || x < -threshold)
            dir = 2;
        else if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || x > threshold)
            dir = 3;
        if (dir >= 0)
            break;
    }
    if (dir_latched_) { // just captured a binding: the push that made it isn't a menu move
        if (dir < 0)
            dir_latched_ = false;
        dir = -1;
    }
    bool fire = false;
    if (dir != held_dir_) {
        held_dir_ = dir;
        fire = dir >= 0;
        next_repeat_ = now_s + 0.35;
    } else if (dir >= 0 && now_s >= next_repeat_) {
        fire = true;
        next_repeat_ = now_s + 0.08;
    }
    if (fire) {
        bool *flags[] = {&pending_.up, &pending_.down, &pending_.left, &pending_.right};
        *flags[dir] = true;
    }
    MenuActions out = pending_;
    pending_ = {};
    return out;
}

SDL_GamepadType Input::gamepad_type() const {
    return pads_.empty() ? SDL_GAMEPAD_TYPE_UNKNOWN : SDL_GetGamepadType(pads_.front());
}

std::string Input::gamepad_name() const {
    if (pads_.empty())
        return {};
    const char *name = SDL_GetGamepadName(pads_.front());
    return name ? name : "Gamepad";
}

} // namespace sawgod::ui
