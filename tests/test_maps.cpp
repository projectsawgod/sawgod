// Field maps as grid files (game/maps.h).
#include <doctest/doctest.h>

#include "game/maps.h"

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
