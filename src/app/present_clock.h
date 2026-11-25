// A steady clock for Enhanced's Smooth pacing.
//
// With vsync each present shows on the next refresh, but the call returns with jitter: a late return
// (+4.5 ms at 120 Hz) is followed by an early one. Taking the picture's time from the wall clock then
// moves the camera 1.5 and 0.5 refreshes' worth on two refreshes in a row, which reads as a hitch.
// This clock advances exactly one refresh per present and leans a sixteenth of the way toward the wall
// clock each time, so it follows real drift (and catches up after a genuinely missed refresh) while
// ignoring return jitter. A stall of several refreshes (a window move, loading) re-anchors it.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
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
// Presentation only: service native ticks, audio and input before consulting this gate.
class LoSpecCadence {
  public:
    static double divisor_for(double refresh_hz) { return std::max(2.0, std::ceil(refresh_hz / 60 - 1e-6)); }
    static uint64_t period(double refresh_hz = 0) {
        // Half refresh, capped at 60 pictures/s: every 2nd refresh through 120 Hz, then the smallest
        // whole divisor at or below 60 (144 Hz: /3 = 48; 165 Hz: /3 = 55; 240 Hz: /4 = 60).
        if (std::isfinite(refresh_hz) && refresh_hz > 1) {
            const auto divisor = divisor_for(refresh_hz);
            return uint64_t(1e9 * divisor / refresh_hz);
        }
        return 1'000'000'000ull / 30;
    }
    bool due(uint64_t now, double refresh_hz = 0) { return due_every(now, period(refresh_hz)); }
    // The same gate for an explicit presentation interval (Smooth's half-refresh fallback).
    bool due_every(uint64_t now, uint64_t interval) {
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
// Smooth's fallback. A device that keeps missing refreshes at full rate presents every second refresh
// instead of hitching (through LoSpecCadence::due_every, with the same interpolation); it retries full
// rate after 10 s, then 20/40/80 s while retries keep failing. Late: more than 1.5 refreshes since the
// previous present; 6 late presents within the last 60 trigger the fallback (a single load stall does not).
class SmoothFallback {
  public:
    static constexpr int kWindow = 60, kLate = 6;
    static constexpr uint64_t kFirstRetry = 10'000'000'000ull, kLastRetry = 80'000'000'000ull;
    // Once per Smooth present: the interval since the previous present and the display refresh period
    // (0 when unknown: the fallback is off). Returns true when the mode changed.
    bool record(uint64_t interval_ns, uint64_t period_ns, uint64_t now) {
        if (!period_ns) {
            const bool was = halved_;
            reset();
            return was;
        }
        if (halved_) {
            if (now < retry_at_)
                return false;
            halved_ = false;
            probing_ = true;
            clear();
            return true;
        }
        const bool late = interval_ns * 2 > period_ns * 3;
        if (count_ == kWindow)
            late_ -= window_[size_t(at_)];
        else
            ++count_;
        window_[size_t(at_)] = late;
        late_ += late;
        at_ = (at_ + 1) % kWindow;
        if (late_ >= kLate) {
            delay_ = probing_ ? std::min(delay_ * 2, kLastRetry) : kFirstRetry;
            retry_at_ = now + delay_;
            halved_ = true;
            probing_ = false;
            clear();
            return true;
        }
        if (probing_ && count_ == kWindow)
            probing_ = false; // the retry held: a later fallback starts again at 10 s
        return false;
    }
    bool halved() const { return halved_; }
    void reset() {
        halved_ = probing_ = false;
        delay_ = kFirstRetry;
        retry_at_ = 0;
        clear();
    }

  private:
    void clear() {
        window_ = {};
        count_ = late_ = at_ = 0;
    }
    std::array<bool, kWindow> window_{};
    int count_ = 0, late_ = 0, at_ = 0;
    bool halved_ = false, probing_ = false;
    uint64_t delay_ = kFirstRetry, retry_at_ = 0;
};
} // namespace sawgod::app
