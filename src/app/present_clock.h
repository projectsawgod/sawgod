// A steady clock for Enhanced's Smooth pacing.
//
// With vsync each present shows on the next refresh, but the call returns with jitter: a late return
// (+4.5 ms at 120 Hz) is followed by an early one. Taking the picture's time from the wall clock then
// moves the camera 1.5 and 0.5 refreshes' worth on two refreshes in a row, which reads as a hitch.
// This clock advances exactly one refresh per present and leans a sixteenth of the way toward the wall
// clock each time, so it follows real drift (and catches up after a genuinely missed refresh) while
// ignoring return jitter. A stall of several refreshes (a window move, loading) re-anchors it.
#pragma once
#include <cstdint>

namespace sawgod::app {
class PresentClock {
  public:
    // `now`: the wall clock at the start of this pass; `period_ns`: the display's refresh period
    // (0 when unknown or without vsync: the wall clock is returned unchanged).
    uint64_t step(uint64_t now, uint64_t period_ns) {
        if (!period_ns || !anchored_) {
            anchored_ = period_ns != 0;
            time_ = now;
            return now;
        }
        const int64_t predicted = int64_t(time_ + period_ns);
        const int64_t error = int64_t(now) - predicted;
        const int64_t stall = 4 * int64_t(period_ns);
        time_ = error > stall || error < -stall ? now : uint64_t(predicted + error / 16);
        return time_;
    }
    void reset() { anchored_ = false; }

  private:
    uint64_t time_ = 0;
    bool anchored_ = false;
};
} // namespace sawgod::app
