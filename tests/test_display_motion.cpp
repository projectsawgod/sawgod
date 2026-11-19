#include "app/display_motion.h"
#include "app/picture_draw.h"
#include "game/mode7.h"
#include <cstdio>
#include <doctest/doctest.h>
#include <vector>
using namespace sawgod;
using namespace sawgod::app;
namespace {
MotionState state(uint64_t frame, float x, float y = 0) {
    MotionState s;
    s.frame = frame;
    s.map = 88;
    s.fidelity = 2;
    s.eligible = true;
    s.camera = {x, y};
    s.actors[0xD520] = {x + 112, y + 119};
    s.actors[0xD5AE] = {400, 500};
    return s;
}
} // namespace
TEST_CASE("Smooth samples completed states at 60 120 144 and 240 Hz without extrapolation") {
    constexpr double hz = 3072000.0 / (159 * 256);
    for (int refresh : {60, 120, 144, 240})
        for (const auto direction :
             {MotionPoint{1, 0}, MotionPoint{-1, 0}, MotionPoint{0, 1}, MotionPoint{0, -1}}) {
            MotionHistory history;
            uint64_t game = 0;
            history.complete(state(0, 0));
            MotionPoint last{};
            for (int present = 0; present < refresh * 10; ++present) {
                const double time = double(present) * hz / refresh;
                const auto due = uint64_t(time);
                while (game < due) {
                    ++game;
                    history.complete(state(game, float(game) * direction[0], float(game) * direction[1]));
                }
                const auto out = history.sample(time - double(due));
                if (due > 2) {
                    const float expected = float(hz / refresh);
                    CHECK((out.camera[0] - last[0]) * direction[0] +
                              (out.camera[1] - last[1]) * direction[1] ==
                          doctest::Approx(expected).epsilon(0.0002));
                    CHECK(out.actors.at(0xD520)[0] - out.camera[0] == doctest::Approx(112));
                    CHECK(out.actors.at(0xD520)[1] - out.camera[1] == doctest::Approx(119));
                    CHECK(out.actors.at(0xD5AE)[0] == 400);
                }
                CHECK(out.fraction >= 0);
                CHECK(out.fraction <= 1);
                CHECK(out.to == game);
                last = out.camera;
            }
            CHECK(history.sample(50).camera == history.sample(1).camera);
        }
}
TEST_CASE("Smooth snaps map warp scene generation owner fade pause jump and speed discontinuities") {
    for (int reason = 0; reason < 10; ++reason) {
        MotionHistory history;
        history.complete(state(10, 10));
        history.complete(state(11, 11));
        auto next = state(12, 12);
        switch (reason) {
        case 0:
            next.map = 80;
            break;
        case 1:
            next.camera = {100, 100};
            break;
        case 2:
            next.eligible = false;
            break;
        case 3:
            next.generation++;
            break;
        case 4:
            next.owner++;
            break;
        case 5:
            next.barrier++;
            break;
        case 6:
            next.fidelity = 1;
            break;
        case 7:
            next.frame += 2;
            break;
        case 8:
            history.reset();
            break;
        case 9:
            next.travel = 2;
            break;
        }
        history.complete(next);
        for (double fraction : {0., .25, .9, 1.}) {
            auto out = history.sample(fraction);
            CHECK(out.snapped);
            CHECK(out.camera == next.camera);
        }
    }
    MotionHistory history;
    history.complete(state(1, 1));
    history.complete(state(2, 2));
    CHECK(history.sample(.2, false).camera == MotionPoint{2, 0});
}
TEST_CASE("Smooth stops and turns immediately and preserves native tile-boundary waits") {
    MotionHistory history;
    history.complete(state(0, 0));
    for (int frame = 1; frame <= 16; ++frame)
        history.complete(state(unsigned(frame), float(frame)));
    const auto before = history.sample(.8);
    history.complete(state(17, 16)); // collision, stop, or native cell-boundary wait
    for (double phase : {0., .5, 1.}) {
        const auto stop = history.sample(phase);
        CHECK(stop.snapped);
        CHECK(stop.camera[0] == 16);
        CHECK(stop.camera[0] >= before.camera[0]);
    }
    history.complete(state(18, 16, 1));
    CHECK(history.sample(.5).camera[0] == 16);
    history.complete(state(19, 17, 1));
    CHECK(history.sample(.5).snapped); // right angle, no diagonal in-between picture
    CHECK(history.sample(.5).camera == MotionPoint{17, 1});
}
TEST_CASE("Smooth record identities survive animation replacement and NPC removal teleport spawn") {
    MotionHistory history;
    history.complete(state(1, 1));
    auto next = state(2, 2);
    next.actors.erase(0xD520);
    next.actors[0xD5AE] = {401, 500};
    next.actors[0xD5C2] = {123, 456};
    history.complete(next);
    const auto out = history.sample(.5);
    CHECK(out.actors.count(0xD520) == 0);
    CHECK(out.actors.at(0xD5AE)[0] == 400.5);
    CHECK(out.actors.at(0xD5C2)[0] == 123);
    next.frame++;
    next.camera[0]++;
    next.actors[0xD5AE][0] = 800;
    history.complete(next);
    CHECK(history.sample(.25).actors.at(0xD5AE)[0] == 800);
    CHECK(MotionHistory::delta(1023, 0, 1024) == 1);
    CHECK(MotionHistory::delta(0, 1023, 1024) == -1);
}
TEST_CASE("Smooth stationary field framing equals the whole-pixel camera at all borders") {
    for (int width : {448, 1353, 1920, 3840})
        for (float x : {-128.f, -8.f, 0.f, 511.f, 1016.f, 1152.f})
            for (float y : {-128.f, -8.f, 0.f, 511.f, 1016.f, 1152.f}) {
                auto exact = game::field_zoom_camera(1.5, 1, width, width * 144 / 224, {x, y});
                auto smooth = game::field_zoom_camera(1.5, 1, width, width * 144 / 224, {x, y}, true);
                CHECK(exact.origin == smooth.origin);
                CHECK(exact.pixel_scale == smooth.pixel_scale);
                CHECK(smooth.origin[0] + x >= -128);
                CHECK(smooth.origin[1] + y >= -128);
            }
}
TEST_CASE("Smooth rejects tiny authored camera jumps but keeps the native final walking step") {
    MotionHistory history;
    auto first = state(1, 100);
    first.native_step_known = true;
    history.complete(first);
    auto jump = first;
    jump.frame++;
    jump.camera[0]++;
    history.complete(jump);
    CHECK(history.sample(.25).snapped);
    auto walking = jump;
    walking.frame++;
    walking.camera[0]++;
    walking.native_step = {1, 0};
    history.complete(walking);
    CHECK_FALSE(history.sample(.25).snapped);
    auto last = walking;
    last.frame++;
    last.camera[0]++;
    last.native_step = {0, 0};
    history.complete(last);
    CHECK_FALSE(history.sample(.25).snapped);
    auto shake = last;
    shake.frame++;
    shake.camera[0]++;
    history.complete(shake);
    CHECK(history.sample(.25).snapped);
}
TEST_CASE("Smooth high-speed vehicle cards stay anchored to the interpolated ground") {
    MotionHistory history;
    auto first = state(1, 4);
    first.walking_step = 4;
    history.complete(first);
    auto next = state(2, 8);
    next.walking_step = 4;
    next.actors[0xD524] = {next.camera[0] + 120, 119};
    first.actors[0xD524] = {first.camera[0] + 120, 119};
    history.reset();
    history.complete(first);
    history.complete(next);
    for (double phase : {0., .25, .5, .75, 1.}) {
        const auto out = history.sample(phase);
        CHECK(out.actors.at(0xD520)[0] - out.camera[0] == doctest::Approx(112));
        CHECK(out.actors.at(0xD524)[0] - out.camera[0] == doctest::Approx(120));
    }
    next.frame++;
    next.camera[0] += 4;
    next.actors[0xD5AE][0] += 0.5f;
    next.actor_step[0xD5AE] = .5f;
    history.complete(next);
    CHECK(history.sample(.5).actors.at(0xD5AE)[0] == 400.25f);
    next.frame++;
    next.camera[0] += 4;
    next.actors[0xD5AE][0] += 1;
    history.complete(next);
    CHECK(history.sample(.5).actors.at(0xD5AE)[0] == 401.5f); // larger than its native declared step: snap
}
// Real tick sequences, recorded headless from the game with SAWGOD_MOTION_TRACE. A token is
// "dx,dy,px,py": the camera's move in that tick and the pending move bytes (0xD515, 0xD516) after it; "xN"
// repeats it. shift is the move shift (0xD517): a step is 1 << shift pixels.
struct Traced {
    const char *name;
    int travel, shift;
    MotionPoint camera;
    const char *ticks;
};
// Walking left on the first world (map 88): 17 ticks a cell, two ticks before the first step.
const Traced kWalk{"walk",
                   0,
                   0,
                   {456, 416},
                   "0,0,0,0 0,0,-1,0x2 -1,0,-1,0x16 0,0,-1,0 -1,0,-1,0x16 0,0,-1,0 -1,0,-1,0x16 0,0,-1,0 "
                   "-1,0,-1,0x16 0,0,-1,0 -1,0,-1,0x16 0,0,-1,0 -1,0,-1,0x2"};
