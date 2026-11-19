#include "app/audio_queue.h"
#include <algorithm>

namespace sawgod::app {
AudioQueue::Plan AudioQueue::plan(size_t queued, size_t incoming, uint64_t now) {
    if (!incoming)
        return {};
    if (target_ > kBase && now >= next_decay_) {
        // 2 ms per second after ten quiet seconds. Never discard a whole latency step at once.
        constexpr size_t kStep = kRate * 2 / 1000;
        const uint64_t steps = 1 + (now - next_decay_) / 1'000'000'000;
        const size_t reduction = size_t(std::min<uint64_t>(steps, (target_ - kBase) / kStep)) * kStep;
        target_ -= reduction;
        recovery_frames_ += reduction;
        next_decay_ = now + 1'000'000'000;
    }
    Plan p;
    if (prime_) {
        p.silence = queued + incoming < target_ ? target_ - queued - incoming : 0;
        prime_ = false;
        recovery_frames_ = 0;
    }
    const size_t level = queued + incoming;
    trimming_ = level > kHighWater || (trimming_ && level > target_);
    if (trimming_) {
        p.skip = std::min(incoming, level - target_);
        recovery_frames_ = 0; // trimming already removes the excess latency
        if (p.skip) {
            trimmed_ += p.skip;
            splice_ = true; // keep the old endpoint until some of the new packet is accepted
        }
    } else if (recovery_frames_ && incoming >= kRate / 100 && level > kBase) {
        // Retire the exact target reduction, independent of device jitter and Fast-Forward grain gaps.
        // At most one frame per 10 ms of PCM, smoothly distributed over the packet. Large grains must
        // retire latency at the same rate as ordinary packets, even though they arrive less often.
        p.shorten = std::min({recovery_frames_, incoming / (kRate / 100), level - kBase});
        recovery_frames_ -= p.shorten;
    }
    return p;
}

std::span<const int16_t> AudioQueue::underrun(uint64_t now) {
    if (dry_)
        return {};
    dry_ = true;
    prime_ = true;
    fade_in_ = kFade;
    fade_from_ = {};
    splice_ = false;
    trimming_ = false;
    recovery_frames_ = 0;
    ++underruns_;
    target_ = std::min(target_ + kRate * 20 / 1000, kMaximum);
    next_decay_ = now + 10'000'000'000;
    for (size_t i = 0; i < kFade; ++i)
        for (size_t c = 0; c < 2; ++c)
            tail_[2 * i + c] = int16_t(int(last_[c]) * int(kFade - 1 - i) / int(kFade));
    return tail_;
}

size_t AudioQueue::prepare(std::span<int16_t> samples, size_t shorten) {
    const size_t frames = samples.size() / 2;
    if (!frames)
        return 0;
    if (splice_) {
        fade_from_ = last_;
        fade_in_ = kFade;
        splice_ = false;
    }
    for (size_t i = 0; i < frames && fade_in_; ++i) {
        // Leading silent worker packets must not consume the first sound's fade-in.
        if (fade_in_ == kFade && fade_from_[0] == 0 && fade_from_[1] == 0 && samples[2 * i] == 0 &&
            samples[2 * i + 1] == 0)
            continue;
        for (size_t c = 0; c < 2; ++c)
            samples[2 * i + c] = int16_t(
                (int(samples[2 * i + c]) * int(kFade - fade_in_) + int(fade_from_[c]) * int(fade_in_)) /
                int(kFade));
        --fade_in_;
    }
    shorten = std::min(shorten, frames - 1);
    const size_t kept = frames - shorten;
    if (shorten && kept > 1) {
        // Forward in-place interpolation is safe: the source is always at or ahead of the destination.
        for (size_t i = 0; i < kept; ++i) {
            const size_t position = i * (frames - 1), a = position / (kept - 1), w = position % (kept - 1);
            const size_t b = std::min(a + 1, frames - 1);
            for (size_t c = 0; c < 2; ++c)
                samples[2 * i + c] = int16_t((int64_t(samples[2 * a + c]) * int64_t(kept - 1 - w) +
                                              int64_t(samples[2 * b + c]) * int64_t(w)) /
                                             int64_t(kept - 1));
        }
    }
    last_ = {samples[2 * kept - 2], samples[2 * kept - 1]};
    dry_ = false;
    return kept;
}

void AudioQueue::reset() { *this = AudioQueue{}; }
} // namespace sawgod::app
