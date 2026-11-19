#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <miniz.h>

#include "engine/mod_pack.h"

namespace fs = std::filesystem;

namespace {
void write(const fs::path &p, const std::string &s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << s;
}
void zip(const fs::path &out, std::initializer_list<std::pair<const char *, std::string>> files) {
    mz_zip_archive z{};
    REQUIRE(mz_zip_writer_init_file(&z, out.string().c_str(), 0));
    for (const auto &[name, data] : files)
        REQUIRE(mz_zip_writer_add_mem(&z, name, data.data(), data.size(), MZ_DEFAULT_COMPRESSION));
    REQUIRE(mz_zip_writer_finalize_archive(&z));
    mz_zip_writer_end(&z);
}
} // namespace

TEST_CASE("mods: folders and .sgz files (flat or one top folder), named by mod.toml, loaded in order") {
    const fs::path dir = fs::temp_directory_path() / "sawgod_test_mods";
    fs::remove_all(dir);
    write(dir / "folder_mod" / "tables" / "items.csv", "id,price\n0,1\n");
    write(dir / "folder_mod" / "mod.toml", "# a mod\nname = \"Cheap\"\nversion = \"1\"\n");
    zip(dir / "flat.sgz", {{"tables/shops.csv", "id,items\n0,1 2\n"}});
    zip(dir / "nested.sgz", {{"nested/mod.toml", "name = \"Nested\"\n"}, {"nested/tables/maps.csv", "id\n"}});

    std::string error;
    auto f = sawgod::ModPack::open((dir / "folder_mod").string(), &error);
    REQUIRE(f);
    CHECK(f->name() == "Cheap");
    CHECK(f->read("tables/items.csv").value() ==
          std::vector<uint8_t>{'i', 'd', ',', 'p', 'r', 'i', 'c', 'e', '\n', '0', ',', '1', '\n'});
    CHECK(f->files() == std::vector<std::string>{"mod.toml", "tables/items.csv"});
    CHECK(!f->read("tables/shops.csv"));

    auto z = sawgod::ModPack::open((dir / "flat.sgz").string(), &error);
    REQUIRE(z);
    CHECK(z->name() == "flat");
    CHECK(z->read("tables/shops.csv").has_value());

    auto n = sawgod::ModPack::open((dir / "nested.sgz").string(), &error);
    REQUIRE(n);
    CHECK(n->name() == "Nested");
    CHECK(n->read("tables/maps.csv").has_value());
    CHECK(n->files() == std::vector<std::string>{"mod.toml", "tables/maps.csv"});

    write(dir / "bad.sgz", "not a zip");
    CHECK(!sawgod::ModPack::open((dir / "bad.sgz").string(), &error));
    CHECK(error.find("bad.sgz") != std::string::npos);

    // --mod paths first, then the mods folder's entries by name; a bad one is reported, the rest load
    std::vector<std::string> errors;
    auto mods = sawgod::open_mods({(dir / "nested.sgz").string()}, dir.string(), &errors);
    std::vector<std::string> names;
    for (const auto &m : mods)
        names.push_back(m->name());
    CHECK(names == std::vector<std::string>{"Nested", "flat", "Cheap", "Nested"});
    CHECK(errors.size() == 1);
    fs::remove_all(dir);
}
