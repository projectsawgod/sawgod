#include "tools/field_preview.h"
#include "game/bank35_field.h"
#include "game/bank35_world.h"
#include "game/machine.h"
#include <algorithm>

namespace sawgod::tools {
bool prepare_field_preview(emu::Wsc &w, int id, const game::MapData &map, int x, int y) {
    if (w.rom.size() != 0x400000 || id < 1 || id >= game::kMapCount || x < 0 || x > 50 || y < 0 || y > 55)
        return false;
    w.set_second(false);
    w.ram.fill(0);
    for (int slot = 0; slot < 16; ++slot)
        w.ram[size_t(0xD5AE + slot * 20)] = 255;
    w.sram.assign(0x10000, 0);
    w.io.fill(0);
    w.cpu.s.halted = true;
    w.cpu.s.flags = 0;
    w.scanline = w.line_cycle = 0;
    w.io[0x16] = 158;
    w.io[0x60] = 0xE0; // color, 4bpp packed
    w.io[0] = 1;       // screen 1 only; no actors or host windows in this fixture
    w.io[7] = 2;       // screen 1 at 1000
    w.ram[0xD0DC] = uint8_t(id);
    // Match the native loaded-map identity and field-frame wait for state-based camera eligibility.
    std::copy_n(w.rom.begin() + 0x35A830 + id * 18 + 1, 9, w.ram.begin() + 0xD52D);
    w.ram[0xC813] = 1;
    std::copy(map.cells.begin(), map.cells.end(), w.sram.begin());
    std::copy(map.attributes.begin(), map.attributes.end(), w.sram.begin() + 0x2100);
    const auto word = [&](uint32_t at) { return uint16_t(w.rom[at] | w.rom[at + 1] << 8); };
    const auto put = [&](uint16_t at, uint16_t v) {
        w.ram[at] = uint8_t(v);
        w.ram[uint16_t(at + 1)] = uint8_t(v >> 8);
    };
    put(0xD59D, uint16_t(x * 16));
    put(0xD59F, uint16_t(y * 16));
    const int set = w.rom[0x35A830 + unsigned(id) * 18];
    const uint32_t row = 0x35B718 + unsigned(set) * 8;
    const uint32_t bank = uint32_t(set >> 2) << 16;
    const uint16_t count = word(row), tiles = word(row + 2), palettes = word(row + 4),
                   attributes = word(row + 6);
    if (count > 512)
        return false;
    for (unsigned i = 0; i < unsigned(count) * 32; ++i)
        w.ram[0x8000 + i] = w.rom[bank + uint16_t(tiles + i)];
    for (unsigned i = 0; i < 128; ++i)
        w.ram[0xFE00 + i] = w.rom[0x0F0000 + uint16_t(palettes + i)];
    constexpr int quadrant[4][4] = {{0, 1, 2, 3}, {1, 0, 3, 2}, {2, 3, 0, 1}, {3, 2, 1, 0}};
    for (int t = 0; t < 256; ++t)
        for (int q = 0; q < 4; ++q) {
            const int base = map.tiles[size_t(t)], mirror = map.mirror[size_t(t)];
            const unsigned source = unsigned(base * 4 + quadrant[mirror][q]);
            const int palette = w.rom[0x0F0000 + uint16_t(attributes + source)];
            const uint16_t value = uint16_t(0x2000 | source | palette << 9 | mirror << 14);
            const size_t at = size_t(0x1900 + t * 8 + q * 2);
            w.sram[at] = uint8_t(value);
            w.sram[at + 1] = uint8_t(value >> 8);
        }
    for (int cy = 0; cy < 32; ++cy)
        for (int cx = 0; cx < 32; ++cx) {
            const int mx = (x + cx / 2) % 64, my = (y + cy / 2) % 64;
            const int t = map.cells[size_t((my + 8) * 80 + mx + 8)];
            const unsigned at = 0x1900u + unsigned(t * 8 + cy % 2 * 4 + cx % 2 * 2);
            put(uint16_t(0x1000 + (cy * 32 + cx) * 2), uint16_t(w.sram[at] | w.sram[at + 1] << 8));
        }
    if (game::Machine::field_cell_hook) {
        game::Machine m(w);
        for (int cy = 0; cy < 16; ++cy)
            for (int cx = 0; cx < 16; ++cx) {
                const int mx = (x + cx) % 64, my = (y + cy) % 64;
                game::Machine::field_cell_hook(m, uint16_t(0x1000 + cy * 128 + cx * 4),
                                               map.cells[size_t((my + 8) * 80 + mx + 8)],
                                               map.attributes[size_t(my * 64 + mx)], mx, my);
            }
    }
    return true;
}
void animate_field_preview(emu::Wsc &w, int frame) {
    // Map 88/102 water animates by the game's six-frame palette callback, not fake UV motion.
    const int map = w.ram[0xD0DC];
    if (map != 88 && map != 102)
        return;
    const int set = w.rom[size_t(0x35A830 + map * 18)];
    const size_t row = size_t(0x35B718 + set * 8 + 4);
    w.ram[0xD538] = w.rom[row];
    w.ram[0xD539] = w.rom[row + 1];
    std::copy_n(w.ram.begin() + 0xFE00, 128, w.ram.begin() + 0xC400);
    w.ram[0xD51B] = 1;
    game::Machine m(w);
    m.ss() = 0;
    m.cs() = 0x5000;
    for (int i = 0; i <= frame; ++i)
        game::update_map_frame_effect_core(m);
    std::copy_n(w.ram.begin() + 0xC400, 128, w.ram.begin() + 0xFE00);
}
void prepare_field_party(emu::Wsc &w, int pose, int type) {
    // Fixture state only: use the game's own art loader, pose decoder and OAM writer.
    w.io[0xC0] = 0xFF;
    w.ram[0xD005] = uint8_t(type);
    w.ram[0xC810] = 1; // face north (native direction 1)
    game::Machine m(w);
    m.ss() = 0;
    m.cs() = 0x5000;
    game::initialize_player_sprite_core(m);
    w.ram[0xD526] = uint8_t(pose & 1);
    w.ram[0xD500] = 0;
    w.ram[0xC80F] = 0; // native OAM count
    game::draw_player_sprite_core(m);
    std::copy_n(w.ram.begin() + 0xC700, 32, w.ram.begin() + 0xFF00);
    std::copy_n(w.ram.begin() + 0xC000, 16, w.ram.begin() + 0x0200);
    w.publish_terrain_cards();
    w.io[4] = 1;
    w.io[5] = 0;
    w.io[6] = 4;
    w.io[0] |= 4;
}
void prepare_field_npcs(emu::Wsc &w) {
    game::Machine m(w);
    const auto saved = w.cpu.s;
    m.ss() = 0;
    m.cs() = 0x5000;
    game::initialize_map_npcs_core(m);
    game::advance_and_draw_npcs_core(m);
    game::advance_player_animation_timers_core(m);
    std::copy_n(w.ram.begin() + 0xC700, 256, w.ram.begin() + 0xFF00);
    w.cpu.s = saved;
}
} // namespace sawgod::tools