// The bike (travel 3, 2 px steps, map 140): 9 ticks a cell. Up, still riding at the end.
const Traced kBikeUp{"bike up",
                     3,
                     1,
                     {104, 416},
                     "0,0,0,0x2 0,0,0,-1x2 0,-2,0,-1x8 0,0,0,-1 0,-2,0,-1x8 0,0,0,-1 0,-2,0,-1x8 0,0,0,-1 "
                     "0,-2,0,-1x2"};
// The bike, three cells right and a stop.
const Traced kBikeStop{"bike stop",
                       3,
                       1,
                       {104, 352},
                       "0,0,0,0x3 0,0,1,0x2 2,0,1,0x8 0,0,1,0 2,0,1,0x8 0,0,1,0 2,0,1,0x7 2,0,0,0 0,0,0,0x3"};
// The bike, two cells right, then down without stopping: the step that reaches the corner already carries
// the next cell's pending move.
const Traced kBikeTurn{"bike turn",
                       3,
                       1,
                       {168, 352},
                       "0,0,0,0x2 0,0,1,0x2 2,0,1,0x8 0,0,1,0 2,0,1,0x7 2,0,0,1 0,0,0,1 0,2,0,1x8 0,0,0,1 "
                       "0,2,0,1x8 0,0,0,1 0,2,0,1x8 0,0,0,1 0,2,0,1x2"};
