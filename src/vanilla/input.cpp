#include "vanilla/input.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace sawgod::vanilla {
namespace {
ui::Binding parse_binding(const std::string &device, const std::string &control) {
    using K = ui::Binding::Kind;
    if (device == "key") {
        const auto scancode = SDL_GetScancodeFromName(control.c_str());
        if (scancode != SDL_SCANCODE_UNKNOWN && scancode != SDL_SCANCODE_ESCAPE &&
            scancode != SDL_SCANCODE_F11)
            return {K::Key, scancode};
    } else if (device == "button") {
        std::string name = control;
        if (name == "south")
            name = "a";
        else if (name == "east")
            name = "b";
        else if (name == "west")
            name = "x";
        else if (name == "north")
            name = "y";
        const auto b = SDL_GetGamepadButtonFromString(name.c_str());
        if (b != SDL_GAMEPAD_BUTTON_INVALID)
            return {K::PadButton, b};
    } else if (device == "axis" && control.size() > 1) {
        const char sign = control.back();
        const auto axis = SDL_GetGamepadAxisFromString(control.substr(0, control.size() - 1).c_str());
        if (axis != SDL_GAMEPAD_AXIS_INVALID && (sign == '+' || sign == '-'))
            return {K::PadAxis, axis, sign == '+' ? 1 : -1};
    }
    return {};
}
} // namespace

Input::~Input() {
    for (auto *pad : pads_)
        SDL_CloseGamepad(pad);
}
void Input::load(const std::filesystem::path &path) {
    if (!std::filesystem::exists(path))
        return;
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("Cannot read input mapping file.");
    std::string line;
    int number = 0;
    while (std::getline(file, line)) {
        ++number;
        line = line.substr(0, line.find('#'));
        std::istringstream row(line);
        std::string device, button, control, extra;
        if (!(row >> device))
            continue;
        int index = -1;
        if (row >> button >> control)
            for (int i = 0; i <= int(ui::WscButton::Start); ++i)
                if (button == ui::wsc_button_id(ui::WscButton(i)))
                    index = i;
        // A line replaces both slots for the keyboard or gamepad; a second pair is optional.
        ui::BindSlots bindings{};
        if (control != "none")
            bindings[0] = parse_binding(device, control);
        if (index < 0 || (device != "key" && device != "button" && device != "axis") ||
            (bindings[0].empty() && control != "none"))
            throw std::runtime_error("Invalid input mapping at line " + std::to_string(number));
        std::string second_device, second_control;
        if (row >> second_device) {
            if (!(row >> second_control) || control == "none" ||
                ((device == "key") != (second_device == "key")) ||
                (bindings[1] = parse_binding(second_device, second_control)).empty() || (row >> extra))
                throw std::runtime_error("Invalid input mapping at line " + std::to_string(number));
        }
        (device == "key" ? map_.keys : map_.pad)[size_t(index)] = bindings;
    }
}
void Input::event(const SDL_Event &event) {
    if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
        if (auto *pad = SDL_OpenGamepad(event.gdevice.which))
            pads_.push_back(pad);
    } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
        std::erase_if(pads_, [&](SDL_Gamepad *pad) {
            if (SDL_GetGamepadID(pad) != event.gdevice.which)
                return false;
            SDL_CloseGamepad(pad);
            return true;
        });
    }
}
uint16_t Input::held() const {
    const bool *keys = SDL_GetKeyboardState(nullptr);
    uint16_t mask = 0;
    using K = ui::Binding::Kind;
    for (int i = 0; i <= int(ui::WscButton::Start); ++i) {
        bool on = false;
        for (const auto &binding : map_.keys[size_t(i)])
            if (binding.kind == K::Key)
                on |= keys[binding.code];
        for (const auto &binding : map_.pad[size_t(i)])
            for (auto *pad : pads_) {
                if (binding.kind == K::PadButton)
                    on |= SDL_GetGamepadButton(pad, SDL_GamepadButton(binding.code));
                else if (binding.kind == K::PadAxis) {
                    const int value = SDL_GetGamepadAxis(pad, SDL_GamepadAxis(binding.code));
                    const int threshold = 32767 * ui::kStickDeadzone / 100;
                    on |= binding.dir < 0 ? value < -threshold : value > threshold;
                }
            }
        if (on)
            mask |= ui::wsc_key_bit(ui::WscButton(i));
    }
    return mask;
}
} // namespace sawgod::vanilla
