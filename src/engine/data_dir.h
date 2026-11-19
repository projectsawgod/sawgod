// The game's data folder (first-run ROM extraction): config, saves, the
// ROM copy, the .sgz archives and mods/ all live in one place.
//
//   - $SAWGOD_HOME, when set (tests, development);
//   - else the executable's own folder when the game may write there ("portable": a Windows zip, a Linux
//     tarball) -- but not in a CMake build tree (CMakeCache.txt beside it), so development builds share one
//     data folder that survives deleting the build;
//   - else the OS per-user data folder (SDL_GetPrefPath: AppData, ~/.local/share, Application Support), for
//     installs whose folder is read-only or replaced on update (a macOS .app, Flatpak, AppImage, Android).
//
// tools/paths.py mirrors this choice.
#pragma once

#include <string>

namespace sawgod {

struct DataDirInputs {
    std::string home;        // $SAWGOD_HOME ("" when unset)
    std::string exe_dir;     // SDL_GetBasePath()
    std::string pref_dir;    // SDL_GetPrefPath("sawgod", "sawgod")
    bool packaged = false;   // inside a .app bundle, APK, Flatpak or AppImage: never portable
    bool build_tree = false; // CMakeCache.txt beside the executable: a development build, never portable
    bool exe_dir_writable = false;
};

// The folder, with a trailing separator; "" when there is none.
std::string choose_data_dir(const DataDirInputs &in);

// The live inputs: environment, SDL paths, and a write probe of the executable's folder.
DataDirInputs data_dir_inputs();

// choose_data_dir(data_dir_inputs()), computed once and cached.
const std::string &data_dir();

} // namespace sawgod
