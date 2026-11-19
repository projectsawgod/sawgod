// Completed-state motion only. No machine, input, audio, or wall clock dependencies.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>

namespace sawgod::app {
using MotionPoint = std::array<float, 2>;
struct MotionState {
    uint64_t frame = 0, generation = 0, barrier = 0;
    int map = -1, fidelity = 0, owner = 0, travel = 0;
    bool eligible = false;
    float walking_step = 1;
    bool native_step_known = false;
    MotionPoint native_step{};
    std::map<uint32_t, float> actor_step;
    // A wrap period is explicit; the stock loaded 80-cell atlas does not wrap.
    float wrap = 0;
    MotionPoint camera{};
    std::map<uint32_t, MotionPoint> actors;
};
struct DisplayMotion {
    MotionPoint camera{};
    std::map<uint32_t, MotionPoint> actors;
    uint64_t from = 0, to = 0;
    float fraction = 1;
    bool snapped = true;
};
class MotionHistory {
  public:
    static float delta(float a, float b, float wrap) {
        return wrap > 0 ? std::remainder(b - a, wrap) : b - a;
    }
    void reset() { seen_ = false; }
    void complete(MotionState state) {
        if (seen_ && state.frame == current_.frame && state.generation == current_.generation)
            return;
        previous_ = current_;
        current_ = std::move(state);
        // A break in the game's own motion ends any cell move. A break only in what is displayed (a graphics
        // switch, the field becoming eligible again, a reset for Options) snaps the picture unless a cell
        // move carries on or resumes: then the picture is the one an unbroken history shows.
        bool broken = current_.frame != previous_.frame + 1 || current_.generation != previous_.generation ||
                      current_.map != previous_.map || current_.barrier != previous_.barrier ||
                      current_.travel != previous_.travel;
        const bool display_break = !seen_ || !previous_.eligible || current_.fidelity != previous_.fidelity ||
                                   current_.owner != previous_.owner;
        MotionPoint movement{};
        for (size_t axis = 0; axis < 2; ++axis)
            movement[axis] = delta(previous_.camera[axis], current_.camera[axis], current_.wrap);
        // A committed 16-pixel cell move is displayed at one constant speed, so the picture never pauses or
        // jumps inside it or between cells. Traced from the game: moving steadily, a cell
        // takes its steps plus one tick (walking 17, the bike 9, the glider 5), but only on average. The
        // picture is read at a fixed place in the frame and the game writes the camera a little earlier or
        // later: the tick without a step can fall anywhere in a cell, a late step shows as "no step, then
        // two" (the glider: 0 then 8 px), and glider cells can alternate 4 and 6 ticks.
        const bool hold = movement[0] == 0 && movement[1] == 0;
        if (broken)
            glide_.active = false;
        const bool cell = !broken && cell_tick(movement, hold);
        if (!cell) {
            for (size_t axis = 0; axis < 2; ++axis)
                broken |= std::abs(movement[axis]) > current_.walking_step + 0.001f;
            if (current_.native_step_known && !hold) {
                // A small authored camera jump can be the same size as walking. Accept only the native
                // pending move (or its final step, after the pending word has been cleared).
                const auto matches = [&](const MotionPoint &move) {
                    return std::abs(movement[0] - move[0]) < 0.001f &&
                           std::abs(movement[1] - move[1]) < 0.001f;
                };
                const bool final_step = current_.native_step == MotionPoint{} && previous_.native_step_known;
                broken |= !matches(current_.native_step) && !(final_step && matches(previous_.native_step));
            }
        }
        // Bookkeeping runs on while the picture is not eligible (the Mode 7 fade-in); only eligible frames
        // glide. A break only in what is displayed snaps the picture unless a cell move carries on.
        snap_ = broken || !current_.eligible || (display_break && !glide_.active);
        // Stops and direction changes publish their endpoint immediately. Do not trail a collision,
        // and never smooth backwards after a turn. A tick of a cell move is neither.
        if (seen_ && !cell && (last_move_[0] != 0 || last_move_[1] != 0))
            snap_ |= movement[0] * last_move_[0] + movement[1] * last_move_[1] <= 0;
        last_move_ = movement;
        seen_ = true;
    }
    bool snapping() const { return snap_; }
    DisplayMotion sample(double phase, bool enabled = true) const {
        DisplayMotion out{current_.camera, current_.actors, previous_.frame, current_.frame};
        out.snapped = !enabled || snap_;
        out.fraction = out.snapped ? 1 : float(std::clamp(phase, 0.0, 1.0));
        if (!out.snapped) {
            if (glide_.active)
                out.camera = glide_.at(out.fraction);
            else
                for (size_t axis = 0; axis < 2; ++axis)
                    out.camera[axis] =
                        previous_.camera[axis] +
                        delta(previous_.camera[axis], current_.camera[axis], current_.wrap) * out.fraction;
            for (auto &[record, point] : out.actors) {
                // During a cell move the party and its vehicle ride the camera: they keep their screen place.
                if (glide_.active && (record == 0xD520 || record == 0xD524)) {
                    for (size_t axis = 0; axis < 2; ++axis)
                        point[axis] += out.camera[axis] - current_.camera[axis];
                    continue;
                }
                const auto old = previous_.actors.find(record);
                if (old == previous_.actors.end())
                    continue; // newly visible/spawned records snap, art/visibility always current
                bool continuous = true;
                const auto speed = current_.actor_step.find(record);
                const float limit = speed == current_.actor_step.end()
                                        ? (record == 0xD520 || record == 0xD524 ? current_.walking_step : 2.f)
                                        : speed->second;
                for (size_t axis = 0; axis < 2; ++axis)
                    continuous &=
                        std::abs(delta(old->second[axis], point[axis], current_.wrap)) <= limit + 0.001f;
                if (continuous)
                    for (size_t axis = 0; axis < 2; ++axis)
                        point[axis] = old->second[axis] +
                                      delta(old->second[axis], point[axis], current_.wrap) * out.fraction;
            }
        }
        return out;
    }

