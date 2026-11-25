// .sgz ("saga zip"): the game's resource archives, built from the player's ROM on
// first run (first-run ROM extraction). A plain zip under our own extension -- any unzip tool opens
// it, and tools/sgz.py unpacks/packs it -- holding the same files the game reads by path
// ("tables/items.csv").
//
//   <data>/wsc.sgz       the WSC game's resources (extracted once; not expected to change)
//   <data>/gb.sgz        the GB layer, from ORIJINARU on the WSC cartridge (art, GB field maps)
//   <data>/mods/*.sgz    mods: the same layout, laid over the base archives file by file (or a folder)
//
// Each base archive carries sgz.json, its stamp: {"kind": "wsc", "build": "...", "rom_sha1": "..."}. A
// missing or damaged archive, or one with different kind, ROM, extraction code or authored inputs,
// is rebuilt from the stored ROM copy. Application revisions alone do not invalidate base archives.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sawgod {

inline constexpr const char *kSgzStampFile = "sgz.json";

struct SgzStamp {
    std::string kind;     // "wsc", "gb", "enhanced"
    std::string build;    // content identity of the extraction code and authored inputs
    std::string rom_sha1; // hex, of the ROM it was built from
    bool operator==(const SgzStamp &) const = default;
};

// Files by path inside the archive ('/' separators).
using SgzFiles = std::map<std::string, std::vector<uint8_t>>;

// Writes `files` plus the stamp as a zip at `path`, atomically (a temporary file, then a rename).
bool write_sgz(const std::string &path, const SgzStamp &stamp, const SgzFiles &files, std::string *error);

// The stamp of the archive at `path`; nullopt when it's missing, unreadable or has no valid stamp.
std::optional<SgzStamp> read_sgz_stamp(const std::string &path);

// Whether the archive at `path` exists, opens, and carries exactly `want`.
inline bool sgz_current(const std::string &path, const SgzStamp &want) {
    const auto stamp = read_sgz_stamp(path);
    return stamp && *stamp == want;
}

// Every file of the archive at `path` (the stamp included); nullopt when it can't be read.
std::optional<SgzFiles> read_sgz(const std::string &path, std::string *error = nullptr);

// The stamp's JSON text, and its parse (nullopt for anything malformed).
std::string sgz_stamp_json(const SgzStamp &stamp);
std::optional<SgzStamp> parse_sgz_stamp(const std::string &json);

} // namespace sawgod
