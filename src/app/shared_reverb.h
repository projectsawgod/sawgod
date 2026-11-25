// One reverb for the whole Enhanced music mix (Freeverb design: 8 damped combs + 4 allpasses per channel,
// Jezar's public-domain algorithm, written for this project). The synths render dry; their reverb sends are
// summed and reverberated once, instead of one reverb per synth instance.
#pragma once
#include <array>
#include <vector>

namespace sawgod::app {
class SharedReverb {
  public:
    // room/damp/width/level follow FluidSynth's synth.reverb.* meanings (0..1).
    explicit SharedReverb(int sample_rate = 24000, double room = 0.55, double damp = 0.3, double width = 0.8,
                          double level = 0.7) {
        static constexpr int combs[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        static constexpr int allpasses[4] = {556, 441, 341, 225};
        constexpr int spread = 23;
        const double scale = sample_rate / 44100.0;
        for (int c = 0; c < 2; ++c) {
            for (int i = 0; i < 8; ++i)
                comb_[c][i].buffer.assign(size_t((combs[i] + c * spread) * scale), 0.f);
            for (int i = 0; i < 4; ++i)
                allpass_[c][i].buffer.assign(size_t((allpasses[i] + c * spread) * scale), 0.f);
        }
        feedback_ = float(room * 0.28 + 0.7);
        damp_ = float(damp * 0.4);
        const double wet = level * 3;
        wet1_ = float(wet * (width / 2 + 0.5));
        wet2_ = float(wet * ((1 - width) / 2));
    }
    void reset() {
        for (auto &channel : comb_)
            for (auto &c : channel) {
                std::fill(c.buffer.begin(), c.buffer.end(), 0.f);
                c.store = 0;
                c.at = 0;
            }
        for (auto &channel : allpass_)
            for (auto &a : channel) {
                std::fill(a.buffer.begin(), a.buffer.end(), 0.f);
                a.at = 0;
            }
    }
    // send and out are interleaved stereo; the wet signal is added to out.
    void process(const float *send, float *out, int frames) {
        for (int i = 0; i < frames; ++i) {
            const float input = (send[2 * i] + send[2 * i + 1]) * 0.015f;
            float l = 0, r = 0;
            for (int k = 0; k < 8; ++k) {
                l += comb_[0][size_t(k)].run(input, feedback_, damp_);
                r += comb_[1][size_t(k)].run(input, feedback_, damp_);
            }
            for (int k = 0; k < 4; ++k) {
                l = allpass_[0][size_t(k)].run(l);
                r = allpass_[1][size_t(k)].run(r);
            }
            out[2 * i] += l * wet1_ + r * wet2_;
            out[2 * i + 1] += r * wet1_ + l * wet2_;
        }
    }

  private:
    struct Comb {
        std::vector<float> buffer;
        float store = 0;
        size_t at = 0;
        float run(float input, float feedback, float damp) {
            const float output = buffer[at];
            store = output * (1 - damp) + store * damp;
            buffer[at] = input + store * feedback;
            if (++at == buffer.size())
                at = 0;
            return output;
        }
    };
    struct Allpass {
        std::vector<float> buffer;
        size_t at = 0;
        float run(float input) {
            const float delayed = buffer[at];
            buffer[at] = input + delayed * 0.5f;
            if (++at == buffer.size())
                at = 0;
            return delayed - input;
        }
    };
    std::array<std::array<Comb, 8>, 2> comb_;
    std::array<std::array<Allpass, 4>, 2> allpass_;
    float feedback_ = 0, damp_ = 0, wet1_ = 0, wet2_ = 0;
};
} // namespace sawgod::app
