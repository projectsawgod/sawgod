#include "game/setup.h"
#include "game/machine.h"

namespace sawgod::game {
size_t cartridge_sram_size(std::span<const uint8_t> rom) {
    static constexpr size_t sizes[] = {0, 0x2000, 0x8000, 0x20000, 0x40000, 0x80000};
    if (rom.size() < 16)
        return 0;
    const auto type = rom[rom.size() - 16 + 0x0B];
    return type < std::size(sizes) ? sizes[type] : 0;
}
std::unique_ptr<emu::Wsc> make_decomp_game(std::vector<uint8_t> rom) {
    const auto bytes = cartridge_sram_size(rom);
    auto w = std::make_unique<emu::Wsc>(std::move(rom), bytes);
    install(*w); // Every routine; no subscribers, resources, save store or port policies.
    return w;
}
} // namespace sawgod::game