// The glider (travel 2, 4 px steps, map 122): 5 ticks a cell on average. Flying right from three places:
// the tick without a step moves around, and a late step shows as none, then 8 px.
const Traced kGliderA{
    "glider A",
    2,
    2,
    {152, 256},
    "0,0,0,0 0,0,1,0x2 8,0,1,0 0,0,1,0 4,0,1,0x2 0,0,1,0 4,0,1,0x4 0,0,1,0 8,0,1,0 0,0,1,0 "
    "4,0,1,0x2 0,0,1,0 4,0,1,0x4 0,0,1,0 8,0,1,0 0,0,1,0 4,0,1,0x2 0,0,1,0 4,0,1,0x4 0,0,1,0 "
    "8,0,1,0 0,0,1,0 4,0,1,0x2 0,0,1,0 4,0,1,0x4 0,0,1,0 8,0,1,0 0,0,1,0 4,0,1,0x2 0,0,1,0 "
    "4,0,1,0x4 0,0,1,0 8,0,1,0 0,0,1,0 4,0,1,0x2 0,0,1,0 4,0,1,0x4 0,0,1,0 8,0,1,0 0,0,1,0"};
const Traced kGliderB{"glider B",
                      2,
                      2,
                      {264, 448},
                      "0,0,0,0 0,0,1,0x2 4,0,1,0 8,0,1,0 0,0,1,0 4,0,1,0 0,0,1,0 4,0,1,0x4 0,0,1,0 4,0,1,0 "
                      "8,0,1,0 0,0,1,0 4,0,1,0 0,0,1,0 4,0,1,0x4 0,0,1,0 4,0,1,0 8,0,1,0 0,0,1,0 4,0,1,0 "
                      "0,0,1,0 4,0,1,0x4 0,0,1,0 4,0,1,0 8,0,1,0 0,0,1,0 4,0,1,0 0,0,1,0 4,0,1,0x4 0,0,1,0 "
                      "4,0,1,0 8,0,1,0 0,0,1,0 4,0,1,0 0,0,1,0 4,0,1,0x4 0,0,1,0 4,0,1,0 8,0,1,0 0,0,1,0 "
                      "4,0,1,0 0,0,1,0 4,0,1,0x4 0,0,1,0 4,0,1,0 8,0,1,0"};
