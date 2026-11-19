// See-through terrain for field walkers (presentation only). Tile flag 0x20 lists their bottom half
// on alternate frames: the WSC LCD blended this, but a modern display shows hard flicker. Rebuild the
// missing bottom words from the recorded card and draw them steadily at half opacity.
#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "emu/wsc.h"

namespace sawgod::game {
struct VeiledCard {
    std::array<int, 4> words{}; // top left, top right, bottom left, bottom right
    int first = 0;              // first entry in the latched presentation OAM
    int x = 0, y = 0;
};
// Only cards still present in the latched OAM qualify. Records for hidden, overwritten or stale cards
// cannot resurrect them. The two lower words need not be in OAM on this frame.
std::vector<VeiledCard> veiled_terrain_cards(const emu::Wsc &w);

// WSC and flat Enhanced: blend each lower body 50% over the topmost layer beneath that card.
// Host UI coverage (`ui`, may be null) is kept. Does not alter machine memory or its framebuffer.
// Geometry diagnostics supply the retained raster inputs; the flat WSC path keeps its existing inputs.
void blend_terrain_veil(emu::Wsc &w, std::vector<uint32_t> &rgb,
                        const std::array<uint8_t, emu::Wsc::kWidth * emu::Wsc::kHeight> *ui,
                        emu::Wsc *raster_inputs = nullptr);
} // namespace sawgod::game
