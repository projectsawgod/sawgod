// Keyed Options defaults shared with the game text export; no dependency on game code.
#pragma once
namespace sawgod::ui {
struct OptionsText {
    const char *key, *japanese, *english, *context;
};
inline constexpr OptionsText kOptionsText[] = {
#include "engine/options_text.inc"
};
} // namespace sawgod::ui
