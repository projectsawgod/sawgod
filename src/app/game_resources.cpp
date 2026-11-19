#include "app/game_resources.h"

#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <utility>

#include "app/first_run.h"
#include "engine/data_dir.h"
#include "engine/debug_log.h"
#include "engine/input.h"
#include "engine/mod_pack.h"
#include "engine/sgz.h"
#include "game/landmark_tags.h"
#include "game/light_tags.h"
#include "game/maps.h"
#include "game/resources.h"
#include "game/session.h"
#include "game/tables.h"
#include "game/text.h"
#include "game/text_render.h"

namespace sawgod::app {

void load_game_resources(emu::Wsc &wsc, const LaunchArgs &args, game::SceneObserver &scene_observer,
                         game::GfxSwap &gfx_swap, std::string &base_text, std::string &english_text,
                         std::vector<uint8_t> &gb_hand_png) {
    sawgod::game::DataTables &tables = sawgod::game::Session::of(wsc).tables();
    // The resource archives (app/first_run.h; built above): the game's data comes from wsc.sgz, the GB
    // layer from gb.sgz; mods (below) lay their files over both, file by file.
    const std::string res =
        sawgod::data_dir().empty() ? std::string() : sawgod::app::wsc_archive_path(sawgod::data_dir());
    sawgod::SgzFiles wsc_files, gb_files;
    if (!res.empty()) {
        std::string error;
        if (auto files = sawgod::read_sgz(res, &error))
            wsc_files = std::move(*files);
        else
            sawgod::debug_log::warnf("resources", "%s (the ROM's own data is used)", error.c_str());
        if (auto files = sawgod::read_sgz(sawgod::app::gb_archive_path(sawgod::data_dir()), &error))
            gb_files = std::move(*files);
        else if (sawgod::ui::kGfxSwapBuild)
            sawgod::debug_log::errorf("gb", "%s (no GB pictures)", error.c_str());
    }
    // A resource file from wsc.sgz; one the archive lacks comes from the ROM (the same bytes).
    auto resource = [&](const std::string &file, const std::function<std::vector<uint8_t>()> &extract) {
        if (auto it = wsc_files.find(file); it != wsc_files.end())
            return it->second;
        return extract();
    };
    auto bytes_of = [](const std::string &s) { return std::vector<uint8_t>(s.begin(), s.end()); };
    auto load = [&](const std::string &name, const std::string &path, const std::vector<uint8_t> &data) {
        std::string error;
        std::vector<std::string> warnings;
        if (!tables.load_csv(name, std::string(data.begin(), data.end()), wsc, &error, &warnings))
            sawgod::debug_log::warnf("tables", "%s: %s (the ROM's values are used)", path.c_str(),
                                     error.c_str());
        for (const std::string &w : warnings)
            sawgod::debug_log::warnf("tables", "%s: %s", path.c_str(), w.c_str());
    };
    for (const std::string &name : sawgod::game::table_names()) {
        const auto data = resource("tables/" + name + ".csv",
                                   [&] { return bytes_of(sawgod::game::export_csv(name, wsc)); });
        if (!data.empty())
            load(name, res + "/tables/" + name + ".csv", data);
    }
    const auto ja = resource("text/ja.csv", [&] { return bytes_of(sawgod::game::export_text(wsc)); });
    base_text.assign(ja.begin(), ja.end());
    // the resource regions (graphics, palettes, map data...: game/resources.h): every region's bytes are
    // its file's (a mod's file over it, below), decoded into the data the game reads
    auto apply_region = [&](const sawgod::game::ResourceRegion &r, const std::vector<uint8_t> &file,
                            const std::string &from) {
        std::vector<uint8_t> bytes;
        std::string error;
        if (!sawgod::game::build_region(r, file, &bytes, &error)) {
            sawgod::debug_log::warnf("tables", "%s: %s (the ROM's data is used)", from.c_str(),
                                     error.c_str());
            return false;
        }
        for (uint32_t i = 0; i < r.size; ++i)
            wsc.rom[(r.rom + i) & (wsc.rom.size() - 1)] = bytes[i];
        return true;
    };
    for (const sawgod::game::ResourceRegion &r : sawgod::game::resource_regions()) {
        const auto file = resource(r.file, [&] { return sawgod::game::extract_region(r, wsc); });
        if (!file.empty())
            apply_region(r, file, res + "/" + r.file);
    }
    // field maps (game/maps.h): grid files, extracted if missing; mods' maps/NNN*.csv replace them
    // (below)
    std::map<std::string, std::pair<std::vector<uint8_t>, std::string>> map_files; // name -> data, from
    for (int id = 0; id < sawgod::game::kMapCount; ++id) {
        if (!sawgod::game::map_has_data(id, wsc, nullptr))
            continue;
        const sawgod::game::MapData original = sawgod::game::extract_map(id, wsc);
        const auto names = sawgod::game::map_file_names(id);
        const std::string made[4] = {
            sawgod::game::map_cells_csv(original), sawgod::game::map_attributes_csv(original),
            sawgod::game::map_metatiles_csv(original), sawgod::game::map_types_csv(original)};
        for (int f = 0; f < 4; ++f)
            map_files[names[size_t(f)]] = {resource(names[size_t(f)], [&] { return bytes_of(made[f]); }),
                                           res + "/" + names[size_t(f)]};
    }
    // Scene materials are seeded from the loaded graphics and shared by tileset.
    auto &light_tags = scene_observer.light_tags();
    light_tags.seed(wsc, &tables);
    for (int set : light_tags.tilesets()) {
        const auto name = light_tags.file(set);
        const auto data = resource(name, [&] { return bytes_of(light_tags.json(set)); });
        std::string error;
        if (!light_tags.load(set, std::string(data.begin(), data.end()), &error))
            sawgod::debug_log::warnf("enhanced", "%s: %s (seeded materials used)", name.c_str(),
                                     error.c_str());
    }
    auto &landmark_tags = scene_observer.landmark_tags();
    const auto landmark_data =
        resource(sawgod::game::LandmarkTags::file, [&] { return bytes_of(landmark_tags.csv()); });
    const std::string landmark_text(landmark_data.begin(), landmark_data.end());
    bool landmark_edited = false;
    const auto refreshed = landmark_tags.refresh(landmark_text, &landmark_edited);
    std::string landmark_error;
    if (!landmark_tags.load(refreshed, &landmark_error))
        sawgod::debug_log::warnf("enhanced", "landmarks: %s (defaults used)", landmark_error.c_str());
    std::vector<std::string> mod_paths, errors;
    for (const std::string &m : args.mods)
        if (std::filesystem::path(m).extension() != ".gsa")
            mod_paths.push_back(m);
    const std::string mods_dir = sawgod::data_dir().empty() ? std::string() : sawgod::data_dir() + "mods";
    for (const auto &mod : sawgod::open_mods(mod_paths, mods_dir, &errors)) {
        int files = 0;
        for (const std::string &name : sawgod::game::table_names())
            if (const auto data = mod->read("tables/" + name + ".csv")) {
                load(name, mod->path() + ": tables/" + name + ".csv", *data);
                ++files;
            }
        for (const sawgod::game::ResourceRegion &r : sawgod::game::resource_regions())
            if (const auto data = mod->read(r.file))
                files += apply_region(r, *data, mod->path() + ": " + r.file) ? 1 : 0;
        for (auto &[name, file] : map_files)
            if (const auto data = mod->read(name)) {
                file = {*data, mod->path() + ": " + name};
                ++files;
            }
        for (int set : light_tags.tilesets())
            if (const auto data = mod->read(light_tags.file(set))) {
                std::string error;
                if (light_tags.load(set, std::string(data->begin(), data->end()), &error))
                    ++files;
                else
                    sawgod::debug_log::warnf("enhanced", "%s: %s", light_tags.file(set).c_str(),
                                             error.c_str());
            }
        if (const auto data = mod->read(sawgod::game::LandmarkTags::file)) {
            if (landmark_tags.load(std::string(data->begin(), data->end()), &landmark_error))
                ++files;
            else
                sawgod::debug_log::warnf("enhanced", "landmarks: %s", landmark_error.c_str());
        }
        for (const std::string &name : mod->files()) // the GB layer: a mod's gb/ files replace gb.sgz's
            if (name.rfind("gb/", 0) == 0)
                if (const auto data = mod->read(name)) {
                    gb_files[name] = *data;
                    ++files;
                }
        if (const auto data = mod->read("text/ja.csv")) {
            base_text = sawgod::game::merge_text(base_text, std::string(data->begin(), data->end()));
            ++files;
        }
        if (const auto data = mod->read("text/en.csv")) {
            english_text =
                english_text.empty()
                    ? std::string(data->begin(), data->end())
                    : sawgod::game::merge_text(english_text, std::string(data->begin(), data->end()));
            ++files;
        }
        std::fprintf(stderr, "sawgod: mod \"%s\" (%s): %d files\n", mod->name().c_str(), mod->path().c_str(),
                     files);
    }
    for (const std::string &e : errors)
        sawgod::debug_log::warnf("mod", "mod %s", e.c_str());
    // The GB layer (gb.sgz): field maps, sprites, font, monsters, title.
    const auto gb_read = [&](const std::string &file) {
        const auto it = gb_files.find(file);
        return it == gb_files.end() ? std::vector<uint8_t>() : it->second;
    };
    const std::string gb_res = sawgod::app::gb_archive_path(sawgod::data_dir());
    for (int id = 0; id < sawgod::game::kMapCount && !gb_files.empty() && sawgod::ui::kGfxSwapBuild; ++id) {
        std::string error;
        gfx_swap.load_field_map(id, gb_read, &error);
        if (!error.empty())
            sawgod::debug_log::errorf("gb", "GB field map %03d: %s", id, error.c_str());
    }
    if (!gb_files.empty() && sawgod::ui::kGfxSwapBuild) {
        const auto png = gb_read("gb/sprites.png");
        const auto csv = gb_read("gb/sprite_types.csv");
        std::string error;
        if (!png.empty() && !csv.empty() &&
            !gfx_swap.add_sprites(png, std::string(csv.begin(), csv.end()), &error))
            sawgod::debug_log::errorf("gb", "%s: gb/sprites: %s", gb_res.c_str(), error.c_str());
        gfx_swap.import_field_sprites(wsc);
        const auto corpse = gb_read("gb/corpse.png");
        if (!corpse.empty() && !gfx_swap.add_corpse(corpse, &error))
            sawgod::debug_log::errorf("gb", "GB corpse: %s", error.c_str());
        const auto hand = gb_hand_png = gb_read("gb/hand.png");
        if (!hand.empty() && !gfx_swap.add_hand(hand, &error))
            sawgod::debug_log::errorf("gb", "GB hand: %s", error.c_str());
        const auto objects = gb_read("gb/objects.png"), sets = gb_read("gb/object_sets.csv");
        if (!objects.empty() && !sets.empty() &&
            !gfx_swap.add_objects(objects, std::string(sets.begin(), sets.end()), &error))
            sawgod::debug_log::errorf("gb", "GB objects: %s", error.c_str());
        const auto font = gb_read("gb/font.png");
        if (!font.empty() && !gfx_swap.add_font(font, &error))
            sawgod::debug_log::errorf("gb", "%s: gb/font.png: %s", gb_res.c_str(), error.c_str());
        const auto monsters = gb_read("gb/monsters.png");
        const auto pics = gb_read("gb/monster_pics.csv"), types = gb_read("gb/monster_types.csv");
        if (!monsters.empty() && !pics.empty() && !types.empty() &&
            !gfx_swap.add_monsters(monsters, std::string(pics.begin(), pics.end()),
                                   std::string(types.begin(), types.end()), &error))
            sawgod::debug_log::errorf("gb", "%s: gb/monsters: %s", gb_res.c_str(), error.c_str());
        if (!gfx_swap.load_battle_effects(gb_read, &error))
            sawgod::debug_log::errorf("gb", "GB battle effects: %s", error.c_str());
        const auto title = gb_read("gb/title.png");
        if (!title.empty() && !gfx_swap.add_title(title, &error))
            sawgod::debug_log::errorf("gb", "%s: gb/title.png: %s", gb_res.c_str(), error.c_str());
    }
    { // build the map banks from the grids (unchanged maps rebuild them byte for byte)
        std::vector<sawgod::game::MapData> maps(sawgod::game::kMapCount);
        std::vector<const sawgod::game::MapData *> use(sawgod::game::kMapCount, nullptr);
        for (int id = 0; id < sawgod::game::kMapCount; ++id) {
            const auto names = sawgod::game::map_file_names(id);
            if (!map_files.count(names[0]))
                continue;
            const auto text = [&](int f) {
                const auto &v = map_files[names[size_t(f)]].first;
                return std::string(v.begin(), v.end());
            };
            std::string error;
            if (!sawgod::game::map_from_files(text(0), text(1), text(2), text(3), &maps[size_t(id)],
                                              &error)) {
                sawgod::debug_log::warnf("maps", "%s: %s (the ROM's map is used)",
                                         map_files[names[0]].second.c_str(), error.c_str());
                continue;
            }
            use[size_t(id)] = &maps[size_t(id)];
        }
        std::vector<std::string> map_errors;
        const int moved = sawgod::game::build_map_banks(wsc, tables, use, &map_errors);
        for (const std::string &e : map_errors)
            sawgod::debug_log::warnf("maps", "%s", e.c_str());
        if (moved)
            std::fprintf(stderr, "sawgod: %d edited maps moved to free space in their banks\n", moved);
    }
    std::vector<std::string> diffs;
    const int rows = tables.compare_rom(wsc, &diffs);
    std::fprintf(stderr, "sawgod: data tables from %s: %d rows, %zu differ from the ROM\n", res.c_str(), rows,
                 diffs.size());
}

void GameLanguage::apply(emu::Wsc &wsc, bool english, std::string &base_text, std::string &english_text,
                         const std::optional<AssetArchive> &english_graphics, const game::GfxSwap &gfx_swap) {
    set_text(wsc, english, base_text, english_text, english_graphics);
    // Numbers in m5x7 with the English text (game/text_render.h: display overrides of the digit glyphs, in
    // the WSC picture and the GB one)
    if (english && !english_text.empty())
        wsc.set_glyph_overrides(
            sawgod::game::m5x7_number_overrides(wsc, [&](int code) { return gfx_swap.gb_glyph(code); }));
    else
        wsc.set_glyph_overrides({});
}

void GameLanguage::set_text(emu::Wsc &wsc, bool english, std::string &base_text, std::string &english_text,
                            const std::optional<AssetArchive> &english_graphics) {
    sawgod::game::TextTable &table = sawgod::game::Session::of(wsc).text();
    table.clear();
    table.set_button_icons(true); // Ａ：けってい as {A}：けってい in Japanese too (game/text_tokens.h)
    for (const auto &[offset, bytes] : graphics_original_) // the ROM's own graphics back
        std::copy(bytes.begin(), bytes.end(), wsc.rom.begin() + offset);
    graphics_original_.clear();
    std::string error;
    std::vector<std::string> warnings;
    if (!base_text.empty() && !table.load(base_text, sawgod::game::Charset::system(), &error, &warnings)) {
        sawgod::debug_log::errorf("text", "text/ja.csv: %s (the ROM's text is used)", error.c_str());
        base_text.clear();
    }
    for (const std::string &w : warnings)
        sawgod::debug_log::warnf("text", "text/ja.csv: %s", w.c_str());
    warnings.clear();
    if (!english || english_text.empty())
        return;
    if (english_graphics)
        for (const auto &e : english_graphics->entries) {
            if (size_t(e.rom_offset) + e.data.size() > wsc.rom.size())
                continue;
            graphics_original_.emplace_back(
                e.rom_offset, std::vector<uint8_t>(wsc.rom.begin() + e.rom_offset,
                                                   wsc.rom.begin() + e.rom_offset + e.data.size()));
            std::copy(e.data.begin(), e.data.end(), wsc.rom.begin() + e.rom_offset);
        }
    if (!table.load(english_text, sawgod::game::Charset::system(), &error, &warnings, nullptr,
                    /*proportional=*/true, /*layer=*/true)) { // m5x7 (game/text_render.h), over the base
        sawgod::debug_log::errorf("text", "English text: %s (the game runs in Japanese)", error.c_str());
        english_text.clear();
    }
    for (const std::string &w : warnings)
        sawgod::debug_log::warnf("text", "English text: %s", w.c_str());
}

} // namespace sawgod::app
