#include "app/shot_fixture.h"

#include <cstddef>
#include <cstdint>

#include "game/hooks.h"
#include "game/machine.h"
#include "game/maps.h"
#include "tools/field_preview.h"

namespace sawgod::app {

bool prepare_shot_field(emu::Wsc &wsc, const LaunchArgs &args, int walk, game::GfxSwap &gfx_swap) {
    // A fixture rebuild clears the shadow even when the map ID stays the same.
    gfx_swap.set_enabled(wsc, false);
    const auto map = sawgod::game::extract_map(args.shot_map, wsc);
    if (!sawgod::tools::prepare_field_preview(wsc, args.shot_map, map,
                                              args.shot_x + (args.shot_mode7_north ? 0 : walk / 16),
                                              args.shot_y - (args.shot_mode7_north ? (walk + 15) / 16 : 0))) {
        return false;
    }
    if (args.shot_field_party || args.shot_mode7_steps) {
        sawgod::tools::prepare_field_party(wsc, walk > 0 && walk < 48 ? (walk / 4) & 1 : 0);
        const int motion = args.shot_mode7_north ? -walk : walk;
        const int axis = args.shot_mode7_north ? 1 : 0;
        wsc.io[size_t(0x10 + axis)] = uint8_t(motion & 15);
        const uint16_t scroll = uint16_t(((axis ? args.shot_y : args.shot_x) * 16) + motion);
        wsc.ram[size_t(0xD59D + axis * 2)] = uint8_t(scroll);
        wsc.ram[size_t(0xD59E + axis * 2)] = uint8_t(scroll >> 8);
    }
    if (args.shot_field_npcs)
        sawgod::tools::prepare_field_npcs(wsc);
    if (args.shot_mode7_steps)
        sawgod::tools::animate_field_preview(wsc, args.shot_effect_frame);
    // The fixture resets the shadow; rebuild the real GB picture before rasterizing both.
    if (gfx_swap.has_data())
        gfx_swap.set_enabled(wsc, true);
    // Explicit halted field fixture, not inferred scene eligibility in gameplay.
    sawgod::game::Machine m(wsc);
    sawgod::game::hooks::emit(sawgod::game::hooks::Event::PresentationField, m, {1});
    wsc.run_frame();
    if (args.shot_field_party || args.shot_mode7_steps)
        wsc.run_frame(); // latch native OAM
    return true;
}

int shot_view_ticks(const LaunchArgs &args, size_t view, uint64_t frame_ns) {
    if (!view)
        return args.frames.value_or(0) + (args.shot_run > 0);
    if (!args.shot_display_hz)
        return 1;
    return int((uint64_t(view) * 1'000'000'000 / unsigned(args.shot_display_hz)) / frame_ns) -
           int((uint64_t(view - 1) * 1'000'000'000 / unsigned(args.shot_display_hz)) / frame_ns);
}

ui::MenuActions shot_menu_actions(char key) {
    ui::MenuActions a;
    a.up = key == 'u';
    a.down = key == 'd';
    a.left = key == 'l';
    a.right = key == 'r';
    a.accept = key == 'a';
    a.back = key == 'b';
    return a;
}

} // namespace sawgod::app
