// Checking a file the player offers as the ROM (first-run ROM extraction): a .wsc, or a .zip/.7z
// holding one. Only the exact supported dump is accepted (SHA-1, assets.h
// kBaselineSha1); anything else gets a verdict that says what it most likely is, for the ROM screen.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sawgod {

enum class RomVerdict {
    Ok,             // the supported ROM
    GameBoy,        // Makai Toushi SaGa / The Final Fantasy Legend for the Game Boy
    Modified,       // this game's WSC header, but not the supported dump (patched, translated, bad dump)
    Unsupported,    // something else
    NoRomInArchive, // a .zip/.7z with nothing ROM-like inside
    Unreadable,     // can't be opened, or a broken archive
};

struct RomCheck {
    RomVerdict verdict = RomVerdict::Unreadable;
    std::vector<uint8_t> rom; // the ROM's bytes (Ok only)
    std::string name;         // the ROM's file name: the file's own, or the entry's inside an archive
    std::string sha1;         // hex SHA-1 of the best candidate examined ("" if none)
};

// Checks the bytes of a file named `name` (archives by their content, not their extension).
RomCheck check_rom_bytes(const std::string &name, const std::vector<uint8_t> &data);

// Reads and checks a file.
RomCheck check_rom_file(const std::string &path);

// The verdict for one candidate ROM image (no archive handling).
RomVerdict classify_rom(const std::vector<uint8_t> &rom, std::string *sha1_hex = nullptr);

// The entries of a 7z archive (engine/archive_7z.cpp); false when it isn't one or can't be decoded.
struct ArchiveEntry {
    std::string name;
    std::vector<uint8_t> data;
};
bool read_7z(const std::vector<uint8_t> &data, std::vector<ArchiveEntry> *entries);

} // namespace sawgod
