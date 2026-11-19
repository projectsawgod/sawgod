#include "emu/apu.h"

namespace sawgod::emu {

namespace {
constexpr int kTap[8] = {14, 10, 13, 4, 8, 6, 9, 11}; // noise LFSR tap bit per mode (port 0x8E bits 0-2)
constexpr int kSweepPeriod = 3072000 / 375;
} // namespace

void Apu::reset() {
    for (int i = 0; i < 4; ++i)
        counter_[i] = index_[i] = 0;
    lfsr_ = 0;
    sweep_cycles_ = sweep_ticks_ = sample_cycles_ = 0;
    out_left_ = out_right_ = 0;
    dc_l_ = dc_r_ = prev_l_ = prev_r_ = 0;
    samples_.clear();
    effects_.clear();
    effect_dc_l_ = effect_dc_r_ = effect_prev_l_ = effect_prev_r_ = 0;
}

void Apu::write(uint8_t port, uint8_t v) {
    switch (port) {
    case 0x81:
    case 0x83:
    case 0x85:
    case 0x87: // frequency dividers are 11 bits
        io_[port] = v & 0x07;
        break;
    case 0x94:
        io_[port] = v & 0x0F;
        break;
    case 0x8D: // writing the sweep ticks restarts the sweep counter
        io_[port] = v & 0x1F;
        sweep_ticks_ = 0;
        sweep_cycles_ = 0;
        break;
    case 0x8E:
        io_[port] = v & 0x17; // the LFSR reset bit doesn't stay set
        if (v & 0x08)
            lfsr_ = 0;
        break;
    default:
        break;
    }
}

uint8_t Apu::read(uint8_t port) const {
    switch (port) {
    case 0x92:
        return lfsr_ & 0xFF;
    case 0x93:
        return (lfsr_ >> 8) & 0x7F;
    case 0x96:
        return out_right_ & 0xFF;
    case 0x97:
        return (out_right_ >> 8) & 0x03;
    case 0x98:
        return out_left_ & 0xFF;
    case 0x99:
        return (out_left_ >> 8) & 0x03;
    case 0x9A:
        return (out_left_ + out_right_) & 0xFF;
    case 0x9B:
        return ((out_left_ + out_right_) >> 8) & 0x07;
    default:
        return io_[port];
    }
}

// The channel's sample period elapsed: next wavetable sample (channel 4 in noise mode also clocks the LFSR).
void Apu::step_channel(int ch) {
    index_[ch] = (index_[ch] + 1) & 31;
    if (ch == 3 && (io_[0x8E] & 0x10)) { // the LFSR runs whenever channel 4 and the LFSR are enabled
        int tap = kTap[io_[0x8E] & 7];
        uint16_t bit = uint16_t(~(((lfsr_ >> 7) ^ (lfsr_ >> tap)) & 1) & 1);
        lfsr_ = uint16_t(((lfsr_ << 1) | bit) & 0x7FFF);
    }
}

void Apu::run(int cycles) {
    while (cycles > 0) {
        int n = kCyclesPerSample - sample_cycles_;
        if (n > cycles)
            n = cycles;
        const uint8_t ctrl = io_[0x90];
        for (int ch = 0; ch < 4; ++ch) {
            if (!(ctrl & (1 << ch)))
                continue;
            // the period timer counts down; at zero the next sample is taken and the timer reloads (so a
            // newly enabled channel steps on its first cycle)
            for (int left = n; left > 0;) {
                if (counter_[ch] == 0) {
                    step_channel(ch);
                    counter_[ch] = 2048 - frequency(ch);
                }
                int d = counter_[ch] < left ? counter_[ch] : left;
                counter_[ch] -= d;
                left -= d;
            }
        }
        // channel 3 sweep, 375 Hz ticks
        if ((ctrl & 0x44) == 0x44) {
            sweep_cycles_ += n;
            while (sweep_cycles_ >= kSweepPeriod) {
                sweep_cycles_ -= kSweepPeriod;
                if (++sweep_ticks_ > (io_[0x8D] & 0x1F)) {
                    sweep_ticks_ = 0;
                    int f = (frequency(2) + int8_t(io_[0x8C])) & 0x7FF;
                    io_[0x84] = f & 0xFF;
                    io_[0x85] = uint8_t(f >> 8);
                }
            }
        }
        sample_cycles_ += n;
        cycles -= n;
        if (sample_cycles_ >= kCyclesPerSample) {
            sample_cycles_ -= kCyclesPerSample;
            mix();
        }
    }
}

void Apu::mix() {
    const uint8_t ctrl = io_[0x90];
    const uint32_t wave_base = uint32_t(io_[0x8F]) << 6;
    int left = 0, right = 0;
    int ch_left[4] = {}, ch_right[4] = {}; // per-channel contributions, for output_gain
    for (int ch = 0; ch < 4; ++ch) {
        const uint8_t vol = io_[0x88 + ch];
        if (ch == 1 && (ctrl & 0x20)) { // channel 2 voice: 8-bit PCM from port 0x89
            int s = vol, sel = io_[0x94];
            ch_left[ch] = (sel & 0x04) ? s : (sel & 0x08) ? s >> 1 : 0;
            ch_right[ch] = (sel & 0x01) ? s : (sel & 0x02) ? s >> 1 : 0;
            left += ch_left[ch];
            right += ch_right[ch];
            continue;
        }
        if (!(ctrl & (1 << ch)))
            continue;
        int sample;
        if (ch == 3 && (ctrl & 0x80)) {
            sample = (lfsr_ & 1) ? 15 : 0;
        } else {
            uint8_t b = ram_[(wave_base + ch * 16 + index_[ch] / 2) & 0xFFFF];
            sample = (index_[ch] & 1) ? b >> 4 : b & 15;
        }
        ch_left[ch] = sample * (vol >> 4);
        ch_right[ch] = sample * (vol & 15);
        left += ch_left[ch];
        right += ch_right[ch];
    }
    out_left_ = uint16_t(left & 0x3FF);
    out_right_ = uint16_t(right & 0x3FF);
    // headphone output: 16-bit (the sum << 5), with a DC-blocking high-pass for playback
    double l = double(out_left_ << 5), r = double(out_right_ << 5);
    if (output_gain[0] != 1.0f || output_gain[1] != 1.0f || output_gain[2] != 1.0f ||
        output_gain[3] != 1.0f) {
        double gl = 0, gr = 0;
        for (int ch = 0; ch < 4; ++ch) {
            gl += ch_left[ch] * double(output_gain[ch]);
            gr += ch_right[ch] * double(output_gain[ch]);
        }
        l = gl * 32;
        r = gr * 32;
    }
    double el = 0, er = 0;
    if (effect_channels) {
        const uint8_t channels = effect_channels();
        for (int ch = 0; ch < 4; ++ch)
            if (channels & (1 << ch)) {
                el += ch_left[ch] * double(output_gain[ch]) * 32;
                er += ch_right[ch] * double(output_gain[ch]) * 32;
            }
        l -= el;
        r -= er;
    }
    effect_dc_l_ = el - effect_prev_l_ + 0.999 * effect_dc_l_;
    effect_dc_r_ = er - effect_prev_r_ + 0.999 * effect_dc_r_;
    effect_prev_l_ = el;
    effect_prev_r_ = er;
    dc_l_ = l - prev_l_ + 0.999 * dc_l_;
    dc_r_ = r - prev_r_ + 0.999 * dc_r_;
    prev_l_ = l;
    prev_r_ = r;
    auto clamp = [](double v) { return int16_t(v > 32767 ? 32767 : v < -32768 ? -32768 : v); };
    if (samples_.size() < 1 << 20) {
        samples_.push_back(clamp(dc_l_));
        samples_.push_back(clamp(dc_r_));
        if (effect_channels) {
            effects_.push_back(clamp(effect_dc_l_));
            effects_.push_back(clamp(effect_dc_r_));
        }
    }
}

std::vector<int16_t> Apu::take() {
    std::vector<int16_t> out;
    out.swap(samples_);
    return out;
}

std::vector<int16_t> Apu::take_effects() {
    std::vector<int16_t> out;
    out.swap(effects_);
    return out;
}

} // namespace sawgod::emu
