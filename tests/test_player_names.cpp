#include "game/bank34.h"
#include "game/bank34_misc.h"
#include "game/bank36_battle.h"
#include "game/bank36_misc.h"
#include "game/player_names.h"
#include "game/save_file.h"
#include "game/save_slots.h"
#include "game/session.h"
#include "game/text_render.h"
#include <algorithm>
#include <doctest/doctest.h>

using namespace sawgod::game;

TEST_CASE("ten glyph names round-trip as host strings in numbered and autosave slots") {
    std::vector<uint8_t> payload(SaveSlots::kPayload, 0);
    for (int m = 0; m < 4; ++m)
        for (int i = 1; i <= 4; ++i)
            payload[size_t(m * 31 + i)] = 0xD9;
    PartyNameList names{"Alexandria", "ABCDEFGHIJ", "アイウエオカキクケコ", "Renée"};
    const auto text = save_to_text(payload, &names);
    CHECK(text.find("name = \"ABCDEFGHIJ\"") != std::string::npos);
    PartyNameList loaded;
    std::vector<uint8_t> back;
    std::string error;
    REQUIRE_MESSAGE(save_from_text(text, &back, &error, &loaded), error);
    CHECK(loaded == names);
    CHECK(back[5] == payload[5]); // species byte was not consumed by longer names
    SaveSlots slots;
    slots.attach_store();
    const auto stored = back;
    for (int slot = 0; slot < SaveSlots::kSlots; ++slot) {
        slots.store(slot, stored, names);
        CHECK(slots.names(slot) == names);
        REQUIRE(slots.load(slot, &back));
    }
    PartyNameList bad = names;
    bad[0] = "ABCDEFGHIJK";
    CHECK_FALSE(save_from_text(save_to_text(payload, &bad), &back, &error, &loaded));
    CHECK(loaded == names);
    CHECK(error.find("10") != std::string::npos);
    // Without host names the file carries the names the four machine bytes spell; they load back as is.
    const auto derived = save_to_text(payload);
    CHECK(derived.find("format = 2") != std::string::npos);
    REQUIRE(save_from_text(derived, &back, &error, &loaded));
    CHECK(save_to_text(back, &loaded) == derived);
    CHECK_FALSE(valid_player_name("bad\nname"));
    CHECK_FALSE(valid_player_name(std::string("\xC0\xAF", 2)));
    CHECK(valid_player_name(names[2]));
}

TEST_CASE("host names follow party swaps, survive species changes, and clear on new games") {
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    w.io[0x60] = 0x80;
    Machine m(w);
    auto &s = Session::of(w);
    auto &names = s.player_names();
    names.enabled = true;
    names.values = {"Alexandria", "ABCDEFGHIJ", "", ""};
    m.ds() = m.ss() = 0;
    m.si() = 0xD000;
    m.di() = 0xD01F;
    swap_party_records_core(m);
    CHECK(names.values[0] == "ABCDEFGHIJ");
    CHECK(names.values[1] == "Alexandria");
    names.draft.assign(10, NameGlyph{0xFE}); // an unmapped ROM code still occupies one glyph
    names.accept(2);
    CHECK(valid_player_name(names.values[2]));
    CHECK(utf8_to_u32(names.values[2]).size() == kPlayerNameGlyphs);
    CHECK(names.rendered(0) == U"ABCDEFGH");
    CHECK(names.values[0] == "ABCDEFGHIJ"); // old saved name remains intact
    w.ram[0xD005] = 80;
    CHECK(names.values[0] == "ABCDEFGHIJ");
    m.ss() = 0;
    m.set_al(0);
    m.di() = 0xD60F;
    build_status_icons_core(m);
    CHECK(m.rd16(0, 0xD60F) == (kTextWord | TextTable::kPlayerNameRun));
    REQUIRE(s.text().run(TextTable::kPlayerNameRun));
    CHECK(*s.text().run(TextTable::kPlayerNameRun) == U"ABCDEFGH");
    s.new_generation();
    CHECK(names.values == PartyNameList{});
    CHECK(names.enabled);
    names.values[0] = "Fresh";
    clear_game_state_core(m);
    CHECK(names.values == PartyNameList{});
}

