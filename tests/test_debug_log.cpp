#include "app/debug_watch.h"
#include "engine/debug_log.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using namespace sawgod;

namespace {
struct Folder {
    fs::path path;
    explicit Folder(const char *name) : path(fs::temp_directory_path() / name) {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~Folder() {
        debug_log::stop();
        fs::remove_all(path);
    }
};
std::string read(const fs::path &p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}
int count(const std::string &text, const std::string &word) {
    int n = 0;
    for (size_t at = 0; (at = text.find(word, at)) != std::string::npos; at += word.size())
        ++n;
    return n;
}
int files(const fs::path &folder) {
    return int(std::distance(fs::directory_iterator(folder), fs::directory_iterator()));
}
} // namespace

TEST_CASE("debug log: severity, area and context on a line; a repeat is counted, not written") {
    Folder f("sawgod-debug-log-lines");
    REQUIRE(debug_log::start(f.path, "1.2.3"));
    const auto path = debug_log::current_file();
    debug_log::set_context("map 1, GB, shop");
    for (int i = 0; i < 1000; ++i)
        debug_log::warning("gfx", "the same thing");
    debug_log::error("save", "another thing");
    debug_log::stop();
    const auto text = read(path);
    CHECK(text.find("1.2.3, ") == 0);
    CHECK(text.find("WARNING gfx: the same thing [map 1, GB, shop]") != std::string::npos);
    CHECK(text.find("ERROR save: another thing") != std::string::npos);
    CHECK(count(text, "the same thing") == 2); // once as it happened, once in the closing count
    CHECK(text.find("repeated 1000 times: WARNING gfx: the same thing") != std::string::npos);
    CHECK(text.find("session end: 0 critical, 1 errors, 1000 warnings") != std::string::npos);
}

TEST_CASE("debug log: a file stops at its limit, but a critical still fits and the end is always written") {
    Folder f("sawgod-debug-log-limit");
    debug_log::Limits limits;
    limits.file_bytes = 16 * 1024;
    REQUIRE(debug_log::start(f.path, "1", limits));
    const auto path = debug_log::current_file();
    for (int i = 0; i < 100000; ++i)
        debug_log::warning("flood", "message number " + std::to_string(i));
    debug_log::critical("save", "the save is gone");
    debug_log::stop();
    CHECK(fs::file_size(path) < 1024 * 1024); // the closing counts of what was written, at most
    const auto text = read(path);
    CHECK(text.find("log full") != std::string::npos);
    CHECK(text.find("CRITICAL save: the save is gone") != std::string::npos);
    CHECK(text.find("session end: 1 critical, 0 errors, 100000 warnings") != std::string::npos);
}

TEST_CASE("debug log: keeps the newest files and reports a session that never ended") {
    Folder f("sawgod-debug-log-files");
    debug_log::Limits limits;
    limits.files_kept = 3;
    for (int i = 0; i < 6; ++i) {
        REQUIRE(debug_log::start(f.path, "1", limits));
        debug_log::stop();
    }
    CHECK(files(f.path) == 3);
    // A session killed before its closing line: the next one says so, and the one after that doesn't.
    REQUIRE(debug_log::start(f.path, "1", limits));
    const auto killed = debug_log::current_file();
    debug_log::stop();
    std::string text = read(killed);
    text.erase(text.find("session end:"));
    std::ofstream(killed, std::ios::binary) << text;
    REQUIRE(debug_log::start(f.path, "1", limits));
    const auto next = debug_log::current_file();
    debug_log::stop();
    CHECK(read(next).find("ERROR session: the previous session never ended") != std::string::npos);
    CHECK(read(next).find(killed.filename().string()) != std::string::npos);
    REQUIRE(debug_log::start(f.path, "1", limits));
    const auto after = debug_log::current_file();
    debug_log::stop();
    CHECK(read(after).find("never ended") == std::string::npos);
    CHECK(files(f.path) == 3);
    limits.files_kept = 0; // the test period's folder: none are deleted
    for (int i = 0; i < 4; ++i) {
        REQUIRE(debug_log::start(f.path, "1", limits));
        debug_log::stop();
    }
    CHECK(files(f.path) == 7);
}

TEST_CASE("debug log: a crash report names the context and closes the session") {
    Folder f("sawgod-debug-log-crash");
    REQUIRE(debug_log::start(f.path, "1"));
    const auto path = debug_log::current_file();
    debug_log::set_context("map 88, Enhanced, field");
    debug_log::write_crash_for_test("invalid memory access (SIGSEGV)");
    const auto text = read(path);
    CHECK(text.find("CRITICAL crash: invalid memory access (SIGSEGV) [map 88, Enhanced, field]") !=
          std::string::npos);
    CHECK(text.find("session end: crashed") != std::string::npos);
}

TEST_CASE("debug watch: stalls, a stuck black picture and a stuck handoff are each reported once") {
    Folder f("sawgod-debug-watch");
    REQUIRE(debug_log::start(f.path, "1"));
    const auto path = debug_log::current_file();
    app::DebugWatch watch;
    uint64_t now = 1, frame = 1;
    const auto play = [&](uint64_t frames, bool black, bool holding) {
        for (uint64_t i = 0; i < frames; ++i)
            watch.frame({now += 13'000'000, ++frame, true, black, holding});
    };
    play(app::DebugWatch::kBlackFrames - 2, true, false); // a long fade is no problem
    play(10, false, false);
    play(app::DebugWatch::kHeldFrames - 2, false, true);
    play(10, false, false);
    debug_log::stop();
    CHECK(read(path).find("picture") == std::string::npos);

    REQUIRE(debug_log::start(f.path, "1"));
    const auto second = debug_log::current_file();
    play(app::DebugWatch::kBlackFrames * 2, true, false);
    play(app::DebugWatch::kHeldFrames * 2, false, true);
    watch.frame({now += 5'000'000'000, ++frame, true, false, false});
    watch.frame({now += 50'000'000'000, ++frame, false, false, false}); // the window was away
    watch.frame({now += 50'000'000'000, ++frame, true, false, false});
    debug_log::stop();
    const auto text = read(second);
    CHECK(count(text, "ERROR picture: the picture has been black") == 1);
    CHECK(count(text, "ERROR picture: a screen handoff") == 1);
    CHECK(count(text, "WARNING stall: a frame took") == 1);
}
