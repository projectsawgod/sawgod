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
#include <initializer_list>

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
// Presentation only: service native ticks, audio and input before consulting this gate.
class LoSpecCadence {
  public:
    static uint64_t period(double refresh_hz = 0) {
        // Whole refresh divisors on the common phone panels; other panels use wall time.
        for (int divisor : {2, 3, 4})
            if (refresh_hz > divisor * 30 - 0.5 && refresh_hz < divisor * 30 + 0.5)
                return uint64_t(1e9 * divisor / refresh_hz);
        return 1'000'000'000ull / 30;
    }
    bool due(uint64_t now, double refresh_hz = 0) {
        const auto interval = period(refresh_hz);
        if (interval != period_) {
            reset();
            period_ = interval;
        }
        if (next_ && now < next_)
            return false;
        if (!next_ || now > next_ + interval * 4)
            next_ = now;
        // Drop missed presentation deadlines, never catch up with a burst of duplicate pictures.
        next_ += ((now - next_) / interval + 1) * interval;
        return true;
    }
    uint64_t remaining(uint64_t now) const { return now < next_ ? next_ - now : 0; }
    void reset() { next_ = period_ = 0; }

  private:
    uint64_t next_ = 0, period_ = 0;
};
} // namespace sawgod::app
