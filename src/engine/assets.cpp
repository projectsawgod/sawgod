#include "engine/assets.h"

#include <cstdio>
#include <cstring>

namespace sawgod {

namespace {

std::vector<uint8_t> read_file(const std::string &path) {
    std::vector<uint8_t> data;
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
        return data;
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
        data.insert(data.end(), buf, buf + n);
    std::fclose(f);
    return data;
}

} // namespace

std::optional<AssetArchive> AssetArchive::load(const std::string &path, std::string *error) {
    std::vector<uint8_t> d = read_file(path);
    size_t pos = 0;
    auto need = [&](size_t n) { return pos + n <= d.size(); };
    auto u32 = [&]() {
        uint32_t v = d[pos] | d[pos + 1] << 8 | d[pos + 2] << 16 | uint32_t(d[pos + 3]) << 24;
        pos += 4;
        return v;
    };
    if (d.size() < 36 || std::memcmp(d.data(), "GSAWPAK1", 8) != 0) {
        if (error)
            *error = path + ": not a SawGod asset archive";
        return std::nullopt;
    }
    AssetArchive a;
    std::memcpy(a.rom_sha1.data(), d.data() + 8, 20);
    pos = 28;
    a.kind = Kind(u32());
    uint32_t count = u32();
    for (uint32_t i = 0; i < count; ++i) {
        if (!need(2))
            break;
        size_t n = d[pos] | d[pos + 1] << 8;
        pos += 2;
        if (!need(n + 8))
            break;
        Entry e;
        e.name.assign(reinterpret_cast<const char *>(d.data() + pos), n);
        pos += n;
        e.rom_offset = u32();
        uint32_t size = u32();
        if (!need(size))
            break;
        e.data.assign(d.begin() + long(pos), d.begin() + long(pos + size));
        pos += size;
        a.entries.push_back(std::move(e));
    }
    if (a.entries.size() != count) {
        if (error)
            *error = path + ": truncated archive";
        return std::nullopt;
    }
    return a;
}

const AssetArchive::Entry *AssetArchive::find(const std::string &name) const {
    for (const Entry &e : entries)
        if (e.name == name)
            return &e;
    return nullptr;
}

bool apply_archive(const AssetArchive &archive, std::vector<uint8_t> &rom, const Sha1 &rom_sha1,
                   std::string *error) {
    if (archive.rom_sha1 != rom_sha1) {
        if (error)
            *error = "archive is for ROM " + to_hex(archive.rom_sha1) + ", this ROM is " + to_hex(rom_sha1);
        return false;
    }
    for (const auto &e : archive.entries) {
        if (size_t(e.rom_offset) + e.data.size() > rom.size()) {
            if (error)
                *error = "asset " + e.name + " doesn't fit the ROM";
            return false;
        }
    }
    for (const auto &e : archive.entries)
        std::copy(e.data.begin(), e.data.end(), rom.begin() + e.rom_offset);
    return true;
}

} // namespace sawgod
