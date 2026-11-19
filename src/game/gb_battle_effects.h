// GB battle-effect frame data and extraction entry point.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>
namespace sawgod::game {
// Original GB display commands, sampled on first run. Coordinates are native pixels;
// rows describe the original 160x64 enemy field. Art is an extracted transparent atlas.
struct GbEffectRow {
    int dx = 0, dy = 0, fill = -1;
    std::array<uint8_t, 4> colors{0, 1, 2, 3};
};
struct GbEffectFrame {
    std::array<GbEffectRow, 64> rows;
    std::array<uint8_t, 224 * 64> sprites{}; // 0 transparent, 1..4 GB shades
};
struct GbBattleEffect {
    std::vector<GbEffectFrame> frames;
};
// Uses the original 3F6186 -> 3F8328 -> 3F8B1E effect interpreter as an extraction codec.
// No native machine state is involved; no original pixels or code are shipped. `progress` (0..1, optional) is
// called as the replay and the effects complete: this is most of the first run's work (several seconds).
std::map<std::string, std::vector<uint8_t>>
extract_gb_battle_effects(const std::vector<uint8_t> &rom, const std::function<void(float)> &progress = {});
} // namespace sawgod::game
