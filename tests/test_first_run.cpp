#include "engine/derived_cache.h"
#include "engine/persist.h"
#include "engine/version.h"
#include "game/gb_extract.h"
#include "game/maps.h"
#include "game/tables.h"
#include "test_first_run_support.h"

TEST_CASE("data folder: SAWGOD_HOME, else portable beside the game, else the OS data folder") {
    sawgod::DataDirInputs in;
    in.exe_dir = "/games/sawgod/";
    in.pref_dir = "/home/p/.local/share/sawgod/sawgod/";
    in.exe_dir_writable = true;
    CHECK(sawgod::choose_data_dir(in) == "/games/sawgod/");
    in.home = "/tmp/h";
    CHECK(sawgod::choose_data_dir(in) == "/tmp/h/");
    in.home.clear();
    in.exe_dir_writable = false;
    CHECK(sawgod::choose_data_dir(in) == in.pref_dir);
    in.exe_dir_writable = true;
    in.packaged = true; // .app, Flatpak, AppImage
    CHECK(sawgod::choose_data_dir(in) == in.pref_dir);
    in.packaged = false;
    in.build_tree = true; // a development build keeps one data folder across builds
    CHECK(sawgod::choose_data_dir(in) == in.pref_dir);
}

TEST_CASE("sgz: files and stamp round trip, a different or broken archive isn't current") {
    const fs::path dir = fs::temp_directory_path() / "sawgod_test_sgz";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string path = (dir / "wsc.sgz").string();
    const sawgod::SgzStamp stamp{"wsc", "0.7.3+aaaaaaaaaaaa", std::string(40, 'a')};
    sawgod::SgzFiles files{{"tables/items.csv", {'a', ',', 'b', '\n'}}, {"gfx/x.bin", {1, 2, 3}}};
    std::string error;
    REQUIRE(sawgod::write_sgz(path, stamp, files, &error));
    CHECK(sawgod::sgz_current(path, stamp));
    CHECK_FALSE(sawgod::sgz_current(path, {"wsc", "0.7.3+bbbbbbbbbbbb", stamp.rom_sha1})); // another build
    CHECK_FALSE(sawgod::sgz_current(path, {"gb", stamp.build, stamp.rom_sha1}));
    CHECK_FALSE(sawgod::sgz_current(path, {"wsc", stamp.build, std::string(40, 'b')}));
    const auto back = sawgod::read_sgz(path);
    REQUIRE(back);
    CHECK(back->at("tables/items.csv") == files["tables/items.csv"]);
    CHECK(back->at("gfx/x.bin") == files["gfx/x.bin"]);
    CHECK(back->count(sawgod::kSgzStampFile) == 1);
    // truncated: not current (rebuilt)
    std::vector<uint8_t> bytes = read(path);
    bytes.resize(bytes.size() / 2);
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    CHECK_FALSE(sawgod::sgz_current(path, stamp));
    CHECK_FALSE(sawgod::sgz_current((dir / "missing.sgz").string(), stamp));
    CHECK_FALSE(sawgod::parse_sgz_stamp("{\"kind\": \"wsc\", \"build\": \"\"}"));
    // The earlier hand-numbered stamp names no build: such an archive is rebuilt.
    CHECK_FALSE(sawgod::parse_sgz_stamp("{\"kind\": \"wsc\", \"format\": 4, \"rom_sha1\": \"" +
                                        std::string(40, 'a') + "\"}\n"));
    fs::remove_all(dir);
}

TEST_CASE("rom check: what an offered file most likely is") {
    using V = sawgod::RomVerdict;
    CHECK(sawgod::check_rom_bytes("x.wsc", fake_wsc()).verdict == V::Modified);
    CHECK(sawgod::check_rom_bytes("saga.gb", fake_gb()).verdict == V::GameBoy);
    CHECK(sawgod::check_rom_bytes("junk.bin", std::vector<uint8_t>(0x10000, 7)).verdict == V::Unsupported);
    CHECK(sawgod::check_rom_bytes("empty.wsc", {}).verdict == V::Unreadable);
    // archives: the most telling entry wins; a nested folder name is dropped
    const auto in_zip = sawgod::check_rom_bytes(
        "r.zip",
        zip_of({{"readme.txt", {'h', 'i'}}, {"dump/Makai.wsc", fake_wsc()}, {"saga.gb", fake_gb()}}));
    CHECK(in_zip.verdict == V::Modified);
    CHECK(in_zip.name == "Makai.wsc");
    CHECK(sawgod::check_rom_bytes("r.zip", zip_of({{"readme.txt", {'h', 'i'}}})).verdict ==
          V::NoRomInArchive);
    CHECK(sawgod::check_rom_bytes("r.zip", {'P', 'K', 3, 4, 0, 0}).verdict == V::Unreadable);
    // .7z (fixtures made with 7-Zip: the footer-only stand-in and a readme)
    const fs::path fixtures = SAWGOD_TEST_FIXTURES;
    const auto in_7z = sawgod::check_rom_file((fixtures / "rom_check_modified.7z").string());
    CHECK(in_7z.verdict == V::Modified);
    CHECK(in_7z.name == "fake modified.wsc");
    CHECK(sawgod::check_rom_file((fixtures / "rom_check_readme.7z").string()).verdict == V::NoRomInArchive);
}

