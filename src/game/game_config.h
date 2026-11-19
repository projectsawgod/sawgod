// The game's own Config tab, shown in Options > Gameplay. The game keeps its
// settings in the game state block, so they are saved with each game: 0xD0E6 the message speed (0-7, shown
// 1-8; 10 is Instant), 0xD0E7 option bits (01h dash, 02h auto-target, 04h run while B is held (clear: run
// while B is released), 08h the monster meat preview) and 0xD0E8-0xD0EB what Y1-Y4 open in the field (1-9:
// Item, Ability, Equip, Order, Gallery, Config, Save, Menu, none). With GameConfig on, the app's settings are
// the source: apply() writes them into those bytes before every frame -- the game's own code reads them as it
// always has -- and the party menu leaves its Config tab out (hooks::VB::ConfigTab). Dash is always on, on
// foot and on the World 2 island, and Equip goes back to the gear list after equipping.
//
// Off (headless runs, the equivalence checks), nothing changes: the game's Config tab and bytes are its own.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include "emu/wsc.h"
#include "game/hooks.h"
#include "game/machine.h"
#include "game/session.h"

namespace sawgod::game {

struct GameConfigValues {
    bool b_runs = true;                      // B held runs, released walks (the game's default)
    bool auto_target = true;                 // an action whose target is gone picks another
    bool meat_preview = true;                // eating meat shows the monster it turns into first
    int message_speed = 1;                   // 1 (fast) - 8 (slow), 11 Instant; port default 1
    std::array<uint8_t, 4> y = {3, 1, 8, 2}; // Y1-Y4: the game's 1-9 (default Equip, Item, Menu, Ability)
};

class GameConfig {
  public:
    static constexpr uint16_t kMessageSpeed = 0xD0E6, kOptions = 0xD0E7, kYButtons = 0xD0E8;
    static constexpr uint8_t kConfigTabValue = 6;           // a Y button opening the Config tab: not offered
    static constexpr int kIslandTravelMode = 1;             // travel_mode of the World 2 floating island
    static constexpr uint16_t kRunningAllowedByte = 0xD59C; // running_allowed (the map's run gate)

    // On: the party menu hides its Config tab.
    void set(Session &s, bool on) {
        if (on == on_ && (sub_.active() || !on))
            return;
        on_ = on;
        sub_.reset();
        dash_sub_.reset();
        equip_sub_.reset();
        if (on) {
            // Running on foot, and on the World 2 island (it is slow; the glider and
            // the bike are fast enough), under the same map rule.
            dash_sub_ = s.hooks().subscribe(hooks::VB::RunningAllowed,
                                            [](Machine &m, bool vanilla, const hooks::Args &a) {
                                                if (a.a == kIslandTravelMode)
                                                    vanilla = m.wsc().ram[kRunningAllowedByte] != 0;
                                                else if (a.a != 0)
                                                    return vanilla;
                                                // the world maps and their later-story copies (199, 24)
                                                return vanilla || shown_world(m.wsc().ram[0xD0DC]);
                                            });
            // After equipping, the Equip screen goes back to the gear list.
            equip_sub_ = s.hooks().subscribe(hooks::VB::EquipReturnToGear,
                                             [](Machine &, bool, const hooks::Args &) { return true; });
            sub_ = s.hooks().subscribe(hooks::VB::ConfigTab,
                                       [](Machine &, bool, const hooks::Args &) { return false; });
        }
    }
    // The four world maps (also the native floor-card map IDs), on foot only.
    static bool world_map(int map) { return map == 88 || map == 102 || map == 122 || map == 136; }
    // The world a map shows: maps 199 and 24 are later-story copies of World 3 (122) and World 4 (136) with
    // the same cells, entered by the game's own warps. Presentation only.
    static int world_of(int map) { return map == 199 ? 122 : map == 24 ? 136 : map; }
    // A world map or one of those copies: the Enhanced world camera, its standing cards and the minimap.
    static bool shown_world(int map) { return world_map(world_of(map)); }
    // Maps the Enhanced world camera (Mode 7) shows: the four worlds (with their copies) and the false
    // paradise (map 74, a world-style field map; its Tower stays flat).
    // Presentation only: floor cards keep world_map(); the host dash runs on shown_world() maps.
    static bool mode7_map(int map) { return shown_world(map) || map == 74; }
    bool on() const { return on_; }

    // The values into the game's bytes (call between frames).
    static void apply(emu::Wsc &w, const GameConfigValues &v) {
        w.ram[kMessageSpeed] = uint8_t(v.message_speed == 11 ? 10 : std::clamp(v.message_speed, 1, 8) - 1);
        uint8_t bits = uint8_t(w.ram[kOptions] & ~0x0F);
        bits |= 0x01; // dash: always on
        bits |= v.auto_target ? 0x02 : 0;
        bits |= v.b_runs ? 0x04 : 0;
        bits |= v.meat_preview ? 0x08 : 0;
        w.ram[kOptions] = bits;
        for (size_t i = 0; i < 4; ++i)
            w.ram[kYButtons + i] = v.y[i] >= 1 && v.y[i] <= 9 && v.y[i] != kConfigTabValue ? v.y[i] : 9;
    }

  private:
    bool on_ = false;
    hooks::Subscription sub_, dash_sub_, equip_sub_;
};

} // namespace sawgod::game