  private:
    static float distance(const MotionPoint &a, const MotionPoint &b) {
        return std::abs(b[0] - a[0]) + std::abs(b[1] - a[1]); // every leg runs along one axis
    }
    static MotionPoint toward(const MotionPoint &a, const MotionPoint &b, float length) {
        const float whole = distance(a, b);
        if (whole <= 0 || length >= whole)
            return b;
        const float t = std::max(0.f, length) / whole;
        return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t};
    }
    // One tick of a cell move. The picture's camera follows the game's at the cell's average speed, 16 px
    // per (steps + 1) ticks, and never passes the on-time place of the tick: the place a game that steps
    // exactly on time (one tick without a step, then the steps) shows at the end of this tick. That place
    // depends on this tick alone, so a history picked up mid-move agrees with an unbroken one as soon as
    // both touch it (always, for walking and the bike). False when the tick is not part of a cell move.
    bool cell_tick(const MotionPoint &movement, bool hold) {
        const float step = current_.walking_step;
        const MotionPoint &pending = current_.native_step;
        const bool pending_set = pending[0] != 0 || pending[1] != 0;
        if (!current_.native_step_known || step <= 0 || (pending[0] != 0 && pending[1] != 0) ||
            (movement[0] != 0 && movement[1] != 0)) {
            glide_.active = false;
            return false;
        }
        const float speed = 16 / (16 / step + 1); // px per tick
        if (hold && !pending_set) {
            // Stopped. A picture still short of the stop (the last step came late) finishes at its speed.
            if (!glide_.active || distance(glide_.to, current_.camera) < 0.001f) {
                glide_.active = false;
                return false;
            }
            glide_.advance(current_.camera, current_.camera,
                           std::max(glide_.speed, distance(glide_.to, current_.camera) - step));
            return true;
        }
        const MotionPoint &heading = hold ? pending : movement;
        const size_t axis = heading[0] != 0 ? 0 : 1;
        const float sign = heading[axis] > 0 ? 1.f : -1.f;
        if (!hold) {
            // A step belongs to the move pending on the tick before it (after a gap in the history: to the
            // move pending now), and is one or more whole steps.
            const MotionPoint &owner = glide_.active ? previous_.native_step : pending;
            const float steps = std::abs(movement[axis]) / step;
            if (owner[1 - axis] != 0 || owner[axis] * sign <= 0 ||
                std::abs(steps - std::round(steps)) > 0.001f || std::abs(movement[axis]) > 16.001f) {
                glide_.active = false;
                return false;
            }
        }
        // The camera's place in its cell: cell edges sit at x = 8 and y = 0, modulo 16.
        const float along = current_.camera[axis] * sign - (axis == 0 ? 8.f : 0.f);
        float into = along - std::floor(along / 16) * 16;
        if (!hold && into < 0.001f)
            into = 16; // the step that reached the cell's end
        const float on_grid = into / step;
        // Only the step that reaches the cell's end may come with its pending move cleared or replaced.
        if (std::abs(on_grid - std::round(on_grid)) > 0.001f ||
            (!hold && into < 15.999f && (pending[1 - axis] != 0 || pending[axis] * sign <= 0))) {
            glide_.active = false;
            return false;
        }
        MotionPoint edge = current_.camera, on_time = current_.camera;
        edge[axis] -= sign * into;
        on_time[axis] = edge[axis] + sign * (into + step) * speed / step;
        if (std::abs(on_time[axis] - current_.camera[axis]) < 0.001f)
            on_time[axis] = current_.camera[axis]; // the cell's end, exactly
        if (!glide_.active) {
            // From standstill the game mostly spends two ticks before the first step, sometimes one: ease
            // in with half a tick's distance, and make it up on the next tick.
            glide_.starting = hold && into < 0.001f && previous_.native_step_known &&
                              previous_.native_step == MotionPoint{};
            glide_.active = true;
            glide_.to = on_time;
            glide_.to[axis] -= sign * speed;
            glide_.speed = speed;
            glide_.advance(glide_.to, on_time, glide_.starting ? speed / 2 : speed);
            return true;
        }
        glide_.speed = speed;
        const bool starting = glide_.starting;
        glide_.starting = false;
        // Through the cell's edge when the picture is not yet in this cell (a turn, or the edge still ahead).
        const bool in_cell = std::abs(glide_.to[1 - axis] - edge[1 - axis]) < 0.001f &&
                             (glide_.to[axis] - edge[axis]) * sign > -0.001f;
        const MotionPoint via = in_cell ? glide_.to : edge;
        // Never more than one step behind the game's camera.
        const float behind = distance(glide_.to, via) + (current_.camera[axis] - via[axis]) * sign;
        glide_.advance(via, on_time, starting ? 16.f : std::max(speed, behind - step));
        if (distance(glide_.to, on_time) < 0.01f)
            glide_.to = on_time; // exactly, so that sums of float steps never drift from it
        return true;
    }
    MotionState previous_{}, current_{};
    MotionPoint last_move_{};
    bool seen_ = false, snap_ = true;
    // The displayed camera of a cell move over the tick just completed: from, through via, to.
    struct Glide {
        bool active = false, starting = false;
        MotionPoint from{}, via{}, to{};
        float speed = 0;
        // Moves on from the last place by at most length: to corner first, then on toward target.
        void advance(const MotionPoint &corner, const MotionPoint &target, float length) {
            from = to;
            const float first = distance(from, corner);
            via = toward(from, corner, length);
            to = length > first ? toward(corner, target, length - first) : via;
        }
        MotionPoint at(float fraction) const {
            const float first = distance(from, via), length = (first + distance(via, to)) * fraction;
            return length <= first ? toward(from, via, length) : toward(via, to, length - first);
        }
    } glide_;
};
} // namespace sawgod::app
