// Data tables (game/tables.h): CSV overrides over the ROM's rows, and the accessors natives read through.
#include <doctest/doctest.h>

#include <memory>

#include "emu/wsc.h"
#include "game/machine.h"
#include "game/session.h"
#include "game/tables.h"

using namespace sawgod::game;
namespace emu = sawgod::emu;

namespace {

std::unique_ptr<emu::Wsc> make_wsc() {
    std::vector<uint8_t> rom(0x400000, 0);
    for (unsigned i = 0; i < 8; ++i) // item 2's record
        rom[0x3943E0 + 2 * 8 + i] = uint8_t(0x10 + i);
    rom[0x34C127 + 2 * 4] = 0x2C; // item 2 costs 300 (0x12C)
    rom[0x34C127 + 2 * 4 + 1] = 0x01;
    for (unsigned i = 0; i < 10; ++i) // shop 1: items 5, 6, then empty
        rom[0x34C527 + 10 + i] = i < 2 ? uint8_t(5 + i) : 0xFF;
    for (unsigned i = 0; i < 8; ++i) // encounter table 1
        rom[0x350C3C + 8 + i] = uint8_t(0x20 + i);
    for (unsigned i = 0; i < 7; ++i) // encounter_odds thresholds
        rom[0x350C31 + i] = uint8_t(128 >> i);
    for (unsigned i = 0; i < 4; ++i) // encounter_odds travel-mode modifiers
        rom[0x350C38 + i] = uint8_t(1 + i);
    for (unsigned i = 0; i < 18; ++i) // map 1's header
        rom[0x35A830 + 18 + i] = uint8_t(0x30 + i);
    rom[0x397A08 + 1] = 5;           // monster type 1's rank
    rom[0x397AD0 + 1] = 3;           // monster type 1's family
    rom[0x397B98 + 3 * 29 + 4] = 7;  // meat_pairs row 3, column 4
    rom[0x397E6D + 7 * 16 + 2] = 42; // meat_results row 7, rank 2
    return std::make_unique<emu::Wsc>(std::move(rom), 0x8000);
}

} // namespace

TEST_CASE("DataTables: a CSV changes only the cells it gives") {
    const auto wp = make_wsc();
    emu::Wsc &w = *wp;
    DataTables t;
    CHECK(t.empty());
    std::string error;
    std::vector<std::string> warnings;
    int changed = 0;
    REQUIRE(t.load_csv("items", "id,name,price,uses,ai_target_flag\n2,ignored,1234,,1\n", w, &error,
                       &warnings, &changed));
    CHECK(changed == 1);
    CHECK(warnings.empty());
    const uint8_t *price = t.row(Table::item_prices, 2);
    REQUIRE(price);
    CHECK((price[0] | price[1] << 8 | price[2] << 16 | price[3] << 24) == 1234);
    const uint8_t *rec = t.row(Table::item_records, 2);
    REQUIRE(rec);
    CHECK(rec[3] == 0x13);                 // uses: empty cell, the ROM's
    CHECK(rec[7] == uint8_t(0x17 | 0x80)); // ai_target_flag is bit 7 of byte 7; bits 0-6 stay
    CHECK(rec[0] == 0x10);
    CHECK(t.row(Table::item_records, 3) == nullptr); // untouched rows read the ROM
    CHECK_FALSE(t.overridden(Table::shops));
}

TEST_CASE("DataTables: shop lists, refusals") {
    const auto wp = make_wsc();
    emu::Wsc &w = *wp;
    DataTables t;
    std::string error;
    std::vector<std::string> warnings;
    int changed = 0;
    REQUIRE(t.load_csv("shops", "id,items,items_names\n1,7 8 9,x\n22,1\n0,1 2 3 4 5 6 7 8 9 10 11\n3,300\n",
                       w, &error, &warnings, &changed));
    CHECK(changed == 1);
    CHECK(warnings.size() == 3); // past the 22 shops, more than 10 items, not a byte
    const uint8_t *shop = t.row(Table::shops, 1);
    REQUIRE(shop);
    CHECK(shop[0] == 7);
    CHECK(shop[2] == 9);
    CHECK(shop[3] == 0xFF);
    CHECK(t.row(Table::shops, 0) == nullptr);
    CHECK_FALSE(t.load_csv("shops", "items\n1 2\n", w, &error));
    CHECK(error == "no id column");
}

