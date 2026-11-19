// WonderSwan sound: four wavetable channels (channel 2 voice, channel 3 sweep, channel 4 noise), mixed to the
// 24 kHz stereo headphone output. Written from ref/docs/wsdev-wiki/Sound.wiki. Sound DMA and Hyper Voice are
// not implemented (Makai Toushi SaGa doesn't use them).
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace sawgod::emu {

class Apu {
  public:
    static constexpr int kSampleRate = 24000;
    static constexpr int kCyclesPerSample = 128; // 3,072,000 Hz / 24,000 Hz

    // io: the system's I/O port array (sound registers live at 0x80-0x9F); ram: internal RAM (wavetables).
    Apu(std::array<uint8_t, 0x100> &io, const std::array<uint8_t, 0x10000> &ram) : io_(io), ram_(ram) {}

    void reset();
    void run(int cycles);                // advance by CPU cycles
    void write(uint8_t port, uint8_t v); // port already stored in io[] by the caller
    uint8_t read(uint8_t port) const;    // ports with live values (0x92-0x9B)
    bool handles_read(uint8_t port) const {
        return port >= 0x92 && port <= 0x9B && port != 0x94 && port != 0x95;
    }

    int sample_index(int ch) const { return index_[ch]; } // debugging / oracle comparison
    int period_counter(int ch) const { return counter_[ch]; }

    // Interleaved stereo samples produced since the last take().
    std::vector<int16_t> take();
    // Optional host routing: take() then contains music; effects have an independent DC filter.
    // Resolve ownership at sample time so a request during a game frame is routed immediately.
    std::function<uint8_t()> effect_channels;
    std::vector<int16_t> take_effects();

    // Playback-only per-channel gains (the front end's music/SFX volume). They scale each channel in the
    // samples take() returns and nothing else: the channel sums the game can read back (ports 0x96-0x9B)
    // are untouched. At 1.0 (the default) the output is bit-identical to the plain mix.
    std::array<float, 4> output_gain{1.0f, 1.0f, 1.0f, 1.0f};

    // Everything but the reference members, for machine snapshots (differential tests that run whole
    // frames). The output buffer is restored to its length at the snapshot.
    struct State {
        int counter[4], index[4];
        uint16_t lfsr;
        int sweep_cycles, sweep_ticks, sample_cycles;
        uint16_t out_left, out_right;
        double dc_l, dc_r, prev_l, prev_r;
        double effect_dc_l, effect_dc_r, effect_prev_l, effect_prev_r;
        size_t effects;
        size_t samples;
    };
    State state() const {
        State s{};
        for (int i = 0; i < 4; ++i) {
            s.counter[i] = counter_[i];
            s.index[i] = index_[i];
        }
        s.lfsr = lfsr_;
        s.sweep_cycles = sweep_cycles_;
        s.sweep_ticks = sweep_ticks_;
        s.sample_cycles = sample_cycles_;
        s.out_left = out_left_;
        s.out_right = out_right_;
        s.dc_l = dc_l_;
        s.dc_r = dc_r_;
        s.prev_l = prev_l_;
        s.prev_r = prev_r_;
        s.samples = samples_.size();
        s.effect_dc_l = effect_dc_l_;
        s.effect_dc_r = effect_dc_r_;
        s.effect_prev_l = effect_prev_l_;
        s.effect_prev_r = effect_prev_r_;
        s.effects = effects_.size();
        return s;
    }
    void set_state(const State &s) {
        for (int i = 0; i < 4; ++i) {
            counter_[i] = s.counter[i];
            index_[i] = s.index[i];
        }
        lfsr_ = s.lfsr;
        sweep_cycles_ = s.sweep_cycles;
        sweep_ticks_ = s.sweep_ticks;
        sample_cycles_ = s.sample_cycles;
        out_left_ = s.out_left;
        out_right_ = s.out_right;
        dc_l_ = s.dc_l;
        dc_r_ = s.dc_r;
        prev_l_ = s.prev_l;
        prev_r_ = s.prev_r;
        effect_dc_l_ = s.effect_dc_l;
        effect_dc_r_ = s.effect_dc_r;
        effect_prev_l_ = s.effect_prev_l;
        effect_prev_r_ = s.effect_prev_r;
        if (s.effects <= effects_.size())
            effects_.resize(s.effects);
        if (s.samples <= samples_.size())
            samples_.resize(s.samples);
    }

  private:
    std::array<uint8_t, 0x100> &io_;
    const std::array<uint8_t, 0x10000> &ram_;

    int counter_[4] = {}; // cycles until the next sample step
    int index_[4] = {};
    uint16_t lfsr_ = 0;
    int sweep_cycles_ = 0; // towards the next 375 Hz sweep tick
    int sweep_ticks_ = 0;
    int sample_cycles_ = 0;
    uint16_t out_left_ = 0, out_right_ = 0; // 10-bit channel sums (ports 0x98 / 0x96)
    double dc_l_ = 0, dc_r_ = 0, prev_l_ = 0, prev_r_ = 0;
    std::vector<int16_t> samples_, effects_;
    double effect_dc_l_ = 0, effect_dc_r_ = 0, effect_prev_l_ = 0, effect_prev_r_ = 0;

    int frequency(int ch) const { return (io_[0x80 + ch * 2] | (io_[0x81 + ch * 2] << 8)) & 0x7FF; }
    void step_channel(int ch);
    void mix();
};

} // namespace sawgod::emu