TEST_CASE("[ROM] rom check: the supported ROM, bare and zipped") {
    const char *path = rom_path();
    if (!path)
        return;
    const std::vector<uint8_t> rom = read(path);
    const auto bare = sawgod::check_rom_file(path);
    CHECK(bare.verdict == sawgod::RomVerdict::Ok);
    CHECK(bare.sha1 == sawgod::kBaselineSha1);
    CHECK(bare.rom == rom);
    const auto zipped = sawgod::check_rom_bytes(
        "game.zip", zip_of({{"notes.txt", {'x'}}, {"Makai Toushi Sa-Ga (Japan).wsc", rom}}));
    CHECK(zipped.verdict == sawgod::RomVerdict::Ok);
    CHECK(zipped.name == "Makai Toushi Sa-Ga (Japan).wsc");
    std::vector<uint8_t> patched = rom;
    patched[0x1000] ^= 0xFF;
    CHECK(sawgod::check_rom_bytes("p.wsc", patched).verdict == sawgod::RomVerdict::Modified);
}

TEST_CASE("[ROM] GB layer: the C++ extraction matches the Python tools' reference") {
    const char *path = rom_path();
    const char *ref = std::getenv("SAWGOD_GB_REFERENCE");
    if (!path || !ref)
        return;
    const std::vector<uint8_t> rom = read(path);
    auto files = sawgod::game::extract_gb_shared(rom);
    std::map<std::string, std::string> assets;
    for (const auto &e : fs::directory_iterator(fs::path(SAWGOD_TEST_FIXTURES).parent_path().parent_path() /
                                                "assets" / "gb_maps")) {
        const auto d = read(e.path());
        assets[e.path().filename().string()] = std::string(d.begin(), d.end());
    }
    std::string error;
    REQUIRE(sawgod::game::build_gb_field_maps(rom, assets, &files, &error));
    int compared = 0;
    for (const auto &[name, data] : files) {
        const fs::path theirs = fs::path(ref) / name;
        if (!fs::exists(theirs))
            continue;
        ++compared;
        const std::vector<uint8_t> want = read(theirs);
        if (name.size() > 4 && name.substr(name.size() - 4) == ".png") {
            const std::vector<uint32_t> pal = {0xFFFFFF, 0xAAAAAA, 0x555555, 0x000000, 0xFF00FF};
            sawgod::IndexedImage a, b;
            REQUIRE(sawgod::decode_indexed_png(data, pal, &a, &error));
            REQUIRE(sawgod::decode_indexed_png(want, pal, &b, &error));
            INFO(name);
            CHECK(a.width == b.width);
            CHECK(a.height == b.height);
            size_t differ = a.pixels.size() == b.pixels.size() ? 0 : a.pixels.size() + b.pixels.size();
            for (size_t i = 0; !differ && i < a.pixels.size(); ++i)
                differ += a.pixels[i] != b.pixels[i];
            CHECK(differ == 0);
        } else if (name.size() > 10 && name.substr(name.size() - 10) == "_cells.csv") {
            // same cells, any order
            auto lines = [](const std::vector<uint8_t> &v) {
                std::vector<std::string> out;
                std::string s(v.begin(), v.end()), line;
                std::istringstream in(s);
                while (std::getline(in, line))
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back(), out.push_back(line);
                    else
                        out.push_back(line);
                std::sort(out.begin(), out.end());
                return out;
            };
            INFO(name);
            CHECK((lines(data) == lines(want)));
        } else {
            INFO(name);
            CHECK((std::string(data.begin(), data.end()) == std::string(want.begin(), want.end())));
        }
    }
    MESSAGE("compared " << compared << " files");
    CHECK(compared > 0);
}