namespace {
// Tags member record `m` (name bytes, species, flags) and its 204-byte history at 0xD105 + 0xCC * m with
// `tag`.
void tag_member(sawgod::emu::Wsc &w, int m, uint8_t tag) {
    const size_t at = 0xD000 + size_t(m) * 31, queue = 0xD105 + size_t(m) * 0xCC;
    std::fill_n(w.ram.begin() + long(at + 1), 30, tag);
    std::fill_n(w.ram.begin() + long(queue), 204, tag);
}
// The tag every per-member field of slot `m` carries, or -1 when they disagree. A loaded save's name bytes
// are the host name's projection, so `name_bytes` false skips them.
int member_tag(const uint8_t *state, int m, bool name_bytes = true) {
    const size_t at = size_t(m) * 31, queue = 0x105 + size_t(m) * 0xCC;
    const uint8_t tag = state[at + 5];
    for (size_t i = name_bytes ? 1 : 5; i < 31; ++i)
        if (state[at + i] != tag)
            return -1;
    for (size_t i = 0; i < 204; ++i)
        if (state[queue + i] != tag)
            return -1;
    return tag;
}
} // namespace

// The after-battle sort moved the records and histories but not the host names, so a name
// ended up on another member's class, species and meat history.
TEST_CASE("host names, records and histories stay together through the battle sort, Order and saves") {
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    w.io[0x60] = 0x80;
    Machine m(w);
    auto &names = Session::of(w).player_names();
    names.enabled = true;
    names.values = {"Treesong", "T-Money", "Ayden", "Kaiser"};
    const std::array<uint8_t, 4> tags{0xA1, 0xA2, 0xA3, 0xA4};
    for (int i = 0; i < 4; ++i) {
        tag_member(w, i, tags[size_t(i)]);
        w.ram[0xD000 + size_t(i) * 31] = 0; // status: fine
    }
    w.ram[0xD000 + 2 * 31] = 0x01; // Ayden is down: the sort moves Kaiser ahead of him
    m.ds() = m.ss() = 0;
    sort_party_after_battle_core(m);
    REQUIRE(member_tag(w.ram.data() + 0xD000, 2) == 0xA4); // the records did move
    for (int i = 0; i < 4; ++i)
        CHECK(member_tag(w.ram.data() + 0xD000, i) != -1);
    CHECK(names.values == PartyNameList{"Treesong", "T-Money", "Kaiser", "Ayden"});

    // Order: swap slots 1 and 3 (the menu's picked and hovered rows), then back.
    for (int round = 0; round < 2; ++round) {
        w.ram[0xD840] = 1;
        w.ram[0xD842] = 3;
        m.ds() = m.ss() = 0;
        m.si() = 0xD000 + 31;
        m.di() = 0xD000 + 3 * 31;
        swap_party_records_core(m);
        const bool swapped = round == 0;
        CHECK(member_tag(w.ram.data() + 0xD000, 1) == (swapped ? 0xA3 : 0xA2));
        CHECK(member_tag(w.ram.data() + 0xD000, 3) == (swapped ? 0xA2 : 0xA3));
        CHECK(names.values[1] == (swapped ? "Ayden" : "T-Money"));
        CHECK(names.values[3] == (swapped ? "T-Money" : "Ayden"));
    }

    // A save and load keeps each member's name, record and history in one [[member]].
    const std::vector<uint8_t> payload(w.ram.begin() + 0xD000, w.ram.begin() + 0xD000 + SaveSlots::kPayload);
    std::vector<uint8_t> back;
    PartyNameList loaded;
    std::string error;
    REQUIRE_MESSAGE(save_from_text(save_to_text(payload, &names.values), &back, &error, &loaded), error);
    CHECK(loaded == names.values);
    for (int i = 0; i < 4; ++i)
        CHECK(member_tag(back.data(), i, false) == member_tag(payload.data(), i));
}

