#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "game/save_file.h"
#include "game/save_slots.h"

using sawgod::game::SaveSlots;

namespace {
std::vector<uint8_t> Payload(uint32_t seed) {
    std::vector<uint8_t> p(SaveSlots::kPayload);
    uint32_t x = seed * 2654435761u + 1;
    for (uint8_t &b : p) {
        x = x * 1103515245u + 12345u;
        b = uint8_t(x >> 16);
    }
    p[0x435] = 0x1B;
    p[0x436] = 0xE4;
    return p;
}

std::string Replace(std::string s, const std::string &from, const std::string &to) {
    const size_t at = s.find(from);
    REQUIRE(at != std::string::npos);
    return s.replace(at, from.size(), to);
}
} // namespace

TEST_CASE("save file: fields cover the payload exactly") {
    std::string error;
    CHECK_MESSAGE(sawgod::game::save_fields_cover_payload(&error), error);
}

TEST_CASE("save file: any payload round-trips exactly") {
    for (uint32_t seed = 0; seed < 64; ++seed) {
        const std::vector<uint8_t> p = Payload(seed);
        const std::string text = sawgod::game::save_to_text(p);
        std::vector<uint8_t> back;
        std::string error;
        REQUIRE_MESSAGE(sawgod::game::save_from_text(text, &back, &error), error);
        CHECK(back == p);
    }
}

TEST_CASE("save file: hand edits load, mistakes are named") {
    std::vector<uint8_t> p = Payload(1);
    p[0xA2] = 0x10, p[0xA3] = p[0xA4] = p[0xA5] = 0; // gold 16
    std::string text = sawgod::game::save_to_text(p);
    std::vector<uint8_t> back;
    std::string error;

    REQUIRE(
        sawgod::game::save_from_text(Replace(text, "gold = 16", "gold = 0x186A0  # 100000"), &back, &error));
    CHECK((back[0xA2] | back[0xA3] << 8 | back[0xA4] << 16) == 100000);

    CHECK(!sawgod::game::save_from_text(Replace(text, "gold = 16", "gold = -1"), &back, &error));
    CHECK(error.find("party.gold") != std::string::npos);
    CHECK(!sawgod::game::save_from_text(Replace(text, "gold = 16", "gold = 16\ngolld = 3"), &back, &error));
    CHECK(error.find("golld") != std::string::npos);
    CHECK(!sawgod::game::save_from_text(Replace(text, "format = 2", "format = 3"), &back, &error));
    CHECK(error.find("format 3") != std::string::npos);
    CHECK(!sawgod::game::save_from_text(Replace(text, "[[member]]", "[[membr]]"), &back, &error));
    CHECK(!sawgod::game::save_from_text("format = 2\n[party]\ngold = \"x\n", &back, &error));
    CHECK(error.find("line 3") != std::string::npos);
}

TEST_CASE("save file: names are characters, shorter ones padded") {
    std::vector<uint8_t> p = Payload(2);
    const std::string text = sawgod::game::save_to_text(p);
    const size_t at = text.find("name = \"");
    REQUIRE(at != std::string::npos);
    const size_t end = text.find('"', at + 8);
    std::string edited = text;
    edited.replace(at, end - at + 1, "name = \"ア\"");
    std::vector<uint8_t> back;
    std::string error;
    REQUIRE_MESSAGE(sawgod::game::save_from_text(edited, &back, &error), error);
    CHECK(back[2] == back[4]); // padded with spaces
    // and it reads back as the same name
    CHECK(sawgod::game::save_to_text(back).find("name = \"ア\u3000\u3000\u3000\"") != std::string::npos);
}

TEST_CASE("save store: written, read back, imported from SRAM") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "sawgod_test_save_store";
    std::filesystem::remove_all(dir);
    std::map<std::string, std::string> written;
    auto write = [&](const std::string &path, const std::string &data, std::string *) {
        written[path] = data;
        std::FILE *f = std::fopen(path.c_str(), "wb");
        if (!f)
            return false;
        std::fwrite(data.data(), 1, data.size(), f);
        std::fclose(f);
        return true;
    };

    // a WSC SRAM image: slot 1's primary copy damaged (its backup loads), slot 3 valid, last used = 3
    std::vector<uint8_t> sram(0x8000, 0);
    auto put = [&](size_t at, const std::vector<uint8_t> &payload) {
        uint16_t sum = 0;
        for (size_t i = 0; i < payload.size(); ++i)
            sram[at + i] = payload[i], sum = uint16_t(sum + payload[i]);
        sram[at + payload.size()] = uint8_t(sum), sram[at + payload.size() + 1] = uint8_t(sum >> 8);
    };
    put(0x3301 + 0xCB1, Payload(10));
    put(0x3301, Payload(10));
    sram[0x3301 + 5] ^= 1;
    put(0x3301 + 2 * SaveSlots::kBlock, Payload(12));
    sram[0x3300] = 2;

    SaveSlots s;
    CHECK(!sawgod::game::read_save_store(dir.string(), s, nullptr)); // nothing there yet
    CHECK(s.store_attached());
    CHECK(sawgod::game::import_wsc_sram(sram, s) == 2);
    CHECK(s.payload(0) == Payload(10));
    CHECK(!s.has(1));
    CHECK(s.payload(2) == Payload(12));
    CHECK(s.last() == 2);
    std::string error;
    REQUIRE(sawgod::game::write_save_store(dir.string(), s, write, &error));
    CHECK(!s.dirty());
    CHECK(written.size() == 3); // slot1.toml, slot3.toml, store.toml

    SaveSlots t;
    std::vector<std::string> warnings;
    REQUIRE(sawgod::game::read_save_store(dir.string(), t, &warnings));
    CHECK(warnings.empty());
    CHECK(t.payload(0) == Payload(10));
    CHECK(t.payload(2) == Payload(12));
    CHECK(t.last() == 2);
    CHECK(!t.dirty());

    // a slot file that doesn't parse: reported, its slot empty and never written over
    write((dir / "slot1.toml").string(), "format = 2\nbroken", &error);
    SaveSlots u;
    REQUIRE(sawgod::game::read_save_store(dir.string(), u, &warnings));
    CHECK(warnings.size() == 1);
    CHECK(!u.has(0));
    u.store(0, Payload(3));
    CHECK(!u.slot_dirty(0));
    std::filesystem::remove_all(dir);
}