TEST_CASE("ROM screen: layout and every state draws, SAWGOD_ROM_SCREEN_SHOTS=DIR writes them as PNGs") {
    using sawgod::app::RomScreenState;
    // the logo on top, centered; one choose-or-drop box vertically centered; errors under it, above the
    // footer
    const auto logo = sawgod::app::rom_screen_logo(), box = sawgod::app::rom_screen_box();
    CHECK(logo.x * 2 + logo.w == sawgod::app::kRomScreenW);
    CHECK(box.x * 2 + box.w == sawgod::app::kRomScreenW);
    CHECK(std::abs(box.y * 2 + box.h - sawgod::app::kRomScreenH) <= 1);
    CHECK(logo.y + logo.h < box.y - 2); // the focus ring
    CHECK(box.y + box.h + 2 < 182);     // the error's first line
    CHECK(230 + 15 <= 252);             // the SHA-1 under it, then the footer
    for (const char *s : {sawgod::app::rom_text::kChooseOrDrop.en, sawgod::app::rom_text::kChooseOrDrop.ja})
        CHECK(sawgod::app::silver_width(s) <= box.w - 16);
    // every line fits the canvas
    for (const auto *p : {&sawgod::app::rom_text::kGameBoy, &sawgod::app::rom_text::kInvalid,
                          &sawgod::app::rom_text::kCopyFailed, &sawgod::app::rom_text::kFoundRom,
                          &sawgod::app::rom_text::kWanted}) {
        CHECK(sawgod::app::silver_width(std::string("! ") + p->en) < sawgod::app::kRomScreenW - 16);
        CHECK(sawgod::app::silver_width(std::string("! ") + p->ja) < sawgod::app::kRomScreenW - 16);
    }
    CHECK(sawgod::app::silver_width(sawgod::kBaselineSha1) < sawgod::app::kRomScreenW - 16);
    // the whole font: any string the screen might be given draws every character
    for (const char *s : {"ñ", "ß", "¿", "«", "漢", "字", "ㄅ", "Ａ", "１"})
        CHECK(sawgod::app::silver_width(s) > 0);
    std::vector<std::pair<std::string, RomScreenState>> states;
    states.push_back({"choose", {}});
    RomScreenState hot;
    hot.box_hot = true;
    states.push_back({"choose_focused", hot});
    RomScreenState drag;
    drag.drop_hot = true;
    states.push_back({"dragging", drag});
    RomScreenState error;
    error.message = &sawgod::app::rom_text::kGameBoy;
    states.push_back({"error_gameboy", error});
    error.message = &sawgod::app::rom_text::kInvalid;
    error.expected_sha1 = sawgod::kBaselineSha1;
    states.push_back({"error_invalid", error});
    RomScreenState found;
    found.mode = RomScreenState::Mode::Found;
    states.push_back({"found", found});
    RomScreenState working;
    working.mode = RomScreenState::Mode::Working;
    working.progress = 0.45f;
    states.push_back({"working", working});
    const char *out = std::getenv("SAWGOD_ROM_SCREEN_SHOTS");
    // the logo, averaged down: size, an opaque pixel's color and premultiplied half-covered edges
    {
        const std::vector<uint32_t> src = {0xFFFF0000, 0x00000000, 0xFFFF0000, 0x00000000};
        const auto px = sawgod::app::scale_rom_screen_logo(src, 2, 2, 1, 1);
        REQUIRE(px.size() == 1);
        CHECK(px[0] == 0x80800000); // half covered red, premultiplied
        CHECK(sawgod::app::scale_rom_screen_logo(src, 2, 2, 2, 2) == src);
    }
    for (const auto &[name, state] : states) {
        const auto px = sawgod::app::draw_rom_screen(state);
        REQUIRE(px.size() == size_t(sawgod::app::kRomScreenW * sawgod::app::kRomScreenH));
        CHECK(std::count(px.begin(), px.end(), 0u) < px.size()); // something drawn
        if (out)
            sawgod::emu::write_file(
                (fs::path(out) / ("rom_screen_" + name + ".png")).string(),
                sawgod::emu::encode_png(px.data(), sawgod::app::kRomScreenW, sawgod::app::kRomScreenH));
    }
}

