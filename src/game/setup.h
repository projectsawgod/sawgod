// The original cartridge configuration, shared by the desktop decomp and its replay mode.
#pragma once
#include "emu/wsc.h"
#include <memory>
#include <span>

namespace sawgod::game {
size_t cartridge_sram_size(std::span<const uint8_t> rom);
std::unique_ptr<emu::Wsc> make_decomp_game(std::vector<uint8_t> rom);
inline constexpr uint64_t kWscFrameNs = uint64_t(1e9 * 159 * 256 / 3072000.0);
} // namespace sawgod::game
