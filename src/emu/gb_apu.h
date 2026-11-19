// Game Boy (DMG) sound: two pulse channels (the first with frequency sweep), a 32-step wave channel and a
// noise channel, the 512 Hz frame sequencer (length counters, sweep, volume envelopes), NR50/NR51 mixing.
// Our own core (CC0), written from Pan Docs ("Audio" / "Audio Registers" / "Audio details"). Output is
// 48 kHz interleaved stereo int16: each output sample is the mean of the analog mix over its span of
// T-cycles (a box filter), then the hardware's DC-blocking high-pass.
//
// Not modeled (no music driver depends on them): wave-RAM access corruption, the "zombie" envelope writes,
// length-counter extra clocks on NRx4 writes, power-off register clearing details, PCM12/34 reads.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace sawgod::emu {

class GbApu {
  public:
    static constexpr int kClock = 4194304;    // T-cycles per second
    static constexpr int kSampleRate = 48000; // the default output rate

    // The output rate (e.g. 24000 to mix with the WonderSwan's sound). Clears the samples not yet taken.
    void set_sample_rate(int rate);
    int sample_rate() const { return rate_; }

    GbApu() { reset(); }
    void reset();

    // Register access, FF10-FF3F.
    void write(uint16_t addr, uint8_t v);
    uint8_t read(uint16_t addr) const;

    // Advances the sound chip by T-cycles, producing output samples.
    void run(int cycles);

    // Interleaved stereo samples produced since the last take().
    std::vector<int16_t> take();
    std::vector<int16_t> take_effects();
    bool split_effects = false;
    uint8_t effect_channels = 0;

    // Per-channel mute (playback only; e.g. to hear one channel).
    std::array<bool, 4> muted{};
    std::array<float, 4> output_gain{1, 1, 1, 1}; // playback only, applied before sample integration

    // Smooth host music envelope in APU time; effect ownership bypasses it.
    void music_fade(float first, float last, double cycles) {
        fade_first_ = first;
        fade_last_ = last;
        fade_cycles_ = cycles;
        fade_elapsed_ = 0;
    }

    // The 4-bit digital output of each channel right now (0 when it is off), for tests and note logs.
    int digital(int ch) const;
    bool channel_on(int ch) const { return ch_[ch].on; }
    // Each channel's frequency register (11 bits) and volume (envelope; the wave channel's output level
    // as 0/15/8/4), as the driver last set them.
    int frequency(int ch) const { return ch_[ch].freq; }
    int volume(int ch) const;

  private:
    struct Channel {
        bool on = false, dac = false, length_enable = false;
        int length = 0; // counts up to 64 (256 for wave)
        int freq = 0;   // 11-bit
        int timer = 0;  // T-cycles until the next waveform step
        int pos = 0;    // duty step (0-7), wave sample (0-31)
        int duty = 0;   // pulse
        int volume = 0, env_period = 0, env_timer = 0;
        bool env_up = false;
        int sweep_period = 0, sweep_shift = 0, sweep_timer = 0, shadow = 0; // pulse 1
        bool sweep_down = false, sweep_on = false;
        int wave_shift = 0;               // wave: NR32 output level code
        int divisor = 0, clock_shift = 0; // noise
        bool narrow = false;
        uint16_t lfsr = 0x7FFF;
    };

    std::array<uint8_t, 0x30> reg_{}; // FF10-FF3F as written
    Channel ch_[4];
    bool power_ = true;
    int seq_timer_ = 0, seq_step_ = 0;
    float fade_first_ = 1, fade_last_ = 1;
    double fade_cycles_ = 1, fade_elapsed_ = 0;
    double acc_l_ = 0, acc_r_ = 0;
    double acc_cycles_ = 0; // cycles accumulated into acc_* for the current output sample
    int rate_ = kSampleRate;
    double cycles_per_sample_ = double(kClock) / kSampleRate;
    double charge_ = 0;          // the high-pass factor per output sample
    double hp_l_ = 0, hp_r_ = 0; // DC-blocking capacitor state
    std::vector<int16_t> samples_, effects_;
    double effect_acc_l_ = 0, effect_acc_r_ = 0, effect_hp_l_ = 0, effect_hp_r_ = 0;

    int period(int ch) const;
    void trigger(int ch);
    int sweep_calc();
    void clock_length();
    void clock_sweep();
    void clock_envelope();
    void mix_span(int cycles);
};

} // namespace sawgod::emu
