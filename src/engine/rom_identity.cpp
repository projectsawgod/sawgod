#include "engine/assets.h"
#include "engine/rom_check.h"
#include <cstring>

namespace sawgod {
namespace {
// The Game Boy game: a GB header (the boot logo's first bytes at 0x104) titled "SAGA" (the Japanese release,
// its revisions and The Final Fantasy Legend all are).
bool is_gb_saga(const std::vector<uint8_t> &rom) {
    static const uint8_t kLogo[] = {0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B};
    if (rom.size() < 0x150 || std::memcmp(rom.data() + 0x104, kLogo, sizeof kLogo) != 0)
        return false;
    std::string title(reinterpret_cast<const char *>(rom.data() + 0x134), 16);
    title = title.substr(0, title.find('\0'));
    return title == "SAGA";
}

// This game's WSC footer (the last 16 bytes): publisher 0x28 (Square), color, game id 0x08.
bool is_this_wsc_game(const std::vector<uint8_t> &rom) {
    if (rom.size() < 0x10000)
        return false;
    const uint8_t *f = rom.data() + rom.size() - 16;
    return f[0] == 0xEA && f[6] == 0x28 && f[7] == 0x01 && f[8] == 0x08;
}

} // namespace
RomVerdict classify_rom(const std::vector<uint8_t> &rom, std::string *sha1_hex) {
    const std::string hex = to_hex(sha1(rom.data(), rom.size()));
    if (sha1_hex)
        *sha1_hex = hex;
    if (hex == kBaselineSha1)
        return RomVerdict::Ok;
    if (is_gb_saga(rom))
        return RomVerdict::GameBoy;
    if (is_this_wsc_game(rom))
        return RomVerdict::Modified;
    return RomVerdict::Unsupported;
}

} // namespace sawgod
