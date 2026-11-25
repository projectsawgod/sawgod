// Field maps as grid files (game/maps.h).
#include <doctest/doctest.h>

#include "engine/derived_cache.h"
#include "game/maps.h"
#include "game/tables.h"
#include <filesystem>
#include <memory>

using namespace sawgod::game;

TEST_CASE("map files: cells, attributes and metatiles round-trip, bad files are refused") {
    MapData m;
    for (size_t i = 0; i < m.cells.size(); ++i)
        m.cells[i] = uint8_t(i * 7);
    for (size_t i = 0; i < m.attributes.size(); ++i)
        m.attributes[i] = uint8_t(i * 3);
    for (int i = 0; i < 256; ++i)
        m.tiles[size_t(i)] = uint8_t(255 - i), m.mirror[size_t(i)] = uint8_t(i & 3);
    for (int i = 0; i < 32; ++i)
        m.type_words[size_t(i)] = uint16_t(i * 1000), m.type_bytes[size_t(i)] = uint8_t(i * 5);
    MapData back;
    std::string error;
    REQUIRE(map_from_files(map_cells_csv(m), map_attributes_csv(m), map_metatiles_csv(m), map_types_csv(m),
                           &back, &error));
    CHECK(back.cells == m.cells);
    CHECK(back.attributes == m.attributes);
    CHECK(back.tiles == m.tiles);
    CHECK(back.mirror == m.mirror);
    CHECK(back.type_words == m.type_words);
    CHECK(back.type_bytes == m.type_bytes);

    std::string cells = map_cells_csv(m);
    cells.replace(0, cells.find(','), "256"); // out of range
    CHECK_FALSE(
        map_from_files(cells, map_attributes_csv(m), map_metatiles_csv(m), map_types_csv(m), &back, &error));
    CHECK(error.find("row 1, column 1") != std::string::npos);
    CHECK_FALSE(map_from_files(map_cells_csv(m), "1,2\n", map_metatiles_csv(m), map_types_csv(m), &back,
                               &error)); // wrong size
    CHECK_FALSE(map_from_files(map_cells_csv(m), map_attributes_csv(m), "id,tile,mirror\n0,1,4\n",
                               map_types_csv(m), &back, &error));
}

TEST_CASE("map bank cache: identical output, all map fields, ROM and headers invalidate") {
    namespace fs = std::filesystem;
    const auto path = (fs::temp_directory_path() / "sawgod-map-bank-test.cache").string();
    fs::remove(path);
    // Zero map banks/header: tests attribute output and every key field without requiring a ROM.
    const std::vector<uint8_t> rom(4u << 20, 0);
    MapData map;
    map.type_words[0] = 0x1234;
    std::vector<const MapData *> maps(kMapCount, nullptr);
    maps[1] = &map;
    auto run = [&](bool expected_hit, const std::vector<uint8_t> &input, bool header = false) {
        auto machine = std::make_unique<sawgod::emu::Wsc>(input, 0);
        DataTables tables;
        if (header)
            tables.set_row(Table::maps, 0, std::vector<uint8_t>(table_layout(Table::maps).stride, 0));
        std::vector<std::string> errors;
        bool hit = false;
        const int moved = build_map_banks_cached(*machine, tables, maps, path, &errors, &hit);
        CHECK(hit == expected_hit);
        auto reference = std::make_unique<sawgod::emu::Wsc>(input, 0);
        DataTables reference_tables;
        if (header)
            reference_tables.set_row(Table::maps, 0,
                                     std::vector<uint8_t>(table_layout(Table::maps).stride, 0));
        std::vector<std::string> reference_errors;
        CHECK(moved == build_map_banks(*reference, reference_tables, maps, &reference_errors));
        CHECK(errors == reference_errors);
        CHECK(bool(machine->rom == reference->rom));
        for (unsigned id = 0; id < table_layout(Table::maps).max_rows; ++id) {
            const auto *a = tables.row(Table::maps, id), *b = reference_tables.row(Table::maps, id);
            REQUIRE(bool(a) == bool(b));
            if (a)
                CHECK(std::equal(a, a + table_layout(Table::maps).stride, b));
        }
    };
    run(false, rom);
    run(true, rom);
    for (int field = 0; field < 6; ++field) {
        switch (field) {
        case 0:
            ++map.cells[0];
            break;
        case 1:
            ++map.attributes[0];
            break;
        case 2:
            ++map.tiles[0];
            break;
        case 3:
            ++map.mirror[0];
            break;
        case 4:
            ++map.type_words[0];
            break;
        default:
            ++map.type_bytes[0];
            break;
        }
        run(false, rom);
        run(true, rom);
    }
    auto changed_rom = rom;
    changed_rom[100] = 1;
    run(false, changed_rom);
    run(true, changed_rom);
    run(false, changed_rom, true);
    run(true, changed_rom, true);
    maps[1] = nullptr;
    run(false, changed_rom, true);
    run(true, changed_rom, true);
    sawgod::write_derived_cache(path, "wrong algorithm version", {1, 2, 3});
    run(false, changed_rom, true);
    fs::remove(path);
}
