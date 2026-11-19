#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "game/save_slots.h"

using sawgod::game::SaveSlots;

namespace {
std::vector<uint8_t> Payload(uint8_t seed) {
    std::vector<uint8_t> p(SaveSlots::kPayload);
    for (size_t i = 0; i < p.size(); ++i)
        p[i] = uint8_t(seed + i * 7);
    p[0x435] = 0x1B; // the magic bytes write_save_slot sets before copying
    p[0x436] = 0xE4;
    return p;
}
} // namespace

TEST_CASE("save slots: 3 is the original, the count changes only when a list opens") {
    SaveSlots s;
    CHECK(s.count() == 3);
    CHECK(!s.extended());
    CHECK(s.slot_for_row(2) == 2);
    s.request_count(9);
    CHECK(s.count() == 3); // not until apply()
    s.apply();
    CHECK(s.count() == 9);
    s.request_count(7); // anything else is 3
    s.apply();
    CHECK(s.count() == 3);
}

TEST_CASE("save slots: the list scrolls at its edges and never wraps") {
    SaveSlots s;
    s.request_count(9);
    s.apply();
    uint8_t row = 0;
    CHECK(!s.move(row, false)); // at slot 1: up stays
    CHECK(row == 0);
    CHECK(s.top() == 0);
    CHECK(!s.move(row, true));
    CHECK(!s.move(row, true));
    CHECK(row == 2);
    for (int i = 0; i < 6; ++i)
        CHECK(s.move(row, true)); // scrolls slots 4..9 into the bottom row
    CHECK(s.slot_for_row(row) == 8);
    CHECK(!s.move(row, true)); // at slot 9: down stays
    CHECK(s.slot_for_row(row) == 8);
    CHECK(s.top() == 6);
    row = s.show(1); // slot 2 above the window: scrolls up to it
    CHECK(s.top() == 1);
    CHECK(row == 0);
    row = s.show(5);
    CHECK(s.top() == 3);
    CHECK(row == 2);
}

TEST_CASE("save slots: without a store only slots 4-9 are held, with one, all nine") {
    SaveSlots s;
    CHECK(!s.has(8));
    std::vector<uint8_t> block;
    CHECK(!s.load(8, &block));
    s.store(8, Payload(3));
    s.store(2, Payload(1)); // an original SRAM slot without a store: ignored
    CHECK(!s.has(2));
    s.set_last(2); // the original byte 0x3300 decides
    CHECK(s.last() == -1);
    s.set_last(8);
    CHECK(s.last() == 8);
    CHECK(s.slot_dirty(8));
    CHECK(s.last_dirty());
    REQUIRE(s.load(8, &block));
    CHECK(block.size() == SaveSlots::kBlock);
    CHECK(s.payload(8) == Payload(3));

    SaveSlots t;
    t.attach_store();
    t.store(0, Payload(4));
    t.set_last(0);
    CHECK(t.has(0));
    CHECK(t.last() == 0);
    REQUIRE(t.load(0, &block));
    // a stored block verifies like an original slot (checksum, marker bytes set by store())
    std::vector<uint8_t> unmarked = Payload(5);
    unmarked[0x436] = 0;
    t.store(1, unmarked);
    CHECK(t.load(1, &block));
    CHECK(block[0x436] == 0xE4);
}

TEST_CASE("save slots: 9 -> 3 -> 9 keeps slots 4-9, locked slots never become dirty") {
    SaveSlots s;
    s.request_count(9);
    s.apply();
    s.store(6, Payload(2));
    s.request_count(3);
    s.apply();
    CHECK(!s.extended());
    CHECK(s.has(6)); // hidden, kept
    s.request_count(9);
    s.apply();
    std::vector<uint8_t> block;
    CHECK(s.load(6, &block));

    s.clear_dirty();
    s.lock(5);
    s.store(5, Payload(7));
    CHECK(s.has(5));
    CHECK(!s.slot_dirty(5));
    CHECK(!s.dirty());
}

TEST_CASE("save slots: AUTO is the first entry while the option is on, and the lists scroll") {
    SaveSlots s;
    s.attach_store();
    s.request_auto(true);
    s.apply(); // 3 slots + AUTO: four entries, a scrolling list
    CHECK(s.auto_listed());
    CHECK(s.entries() == 4);
    CHECK(s.extended());
    CHECK(s.slot_for_row(0) == SaveSlots::kAuto);
    CHECK(s.slot_for_row(1) == 0);
    uint8_t row = 2;
    CHECK(s.move(row, true)); // scrolls to show slot 3
    CHECK(s.slot_for_row(row) == 2);
    CHECK(s.show(SaveSlots::kAuto) == 0);
    CHECK(s.top() == 0);
    s.store(SaveSlots::kAuto, std::vector<uint8_t>(SaveSlots::kPayload, 0));
    CHECK(s.has(SaveSlots::kAuto));
    CHECK(s.slot_dirty(SaveSlots::kAuto));
    s.request_auto(false);
    s.apply(); // off: hidden (kept), the original three rows
    CHECK_FALSE(s.auto_listed());
    CHECK_FALSE(s.extended());
    CHECK(s.slot_for_row(0) == 0);
    CHECK(s.has(SaveSlots::kAuto));
    SaveSlots bare; // no store: no AUTO
    bare.request_auto(true);
    bare.apply();
    CHECK_FALSE(bare.auto_listed());
}