TEST_CASE("[ROM] first run: store the ROM, build both archives, rebuild a damaged or outdated one") {
    const char *path = rom_path();
    if (!path)
        return;
    const fs::path home = fs::temp_directory_path() / "sawgod_test_first_run";
    fs::remove_all(home);
    fs::create_directories(home);
    const std::string data = home.string() + "/";
    // a zipped ROM beside the game is found; the stored copy keeps the inner file's name
    const std::vector<uint8_t> rom = read(path);
    const auto zipped = zip_of({{"Makai Toushi Sa-Ga (Japan).wsc", rom}});
    std::ofstream(home / "game.zip", std::ios::binary)
        .write(reinterpret_cast<const char *>(zipped.data()), zipped.size());
    // the same file, whatever the separators (Windows: the data folder is written with a trailing "/")
    CHECK(fs::equivalent(sawgod::app::detect_rom_beside(data, ""), home / "game.zip"));
    CHECK(sawgod::app::stored_rom(data).empty());
    const auto check = sawgod::check_rom_file((home / "game.zip").string());
    REQUIRE(check.verdict == sawgod::RomVerdict::Ok);
    std::string error;
    const std::string stored = sawgod::app::store_rom(data, check.name, check.rom, &error);
    CHECK(fs::equivalent(stored, home / "rom" / "Makai Toushi Sa-Ga (Japan).wsc"));
    CHECK(sawgod::app::stored_rom(data) == stored);
    // both archives, current for this ROM
    const fs::path gb_maps =
        fs::path(SAWGOD_TEST_FIXTURES).parent_path().parent_path() / "assets" / "gb_maps";
    CHECK_FALSE(sawgod::app::archives_current(data, check.sha1, gb_maps.string()));
    REQUIRE(sawgod::app::ensure_archives(data, check.rom, check.sha1, gb_maps.string(), nullptr, &error));
    CHECK(sawgod::app::archives_current(data, check.sha1, gb_maps.string()));
    const auto wsc = sawgod::read_sgz(sawgod::app::wsc_archive_path(data));
    const auto gb = sawgod::read_sgz(sawgod::app::gb_archive_path(data));
    REQUIRE(wsc);
    REQUIRE(gb);
    CHECK(wsc->count("text/ja.csv") == 1);
    CHECK(gb->count("gb/title.png") == 1);
    CHECK(gb->count("gb/001_metatiles.png") == 1);
    // Authored GB inputs use content, including names, rather than revision or timestamps.
    const auto authored = home / "authored";
    fs::copy(gb_maps, authored, fs::copy_options::recursive);
    CHECK(sawgod::app::archives_current(data, check.sha1, authored.string()));
    fs::path sample;
    for (const auto &entry : fs::directory_iterator(authored))
        if (entry.path().extension() == ".csv") {
            sample = entry.path();
            break;
        }
    REQUIRE_FALSE(sample.empty());
    const auto original_input = read(sample);
    const auto input_time = fs::last_write_time(sample);
    const auto memo = data + "gb-inputs.cache";
    if (fs::exists(memo)) { // Reliable POSIX filesystems use the metadata fast path.
        const auto original_memo = read(memo);
        const auto memo_time = fs::last_write_time(memo);
        CHECK(sawgod::app::archives_current(data, check.sha1, authored.string()));
        CHECK(read(memo) == original_memo);
        CHECK(fs::last_write_time(memo) == memo_time);
        auto broken_memo = original_memo;
        broken_memo.back() ^= 1;
        REQUIRE(sawgod::WriteFileAtomic(memo, broken_memo));
        CHECK(sawgod::app::archives_current(data, check.sha1, authored.string()));
        CHECK(read(memo) == original_memo); // Damaged cache was rehashed/repaired.
    }
    auto same_size_edit = original_input;
    REQUIRE_FALSE(same_size_edit.empty());
    same_size_edit[0] ^= 1;
    { // In-place edit: inode stays the same, so restored mtime must still be caught through ctime.
        std::ofstream out(sample, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(same_size_edit.data()),
                  std::streamsize(same_size_edit.size()));
        REQUIRE(bool(out));
    }
    fs::last_write_time(sample, input_time);
    CHECK_FALSE(sawgod::app::archives_current(data, check.sha1, authored.string()));
    REQUIRE(sawgod::WriteFileAtomic(sample.string(), original_input));
    fs::last_write_time(sample, input_time);
    CHECK(sawgod::app::archives_current(data, check.sha1, authored.string()));
    auto edited_input = original_input;
    edited_input.push_back('\n');
    REQUIRE(sawgod::WriteFileAtomic(sample.string(), edited_input));
    fs::last_write_time(sample, input_time);
    CHECK_FALSE(sawgod::app::archives_current(data, check.sha1, authored.string()));
    REQUIRE(sawgod::WriteFileAtomic(sample.string(), original_input));
    CHECK(sawgod::app::archives_current(data, check.sha1, authored.string()));
    REQUIRE(sawgod::WriteFileAtomic((authored / "extra.csv").string(), original_input));
    CHECK_FALSE(sawgod::app::archives_current(data, check.sha1, authored.string()));
    fs::remove(authored / "extra.csv");
    CHECK(sawgod::app::archives_current(data, check.sha1, authored.string()));
    fs::remove(sample);
    CHECK_FALSE(sawgod::app::archives_current(data, check.sha1, authored.string()));
    REQUIRE(sawgod::WriteFileAtomic(sample.string(), edited_input));
    REQUIRE(sawgod::app::ensure_archives(data, check.rom, check.sha1, authored.string(), nullptr, &error));
    CHECK(sawgod::app::archives_current(data, check.sha1, authored.string()));
    auto rebuilt = sawgod::read_sgz(sawgod::app::gb_archive_path(data));
    REQUIRE(rebuilt);
    auto expected = *gb;
    expected.erase(sawgod::kSgzStampFile);
    rebuilt->erase(sawgod::kSgzStampFile);
    // Shared art depends only on the unchanged ROM; recompute every authored-map result from current inputs.
    std::map<std::string, std::string> current_inputs;
    for (const auto &entry : fs::directory_iterator(authored))
        if (entry.path().extension() == ".csv") {
            const auto bytes = read(entry.path());
            current_inputs[entry.path().filename().string()] = std::string(bytes.begin(), bytes.end());
        }
    REQUIRE(sawgod::game::build_gb_field_maps(check.rom, current_inputs, &expected, &error));
    CHECK(bool(*rebuilt == expected));
    // damage gb.sgz: only it is rebuilt
    const auto wsc_time = fs::last_write_time(sawgod::app::wsc_archive_path(data));
    std::ofstream(sawgod::app::gb_archive_path(data), std::ios::binary) << "broken";
    CHECK_FALSE(sawgod::app::archives_current(data, check.sha1, gb_maps.string()));
    REQUIRE(sawgod::app::ensure_archives(data, check.rom, check.sha1, gb_maps.string(), nullptr, &error));
    CHECK(sawgod::app::archives_current(data, check.sha1, gb_maps.string()));
    CHECK(fs::last_write_time(sawgod::app::wsc_archive_path(data)) == wsc_time);
    // an archive another build made is outdated
    CHECK_FALSE(sawgod::sgz_current(sawgod::app::gb_archive_path(data),
                                    {"gb", std::string(sawgod::kBuildId) + "x", check.sha1}));
    fs::remove_all(home);
}

