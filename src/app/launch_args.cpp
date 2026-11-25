#include "app/launch_args.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "game/maps.h"

namespace sawgod::app {

LaunchArgs parse_launch_args(int argc, char **argv) {
    LaunchArgs args;
    if (const char *env = std::getenv("SAWGOD_ROM"))
        args.rom = env;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            args.scale = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--touch-controls") == 0) {
            args.touch_controls = true;
        } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            args.frames = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--audio-review") == 0 && i + 1 < argc) {
            args.audio_review = argv[++i];
        } else if (std::strncmp(argv[i], "--audio-review-", 15) == 0) {
            const std::string option = argv[i];
            if (i + 1 == argc) {
                args.audio_review_error = option + " requires a value";
                break;
            }
            if (!args.audio_review_options)
                args.audio_review_options.emplace();
            args.audio_review_error =
                sawgod::app::audio_review_option(*args.audio_review_options, option, argv[++i]);
            if (!args.audio_review_error.empty())
                break;
        } else if (std::strcmp(argv[i], "--rom") == 0 && i + 1 < argc) {
            args.rom = argv[++i];
        } else if (std::strcmp(argv[i], "--assets") == 0 && i + 1 < argc) {
            args.assets = argv[++i];
        } else if (std::strcmp(argv[i], "--mod") == 0 && i + 1 < argc) {
            args.mods.push_back(argv[++i]);
        } else if (std::strcmp(argv[i], "--english") == 0 && i + 1 < argc) {
            args.english = argv[++i];
        } else if (std::strcmp(argv[i], "--any-rom") == 0) {
            args.any_rom = true;
        } else if (std::strcmp(argv[i], "--native") == 0 && i + 1 < argc) {
            args.native = argv[++i];
            if (args.native == "none")
                args.native.clear();
        } else if (std::strcmp(argv[i], "--record") == 0 && i + 1 < argc) {
            args.record = argv[++i];
        } else if (std::strcmp(argv[i], "--sram") == 0 && i + 1 < argc) {
            args.sram = argv[++i];
        } else if (std::strcmp(argv[i], "--no-save") == 0) {
            args.no_save = true;
        } else if (std::strcmp(argv[i], "--shot-ff-review") == 0) {
            args.shot_ff_review = true;
        } else if (std::strcmp(argv[i], "--live-picture-log") == 0 && i + 1 < argc) {
            args.live_picture_log = argv[++i];
        } else if (std::strcmp(argv[i], "--live-warmup") == 0 && i + 1 < argc) {
            args.live_warmup = std::max(0, std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--shot-display-hz") == 0 && i + 1 < argc) {
            args.shot_display_hz = std::clamp(std::atoi(argv[++i]), 1, 1000);
        } else if (std::strcmp(argv[i], "--frame-picture-log") == 0 && i + 1 < argc) {
            args.frame_picture_log = true;
            args.frame_timing_log = argv[++i];
        } else if (std::strcmp(argv[i], "--frame-timing-log") == 0 && i + 1 < argc) {
            args.frame_timing_log = argv[++i];
        } else if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            args.config = argv[++i];
        } else if (std::strcmp(argv[i], "--shot-overlay") == 0 && i + 1 < argc) {
            args.shot = argv[++i];
        } else if (std::strcmp(argv[i], "--shot-page") == 0 && i + 1 < argc) {
            args.shot_page = argv[++i];
        } else if (std::strcmp(argv[i], "--inputs") == 0 && i + 1 < argc) {
            args.inputs = argv[++i];
        } else if (std::strcmp(argv[i], "--shot-keys") == 0 && i + 1 < argc) {
            args.shot_keys = argv[++i];
        } else if (std::strcmp(argv[i], "--shot-options-sequence") == 0 && i + 1 < argc) {
            args.shot_options_sequence = argv[++i];
        } else if (std::strcmp(argv[i], "--shot-row") == 0 && i + 1 < argc) {
            args.shot_row = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--shot-size") == 0 && i + 1 < argc) {
            std::sscanf(argv[++i], "%dx%d", &args.shot_w, &args.shot_h);
        } else if (std::strcmp(argv[i], "--renderer") == 0 && i + 1 < argc) {
            args.renderer = argv[++i];
        } else if (std::strcmp(argv[i], "--enhanced-graphics") == 0) {
            args.enhanced = true;
        } else if (std::strcmp(argv[i], "--no-snap-to-grid") == 0) {
            args.unsnapped = true;
        } else if (std::strcmp(argv[i], "--shot-native-ui") == 0) {
            args.shot_native_ui = true;
        } else if (std::strcmp(argv[i], "--shot-badges") == 0) {
            args.shot_badges = true;
        } else if (std::strcmp(argv[i], "--shot-mask") == 0 && i + 1 < argc) {
            args.shot_mask = argv[++i];
        } else if (std::strcmp(argv[i], "--shot-state") == 0 && i + 1 < argc) {
            args.shot_state = argv[++i];
        } else if (std::strcmp(argv[i], "--shot-state-frames") == 0) {
            args.shot_state_frames = true;
        } else if (std::strcmp(argv[i], "--shot-map") == 0 && i + 1 < argc) {
            std::sscanf(argv[++i], "%d,%d,%d", &args.shot_map, &args.shot_x, &args.shot_y);
        } else if (std::strcmp(argv[i], "--shot-field-party") == 0) {
            args.shot_field_party = true;
        } else if (std::strcmp(argv[i], "--shot-field-walk") == 0) {
            args.shot_field_walk = true;
        } else if (std::strcmp(argv[i], "--shot-audit-only") == 0) {
            args.shot_audit_only = true;
        } else if (std::strcmp(argv[i], "--shot-timing-only") == 0) {
            args.shot_timing_only = true;
        } else if (std::strcmp(argv[i], "--shot-field-npcs") == 0) {
            args.shot_field_npcs = true;
        } else if (std::strcmp(argv[i], "--shot-native-npcs") == 0) {
            args.shot_native_npcs = true;
        } else if (std::strcmp(argv[i], "--shot-mode7-flat") == 0) {
            args.shot_mode7_flat = true;
        } else if (std::strcmp(argv[i], "--shot-mode7-steps") == 0) {
            args.shot_mode7_steps = true;
        } else if (std::strcmp(argv[i], "--shot-mode7-no-walk") == 0) {
            args.shot_mode7_no_walk = true;
        } else if (std::strcmp(argv[i], "--shot-mode7-entry") == 0) {
            args.shot_mode7_steps = args.shot_mode7_entry = true;
        } else if (std::strcmp(argv[i], "--shot-mode7-north") == 0) {
            args.shot_mode7_north = true;
        } else if (std::strcmp(argv[i], "--shot-fidelity-steps") == 0) {
            args.shot_fidelity_steps = true;
        } else if (std::strcmp(argv[i], "--shot-map-batch") == 0 && i + 1 < argc) {
            args.shot_map_batch = argv[++i];
        } else if (std::strcmp(argv[i], "--shot-effect-frame") == 0 && i + 1 < argc) {
            args.shot_effect_frame = std::max(0, std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--shot-benchmark") == 0 && i + 1 < argc) {
            args.shot_benchmark = std::clamp(std::atoi(argv[++i]), 0, 1000);
        } else if (std::strcmp(argv[i], "--identity-shader") == 0) {
            args.identity_shader = true;
        } else if (std::strcmp(argv[i], "--shot-gb-weight") == 0 && i + 1 < argc) {
            args.shot_gb_weight = std::clamp(std::atoi(argv[++i]), 0, 256);
        } else if (std::strcmp(argv[i], "--shot-run") == 0 && i + 1 < argc) {
            args.shot_run = std::clamp(std::atoi(argv[++i]), 0, 10'000'000);
        } else if (std::strcmp(argv[i], "--shot-soak") == 0 && i + 1 < argc) {
            args.shot_soak = argv[++i];
        } else if (std::strcmp(argv[i], "--shot-options-file") == 0 && i + 1 < argc) {
            std::ifstream file(argv[++i]);
            std::getline(file, args.shot_options_sequence);
        } else if (std::strcmp(argv[i], "--shot-start-level") == 0 && i + 1 < argc) {
            args.shot_start_level = std::clamp(std::atoi(argv[++i]), 0, 2);
        } else if (std::strcmp(argv[i], "--shot-capture") == 0 && i + 1 < argc) {
            args.shot_captures.push_back(size_t(std::max(0, std::atoi(argv[++i]))));
        } else if (std::strcmp(argv[i], "--shot-render-reset") == 0 && i + 1 < argc) {
            const std::string value = argv[++i];
            const auto colon = value.find(':');
            const std::string type = colon == std::string::npos ? "" : value.substr(colon + 1);
            if (type != "targets" && type != "device" && type != "lost") {
                args.shot_render_reset_error = "--shot-render-reset requires VIEW:targets|device|lost";
                break;
            }
            args.shot_render_resets.emplace_back(size_t(std::max(0, std::atoi(value.c_str()))),
                                                 type == "targets"  ? SDL_EVENT_RENDER_TARGETS_RESET
                                                 : type == "device" ? SDL_EVENT_RENDER_DEVICE_RESET
                                                                    : SDL_EVENT_RENDER_DEVICE_LOST);
        } else if (std::strcmp(argv[i], "--shot-hold") == 0 && i + 1 < argc) {
            unsigned address = 0, value = 0;
            if (std::sscanf(argv[++i], "%x=%x", &address, &value) == 2 && address < 0x10000 && value < 256)
                args.shot_holds.emplace_back(address, value);
        } else if (std::strcmp(argv[i], "--shot-camera") == 0 && i + 1 < argc) {
            auto &c = args.shot_camera; // degrees, degrees, multiplier, multiplier
            std::sscanf(argv[++i], "%f,%f,%f,%f", &c.tilt, &c.lens, &c.distance, &c.field_zoom);
        } else if (std::strcmp(argv[i], "--shot-step") == 0 && i + 1 < argc) {
            unsigned long long frame = 0;
            int direction = 0;
            if (std::sscanf(argv[++i], "%llu:%d", &frame, &direction) == 2)
                args.shot_steps.emplace_back(frame, direction);
        } else if (std::strcmp(argv[i], "--shot-store") == 0 && i + 1 < argc) {
            args.shot_store = argv[++i];
        } else if (std::strcmp(argv[i], "--shot-poke") == 0 && i + 1 < argc) {
            LaunchArgs::ShotPoke poke{};
            if (std::sscanf(argv[++i], "%x=%x@%u", &poke.address, &poke.value, &poke.frame) == 3 &&
                poke.address < 0x10000 && poke.value < 256)
                args.shot_pokes.push_back(poke);
        }
    }
    return args;
}

