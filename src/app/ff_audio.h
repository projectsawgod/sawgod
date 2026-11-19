// Fast-forward sound (no squeaky high-pitched music): while the game runs N times faster,
// its sound is N times as many samples. Instead of keeping every Nth sample (pitch up N times), keep whole
// grains at their own pitch -- kGrain stereo frames, then skip (N - 1) grains' worth -- crossfading each
// grain into the next over kFade frames: the music at its own pitch, jumping ahead ("scrubbing").
// Normal-speed music under a fast game needs the music played apart from the game (the host-played music).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sawgod::app {

class FastForwardAudio {
  public:
    static constexpr size_t kGrain = 1440; // stereo frames: 60 ms at 24 kHz
    static constexpr size_t kFade = 240;   // 10 ms

    // Interleaved stereo in, 1/speed as many frames out (speed 1: unchanged; a speed change restarts).
    std::vector<int16_t> process(std::vector<int16_t> in, int speed) {
        if (speed <= 1) {
            speed_ = 1;
            return in;
        }
        if (speed != speed_) {
            speed_ = speed;
            pos_ = 0;
            tail_.clear();
        }
        const size_t cycle = kGrain * size_t(speed);
        std::vector<int16_t> out;
        out.reserve(in.size() / size_t(speed) + 2);
        for (size_t i = 0; i + 1 < in.size(); i += 2, pos_ = (pos_ + 1) % cycle) {
            if (pos_ < kGrain) { // in a kept grain: fade in from the last grain's continuation
                int16_t l = in[i], r = in[i + 1];
                if (pos_ < kFade && tail_.size() >= 2 * (pos_ + 1)) {
                    const int32_t w = int32_t(pos_ * 256 / kFade);
                    l = int16_t((l * w + tail_[2 * pos_] * (256 - w)) / 256);
                    r = int16_t((r * w + tail_[2 * pos_ + 1] * (256 - w)) / 256);
                }
                out.push_back(l);
                out.push_back(r);
                if (pos_ + 1 == kGrain)
                    tail_.clear();
            } else if (pos_ < kGrain + kFade) { // what would have followed it: faded out into the next grain
                tail_.push_back(in[i]);
                tail_.push_back(in[i + 1]);
            }
        }
        return out;
    }

  private:
    int speed_ = 1;
    size_t pos_ = 0; // stereo frames into the current grain + skip cycle
    std::vector<int16_t> tail_;
};

} // namespace sawgod::app