TEST_CASE("table_at / table_u8: the store's bytes where overridden, else exactly the original read") {
    const auto wp = make_wsc();
    emu::Wsc &w = *wp;
    Machine m(w);
    const uint16_t seg = table_layout(Table::shops).segment, base = table_layout(Table::shops).offset;
    // nothing loaded: identical to the original's read, including outside the table
    for (uint16_t off : {uint16_t(base + 10), uint16_t(base + 220), uint16_t(0x1234)})
        CHECK(table_at(m, Table::shops, seg, off) == m.rd8(seg, off));
    Session &s = Session::of(w);
    s.tables().set_row(Table::shops, 1, {9, 8, 7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
    CHECK(table_at(m, Table::shops, seg, uint16_t(base + 10)) == 9);
    CHECK(table_at(m, Table::shops, seg, uint16_t(base + 12)) == 7);
    CHECK(table_u8(m, Table::shops, 1, 1) == 8);
    // other rows, the debug shop's list after row 21, and other segments still read the machine
    CHECK(table_at(m, Table::shops, seg, base) == m.rd8(seg, base));
    CHECK(table_at(m, Table::shops, seg, uint16_t(base + 220)) == m.rd8(seg, uint16_t(base + 220)));
    CHECK(table_at(m, Table::shops, 0x9000, uint16_t(base + 10)) == m.rd8(0x9000, uint16_t(base + 10)));
    s.tables().set_row(Table::item_prices, 2, {0x10, 0x27, 0, 0});
    CHECK(table_at16(m, Table::item_prices, 0x4000, uint16_t(0xC127 + 8)) == 10000);
}

TEST_CASE("DataTables: encounters, encounter_odds, maps, meat_* load and override") {
    const auto wp = make_wsc();
    emu::Wsc &w = *wp;
    DataTables t;
    std::string error;
    int changed = 0;
    REQUIRE(t.load_csv("encounters", "id,chance,formation_50pct\n1,99,200\n", w, &error, nullptr, &changed));
    CHECK(changed == 1);
    const uint8_t *enc = t.row(Table::encounters, 1);
    REQUIRE(enc);
    CHECK(enc[0] == 99);
    CHECK(enc[1] == 200);
    CHECK(enc[2] == 0x22); // formation_25pct: not given, stays the ROM's

    REQUIRE(t.load_csv("encounter_odds", "id,threshold1,mode0\n0,255,9\n", w, &error, nullptr, &changed));
    CHECK(changed == 1);
    const uint8_t *th = t.row(Table::encounter_thresholds, 0);
    REQUIRE(th);
    CHECK(th[0] == 255);
    CHECK(th[1] == 64); // threshold2 untouched, stays the ROM's
    const uint8_t *mod = t.row(Table::encounter_modifiers, 0);
    REQUIRE(mod);
    CHECK(mod[0] == 9);

    REQUIRE(t.load_csv("maps", "id,tileset,running_allowed\n1,5,0\n", w, &error, nullptr, &changed));
    CHECK(changed == 1);
    const uint8_t *map = t.row(Table::maps, 1);
    REQUIRE(map);
    CHECK(map[0] == 5);
    CHECK(map[1] == 0x31); // data_bank: not given, stays the ROM's
    CHECK(map[17] == 0);

    REQUIRE(t.load_csv("monster_types", "id,rank,family\n1,9,2\n", w, &error, nullptr, &changed));
    CHECK(changed == 1);
    CHECK(t.row(Table::monster_type_rank, 1)[0] == 9);
    CHECK(t.row(Table::monster_type_family, 1)[0] == 2);
    REQUIRE(t.load_csv("monster_types", "id,name,graphics,descriptor\n2,ignored,770,513\n", w, &error,
                       nullptr, &changed)); // name: read-only, ignored
    CHECK(t.row(Table::monster_type_graphics, 2)[0] == 0x02);
    CHECK(t.row(Table::monster_type_graphics, 2)[1] == 0x03);
    CHECK(t.row(Table::monster_type_descriptor, 2)[0] == 0x01);
    CHECK(t.row(Table::monster_type_descriptor, 2)[1] == 0x02);

    REQUIRE(t.load_csv("meat_pairs", "id,f4\n3,55\n", w, &error, nullptr, &changed));
    CHECK(changed == 1);
    CHECK(t.row(Table::meat_pairs, 3)[4] == 55);
    CHECK(t.row(Table::meat_pairs, 3)[5] == 0); // f5: not given, stays the ROM's (0 here)

    REQUIRE(t.load_csv("meat_results", "id,rank2\n7,11\n", w, &error, nullptr, &changed));
    CHECK(changed == 1);
    CHECK(t.row(Table::meat_results, 7)[2] == 11);
}

TEST_CASE("table_at: encounters/maps read through segment 5000h, monster_types/meat through 9000h") {
    const auto wp = make_wsc();
    emu::Wsc &w = *wp;
    Machine m(w);
    Session &s = Session::of(w);
    const TableLayout &enc = table_layout(Table::encounters);
    CHECK(table_at(m, Table::encounters, enc.segment, enc.offset) == m.rd8(enc.segment, enc.offset));
    s.tables().set_row(Table::encounters, 0, {1, 2, 3, 4, 5, 6, 7, 8});
    CHECK(table_at(m, Table::encounters, enc.segment, enc.offset) == 1);
    CHECK(table_at(m, Table::encounters, enc.segment, uint16_t(enc.offset + 7)) == 8);

    const TableLayout &maps = table_layout(Table::maps);
    s.tables().set_row(Table::maps, 2, std::vector<uint8_t>(18, 0x77));
    CHECK(table_at(m, Table::maps, maps.segment, uint16_t(maps.offset + 2 * 18 + 5)) == 0x77);
    CHECK(table_at(m, Table::maps, maps.segment, uint16_t(maps.offset + 1 * 18)) ==
          m.rd8(maps.segment, uint16_t(maps.offset + 1 * 18))); // row 1 untouched

    const TableLayout &fam = table_layout(Table::monster_type_family);
    CHECK(table_at(m, Table::monster_type_family, fam.segment, uint16_t(fam.offset + 1)) == 3); // ROM seed
}

TEST_CASE("pointer tables: variable-length records packed behind their pointers, served by table_at") {
    const auto wp = make_wsc(); // all-zero pointers: every ROM record is an empty one at the bank's start
    emu::Wsc &w = *wp;
    Machine m(w);
    Session &s = Session::of(w);
    DataTables &t = s.tables();
    const TableLayout &ml = table_layout(Table::monsters);
    std::string error;
    std::vector<std::string> warnings;
    int changed = 0;
    REQUIRE(t.load_csv("monsters", "id,name,hp,gear_count,gear\n3,x,500,3,1 2 3\n4,,,2,9\n200,,1,,\n", w,
                       &error, &warnings, &changed));
    CHECK(changed == 1);
    CHECK(warnings.size() == 2); // gear_count 2 but one id; past the 200 types
    const std::vector<uint8_t> *rec = t.record(Table::monsters, 3);
    REQUIRE(rec);
    CHECK(rec->size() == 14);
    CHECK((*rec)[2] == 3);
    CHECK(((*rec)[3] | (*rec)[4] << 8) == 500);
    CHECK((*rec)[13] == 3);
    // pointers: the 11-byte records back to back from `data`, row 4 moved by row 3's 3 gear ids
    CHECK(table_at16(m, Table::monsters, ml.segment, uint16_t(ml.offset + 2 * 3)) == ml.data + 33);
    CHECK(table_at16(m, Table::monsters, ml.segment, uint16_t(ml.offset + 2 * 4)) == ml.data + 47);
    CHECK(table_at16(m, Table::monsters, ml.segment, uint16_t(ml.data + 33 + 3)) == 500);
    CHECK(table_at(m, Table::monsters, ml.segment, uint16_t(ml.data + 33 + 12)) == 2);
    // past the packed records, and other segments: the machine's own read
    const uint16_t end = uint16_t(ml.data + 200 * 11 + 3);
    CHECK(table_at(m, Table::monsters, ml.segment, end) == m.rd8(ml.segment, end));
    CHECK(table_at(m, Table::monsters, 0x9000, ml.offset) == m.rd8(0x9000, ml.offset));
    std::vector<std::string> diffs;
    t.compare_rom(w, &diffs);
    CHECK(diffs.size() == 200); // (this ROM's pointers are all 0, the packed ones never)
}

TEST_CASE("formations: groups set, cleared and compacted") {
    const auto wp = make_wsc();
    emu::Wsc &w = *wp;
    DataTables t;
    std::string error;
    std::vector<std::string> warnings;
    REQUIRE(t.load_csv(
        "formations",
        "id,cant_run,group1_type,group1_name,group1_min,group1_max,group2_type,group2_min,group2_max\n"
        "5,1,7,x,1,3,9,2,2\n6,,,,,,8,1,4\n7,,4,,,,,,\n8,,300,,1,1,,,\n",
        w, &error, &warnings));
    CHECK(warnings.size() == 2); // group1 of 7: no min/max to keep; type 300
    const std::vector<uint8_t> *f5 = t.record(Table::formations, 5);
    REQUIRE(f5);
    CHECK(*f5 == std::vector<uint8_t>{0x12, 7, 0x31, 9, 0x22});
    const std::vector<uint8_t> *f6 = t.record(Table::formations, 6); // no group 1: group 2 moves up
    REQUIRE(f6);
    CHECK(*f6 == std::vector<uint8_t>{0x01, 8, 0x41});
    CHECK(t.record(Table::formations, 0)->size() == 1); // the ROM's (no groups)
    CHECK_FALSE(t.load_csv("formations", "cant_run\n1\n", w, &error));
}