// All four priority bits plus the empty-slot sentinel, in every combination: after
// sort_party_after_battle each slot's host name is the one tagged on the record and history now there.
TEST_CASE("the after-battle sort keeps names with records and histories for every status combination") {
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    w.io[0x60] = 0x80;
    Machine m(w);
    auto &names = Session::of(w).player_names();
    names.enabled = true;
    const PartyNameList tagged{"ANNALISE", "BARTHOLO", "CORDELIA", "DMITRIUS"};
    const std::array<uint8_t, 17> statuses{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0xFF};
    int moved = 0;
    for (int combo = 0; combo < 17 * 17 * 17 * 17; ++combo) {
        CAPTURE(combo);
        names.values = tagged;
        for (int i = 0, c = combo; i < 4; ++i, c /= 17) {
            tag_member(w, i, uint8_t(0xA1 + i));
            w.ram[0xD000 + size_t(i) * 31] = statuses[size_t(c % 17)];
        }
        m.ds() = m.ss() = 0;
        sort_party_after_battle_core(m);
        for (int i = 0; i < 4; ++i) {
            const int tag = member_tag(w.ram.data() + 0xD000, i);
            REQUIRE(tag >= 0xA1);
            REQUIRE(tag <= 0xA4);
            CHECK(names.values[size_t(i)] == tagged[size_t(tag - 0xA1)]);
            moved += tag != 0xA1 + i;
        }
    }
    CHECK(moved > 0);
}

// Order swaps any two members (swap_selected_party_members picks both rows): each ordered pair, twice.
TEST_CASE("Order keeps names with records and histories for every pair") {
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    w.io[0x60] = 0x80;
    Machine m(w);
    auto &names = Session::of(w).player_names();
    names.enabled = true;
    const PartyNameList tagged{"ANNALISE", "BARTHOLO", "CORDELIA", "DMITRIUS"};
    for (int from = 0; from < 4; ++from)
        for (int to = 0; to < 4; ++to) {
            CAPTURE(from);
            CAPTURE(to);
            names.values = tagged;
            for (int i = 0; i < 4; ++i)
                tag_member(w, i, uint8_t(0xA1 + i));
            for (int round = 0; round < 2; ++round) {
                w.ram[0xD840] = uint8_t(from);
                w.ram[0xD842] = uint8_t(to);
                m.ds() = m.ss() = 0;
                m.si() = uint16_t(0xD000 + from * 31);
                m.di() = uint16_t(0xD000 + to * 31);
                swap_party_records_core(m);
                for (int i = 0; i < 4; ++i) {
                    const int tag = member_tag(w.ram.data() + 0xD000, i);
                    REQUIRE(tag >= 0xA1);
                    REQUIRE(tag <= 0xA4);
                    CHECK(names.values[size_t(i)] == tagged[size_t(tag - 0xA1)]);
                }
                const int expected_tag = 0xA1 + (round == 0 ? to : from);
                CHECK(member_tag(w.ram.data() + 0xD000, from) == expected_tag);
            }
        }
}

TEST_CASE("explicitly empty host names survive text saves without changing their RAM fallback") {
    for (bool empty_slot : {false, true}) {
        std::vector<uint8_t> payload(SaveSlots::kPayload, 0);
        payload[0] = empty_slot ? 0xFF : 0;
        payload[0x435] = 0x1B;
        payload[0x436] = 0xE4;
        const PartyNameList names{};
        const auto text = save_to_text(payload, &names);
        REQUIRE(text.find("host_name = \"\"") != std::string::npos);
        std::vector<uint8_t> back;
        PartyNameList loaded;
        std::string error;
        REQUIRE_MESSAGE(save_from_text(text, &back, &error, &loaded), error);
        CHECK(loaded == names);
        CHECK(back == payload);
        CHECK(save_to_text(back, &loaded) == text);
        const auto legacy = save_to_text(payload);
        REQUIRE(save_from_text(legacy, &back, &error, &loaded));
        CHECK_FALSE(loaded[0].empty()); // legacy files still derive full names from their name field
        CHECK(back == payload);
        auto invalid = text;
        invalid.replace(invalid.find("host_name = \"\""), 14, "host_name = \"unexpected\"");
        CHECK_FALSE(save_from_text(invalid, &back, &error, &loaded));
        CHECK(error.find("host_name") != std::string::npos);
    }
}
