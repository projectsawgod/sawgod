// Port-level departures from the WSC version that are not options. Each is a hook
// subscription, so the decompiled code itself stays the original (the difftests run with none).
#pragma once

#include <algorithm>

#include "game/hooks.h"
#include "game/session.h"

namespace sawgod::game {

// The title menu offers new game and continue only: ORIJINARU (the GB original) is out of scope and not
// shown. TitleMenuOptions 3 -> 2: the menu draws two rows and its cursor wraps within
// them.
[[nodiscard]] inline hooks::Subscription hide_orijinaru(Session &s) {
    return s.hooks().subscribe(hooks::Value::TitleMenuOptions,
                               [](Machine &, int32_t n, const hooks::Args &) { return std::min(n, 2); });
}

} // namespace sawgod::game
