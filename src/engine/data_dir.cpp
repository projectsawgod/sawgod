#include "engine/data_dir.h"

#include <filesystem>
#include <fstream>

#include <SDL3/SDL.h>

namespace sawgod {

namespace fs = std::filesystem;

namespace {

std::string with_slash(std::string p) {
    if (!p.empty() && p.back() != '/' && p.back() != '\\')
        p += '/';
    return p;
}

// Whether a file can be created (and removed) in `dir`.
bool writable(const std::string &dir) {
    if (dir.empty())
        return false;
    const fs::path probe = fs::path(dir) / ".sawgod-write-probe";
    {
        std::ofstream out(probe, std::ios::binary);
        if (!out)
            return false;
        out << "probe";
        if (!out)
            return false;
    }
    std::error_code ec;
    fs::remove(probe, ec);
    return true;
}

bool packaged(const std::string &exe_dir) {
#ifdef __ANDROID__
    (void)exe_dir;
    return true; // APK assets are read-only; SDL_GetPrefPath is the app's internal files directory.
#else
    if (SDL_getenv("APPIMAGE") || SDL_getenv("FLATPAK_ID"))
        return true;
    // a macOS bundle: <...>.app/Contents/Resources/ (SDL_GetBasePath) or .../MacOS/
    return exe_dir.find(".app/Contents/") != std::string::npos;
#endif
}

} // namespace

std::string choose_data_dir(const DataDirInputs &in) {
    if (!in.home.empty())
        return with_slash(in.home);
    if (!in.packaged && !in.build_tree && in.exe_dir_writable && !in.exe_dir.empty())
        return with_slash(in.exe_dir);
    return with_slash(in.pref_dir);
}

DataDirInputs data_dir_inputs() {
    DataDirInputs in;
    if (const char *home = SDL_getenv("SAWGOD_HOME"))
        in.home = home;
    if (const char *base = SDL_GetBasePath())
        in.exe_dir = base;
    in.packaged = packaged(in.exe_dir);
    std::error_code ec;
    in.build_tree = !in.exe_dir.empty() && fs::exists(fs::path(in.exe_dir) / "CMakeCache.txt", ec);
    // the probe only matters when it decides (no $SAWGOD_HOME, not packaged)
    in.exe_dir_writable = in.home.empty() && !in.packaged && !in.build_tree && writable(in.exe_dir);
    if (in.home.empty() && (in.packaged || in.build_tree || !in.exe_dir_writable)) {
        if (char *pref = SDL_GetPrefPath("sawgod", "sawgod")) {
            in.pref_dir = pref;
            SDL_free(pref);
        }
    }
    return in;
}

const std::string &data_dir() {
    static const std::string dir = [] {
        std::string d = choose_data_dir(data_dir_inputs());
        if (!d.empty()) {
            std::error_code ec;
            fs::create_directories(d, ec);
        }
        return d;
    }();
    return dir;
}

} // namespace sawgod
