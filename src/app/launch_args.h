// The app's command line (the flags are described at the top of app/main.cpp).
#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "app/audio_review.h"
#include "game/mode7.h"

namespace sawgod::app {

struct LaunchArgs {
    std::optional<int> scale;
    std::optional<int> frames; // if set, exit after this many frames (headless smoke test)
    std::string rom;
    std::string audio_review; // accelerated live-mixer crossfade capture directory
    std::optional<sawgod::app::AudioReviewOptions> audio_review_options;
    std::string audio_review_error;
    std::string assets;
    std::vector<std::string> mods;
    std::string english; // --english FILE: the English text resource (see the header)
    bool any_rom = false;
    std::string native = "all"; // --native all|none|NAME: decompiled routines to install (none: interpreter)
    std::string record;         // --record FILE: write held-button changes in tests/movies text format
    std::string sram;           // --sram FILE: start from this save file instead of the usual per-user one
    bool no_save = false;       // --no-save: don't load the save (unless --sram) or write it back out on exit
    std::string frame_timing_log;
    bool frame_picture_log = false;
    std::string config; // --config FILE: settings file instead of the per-user sawgod.toml
    std::string shot;   // --shot-overlay FILE
    std::string shot_page;
    std::string inputs; // --inputs FILE: play these button changes instead of live input (for shots)
    int shot_row = -1;
    std::string shot_options_sequence; // per-view . / t toggle / b back / a accept
    std::string shot_keys;             // --shot-keys: menu keys pressed before the shot
    int shot_w = 0, shot_h = 0; // --shot-size WxH: picture size instead of the window's (e.g. 1920x1080)
    std::string renderer;       // --renderer gpu|legacy: session-only startup override
    bool enhanced = false, unsnapped = false, shot_native_ui = false, shot_badges = false;
    std::string shot_mask, shot_state;
    bool shot_state_frames = false; // native RAM for each view of a handoff/fade audit
    std::string shot_map_batch;
    bool shot_field_party = false;
    bool shot_mode7_steps = false;
    bool shot_mode7_flat = false; // deterministic ordinary Enhanced world fixture
    bool shot_mode7_north = false;
    bool shot_mode7_entry = false;
    bool shot_field_npcs = false, shot_native_npcs = false, shot_field_walk = false, shot_timing_only = false;
    bool shot_mode7_no_walk = false;
    bool shot_fidelity_steps = false; // accelerated sample sequence through the live fade state
    struct MapShot {
        int map, x, y;
        std::string output;
    };
    std::vector<MapShot> map_shots;
    int shot_map = -1, shot_x = 0, shot_y = 0, shot_benchmark = 0, shot_effect_frame = -1;
    bool identity_shader = false; // --identity-shader: diagnostic shader, session-only
    int shot_gb_weight = -1;      // --shot-gb-weight 0..256: fixed live-picture crossfade for readback
    int shot_display_hz = 0;      // simulated presentation clock, only for accelerated captures
    bool shot_ff_review = false;  // simulated hold at 0.60..3.20 s; real host speed and VFX paths
    std::string shot_soak;        // per-view RGB/state hashes and camera diagnostics; skips PNGs
    int shot_start_level = 2;     // settled initial level; same Swap setting in all soak controls
    std::vector<size_t> shot_captures;
    std::vector<std::pair<size_t, SDL_EventType>> shot_render_resets;
    std::string shot_render_reset_error;
    std::vector<std::pair<unsigned, unsigned>> shot_holds;
    bool shot_audit_only = false; // --shot-audit-only: metadata replay, no SDL submissions
    int shot_run = 0;             // --shot-run N: N consecutive played frames on a simulated clock
    std::vector<std::pair<uint64_t, int>> shot_steps; // --shot-step FRAME:DIR: fidelity steps in that run
    sawgod::game::Mode7Look
        shot_camera;        // the fixed camera; --shot-camera TILT,LENS,DISTANCE,ZOOM for reviews
    std::string shot_store; // --shot-store DIR: read-only native save fixture
    struct ShotPoke {
        unsigned address, value, frame;
    };
    std::vector<ShotPoke> shot_pokes; // --shot-poke HEX=HEX@FRAME: deterministic screenshot fixture
};

// The flags as given; $SAWGOD_ROM is the default ROM path.
LaunchArgs parse_launch_args(int argc, char **argv);

// The shot fixtures' flag combinations, and the views of --shot-map-batch and the step fixtures (added to
// args.map_shots). Returns 0, or the exit code after printing what is wrong.
int check_shot_args(LaunchArgs &args);

} // namespace sawgod::app
