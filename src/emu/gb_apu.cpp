#include "emu/gb_apu.h"

#include <algorithm>
#include <cmath>

namespace sawgod::emu {

namespace {
constexpr uint8_t kDuty[4] = {0x01, 0x81, 0x87, 0x7E}; // bit (7 - step): 12.5%, 25%, 50%, 75%
constexpr int kNoiseDivisor[8] = {8, 16, 32, 48, 64, 80, 96, 112};
constexpr int kSeqPeriod = GbApu::kClock / 512;
// Register index helpers (offset from FF10).
constexpr int kNr10 = 0x00, kNr50 = 0x14, kNr51 = 0x15, kNr52 = 0x16, kWave = 0x20;
// Each channel's first register (NRx0 position; pulse 2 has no NRx0, so its NR21 is base + 1).
constexpr int kBase[4] = {0x00, 0x05, 0x0A, 0x0F};
// The DC-blocking capacitor's charge factor per T-cycle (Pan Docs: 0.999958 on DMG).
const double kHpPerCycle = 0.999958;
} // namespace

void GbApu::set_sample_rate(int rate) {
    rate_ = rate;
    cycles_per_sample_ = double(kClock) / rate;
    charge_ = std::pow(kHpPerCycle, cycles_per_sample_);
    acc_l_ = acc_r_ = acc_cycles_ = 0;
    samples_.clear();
    effects_.clear();
    effect_acc_l_ = effect_acc_r_ = effect_hp_l_ = effect_hp_r_ = 0;
}

void GbApu::reset() {
    music_fade(1, 1, 1);
    reg_.fill(0);
    for (Channel &c : ch_)
        c = Channel{};
    power_ = true;
    seq_timer_ = kSeqPeriod;
    seq_step_ = 0;
    acc_l_ = acc_r_ = acc_cycles_ = 0;
    hp_l_ = hp_r_ = 0;
    samples_.clear();
    effects_.clear();
    effect_acc_l_ = effect_acc_r_ = effect_hp_l_ = effect_hp_r_ = 0;
    charge_ = std::pow(kHpPerCycle, cycles_per_sample_);
}

int GbApu::period(int ch) const {
    const Channel &c = ch_[ch];
    switch (ch) {
    case 0:
    case 1:
        return (2048 - c.freq) * 4;
    case 2:
        return (2048 - c.freq) * 2;
    default:
        return kNoiseDivisor[c.divisor] << c.clock_shift;
    }
}

int GbApu::sweep_calc() {
    Channel &c = ch_[0];
    int f = c.shadow >> c.sweep_shift;
    f = c.sweep_down ? c.shadow - f : c.shadow + f;
    if (f > 2047)
        c.on = false;
    return f;
}

void GbApu::trigger(int ch) {
    Channel &c = ch_[ch];
    c.on = c.dac;
    if (c.length == 0)
        c.length = ch == 2 ? 256 : 64;
    c.timer = period(ch);
    if (ch == 2) {
        c.pos = 0;
        return;
    }
    const uint8_t env = reg_[size_t(kBase[ch] + 2)];
    c.volume = env >> 4;
    c.env_up = (env & 0x08) != 0;
    c.env_period = env & 7;
    c.env_timer = c.env_period;
    if (ch == 3)
        c.lfsr = 0x7FFF;
    if (ch == 0) {
        c.shadow = c.freq;
        c.sweep_timer = c.sweep_period ? c.sweep_period : 8;
        c.sweep_on = c.sweep_period != 0 || c.sweep_shift != 0;
        if (c.sweep_shift)
            sweep_calc();
    }
}

void GbApu::write(uint16_t addr, uint8_t v) {
    if (addr < 0xFF10 || addr > 0xFF3F)
        return;
    const int i = addr - 0xFF10;
    if (i >= kWave) { // wave RAM works with the power off too
        reg_[size_t(i)] = v;
        return;
    }
    if (i == kNr52) {
        const bool on = (v & 0x80) != 0;
        if (power_ && !on) { // power off clears the registers and stops every channel
            for (int r = 0; r < kNr52; ++r)
                reg_[size_t(r)] = 0;
            for (Channel &c : ch_) {
                const int length = c.length;
                c = Channel{};
                c.length = length;
            }
        }
        if (!power_ && on)
            seq_step_ = 0;
        power_ = on;
        reg_[size_t(i)] = v & 0x80;
        return;
    }
    if (!power_)
        return;
    reg_[size_t(i)] = v;
    const int ch = i < 5 ? 0 : i < 10 ? 1 : i < 15 ? 2 : i < 20 ? 3 : -1;
    if (ch < 0)
        return; // NR50 / NR51: read at mix time
    Channel &c = ch_[ch];
    switch (i - kBase[ch]) {
    case 0:
        if (ch == 0) {
            c.sweep_period = (v >> 4) & 7;
            c.sweep_down = (v & 0x08) != 0;
            c.sweep_shift = v & 7;
        } else if (ch == 2) {
            c.dac = (v & 0x80) != 0;
            if (!c.dac)
                c.on = false;
        }
        break;
    case 1:
        if (ch == 2) {
            c.length = 256 - v;
        } else {
            c.length = 64 - (v & 0x3F);
            if (ch != 3)
                c.duty = v >> 6;
        }
        break;
    case 2:
        if (ch == 2) {
            c.wave_shift = (v >> 5) & 3;
        } else {
            c.dac = (v & 0xF8) != 0;
            if (!c.dac)
                c.on = false;
        }
        break;
    case 3:
        if (ch == 3) {
            c.clock_shift = v >> 4;
            c.narrow = (v & 0x08) != 0;
            c.divisor = v & 7;
        } else {
            c.freq = (c.freq & 0x700) | v;
        }
        break;
    default: // NRx4
        if (ch != 3)
            c.freq = (c.freq & 0xFF) | ((v & 7) << 8);
        c.length_enable = (v & 0x40) != 0;
        if (v & 0x80)
            trigger(ch);
        break;
    }
}

uint8_t GbApu::read(uint16_t addr) const {
    if (addr < 0xFF10 || addr > 0xFF3F)
        return 0xFF;
    const int i = addr - 0xFF10;
    // Pan Docs "Audio Registers": the bits that read back as 1 (write-only or unused).
    static const uint8_t kOr[0x17] = {0x80, 0x3F, 0x00, 0xFF, 0xBF, 0xFF, 0x3F, 0x00, 0xFF, 0xBF, 0x7F, 0xFF,
                                      0x9F, 0xFF, 0xBF, 0xFF, 0xFF, 0x00, 0x00, 0xBF, 0x00, 0x00, 0x70};
    if (i >= kWave)
        return reg_[size_t(i)];
    if (i == kNr52) {
        uint8_t s = uint8_t((power_ ? 0x80 : 0) | 0x70);
        for (int ch = 0; ch < 4; ++ch)
            if (ch_[ch].on)
                s |= uint8_t(1 << ch);
        return s;
    }
    if (i > kNr52)
        return 0xFF;
    return uint8_t(reg_[size_t(i)] | kOr[i]);
}

int GbApu::volume(int ch) const {
    static const int kWaveLevel[4] = {0, 15, 8, 4};
    return ch == 2 ? kWaveLevel[ch_[2].wave_shift] : ch_[ch].volume;
}

int GbApu::digital(int ch) const {
    const Channel &c = ch_[ch];
    if (!c.on)
        return 0;
    switch (ch) {
    case 0:
    case 1:
        return ((kDuty[c.duty] >> (7 - c.pos)) & 1) ? c.volume : 0;
    case 2: {
        if (c.wave_shift == 0)
            return 0;
        const uint8_t b = reg_[size_t(kWave + c.pos / 2)];
        const int s = (c.pos & 1) ? (b & 0xF) : (b >> 4);
        return s >> (c.wave_shift - 1);
    }
    default:
        return (c.lfsr & 1) ? 0 : c.volume;
    }
}

void GbApu::clock_length() {
    for (Channel &c : ch_)
        if (c.length_enable && c.length > 0 && --c.length == 0)
            c.on = false;
}

void GbApu::clock_sweep() {
    Channel &c = ch_[0];
    if (--c.sweep_timer > 0)
        return;
    c.sweep_timer = c.sweep_period ? c.sweep_period : 8;
    if (!c.sweep_on || c.sweep_period == 0)
        return;
    const int f = sweep_calc();
    if (f <= 2047 && c.sweep_shift) {
        c.freq = c.shadow = f;
        reg_[size_t(kNr10 + 3)] = uint8_t(f);
        reg_[size_t(kNr10 + 4)] = uint8_t((reg_[size_t(kNr10 + 4)] & 0xF8) | (f >> 8));
        sweep_calc();
    }
}

void GbApu::clock_envelope() {
    for (int ch : {0, 1, 3}) {
        Channel &c = ch_[ch];
        if (c.env_period == 0)
            continue;
        if (--c.env_timer > 0)
            continue;
        c.env_timer = c.env_period;
        if (c.env_up && c.volume < 15)
            ++c.volume;
        else if (!c.env_up && c.volume > 0)
            --c.volume;
    }
}

// Adds `cycles` T-cycles of the current output to the sample accumulator, emitting finished samples.
void GbApu::mix_span(int cycles) {
    double l = 0, r = 0, el = 0, er = 0;
    const double position = std::min(1.0, (fade_elapsed_ + cycles * 0.5) / fade_cycles_);
    const double fade = fade_first_ + (fade_last_ - fade_first_) * position;
    fade_elapsed_ += cycles;
    if (power_) {
        const uint8_t nr51 = reg_[size_t(kNr51)], nr50 = reg_[size_t(kNr50)];
        for (int ch = 0; ch < 4; ++ch) {
            const Channel &c = ch_[ch];
            if (!c.dac || muted[size_t(ch)])
                continue;
            const double a = (1.0 - digital(ch) / 7.5) * output_gain[size_t(ch)] *
                             ((effect_channels & (1 << ch)) ? 1 : fade);
            const bool effect = split_effects && (effect_channels & (1 << ch));
            if (nr51 & (0x10 << ch))
                (effect ? el : l) += a;
            if (nr51 & (0x01 << ch))
                (effect ? er : r) += a;
        }
        l *= ((nr50 >> 4) & 7) + 1;
        r *= (nr50 & 7) + 1;
        el *= ((nr50 >> 4) & 7) + 1;
        er *= (nr50 & 7) + 1;
    }
    double left = cycles;
    while (left > 0) {
        const double take = std::min(left, cycles_per_sample_ - acc_cycles_);
        acc_l_ += l * take;
        acc_r_ += r * take;
        effect_acc_l_ += el * take;
        effect_acc_r_ += er * take;
        acc_cycles_ += take;
        left -= take;
        if (acc_cycles_ >= cycles_per_sample_ - 1e-9) {
            const double in_l = acc_l_ / acc_cycles_, in_r = acc_r_ / acc_cycles_;
            const double out_l = in_l - hp_l_, out_r = in_r - hp_r_;
            hp_l_ = in_l - out_l * charge_;
            hp_r_ = in_r - out_r * charge_;
            // 4 channels x 8 master-volume steps = 32 at most before the high-pass; half of full scale leaves
            // room for the DC steps of a channel panned on or off (a pop on hardware too)
            auto s16 = [](double v) { return int16_t(std::clamp(v * (32767.0 / 64.0), -32768.0, 32767.0)); };
            samples_.push_back(s16(out_l));
            samples_.push_back(s16(out_r));
            const double ein_l = effect_acc_l_ / acc_cycles_, ein_r = effect_acc_r_ / acc_cycles_;
            const double eout_l = ein_l - effect_hp_l_, eout_r = ein_r - effect_hp_r_;
            effect_hp_l_ = ein_l - eout_l * charge_;
            effect_hp_r_ = ein_r - eout_r * charge_;
            if (split_effects) {
                effects_.push_back(s16(eout_l));
                effects_.push_back(s16(eout_r));
            }
            effect_acc_l_ = effect_acc_r_ = 0;
            acc_l_ = acc_r_ = acc_cycles_ = 0;
        }
    }
}

void GbApu::run(int cycles) {
    while (cycles > 0) {
        int span = std::min(cycles, seq_timer_);
        for (int ch = 0; ch < 4; ++ch)
            if (ch_[ch].on)
                span = std::min(span, ch_[ch].timer);
        span = std::max(span, 1);
        mix_span(span);
        cycles -= span;
        seq_timer_ -= span;
        for (int ch = 0; ch < 4; ++ch) {
            Channel &c = ch_[ch];
            if (!c.on)
                continue;
            c.timer -= span;
            if (c.timer > 0)
                continue;
            c.timer += period(ch);
            if (ch == 3) {
                const int x = (c.lfsr ^ (c.lfsr >> 1)) & 1;
                c.lfsr = uint16_t((c.lfsr >> 1) | (x << 14));
                if (c.narrow)
                    c.lfsr = uint16_t((c.lfsr & ~0x40) | (x << 6));
            } else {
                c.pos = (c.pos + 1) & (ch == 2 ? 31 : 7);
            }
        }
        if (seq_timer_ <= 0) {
            seq_timer_ += kSeqPeriod;
            if (power_) {
                if ((seq_step_ & 1) == 0)
                    clock_length();
                if (seq_step_ == 2 || seq_step_ == 6)
                    clock_sweep();
                if (seq_step_ == 7)
                    clock_envelope();
            }
            seq_step_ = (seq_step_ + 1) & 7;
        }
    }
}

std::vector<int16_t> GbApu::take() {
    std::vector<int16_t> out;
    out.swap(samples_);
    return out;
}

std::vector<int16_t> GbApu::take_effects() {
    std::vector<int16_t> out;
    out.swap(effects_);
    return out;
}

} // namespace sawgod::emu
