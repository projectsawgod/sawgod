// Playback-only buffering. All lengths are stereo frames, times are monotonic nanoseconds.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace sawgod::app {
class AudioQueue {
  public:
    static constexpr size_t kRate = 24000;
    // 100 ms absorbs a main-thread hitch of about 60 ms after a 1024-frame device period
    // (70 ms before). Each dry-out adds 20 ms up to the cap; trimming starts well above the cap.
    static constexpr size_t kBase = kRate * 100 / 1000;
    static constexpr size_t kMaximum = kRate * 130 / 1000;
    static constexpr size_t kHighWater = kRate * 170 / 1000;
    static constexpr size_t kFade = kRate * 5 / 1000;
    struct Plan {
        size_t silence = 0, skip = 0, shorten = 0;
    };

    // Only startup and a dry-out insert silence; ordinary packet/device jitter must not accumulate it.
    Plan plan(size_t queued, size_t incoming, uint64_t now);
    // Called immediately before a device pull that cannot be satisfied. One tail/event per dry spell.
    std::span<const int16_t> underrun(uint64_t now);
    // Process only the accepted portion of a packet, after any planned silence/skip.
    // shorten gently removes recovery latency by interpolating across this packet on the producer thread.
    size_t prepare(std::span<int16_t> samples, size_t shorten = 0);
    void reset();
    size_t target() const { return target_; }
    uint64_t underruns() const { return underruns_; }
    uint64_t trimmed() const { return trimmed_; }

  private:
    size_t target_ = kBase, fade_in_ = kFade, recovery_frames_ = 0;
    bool prime_ = true, dry_ = true, trimming_ = false, splice_ = false;
    uint64_t next_decay_ = 0, underruns_ = 0, trimmed_ = 0;
    std::array<int16_t, 2> fade_from_{};
    std::array<int16_t, 2> last_{};
    std::array<int16_t, kFade * 2> tail_{};
};
} // namespace sawgod::app
