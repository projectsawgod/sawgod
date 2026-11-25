// SawGod front end: runs the native game on the WSC runtime (src/emu) in a window, with the options
// overlay (src/engine/overlay.h).
//
//   sawgod [--rom PATH] [--assets ARCHIVE] [--mod MOD]... [--english FILE] [--any-rom]
//   [--scale N]
//   [--frames N] [--native all|none|NAME] [--record FILE] [--sram FILE] [--no-save] [--config FILE]
//   [--inputs FILE] [--shot-overlay FILE [--shot-page NAME|none] [--shot-row N] [--shot-keys KEYS]
//   [--shot-size WxH]]
//
// The ROM path defaults to $SAWGOD_ROM; its SHA-1 must be the supported ROM's (--any-rom skips the check).
// Without either, the game uses its stored copy (<data>/rom/; app/first_run.h), or shows the ROM screen,
// which stores the player's ROM and builds the resource archives (<data>/wsc.sgz, gb.sgz) before the title.
// <data> is engine/data_dir.h's folder: $SAWGOD_HOME, else beside the game (portable), else the OS folder.
// --assets loads a byte archive (tools/assets.py archive; an import path for ROM patches); ordinary
// resources come from .sgz.
// --mod MOD (repeatable): a mod, a .sgz or a folder of override files (docs/MODS.md); every mod in
// <data>/mods/ loads too, after these, by name. (A .gsa byte pack still loads, for imports.)
// --english FILE overrides the English text resource (key,text CSV; game/text.h). Otherwise the app uses
// english.csv in <data> if present, then the bundled text/en.csv and any development en_fill.csv.
// The Language setting (Gameplay; English by default) switches between it
// and the original Japanese at once, on the native code. Sound plays through the default audio device (the
// headphone mix). The GB music beside the WSC's (app/gb_music.h), cross-faded with the GB picture, comes from
// the GB game on the cartridge (ORIJINARU); no GB ROM is needed. The window starts at 5x the WSC screen (or
// its last size), resizes freely and keeps the screen's shape; the picture fills it with crisp pixels
// (nearest-neighbor at whole scales, sharp bilinear otherwise). Keys: arrows = X pad (the game's d-pad),
// W/A/S/D = Y pad, X = A, Z = B, Enter = Start; gamepads (SDL's controller database), by button position:
// D-pad + left stick = X pad, right stick = Y pad, south face button = A, east = B, Start or north = Start
// (Back/Select is unused). Esc or F1 (the pad's Select button) opens the options menu, which pauses the
// game (the music keeps playing); resetting and quitting are in its System page (or close the window).
// G/H or L1/R1 step Swap down/up in every game context; Tab/R2 holds Fast-Forward.
// Alt+Enter toggles fullscreen. Settings live in <data>/sawgod.toml
// (docs/CONFIG.md), or --config FILE;
// --scale (initial window size) overrides it for one run without changing the file.
// Saves live in that same data directory, one readable file per slot in saves/ (the save
// store, docs/SAVES.md). --no-save starts with no saves
// and writes none. --sram FILE (or --native none) plays from a raw WSC SRAM image instead, as the original
// stores saves (needed to replay a --record movie made from an existing save); --native none writes it back
// to <rom name>.sav, --sram never does.
// The game runs as native code: every decompiled routine installed (--native all, the default). --native none
// runs the original ROM code in the interpreter instead, --native NAME installs one routine (as
// sawgod-headless's --native).
// --frame-picture-log CSV: opt-in final RGB readbacks plus measured ground/party motion.
// GPU synchronization adds diagnostic latency; validity/error columns identify unreliable patches.
// --record writes the player's held-button changes to FILE in the same "FRAME buttons" text format as
// tests/movies/*.txt / sawgod-headless --inputs, so a session can be replayed later.
// --shot-overlay FILE runs --frames N frames (default 0) as fast as possible, opens the options menu on
// --shot-page (a category name or index, default the first; "none": no menu) with the cursor on --shot-row
// N (default: on the category list), writes the window's picture (its size, or --shot-size WxH) to FILE as
// PNG and exits; --shot-keys presses menu keys first, one per menu frame (u d l r: directions, a: A, b: B).
// Works without a display (SDL_VIDEODRIVER=dummy). --inputs FILE plays "FRAME buttons" changes
// (sawgod-headless --inputs format) instead of live input, e.g. to put a real game scene behind the menu.
// --live-picture-log CSV --shot-display-hz HZ --frames N [--live-warmup TICKS]: run the ordinary
// main loop on a fixed clock, hash every final RGB picture (including startup), and time CPU work.
// Warmup advances native ticks before presenting; audio submission and late-frame catch-up are disabled.
// Existing --inputs/--shot-store/--shot-poke fixtures apply; --shot-step uses live presentation indices.
// --shot-map-batch FILE: one map,x,y,output.png per line; paths relative to FILE.
// Reuses one ROM/session/renderer for every halted field view. Blank/# lines ignored.
// Uses --shot-size, --shot-effect-frame and the same settings as --shot-map; no menu.
// --shot-run N (with --shot-overlay PREFIX --shot-page none): after --frames, N more game frames, each
// written to PREFIX_NNN.png, on a simulated 75.47 Hz clock (real game transitions through the live camera).
// --shot-step FRAME:DIR (repeatable): a fidelity step down (-1) or up (1) at that game frame of the run.
// --shot-soak CSV: full RGB hashes each view, RAM/SRAM every 300 movie frames and at the end;
// includes the live debug watches. --shot-capture VIEW retains one PNG for failure replay. Steps use
// presentation view indices in this mode, advancing through Options pauses. --shot-hold HEX=HEX
// replays a movie hold before each game frame. Options use --shot-options-sequence as in review runs.
// --shot-camera TILT,LENS,DISTANCE,ZOOM: another camera than the fixed one for that shot (16,24,1.5,1.5).
#include <SDL3/SDL.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "app/audio_latency.h"
#include "app/audio_output.h"
#include "app/audio_review.h"
#include "app/battle_wipe.h"
#include "app/button_prompts.h"
#include "app/debug_watch.h"
#include "app/display_motion.h"
#include "app/enhanced_music.h"
#include "app/field_motion.h"
#include "app/first_run.h"
#include "app/frame_audit.h"
#include "app/frame_timing.h"
#include "app/game_resources.h"
#include "app/gb_music.h"
#include "app/host_settings.h"
#include "app/input_movie.h"
#include "app/launch_args.h"
#include "app/live_picture_check.h"
#include "app/minimap.h"
#include "app/mode7_renderer.h"
#include "app/picture_draw.h"
#include "app/picture_motion.h"
#include "app/present_clock.h"
#include "app/save_setup.h"
#include "app/shot_fixture.h"
#include "app/shot_report.h"
#include "app/shot_soak.h"
#include "app/sound_mix.h"
#include "app/splash.h"
#include "app/startup.h"
#include "engine/startup_timing.h"
#ifdef __ANDROID__
#include <jni.h>
#endif
#include "app/title_logo_overlay.h"
#include "app/ui_screens.h"
#include "app/world_card_overlay.h"
#include "emu/png.h"
#include "emu/wsc.h"
#include "engine/assets.h"
#include "engine/config.h"
#include "engine/cpu_scope.h"
#include "engine/data_dir.h"
#include "engine/debug_log.h"
#include "engine/enhanced_graphics.h"
#include "engine/fast_forward_vfx.h"
#include "engine/fidelity.h"
#include "engine/focus_pass.h"
#include "engine/gpu_upload_work.h"
#include "engine/hd_menus.h"
#include "engine/input.h"
#include "engine/menu_fill.h"
#include "engine/mod_pack.h"
#include "engine/overlay.h"
#include "engine/persist.h"
#include "engine/pixel_upload.h"
#include "engine/presentation_diagnostics.h"
#include "engine/presentation_renderer.h"
#include "engine/screen.h"
#include "engine/settings.h"
#include "engine/sgz.h"
#include "engine/version.h"
#include "game/autosave.h"
#include "game/cheats.h"
#include "game/game_config.h"
#include "game/gfx_swap.h"
#include "game/hooks.h"
#include "game/host_picture.h"
#include "game/info.h"
#include "game/machine.h"
#include "game/maps.h"
#include "game/mode7.h"
#include "game/port_policy.h"
#include "game/presentation_scene.h"
#include "game/resources.h"
#include "game/save_file.h"
#include "game/session.h"
#include "game/setup.h"
#include "game/tables.h"
#include "game/terrain_veil.h"
#include "game/text_render.h"
#include "tools/field_preview.h"

#ifdef SAWGOD_GPU_TIMESTAMPS
extern "C" bool SawGodRendererTimingStart(SDL_Renderer *, Uint64);
extern "C" bool SawGodRendererTimingPoll(SDL_Renderer *, Uint64 *, double *);
#endif
namespace {

using sawgod::emu::Wsc;

// The data folder (engine/data_dir.h): config, saves, the ROM copy, archives and mods.
std::string PrefDir() { return sawgod::data_dir(); }

using sawgod::app::BlurredBackdrop;
using sawgod::app::GameRect;
using sawgod::app::LcdGrid;
using sawgod::app::MenuRect;
using sawgod::app::Rect;
using sawgod::app::SharpScaler;
using sawgod::app::ZoomBackdrop;

} // namespace

#ifdef __ANDROID__
#include <SDL3/SDL_main.h>
#endif

