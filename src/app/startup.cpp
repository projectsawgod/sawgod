#include "app/startup.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string_view>

#include "app/first_run.h"
#include "engine/data_dir.h"
#include "engine/debug_log.h"
#include "engine/version.h"
#include "game/text.h"

namespace sawgod::app {

std::vector<uint8_t> read_file(const std::string &path) {
    std::vector<uint8_t> data;
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
        return data;
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
        data.insert(data.end(), buf, buf + n);
    std::fclose(f);
    return data;
}

int load_rom(const LaunchArgs &args, [[maybe_unused]] const std::string &exe_dir,
             const std::string &gb_maps_dir, LoadedRom &loaded) {
    std::vector<uint8_t> &rom = loaded.rom;
    std::string &english_text = loaded.english_text;
    std::optional<AssetArchive> &english_graphics = loaded.english_graphics;
    rom = read_file(args.rom);
    if (rom.size() < 0x10000 || (rom.size() & (rom.size() - 1))) {
        sawgod::debug_log::errorf("rom", "cannot use ROM %s", args.rom.c_str());
        return 1;
    }
    const sawgod::Sha1 digest = sawgod::sha1(rom.data(), rom.size());
    if (sawgod::to_hex(digest) != sawgod::kBaselineSha1 && !args.any_rom) {
        std::fprintf(stderr,
                     "sawgod: %s is not the supported ROM (SHA-1 %s, want %s); --any-rom to run it anyway\n",
                     args.rom.c_str(), sawgod::to_hex(digest).c_str(), sawgod::kBaselineSha1);
        return 1;
    }
    // --rom follows the same first-run storage contract as a drop/picker. Never store --any-rom input.
    if (sawgod::to_hex(digest) == sawgod::kBaselineSha1 && !sawgod::data_dir().empty() &&
        sawgod::app::stored_rom(sawgod::data_dir()).empty()) {
        std::string error;
        if (sawgod::app::store_rom(sawgod::data_dir(), args.rom, rom, &error).empty()) {
            sawgod::debug_log::errorf("rom", "%s", error.c_str());
            return 1;
        }
    }
    // The resource archives for this ROM (first run, a format change, or a damaged archive): built from
    // the ROM as dumped, before any byte pack below.
    if (const std::string sha1 = sawgod::to_hex(digest);
        !sawgod::data_dir().empty() && !sawgod::app::archives_current(sawgod::data_dir(), sha1)) {
        std::fprintf(stderr, "sawgod: building the resource archives in %s\n", sawgod::data_dir().c_str());
        std::string error;
        if (!sawgod::app::ensure_archives(sawgod::data_dir(), rom, sha1, gb_maps_dir, nullptr, &error))
            sawgod::debug_log::warnf("resources", "%s (the ROM's own data is used)", error.c_str());
    }
    // .gsa byte packs: an import path only (third-party ROM patches); mods are structured zips (below)
    std::vector<std::string> archives;
    for (const std::string &m : args.mods)
        if (std::filesystem::path(m).extension() == ".gsa")
            archives.push_back(m);
    if (!args.assets.empty())
        archives.insert(archives.begin(), args.assets);
    for (const std::string &path : archives) {
        std::string error;
        auto archive = sawgod::AssetArchive::load(path, &error);
        if (!archive || !sawgod::apply_archive(*archive, rom, digest, &error)) {
            sawgod::debug_log::errorf("resources", "%s", error.c_str());
            return 1;
        }
    }
    // English: --english FILE, else english.csv in the per-user directory, else the build's own
    // (text/en.csv next to the app) when it has any strings -- laid over text/en_fill.csv when a
    // development build made one (never packaged).
    std::string english_fill;
    std::string english = !args.english.empty()        ? args.english
                          : sawgod::data_dir().empty() ? std::string()
                                                       : sawgod::data_dir() + "english.csv";
    if (args.english.empty() && (english.empty() || !std::filesystem::exists(english))) {
#ifdef __ANDROID__
        const char *base = exe_dir.c_str();
#else
        const char *base = SDL_GetBasePath();
#endif
        const std::string bundled = base ? std::string(base) + "text/en.csv" : std::string();
        const auto has_rows = [](const std::string &p) {
            return !p.empty() && std::filesystem::exists(p) &&
                   std::filesystem::file_size(p) > std::string_view("key,text\n").size();
        };
        const std::string fill = base ? std::string(base) + "text/en_fill.csv" : std::string();
        if (has_rows(fill)) {
            const std::vector<uint8_t> data = read_file(fill);
            english_fill.assign(data.begin(), data.end());
        }
        english = has_rows(bundled) || (!english_fill.empty() && std::filesystem::exists(bundled))
                      ? bundled
                      : std::string();
    }
    if (!english.empty() && (!args.english.empty() || std::filesystem::exists(english))) {
        const std::vector<uint8_t> data = read_file(english);
        english_text.assign(data.begin(), data.end());
        if (!english_fill.empty())
            english_text = sawgod::game::merge_text(english_fill, english_text);
        if (english_text.empty())
            sawgod::debug_log::errorf("text", "cannot read %s (the game runs in Japanese)", english.c_str());
        const std::filesystem::path pack = std::filesystem::path(english).parent_path().parent_path();
        if (std::filesystem::exists(pack / "graphics.gsa")) {
            std::string error;
            english_graphics = sawgod::AssetArchive::load((pack / "graphics.gsa").string(), &error);
            if (english_graphics && english_graphics->rom_sha1 != digest)
                english_graphics.reset();
            if (!english_graphics)
                sawgod::debug_log::errorf("text", "English graphics: %s", error.c_str());
        }
    }
    return 0;
}

void start_debug_log(const LaunchArgs &args) {
    const char *debug_log_env = SDL_getenv("SAWGOD_DEBUG_LOG");
    const char *debug_log_dir = SDL_getenv("SAWGOD_DEBUG_LOG_DIR");
    const bool scripted =
        !args.shot.empty() || !args.audio_review.empty() || args.frames || !args.inputs.empty();
    if (debug_log_env ? std::strcmp(debug_log_env, "0") != 0 : !scripted) {
        sawgod::debug_log::Limits limits;
        if (debug_log_dir && *debug_log_dir) {
            limits.files_kept = 0;
            sawgod::debug_log::start(debug_log_dir, sawgod::kVersionString, limits);
        } else if (!sawgod::data_dir().empty()) {
            sawgod::debug_log::start(std::filesystem::path(sawgod::data_dir()) / "logs",
                                     sawgod::kVersionString, limits);
        }
    }
}

std::string config_file(const LaunchArgs &args) {
    return !args.config.empty()         ? args.config
           : sawgod::data_dir().empty() ? std::string()
                                        : sawgod::data_dir() + "sawgod.toml";
}

void load_settings(const LaunchArgs &args, const std::string &config_path, Config &config,
                   Settings &settings) {
    if (!config_path.empty()) {
        std::vector<std::string> warnings;
        config.load(config_path, &warnings);
        for (const std::string &w : warnings)
            sawgod::debug_log::warnf("config", "%s: %s", config_path.c_str(), w.c_str());
    }
    if (args.scale) { // initial window size for this run
        config.override_session(*settings.window_width, sawgod::kNativeWidth * std::max(2, *args.scale));
        config.override_session(*settings.window_height, sawgod::kNativeHeight * std::max(2, *args.scale));
    }
    if (!args.renderer.empty())
        config.override_session(*settings.renderer, args.renderer == "legacy" ? 1 : 0);
    if (args.enhanced && settings.gfx_mode)
        config.override_session(*settings.gfx_mode, int(sawgod::FidelityMode::Enhanced));
    if ((args.shot_fidelity_steps || args.shot_mode7_steps) && settings.gfx_mode) {
        config.override_session(*settings.gfx_mode, int(sawgod::FidelityMode::Swap));
        config.override_session(*settings.music_mode, int(sawgod::FidelityMode::Swap));
    }
    if (args.unsnapped)
        config.override_session(*settings.enhanced_snap, 0);
    if (args.identity_shader)
        config.override_session(*settings.identity_shader, 1);
}

} // namespace sawgod::app
