// A mod: a .sgz (a zip) -- or a folder with the same layout -- of
// override files, laid out like the resources the first run extracts (docs/MODS.md):
//
//   mod.toml              optional: name, version, author, description
//   tables/<name>.csv     data-table overrides (only the rows and columns it changes)
//   ...                   more resource kinds as the game reads them from files
//
// The engine only opens mods and hands out their files by path; what a path means is the game's business.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sawgod {

class ModPack {
  public:
    // A .zip file or a folder; nullptr and `error` if it can't be read.
    static std::unique_ptr<ModPack> open(const std::string &path, std::string *error);
    virtual ~ModPack() = default;

    const std::string &path() const { return path_; }
    // mod.toml's name, else the file or folder name.
    const std::string &name() const { return name_; }
    // A file's bytes by its path inside the mod ("tables/items.csv", '/' separators), or nullopt.
    virtual std::optional<std::vector<uint8_t>> read(const std::string &file) const = 0;
    // Every file's path inside the mod, sorted.
    virtual std::vector<std::string> files() const = 0;

  protected:
    std::string path_, name_;
};

// The mods to load, in order: `explicit_paths` (--mod), then every .sgz and folder in `dir` (the user's
// mods folder) sorted by name. Later mods override earlier ones. Unreadable ones are reported and skipped.
std::vector<std::unique_ptr<ModPack>> open_mods(const std::vector<std::string> &explicit_paths,
                                                const std::string &dir, std::vector<std::string> *errors);

} // namespace sawgod