int main(int argc, char **argv) {
#ifdef __ANDROID__
    auto *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    jclass cls = env->GetObjectClass(activity);
    auto startup_time = [&](const char *field) {
        return double(env->GetLongField(activity, env->GetFieldID(cls, field, "J"))) / 1e6;
    };
    sawgod::startup::platform_origin(startup_time("processStartNs"));
    sawgod::startup::platform_stage("process start", startup_time("processStartNs"), 0);
    sawgod::startup::platform_stage("activity start", startup_time("activityStartNs"), 0);
    sawgod::startup::platform_stage("Android bundled-asset install", startup_time("installEndNs"),
                                    startup_time("installEndNs") - startup_time("installBeginNs"));
    sawgod::startup::platform_stage("native library load", startup_time("libraryEndNs"),
                                    startup_time("libraryEndNs") - startup_time("libraryBeginNs"));
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
#endif
    sawgod::startup::begin();
    sawgod::app::LaunchArgs args = sawgod::app::parse_launch_args(argc, argv);
    if (!args.shot_render_reset_error.empty()) {
        std::fprintf(stderr, "sawgod: %s\n", args.shot_render_reset_error.c_str());
        return 2;
    }
    if (!args.audio_review_error.empty() || (args.audio_review_options && args.audio_review.empty())) {
        std::fprintf(stderr, "sawgod: %s\n",
                     args.audio_review_error.empty() ? "--audio-review-* options require --audio-review DIR"
                                                     : args.audio_review_error.c_str());
        return 1;
    }
    sawgod::app::FrameAuditLog frame_audit(SDL_getenv("SAWGOD_SCENE_AUDIT"));
    if (args.shot_audit_only &&
        (!frame_audit.enabled() || args.shot_run <= 0 || !args.shot_timing_only || args.shot_page != "none" ||
         !args.shot_soak.empty() || !args.shot_options_sequence.empty() || args.shot_map >= 0 ||
         args.shot_display_hz || !args.shot_state.empty() || args.shot_benchmark)) {
        std::fprintf(stderr, "sawgod: --shot-audit-only requires SAWGOD_SCENE_AUDIT, --shot-run, "
                             "--shot-timing-only and --shot-page none, without other shot fixtures\n");
        return 1;
    }
    sawgod::app::FrameTimingLog timing_log;
    std::string timing_path = args.frame_timing_log;
    if (timing_path.empty() && args.shot.empty())
        if (const char *path = SDL_getenv("SAWGOD_FRAME_LOG"))
            timing_path = path;
    if (!timing_log.open(timing_path)) {
        std::fprintf(stderr, "sawgod: cannot open frame timing log %s\n", timing_path.c_str());
#ifdef __ANDROID__ // a test build's log (files/frame-timing): play on without it
        SDL_Log("frame timing log %s: %s", timing_path.c_str(), std::strerror(errno));
#else
        return 1;
#endif
    }
    if (const int code = sawgod::app::check_shot_args(args))
        return code;
    const bool live_check = !args.live_picture_log.empty();
    if (live_check && (!args.shot.empty() || !args.frames || !args.shot_display_hz)) {
        std::fprintf(stderr,
                     "--live-picture-log requires --frames and --shot-display-hz, without --shot-overlay\n");
        return 2;
    }
    sawgod::app::LivePictureCheck live_pictures(args.live_picture_log);
    using sawgod::Pacing;

    // The ROM, if any; its footer says how much battery RAM the cartridge has.
    std::unique_ptr<Wsc> wsc;
    std::string english_text; // the English text resource (empty: none)
    // its pack's graphics (<pack>/graphics.gsa next to <pack>/text/strings.csv; e.g. Tower Reversed's redrawn
    // opening): laid over the ROM image while English is on, the original bytes kept to put back
    std::optional<sawgod::AssetArchive> english_graphics;
    sawgod::game::hooks::Subscription hide_orijinaru, party_menu_rows, party_menu_action;
    bool party_options_requested = false;
    sawgod::game::Cheats cheats;          // Options > Modifiers (game/cheats.h)
    sawgod::game::GameConfig game_config; // the game's Config tab in Options > Gameplay (game/game_config.h)
    sawgod::game::Autosave autosave;      // Options > Gameplay > Autosave (game/autosave.h)
    sawgod::app::UiScreens ui;            // the host screens (app/ui_screens.h)
    sawgod::game::PresentationScene held_field; // the field under Inn/Guild/Shop windows
    sawgod::app::BattleSpan battle_span;        // Battle Speed: in a battle (app/host_settings.h)
    sawgod::app::GbMusic gb_music;              // the GB music beside the WSC's (app/gb_music.h)
    std::string save_path;
    // First run (app/first_run.h): without --rom / $SAWGOD_ROM, the stored ROM copy, else the ROM screen (an
    // ordinary launch only: scripted runs without a ROM (--frames, screenshots, reviews) have no game, as
    // before).
    const std::string exe_dir = [] {
#ifdef __ANDROID__
        return PrefDir(); // Authored APK assets are installed here by SawGodActivity.
#else
        const char *base = SDL_GetBasePath();
        return base ? std::string(base) : std::string();
#endif
    }();
    const std::string gb_maps_dir = exe_dir + "gb_maps"; // assets/gb_maps, copied beside the app
    if (args.rom.empty() && !PrefDir().empty()) {
        args.rom = sawgod::app::stored_rom(PrefDir());
        if (args.rom.empty() && args.shot.empty() && args.audio_review.empty() && !args.frames) {
            args.rom = sawgod::app::run_rom_screen(PrefDir(), exe_dir, gb_maps_dir);
            if (args.rom.empty())
                return 0; // the window was closed
        }
    }
    if (!args.rom.empty()) {
        sawgod::app::LoadedRom loaded;
        if (const int code = sawgod::app::load_rom(args, exe_dir, gb_maps_dir, loaded))
            return code;
        std::vector<uint8_t> rom = std::move(loaded.rom);
        english_text = std::move(loaded.english_text);
        english_graphics = std::move(loaded.english_graphics);
        const size_t sram_size = sawgod::game::cartridge_sram_size(rom);
        gb_music.load(rom); // the GB music from the GB game on the cartridge (ORIJINARU)
        wsc = std::make_unique<Wsc>(std::move(rom), sram_size);
        // ORIJINARU hidden from the title menu (game/port_policy.h). Natives only: the
        // interpreter runs the original menu.
        hide_orijinaru = sawgod::game::hide_orijinaru(sawgod::game::Session::of(*wsc));
        ui.install(*wsc);
        auto &party_hooks = sawgod::game::Session::of(*wsc).hooks();
        party_menu_rows = party_hooks.subscribe(
            sawgod::game::hooks::Value::PartyMenuRows,
            [](sawgod::game::Machine &, int32_t, const sawgod::game::hooks::Args &) { return 2; });
        party_menu_action =
            party_hooks.subscribe(sawgod::game::hooks::Event::PartyMenuAction,
                                  [&](sawgod::game::Machine &, const sawgod::game::hooks::Args &a) {
                                      if (a.a == 0)
                                          party_options_requested = true;
                                  });
        battle_span.install(*wsc);
        if (gb_music.loaded())
            gb_music.attach(sawgod::game::Session::of(*wsc));
    }

    sawgod::app::start_debug_log(args);
    sawgod::startup::flush();
    struct DebugLogSession { // every return from here closes the session's file
        ~DebugLogSession() { sawgod::debug_log::stop(); }
    } debug_log_session;
    sawgod::app::DebugWatch debug_watch;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        sawgod::debug_log::criticalf("start", "SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    // Settings: registered defaults include Swap fidelity; the file holds persistent user choices.
    sawgod::Config config;
    sawgod::Settings settings;
    settings.register_all(config, args.touch_controls);
    const std::string config_path = sawgod::app::config_file(args);
    {
        sawgod::startup::Stage timing("config load");
        sawgod::app::load_settings(args, config_path, config, settings);
    }
    auto save_config = [&] {
        if (!config.dirty() || config_path.empty() || !args.shot.empty() || live_check)
            return;
        std::string error;
        if (!config.save(config_path, &error))
            sawgod::debug_log::errorf("config", "%s", error.c_str());
    };

    if (wsc && !args.native.empty())
        sawgod::game::install(*wsc, args.native == "all" ? nullptr : args.native.c_str());
    std::string soundfont = config.string_value("audio.soundfont", sawgod::app::default_soundfont());
    if (!config.loaded("audio.soundfont"))
        config.set_string("audio.soundfont", soundfont == exe_dir + "soundfonts/GeneralUser-GS.sf2"
                                                 ? "soundfonts/GeneralUser-GS.sf2"
                                                 : soundfont);
    if (!soundfont.empty() && std::filesystem::path(soundfont).is_relative())
        soundfont = exe_dir + soundfont;
    sawgod::app::EnhancedObserver enhanced_observer;
    if (wsc)
        enhanced_observer.attach(sawgod::game::Session::of(*wsc));
    if (!args.audio_review.empty()) {
        if (!wsc)
            return 1;
        const int result = sawgod::app::audio_review(*wsc, gb_music, enhanced_observer, soundfont,
                                                     args.audio_review, args.audio_review_options);
        SDL_Quit();
        return result;
    }
    sawgod::startup::mark("SDL and game setup complete");
    sawgod::app::EnhancedAudio enhanced_audio(soundfont);
    // First run builds <data>/wsc.sgz and gb.sgz; folders/.sgz in mods/ override their files in load order.
    std::string base_text; // text/ja.csv with the mods' text/ja.csv over it (empty: none, the ROM's text)
    std::vector<uint8_t> gb_hand_png; // archive/mod art shared with Options
    sawgod::game::GfxSwap gfx_swap;   // the GB picture (game/gfx_swap.h): gb/ resources
    sawgod::Fidelity fidelity(settings.gfx(), settings.music());
    bool title_screen = false; // the title's Enhanced logo is showing
    const auto effective_screen = [&] {
        return sawgod::app::effective_screen(fidelity.graphics(), settings.screen(), title_screen);
    };
    // Screen Mode LCD: exact on the CPU picture (colors and smear) at WSC and GB; at Enhanced, whose picture
    // is drawn on the GPU, as a wash over it (lcd_wash).
    const auto lcd_on_cpu = [&] {
        return effective_screen() == sawgod::ScreenMode::Lcd &&
               fidelity.graphics() != sawgod::FidelityMode::Enhanced;
    };
    sawgod::app::FidelityButtons fidelity_buttons;
    // The Encounters button: Modifiers > Encounters to Off and back to what it was
    constexpr int kEncountersOff = 2;
    constexpr uint64_t kBadgeNs = 2'000'000'000; // "Encounters On" shows this long
    bool encounters_was_held = false;
    bool minimap_was_held = false;
    // Menu moves taken on a loop pass that waits instead of presenting: the open menu gets them next pass.
    sawgod::ui::MenuActions menu_carry;
    sawgod::app::MinimapFade minimap_fade;
    sawgod::app::MinimapSize minimap_shown = sawgod::app::MinimapSize::Small; // kept while it fades out
    int encounters_back = 0;                                                  // Normal
    uint64_t encounters_badge_until = 0;
    bool fast_forwarding = false;
    sawgod::game::SceneObserver scene_observer;
    if (wsc && !args.native.empty())
        scene_observer.install(*wsc);
    // Which native battle animation plays when (reference captures for enhancing them; observe only).
    sawgod::game::hooks::Subscription battle_animation_log;
    if (wsc && !args.native.empty())
        battle_animation_log = sawgod::game::Session::of(*wsc).hooks().subscribe(
            sawgod::game::hooks::Event::BattleAnimation,
            [](sawgod::game::Machine &m, const sawgod::game::hooks::Args &a) {
                SDL_Log("Battle animation: frame=%llu rom=%06X palette=%d item=%02X",
                        (unsigned long long)m.wsc().frame_count, a.a, a.b, a.c);
            });
    const bool capture_presentation = true; // Any session can step up to Enhanced.
    if (wsc)
        wsc->capture_layers(capture_presentation, false); // nothing in the app reads the GB picture's layers
    sawgod::game::HostPicture host_picture;
    sawgod::app::WorldCardOverlay world_card_overlay;
    world_card_overlay.preload();
    sawgod::app::TitleLogoOverlay title_logo_overlay;
    if (wsc && !args.native.empty())
        host_picture.install(*wsc, {&ui.battle, &ui.inventory, &ui.item_dialog, &ui.party, &ui.creation,
                                    &ui.bestiary, &ui.dialogue, &ui.service, &ui.save});
    std::array<uint8_t, Wsc::kWidth * Wsc::kHeight> host_ui{};
    sawgod::game::PresentationScene enhanced_scene;
    std::vector<uint32_t> enhanced_wsc, enhanced_gb;
    float enhanced_gb_weight = 0, enhanced_effect_weight = 0;
    std::optional<uint64_t> shot_fidelity_time;
    bool enhanced_frame = false;
    const char *audit_field_step = "None", *audit_world_step = "None";
    float audit_settled_zoom = 0;
    uint64_t audit_battle_start = 0, audit_battle_end = 0;
    uint64_t presented_frame = 0, production_end = 0, log_game_tick = 0, log_focus = 0;
    std::array<float, 2> presented_scroll{};
    int presented_map = -1;
    sawgod::game::Mode7Fade mode7_fade;
    sawgod::game::WorldTiltEntry world_tilt_entry;
    // Effects use a shorter, independent envelope; windows never change its target.
    sawgod::game::SceneEffectFade scene_effect_fade;
    bool camera_was_hidden = true;
    sawgod::game::FieldZoomEntry field_zoom_entry;
    sawgod::game::PresentationScene mode7_view;
    sawgod::game::Mode7Camera mode7_camera, completed_camera;
    sawgod::game::Mode7World mode7_source;
    sawgod::app::MotionHistory motion_history;
    // Opt-in display-motion trace: one T line per completed game tick, one P line per present.
    // Documented in docs/CONFIG.md; closed (and flushed) when the run ends.
    std::unique_ptr<std::FILE, int (*)(std::FILE *)> motion_trace_file(nullptr, std::fclose);
    if (const char *path = SDL_getenv("SAWGOD_MOTION_TRACE"); path && *path)
        motion_trace_file.reset(std::fopen(path, "w"));
    std::FILE *const motion_trace = motion_trace_file.get();
    sawgod::app::DisplayMotion display_motion;
    bool display_subpixel = false;
    std::array<float, 2> completed_scroll{};
    std::map<uint32_t, sawgod::app::MotionPoint> completed_actors;
    uint64_t motion_barrier = 0;
    std::vector<sawgod::game::hooks::Subscription> motion_hooks;
    if (wsc && !args.native.empty()) {
        using namespace sawgod::game::hooks;
        for (const auto event :
             {Event::PresentationPicture, Event::BattleStart, Event::BattleEnd, Event::Dialogue})
            motion_hooks.push_back(sawgod::game::Session::of(*wsc).hooks().subscribe(
                event, [&, event](sawgod::game::Machine &, const sawgod::game::hooks::Args &a) {
                    // Key repeats and the ordinary tile dispatch are not scene changes.
                    if ((event == Event::PresentationPicture && (a.a == 3 || a.a == 4)) ||
                        event == Event::BattleStart || event == Event::BattleEnd ||
                        (event == Event::Dialogue && (a.a == 0 || a.a == 1)))
                        ++motion_barrier;
                }));
    }
    if (frame_audit.enabled() && wsc && !args.native.empty()) {
        using namespace sawgod::game::hooks;
        motion_hooks.push_back(sawgod::game::Session::of(*wsc).hooks().subscribe(
            Event::BattleStart,
            [&](sawgod::game::Machine &, const sawgod::game::hooks::Args &) { ++audit_battle_start; }));
        motion_hooks.push_back(sawgod::game::Session::of(*wsc).hooks().subscribe(
            Event::BattleEnd,
            [&](sawgod::game::Machine &, const sawgod::game::hooks::Args &) { ++audit_battle_end; }));
    }
    const bool explicit_camera =
        std::any_of(argv, argv + argc, [](const char *a) { return std::strcmp(a, "--shot-camera") == 0; });
    float mode7_opacity = 1;
    float mode7_amount = 0;
    uint64_t log_describe = 0, log_m7_update = 0, log_m7_project = 0, log_m7_draw = 0,
             log_m7_effects = 0; // SAWGOD_FRAME_LOG
    uint64_t mode7_generation = ~0ull;
    bool mode7_supported = false, retained_camera_allowed = false;
    bool host_options_open = false;
    std::optional<uint64_t> simulated_present_time;
    std::array<uint32_t, Wsc::kWidth * Wsc::kHeight> mode7_ui{};
    sawgod::game::Mode7UiMask mode7_ui_cache;
    SDL_Renderer *renderer = nullptr; // initialized before any presentation collection
    Wsc::UiFill menu_fills;
    sawgod::MenuFill menu_fill;
    Wsc::UiText menu_text;
    sawgod::HdMenus hd_menus;
    sawgod::UiDrawCache menu_draw_inputs;
    uint64_t menu_cache_ns = 0;
    float menu_fill_weight = 0;
    sawgod::BattleTargetLight battle_target_light;
    uint64_t battle_light_generation = ~0ull;
    float ips_amount = 1 - 0.5f * fidelity.graphics_weights(SDL_GetTicksNS())[2];
    // The picture to show: the WSC's, the GB's, or the two blended mid-fade.
    // Changes only when the CPU picture/provenance producers run, never for display interpolation.
    uint64_t picture_revision = 0;
    auto game_rgb = [&] {
        ++picture_revision;
        if (args.shot.empty())
            sawgod::presentation_diagnostics::trace().begin(wsc->frame_count);
        sawgod::CpuScope cpu_scope("game_rgb", wsc->frame_count);
        if (settings.enhanced_field_zoom && !explicit_camera)
            args.shot_camera.field_zoom = float(settings.enhanced_field_zoom->get()) / 100;
        const auto weights = fidelity.graphics_weights(shot_fidelity_time.value_or(SDL_GetTicksNS()));
        const sawgod::app::PictureMix mix = sawgod::app::picture_mix(weights);
        uint32_t gb = mix.gb;
        enhanced_effect_weight = mix.enhanced;
        if (args.shot_gb_weight >= 0)
            gb = uint32_t(args.shot_gb_weight);
        ui.battle.set_enhanced_weight(mix.enhanced);
        if (auto &t = sawgod::presentation_diagnostics::trace(); t.enabled()) {
            t.range("state.ram", wsc->ram);
            t.range("state.sram", wsc->sram);
        }
        auto pictures = host_picture.collect();
        const auto picture = [&](bool second = false) {
            if (args.shot_native_ui && !args.shot.empty())
                return wsc->framebuffer_rgb(Wsc::ColorMode::Raw, second);
            return pictures.picture(*wsc, Wsc::ColorMode::Raw, second, false);
        };
        menu_fill_weight = mix.enhanced * (1 - float(gb) / 256);
        // Both IPS axes follow the same continuous Enhanced/WSC/GB presentation fade.
        ips_amount = 1 - 0.5f * weights[2];
        enhanced_frame = false;
        if (capture_presentation) {
            host_ui.fill(0);
            wsc->ui_coverage = &host_ui;
            menu_text.clear();
            wsc->ui_text = &menu_text;
            menu_fills.clear();
            wsc->ui_fill = &menu_fills;
            enhanced_wsc = picture();
            if (!host_picture.holding()) {
                presented_frame = wsc->frame_count;
                const auto &raster = sawgod::game::Session::of(*wsc).raster_field(*wsc);
                presented_map = raster.ram[0xD0DC];
                presented_scroll = sawgod::game::field_raster_scroll(raster);
                // Inn, Guild and Shop are windows over the field, like dialogue: the Enhanced field camera
                // stays where it is under them.
                const bool service_over_field = ui.service.active() && host_picture.shown() == 4;
                const bool full_ui =
                    host_picture.native_full_screen(*wsc) || (host_picture.full_ui() && !service_over_field);
                const bool windows_over_scene = ui.dialogue.active() || service_over_field;
                const uint64_t describe_start = SDL_GetTicksNS();
                enhanced_scene = scene_observer.describe(*wsc, ui.battle.active(), full_ui,
                                                         !args.shot_native_ui &&
                                                             (windows_over_scene || ui.battle.active()));
                // The service screens rebuild the native picture, so the field is no longer recognized
                // under them: keep the field as it was when the menu opened, so the camera holds still.
                if (service_over_field &&
                    enhanced_scene.kind != sawgod::game::PresentationScene::Kind::Field &&
                    held_field.kind == sawgod::game::PresentationScene::Kind::Field &&
                    held_field.map == enhanced_scene.map && !sawgod::game::recognized_field(*wsc)) {
                    enhanced_scene = held_field;
                    enhanced_scene.perspective_allowed = false;
                } else if (!service_over_field) {
                    if (enhanced_scene.kind == sawgod::game::PresentationScene::Kind::Field)
                        held_field = enhanced_scene;
                    else
                        held_field.kind = sawgod::game::PresentationScene::Kind::Unknown;
                }
                log_describe += SDL_GetTicksNS() - describe_start;
                // See-through terrain: a steady half-opacity lower body instead of the native flicker.
                if (enhanced_scene.kind == sawgod::game::PresentationScene::Kind::Field &&
                    !ui.battle.active())
                    sawgod::game::blend_terrain_veil(*wsc, enhanced_wsc, &host_ui);
                const uint64_t now = shot_fidelity_time.value_or(SDL_GetTicksNS());
                const auto generation = sawgod::game::Session::of(*wsc).generation();
                if (generation != mode7_generation) {
                    mode7_generation = generation;
                    mode7_fade = {};
                    scene_effect_fade.settle_next();
                    camera_was_hidden = true;
                    field_zoom_entry = {};
                    world_tilt_entry = {};
                    mode7_view = {};
                    mode7_source = {};
                }
                // Both Enhanced cameras stay put under screen-space windows: dialogue and service screens are
                // composed over the field or world as host UI.
                const bool zoom_layout = full_ui || ui.battle.active() ||
                                         sawgod::game::presented_battle_entry_wipe(*wsc) ||
                                         (!args.shot.empty() && args.shot_mode7_flat) || !mode7_supported;
                const bool camera_layout = zoom_layout || !settings.enhanced_mode7->on();
                const bool field_scene = enhanced_scene.camera_field();
                const bool enhanced_level = fidelity.graphics() == sawgod::FidelityMode::Enhanced;
                const bool zoom_field =
                    sawgod::game::field_zoom_allowed(enhanced_scene.map, sawgod::FidelityMode::Enhanced,
                                                     enhanced_scene.camera_field(), zoom_layout);
                bool requested = sawgod::game::mode7_allowed(enhanced_scene.map, fidelity.graphics(),
                                                             field_scene, camera_layout) ||
                                 (zoom_field && enhanced_level);
                using ZoomStep = sawgod::game::FieldZoomEntry::Step;
                const bool camera_hidden =
                    full_ui || sawgod::game::presented_battle_entry_wipe(*wsc) ||
                    enhanced_scene.transition == sawgod::game::PresentationScene::Transition::Uniform ||
                    enhanced_scene.transition ==
                        sawgod::game::PresentationScene::Transition::RasterUnavailable;
                const bool hidden_entry =
                    camera_hidden || camera_was_hidden || wsc->ram[0xD519] || mode7_source.map() < 0;
                camera_was_hidden = camera_hidden;
                const auto field_step = field_zoom_entry.step(
                    enhanced_scene.map, zoom_field, enhanced_level, full_ui, mode7_source.map(),
                    mode7_source.map() >= 0 && !sawgod::game::GameConfig::mode7_map(mode7_source.map()),
                    mode7_fade.amount(now), hidden_entry);
                const bool world_field = sawgod::game::mode7_allowed(
                    enhanced_scene.map, sawgod::FidelityMode::Enhanced, field_scene, camera_layout);
                retained_camera_allowed = zoom_field || world_field;
                const auto world_step =
                    world_tilt_entry.step(enhanced_scene.map, world_field, enhanced_level, hidden_entry);
                if (frame_audit.enabled()) {
                    const auto name = [](ZoomStep step) {
                        return step == ZoomStep::Fade ? "Fade" : step == ZoomStep::Settle ? "Settle" : "Drop";
                    };
                    audit_field_step = name(field_step);
                    audit_world_step = name(world_step);
                    int width = 224, height = 144;
                    SDL_GetRenderOutputSize(renderer, &width, &height);
                    const auto dst = GameRect(args.shot_w > 0 ? args.shot_w : width,
                                              args.shot_h > 0 ? args.shot_h : height);
                    const auto settled = sawgod::game::field_zoom_camera(
                        args.shot_camera.field_zoom, 1, int(std::lround(dst.w)), int(std::lround(dst.h)),
                        mode7_source.scroll());
                    audit_settled_zoom = settled.output_scale[0] / settled.pixel_scale;
                }
                const auto zoom_step =
                    sawgod::game::GameConfig::mode7_map(enhanced_scene.map) ? world_step : field_step;
                if (zoom_step != ZoomStep::Fade)
                    mode7_fade.settle(zoom_step == ZoomStep::Settle);
                // Lower the old world before tilting a different one in. Never delay a game transition.
                else if (mode7_source.map() != enhanced_scene.map && mode7_fade.amount(now) > 0)
                    requested = false;
                mode7_amount = mode7_fade.update(requested, now);
                // A single still represents the settled camera, not its first instant.
                if (!args.shot.empty() && !args.shot_mode7_steps && !args.shot_fidelity_steps &&
                    !args.shot_run && requested)
                    mode7_amount = mode7_fade.update(true, now + sawgod::Fidelity::kFadeNs);
                mode7_source.set_motion(completed_scroll, completed_actors);
                const auto mode7_cpu_start = SDL_GetTicksNS();
                // This is also the retained Enhanced endpoint for a wipe that starts during a level
                // fade. Keep its field inputs current even while another Graphics level is shown.
                // A single still shows every light; played frames spread a whole screen of stale lit
                // chunks (about 20k light pixels each on glowing floors) over a few frames.
                mode7_source.light_pixel_budget = !args.shot.empty() && !args.shot_run ? 0 : 96 * 1024;
                // A service screen (Inn, Guild, shops, House of Healing) rebases both native screens to
                // scroll (8,8) as it opens (save_field_display_for_menu) but leaves the field's pending
                // scroll, so the live field reads as moved: by the whole ring offset on the first service
                // after a map entry, 8 px later, and its sprites no longer match their records and come back
                // as script sprites. Nothing on the field moves under the modal menu:
                // keep the world as it was when the menu opened.
                const bool service_held = ui.service.active() && mode7_source.map() == enhanced_scene.map;
                bool world_updated = false;
                if ((zoom_field || world_field) && !enhanced_scene.field_cleared && !service_held)
                    world_updated =
                        mode7_source.update(*wsc, enhanced_scene, scene_observer.light_tags(),
                                            scene_observer.landmark_tags(), !args.shot_native_npcs);
                // The Inn's stay fades the palettes under its screen: the held field fades with it.
                // Not an update: its sprites stay those of the tick the menu opened (world_sprites below).
                else if ((zoom_field || world_field) && !enhanced_scene.field_cleared && service_held)
                    mode7_source.refresh_palettes(*wsc);
                log_m7_update += SDL_GetTicksNS() - mode7_cpu_start;
                // The world's sprites stand in for the field's only on a tick it read them: otherwise a
                // sprite no record owns stays native pixels (held or cleared worlds do not draw it).
                mode7_ui = mode7_ui_cache.get(
                    *wsc, ui.battle.active(),
                    !args.shot_native_ui && (windows_over_scene || ui.battle.active()),
                    service_over_field && enhanced_scene.map == held_field.map, world_updated);
                completed_scroll = mode7_source.scroll();
                completed_actors.clear();
                for (const auto &sprite : mode7_source.sprites())
                    if (sprite.record)
                        completed_actors[sprite.record] = {sprite.x, sprite.y};
                sawgod::app::MotionState state;
                state.frame = presented_frame;
                state.generation = generation;
                state.barrier = motion_barrier;
                state.map = presented_map;
                state.fidelity = int(fidelity.graphics());
                state.owner = host_picture.shown();
                state.travel = raster.ram[0xD0D6];
                state.eligible = enhanced_level &&
                                 enhanced_scene.kind == sawgod::game::PresentationScene::Kind::Field &&
                                 mode7_source.map() == presented_map && enhanced_scene.perspective_allowed &&
                                 mode7_amount == 1 && !full_ui && !host_options_open && !windows_over_scene &&
                                 !ui.battle.active() && !raster.ram[0xD52C] && !raster.ram[0xD519];
                state.camera = completed_scroll;
                state.actors = completed_actors;
                sawgod::app::native_field_motion(raster, state);
                const bool trace_eligible = state.eligible;
                motion_history.complete(std::move(state));
                if (motion_trace)
                    std::fprintf(motion_trace, "T,%llu,%d,%d,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%d\n",
                                 (unsigned long long)presented_frame, presented_map, raster.ram[0xD0D6],
                                 double(completed_scroll[0]), double(completed_scroll[1]),
                                 int(int8_t(raster.ram[0xD515])), int(int8_t(raster.ram[0xD516])),
                                 raster.ram[0xD517], raster.ram[0xD519], raster.ram[0xD52C],
                                 int(trace_eligible), int(motion_history.snapping()));
                const uint64_t project_start = SDL_GetTicksNS();
                if (mode7_amount > 0) {
                    mode7_camera = {args.shot_camera.tilt * mode7_amount, args.shot_camera.lens, mode7_amount,
                                    1 + (args.shot_camera.distance - 1) * mode7_amount};
                    if (!sawgod::game::GameConfig::mode7_map(mode7_source.map())) {
                        int out_w = 224, out_h = 144;
                        SDL_GetRenderOutputSize(renderer, &out_w, &out_h);
                        const auto destination = GameRect(args.shot_w > 0 ? args.shot_w : out_w,
                                                          args.shot_h > 0 ? args.shot_h : out_h);
                        mode7_camera = sawgod::game::field_zoom_camera(
                            args.shot_camera.field_zoom, mode7_amount, int(std::lround(destination.w)),
                            int(std::lround(destination.h)), mode7_source.scroll());
                    }
                    mode7_view = mode7_source.project(
                        mode7_camera, false,
                        args.shot_effect_frame >= 0 ? uint64_t(args.shot_effect_frame) : presented_frame);
                    if (mode7_source.surface().words.empty())
                        mode7_amount = 0;
                }
                completed_camera = mode7_camera;
                log_m7_project += SDL_GetTicksNS() - project_start;
                if (mode7_amount == 1 && SDL_getenv("SAWGOD_MODE7_TIMING"))
                    SDL_Log("Mode7 CPU us: %.3f", double(SDL_GetTicksNS() - mode7_cpu_start) / 1000);
                // Geometry replaces the same world's flat picture. Only a different scene needs an underlay
                // fade.
                mode7_opacity = mode7_source.map() == enhanced_scene.map && !ui.battle.active() && !full_ui
                                    ? 1.0f
                                    : mode7_amount;
            } else if (mode7_source.map() == enhanced_scene.map) {
                // Holding pixels pauses the scene, never the Graphics camera clock in either direction.
                const bool enhanced = fidelity.graphics() == sawgod::FidelityMode::Enhanced;
                mode7_amount = mode7_fade.update(enhanced && retained_camera_allowed,
                                                 shot_fidelity_time.value_or(SDL_GetTicksNS()));
            }
            if (host_picture.holding() && !enhanced_wsc.empty() &&
                std::all_of(enhanced_wsc.begin(), enhanced_wsc.end(),
                            [&](uint32_t color) { return color == enhanced_wsc.front(); }))
                enhanced_scene.hold_uniform_picture();
            wsc->ui_coverage = nullptr;
            wsc->ui_fill = nullptr;
            wsc->ui_text = nullptr;
            enhanced_effect_weight *=
                scene_effect_fade.update(enhanced_scene, shot_fidelity_time.value_or(SDL_GetTicksNS()));
            enhanced_gb = wsc->second() && gb > 0 ? picture(true) : enhanced_wsc;
            enhanced_gb_weight = wsc->second() ? float(gb) / 256 : 0;
            // The native defeat owns the complete raster, including white and the repurposed planes.
            // Retained host chrome still uses Enhanced styling; scene postprocessing must not cover it.
            enhanced_frame = enhanced_effect_weight > 0 && gb < 256 &&
                             enhanced_scene.kind != sawgod::game::PresentationScene::Kind::Unknown &&
                             !enhanced_scene.field_cleared && !host_picture.native_battle_effect();
            if (gb == 0 || !wsc->second())
                return enhanced_wsc;
            if (gb == 256)
                return enhanced_gb;
            auto a = enhanced_wsc;
            sawgod::app::blend_gb_picture(a, enhanced_gb, gb);
            return a;
        }
        if (!wsc->second() || gb == 0)
            return picture();
        if (gb == 256)
            return picture(true);
        std::vector<uint32_t> a = picture();
        const std::vector<uint32_t> b = picture(true);
        sawgod::app::blend_gb_picture(a, b, gb);
        return a;
    };
    if (wsc && sawgod::game::Session::find(*wsc))
        sawgod::app::load_game_resources(*wsc, args, scene_observer, gfx_swap, base_text, english_text,
                                         gb_hand_png);
    // Language (see the header): the Session's text table, switched at once (the next string drawn) -- or,
    // in the middle of a dialogue that reads the current language's text, when it is done with it.
    bool language_pending = false;
    sawgod::app::GameLanguage language;
    auto set_language = [&] {
        if (wsc)
            language.apply(*wsc, settings.english(), base_text, english_text, english_graphics, gfx_swap);
    };
    set_language();

    // The save store (game/save_file.h, docs/SAVES.md): with the native game, every save slot is a readable
    // file in <user data>/saves/ and SRAM holds no save. Not with --native none (the original code
    // reads its SRAM) or --sram FILE (a raw SRAM image to replay a recording from): then the .sav below.
    const bool use_store = wsc && sawgod::game::Session::find(*wsc) && args.sram.empty();
    if (wsc && !use_store) {
        save_path = sawgod::app::raw_save_path(args.rom);
        sawgod::app::load_raw_save(*wsc, args, save_path);
    }

    SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE;
#ifdef __ANDROID__
    window_flags = SDL_WINDOW_FULLSCREEN;
    SDL_SetHint(SDL_HINT_GPU_DRIVER, "vulkan");
#endif
    if (settings.fullscreen->on() && args.shot.empty())
        window_flags |= SDL_WINDOW_FULLSCREEN;
    if (settings.borderless->on())
        window_flags |= SDL_WINDOW_BORDERLESS;
    SDL_Window *window =
        SDL_CreateWindow("SawGod", settings.window_width->get(), settings.window_height->get(), window_flags);
    if (!window) {
        sawgod::debug_log::criticalf("start", "SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    sawgod::app::set_window_icon(window);
    // freely resizable, keeping the WSC screen's shape; 2x is the smallest the menu stays legible at
    const float aspect = float(sawgod::kNativeWidth) / sawgod::kNativeHeight;
    SDL_SetWindowAspectRatio(window, aspect, aspect);
    SDL_SetWindowMinimumSize(window, sawgod::kNativeWidth * 2, sawgod::kNativeHeight * 2);

    sawgod::PresentationRenderer presentation;
    if (!presentation.init(window, settings.renderer->get() == 1, settings.identity_shader->on())) {
        sawgod::debug_log::criticalf("start", "Presentation renderer creation failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    sawgod::startup::mark("renderer ready");
    renderer = presentation.get();
    if (!world_card_overlay.prepare(renderer))
        sawgod::debug_log::warnf("gpu", "World card preparation failed: %s", SDL_GetError());
    hd_menus.prepare(renderer);
    if (!title_logo_overlay.prepare(renderer))
        sawgod::debug_log::warnf("gpu", "Title logo preparation failed: %s", SDL_GetError());
    sawgod::EnhancedGraphics enhanced_graphics, mode7_graphics;
    sawgod::Mode7Renderer mode7_renderer;
    sawgod::app::BattleWipe battle_wipe;
    enhanced_graphics.timing_enabled = mode7_graphics.timing_enabled = mode7_renderer.timing_enabled =
        timing_log.enabled();
    sawgod::FocusPass mode7_focus;
    int initial_width = 224, initial_height = 144;
    SDL_GetRenderOutputSize(renderer, &initial_width, &initial_height);
    bool running = true;
    sawgod::ui::TouchControls touch;
    if (!settings.touch_controls->hidden && !touch.init())
        sawgod::debug_log::errorf("input", "Touch gamepad initialization failed: %s", SDL_GetError());
    sawgod::ui::Input input(touch.id());
    bool touch_foreground = true;
    const auto initial_game = GameRect(args.shot_w > 0 ? args.shot_w : initial_width,
                                       args.shot_h > 0 ? args.shot_h : initial_height);
    const auto initial_scene =
        sawgod::app::enhanced_scene_size(int(std::lround(initial_game.w)), int(std::lround(initial_game.h)),
                                         settings.pacing_mode() == sawgod::Pacing::LoSpec);
    int mode7_width = initial_scene[0], mode7_height = initial_scene[1];
    // Enhanced's GPU state (shader and pipeline builds) in three steps, run during the splash's still frames
    // (startup timing: about two thirds of a cold start's GPU work). Whatever is left when the splash ends or
    // is skipped runs before anything draws Enhanced (finish_enhanced_setup), so no frame sees it half built.
    int enhanced_setup_step = 0;
    const auto enhanced_setup = [&]() -> bool {
        switch (enhanced_setup_step++) {
        case 0:
            if (!mode7_focus.init(renderer))
                sawgod::debug_log::warnf("gpu", "Mode 7 focus unavailable: %s", SDL_GetError());
            return true;
        case 1:
            if (!mode7_renderer.init(renderer) || !mode7_renderer.resize(mode7_width, mode7_height) ||
                !mode7_graphics.init(renderer, mode7_renderer.picture(), mode7_renderer.ownership(),
                                     mode7_renderer.water()))
                sawgod::debug_log::errorf("gpu", "World camera GPU effects unavailable: %s", SDL_GetError());
            return true;
        case 2:
            if (!enhanced_graphics.init(renderer, nullptr, nullptr, nullptr, &mode7_graphics))
                sawgod::debug_log::errorf("gpu", "Enhanced graphics unavailable: %s; using ordinary picture",
                                          SDL_GetError());
            mode7_supported = mode7_renderer.ready() && mode7_graphics.ready() && enhanced_graphics.ready();
            sawgod::startup::mark("Enhanced GPU setup complete");
            return false;
        default:
            return false;
        }
    };
    const auto finish_enhanced_setup = [&] {
        while (enhanced_setup())
            ;
    };
    bool mode7_uploaded = false;
    bool enhanced_uploaded = false;
    // Screen Mode LCD on the CPU picture, except while a GPU picture (Enhanced's, or its camera) is still on
    // screen on the way out of Enhanced: then, as at Enhanced, the wash over the whole picture (lcd_wash), so
    // the filter stays on through the fade. The CPU history (fstn_rgb) keeps running for the moment it ends.
    const auto lcd_exact = [&] { return lcd_on_cpu() && !enhanced_uploaded && !mode7_uploaded; };
    SharpScaler enhanced_scaler;
    sawgod::app::SceneScaler scene_scaler;
    SDL_SetRenderVSync(renderer, sawgod::presentation_diagnostics::cpu_only() ? 0 : 1);
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                             sawgod::kNativeWidth, sawgod::kNativeHeight);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    SharpScaler game_scaler;
    // The menu is drawn straight at the window's resolution into this (recreated when the size changes).
    SDL_Texture *canvas = nullptr;
    SDL_Texture *shot_target = nullptr;
    int canvas_w = 0, canvas_h = 0;
    // Options owns a separate GPU canvas. Publish it only after its complete render;
    // return to the game only after collecting/uploading the paused scene again.
    bool options_canvas_submitted = false;
    // The menu's backdrop: the paused game, blurred, smoothly scaled.
    SDL_Texture *backdrop = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                              sawgod::kNativeWidth, sawgod::kNativeHeight);
    SDL_SetTextureScaleMode(backdrop, SDL_SCALEMODE_LINEAR);
    std::vector<uint32_t> backdrop_source, backdrop_uploaded;
    auto make_backdrop = [&]() -> std::vector<uint32_t> {
        if (!wsc)
            return {};
        const auto rgb = game_rgb();
        if (backdrop_source != rgb || backdrop_uploaded.empty() ||
            sawgod::presentation_diagnostics::fresh()) {
            const auto blurred = BlurredBackdrop(rgb, sawgod::kNativeWidth, sawgod::kNativeHeight, 2);
            sawgod::upload_pixels(backdrop, blurred.data(), sawgod::kNativeWidth, sawgod::kNativeHeight,
                                  backdrop_uploaded);
            backdrop_source = rgb;
        }
        return rgb;
    };
    LcdGrid lcd_grid; // Screen Mode IPS
    ZoomBackdrop zoom_backdrop;
    // Allocate size-dependent presentation targets before any game/audio frame can use them.
    auto *startup_target = SDL_GetRenderTarget(renderer);
    if (zoom_backdrop.begin(renderer, int(initial_game.w), int(initial_game.h), initial_game.scale))
        SDL_SetRenderTarget(renderer, startup_target);
    if (battle_wipe.begin(renderer, args.shot_w > 0 ? args.shot_w : initial_width,
                          args.shot_h > 0 ? args.shot_h : initial_height, 0, -1,
                          {initial_game.x, initial_game.y, initial_game.w, initial_game.h}))
        SDL_SetRenderTarget(renderer, startup_target);

    // bindings: the defaults, then sawgod.toml's [keyboard] / [gamepad] tables (the menu edits them)
    sawgod::ui::InputMap input_map = sawgod::ui::InputMap::defaults();
    {
        std::vector<std::string> warnings;
        input_map.load(config, &warnings);
        for (const std::string &w : warnings)
            sawgod::debug_log::warnf("config", "%s: %s", config_path.c_str(), w.c_str());
    }
    sawgod::app::install_button_prompts(input, input_map, settings, fidelity, host_picture);
    sawgod::ui::Overlay overlay;
    uint16_t swallow = 0; // game buttons held when the menu closed: ignored until released
    sawgod::app::AudioOutput audio;
    std::vector<uint32_t> cur_rgb;
    std::vector<uint32_t> fstn_rgb, fstn_frame; // Screen Mode LCD: what the stock screen shows (with smear)
    std::vector<uint32_t> uploaded_rgb;
    sawgod::GpuUploadWork log_upload_work;
    bool native_pending = false;
    auto upload_native = [&] {
        if (!native_pending)
            return;
        sawgod::CpuScope cpu_scope("upload native 224x144x4", wsc->frame_count);
        const auto before =
            timing_log.enabled() ? sawgod::GpuUploadWork::read(renderer) : sawgod::GpuUploadWork{};
        const auto &src = lcd_exact() && fstn_rgb.size() == cur_rgb.size() ? fstn_rgb : cur_rgb;
        sawgod::upload_pixels(texture, src.data(), sawgod::kNativeWidth, sawgod::kNativeHeight, uploaded_rgb);
        if (timing_log.enabled()) {
            // Uploads deferred until a badge or fallback draw still belong to this present.
            // upload_game replaces this subtotal when it includes the upload in its own interval.
            const auto work = sawgod::GpuUploadWork::read(renderer) - before;
            log_upload_work.bytes += work.bytes;
            log_upload_work.calls += work.calls;
            log_upload_work.allocations += work.allocations;
        }
        native_pending = false;
    };

    auto apply = [&](sawgod::CVar &v) {
        if (&v == settings.fullscreen) {
            SDL_SetWindowFullscreen(window, v.on());
        } else if (&v == settings.borderless) {
            SDL_SetWindowBordered(window, !v.on());
        } else if (&v == settings.language) {
            language_pending = true; // applied once no dialogue is reading the current language's text
        }
    };
    auto close_menu = [&] {
        overlay.set_open(false);
        host_options_open = false;
        swallow = input.held(input_map);
        save_config();
    };
    sawgod::ui::Overlay::Hooks hooks;
    hooks.changed = apply;
    hooks.quit = [&] { running = false; };
    hooks.close = [&] {
        host_options_open = false;
        swallow = input.held(input_map);
        save_config();
    };
    hooks.reset = [&] {
        // a soft reset: the machine restarts from power-on; the battery RAM (and the installed natives) stay
        if (!wsc)
            return;
        wsc->reset();
        if (!args.native.empty())
            sawgod::game::reset_natives(*wsc, args.native == "all" ? nullptr : args.native.c_str());
        cur_rgb.clear();
        audio.clear();
    };
    // Cheats > Teleport to Floor (game/cheats.h): floors 1 to the highest reached (the save's 0xD0DA), once a
    // game is loaded; it happens when the party next stands still in the field
    hooks.teleport_max = [&] { return sawgod::app::teleport_max(wsc.get()); };
    hooks.teleport_current = [&] { return wsc ? int(wsc->ram[0xD0DB]) : 1; };
    hooks.teleport = [&](int floor) { cheats.request_teleport(floor); };
    hooks.input = &input;
    hooks.map = &input_map;
    hooks.host_text = [&](std::string_view key, std::string_view fallback) {
        return wsc ? sawgod::game::name_utf8(sawgod::game::host_text(*wsc, key)) : std::string(fallback);
    };
    hooks.gb_style = [&] { return fidelity.graphics() == sawgod::FidelityMode::Gb; };
    hooks.enhanced_style = [&] { return fidelity.graphics() == sawgod::FidelityMode::Enhanced; };
    input.set_map(&input_map); // (Options' rebindable slot opens the menu)
    if (!overlay.init(renderer, config, settings, hooks))
        sawgod::debug_log::errorf("options", "cannot set up the options menu");
    if (wsc && !overlay.build_skin(wsc->rom, gb_hand_png))
        sawgod::debug_log::errorf("options", "cannot build the options menu's font");

    // The WonderSwan refreshes at 3,072,000 / (159 * 256) = ~75.47 Hz; game logic always runs on that clock.
    const uint64_t frame_ns = sawgod::game::kWscFrameNs;
    uint64_t next_frame = SDL_GetTicksNS();

    // --record and --inputs (app/input_movie.h). movie_frame: the count of run_frame() calls completed.
    uint64_t movie_frame = 0;
    sawgod::app::InputMovie movie;
    movie.open(args, wsc != nullptr);

    // Saving (app/save_setup): the raw SRAM .sav of --native none, or the save store.
    sawgod::app::SaveWriter saves(window);
    if (const int code = saves.open(wsc.get(), args, use_store, save_path))
        return code;

    if (wsc)
        wsc->apu.effect_channels = [&] {
            return sawgod::app::sfx_channels(*wsc, gb_music.wsc_music_slots(*wsc) |
                                                       enhanced_observer.wsc_music_slots(*wsc));
        };
    int game_frames = 0; // for Show FPS
    sawgod::app::AudioLatency audio_latency;
    int audio_speed = 1; // Fast-Forward grains use the same clock for all three arrangements
    // Native menu windows also suppress the world-map minimap.
    const auto menu_screen = [&] {
        return ui.item_dialog.active() || ui.save.active() || ui.service.active() || ui.creation.active() ||
               ui.bestiary.active() || ui.party.input_active() || ui.inventory.active();
    };
    auto audit_frame = [&](int64_t view = -1) {
        if (!frame_audit.enabled() || !wsc)
            return;
        const bool service = ui.service.active() && host_picture.shown() == 4;
        frame_audit.write(*wsc,
                          {view,
                           movie_frame,
                           sawgod::game::Session::of(*wsc).generation(),
                           int(enhanced_scene.kind),
                           enhanced_scene.map,
                           mode7_amount,
                           enhanced_scene.perspective_allowed,
                           ui.dialogue.active(),
                           ui.battle.active(),
                           host_picture.native_full_screen(*wsc) || (host_picture.full_ui() && !service),
                           ui.dialogue.active() || service,
                           host_picture.shown(),
                           host_picture.holding(),
                           enhanced_scene.field_cleared,
                           enhanced_frame,
                           audit_field_step,
                           audit_world_step,
                           audit_settled_zoom,
                           sawgod::game::presented_battle_entry_wipe(*wsc),
                           int(fidelity.graphics()),
                           audit_battle_start,
                           audit_battle_end,
                           sawgod::game::recognized_field(*wsc),
                           scene_observer.field_published(*wsc),
                           int(enhanced_scene.previous),
                           int(enhanced_scene.transition),
                           enhanced_effect_weight,
                           bool(wsc->ram[0xD519]),
                           scene_effect_fade.fading()},
                          enhanced_wsc);
    };
    // One 75.47 Hz tick: a game frame, or while the menu is open just the music (sound_mix.h).
    auto tick = [&](bool audio_tick = true) {
        if (frame_audit.enabled())
            audit_field_step = audit_world_step = "None";
        if (party_options_requested) {
            party_options_requested = false;
            touch.show(false);
            overlay.set_open(true);
            host_options_open = true;
            motion_history.reset();
            make_backdrop();
        }
        auto &session = sawgod::game::Session::of(*wsc);
        sawgod::app::apply_esper_confirmation(*wsc, settings);
        if (wsc->host_paused) {
            if (!overlay.is_open())
                session.esper_input(*wsc, input.held(input_map));
        }
        const uint64_t timing_start = timing_log.enabled() ? SDL_GetTicksNS() : 0;
        const auto audio_plan =
            audio_tick ? audio_latency.frame(fidelity.music(), enhanced_audio.available(), SDL_GetTicksNS(),
                                             settings.music() == sawgod::FidelityMode::Swap ||
                                                 settings.music() == sawgod::FidelityMode::Enhanced,
                                             enhanced_observer.generation())
                       : sawgod::app::AudioLatency::Plan{};
        const auto music_weights = audio_plan.weights;
        const sawgod::app::MusicMix music_mix =
            sawgod::app::music_mix(music_weights, gb_music.loaded(), gb_music.playing(),
                                   enhanced_audio.available() && enhanced_observer.playing(*wsc));
        const float gb = music_mix.gb, enhanced = music_mix.enhanced;
        const auto cue_mix =
            sawgod::app::music_mix(music_weights, gb_music.loaded(), gb_music.playing(),
                                   enhanced_audio.available() && enhanced_observer.cue_playing(*wsc));
        const uint8_t cue_slots = enhanced_observer.wsc_music_slots(*wsc);
        if (audio_tick) {
            sawgod::app::apply_channel_gains(*wsc, settings.music_gain() * music_mix.wsc, settings.sfx_gain(),
                                             gb_music.wsc_music_slots(*wsc) | cue_slots,
                                             gb_music.mapped_wsc_slots(*wsc), gb);
            sawgod::app::apply_cue_gain(*wsc, cue_slots, settings.music_gain() * cue_mix.wsc);
        }
        if (overlay.is_open()) {
            sawgod::app::paused_tick(*wsc);
            cur_rgb = make_backdrop();
        } else if (wsc->host_paused) {
            sawgod::app::run_game_frame(*wsc, audio_tick);
            cur_rgb = game_rgb();
        } else {
            uint16_t raw = input.held(input_map);
            raw = movie.held(movie_frame, raw); // --inputs
            swallow &= raw;
            const uint16_t keys = raw & ~swallow;
            movie.record(movie_frame, keys); // --record
            wsc->set_keys(gfx_swap.intro_keys(*wsc, keys, fidelity.graphics() == sawgod::FidelityMode::Gb));
            if (language_pending) {
                const sawgod::game::Session *session = sawgod::game::Session::find(*wsc);
                if (!session || !session->reading_language_dialogue()) {
                    set_language();
                    language_pending = false;
                }
            }
            if (game_config.on())
                sawgod::game::GameConfig::apply(*wsc, sawgod::app::game_config_values(settings));
            for (const auto &[address, value] : args.shot_holds)
                wsc->ram[address] = uint8_t(value);
            for (const auto &poke : args.shot_pokes)
                if (poke.frame == movie_frame)
                    wsc->ram[poke.address] = uint8_t(poke.value);
            // A paused decision or retained menu can expose GB at any instant, including mid-raster.
            // Keep its exact scanout; reconstructing from the last frame's final memory loses tile writes.
            wsc->second_visible = true;
            {
                sawgod::CpuScope cpu_scope("run_game_frame", wsc->frame_count + 1);
                sawgod::app::run_game_frame(*wsc, audio_tick);
            }
            if (wsc->host_paused) {
                cur_rgb = game_rgb();
                audit_frame();
                // Submit these partial samples together with the next audio-only driver tick.
                return;
            }
            ui.frame(*wsc, settings.battle_transparency->get());
            saves.frame(*wsc, settings.save_slot_count());
            ++movie_frame;
            ++game_frames;
            if (gfx_swap.enabled())
                gfx_swap.frame(*wsc); // the GB picture's shadow follows the frame just made
            host_picture.frame(*wsc);
            // Info: drawn on the next frame; only in a battle (a wipe-out leaves the last groups in RAM).
            sawgod::game::update_enemy_hp_boxes(*wsc, settings.enemy_hp->on() && battle_span.active(*wsc));
            cur_rgb = game_rgb();
            if (lcd_on_cpu()) {
                fstn_frame = cur_rgb;
                sawgod::fstn_color(fstn_frame);
                sawgod::fstn_persist(fstn_rgb, fstn_frame);
            } else {
                fstn_rgb.clear();
            }
        }
        audit_frame();
        if (!audio_tick)
            return;
        std::vector<int16_t> samples = wsc->apu.take();
        auto effects = wsc->apu.take_effects();
        gb_music.frame(*wsc, samples, settings.music_gain() * gb, settings.sfx_gain() * gb, &effects);
        auto packet = enhanced_observer.frame(*wsc);
        packet.samples = std::move(samples);
        packet.effects = std::move(effects);
        packet.gain =
            settings.music_gain() * (sawgod::app::enhanced_song(packet.song) ? music_weights[2] : enhanced);
        packet.cue_gain = settings.music_gain() * cue_mix.enhanced;
        packet.speed = audio_speed;
        packet.lookahead = audio_plan.lookahead;
        packet.master = settings.master_gain();
        enhanced_audio.submit(std::move(packet));
        if (timing_log.enabled()) {
            production_end = SDL_GetTicksNS();
            log_game_tick = production_end - timing_start;
        }
    };

    sawgod::FastForwardVfx ff_vfx;
    sawgod::FastForwardEnvelope ff_envelope;
    std::array<uint32_t, 224 * 144> ff_ui{};
    if (ff_vfx.begin(renderer, args.shot_w > 0 ? args.shot_w : initial_width,
                     args.shot_h > 0 ? args.shot_h : initial_height)) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        ff_vfx.end({initial_game.x, initial_game.y, initial_game.w, initial_game.h}, ff_ui.data(), 0, 0, 0);
        ff_vfx.reset_history();
    }

    // On-screen badges, top right: double arrow while Fast-Forward runs
    // Encounters Off/On appears briefly after the Encounters button, fading out.
    constexpr uint64_t kBadgeFadeNs = 400'000'000;
    // Translucent SDL overlays own their underlay too, just like host windows.
    // Restore the ordinary scaled picture inside their output bounds, after IPS.
    // Screen Mode LCD over Enhanced's picture (engine/screen.h fstn_wash): multiply, then add.
    auto lcd_wash = [&](const SDL_FRect &dst) {
        if (effective_screen() != sawgod::ScreenMode::Lcd || lcd_exact())
            return;
        const sawgod::FstnWash w = sawgod::fstn_wash();
        SDL_BlendMode old = SDL_BLENDMODE_NONE;
        SDL_GetRenderDrawBlendMode(renderer, &old);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_MOD);
        SDL_SetRenderDrawColorFloat(renderer, w.scale[0], w.scale[1], w.scale[2], 1);
        SDL_RenderFillRect(renderer, &dst);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
        SDL_SetRenderDrawColorFloat(renderer, w.offset[0], w.offset[1], w.offset[2], 1);
        SDL_RenderFillRect(renderer, &dst);
        SDL_SetRenderDrawBlendMode(renderer, old);
    };
    auto restore_ui_rect = [&](const Rect &g, const SDL_FRect &box) {
        if (!enhanced_uploaded && !mode7_uploaded)
            return;
        if (native_pending) {
            upload_native();
            const SDL_FRect destination{g.x, g.y, g.w, g.h};
            game_scaler.draw(renderer, texture, 224, 144, destination, g.scale, 1, false);
        }
        SDL_Rect old_clip{};
        const bool clipped = SDL_RenderClipEnabled(renderer);
        SDL_GetRenderClipRect(renderer, &old_clip);
        const SDL_Rect clip{int(std::floor(box.x)) - 1, int(std::floor(box.y)) - 1,
                            int(std::ceil(box.x + box.w)) - int(std::floor(box.x)) + 2,
                            int(std::ceil(box.y + box.h)) - int(std::floor(box.y)) + 2};
        SDL_SetRenderClipRect(renderer, &clip);
        const SDL_FRect destination{g.x, g.y, g.w, g.h};
        const bool whole = std::abs(g.scale - std::round(g.scale)) < 0.001f;
        SDL_RenderTexture(renderer, whole ? texture : game_scaler.big, nullptr, &destination);
        if (effective_screen() == sawgod::ScreenMode::Ips)
            lcd_grid.draw(renderer, destination, ips_amount);
        lcd_wash(destination);
        SDL_SetRenderClipRect(renderer, clipped ? &old_clip : nullptr);
    };
    // The world-map minimap (app/minimap.h): where the party walks a world map in the Enhanced camera.
    auto minimap_scene = [&] {
        sawgod::app::MinimapScene s;
        s.world_map = sawgod::game::GameConfig::shown_world(enhanced_scene.map) &&
                      mode7_source.map() == enhanced_scene.map;
        s.field = enhanced_scene.kind == sawgod::game::PresentationScene::Kind::Field &&
                  enhanced_scene.perspective_allowed;
        // (GB and WSC graphics draw no world camera; through a fidelity switch the weight fades it)
        s.mode7 = mode7_uploaded && mode7_amount > 0 && mode7_camera.pixel_scale == 0;
        s.menu = host_options_open || overlay.is_open() || host_picture.full_ui() || menu_screen();
        s.dialogue = ui.dialogue.active();
        s.battle = ui.battle.active();
        s.transition = wsc && (wsc->ram[0xD52C] || wsc->ram[0xD519]);
        s.fast_forward = fast_forwarding;
        return s;
    };
    // Over the finished picture, after every scene effect and the Fast-Forward pass; under the badges.
    auto draw_minimap = [&](const Rect &g) {
        const uint64_t now = shot_fidelity_time.value_or(simulated_present_time.value_or(SDL_GetTicksNS()));
        const auto size =
            sawgod::app::minimap_size(settings.minimap->on(), settings.minimap_size->get() == 1);
        const auto scene = minimap_scene();
        const bool shown = sawgod::app::minimap_visible(scene, size);
        // A single still shows the settled picture, as the world camera does.
        const bool still =
            !args.shot.empty() && !args.shot_run && !args.shot_mode7_steps && !args.shot_fidelity_steps;
        // UI (menus, dialogue, battle) hides it at once: a fade-out would sit over the box for 0.2 s.
        if (still || scene.menu || scene.dialogue || scene.battle)
            minimap_fade.settle(shown, now);
        else
            minimap_fade.update(shown, now);
        if (shown)
            minimap_shown = size;
        // Follows the Enhanced weight through a fidelity switch, and the world camera's swoop.
        const float amount =
            minimap_fade.amount() * std::min(fidelity.graphics_weights(now)[2], mode7_amount);
        if (amount <= 0 || !mode7_uploaded || mode7_camera.pixel_scale != 0 ||
            !sawgod::game::GameConfig::shown_world(mode7_source.map()))
            return;
        const auto trim = sawgod::app::minimap_trim(sawgod::game::GameConfig::world_of(mode7_source.map()));
        const auto layout = sawgod::app::minimap_layout(g.x, g.y, g.w, g.h, g.scale, minimap_shown, trim);
        const auto look = sawgod::app::minimap_look();
        const auto party =
            sawgod::app::minimap_party_tile(mode7_source.scroll()[0], mode7_source.scroll()[1]);
        const SDL_FRect area{float(layout.x), float(layout.y), float(layout.side), float(layout.height)};
        // The party dot is one of the game's own pixels, not the map's finer tiles.
        const int block = std::max(layout.tile, int(std::lround(g.scale)));
        if (!mode7_renderer.draw_minimap(
                area, layout.tile, trim.rows, trim.columns, block, party, amount * look.opacity,
                look.brightness, still ? sawgod::app::kMarkerPeak : sawgod::app::minimap_marker_level(now),
                amount))
            sawgod::debug_log::errorf("gpu", "Minimap draw failed: %s", SDL_GetError());
    };
    auto draw_badges = [&](const Rect &g) {
        if (!args.shot_mask.empty())
            ++picture_revision;
        std::vector<std::pair<std::string, float>> lines; // text, opacity
        const uint64_t t = SDL_GetTicksNS();
        if (t < encounters_badge_until)
            lines.emplace_back(settings.encounters->get() == kEncountersOff ? "Encounters Off"
                                                                            : "Encounters On",
                               std::min(1.0f, float(encounters_badge_until - t) / float(kBadgeFadeNs)));
        const float s = g.scale; // one font pixel = one game pixel
        float y = g.y + 3 * s;
        if (fast_forwarding) {
            const SDL_FRect box{g.x + g.w - 17 * s, y, 14 * s, 12 * s};
            restore_ui_rect(g, box);
            sawgod::draw_fast_forward_indicator(renderer, box, s, overlay.gb_style());
            if (!args.shot_mask.empty())
                for (int yy = 3; yy < 15; ++yy)
                    for (int xx = 207; xx < 221; ++xx)
                        enhanced_scene.mask[size_t(yy * 224 + xx)] |= 0xFF0000;
            y += 14 * s;
        }
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        for (const auto &[line, alpha] : lines) {
            const float badge_scale =
                s * (1 - 0.5f * fidelity.graphics_weights(shot_fidelity_time.value_or(SDL_GetTicksNS()))[2]);
            const float w = sawgod::ui::Skin::text_width(line) + 4, h = 12;
            const float x = g.x + g.w - 3 * s - w * badge_scale;
            // GB graphics: black on a solid white box, so the picture keeps to plain shades; otherwise white
            // on a see-through dark box.
            const bool gb = overlay.gb_style();
            if (gb)
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, Uint8(255 * alpha));
            else
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, Uint8(160 * alpha));
            const SDL_FRect box{x, y, w * badge_scale, h * badge_scale};
            if (!args.shot_mask.empty()) {
                const int left = int(std::floor((x - g.x) / s)), top = int(std::floor((y - g.y) / s));
                for (int yy = std::max(0, top); yy < std::min(144, top + int(std::ceil(h * badge_scale / s)));
                     ++yy)
                    for (int xx = std::max(0, left);
                         xx < std::min(224, left + int(std::ceil(w * badge_scale / s))); ++xx)
                        enhanced_scene.mask[size_t(yy * 224 + xx)] |= 0xFF0000;
            }
            SDL_RenderFillRect(renderer, &box);
            overlay.skin().text_sdl(renderer, x + 2 * badge_scale, y + 2 * badge_scale, line, badge_scale,
                                    alpha, gb);
            y += (h + 2) * badge_scale;
        }
    };

    // Show FPS (debug builds): game frames per second and display refreshes per second, measured.
    std::string fps_text;
    uint64_t fps_start = SDL_GetTicksNS();
    int fps_presents = 0;
    uint64_t logged_underruns = 0, next_underrun_log = 0; // debug log: sound dropouts

    // Draws the game and (if open) the menu into the current render target, out_w x out_h pixels.
    uint64_t timing_present_id = 1, gpu_over_budget_count = 0;
    auto start_gpu_timing = [&]() {
#ifdef SAWGOD_GPU_TIMESTAMPS
        if (timing_log.enabled())
            SawGodRendererTimingStart(renderer, timing_present_id);
#endif
    };
    auto draw_scene = [&](int out_w, int out_h, bool fullscreen, bool retained_field = false) {
        sawgod::CpuScope cpu_scope("draw_scene");
        if (sawgod::presentation_diagnostics::cpu_only()) {
            // Consume the same collected inputs; the CPU contract is independent of scene rasterization.
            const auto g = GameRect(out_w, out_h);
            const SDL_FRect dst{g.x, g.y, g.w, g.h};
            if (enhanced_uploaded)
                enhanced_graphics.render();
            if (mode7_uploaded)
                mode7_graphics.render();
            if (menu_fill_weight > 0) {
                menu_draw_inputs.update(dst, &menu_fills, &menu_text, enhanced_wsc, menu_fill_weight,
                                        cur_rgb);
                menu_fill.draw(renderer, dst, menu_fills, enhanced_wsc, menu_fill_weight, &menu_text,
                               &menu_draw_inputs);
                hd_menus.draw(renderer, dst, menu_text, &menu_fills, enhanced_wsc, menu_fill_weight, cur_rgb,
                              &menu_draw_inputs);
            }
            return;
        }
        start_gpu_timing();
        const uint64_t ff_time = simulated_present_time.value_or(SDL_GetTicksNS());
        const float ff_amount =
            retained_field ? 0 : ff_envelope.update(settings.ff_vfx->on(), fast_forwarding, ff_time);
        // Full-screen Options is composited afterwards; its backdrop stays clean too.
        const bool ff_composite = wsc && !retained_field && !options_canvas_submitted && ff_amount > 0 &&
                                  ff_vfx.begin(renderer, out_w, out_h);
        if (!ff_composite && !retained_field)
            ff_vfx.reset_history();
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        hd_menus.cpu_ns = 0;
        hd_menus.strokes = 0;
        hd_menus.draws = 0;
        menu_fill.cpu_ns = 0;
        menu_cache_ns = 0;
        menu_fill.quads = 0;
        const Rect g = GameRect(out_w, out_h);
        const SDL_FRect screen_dst{g.x, g.y, g.w, g.h};
        SDL_FRect backdrop_dst = screen_dst; // black bars around it in fullscreen
        if (!fullscreen && std::strcmp(SDL_GetCurrentVideoDriver(), "android") != 0) {
            // Android is always immersive, even with the desktop fullscreen setting off.
            const float s =
                std::max(float(out_w) / sawgod::kNativeWidth, float(out_h) / sawgod::kNativeHeight);
            const float w = sawgod::kNativeWidth * s, h = sawgod::kNativeHeight * s;
            backdrop_dst = {(out_w - w) / 2, (out_h - h) / 2, w, h};
        }
        // Options over a zoomed flat field: its backdrop is the zoomed picture, blurred here.
        SDL_Texture *const frame_target = SDL_GetRenderTarget(renderer);
        const bool zoomed_backdrop = wsc && options_canvas_submitted && mode7_uploaded &&
                                     mode7_camera.pixel_scale > 0 &&
                                     zoom_backdrop.begin(renderer, int(g.w), int(g.h), g.scale);
        const SDL_FRect gdst = zoomed_backdrop ? SDL_FRect{0, 0, g.w, g.h} : screen_dst;
        const auto ordinary = [&] {
            upload_native();
            game_scaler.draw(renderer, texture, 224, 144, gdst, g.scale, 1, false);
        };
        const bool wiping = !retained_field && wsc && sawgod::game::presented_battle_entry_wipe(*wsc);
        const auto generation = wsc ? sawgod::game::Session::of(*wsc).generation() : 0;
        const float enhanced_weight =
            fidelity.graphics_weights(shot_fidelity_time.value_or(SDL_GetTicksNS()))[2];
        const bool wipe_picture =
            wiping && enhanced_weight > 0 && battle_wipe.available(generation, wsc->ram[0xD0DC]);
        const bool keep_field =
            wsc && !wiping && !options_canvas_submitted && enhanced_weight == 1 && !host_picture.holding() &&
            enhanced_scene.kind == sawgod::game::PresentationScene::Kind::Field &&
            battle_wipe.begin(renderer, out_w, out_h, generation, enhanced_scene.map, screen_dst);

        if (!wsc) {
            SDL_SetRenderDrawColor(renderer, 24, 24, 32, 255);
            SDL_RenderClear(renderer);
        } else if (options_canvas_submitted && !zoomed_backdrop &&
                   !sawgod::game::GameConfig::mode7_map(enhanced_scene.map)) {
            // Ordinary Options backgrounds stay blurred; worlds show the live lowering camera.
            SDL_RenderTexture(renderer, backdrop, nullptr, &backdrop_dst);
        } else if (wipe_picture) {
            upload_native();
            // Native pixels remain the other fidelity endpoint while a mode step is in flight.
            game_scaler.draw(renderer, texture, 224, 144, gdst, g.scale);
            battle_wipe.draw(renderer, *wsc, gdst, enhanced_weight);
        } else {
            const bool opaque_camera = mode7_uploaded && mode7_opacity >= 1;
            bool flat_finished = false;
            std::optional<SDL_FRect> overlay_native; // the native picture's place under a scripted overlay
            presentation.begin_game();
            if (opaque_camera) {
                if (!mode7_graphics.ui_regions.empty())
                    ordinary();
            } else if (enhanced_uploaded) {
                game_scaler.draw(renderer, texture, 224, 144, gdst, g.scale, 1, false);
                presentation.end_game();
                auto *effect = enhanced_graphics.render(args.shot_benchmark > 0);
                if (effect) {
                    const bool whole = std::abs(g.scale - std::round(g.scale)) < 0.001f;
                    const auto scene_size = sawgod::app::enhanced_scene_size(
                        int(g.w), int(g.h), settings.pacing_mode() == sawgod::Pacing::LoSpec);
                    auto *ordinary = whole ? texture : game_scaler.big;
                    if ((scene_size[0] < int(g.w) || scene_size[1] < int(g.h)) &&
                        scene_scaler.prepare(renderer, scene_size[0], scene_size[1])) {
                        auto *target = SDL_GetRenderTarget(renderer);
                        const SDL_FRect scene_dst{0, 0, float(scene_size[0]), float(scene_size[1])};
                        SDL_SetRenderTarget(renderer, scene_scaler.target);
                        flat_finished = enhanced_graphics.present(effect, ordinary, scene_dst);
                        SDL_SetRenderTarget(renderer, target);
                        auto *scaled = scene_scaler.scaled(renderer, gdst);
                        // Host UI keeps the output scaler; enemy art and lighting are already in the scene.
                        flat_finished =
                            flat_finished && enhanced_graphics.present(scaled, ordinary, gdst, true);
                    } else {
                        if (!whole)
                            enhanced_scaler.draw(renderer, effect, 224, 144, gdst, g.scale, 1, false);
                        flat_finished =
                            enhanced_graphics.present(whole ? effect : enhanced_scaler.big, ordinary, gdst);
                    }
                }
                if (!flat_finished) {
                    enhanced_uploaded = false;
                    sawgod::debug_log::errorf("gpu", "Enhanced final draw failed: %s; using ordinary picture",
                                              SDL_GetError());
                    game_scaler.draw(renderer, texture, 224, 144, gdst, g.scale);
                }
            } else if ((enhanced_scene.transition ==
                            sawgod::game::PresentationScene::Transition::ScriptedOverlay ||
                        sawgod::game::crystal_door_vignette(*wsc)) &&
                       mode7_amount > 0 && mode7_camera.pixel_scale > 0) {
                // A scripted overlay (the story lightning) or the travel crystal's door vignette
                // over a zoomed field: the native picture keeps the camera's scale and framing, so the
                // view does not jump in and out around it. Beyond the native picture, a uniform border
                // (black, a white flash) continues; anything else is black.
                const float scale = mode7_camera.pixel_scale; // the camera is laid out on this same game rect
                const SDL_FRect native{gdst.x - mode7_camera.origin[0] * scale,
                                       gdst.y - mode7_camera.origin[1] * scale, sawgod::kNativeWidth * scale,
                                       sawgod::kNativeHeight * scale};
                uint32_t border = cur_rgb.empty() ? 0 : cur_rgb[0];
                for (int x = 0; x < sawgod::kNativeWidth && border; ++x)
                    for (int y : {0, sawgod::kNativeHeight - 1})
                        border = cur_rgb[size_t(y * sawgod::kNativeWidth + x)] == border ? border : 0;
                for (int y = 0; y < sawgod::kNativeHeight && border; ++y)
                    for (int x : {0, sawgod::kNativeWidth - 1})
                        border = cur_rgb[size_t(y * sawgod::kNativeWidth + x)] == border ? border : 0;
                // The vignette's beam crosses the top edge during its gray fades: there the edge's own
                // color (nine tenths of it, or more) continues the background.
                if (!border && !cur_rgb.empty() && sawgod::game::crystal_door_vignette(*wsc)) {
                    // Nine tenths is a strict majority: one voting pass finds the only candidate, a second
                    // counts it (no allocation on the present path).
                    const auto each_edge = [&](auto &&visit) {
                        for (int x = 0; x < sawgod::kNativeWidth; ++x)
                            for (int y : {0, sawgod::kNativeHeight - 1})
                                visit(cur_rgb[size_t(y * sawgod::kNativeWidth + x)]);
                        for (int y = 1; y < sawgod::kNativeHeight - 1; ++y)
                            for (int x : {0, sawgod::kNativeWidth - 1})
                                visit(cur_rgb[size_t(y * sawgod::kNativeWidth + x)]);
                    };
                    uint32_t candidate = 0;
                    int votes = 0;
                    each_edge([&](uint32_t color) {
                        if (votes == 0)
                            candidate = color;
                        votes += color == candidate ? 1 : -1;
                    });
                    int count = 0;
                    each_edge([&](uint32_t color) { count += color == candidate; });
                    if (count * 10 >= (sawgod::kNativeWidth + sawgod::kNativeHeight - 2) * 2 * 9)
                        border = candidate;
                }
                SDL_SetRenderDrawColor(renderer, border >> 16 & 255, border >> 8 & 255, border & 255, 255);
                SDL_RenderFillRect(renderer, &gdst);
                SDL_Rect old_clip{};
                const bool clipped = SDL_RenderClipEnabled(renderer);
                SDL_GetRenderClipRect(renderer, &old_clip);
                const SDL_Rect clip{int(gdst.x), int(gdst.y), int(gdst.w), int(gdst.h)};
                SDL_SetRenderClipRect(renderer, &clip);
                game_scaler.draw(renderer, texture, sawgod::kNativeWidth, sawgod::kNativeHeight, native,
                                 scale);
                SDL_SetRenderClipRect(renderer, clipped ? &old_clip : nullptr);
                overlay_native = native;
            } else
                game_scaler.draw(renderer, texture, sawgod::kNativeWidth, sawgod::kNativeHeight, gdst,
                                 g.scale);
            presentation.end_game();
            if (!args.shot_native_ui) {
                const auto weights = fidelity.graphics_weights(shot_fidelity_time.value_or(SDL_GetTicksNS()));
                world_card_overlay.draw(renderer, host_picture.world_cards(), *wsc, gdst, weights);
                const bool title = host_picture.title_menu().active(*wsc);
                title_logo_overlay.observe(
                    title, title ? wsc->framebuffer_rgb(Wsc::ColorMode::Raw) : std::vector<uint32_t>(),
                    shot_fidelity_time.value_or(SDL_GetTicksNS()), !args.shot.empty() && !args.shot_run);
                title_logo_overlay.draw(renderer, gdst, weights[2]);
            }
            // Give the flat underlay its ordinary IPS grid during the perspective fade.
            if (mode7_uploaded && mode7_opacity < 1 && effective_screen() == sawgod::ScreenMode::Ips)
                lcd_grid.draw(renderer, gdst, ips_amount);
            if (mode7_uploaded) {
                if (auto *world = mode7_graphics.render()) {
                    SDL_SetTextureBlendMode(world,
                                            mode7_opacity < 1 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
                    SDL_SetTextureAlphaModFloat(world, mode7_opacity);
                    // Gentle focus: a soft band along the screen edges on every map, faded with the swoop or
                    // field zoom. UI is restored sharp on top below.
                    const auto focus_start = timing_log.enabled() ? SDL_GetTicksNS() : 0;
                    const float focus_strength =
                        settings.enhanced_dof->on()
                            ? float(settings.enhanced_mode7_focus->get()) / 100 * mode7_amount
                            : 0.f;
                    const float focus_size = float(settings.enhanced_mode7_focus_size->get()) / 100;
                    if ((mode7_width < int(g.w) || mode7_height < int(g.h)) &&
                        scene_scaler.prepare(renderer, mode7_width, mode7_height)) {
                        auto *target = SDL_GetRenderTarget(renderer);
                        SDL_SetTextureAlphaModFloat(world, 1);
                        SDL_SetTextureBlendMode(world, SDL_BLENDMODE_NONE);
                        SDL_SetRenderTarget(renderer, scene_scaler.target);
                        mode7_focus.draw(world, {0, 0, float(mode7_width), float(mode7_height)},
                                         focus_strength, focus_size);
                        SDL_SetRenderTarget(renderer, target);
                        scene_scaler.scaled(renderer, gdst, mode7_opacity);
                    } else
                        mode7_focus.draw(world, gdst, focus_strength, focus_size);
                    SDL_SetTextureAlphaModFloat(world, 1);
                    SDL_SetTextureBlendMode(world, SDL_BLENDMODE_NONE);
                    // Pixel border lines on the zoomed flat field, as on the world map.
                    if (timing_log.enabled())
                        log_focus += SDL_GetTicksNS() - focus_start;
                    if (mode7_camera.pixel_scale > 0)
                        mode7_renderer.draw_field_grid(gdst);
                    const bool whole = std::abs(g.scale - std::round(g.scale)) < 0.001f;
                    if (!mode7_graphics.restore_ui(whole ? texture : game_scaler.big, gdst)) {
                        sawgod::debug_log::errorf("gpu", "World camera UI restoration failed: %s",
                                                  SDL_GetError());
                        upload_native();
                        game_scaler.draw(renderer, texture, 224, 144, gdst, g.scale);
                        mode7_uploaded = false;
                    }
                } else {
                    mode7_uploaded = false;
                    upload_native();
                    game_scaler.draw(renderer, texture, 224, 144, gdst, g.scale);
                }
            }
            if (menu_fill_weight > 0) {
                const auto cache_start = SDL_GetTicksNS();
                menu_draw_inputs.update(gdst, &menu_fills, &menu_text, enhanced_wsc, menu_fill_weight,
                                        cur_rgb);
                menu_cache_ns = SDL_GetTicksNS() - cache_start;
                menu_fill.draw(renderer, gdst, menu_fills, enhanced_wsc, menu_fill_weight, &menu_text,
                               &menu_draw_inputs);
                if (!hd_menus.draw(renderer, gdst, menu_text, &menu_fills, enhanced_wsc, menu_fill_weight,
                                   cur_rgb, &menu_draw_inputs))
                    sawgod::debug_log::errorf("gpu", "HD Menus failed: %s", SDL_GetError());
            }
            if (effective_screen() == sawgod::ScreenMode::Ips) {
                if (mode7_uploaded)
                    mode7_renderer.draw_ui_grid(gdst);
                else
                    lcd_grid.draw(renderer, overlay_native.value_or(gdst), ips_amount);
            }
            lcd_wash(gdst);
        }
        if (keep_field)
            battle_wipe.end(renderer);
        if (retained_field)
            return;
        if (zoomed_backdrop)
            zoom_backdrop.end(renderer, frame_target, backdrop_dst);
        if (ff_composite) {
            for (size_t i = 0; i < ff_ui.size(); ++i)
                ff_ui[i] = (host_ui[i] || (enhanced_scene.mask[i] & 0xFF0000)) ? 0xFF0000 : 0;
            // Badges own their underlay, including translucent portions. No digits are drawn.
            if (fast_forwarding)
                for (int y = 2; y < 16; ++y)
                    for (int x = 206; x < 222; ++x)
                        ff_ui[size_t(y * 224 + x)] = 0xFF0000;
            // Other host badges and debug text also keep a clean translucent underlay.
            const auto protect = [&](const SDL_FRect &box) {
                const int left = int(std::floor((box.x - g.x) / g.scale)) - 1;
                const int top = int(std::floor((box.y - g.y) / g.scale)) - 1;
                const int right = int(std::ceil((box.x + box.w - g.x) / g.scale)) + 1;
                const int bottom = int(std::ceil((box.y + box.h - g.y) / g.scale)) + 1;
                for (int y = std::max(0, top); y < std::min(144, bottom); ++y)
                    for (int x = std::max(0, left); x < std::min(224, right); ++x)
                        ff_ui[size_t(y * 224 + x)] = 0xFF0000;
            };
            if (SDL_GetTicksNS() < encounters_badge_until) {
                const char *label =
                    settings.encounters->get() == kEncountersOff ? "Encounters Off" : "Encounters On";
                const float width = sawgod::ui::Skin::text_width(label) + 4;
                const float badge_scale =
                    g.scale *
                    (1 - 0.5f * fidelity.graphics_weights(shot_fidelity_time.value_or(SDL_GetTicksNS()))[2]);
                protect({g.x + g.w - 3 * g.scale - width * badge_scale,
                         g.y + (fast_forwarding ? 17 : 3) * g.scale, width * badge_scale, 12 * badge_scale});
            }
            if (settings.fps_shown() && !fps_text.empty())
                protect({4, 4, float(fps_text.size() * 8 + 4) * 2, 24});
            ff_vfx.end(screen_dst, ff_ui.data(), ff_amount,
                       fidelity.graphics_weights(shot_fidelity_time.value_or(SDL_GetTicksNS()))[0],
                       double(ff_time) / 1e9);
        }
        if (wsc && !options_canvas_submitted)
            draw_minimap(g);
        if (options_canvas_submitted) {
            const Rect m = MenuRect(out_w, out_h);
            const float pad = std::round(sawgod::ui::Overlay::kPadLeft * m.scale);
            const SDL_FRect dst{m.x - pad, m.y, float(canvas_w), float(canvas_h)};
            SDL_RenderTexture(renderer, canvas, nullptr, &dst);
        }
        if (wsc && !options_canvas_submitted && overlay.ready())
            draw_badges(g);
        if (settings.fps_shown() && !fps_text.empty()) {
            if (!options_canvas_submitted)
                restore_ui_rect(g, {4, 4, float(fps_text.size() * 8 + 4) * 2, 24});
            SDL_SetRenderScale(renderer, 2, 2);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            const SDL_FRect box{2, 2, float(fps_text.size() * 8 + 4), 12};
            SDL_RenderFillRect(renderer, &box);
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderDebugText(renderer, 4, 4, fps_text.c_str());
            SDL_SetRenderScale(renderer, 1, 1);
        }
    };
    // Runs a menu frame and renders it into the canvas at the menu's size in this output.
    auto render_menu = [&](const sawgod::ui::MenuActions &actions, float dt, int out_w, int out_h) {
        overlay.frame(actions, dt);
        if (!overlay.is_open())
            return; // retain the previous complete canvas until the scene upload below
        if (!options_canvas_submitted)
            overlay.frame({}, 0); // settle ImGui layout in the same presentation, without a game tick
        const Rect m = MenuRect(out_w, out_h);
        const int w = std::max(1, int(m.w + std::round(sawgod::ui::Overlay::kPadLeft * m.scale))),
                  h = std::max(1, int(m.h));
        if (!canvas || canvas_w != w || canvas_h != h) {
            if (canvas)
                SDL_DestroyTexture(canvas);
            canvas = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
            SDL_SetTextureScaleMode(canvas, SDL_SCALEMODE_NEAREST);
            // (drawing into a transparent target leaves premultiplied color: composite it as such)
            SDL_SetTextureBlendMode(canvas, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
            overlay.invalidate_canvas();
            canvas_w = w;
            canvas_h = h;
        }
        if (!canvas) {
            sawgod::debug_log::errorf("options", "Options canvas allocation failed: %s", SDL_GetError());
            return;
        }
        SDL_Texture *old = SDL_GetRenderTarget(renderer);
        if (!SDL_SetRenderTarget(renderer, canvas)) {
            sawgod::debug_log::errorf("options", "Options canvas target failed: %s", SDL_GetError());
            return;
        }
        overlay.render(renderer, m.scale, true);
        SDL_SetRenderTarget(renderer, old);
        options_canvas_submitted = true;
    };
    sawgod::app::PictureMotion picture_motion;
    sawgod::app::PictureMotionSample picture_sample;
    std::array<float, 2> picture_reference{}, picture_geometry{}, party_geometry{};
    int picture_probe_map = -1, picture_probe_width = 0, picture_probe_height = 0;
    float picture_patch_native_x = 50;
    uint64_t picture_probe_generation = ~0ull, picture_hash = 0, picture_readback_ns = 0;
    bool picture_reference_valid = false;
    auto measure_picture = [&](int width, int height) {
        picture_sample = {};
        picture_hash = picture_readback_ns = 0;
        if (!args.frame_picture_log || !wsc)
            return;
        const auto start = SDL_GetTicksNS();
        auto *surface = SDL_RenderReadPixels(renderer, nullptr);
        auto *rgb = surface ? SDL_ConvertSurface(surface, SDL_PIXELFORMAT_XRGB8888) : nullptr;
        if (!rgb) {
            SDL_DestroySurface(surface);
            picture_motion.reset();
            picture_reference_valid = false;
            return;
        }
        std::vector<uint32_t> pixels(size_t(rgb->w * rgb->h));
        for (int y = 0; y < rgb->h; ++y)
            std::memcpy(pixels.data() + y * rgb->w, static_cast<uint8_t *>(rgb->pixels) + y * rgb->pitch,
                        size_t(rgb->w) * 4);
        SDL_DestroySurface(rgb);
        SDL_DestroySurface(surface);
        picture_hash = 1469598103934665603ull;
        for (auto pixel : pixels) {
            picture_hash ^= pixel & 0xFFFFFF;
            picture_hash *= 1099511628211ull;
        }
        const auto generation = sawgod::game::Session::of(*wsc).generation();
        if (picture_probe_map != presented_map || picture_probe_generation != generation ||
            picture_probe_width != width || picture_probe_height != height || !mode7_uploaded ||
            host_picture.full_ui()) {
            picture_motion.reset();
            picture_reference_valid = false;
        }
        picture_probe_map = presented_map;
        picture_probe_generation = generation;
        picture_probe_width = width;
        picture_probe_height = height;
        if (mode7_uploaded && !host_picture.full_ui()) {
            const auto dst = GameRect(width, height);
            if (!picture_reference_valid) {
                picture_patch_native_x = 50;
                for (float candidate : {50.f, 174.f, 32.f, 192.f}) {
                    const auto p = mode7_camera.ground(candidate, 79);
                    if (!mode7_source.water_at(p[0] + mode7_source.scroll()[0],
                                               p[1] + mode7_source.scroll()[1])) {
                        picture_patch_native_x = candidate;
                        break;
                    }
                }
                const auto point = mode7_camera.ground(picture_patch_native_x, 79);
                picture_reference = {point[0] + mode7_source.scroll()[0],
                                     point[1] + mode7_source.scroll()[1]};
            }
            const auto point = mode7_camera.project(picture_reference[0] - mode7_source.scroll()[0],
                                                    picture_reference[1] - mode7_source.scroll()[1]);
            const std::array<float, 2> ground{dst.x + point[0] * dst.w / 224, dst.y + point[1] * dst.h / 144};
            const std::array<float, 2> delta =
                picture_reference_valid
                    ? std::array<float, 2>{ground[0] - picture_geometry[0], ground[1] - picture_geometry[1]}
                    : std::array<float, 2>{};
            picture_geometry = ground;
            picture_reference_valid = true;
            auto party = std::find_if(mode7_source.sprites().begin(), mode7_source.sprites().end(),
                                      [](const auto &card) { return card.record == 0xD520; });
            uint64_t art = 0;
            int scale = 0;
            std::span<const uint32_t> actor;
            if (party != mode7_source.sprites().end()) {
                const auto foot = mode7_camera.project(party->x - mode7_source.scroll()[0],
                                                       party->y - mode7_source.scroll()[1]);
                party_geometry = {dst.x + std::round(foot[0] * mode7_width / 224) * dst.w / mode7_width,
                                  dst.y + std::round(foot[1] * mode7_height / 144) * dst.h / mode7_height};
                art = party->id;
                scale = std::max(1, int(std::round(foot[2] * mode7_width / 224)));
                actor = *party->pixels;
            }
            picture_sample = picture_motion.measure(
                pixels, width, height, int(std::round(dst.x + picture_patch_native_x * dst.w / 224)),
                int(std::round(dst.y + 79 * dst.h / 144)), delta, art, party_geometry, scale, actor);
        }
        picture_readback_ns = SDL_GetTicksNS() - start;
    };
    auto present_motion = [&](double phase, bool enabled) {
        display_motion =
            motion_history.sample(phase, enabled && !host_picture.holding() && !overlay.is_open());
        if (motion_trace)
            std::fprintf(motion_trace, "P,%llu,%.6f,%.4f,%.4f,%.4f,%d,%d\n",
                         (unsigned long long)display_motion.to, phase, double(display_motion.camera[0]),
                         double(display_motion.camera[1]), double(display_motion.fraction),
                         int(display_motion.snapped), int(enabled));
        if (!enabled) {
            display_motion.camera = completed_scroll;
            display_motion.actors = completed_actors;
        }
        display_subpixel =
            enabled &&
            (std::abs(display_motion.camera[0] - std::round(display_motion.camera[0])) > 0.00001f ||
             std::abs(display_motion.camera[1] - std::round(display_motion.camera[1])) > 0.00001f);
        if (enabled)
            for (const auto &[record, point] : display_motion.actors) {
                (void)record;
                display_subpixel |= std::abs(point[0] - std::round(point[0])) > 0.00001f ||
                                    std::abs(point[1] - std::round(point[1])) > 0.00001f;
            }
        mode7_source.set_motion(display_motion.camera, display_motion.actors);
        mode7_camera = completed_camera;
        if (mode7_amount > 0) {
            mode7_camera = {args.shot_camera.tilt * mode7_amount, args.shot_camera.lens, mode7_amount,
                            1 + (args.shot_camera.distance - 1) * mode7_amount};
            if (!sawgod::game::GameConfig::mode7_map(mode7_source.map())) {
                int out_w = 224, out_h = 144;
                SDL_GetRenderOutputSize(renderer, &out_w, &out_h);
                const auto dst =
                    GameRect(args.shot_w > 0 ? args.shot_w : out_w, args.shot_h > 0 ? args.shot_h : out_h);
                mode7_camera = sawgod::game::field_zoom_camera(
                    args.shot_camera.field_zoom, mode7_amount, int(std::lround(dst.w)),
                    int(std::lround(dst.h)), display_motion.camera, !display_motion.snapped);
            }
            mode7_view = mode7_source.project(mode7_camera, false,
                                              args.shot_effect_frame >= 0 ? uint64_t(args.shot_effect_frame)
                                                                          : presented_frame);
        }
    };
    auto upload_game = [&] {
        const auto work_before = sawgod::GpuUploadWork::read(renderer);
        start_gpu_timing();
        if (!wsc)
            return;
        if (cur_rgb.empty())
            cur_rgb = game_rgb();
        const std::vector<uint32_t> *src = &cur_rgb;
        if (lcd_on_cpu() && fstn_rgb.size() == cur_rgb.size()) {
            src = &fstn_rgb; // the smear already blends frames
        }
        if (auto &t = sawgod::presentation_diagnostics::trace(); t.enabled()) {
            t.range("Presentation.picture", *src);
            t.range("Presentation.host_ui", host_ui);
            t.range("Presentation.ui_mask", mode7_ui);
            sawgod::presentation_diagnostics::Hash h;
            h.add(enhanced_frame);
            h.add(enhanced_scene.kind);
            h.add(enhanced_scene.map);
            h.add(enhanced_scene.field_cleared);
            h.add(host_picture.holding());
            h.add(mode7_amount);
            h.add(mode7_opacity);
            t.write("Presentation.owner", h);
        }
        native_pending = true;
        // LCD persistence keeps its CPU history; GPU effects and the camera consume the current
        // completed picture.
        // Frame pacing is not a scene/layout restriction.
        enhanced_uploaded = mode7_uploaded = false;
        if (!enhanced_frame)
            battle_target_light = {};
        // A cleared native field owns the visible pixels, including its host message.
        // Keep updating the camera above so map loads and fades never restart its entry.
        if (!enhanced_scene.field_cleared &&
            enhanced_scene.kind != sawgod::game::PresentationScene::Kind::Unknown &&
            (enhanced_frame || mode7_amount > 0) && enhanced_graphics.ready()) {
            sawgod::EnhancedParameters p;
            p.snap = settings.enhanced_snap->on();
            p.shadows = settings.enhanced_shadows->on();
            p.grade = float(settings.enhanced_grade->get()) / 100;
            // 60% = 1.25x the calibrated bloom, tuned on battle backgrounds. Maps (fields and worlds) get a
            // quarter of it: stronger, pale stone towns glow like a light over the whole map (map 001).
            // Tagged lights are composited after bloom and keep their full strength.
            p.bloom = settings.bloom_gain() *
                      (enhanced_scene.kind == sawgod::game::PresentationScene::Kind::Battle ? 1.0f : 0.25f);
            p.gb_weight = enhanced_gb_weight;
            p.effect_weight = enhanced_effect_weight;
            p.palette_range = enhanced_scene.palette_range;
            p.light_strength =
                float(settings.enhanced_lights->get()) * 1.5f / 100; // the same scale for every light type
            p.lights = enhanced_scene.lights;
            p.beam_style = settings.enhanced_beam_style->get();
            p.water_strength = float(settings.enhanced_water->get()) / 100;
            p.water = enhanced_scene.water;
            p.frame = args.shot_effect_frame >= 0 ? uint64_t(args.shot_effect_frame) : presented_frame;
            p.actors = enhanced_scene.actors;
            const auto light_generation = sawgod::game::Session::of(*wsc).generation();
            if (light_generation != battle_light_generation) {
                battle_target_light = {};
                battle_light_generation = light_generation;
            }
            const bool battle = enhanced_scene.kind == sawgod::game::PresentationScene::Kind::Battle;
            const double light_seconds = double(p.frame) * (159.0 * 256 / 3072000);
            const auto gains =
                battle_target_light.gains(battle ? ui.battle.enemy_target(*wsc) : -1, light_seconds);
            p.battle_lighting.weight = sawgod::BattleLighting::strength(
                settings.battle_lighting->on(), battle, enhanced_effect_weight, enhanced_gb_weight);
            p.battle_lighting.groups = enhanced_scene.battle_groups;
            p.battle_lighting.selection = battle_target_light.weights(light_seconds);
            for (size_t g = 0; g < gains.size(); ++g)
                p.battle_lighting.groups[g][3] *= gains[g];
            p.background = enhanced_scene.background;
            if (enhanced_frame && mode7_amount < 1)
                enhanced_uploaded =
                    enhanced_graphics.upload(enhanced_wsc, enhanced_gb, enhanced_scene.mask.data(), p);
            if (mode7_amount > 0 && mode7_renderer.ready() && mode7_graphics.ready()) {
                const auto cpu_start = SDL_GetTicksNS();
                p.effect_weight = enhanced_effect_weight;
                p.actors.clear(); // Mode 7 contact shadows are drawn on the perspective ground before cards.
                p.lights = mode7_view.lights;
                p.water.clear();
                p.background.clear();
                // Effects snap to the field's whole pixels; inside a fidelity fade its scale is fractional.
                // The geometry already has whole-sized art pixels. Re-snapping the finished
                // picture on a fixed native lattice would erase fractional camera/actor motion.
                // At an integer endpoint retain the settled effect grid.
                p.snap = !display_subpixel && mode7_camera.pixel_scale > 0 &&
                         mode7_camera.pixel_scale == std::floor(mode7_camera.pixel_scale) &&
                         settings.enhanced_snap->on();
                if (mode7_camera.pixel_scale > 0) {
                    p.effect_grid = {224 * mode7_camera.output_scale[0] / mode7_camera.pixel_scale,
                                     144 * mode7_camera.output_scale[1] / mode7_camera.pixel_scale};
                    p.effect_origin = mode7_camera.origin;
                }
                int out_w = 224, out_h = 144;
                SDL_GetRenderOutputSize(renderer, &out_w, &out_h);
                if (args.shot_w > 0)
                    out_w = args.shot_w;
                if (args.shot_h > 0)
                    out_h = args.shot_h;
                const auto destination = GameRect(out_w, out_h);
                const auto scene_size = sawgod::app::enhanced_scene_size(
                    int(std::lround(destination.w)), int(std::lround(destination.h)),
                    settings.pacing_mode() == sawgod::Pacing::LoSpec);
                const int width = scene_size[0], height = scene_size[1];
                if (width != mode7_width || height != mode7_height) {
                    mode7_graphics.shutdown(); // release sampler bindings before replacing plane targets
                    if (mode7_renderer.resize(width, height) &&
                        mode7_graphics.init(renderer, mode7_renderer.picture(), mode7_renderer.ownership(),
                                            mode7_renderer.water(), &enhanced_graphics)) {
                        mode7_width = width;
                        mode7_height = height;
                    }
                }
                // Keep the output camera for layout, motion, and host overlays. Only rasterization shrinks.
                auto scene_camera = mode7_camera;
                const float ratio = float(width) / destination.w;
                scene_camera.pixel_scale *= ratio;
                scene_camera.output_scale[0] *= ratio;
                scene_camera.output_scale[1] *= float(height) / destination.h;
                mode7_uploaded =
                    mode7_renderer.draw(mode7_source, scene_camera, p.snap, mode7_ui.data(),
                                        effective_screen() == sawgod::ScreenMode::Ips, p.shadows, ips_amount,
                                        p.palette_range[1] - p.palette_range[0], 1.f / ratio);
                log_m7_draw += SDL_GetTicksNS() - cpu_start;
                const uint64_t effects_start = SDL_GetTicksNS();
                mode7_uploaded =
                    mode7_uploaded && mode7_graphics.upload(enhanced_wsc, enhanced_gb, mode7_ui.data(), p);
                log_m7_effects += SDL_GetTicksNS() - effects_start;
                if (mode7_amount == 1 && SDL_getenv("SAWGOD_MODE7_TIMING"))
                    SDL_Log("Mode7 GPU submit CPU us: %.3f", double(SDL_GetTicksNS() - cpu_start) / 1000);
            }
        }
        // An opaque camera with no protected UI never samples the native picture. Keep its
        // CPU pixels for fallbacks/badges, but avoid uploading and enlarging invisible pixels.
        if (!mode7_uploaded || mode7_opacity < 1 || !mode7_graphics.ui_regions.empty())
            upload_native();
        log_upload_work = sawgod::GpuUploadWork::read(renderer) - work_before;
    };
    // A wipe owns the last complete field, at every Graphics endpoint. Its Enhanced endpoint is
    // derived on entry from retained field inputs, never from a partially faded presentation texture.
    // Reuse the ordinary render passes once, then restore their live inputs before drawing the wipe.
    const auto picture_buffers = [&] {
        return std::tie(cur_rgb, enhanced_wsc, enhanced_gb, enhanced_scene, host_ui, mode7_ui, menu_fills,
                        menu_text);
    };
    const auto picture_values = [&] {
        // Only projected lights are consumed from mode7_view by upload_game. Its 224x144 mask,
        // background and actor records are not presentation inputs; do not retain that empty raster.
        return std::tie(fidelity, presented_frame, mode7_amount, mode7_camera, mode7_view.lights,
                        mode7_opacity, enhanced_frame, enhanced_effect_weight, enhanced_gb_weight,
                        menu_fill_weight, ips_amount);
    };
    const auto picture_state = [&] {
        using Buffers = std::tuple<decltype(cur_rgb), decltype(enhanced_wsc), decltype(enhanced_gb),
                                   decltype(enhanced_scene), decltype(host_ui), decltype(mode7_ui),
                                   decltype(menu_fills), decltype(menu_text)>;
        using Values =
            std::tuple<decltype(fidelity), decltype(presented_frame), decltype(mode7_amount),
                       decltype(mode7_camera), decltype(mode7_view.lights), decltype(mode7_opacity),
                       decltype(enhanced_frame), decltype(enhanced_effect_weight),
                       decltype(enhanced_gb_weight), decltype(menu_fill_weight), decltype(ips_amount)>;
        return std::make_unique<std::pair<Buffers, Values>>(picture_buffers(), picture_values());
    };
    const auto restore_picture = [&](const auto &state) {
        picture_buffers() = state->first;
        picture_values() = state->second;
    };
    uint64_t wipe_picture_revision = ~uint64_t(0);
    uint64_t bulk_retains = 0;
    const auto retain_picture = [&](auto &state) {
        if (!state || wipe_picture_revision != picture_revision)
            ++bulk_retains;
        if (!state)
            state = picture_state();
        else {
            if (wipe_picture_revision != picture_revision)
                state->first = picture_buffers();
            state->second = picture_values();
        }
        wipe_picture_revision = picture_revision;
    };
    decltype(picture_state()) wipe_field;
    bool was_wiping = false;
    int wipe_field_map = -1;
    bool wipe_field_camera = false;
    auto update_touch = [&](int width, int height) {
        touch.show(wsc && touch_foreground && settings.touch_enabled(input.real_controller_connected()),
                   overlay.is_open());
        touch.layout(window, renderer, float(width), float(height), GameRect(width, height).w);
    };
    uint64_t retain_ns = 0, retain_cpu_ns = 0;
    bool retained_picture = false;
    auto draw_frame = [&](int out_w, int out_h, bool fullscreen) {
        sawgod::CpuScope cpu_scope("draw_frame");
        retain_ns = retain_cpu_ns = 0;
        retained_picture = false;
        update_touch(out_w, out_h);
        const bool wiping = wsc && sawgod::game::presented_battle_entry_wipe(*wsc);
        if (wiping && !options_canvas_submitted && !was_wiping && wipe_field &&
            (!wipe_field_camera || mode7_source.map() == wipe_field_map)) {
            auto live = picture_state();
            restore_picture(wipe_field);
            fidelity = sawgod::Fidelity(sawgod::FidelityMode::Enhanced, fidelity.music());
            cur_rgb = enhanced_wsc;
            menu_fill_weight = 1;
            mode7_amount = mode7_opacity = wipe_field_camera ? 1.f : 0.f;
            // The wipe forces the full Enhanced level above, so its effects are at full strength too. A
            // snapshot taken inside a level crossfade must not carry that fade's partial weight into the
            // wipe.
            enhanced_effect_weight = 1.f;
            enhanced_frame = enhanced_effect_weight > 0;
            enhanced_gb_weight = 0;
            ips_amount = 0.5f;
            mode7_camera = {args.shot_camera.tilt, args.shot_camera.lens, 1, args.shot_camera.distance};
            if (wipe_field_camera && !sawgod::game::GameConfig::mode7_map(wipe_field_map)) {
                const auto dst = GameRect(out_w, out_h);
                mode7_camera =
                    sawgod::game::field_zoom_camera(args.shot_camera.field_zoom, 1, int(std::lround(dst.w)),
                                                    int(std::lround(dst.h)), mode7_source.scroll());
            }
            if (wipe_field_camera)
                mode7_view = mode7_source.project(
                    mode7_camera, false,
                    args.shot_effect_frame >= 0 ? uint64_t(args.shot_effect_frame) : presented_frame);
            upload_game();
            draw_scene(out_w, out_h, fullscreen, true);
            restore_picture(live);
            upload_game();
        }
        was_wiping = wiping && !options_canvas_submitted;
        draw_scene(out_w, out_h, fullscreen);
        if (wsc && !wiping && !options_canvas_submitted && !host_picture.holding() &&
            enhanced_scene.kind == sawgod::game::PresentationScene::Kind::Field) {
            const auto retain_cpu_start = live_check ? sawgod::app::LivePictureCheck::cpu_time() : 0;
            const auto retain_start = live_check ? SDL_GetTicksNS() : 0;
            retain_picture(wipe_field);
            retained_picture = true;
            if (live_check) {
                retain_ns = SDL_GetTicksNS() - retain_start;
                retain_cpu_ns = sawgod::app::LivePictureCheck::cpu_time() - retain_cpu_start;
            }
            wipe_field_map = enhanced_scene.map;
            wipe_field_camera = sawgod::game::field_zoom_allowed(
                                    wipe_field_map, sawgod::FidelityMode::Enhanced, true, false) ||
                                sawgod::game::mode7_allowed(wipe_field_map, sawgod::FidelityMode::Enhanced,
                                                            true, !settings.enhanced_mode7->on());
        }
        touch.render(renderer);
    };
    auto write_timing = [&](uint64_t tick_ns, uint64_t upload_ns, uint64_t draw_ns, uint64_t present_ns,
                            int ran, int speed, bool accelerated, bool clock_reset, int width, int height) {
        if (!timing_log.enabled())
            return;
        sawgod::app::FrameTimingSample sample;
        sample.time = SDL_GetTicksNS();
        sample.presentation_time = simulated_present_time.value_or(sample.time);
        sample.ran = ran;
        sample.speed = speed;
        sample.accelerated = accelerated;
        sample.clock_reset = clock_reset;
        sample.game_frame = wsc ? wsc->frame_count : 0;
        sample.picture_frame = presented_frame;
        sample.generation = wsc ? sawgod::game::Session::of(*wsc).generation() : 0;
        sample.production_end = production_end;
        sample.graphics = int(fidelity.graphics());
        sample.map = wsc ? wsc->ram[0xD0DC] : -1;
        sample.picture_map = presented_map;
        sample.width = width;
        sample.height = height;
        sample.mode7 = mode7_amount;
        sample.mode7_drawn = mode7_uploaded;
        sample.pacing = int(settings.pacing_mode());
        sample.screen = int(effective_screen());
        sample.scene = int(enhanced_scene.kind);
        sample.battle = ui.battle.active();
        sample.full_ui = host_picture.full_ui();
        sample.field_picture =
            wsc && sawgod::game::recognized_field(*wsc) && !sawgod::game::presented_battle_entry_wipe(*wsc);
        sample.pixel_scale = mode7_camera.pixel_scale;
        sample.scroll = sawgod::interpolated_pacing(settings.pacing_mode()) &&
                                fidelity.graphics() == sawgod::FidelityMode::Enhanced && mode7_amount > 0
                            ? display_motion.camera
                            : presented_scroll;
        sample.interpolation = display_motion.fraction;
        sample.from_frame = display_motion.from;
        sample.to_frame = display_motion.to;
        sample.snapped = display_motion.snapped;
        sample.origin = mode7_camera.origin;
        sample.pixel_probe = args.frame_picture_log;
        sample.picture_hash = picture_hash;
        sample.upload_work = log_upload_work;
        sample.picture_readback = picture_readback_ns;
        sample.ground_geometry = picture_geometry;
        sample.party_geometry = party_geometry;
        sample.pixels = picture_sample;
        sample.held = host_picture.holding();
        sample.paused = overlay.is_open();
        const auto *display = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
        sample.refresh = args.shot_display_hz ? float(args.shot_display_hz)
                         : display            ? display->refresh_rate
                                              : 0;
        SDL_GetRenderVSync(renderer, &sample.vsync);
        const auto &surface = mode7_source.surface();
        sample.topology = surface.revision;
        sample.tiles = surface.tile_revision;
        sample.palettes = surface.palette_revision;
        sample.work = {tick_ns,
                       upload_ns,
                       log_describe,
                       log_m7_update,
                       log_m7_project,
                       log_m7_draw,
                       log_m7_effects,
                       draw_ns,
                       present_ns,
                       log_game_tick,
                       mode7_renderer.atlas_ns,
                       mode7_renderer.pass_ns[0],
                       mode7_renderer.pass_ns[1],
                       mode7_renderer.pass_ns[2],
                       enhanced_graphics.pass_ns[0],
                       enhanced_graphics.pass_ns[1],
                       enhanced_graphics.pass_ns[2] + enhanced_graphics.pass_ns[3],
                       mode7_graphics.pass_ns[0],
                       mode7_graphics.pass_ns[1],
                       mode7_graphics.pass_ns[2] + mode7_graphics.pass_ns[3],
                       log_focus,
                       enhanced_graphics.ui_ns + mode7_graphics.ui_ns};
        const auto audio_stats = audio.stats();
        sample.audio_queued = float(audio_stats.queued) * 1000 / sawgod::app::AudioQueue::kRate;
        sample.audio_target = float(audio_stats.target) * 1000 / sawgod::app::AudioQueue::kRate;
        sample.audio_underruns = audio_stats.underruns;
        sample.audio_trimmed = audio_stats.trimmed;
        sample.title_active = wsc && host_picture.title_menu().active(*wsc);
        sample.synth = enhanced_audio.cost_ms();
        sample.audio_dropped = enhanced_audio.dropped();
        sample.present_id = timing_present_id++;
#ifdef SAWGOD_GPU_TIMESTAMPS
        sample.gpu_valid = SawGodRendererTimingPoll(renderer, &sample.gpu_present_id, &sample.gpu_ms);
#endif
        if (sample.gpu_valid && sample.refresh > 0 && sample.gpu_ms > 1000.0 / sample.refresh)
            ++gpu_over_budget_count;
        sample.gpu_over_budget_count = gpu_over_budget_count;
        timing_log.write(sample);
        enhanced_graphics.pass_ns = mode7_graphics.pass_ns = {};
        mode7_renderer.pass_ns = {};
        enhanced_graphics.ui_ns = mode7_graphics.ui_ns = mode7_renderer.atlas_ns = log_focus = 0;
        log_describe = log_m7_update = log_m7_project = log_m7_draw = log_m7_effects = 0;
    };
    auto submit_options_return = [&] {
        if (!options_canvas_submitted || overlay.is_open())
            return;
        host_options_open = false;
        motion_history.reset();
        cur_rgb = game_rgb();
        fstn_rgb.clear();
        upload_game();
        options_canvas_submitted = false;
    };
    auto destroy_textures = [&](bool recovering = false) {
        touch.release_renderer();
        battle_wipe.release();
        world_card_overlay.shutdown();
        title_logo_overlay.shutdown();
        mode7_focus.shutdown();
        scene_scaler.release();
        mode7_graphics.shutdown();
        ff_vfx.shutdown();
        mode7_renderer.shutdown();
        enhanced_graphics.shutdown();
        if (enhanced_scaler.big)
            SDL_DestroyTexture(enhanced_scaler.big);
        enhanced_scaler.big = nullptr;
        if (recovering)
            overlay.release_renderer();
        else
            overlay.shutdown();
        lcd_grid.release();
        zoom_backdrop.release();
        if (game_scaler.big)
            SDL_DestroyTexture(game_scaler.big);
        game_scaler.big = nullptr;
        SDL_DestroyTexture(backdrop);
        if (canvas)
            SDL_DestroyTexture(canvas);
        SDL_DestroyTexture(texture);
        SDL_DestroyTexture(shot_target);
        texture = backdrop = canvas = shot_target = nullptr;
        backdrop_source.clear();
        backdrop_uploaded.clear();
        canvas_w = canvas_h = 0;
        uploaded_rgb.clear();
        native_pending = false;
        enhanced_uploaded = mode7_uploaded = false;
    };

    // All renderer-owned resources share one lifetime. Recreate even on a targets-only reset: cached
    // outputs, initialization-only art and revision keys must never outlive the content they describe.
    auto render_event = [&](const SDL_Event &event) {
        if (event.type != SDL_EVENT_RENDER_TARGETS_RESET && event.type != SDL_EVENT_RENDER_DEVICE_RESET &&
            event.type != SDL_EVENT_RENDER_DEVICE_LOST)
            return true;
        if (event.render.windowID && event.render.windowID != SDL_GetWindowID(window))
            return true;
        SDL_SetRenderTarget(renderer, nullptr);
        destroy_textures(true);
        hd_menus.shutdown();
        if (event.type == SDL_EVENT_RENDER_DEVICE_LOST) {
            presentation.shutdown();
            const bool restored =
                presentation.init(window, settings.renderer->get() == 1, settings.identity_shader->on());
            renderer = presentation.get();
            if (!restored)
                return false;
        }
        SDL_SetRenderVSync(renderer, sawgod::presentation_diagnostics::cpu_only() ? 0 : 1);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                    sawgod::kNativeWidth, sawgod::kNativeHeight);
        backdrop = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                     sawgod::kNativeWidth, sawgod::kNativeHeight);
        if (!texture || !backdrop || !SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST) ||
            !SDL_SetTextureScaleMode(backdrop, SDL_SCALEMODE_LINEAR) ||
            !world_card_overlay.prepare(renderer) || !title_logo_overlay.reset_renderer(renderer) ||
            !overlay.restore_renderer(renderer) || (wsc && !overlay.build_skin(wsc->rom, gb_hand_png)))
            return false;
        hd_menus.prepare(renderer);
        mode7_focus.init(renderer);
        const bool flat = enhanced_graphics.init(renderer);
        const bool world = mode7_renderer.init(renderer) &&
                           mode7_renderer.resize(mode7_width, mode7_height) &&
                           mode7_graphics.init(renderer, mode7_renderer.picture(), mode7_renderer.ownership(),
                                               mode7_renderer.water(), &enhanced_graphics);
        mode7_supported = flat && world;
        if (presentation.is_gpu() && !mode7_supported)
            return false;
        was_wiping = false; // rebuild a mid-wipe snapshot from the retained CPU field inputs
        if (overlay.is_open())
            make_backdrop();
        const auto before_upload = sawgod::GpuUploadWork::read(renderer);
        upload_game();   // no native tick: the current picture must be resident before its next draw
        upload_native(); // Recovery also initializes the fallback, even for an opaque camera.
        const auto upload = sawgod::GpuUploadWork::read(renderer) - before_upload;
        SDL_Log("Render recovery uploads: native=%u bytes=%llu calls=%llu", unsigned(uploaded_rgb.size()),
                (unsigned long long)upload.bytes, (unsigned long long)upload.calls);
        SDL_Log("Render recovery caches: flat=%d/%d mode7=%d/%d", int(enhanced_uploaded),
                int(enhanced_graphics.last_upload_full), int(mode7_uploaded),
                int(mode7_renderer.last_upload_full));
        if (wsc && uploaded_rgb.size() != size_t(sawgod::kNativeWidth * sawgod::kNativeHeight))
            return false;
        SDL_Log("Presentation resources rebuilt after render event %u", unsigned(event.type));
        return true;
    };

    if (!args.shot.empty())
        world_card_overlay.wait_for_decode(); // deterministic offline capture, outside the live frame loop
    // --shot-overlay: a picture of the menu (or with --shot-page none, of the game), then exit.
    if (!args.shot.empty()) {
        finish_enhanced_setup();
        int code = 0;
        sawgod::app::ShotSoak soak;
        if (!args.shot_soak.empty()) {
            if (!soak.open(args.shot_soak))
                return 1;
            if (!wsc)
                return 1;
            for (int level = 2; level > args.shot_start_level; --level)
                fidelity.step(-1, 0);
            wsc->on_irq = [&](const sawgod::emu::Wsc &w) { soak.irq(w, movie_frame); };
        }
        FILE *shot_frame_log = nullptr;
        if (const char *path = SDL_getenv("SAWGOD_FRAME_LOG"); path && *path) {
            shot_frame_log = std::fopen(path, "w");
            if (shot_frame_log)
                std::fprintf(
                    shot_frame_log,
                    "fixture_view,walk_px,map,mode7,m7_update_ms,m7_project_ms,m7_draw_ms,m7_effects_ms\n");
        }
        if (!wsc || !overlay.ready()) {
            std::fprintf(stderr, "sawgod: --shot-overlay needs a ROM (the menu is drawn with its font)\n");
            code = 1;
        } else {
            // The accelerated readback loop bypasses live input/presentation scheduling. Enable both
            // pictures explicitly so fixed --shot-gb-weight samples use the same live GB composition.
            if (gfx_swap.has_data())
                gfx_swap.set_enabled(*wsc, true);
            const uint64_t preview_frame = wsc->frame_count;
            size_t next_step = 0;
            constexpr uint64_t step_times[] = {500'000'000, 1'500'000'000, 2'500'000'000, 3'500'000'000};
            constexpr int step_directions[] = {-1, -1, 1, 1};
            const std::string shot_prefix = args.shot;
            const std::string mask_prefix = args.shot_mask, state_prefix = args.shot_state;
            const size_t views = args.shot_run ? size_t(args.shot_run) : args.map_shots.size();
            for (size_t view = 0; view < std::max<size_t>(1, views); ++view) {
                sawgod::presentation_diagnostics::trace().begin(wsc->frame_count + 1);
                log_describe = log_m7_update = log_m7_project = log_m7_draw = log_m7_effects = 0;
                if (args.shot_mode7_steps) {
                    shot_fidelity_time = uint64_t(view) * 25'000'000;
                    args.shot_effect_frame = int(std::lround(double(view) * 75.47 / 40));
                    // WSC hold -> Enhanced -> camera hold/walk -> WSC. Same live fade classes.
                    if (view == 0 && !args.shot_mode7_entry)
                        fidelity.step(-1, 0);
                    if (view == 20 && !args.shot_mode7_entry)
                        fidelity.step(1, *shot_fidelity_time);
                    if (view == 140 && !args.shot_mode7_entry)
                        fidelity.step(-1, *shot_fidelity_time);
                }
                if (args.shot_fidelity_steps) {
                    shot_fidelity_time = uint64_t(view) * 25'000'000; // 40 samples/s; GIF plays 20/s
                    while (next_step < 4 && step_times[next_step] <= *shot_fidelity_time) {
                        fidelity.step(step_directions[next_step], step_times[next_step]);
                        ++next_step;
                    }
                }
                if (!args.map_shots.empty()) {
                    // Independent batch stills settle their own camera endpoint, not a prior map's fade.
                    if (!args.shot_mode7_steps && !args.shot_fidelity_steps && !args.shot_field_walk) {
                        mode7_fade = {};
                        scene_effect_fade.settle_next();
                        camera_was_hidden = true;
                        mode7_view = {};
                        mode7_source = {};
                    }
                    const auto &shot = args.map_shots[view];
                    args.shot_map = shot.map;
                    args.shot_x = shot.x;
                    args.shot_y = shot.y;
                    args.shot = shot.output;
                    wsc->frame_count =
                        preview_frame + (args.shot_field_walk ? view : 0); // fixture source clock
                }
                if (args.shot_field_walk)
                    args.shot_effect_frame = int(view);
                // Accelerated entering/leaving-world fixture, through the same observer and camera gate.
                if (args.shot_mode7_entry && (view < 20 || view >= 140)) {
                    args.shot_map = 1;
                    args.shot_x = args.shot_y = 0;
                }
                const int walk = args.shot_field_walk ? int(view)
                                 : args.shot_mode7_steps && !args.shot_mode7_entry && !args.shot_mode7_no_walk
                                     ? std::clamp((int(view) - 80), 0, args.shot_mode7_north ? 16 : 48)
                                     : 0;
                if (args.shot_map >= 0) {
                    if (!sawgod::app::prepare_shot_field(*wsc, args, walk, gfx_swap)) {
                        SDL_Log("Invalid field preview");
                        destroy_textures();
                        hd_menus.shutdown();
                        presentation.shutdown();
                        SDL_DestroyWindow(window);
                        touch.shutdown();
                        SDL_Quit();
                        return 1;
                    }
                    cur_rgb.clear(); // each batch viewport collects its own scene and pixels
                }
                int ff_ticks = -1;
                if (args.shot_ff_review && args.shot_display_hz > 0) {
                    const auto elapsed = [&](size_t v) {
                        const uint64_t t = uint64_t(v) * 1'000'000'000 / unsigned(args.shot_display_hz);
                        const uint64_t hold =
                            std::min<uint64_t>(t > 600'000'000 ? t - 600'000'000 : 0, 2'600'000'000);
                        return (t + hold * unsigned(settings.fast_forward_speed() - 1)) / frame_ns;
                    };
                    ff_ticks = view ? int(elapsed(view) - elapsed(view - 1)) : args.frames.value_or(0) + 1;
                    const uint64_t t = uint64_t(view) * 1'000'000'000 / unsigned(args.shot_display_hz);
                    fast_forwarding = t >= 600'000'000 && t < 3'200'000'000;
                }
                int shot_ticks = 0;
                for (int i = 0;
                     args.shot_map < 0 &&
                     i < (ff_ticks >= 0 ? ff_ticks : sawgod::app::shot_view_ticks(args, view, frame_ns));
                     ++i) {
                    if (args.shot_run) { // the fades' clock follows the game frames, not the accelerated run
                        shot_fidelity_time = args.shot_soak.empty()
                                                 ? movie_frame * frame_ns
                                                 : sawgod::Fidelity::kFadeNs + view * frame_ns;
                        for (const auto &[frame, direction] : args.shot_steps)
                            if (frame == (args.shot_soak.empty() ? movie_frame : view))
                                fidelity.step(direction, *shot_fidelity_time);
                    }
                    if (wsc && sawgod::game::Session::find(*wsc)) // Options > Cheats, as in the live loop
                        cheats.set(sawgod::game::Session::of(*wsc), sawgod::app::cheat_flags(settings));
                    tick();
                    ++shot_ticks;
                    enhanced_audio.take();
                    if (args.shot_run && shot_frame_log)
                        std::fprintf(shot_frame_log, "%llu,%d,%d,%.3f,%d,%d,0,0\n",
                                     (unsigned long long)movie_frame, int(enhanced_scene.kind),
                                     enhanced_scene.map, double(mode7_amount),
                                     int(enhanced_scene.perspective_allowed), int(ui.dialogue.active()));
                }
                if (args.shot_run) {
                    char suffix[32];
                    std::snprintf(suffix, sizeof(suffix), "_%03zu.png", view);
                    args.shot = shot_prefix + suffix;
                    if (args.shot_ff_review) {
                        if (!mask_prefix.empty())
                            args.shot_mask = mask_prefix + suffix;
                    }
                    if (!state_prefix.empty() && (args.shot_ff_review || args.shot_state_frames))
                        args.shot_state = state_prefix + suffix + ".bin";
                }
                // Metadata-only replay still runs the normal game tick, host composition, scene
                // observer and camera state. No picture is requested in this opt-in diagnostic pass.
                if (args.shot_audit_only)
                    continue;
                const char options_key =
                    view < args.shot_options_sequence.size() ? args.shot_options_sequence[view] : '.';
                if (options_key == 't') {
                    if (overlay.is_open())
                        close_menu();
                    else {
                        overlay.set_open(true);
                        host_options_open = true;
                        make_backdrop();
                    }
                }
                const bool menu =
                    args.shot_options_sequence.empty() ? args.shot_page != "none" : overlay.is_open();
                if (menu && args.shot_options_sequence.empty()) {
                    overlay.set_open(true);
                    host_options_open = true;
                    make_backdrop();
                    if (!args.shot_page.empty() && !overlay.show(args.shot_page, args.shot_row))
                        std::fprintf(stderr, "sawgod: no options page %s\n", args.shot_page.c_str());
                    else if (args.shot_page.empty() && args.shot_row >= 0)
                        overlay.show("0", args.shot_row);
                }
                const auto shot_upload_start = SDL_GetTicksNS();
                for (const auto &[at, type] : args.shot_render_resets)
                    if (at == view) {
                        SDL_Event event{};
                        event.type = type;
                        event.render.windowID = SDL_GetWindowID(window);
                        // Inject through SDL's event queue, then use the live loop's handler.
                        SDL_PushEvent(&event);
                        while (SDL_PollEvent(&event))
                            if (!render_event(event)) {
                                code = 1;
                                running = false;
                            }
                    }
                if (!running)
                    break;
                if (args.shot_display_hz)
                    simulated_present_time = uint64_t(view) * 1'000'000'000 / unsigned(args.shot_display_hz);
                const double phase =
                    args.shot_display_hz
                        ? double((uint64_t(view) * 1'000'000'000 / unsigned(args.shot_display_hz)) %
                                 frame_ns) /
                              double(frame_ns)
                        : 1;
                present_motion(phase, sawgod::interpolated_pacing(settings.pacing_mode()));
                upload_game();
                const auto shot_upload_ns = SDL_GetTicksNS() - shot_upload_start;
                if (args.shot_badges) {
                    fast_forwarding = true; // presentation fixture: no fast-forward simulation ticks
                    encounters_badge_until = SDL_GetTicksNS() + 1'000'000'000;
                }
                const int w = args.shot_w > 0 ? args.shot_w : settings.window_width->get();
                const int h = args.shot_h > 0 ? args.shot_h : settings.window_height->get();
                if (!args.shot_options_sequence.empty() && overlay.is_open()) {
                    render_menu(sawgod::app::shot_menu_actions(options_key), 1.0f / 75, w, h);
                }
                if (menu && args.shot_options_sequence.empty()) {
                    for (int i = 0; i < 2; ++i) // (ImGui settles its windows on the first frame)
                        render_menu({}, 1.0f / 75, w, h);
                    for (char k : args.shot_keys)
                        render_menu(sawgod::app::shot_menu_actions(k), 1.0f / 75, w, h);
                }
                // One target for the whole run: a target per view is never reclaimed without a present.
                if (!shot_target || shot_target->w != w || shot_target->h != h) {
                    SDL_DestroyTexture(shot_target);
                    shot_target =
                        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
                }
                SDL_Texture *target = shot_target;
                SDL_SetRenderTarget(renderer, target);
                submit_options_return();
                const auto shot_draw_start = SDL_GetTicksNS();
                draw_frame(w, h, settings.fullscreen->on());
                const auto shot_draw_ns = SDL_GetTicksNS() - shot_draw_start;
                measure_picture(w, h);
                if (args.shot_benchmark > 0) {
                    sawgod::app::ShotBenchmark bench;
                    for (int i = 0; i < args.shot_benchmark + 10; ++i) {
                        const auto start = SDL_GetTicksNS();
#ifdef SAWGOD_GPU_TIMESTAMPS
                        const bool gpu_timed = SawGodRendererTimingStart(renderer, Uint64(1000000 + i));
#endif
                        // Include CPU scene ownership/source collection and texture uploads for one frame.
                        cur_rgb = game_rgb();
                        upload_game();
                        const auto collected = SDL_GetTicksNS();
                        draw_frame(w, h, settings.fullscreen->on());
                        // FlushRenderer only encodes GPU commands. A tiny diagnostic
                        // readback submits the queue and waits on its fence; its overhead
                        // is included in both ordinary/enhanced costs. Never used in play.
                        const SDL_Rect sync_pixel{0, 0, 1, 1};
                        SDL_Surface *sync = SDL_RenderReadPixels(renderer, &sync_pixel);
                        if (!sync) {
                            sawgod::debug_log::warnf("gpu", "Presentation timing synchronization failed: %s",
                                                     SDL_GetError());
                            code = 1;
                            break;
                        }
                        SDL_DestroySurface(sync);
#ifdef SAWGOD_GPU_TIMESTAMPS
                        Uint64 gpu_id = 0;
                        double gpu_ms = 0;
                        // Results arrive a submit or more later; keep any completed benchmark frame.
                        if (gpu_timed && SawGodRendererTimingPoll(renderer, &gpu_id, &gpu_ms) &&
                            gpu_id >= 1000010)
                            bench.gpu.push_back(gpu_ms);
#endif
                        if (i >= 10) {
                            bench.total.push_back(double(SDL_GetTicksNS() - start) / 1e6);
                            bench.cache.push_back(double(menu_cache_ns) / 1e6);
                            bench.fill.push_back(double(menu_fill.cpu_ns) / 1e6);
                            bench.text.push_back(double(hd_menus.cpu_ns) / 1e6);
                            bench.cpu.push_back(double(collected - start) / 1e6);
                        }
                    }
                    sawgod::app::log_shot_benchmark(w, h, bench, unsigned(hd_menus.strokes), hd_menus.draws,
                                                    unsigned(menu_fill.quads));
                }
                if (!args.shot_mask.empty()) {
                    sawgod::app::write_shot_masks(args.shot_mask, enhanced_scene,
                                                  enhanced_uploaded || mode7_uploaded);
                    sawgod::app::log_shot_gpu_work(
                        mode7_renderer, mode7_uploaded ? mode7_graphics : enhanced_graphics, mode7_uploaded,
                        (mode7_uploaded && mode7_opacity >= 1) || enhanced_uploaded
                            ? 0
                            : uint64_t(GameRect(w, h).w) * uint64_t(GameRect(w, h).h));
                    sawgod::app::log_shot_lights(enhanced_scene, mode7_view);
                }
                if (!args.shot_state.empty())
                    sawgod::emu::write_file(
                        args.shot_state,
                        sawgod::app::shot_state(*wsc, args.shot_state_frames ? &cur_rgb : nullptr));
                if (shot_frame_log && !args.shot_run)
                    std::fprintf(shot_frame_log, "%zu,%d,%d,%.2f,%.6f,%.6f,%.6f,%.6f\n", view, walk,
                                 enhanced_scene.map, double(mode7_amount), double(log_m7_update) / 1e6,
                                 double(log_m7_project) / 1e6, double(log_m7_draw) / 1e6,
                                 double(log_m7_effects) / 1e6);
                if (!args.shot_run)
                    audit_frame(int64_t(view));
                const bool capture_view = args.shot_captures.empty() ||
                                          std::find(args.shot_captures.begin(), args.shot_captures.end(),
                                                    view) != args.shot_captures.end();
                // Still render every view so retained GPU state follows the complete recording.
                // Outside soak's full-picture hashes, unselected capture views need only a fence.
                const bool timing_view = args.shot_timing_only || (args.shot_soak.empty() && !capture_view);
                const auto readback_start = SDL_GetTicksNS();
                const SDL_Rect sync_pixel{0, 0, 1, 1};
                SDL_Surface *surface =
                    sawgod::presentation_diagnostics::cpu_only()
                        ? nullptr
                        : SDL_RenderReadPixels(renderer, timing_view ? &sync_pixel : nullptr);
                write_timing(0, shot_upload_ns, shot_draw_ns, SDL_GetTicksNS() - readback_start,
                             args.shot_run ? (view ? shot_ticks : 1) : 0, 1, true, false, w, h);
                SDL_SetRenderTarget(renderer, nullptr);
                // The GPU renderer releases a frame's transient resources only when the frame is presented.
                if (args.shot_run) {
                    SDL_RenderPresent(renderer);
                    sawgod::startup::finish();
                    presentation.persist_startup_cache();
                    world_card_overlay.preload();
                }
                if (timing_view) {
                    if (!surface && !sawgod::presentation_diagnostics::cpu_only())
                        code = 1;
                    SDL_DestroySurface(surface);
                    if (code)
                        break;
                    continue;
                }
                SDL_Surface *rgb = surface ? SDL_ConvertSurface(surface, SDL_PIXELFORMAT_XRGB8888) : nullptr;
                if (rgb) {
                    const std::vector<uint32_t> px = sawgod::app::surface_rgb(*rgb);
                    if (!args.shot_soak.empty()) {
                        const auto weights = fidelity.graphics_weights(*shot_fidelity_time);
                        const bool settled = weights[size_t(fidelity.graphics())] == 1;
                        soak.frame(
                            view, movie_frame, *wsc, int(fidelity.graphics()), settled, overlay.is_open(),
                            int(enhanced_scene.kind),
                            sawgod::game::recognized_field(*wsc) &&
                                !sawgod::game::presented_battle_entry_wipe(*wsc),
                            ui.battle.active(), host_picture.full_ui(), host_picture.native_full_screen(*wsc),
                            !scene_observer.field_published(*wsc) && wsc->ram[0xD519] != 1,
                            enhanced_scene.map, sawgod::game::GameConfig::mode7_map(enhanced_scene.map),
                            mode7_amount, mode7_uploaded, mode7_camera.pixel_scale, host_picture.holding(),
                            px, view + 1 == views);
                        char context[160];
                        std::snprintf(context, sizeof context,
                                      "view %zu, movie frame %llu, map %d, graphics %d, scene %d", view,
                                      (unsigned long long)movie_frame, enhanced_scene.map,
                                      int(fidelity.graphics()), int(enhanced_scene.kind));
                        sawgod::debug_log::set_context(context);
                        const bool black = std::none_of(px.begin(), px.end(), [](uint32_t p) {
                            return (p >> 16 & 255) >= 16 || (p >> 8 & 255) >= 16 || (p & 255) >= 16;
                        });
                        debug_watch.frame(
                            {*shot_fidelity_time, movie_frame, false, black, host_picture.holding()});
                    }
                    if (!args.shot_soak.empty() &&
                        std::find(args.shot_captures.begin(), args.shot_captures.end(), view) ==
                            args.shot_captures.end()) {
                        // Hash every full RGB readback, retain only a failing view on replay.
                    } else if (sawgod::emu::write_file(args.shot,
                                                       sawgod::emu::encode_png(px.data(), rgb->w, rgb->h)))
                        std::fprintf(stderr, "sawgod: wrote %s (%dx%d)\n", args.shot.c_str(), rgb->w, rgb->h);
                    else
                        code = 1;
                } else {
                    std::fprintf(stderr, "sawgod: cannot read the picture back: %s\n", SDL_GetError());
                    code = 1;
                }
                SDL_DestroySurface(rgb);
                SDL_DestroySurface(surface);
                if (code)
                    break;
            }
        }
        if (shot_frame_log)
            std::fclose(shot_frame_log);
        destroy_textures();
        hd_menus.shutdown();
        presentation.shutdown();
        SDL_DestroyWindow(window);
        touch.shutdown();
        SDL_Quit();
        return code;
    }

    // The splash (app/splash.h), then the game from power-on; an A that skipped it waits to be let go.
    if (wsc && !args.frames) {
        sawgod::app::SplashScreen screen;
        screen.picture = [&](int w, int h) {
            const Rect g = GameRect(w, h);
            return SDL_FRect{g.x, g.y, g.w, g.h};
        };
        if (effective_screen() == sawgod::ScreenMode::Lcd)
            screen.recolor = [](std::vector<uint32_t> &rgb) { sawgod::fstn_color(rgb); };
        else if (effective_screen() == sawgod::ScreenMode::Ips)
            screen.overlay = [&](const SDL_FRect &r) { lcd_grid.draw(renderer, r, ips_amount); };
        screen.work = enhanced_setup;
        running = sawgod::app::run_splash(renderer, input, input_map, screen);
        swallow = input.held(input_map);
    }
    finish_enhanced_setup(); // a skipped or short splash: the rest now, before the first game frame

    if (wsc && live_check) {
        world_card_overlay.wait_for_decode();
        for (int i = 0; i < args.live_warmup; ++i) {
            shot_fidelity_time = 1'000'000'000 + movie_frame * frame_ns;
            tick(false);
        }
    }
    if (wsc) {
        // Loading the soundfont must finish before game packets can accumulate in the worker.
        enhanced_audio.wait_initialized();
        // Exercise the actual window-size presentation path and swapchain before the first sound.
        // Pipeline warm-up on tiny offscreen targets alone does not cover this first present.
        cur_rgb.assign(sawgod::kNativeWidth * sawgod::kNativeHeight, 0);
        sawgod::upload_pixels(texture, cur_rgb.data(), sawgod::kNativeWidth, sawgod::kNativeHeight,
                              uploaded_rgb);
        int ow = 0, oh = 0;
        SDL_GetCurrentRenderOutputSize(renderer, &ow, &oh);
        if (mode7_supported) {
            const std::vector<uint32_t> blank(224 * 144, 0xff808080);
            std::array<uint32_t, 224 * 144> mask{};
            mask.fill(0xffffffff);
            const SDL_FRect destination{initial_game.x, initial_game.y, initial_game.w, initial_game.h};
            const sawgod::EnhancedParameters parameters;
            if (!mode7_renderer.warmup())
                sawgod::debug_log::warnf("gpu", "World camera warm-up failed: %s", SDL_GetError());
            for (auto *effects : {&enhanced_graphics, &mode7_graphics}) {
                if (effects->upload(blank, blank, mask.data(), parameters))
                    if (auto *picture = effects->render(true)) {
                        if (effects == &enhanced_graphics)
                            effects->present(picture, texture, destination);
                        else {
                            mode7_focus.draw(picture, destination, 1, 0.8f);
                            effects->restore_ui(texture, destination);
                        }
                    }
            }
        }
        draw_frame(ow, oh, (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0);
        if (live_check)
            live_pictures.record(renderer, -1, movie_frame, enhanced_scene.map, int(enhanced_scene.kind),
                                 sawgod::game::presented_battle_entry_wipe(*wsc), retained_picture, retain_ns,
                                 0, 0, retain_cpu_ns, 0);
        SDL_RenderPresent(renderer);
        sawgod::startup::finish();
        presentation.persist_startup_cache();
        world_card_overlay.preload();
        if (auto *device = SDL_GetGPURendererDevice(renderer))
            SDL_WaitForGPUIdle(device);
        cur_rgb.clear();
        if (running) {
            audio.open();
            enhanced_audio.set_output_sink(
                [&](std::vector<int16_t> samples) { audio.submit(samples, SDL_GetTicksNS()); });
        }
        next_frame = live_check ? 1'000'000'000 + movie_frame * frame_ns : SDL_GetTicksNS();
    }
    const uint64_t live_origin = next_frame;
    int frame_count = 0;
    uint64_t last_present = SDL_GetTicksNS();
    sawgod::app::LoSpecCadence lospec_cadence;
    sawgod::app::PresentClock present_clock; // Enhanced Smooth's steady refresh clock
    while (running) {
        const auto work_cpu_start = live_check ? sawgod::app::LivePictureCheck::cpu_time() : 0;
        uint64_t t_tick = 0, t_upload = 0, t_draw = 0, t_present = 0;
        int log_speed = 1;
        bool clock_reset = false;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (!render_event(event)) {
                sawgod::debug_log::criticalf("gpu", "Renderer recovery failed: %s", SDL_GetError());
                running = false;
                break;
            }
            input.handle_event(event);
            if (event.type == SDL_EVENT_WILL_ENTER_BACKGROUND ||
                event.type == SDL_EVENT_DID_ENTER_BACKGROUND || event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
                event.type == SDL_EVENT_WINDOW_MINIMIZED) {
                touch_foreground = false;
                touch.show(false);
            } else if (event.type == SDL_EVENT_DID_ENTER_FOREGROUND ||
                       event.type == SDL_EVENT_WINDOW_FOCUS_GAINED ||
                       event.type == SDL_EVENT_WINDOW_RESTORED) {
                touch_foreground = true;
            }
            int touch_w = 1, touch_h = 1;
            SDL_GetCurrentRenderOutputSize(renderer, &touch_w, &touch_h);
            update_touch(touch_w, touch_h);
            if (touch.event(event, window, args.touch_controls))
                continue;
            switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_WINDOW_RESIZED: {
                // remember the windowed size (not fullscreen or maximized)
                const SDL_WindowFlags f = SDL_GetWindowFlags(window);
                if (!(f & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED))) {
                    if (event.window.data1 != settings.window_width->get())
                        config.set(*settings.window_width, event.window.data1);
                    if (event.window.data2 != settings.window_height->get())
                        config.set(*settings.window_height, event.window.data2);
                }
                break;
            }
            case SDL_EVENT_KEY_DOWN:
                if ((event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) &&
                    (event.key.mod & SDL_KMOD_ALT) && !event.key.repeat) {
                    config.set(*settings.fullscreen, !settings.fullscreen->on());
                    apply(*settings.fullscreen);
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            case SDL_EVENT_MOUSE_WHEEL:
            case SDL_EVENT_WINDOW_MOUSE_LEAVE:
                if (overlay.is_open()) {
                    // window coordinates -> render output pixels -> menu units
                    float mx = 0, my = 0;
                    if (event.type == SDL_EVENT_MOUSE_MOTION) {
                        mx = event.motion.x;
                        my = event.motion.y;
                    } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                        mx = event.wheel.mouse_x;
                        my = event.wheel.mouse_y;
                    } else {
                        mx = event.button.x;
                        my = event.button.y;
                    }
                    int ww = 1, wh = 1, ow = 1, oh = 1;
                    SDL_GetWindowSize(window, &ww, &wh);
                    SDL_GetCurrentRenderOutputSize(renderer, &ow, &oh);
                    mx *= float(ow) / float(std::max(ww, 1));
                    my *= float(oh) / float(std::max(wh, 1));
                    const Rect m = MenuRect(ow, oh);
                    overlay.mouse_event(event, (mx - m.x) / m.scale, (my - m.y) / m.scale);
                }
                break;
            default:
                break;
            }
        }
        if (!running)
            break; // recovery can leave no renderer; do not tick or submit another frame

        const uint64_t now =
            live_check ? live_origin + uint64_t(frame_count) * 1'000'000'000 / unsigned(args.shot_display_hz)
                       : SDL_GetTicksNS();
        if (live_check) {
            simulated_present_time = now;
            shot_fidelity_time = now;
        }
        if (touch.take_options())
            input.press_options();
        sawgod::ui::MenuActions actions = input.take_menu_actions(double(now) / 1e9);
        for (auto field :
             {&sawgod::ui::MenuActions::up, &sawgod::ui::MenuActions::down, &sawgod::ui::MenuActions::left,
              &sawgod::ui::MenuActions::right, &sawgod::ui::MenuActions::accept,
              &sawgod::ui::MenuActions::back, &sawgod::ui::MenuActions::prev_page,
              &sawgod::ui::MenuActions::next_page, &sawgod::ui::MenuActions::clear})
            actions.*field = actions.*field || menu_carry.*field;
        menu_carry = {};
        // Host actions act on a press (trigger bindings use edges of held state).
        const bool encounters_held =
            !overlay.is_open() && input.button_held(input_map, sawgod::ui::WscButton::Encounters);
        if (encounters_held && !encounters_was_held && wsc) { // Off <-> the Modifiers setting it had
            if (settings.encounters->get() == kEncountersOff) {
                config.set(*settings.encounters, encounters_back);
            } else {
                encounters_back = settings.encounters->get();
                config.set(*settings.encounters, kEncountersOff);
            }
            encounters_badge_until = SDL_GetTicksNS() + kBadgeNs;
        }
        encounters_was_held = encounters_held;
        // The Minimap button is read only where the minimap can show (no feedback but the map itself).
        const bool minimap_held =
            !overlay.is_open() && input.button_held(input_map, sawgod::ui::WscButton::Minimap);
        if (minimap_held && !minimap_was_held && wsc && sawgod::app::minimap_context(minimap_scene())) {
            bool on = settings.minimap->on(), large = settings.minimap_size->get() == 1;
            sawgod::app::minimap_cycle(on, large);
            config.set(*settings.minimap, on ? 1 : 0);
            config.set(*settings.minimap_size, large ? 1 : 0);
        }
        minimap_was_held = minimap_held;
        fidelity.settings(settings.gfx(), settings.music(), now);
        if (live_check)
            for (const auto &[view, direction] : args.shot_steps)
                if (view == uint64_t(frame_count))
                    fidelity.step(direction, now);
        if (!overlay.is_open() && touch.take_mode()) {
            fidelity.cycle(now);
            motion_history.reset();
        }
        title_screen = title_logo_overlay.ready();
        const bool down = input.button_held(input_map, sawgod::ui::WscButton::FidelityDown);
        const bool up = input.button_held(input_map, sawgod::ui::WscButton::FidelityUp);
        if (const int direction = fidelity_buttons.press(overlay.is_open(), down, up)) {
            fidelity.step(direction, now);
            motion_history.reset();
        }
        if (wsc && sawgod::game::Session::find(*wsc))
            autosave.set(sawgod::game::Session::of(*wsc), !movie.replaying());
        if (wsc && sawgod::game::Session::find(*wsc)) // (a replayed movie plays the game's own Config)
            game_config.set(sawgod::game::Session::of(*wsc), !movie.replaying());
        if (wsc && sawgod::game::Session::find(*wsc))
            cheats.set(sawgod::game::Session::of(*wsc), sawgod::app::cheat_flags(settings));
        // Keep both live pictures available throughout any fade, including a locked WSC setting.
        if (wsc && gfx_swap.has_data())
            gfx_swap.set_enabled(*wsc, true);
        if (actions.toggle) {
            touch.show(false);
            motion_history.reset();
            if (!wsc || !overlay.ready())
                running = false; // no menu without a ROM: Esc quits, as before
            else if (overlay.is_open())
                close_menu();
            else {
                overlay.set_open(true);
                host_options_open = true;
                make_backdrop();
            }
        }

        int ran = 0;
        const sawgod::Pacing pacing = settings.pacing_mode();
        const bool lospec = pacing == sawgod::Pacing::LoSpec;
        if (!lospec)
            lospec_cadence.reset();
        sawgod::app::set_enhanced_music_voices(lospec ? 16 : 32); // from the next song's synth
        if (wsc) {
            // Both speed controls run extra game frames. Battle Speed clocks sound once per scheduled
            // frame; held Fast-Forward keeps its existing audio grains (app/ff_audio.h).
            const sawgod::app::FrameSpeed frame_speed = sawgod::app::frame_speed(
                settings, overlay.is_open(),
                !overlay.is_open() && input.button_held(input_map, sawgod::ui::WscButton::FastForward),
                battle_span.active(*wsc)); // (a wipe-out mid-battle ends it: app/host_settings.h)
            const int speed = frame_speed.speed;
            // Enhanced's Smooth pacing draws every refresh: schedule its game frames and its in-between
            // pictures on a steady refresh clock rather than on when the last present returned
            // (app/present_clock.h). Exact and GB/WSC keep the wall clock.
            const bool smooth = pacing == sawgod::Pacing::Smooth &&
                                fidelity.graphics() == sawgod::FidelityMode::Enhanced && speed == 1;
            uint64_t refresh_ns = 0;
            if (smooth && !live_check) {
                int vsync = 0;
                SDL_GetRenderVSync(renderer, &vsync);
                const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
                if (vsync && mode && mode->refresh_rate > 1)
                    refresh_ns = uint64_t(1e9 / double(mode->refresh_rate));
            } else
                present_clock.reset();
            const uint64_t clock_now = refresh_ns ? present_clock.step(now, refresh_ns) : now;
            // (compare, don't subtract: when ahead of schedule clock_now - next_frame would wrap around)
            if (clock_now > next_frame + frame_ns * 8) {
                clock_reset = true;
                motion_history.reset();
                present_clock.reset();
                next_frame = clock_now;
            } // fell far behind: don't try to catch up
            fast_forwarding = frame_speed.fast_forwarding;
            // Observe the button edge even if this loop waits for a native tick before presenting.
            ff_envelope.update(settings.ff_vfx->on(), fast_forwarding, now);
            const uint64_t tick_start = SDL_GetTicksNS();
            log_speed = speed;
            audio_speed = frame_speed.audio_speed();
            while (next_frame <= clock_now && ran < 4) {
                // (a fresh clock: the earlier ticks of this pass count; vsync jitter stays under two ticks)
                const bool late = !live_check && SDL_GetTicksNS() > next_frame + 2 * frame_ns;
                for (int k = 0; k < frame_speed.steps(late); ++k)
                    tick(!live_check && frame_speed.audio_tick(k, late));
                next_frame += frame_ns;
                ++ran;
            }
            t_tick = SDL_GetTicksNS() - tick_start;
            if (!live_check && !lospec && ran == 0 &&
                !(pacing == sawgod::Pacing::Smooth &&
                  fidelity.graphics() == sawgod::FidelityMode::Enhanced)) {
                // Nothing due: wait for the next tick rather than present the same picture again, so a
                // VRR or >= 75 Hz display shows every frame as it's made.
                SDL_DelayPrecise(std::min<uint64_t>(next_frame - now, frame_ns));
                if (overlay.is_open()) { // toggle and reset were acted on above
                    menu_carry = actions;
                    menu_carry.toggle = false;
                }
                continue;
            }
            if (lospec) {
                int vsync = 0;
                SDL_GetRenderVSync(renderer, &vsync);
                const auto *mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
                if (!lospec_cadence.due(now, vsync && mode ? mode->refresh_rate : 0)) {
                    if (overlay.is_open()) {
                        menu_carry = actions;
                        menu_carry.toggle = false;
                    }
                    const auto wait_now = SDL_GetTicksNS();
                    const auto native_wait = wait_now < next_frame ? next_frame - wait_now : 0;
                    // Wake for whichever work comes first; do not spin at 1 kHz on a phone.
                    SDL_DelayPrecise(std::min(native_wait, lospec_cadence.remaining(wait_now)));
                    continue;
                }
            }
            const uint64_t upload_start = SDL_GetTicksNS();
            // The place of this refresh between the last two game frames, on the pass's clock (read before
            // its game frames; for Enhanced Smooth the steady refresh clock): a clock read here would run
            // later whenever this pass ran one more game frame, and the picture would move unevenly.
            const double phase =
                double(int64_t(clock_now) + int64_t(frame_ns) - int64_t(next_frame)) / double(frame_ns);
            present_motion(phase, sawgod::interpolated_pacing(pacing) &&
                                      fidelity.graphics() == sawgod::FidelityMode::Enhanced && speed == 1 &&
                                      !clock_reset);
            upload_game();
            t_upload = SDL_GetTicksNS() - upload_start;
        }

        int ow = 1, oh = 1;
        SDL_GetCurrentRenderOutputSize(renderer, &ow, &oh);
        if (overlay.is_open())
            render_menu(actions, float(double(now - last_present) / 1e9), ow, oh);
        submit_options_return();
        const uint64_t draw_start = SDL_GetTicksNS();
        draw_frame(ow, oh, (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0);
        t_draw = SDL_GetTicksNS() - draw_start;
        if (live_check)
            live_pictures.record(renderer, frame_count, movie_frame, enhanced_scene.map,
                                 int(enhanced_scene.kind),
                                 wsc && sawgod::game::presented_battle_entry_wipe(*wsc), retained_picture,
                                 retain_ns, t_draw, t_tick + t_upload + t_draw, retain_cpu_ns,
                                 live_check ? sawgod::app::LivePictureCheck::cpu_time() - work_cpu_start : 0);
        measure_picture(ow, oh);
        if (wsc && sawgod::debug_log::active()) {
            static constexpr const char *kScreens[] = {"field",   "battle",   "item",     "save",
                                                       "service", "dialogue", "creation", "bestiary",
                                                       "party",   "inventory"};
            static constexpr const char *kLevels[] = {"GB", "WSC", "Enhanced"};
            if (fps_presents % 64 == 0) {
                const int shown = host_picture.shown();
                char context[96];
                std::snprintf(context, sizeof context, "map %d, %s, %s%s", int(wsc->ram[0xD0DC]),
                              kLevels[std::clamp(int(fidelity.graphics()), 0, 2)],
                              shown >= 0 && shown < 10 ? kScreens[shown] : "?",
                              overlay.is_open() ? ", Options" : "");
                sawgod::debug_log::set_context(context);
                // Sound dropouts, at most one line per 10 s; the silent startup is not
                // counted.
                const auto audio_stats = audio.stats();
                if (wsc->frame_count < 150)
                    logged_underruns = audio_stats.underruns;
                else if (audio_stats.underruns > logged_underruns && now >= next_underrun_log) {
                    sawgod::debug_log::warnf("audio", "%llu sound dropout(s); buffer now %zu ms",
                                             (unsigned long long)(audio_stats.underruns - logged_underruns),
                                             audio_stats.target * 1000 / sawgod::app::AudioQueue::kRate);
                    logged_underruns = audio_stats.underruns;
                    next_underrun_log = now + 10'000'000'000ull;
                }
            }
            const bool black = std::none_of(cur_rgb.begin(), cur_rgb.end(), [](uint32_t px) {
                return (px >> 16 & 255) >= 16 || (px >> 8 & 255) >= 16 || (px & 255) >= 16;
            });
            const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
            debug_watch.frame({now, wsc->frame_count,
                               !(flags & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_OCCLUDED)),
                               black, host_picture.holding()});
        }
        const auto present_start = SDL_GetTicksNS();
        SDL_RenderPresent(renderer);
        sawgod::startup::finish();
        presentation.persist_startup_cache();
        world_card_overlay.preload();
        t_present = SDL_GetTicksNS() - present_start;
        write_timing(t_tick, t_upload, t_draw, t_present, ran, log_speed, false, clock_reset, ow, oh);
        last_present = now;
        ++fps_presents;
        if (now - fps_start >= 500000000ull) {
            const double secs = double(now - fps_start) / 1e9;
            const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
            char buf[80];
            std::snprintf(buf, sizeof buf, "GAME %.1f FPS  SHOWN %.1f/S  DISPLAY %.0f HZ", game_frames / secs,
                          fps_presents / secs, mode ? double(mode->refresh_rate) : 0.0);
            fps_text = buf;
            fps_start = now;
            game_frames = 0;
            fps_presents = 0;
        }

        ++frame_count;
        if (args.frames && frame_count >= *args.frames) {
            running = false;
        }
    }

    if (live_check)
        std::fprintf(stderr, "live snapshot bulk retains: %llu\n", (unsigned long long)bulk_retains);
    save_config();
    saves.finish(wsc.get());

    movie.finish();

    // Join the producer before its output adapter or SDL device is destroyed.
    enhanced_audio.finish();
    audio.close();
    destroy_textures();
    hd_menus.shutdown();
    presentation.shutdown();
    SDL_DestroyWindow(window);
    touch.shutdown();
    SDL_Quit();
    return 0;
}
