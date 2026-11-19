// The game's resources at startup (app/main.cpp): the data tables, text, graphics regions, maps and scene
// materials from wsc.sgz, the GB layer from gb.sgz, and the mods' files over both.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "app/launch_args.h"
#include "emu/wsc.h"
#include "engine/assets.h"
#include "game/gfx_swap.h"
#include "game/presentation_scene.h"

namespace sawgod::app {

// Loads the resource archives (app/first_run.h) and the mods (args.mods and the data folder's mods/) into the
// machine's session, the scene observer's materials and the GB picture. The machine needs its Session.
// base_text: text/ja.csv with the mods' over it. english_text: the mods' text/en.csv merged over the given
// one. gb_hand_png: gb/hand.png, for Options.
void load_game_resources(emu::Wsc &wsc, const LaunchArgs &args, game::SceneObserver &scene_observer,
                         game::GfxSwap &gfx_swap, std::string &base_text, std::string &english_text,
                         std::vector<uint8_t> &gb_hand_png);

// Language (see app/main.cpp's header): the Session's text table, switched at once (the next string drawn).
// main() delays the switch in the middle of a dialogue that reads the current language's text.
class GameLanguage {
  public:
    // Loads the base text and, with `english`, the English text and graphics over it; then picks the number
    // font. A text that fails to load is reported and cleared (base_text, english_text).
    void apply(emu::Wsc &wsc, bool english, std::string &base_text, std::string &english_text,
               const std::optional<AssetArchive> &english_graphics, const game::GfxSwap &gfx_swap);

  private:
    void set_text(emu::Wsc &wsc, bool english, std::string &base_text, std::string &english_text,
                  const std::optional<AssetArchive> &english_graphics);

    // The ROM bytes under the English graphics, put back when the language changes.
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> graphics_original_;
};

} // namespace sawgod::app
