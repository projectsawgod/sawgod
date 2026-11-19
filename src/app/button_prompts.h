// What the player presses, as the game's text and the title menu show it (app/main.cpp): the icons of the
// button tokens in text ({A}, {B}...) and the title menu's step hint.
#pragma once

#include "engine/fidelity.h"
#include "engine/input.h"
#include "engine/settings.h"
#include "game/host_picture.h"

namespace sawgod::app {

// Installs both sources. They read the given objects whenever text is drawn, so those must outlive the
// game's drawing (main()'s locals do).
void install_button_prompts(ui::Input &input, ui::InputMap &input_map, Settings &settings, Fidelity &fidelity,
                            game::HostPicture &host_picture);

} // namespace sawgod::app