// Here the cells alternate 4 and 6 ticks, and only one tick passes before the first step.
const Traced kGliderC{"glider C",
                      2,
                      2,
                      {-104, 864},
                      "0,0,0,0x2 0,0,1,0 4,0,1,0x4 0,0,1,0 4,0,1,0x2 8,0,1,0 0,0,1,0x2 4,0,1,0x4 0,0,1,0 "
                      "4,0,1,0x2 8,0,1,0 0,0,1,0x2 4,0,1,0x4 0,0,1,0 4,0,1,0x2 8,0,1,0 0,0,1,0x2 4,0,1,0x4 "
                      "0,0,1,0 4,0,1,0x2 8,0,1,0 0,0,1,0x2 4,0,1,0x4 0,0,1,0 4,0,1,0x2 8,0,1,0 0,0,1,0x2 "
                      "4,0,1,0x4 0,0,1,0 4,0,1,0x2 8,0,1,0 0,0,1,0x2 4,0,1,0x4 0,0,1,0 4,0,1,0x2 8,0,1,0 "
                      "0,0,1,0x2 4,0,1,0"};
// The glider, three cells up and a stop: one cell starts with a step, the tick without one comes second.
const Traced kGliderUp{"glider up",
                       2,
                       2,
                       {712, 816},
                       "0,0,0,0 0,0,0,-1x2 0,-4,0,-1x5 0,0,0,-1 0,-4,0,-1x3 0,0,0,-1 0,-4,0,-1x3 0,-4,0,0 "
                       "0,0,0,0x3"};
// The glider, four cells right and a stop.
const Traced kGliderStop{"glider stop",
                         2,
                         2,
                         {712, 832},
                         "0,0,0,0x3 0,0,1,0x2 4,0,1,0x4 0,0,1,0 8,0,1,0 0,0,1,0 4,0,1,0x2 0,0,1,0 4,0,1,0x4 "
                         "0,0,1,0 8,0,1,0 0,0,1,0 4,0,1,0 4,0,0,0 0,0,0,0x4"};
