// First run (first-run ROM extraction, modeled on Ship of Harkinian):
// the ROM screen, the stored ROM copy, and the .sgz resource archives built from it.
//
//   <data>/rom/<name>.wsc   the verified ROM, copied in; re-checked (SHA-1) on every launch
//   <data>/wsc.sgz          the WSC game's resources (tables, text, graphics regions, maps, scene materials)
//   <data>/gb.sgz           the GB layer from ORIJINARU (game/gb_extract.h)
//
// <data> is engine/data_dir.h's folder. A missing, broken or outdated archive (engine/sgz.h stamp) is rebuilt
// from the ROM; the first run builds both behind a progress bar, then goes straight to the title.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "engine/rom_check.h"
#include "engine/sgz.h"

namespace sawgod::app {

// The archives' paths in `data_dir`.
std::string wsc_archive_path(const std::string &data_dir);
std::string gb_archive_path(const std::string &data_dir);

// Read a picker selection through SDL IO (including Android content:// URIs), bounded to 64 MiB.
// Unknown-length provider streams are supported. The returned bytes still require exact ROM validation.
RomCheck check_rom_selection(const std::string &path);

// The stored ROM copy in `data_dir`, if any passes its check (path; "" when none).
std::string stored_rom(const std::string &data_dir);

// Copies a verified ROM into <data>/rom/ (atomically), replacing an older copy. The copy's path, or "" and
// `error`.
std::string store_rom(const std::string &data_dir, const std::string &name, const std::vector<uint8_t> &rom,
                      std::string *error);

// A ROM file beside the executable (or in `data_dir`) that passes the check; "" when none.
std::string detect_rom_beside(const std::string &exe_dir, const std::string &data_dir);

// The archives' contents, built from the ROM. `gb_maps_dir` holds the GB field maps' authored data
// (assets/gb_maps/, beside the app as gb_maps/).
SgzFiles build_wsc_resources(const std::vector<uint8_t> &rom);
SgzFiles build_gb_resources(const std::vector<uint8_t> &rom, const std::string &gb_maps_dir,
                            const std::function<void(float)> &progress, std::string *error);

// Whether either archive needs (re)building for this ROM.
bool archives_current(const std::string &data_dir, const std::string &rom_sha1,
                      const std::string &gb_maps_dir);

// Builds the archives that aren't current. `progress` (0..1) is called as work completes; may be called from
// any thread. False and `error` on failure.
bool ensure_archives(const std::string &data_dir, const std::vector<uint8_t> &rom,
                     const std::string &rom_sha1, const std::string &gb_maps_dir,
                     const std::function<void(float)> &progress, std::string *error);

// On Android the picker URI is read through SDL IO and only the verified bytes are stored; no external
// storage permission or persistent URI access is needed.
// The ROM screen: runs its own window until the player supplies the supported ROM, then builds the archives
// behind a progress bar. Returns the stored ROM's path, or "" when the player closed the window.
std::string run_rom_screen(const std::string &data_dir, const std::string &exe_dir,
                           const std::string &gb_maps_dir);

} // namespace sawgod::app
