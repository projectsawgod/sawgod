// Draws optional enemy HP labels from the current battle state.
#include "game/info.h"
#include "game/session.h"
#include "game/ui_chrome.h"

#include <algorithm>
#include <string>
#include <vector>

namespace sawgod::game {

namespace {
constexpr uint16_t kGroupRecords = 0xD500, kGroupTypes = 0xD6D5, kGroupCount = 0xD6D4, kBattleActors = 0xD71A;
uint16_t ram16(const emu::Wsc &w, uint16_t at) { return uint16_t(w.ram[at] | w.ram[uint16_t(at + 1)] << 8); }
} // namespace

void update_enemy_hp_boxes(emu::Wsc &w, bool on) {
    auto &boxes = Session::of(w).enemy_hp_boxes();
    boxes.clear();
    if (!on)
        return;
    const int groups = w.ram[kGroupCount];
    if (groups < 1 || groups > 3)
        return;
    static constexpr int kColumns[3][3] = {{0x0C}, {0x07, 0x11}, {0x04, 0x0C, 0x14}};
    const uint16_t screen2 = uint16_t(((w.io[0x07] >> 4) & 0x0F) << 11);
    // Screen 2's scroll as a signed offset: the party-hit shake (shake_screen_for_party_hit) moves it by
    // -3/-4 px, i.e. FDh/FCh, which must move the bars with the enemies, not 253 px off to a clamped corner.
    const int sx = int8_t(w.io[0x12]), sy = int8_t(w.io[0x13]);
    for (int g = 0; g < groups; ++g) {
        if (w.ram[uint16_t(kGroupTypes + 1 + g * 2)] == 0) // no one left in the group
            continue;
        const uint16_t rec = uint16_t(kGroupRecords + g * 12);
        const int pw = w.ram[uint16_t(rec + 9)], ph = w.ram[uint16_t(rec + 10)];
        int col = kColumns[groups - 1][g], row = 3;
        if (pw == 6)
            col -= 1;
        else if (pw == 9)
            col -= 2, row -= 2;
        bool shown = false; // the picture's cells still on screen 2
        for (int y = 0; y < ph && !shown; ++y)
            for (int x = 0; x < pw && !shown; ++x) {
                const uint16_t c = ram16(w, uint16_t(screen2 + ((row + y) * 32 + col + x) * 2));
                shown = (c & 0x2000) && (c >> 9 & 15) == 4 + g;
            }
        if (!shown)
            continue;
        // the group's first living member (its type at actor +1Fh; alive: status not FFh, bits 0-1 clear)
        const uint8_t type = w.ram[uint16_t(kGroupTypes + g * 2)];
        int hp = -1, max = 0;
        for (int a = 4; a < 31 && hp < 0; ++a) {
            const uint16_t at = uint16_t(kBattleActors + a * 0x20);
            const uint8_t status = w.ram[uint16_t(at + 2)];
            if (w.ram[uint16_t(at + 0x1F)] != type || status == 0xFF || (status & 3))
                continue;
            hp = ram16(w, uint16_t(at + 3));
            max = ram16(w, uint16_t(at + 5));
        }
        if (hp < 0)
            continue;
        const std::string text = std::to_string(hp) + "/" + std::to_string(max);
        // A red HP bar like the party's: at least the party bars' 32 px.
        const int width = std::max(32, Chrome::numerals_width(text) + 1 + 4);
        const int x0 = std::clamp(col * 8 + pw * 4 - width / 2 - sx, 0, emu::Wsc::kWidth - width - 1);
        // Keep two clear rows above every meter; clamp after scrolling, including its drop shadow.
        const int y0 = std::clamp(row * 8 - 8 - sy, 2, emu::Wsc::kHeight - 9);
        boxes.push_back({x0, y0, width, text, hp, max});
    }
}

void draw_enemy_hp_boxes(emu::Wsc &w, std::vector<uint32_t> &picture, emu::Wsc::ColorMode mode, bool second) {
    const auto *session = Session::find(w);
    if (!session || session->enemy_hp_boxes().empty())
        return;
    Chrome chrome(w, picture, mode, second);
    for (const auto &box : session->enemy_hp_boxes())
        chrome.bar(box.x, box.y, box.width, 8, box.hp, box.max, box.text, Chrome::kEnemyHp);
}

} // namespace sawgod::game