std::vector<MotionState> traced(const Traced &t) {
    std::vector<MotionState> frames;
    MotionPoint camera = t.camera;
    const float step = float(1 << t.shift);
    for (const char *p = t.ticks; *p;) {
        int dx = 0, dy = 0, px = 0, py = 0, used = 0, count = 1;
        REQUIRE(std::sscanf(p, "%d,%d,%d,%d%n", &dx, &dy, &px, &py, &used) == 4);
        p += used;
        if (*p == 'x' && std::sscanf(p, "x%d%n", &count, &used) == 1)
            p += used;
        while (*p == ' ')
            ++p;
        for (int i = 0; i < count; ++i) {
            camera = {camera[0] + float(dx), camera[1] + float(dy)};
            auto s = state(frames.size() + 100, camera[0], camera[1]);
            s.travel = t.travel;
            s.walking_step = step;
            s.native_step_known = true;
            s.native_step = {float(px) * step, float(py) * step};
            s.actors[0xD524] = {camera[0] + 120, camera[1] + 119};
            frames.push_back(s);
        }
    }
    return frames;
}
struct Shown {
    size_t tick; // the last completed tick
    DisplayMotion out;
};
// The pictures a display of that refresh rate shows: every tick completed in time, sampled at each refresh.
std::vector<Shown> present(const std::vector<MotionState> &frames, double refresh) {
    MotionHistory history;
    std::vector<Shown> shown;
    size_t completed = 0;
    for (int at = 0;; ++at) {
        const double t = at * 75.47 / refresh;
        const size_t tick = size_t(t);
        if (tick >= frames.size())
            return shown;
        while (completed <= tick)
            history.complete(frames[completed++]);
        shown.push_back({tick, history.sample(t - double(tick))});
    }
}
float travelled(const MotionPoint &a, const MotionPoint &b) {
    return std::abs(b[0] - a[0]) + std::abs(b[1] - a[1]);
}
size_t first_pending(const std::vector<MotionState> &frames) {
    size_t at = 0;
    while (frames[at].native_step == MotionPoint{})
        ++at;
    return at;
}
TEST_CASE("Smooth shows the traced walking, bike and glider cameras at an even speed on every display") {
    for (const Traced *t : {&kWalk, &kBikeUp, &kBikeStop, &kBikeTurn, &kGliderA, &kGliderB, &kGliderC,
                            &kGliderUp, &kGliderStop})
        for (const double refresh : {60., 75.47, 120., 144.}) {
            CAPTURE(t->name);
            CAPTURE(refresh);
            const auto frames = traced(*t);
            const auto shown = present(frames, refresh);
            const float step = float(1 << t->shift);
            // 16 px in (steps + 1) ticks: walking 0.94, the bike 1.78, the glider 3.2 px a tick.
            const float speed = 16 / (16 / step + 1) * float(75.47 / refresh);
            // Steady: from the third tick of the move (the start eases in) to the last tick that still has
            // a move pending.
            const size_t from = first_pending(frames) + 3;
            size_t to = frames.size() - 1;
            while (frames[to].native_step == MotionPoint{})
                --to;
            int steady = 0;
            for (size_t i = 1; i < shown.size(); ++i) {
                const auto &out = shown[i].out;
                CHECK(out.actors.at(0xD520)[0] - out.camera[0] == doctest::Approx(112));
                CHECK(out.actors.at(0xD520)[1] - out.camera[1] == doctest::Approx(119));
                CHECK(out.actors.at(0xD524)[0] - out.camera[0] == doctest::Approx(120));
                if (shown[i - 1].tick < from || shown[i].tick > to)
                    continue;
                CHECK_FALSE(out.snapped);
                CHECK(travelled(shown[i - 1].out.camera, out.camera) == doctest::Approx(speed).epsilon(0.03));
                ++steady;
            }
            CHECK(steady >= 8);
        }
}
TEST_CASE("Smooth starts a traced move without a jump and ends it on the stop") {
    for (const Traced *t : {&kBikeStop, &kGliderUp, &kGliderStop})
        for (const double refresh : {60., 75.47, 144.}) {
            CAPTURE(t->name);
            CAPTURE(refresh);
            const auto frames = traced(*t);
            const auto shown = present(frames, refresh);
            const MotionPoint start = frames.front().camera, stop = frames.back().camera;
            const float step = float(1 << t->shift), whole = travelled(start, stop);
            const float speed = 16 / (16 / step + 1) * float(75.47 / refresh);
            size_t last_move = frames.size() - 1;
            while (frames[last_move].camera == stop)
                --last_move;
            ++last_move; // the tick whose step reached the stop
            for (size_t i = 1; i < shown.size(); ++i) {
                const float before = travelled(start, shown[i - 1].out.camera),
                            now = travelled(start, shown[i].out.camera);
                CHECK(now >= before - 0.001f);        // never backwards
                CHECK(now <= whole + 0.001f);         // never past the stop
                CHECK(now - before <= speed * 1.03f); // and never faster than the steady speed
                // The picture is never more than a step (and its own lead) from the game's camera.
                CHECK(travelled(shown[i].out.camera, frames[shown[i].tick].camera) <= 2 * step + 0.001f);
                if (shown[i].tick > last_move) // on the stop a tick after the game at the latest
                    CHECK(shown[i].out.camera == stop);
            }
            CHECK(shown.back().out.camera == stop);
        }
}
TEST_CASE("Smooth takes a traced corner along the path walked, at the same speed") {
    const auto frames = traced(kBikeTurn);
    const MotionPoint corner{200, 352};
    for (const double refresh : {60., 75.47, 144.}) {
        CAPTURE(refresh);
        const auto shown = present(frames, refresh);
        const float speed = 16 / 9.f * float(75.47 / refresh);
        const size_t from = first_pending(frames) + 3;
        bool turned = false;
        for (size_t i = 1; i < shown.size(); ++i) {
            const auto &camera = shown[i].out.camera;
            // Right along y = 352 up to the corner, then down along x = 200: no diagonal picture.
            CHECK((camera[1] == 352 || camera[0] == 200));
            CHECK(camera[0] <= 200);
            CHECK(camera[1] >= 352);
            turned |= camera[1] > 352;
            if (shown[i - 1].tick >= from) {
                CHECK_FALSE(shown[i].out.snapped);
                const float path =
                    travelled(shown[i - 1].out.camera, corner) * (camera[1] > 352 ? 1.f : -1.f) +
                    travelled(corner, camera) * (shown[i - 1].out.camera[1] > 352 ? -1.f : 1.f);
                CHECK(std::abs(path) == doctest::Approx(speed).epsilon(0.03));
            }
        }
        CHECK(turned);
    }
}
TEST_CASE("Smooth picked up mid-move shows what an unbroken history shows") {
    // The mode soak compares a run with graphics switches against one without; once the switch settles the
    // pictures must agree, also partway through a cell. Three ways to lose the thread: a reset (the switch
    // itself), frames that are not eligible (the Mode 7 fade-in), and frames never seen (another Graphics
    // level). Walking and the bike step on time, so they agree from the first tick shown.
    for (const Traced *t : {&kWalk, &kBikeUp, &kBikeTurn}) {
        CAPTURE(t->name);
        const auto frames = traced(*t);
        const size_t begin = first_pending(frames) + 3;
        for (size_t cut = begin; cut + 5 < frames.size(); ++cut)
            for (const int gap : {0, 1, 3}) {
                MotionHistory control, switched, late;
                for (size_t f = 0; f < frames.size(); ++f) {
                    control.complete(frames[f]);
                    auto seen = frames[f];
                    if (f == cut)
                        switched.reset();
                    if (f >= cut && f < cut + size_t(gap)) {
                        seen.eligible = false;
                        seen.fidelity = 0;
                    }
                    switched.complete(seen);
                    if (f >= cut + size_t(gap))
                        late.complete(frames[f]);
                    for (double phase : {0., .5, 1.}) {
                        const auto want = control.sample(phase).camera;
                        if (f >= cut + size_t(gap)) {
                            CHECK(switched.sample(phase).camera[0] == doctest::Approx(want[0]));
                            CHECK(switched.sample(phase).camera[1] == doctest::Approx(want[1]));
                        }
                        if (f > cut + size_t(gap) && !late.sample(phase).snapped) {
                            CHECK(late.sample(phase).camera[0] == doctest::Approx(want[0]));
                            CHECK(late.sample(phase).camera[1] == doctest::Approx(want[1]));
                        }
                    }
                }
            }
    }
    // The glider's ticks are uneven; a history picked up mid-move leads by less than a tick's distance and
    // agrees again within two cells.
    const auto frames = traced(kGliderC);
    for (size_t cut = 8; cut + 14 < frames.size(); ++cut) {
        MotionHistory control, late;
        for (size_t f = 0; f < frames.size(); ++f) {
            control.complete(frames[f]);
            if (f < cut)
                continue;
            late.complete(frames[f]);
            if (f == cut)
                continue;
            const float lead = late.sample(1).camera[0] - control.sample(1).camera[0];
            CHECK(lead >= -0.001f);
            CHECK(lead <= 3.201f);
            if (f >= cut + 11)
                CHECK(lead == doctest::Approx(0));
        }
    }
}

TEST_CASE("Enhanced scene cap preserves small outputs and fits larger outputs at an integer native scale") {
    for (const auto size : {std::array<int, 2>{224, 144}, {672, 432}, {1120, 720}, {1008, 648}})
        CHECK(enhanced_scene_size(size[0], size[1]) == size);
    CHECK(enhanced_scene_size(2240, 1440) == std::array<int, 2>{1120, 720});
    CHECK(enhanced_scene_size(1344, 864) == std::array<int, 2>{1120, 720});
    CHECK(enhanced_scene_size(3360, 2160) == std::array<int, 2>{1120, 720});
}
