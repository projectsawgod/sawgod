#include "app/field_motion.h"

#include <algorithm>
#include <cmath>

#include "game/mode7.h"

namespace sawgod::app {

void native_field_motion(const emu::Wsc &raster, MotionState &state) {
    state.walking_step = float(1 << std::min(2, int(raster.ram[0xD517])));
    state.native_step_known = true;
    for (unsigned axis = 0; axis < 2; ++axis)
        state.native_step[axis] = float(int8_t(raster.ram[0xD515 + axis])) * state.walking_step;
    if (state.eligible)
        for (auto &[record, point] : state.actors)
            if (record >= 0xD5AE && record < 0xD6EE) {
                float step = 0;
                for (unsigned axis = 0; axis < 2; ++axis) {
                    const unsigned velocity = record + 10 + axis * 2;
                    step = std::max(
                        step, std::abs(float(int16_t(raster.ram[velocity] | raster.ram[velocity + 1] << 8))) *
                                  0.5f);
                    if (raster.ram[record] & 0x80)
                        // Native records are 11.5 fixed point; OAM loses the half pixel.
                        point[axis] = sawgod::game::field_subpixel_position(
                            point[axis],
                            uint16_t(raster.ram[record + 6 + axis * 2] | raster.ram[record + 7 + axis * 2]
                                                                             << 8),
                            axis ? 15.f : 8.f);
                }
                state.actor_step[record] = step;
            }
}

} // namespace sawgod::app
