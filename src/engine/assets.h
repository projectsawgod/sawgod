// Byte archives (tools/assets.py archive / pack): entries overlay the ROM image at their offsets,
// including imported third-party patches. Named resources and ordinary mods use .sgz (engine/sgz.h,
// engine/mod_pack.h); the byte archive remains an import path for both native and interpreted execution.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sawgod {

using Sha1 = std::array<uint8_t, 20>;

Sha1 sha1(const uint8_t *data, size_t size);
std::string to_hex(const Sha1 &digest);

// SHA-1 of the supported ROM: Makai Toushi Sa-Ga (Japan).wsc
inline constexpr const char *kBaselineSha1 = "6b7a9b810c932974cbda4de7178cf0ea5a90e628";

struct AssetArchive {
    enum Kind : uint32_t { Base = 0, Override = 1 };
    struct Entry {
        std::string name;
        uint32_t rom_offset = 0;
        std::vector<uint8_t> data;
    };
    Sha1 rom_sha1{};
    Kind kind = Base;
    std::vector<Entry> entries;

    // Reads an archive; std::nullopt (and a message in *error) if it can't.
    static std::optional<AssetArchive> load(const std::string &path, std::string *error);
    const Entry *find(const std::string &name) const;
};

// Lays the archive's entries over the ROM image. Fails (message in *error) if the archive was built for a
// different ROM or an entry doesn't fit.
bool apply_archive(const AssetArchive &archive, std::vector<uint8_t> &rom, const Sha1 &rom_sha1,
                   std::string *error);

} // namespace sawgod