int check_shot_args(LaunchArgs &args) {
    if (args.shot_ff_review &&
        (args.shot.empty() || args.shot_run <= 0 || args.shot_display_hz <= 0 || args.shot_map >= 0)) {
        std::fprintf(stderr,
                     "sawgod: --shot-ff-review needs --shot-overlay, --shot-run and --shot-display-hz\n");
        return 1;
    }
    if (args.shot_fidelity_steps || args.shot_mode7_steps || args.shot_field_walk) {
        if (args.shot.empty() || args.shot_map < 0 || !args.shot_map_batch.empty() ||
            args.shot_page != "none" || (args.shot_fidelity_steps && args.shot_mode7_steps)) {
            std::fprintf(
                stderr,
                "sawgod: --shot-fidelity-steps/--shot-mode7-steps requires --shot-overlay PREFIX --shot-map "
                "ID,X,Y --shot-page none\n");
            return 1;
        }
        for (int frame = 0; frame <= (args.shot_field_walk ? 48 : 180); ++frame) {
            char suffix[32];
            std::snprintf(suffix, sizeof(suffix), "_%03d.png", frame);
            args.map_shots.push_back({args.shot_map, args.shot_x, args.shot_y, args.shot + suffix});
        }
    }
    if (!args.shot_map_batch.empty()) {
        if (!args.shot.empty() || args.shot_map >= 0 || !args.shot_state.empty() || !args.shot_mask.empty() ||
            !args.shot_pokes.empty() || !args.shot_keys.empty() || args.frames.value_or(0) != 0) {
            std::fprintf(
                stderr,
                "sawgod: --shot-map-batch cannot combine with individual shot/state/input fixtures\n");
            return 1;
        }
        std::ifstream input(args.shot_map_batch);
        std::string line;
        unsigned row = 0;
        while (std::getline(input, line)) {
            ++row;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty() || line[0] == '#')
                continue;
            LaunchArgs::MapShot shot{};
            int end = 0;
            if (std::sscanf(line.c_str(), "%d,%d,%d,%n", &shot.map, &shot.x, &shot.y, &end) != 3 ||
                end == 0 || size_t(end) == line.size() || shot.map < 1 ||
                shot.map >= sawgod::game::kMapCount || shot.x < 0 || shot.x > 50 || shot.y < 0 ||
                shot.y > 55) {
                std::fprintf(stderr, "sawgod: invalid map,x,y,output row %u in %s\n", row,
                             args.shot_map_batch.c_str());
                return 1;
            }
            shot.output =
                (std::filesystem::path(args.shot_map_batch).parent_path() / line.substr(size_t(end)))
                    .string();
            args.map_shots.push_back(std::move(shot));
        }
        if (!input.eof() || args.map_shots.empty()) {
            std::fprintf(stderr, "sawgod: empty/unreadable --shot-map-batch %s\n",
                         args.shot_map_batch.c_str());
            return 1;
        }
        args.shot = args.map_shots.front().output;
        args.shot_page = "none";
    }
    if (!args.renderer.empty() && args.renderer != "gpu" && args.renderer != "legacy") {
        std::fprintf(stderr, "sawgod: --renderer must be gpu or legacy\n");
        return 1;
    }
    if (args.shot.empty() && args.live_picture_log.empty() &&
        (args.shot_gb_weight >= 0 || !args.shot_store.empty() || !args.shot_pokes.empty())) {
        std::fprintf(stderr, "sawgod: --shot-gb-weight/--shot-store/--shot-poke require --shot-overlay\n");
        return 1;
    }
    return 0;
}

} // namespace sawgod::app
