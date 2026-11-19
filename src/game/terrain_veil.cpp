#include "game/terrain_veil.h"

namespace sawgod::game {
namespace {
uint32_t rgb(uint16_t c) {
    return uint32_t((c >> 8 & 15) * 17) << 16 | uint32_t((c >> 4 & 15) * 17) << 8 | uint32_t((c & 15) * 17);
}
// A sprite tile's 4bpp palette index at (x, y), honoring the word's flips.
int tile_index(const emu::Wsc &w, int word, int x, int y) {
    const int sx = word & 0x4000 ? 7 - x : x, sy = word & 0x8000 ? 7 - y : y;
    const int byte = w.ram[size_t(0x4000 + (word & 511) * 32 + sy * 4 + sx / 2)];
    return sx & 1 ? byte & 15 : byte >> 4;
}
} // namespace

std::vector<VeiledCard> veiled_terrain_cards(const emu::Wsc &w) {
    std::vector<VeiledCard> result;
    const auto oam = w.presentation_sprites();
    for (const auto &record : w.presentation_terrain_cards()) {
        if (!record.veiled || record.first < 0)
            continue;
        const size_t at = size_t(record.first) * 4;
        if (at + 7 >= oam.size() || (oam[at] | oam[at + 1] << 8) != record.word)
            continue;
        // Both top cells must still belong to this card. A later NPC can overwrite an unreserved card.
        if ((oam[at + 4] | oam[at + 5] << 8) != record.word + 1 || oam[at + 6] != oam[at + 2] ||
            oam[at + 7] != uint8_t(oam[at + 3] + 8))
            continue;
        VeiledCard card;
        card.first = record.first;
        card.x = oam[at + 3] >= 240 ? oam[at + 3] - 256 : oam[at + 3];
        card.y = oam[at + 2] >= 240 ? oam[at + 2] - 256 : oam[at + 2];
        // Both native writers increment the top-left word for the following quadrants.
        for (int q = 0; q < 4; ++q)
            card.words[size_t(q)] = uint16_t(record.word + q);
        result.push_back(card);
    }
    return result;
}

void blend_terrain_veil(emu::Wsc &live, std::vector<uint32_t> &rgb_picture,
                        const std::array<uint8_t, emu::Wsc::kWidth * emu::Wsc::kHeight> *ui,
                        emu::Wsc *raster_inputs) {
    auto &w = raster_inputs ? *raster_inputs : live;
    const auto *layers = live.frame_layers();
    if (!layers || rgb_picture.size() != size_t(emu::FrameLayers::Pixels))
        return;
    for (const auto &card : veiled_terrain_cards(w))
        for (int q = 2; q < 4; ++q) {
            const int word = card.words[size_t(q)];
            const int ox = card.x + q % 2 * 8, oy = card.y + 8;
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) {
                    const int sx = ox + x, sy = oy + y;
                    if (sx < 0 || sy < 0 || sx >= emu::FrameLayers::Width || sy >= emu::FrameLayers::Height)
                        continue;
                    const size_t at = size_t(sy * emu::FrameLayers::Width + sx);
                    if ((ui && (*ui)[at]) || !layers->valid[size_t(sy)])
                        continue;
                    const int index = tile_index(w, word, x, y);
                    if (!index)
                        continue;
                    // Beneath: the topmost layer pixel that is not one of this card's own sprite tiles.
                    uint16_t below = layers->planes[emu::FrameLayers::Backdrop][at].color;
                    for (int i = emu::FrameLayers::SpriteFront; i > emu::FrameLayers::Backdrop; --i) {
                        const auto &p = layers->planes[size_t(i)][at];
                        if (!(p.flags & emu::RasterPixel::Present))
                            continue;
                        const bool sprite =
                            i == emu::FrameLayers::SpriteFront || i == emu::FrameLayers::SpriteBehind;
                        // The completed raster can use the preceding DMA buffer, while the OAM
                        // latch already holds the next one. Both four-tile buffers belong to this
                        // walker; excluding only the current words would blend over its opaque feet.
                        if (sprite && p.tile / 8 == (card.words[0] & 511) / 8)
                            continue;
                        below = p.color;
                        break;
                    }
                    const uint32_t a =
                        rgb(w.presentation_palette_color(8 + (word >> 9 & 7), index, false, true));
                    const uint32_t b = rgb(below);
                    rgb_picture[at] = ((a & 0xFEFEFE) >> 1) + ((b & 0xFEFEFE) >> 1);
                }
        }
}

} // namespace sawgod::game
