#include <doctest/doctest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "engine/persist.h"

namespace fs = std::filesystem;

namespace {

std::vector<uint8_t> Slurp(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// A fresh scratch directory per test case, removed afterwards.
struct Scratch {
    fs::path dir;
    explicit Scratch(const char *name)
        : dir(fs::temp_directory_path() / ("sawgod_test_persist_" + std::string(name))) {
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

int g_writes = 0;
bool g_fail = false;
std::vector<uint8_t> g_last;
bool FakeWriter(const std::string &, const std::vector<uint8_t> &data, std::string *error) {
    if (g_fail) {
        if (error)
            *error = "disk full";
        return false;
    }
    ++g_writes;
    g_last = data;
    return true;
}

} // namespace

TEST_CASE("WriteFileAtomic writes the exact bytes and replaces an existing file") {
    Scratch s("replace");
    const std::string path = (s.dir / "game.sav").string();
    const std::vector<uint8_t> a(32768, 0x11), b{1, 2, 3, 0, 255};
    std::string error;
    REQUIRE(sawgod::WriteFileAtomic(path, a, &error));
    CHECK((Slurp(path) == a));
    REQUIRE(sawgod::WriteFileAtomic(path, b, &error));
    CHECK((Slurp(path) == b));
    CHECK(!fs::exists(sawgod::AtomicTempPath(path)));
}

TEST_CASE("WriteFileAtomic keeps the previous file when the temporary can't be created") {
    Scratch s("tmpfail");
    const std::string path = (s.dir / "game.sav").string();
    const std::vector<uint8_t> good(64, 0x5A);
    REQUIRE(sawgod::WriteFileAtomic(path, good));
    fs::create_directory(sawgod::AtomicTempPath(path)); // a directory where the temporary file would go
    std::string error;
    CHECK(!sawgod::WriteFileAtomic(path, std::vector<uint8_t>(64, 0), &error));
    CHECK(!error.empty());
    CHECK((Slurp(path) == good));
}

TEST_CASE("WriteFileAtomic leaves another running copy's temporary alone") {
    Scratch s("othercopy");
    const std::string path = (s.dir / "game.sav").string();
    const std::string other = path + ".1.tmp"; // the temporary of a copy with another process id
    REQUIRE(other != sawgod::AtomicTempPath(path));
    const std::vector<uint8_t> theirs(64, 0x22), ours(64, 0x33);
    REQUIRE(sawgod::WriteFileAtomic(other, theirs));
    REQUIRE(sawgod::WriteFileAtomic(path, ours));
    CHECK((Slurp(other) == theirs));
    CHECK((Slurp(path) == ours));
}

TEST_CASE("WriteFileAtomic keeps the previous state when the replace fails") {
    Scratch s("replacefail");
    const fs::path dest = s.dir / "game.sav";
    fs::create_directories(dest / "inside"); // a non-empty directory can't be replaced by a file
    std::string error;
    CHECK(!sawgod::WriteFileAtomic(dest.string(), std::vector<uint8_t>(8, 1), &error));
    CHECK(!error.empty());
    CHECK(fs::is_directory(dest / "inside"));
    CHECK(!fs::exists(sawgod::AtomicTempPath(dest.string()))); // cleaned up
}

TEST_CASE("WriteFileAtomic reports a missing directory") {
    Scratch s("nodir");
    std::string error;
    CHECK(!sawgod::WriteFileAtomic((s.dir / "no" / "such" / "game.sav").string(), std::vector<uint8_t>(4),
                                   &error));
    CHECK(error.find("cannot create") != std::string::npos);
}

TEST_CASE("SaveFlusher writes once the save area settles, ignores work memory, and retries after a failure") {
    g_writes = 0;
    g_fail = false;
    std::vector<uint8_t> sram(0x8000, 0);
    sawgod::SaveFlusher f("x.sav", 0x3300, 0x4C63, 5, &FakeWriter);
    f.reset(sram);
    for (int i = 0; i < 100; ++i) { // work memory below the save area changes every frame: no write
        sram[0x1E69] = uint8_t(i);
        CHECK(f.frame(sram));
    }
    CHECK(g_writes == 0);

    sram[0x3301] = 7; // a save: primary copy, then the backup two frames later
    CHECK(f.frame(sram));
    CHECK(f.frame(sram));
    sram[0x3FB2] = 7;
    for (int i = 0; i < 5; ++i) // the change frame, then 4 unchanged ones
        CHECK(f.frame(sram));
    CHECK(g_writes == 0); // not settled yet
    CHECK(f.frame(sram));
    CHECK(g_writes == 1);
    CHECK((g_last == sram)); // the whole image, as is
    for (int i = 0; i < 50; ++i)
        CHECK(f.frame(sram));
    CHECK(g_writes == 1); // nothing new

    g_fail = true;
    sram[0x3302] = 9;
    std::string error;
    bool failed = false;
    for (int i = 0; i < 10; ++i)
        failed |= !f.frame(sram, &error);
    CHECK(failed);
    CHECK(error == "disk full");
    g_fail = false;
    for (int i = 0; i < 10; ++i)
        CHECK(f.frame(sram));
    CHECK(g_writes == 1); // no retry every frame...
    CHECK(f.flush(sram)); // ...but the exit flush writes it
    CHECK(g_writes == 2);
    CHECK((g_last == sram));
}

TEST_CASE("SaveFlusher bounds persistence while all cartridge SRAM keeps changing") {
    g_writes = 0;
    g_fail = false;
    std::vector<uint8_t> sram(32, 0);
    sawgod::SaveFlusher f("x.sav", 0, sram.size(), 5, &FakeWriter, 3);
    f.reset(sram);
    for (int frame = 0; frame < 9; ++frame) {
        sram[0] = uint8_t(frame + 1);
        REQUIRE(f.frame(sram));
        CHECK(f.writes() == (frame + 1) / 3);
    }
    CHECK(g_writes == 3);
    CHECK(g_last == sram);
    for (int frame = 0; frame < 10; ++frame)
        REQUIRE(f.frame(sram));
    CHECK(g_writes == 3);
    sram.back() = 42;
    REQUIRE(f.frame(sram));
    REQUIRE(f.flush(sram));
    CHECK(g_writes == 4);
}
