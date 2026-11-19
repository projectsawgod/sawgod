#pragma once

// First-run building blocks (first-run ROM extraction): the data folder choice, .sgz archives and
// their stamps, and the ROM checks behind the ROM screen.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

#include <miniz.h>

#include "app/first_run.h"
#include "engine/assets.h"
#include "engine/data_dir.h"
#include "engine/rom_check.h"
#include "engine/sgz.h"

namespace fs = std::filesystem;

namespace {

inline std::vector<uint8_t> read(const fs::path &p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

inline std::vector<uint8_t>
zip_of(std::initializer_list<std::pair<std::string, std::vector<uint8_t>>> files) {
    mz_zip_archive z{};
    REQUIRE(mz_zip_writer_init_heap(&z, 0, 0));
    for (const auto &[name, data] : files)
        REQUIRE(mz_zip_writer_add_mem(&z, name.c_str(), data.data(), data.size(), MZ_DEFAULT_COMPRESSION));
    void *buf = nullptr;
    size_t size = 0;
    REQUIRE(mz_zip_writer_finalize_heap_archive(&z, &buf, &size));
    std::vector<uint8_t> out(static_cast<uint8_t *>(buf), static_cast<uint8_t *>(buf) + size);
    mz_zip_writer_end(&z);
    return out;
}

// A stand-in with this game's WSC footer and nothing else: the "modified" verdict without ROM content.
inline std::vector<uint8_t> fake_wsc() {
    std::vector<uint8_t> d(0x10000, 0);
    const uint8_t footer[16] = {0xEA, 0, 0, 0, 0, 0, 0x28, 0x01, 0x08, 0, 0, 0, 0, 0, 0, 0};
    std::copy(std::begin(footer), std::end(footer), d.end() - 16);
    return d;
}

// A stand-in Game Boy SaGa header: the boot logo's start and the title, nothing else.
inline std::vector<uint8_t> fake_gb() {
    std::vector<uint8_t> d(0x40000, 0);
    const uint8_t logo[] = {0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B};
    std::copy(std::begin(logo), std::end(logo), d.begin() + 0x104);
    const char title[] = "SAGA";
    std::copy(title, title + 4, d.begin() + 0x134);
    return d;
}

inline const char *rom_path() { return std::getenv("SAWGOD_WSC_ROM"); }

} // namespace

// The GB layer against a reference made by the Python tools (the GB skin, the GB-map installation):
// SAWGOD_GB_REFERENCE=DIR (holding gb/...) compares every file the C++ extraction writes there -- PNGs by
// pixel, CSVs by text. Local only: the reference is ROM-derived and never committed.
#include <map>

#include "engine/indexed_png.h"
#include "game/gb_extract.h"

#include "app/rom_screen.h"
#include "emu/png.h"

#include "app/splash.h"

#include "app/first_run.h"
