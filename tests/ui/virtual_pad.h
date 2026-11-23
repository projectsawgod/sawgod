#pragma once

#include <SDL3/SDL.h>

#include <cstdlib>

namespace sawgod::test {

inline bool gamepad_listed(SDL_JoystickID id) {
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    if (!ids) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "pilot gamepad enumeration failed: %s", SDL_GetError());
        std::abort();
    }
    bool found = false;
    for (int i = 0; i < count; ++i)
        found |= ids[i] == id;
    SDL_free(ids);
    return found;
}

// A removal can need another SDL pump even after PollEvent first returns false. Keep the rig's
// Input alive to close its gamepad handle before the next rig enumerates devices. The callback
// drains the rig's events and returns whether Input has released this pad.
template <class Pump> void detach_virtual_pad(SDL_JoystickID id, Pump pump) {
    if (SDL_DetachVirtualJoystick(id)) {
        for (int attempt = 0; attempt < 100; ++attempt) {
            SDL_PumpEvents();
            const bool closed = pump();
            if (!gamepad_listed(id) && closed)
                return;
            SDL_Delay(1);
        }
        SDL_SetError("virtual gamepad still listed or open in Input after 100 event pumps");
    }
    // Cleanup runs in destructors, so report failure without throwing during unwinding.
    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "pilot gamepad cleanup failed: %s", SDL_GetError());
    std::abort();
}

} // namespace sawgod::test