TEST_CASE("picker IO validates raw and archive contents rather than filename extensions") {
    using V = sawgod::RomVerdict;
    const fs::path dir = fs::temp_directory_path() / "sawgod_picker_io";
    fs::create_directories(dir);
    const auto path = dir / "document";
    auto write = [&](const std::vector<uint8_t> &bytes) {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    };
    write(fake_wsc());
    CHECK(sawgod::app::check_rom_selection(path.string()).verdict == V::Modified);
    write(zip_of({{"dump/game.wsc", fake_wsc()}}));
    auto archive = sawgod::app::check_rom_selection(path.string());
    CHECK(archive.verdict == V::Modified);
    CHECK(archive.name == "game.wsc");
    write({});
    CHECK(sawgod::app::check_rom_selection(path.string()).verdict == V::Unreadable);
    // A provider can expose arbitrarily large documents: reject without reading it into unbounded RAM.
    {
        std::ofstream out(path, std::ios::binary);
        out.seekp(64u << 20);
        out.put('x');
    }
    CHECK(sawgod::app::check_rom_selection(path.string()).verdict == V::Unreadable);
    fs::remove_all(dir);
    CHECK(sawgod::app::check_rom_selection(path.string()).verdict == V::Unreadable);
}

TEST_CASE("derived cache: exact hit, changed input/version miss, damaged payload ignored") {
    const auto path = (fs::temp_directory_path() / "sawgod-derived-test.cache").string();
    fs::remove(path);
    const std::vector<uint8_t> bytes{0, 1, 2, 255, 0};
    CHECK_FALSE(sawgod::read_derived_cache(path, "inputs-v1"));
    sawgod::write_derived_cache(path, "inputs-v1", bytes);
    REQUIRE(sawgod::read_derived_cache(path, "inputs-v1"));
    CHECK(*sawgod::read_derived_cache(path, "inputs-v1") == bytes);
    CHECK_FALSE(sawgod::read_derived_cache(path, "inputs-v2"));
    auto damaged = read(path);
    damaged.back() ^= 1;
    REQUIRE(sawgod::WriteFileAtomic(path, damaged));
    CHECK_FALSE(sawgod::read_derived_cache(path, "inputs-v1"));
    fs::remove(path);
}

