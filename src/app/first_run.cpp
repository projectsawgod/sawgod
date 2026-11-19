#include "app/first_run.h"

#include <SDL3/SDL.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>

#include "emu/wsc.h"
#include "engine/assets.h"
#include "engine/persist.h"
#include "engine/rom_check.h"
#include "engine/version.h"
#include "game/gb_extract.h"
#include "game/landmark_tags.h"
#include "game/light_tags.h"
#include "game/machine.h"
#include "game/maps.h"
#include "game/resources.h"
#include "game/session.h"
#include "game/tables.h"
#include "game/text.h"

namespace sawgod::app {

namespace fs = std::filesystem;

namespace {

std::vector<uint8_t> bytes_of(const std::string &s) { return std::vector<uint8_t>(s.begin(), s.end()); }

std::vector<uint8_t> read_file(const fs::path &p) {
    std::ifstream in(p, std::ios::binary);
    if (!in)
        return {};
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

std::unique_ptr<emu::Wsc> machine(const std::vector<uint8_t> &rom) {
    static const size_t kSram[] = {0, 0x2000, 0x8000, 0x20000, 0x40000, 0x80000};
    const uint8_t save_type = rom[rom.size() - 16 + 0x0B];
    return std::make_unique<emu::Wsc>(rom, save_type <= 5 ? kSram[save_type] : 0);
}

} // namespace

std::string wsc_archive_path(const std::string &data_dir) { return data_dir + "wsc.sgz"; }
std::string gb_archive_path(const std::string &data_dir) { return data_dir + "gb.sgz"; }

RomCheck check_rom_selection(const std::string &path) {
    const RomCheck unreadable{RomVerdict::Unreadable, {}, "rom.wsc", {}};
    SDL_IOStream *io = SDL_IOFromFile(path.c_str(), "rb");
    if (!io)
        return unreadable;
    constexpr size_t limit = 64u << 20;
    std::vector<uint8_t> data;
    std::array<uint8_t, 65536> chunk;
    bool ok = true;
    // SAF providers may report no length; read to EOF, enforcing the bound while streaming.
    for (;;) {
        const size_t count = SDL_ReadIO(io, chunk.data(), chunk.size());
        if (count > limit - data.size()) {
            ok = false;
            break;
        }
        data.insert(data.end(), chunk.begin(), chunk.begin() + count);
        if (count == 0) {
            ok = SDL_GetIOStatus(io) == SDL_IO_STATUS_EOF;
            break;
        }
    }
    SDL_CloseIO(io);
    if (!ok)
        return unreadable;
    // Never use a document URI as a stored filename. Archive members retain their own safe basenames.
    return check_rom_bytes(path.starts_with("content://") ? "rom.wsc" : path, data);
}

std::string stored_rom(const std::string &data_dir) {
    std::error_code ec;
    const fs::path dir = fs::path(data_dir) / "rom";
    if (data_dir.empty() || !fs::is_directory(dir, ec))
        return {};
    for (const auto &e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file() || e.path().extension() != ".wsc")
            continue;
        if (check_rom_file(e.path().string()).verdict == RomVerdict::Ok)
            return e.path().string();
    }
    return {};
}

std::string store_rom(const std::string &data_dir, const std::string &name, const std::vector<uint8_t> &rom,
                      std::string *error) {
    const fs::path dir = fs::path(data_dir) / "rom";
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::string stem = fs::path(name).stem().string();
    if (stem.empty())
        stem = "rom";
    const fs::path path = dir / (stem + ".wsc");
    if (!WriteFileAtomic(path.string(), rom, error))
        return {};
    // one copy only: drop older ones under other names
    for (const auto &e : fs::directory_iterator(dir, ec))
        if (e.path() != path && e.path().extension() == ".wsc")
            fs::remove(e.path(), ec);
    return path.string();
}

std::string detect_rom_beside(const std::string &exe_dir, const std::string &data_dir) {
    for (const std::string &d : {exe_dir, data_dir}) {
        std::error_code ec;
        if (d.empty() || !fs::is_directory(d, ec))
            continue;
        std::vector<fs::path> found;
        for (const auto &e : fs::directory_iterator(d, ec)) {
            std::string ext = e.path().extension().string();
            for (char &c : ext)
                c = char(std::tolower(static_cast<unsigned char>(c)));
            if (e.is_regular_file() && (ext == ".wsc" || ext == ".zip" || ext == ".7z") &&
                e.file_size(ec) < (64u << 20))
                found.push_back(e.path());
        }
        std::sort(found.begin(), found.end());
        for (const fs::path &p : found)
            if (check_rom_file(p.string()).verdict == RomVerdict::Ok)
                return p.string();
    }
    return {};
}

SgzFiles build_wsc_resources(const std::vector<uint8_t> &rom) {
    std::unique_ptr<emu::Wsc> wsc = machine(rom);
    game::install(*wsc);
    game::DataTables &tables = game::Session::of(*wsc).tables();
    SgzFiles out;
    for (const std::string &name : game::table_names())
        out["tables/" + name + ".csv"] = bytes_of(game::export_csv(name, *wsc));
    out["text/ja.csv"] = bytes_of(game::export_text(*wsc));
    for (const game::ResourceRegion &r : game::resource_regions())
        out[r.file] = game::extract_region(r, *wsc);
    for (int id = 0; id < game::kMapCount; ++id) {
        if (!game::map_has_data(id, *wsc, nullptr))
            continue;
        const game::MapData original = game::extract_map(id, *wsc);
        const auto names = game::map_file_names(id);
        out[names[0]] = bytes_of(game::map_cells_csv(original));
        out[names[1]] = bytes_of(game::map_attributes_csv(original));
        out[names[2]] = bytes_of(game::map_metatiles_csv(original));
        out[names[3]] = bytes_of(game::map_types_csv(original));
    }
    game::LightTags light_tags;
    light_tags.seed(*wsc, &tables);
    for (int set : light_tags.tilesets())
        out[light_tags.file(set)] = bytes_of(light_tags.json(set));
    game::LandmarkTags landmarks;
    out[game::LandmarkTags::file] = bytes_of(landmarks.csv());
    return out;
}

SgzFiles build_gb_resources(const std::vector<uint8_t> &rom, const std::string &gb_maps_dir,
                            const std::function<void(float)> &progress, std::string *error) {
    SgzFiles out = game::extract_gb_shared(rom, progress);
    std::map<std::string, std::string> assets;
    std::error_code ec;
    if (!gb_maps_dir.empty() && fs::is_directory(gb_maps_dir, ec))
        for (const auto &e : fs::directory_iterator(gb_maps_dir, ec))
            if (e.is_regular_file() && e.path().extension() == ".csv") {
                const auto d = read_file(e.path());
                assets[e.path().filename().string()] = std::string(d.begin(), d.end());
            }
    if (!game::build_gb_field_maps(rom, assets, &out, error))
        return {};
    return out;
}

bool archives_current(const std::string &data_dir, const std::string &rom_sha1) {
    return sgz_current(wsc_archive_path(data_dir), {"wsc", kBuildId, rom_sha1}) &&
           sgz_current(gb_archive_path(data_dir), {"gb", kBuildId, rom_sha1});
}

bool ensure_archives(const std::string &data_dir, const std::vector<uint8_t> &rom,
                     const std::string &rom_sha1, const std::string &gb_maps_dir,
                     const std::function<void(float)> &progress, std::string *error) {
    const SgzStamp wsc_stamp{"wsc", kBuildId, rom_sha1}, gb_stamp{"gb", kBuildId, rom_sha1};
    const bool need_wsc = !sgz_current(wsc_archive_path(data_dir), wsc_stamp);
    const bool need_gb = !sgz_current(gb_archive_path(data_dir), gb_stamp);
    // The bar follows the time each part takes (measured, about 10 s in all): the WSC archive
    // about 6%, the GB layer's title and battle-effect replays about 84% (reported as they go), its maps the
    // rest.
    constexpr float kWscEnd = 0.06f, kGbSharedEnd = 0.9f;
    if (progress)
        progress(0.01f);
    if (need_wsc) {
        const SgzFiles files = build_wsc_resources(rom);
        if (!write_sgz(wsc_archive_path(data_dir), wsc_stamp, files, error))
            return false;
    }
    if (progress)
        progress(kWscEnd);
    if (need_gb) {
        const SgzFiles files = build_gb_resources(
            rom, gb_maps_dir,
            [&](float f) {
                if (progress)
                    progress(kWscEnd + (kGbSharedEnd - kWscEnd) * f);
            },
            error);
        if (files.empty())
            return false;
        if (progress)
            progress(0.98f);
        if (!write_sgz(gb_archive_path(data_dir), gb_stamp, files, error))
            return false;
    }
    if (progress)
        progress(1.0f);
    return true;
}

} // namespace sawgod::app
