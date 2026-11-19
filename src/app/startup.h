// Startup pieces of the app (app/main.cpp): what is read and checked before the game runs.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "app/launch_args.h"
#include "engine/assets.h"
#include "engine/config.h"
#include "engine/settings.h"

namespace sawgod::app {

// The file's bytes (empty: unreadable or empty).
std::vector<uint8_t> read_file(const std::string &path);

// The ROM of a launch and the English resources that go with it.
struct LoadedRom {
    std::vector<uint8_t> rom; // the ROM, with --assets and .gsa mods applied
    std::string english_text; // the English text resource (empty: none)
    // its pack's graphics (<pack>/graphics.gsa next to <pack>/text/strings.csv; e.g. Tower Reversed's redrawn
    // opening)
    std::optional<AssetArchive> english_graphics;
};

// Reads and checks args.rom, stores it and builds the resource archives on a first run, applies the byte
// packs and finds the English text. Returns 0, or the exit code after reporting what is wrong.
int load_rom(const LaunchArgs &args, const std::string &exe_dir, const std::string &gb_maps_dir,
             LoadedRom &loaded);

// Starts the debug log (docs/DEBUG_LOG.md): real play only, so scripted and test runs leave the player's logs
// alone (SAWGOD_DEBUG_LOG=1 logs those too, into their own data folder). SAWGOD_DEBUG_LOG_DIR: another
// folder, every file kept. The caller stops it.
void start_debug_log(const LaunchArgs &args);

// The settings file of a launch: --config, else sawgod.toml in the data folder (empty: none).
std::string config_file(const LaunchArgs &args);

// Loads the file's choices into the registered settings, then applies the command line's overrides for this
// run (they are not saved).
void load_settings(const LaunchArgs &args, const std::string &config_path, Config &config,
                   Settings &settings);

} // namespace sawgod::app