TEST_CASE("sgz verification receipt: hit is read-only, content changes with preserved size/time invalidate") {
    const auto dir = fs::temp_directory_path() / "sawgod-verify-receipt-test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const auto path = (dir / "archive.sgz").string();
    const sawgod::SgzStamp stamp{"wsc", "test", std::string(40, 'a')};
    std::string error;
    REQUIRE(sawgod::write_sgz(path, stamp, {{"x", {1, 2, 3, 4}}}, &error));
    REQUIRE(sawgod::sgz_current(path, stamp));
    const auto receipt = path + ".verified";
    REQUIRE(fs::exists(receipt));
    const auto receipt_time = fs::last_write_time(receipt);
    CHECK(sawgod::sgz_current(path, stamp));
    CHECK(fs::last_write_time(receipt) == receipt_time);
    const auto original_time = fs::last_write_time(path);
    auto bytes = read(path);
    // Damage the compressed first entry while preserving the ZIP directory and file metadata.
    const size_t name_size = bytes[26] | unsigned(bytes[27]) << 8;
    const size_t extra_size = bytes[28] | unsigned(bytes[29]) << 8;
    bytes[30 + name_size + extra_size] ^= 0xff;
    REQUIRE(sawgod::WriteFileAtomic(path, bytes));
    fs::last_write_time(path, original_time);
    CHECK_FALSE(sawgod::sgz_current(path, stamp));
    fs::remove_all(dir);
}

TEST_CASE("map bank cache: real maps preserve ROM and table output byte for byte") {
    const char *path = rom_path();
    if (!path) {
        MESSAGE("set SAWGOD_WSC_ROM for real map cache proof");
        return;
    }
    const auto rom = read(path);
    REQUIRE(rom.size() == 0x400000);
    const auto cache = (fs::temp_directory_path() / "sawgod-real-map-bank-test.cache").string();
    fs::remove(cache);
    auto original = std::make_unique<sawgod::emu::Wsc>(rom, 0);
    std::vector<sawgod::game::MapData> maps(sawgod::game::kMapCount);
    std::vector<const sawgod::game::MapData *> use(maps.size());
    for (int id = 0; id < sawgod::game::kMapCount; ++id)
        if (sawgod::game::map_has_data(id, *original, nullptr)) {
            maps[size_t(id)] = sawgod::game::extract_map(id, *original);
            use[size_t(id)] = &maps[size_t(id)];
        }
    const auto &layout = sawgod::game::table_layout(sawgod::game::Table::maps);
    for (bool edited : {false, true}) {
        if (edited)
            maps[1].type_words[0] ^= 1;
        auto reference = std::make_unique<sawgod::emu::Wsc>(rom, 0);
        sawgod::game::DataTables reference_tables;
        std::vector<std::string> reference_errors;
        const auto moved =
            sawgod::game::build_map_banks(*reference, reference_tables, use, &reference_errors);
        REQUIRE(reference_errors.empty());
        for (bool want_hit : {false, true}) {
            auto current = std::make_unique<sawgod::emu::Wsc>(rom, 0);
            sawgod::game::DataTables tables;
            std::vector<std::string> errors;
            bool hit = false;
            CHECK(sawgod::game::build_map_banks_cached(*current, tables, use, cache, &errors, &hit) == moved);
            CHECK(hit == want_hit);
            CHECK(errors == reference_errors);
            CHECK(bool(current->rom == reference->rom));
            for (unsigned id = 0; id < layout.max_rows; ++id) {
                const auto *a = tables.row(sawgod::game::Table::maps, id);
                const auto *b = reference_tables.row(sawgod::game::Table::maps, id);
                REQUIRE(bool(a) == bool(b));
                if (a)
                    CHECK(std::equal(a, a + layout.stride, b));
            }
        }
    }
    fs::remove(cache);
}
