// Tracks presentation nesting around native menus and dialogs.
#pragma once
#include "game/hooks.h"
namespace sawgod::game {
// Presentation nesting follows native C++ call scopes, including Fiber cancellation.
struct UiSceneScope {
    Machine &machine;
    hooks::Event event;
    UiSceneScope(Machine &m, hooks::Event e, int scene) : machine(m), event(e) {
        hooks::emit(event, machine, {scene, 1});
    }
    ~UiSceneScope() { hooks::emit(event, machine, {0, -1}); }
};
} // namespace sawgod::game
