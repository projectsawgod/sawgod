#include "app/sound_mix.h"

#include "emu/wsc.h"
#include "game/bank3d_sound.h"
#include "game/machine.h"
#include <algorithm>
#include <cmath>

namespace sawgod::app {

namespace {
constexpr uint16_t kChannels = 0x062C; // sound_channels: 8 logical channel structs (symbols/ram.toml)
constexpr uint16_t kChannelSize = 0x32;
constexpr int kSfxSlots = 4; // slots 0-3 carry sound effects, 4-7 songs
constexpr int kCyclesPerFrame = 159 * 256;
} // namespace

namespace {
uint8_t effect_channels(sawgod::emu::Wsc &wsc, uint8_t music_slots) {
    uint8_t claimed = 0, sfx = 0;
    for (int slot = 0; slot < 8; ++slot) {
        const uint16_t base = uint16_t(kChannels + slot * kChannelSize);
        const uint16_t step = uint16_t(wsc.ram[base] | wsc.ram[base + 1] << 8);
        if (step == 0xFFFF) // inactive
            continue;
        const int hw = wsc.ram[base + 2] & 3;
        if (claimed & (1 << hw))
            continue; // a lower slot already drives this channel (sound_tick's retrigger mask)
        claimed |= uint8_t(1 << hw);
        if (slot < kSfxSlots && !(music_slots & (1 << slot)))
            sfx |= uint8_t(1 << hw);
    }
    return sfx;
}

} // namespace

uint8_t sfx_channels(sawgod::emu::Wsc &wsc, uint8_t music_slots) { return effect_channels(wsc, music_slots); }

float SlowMusicFade::gain(int frame) {
    if (frame >= kFrames)
        return 0;
    if (frame <= kDelay)
        return 1;
    frame -= kDelay;
    constexpr int end = kFrames - kDelay - kZeroFrames;
    if (frame > end)
        return std::pow(10.0f, -2.5f) * float(kFrames - kDelay - frame) / kZeroFrames;
    return std::pow(10.0f, -2.5f * float(frame) / end);
}
SlowMusicFade::Frame SlowMusicFade::frame(uint8_t mode) {
    if (mode != 2)
        reset();
    if (!active_)
        return {};
    const float first = gain(elapsed_);
    elapsed_ = std::min(elapsed_ + 1, kFrames);
    return {first, gain(elapsed_), true};
}

DriverMusicFade driver_music_fade(const sawgod::emu::Wsc &wsc) {
    DriverMusicFade out;
    const int mode = wsc.ram[0x624];
    if (mode != 1 && mode != 2)
        return out;
    out.engaged = true;
    game::Machine m(const_cast<emu::Wsc &>(wsc)); // Reads only, including the supplied sound bank's tables.
    out.gain = 0;
    for (int slot = 4; slot < 8; ++slot) {
        const size_t b = size_t(kChannels + slot * kChannelSize);
        const int original = wsc.ram[b + 0x2C];
        const bool active = (wsc.ram[b] | wsc.ram[b + 1] << 8) != 0xFFFF;
        int full = (original << 4) | (original >> 4);
        const int envelope = wsc.ram[b + 0xD], duration = wsc.ram[b + 0xE];
        const int phase = wsc.ram[b + 0xF];
        if (envelope && duration && phase) {
            // Volume service precedes the phase increment. A freshly decoded note (phase 0)
            // instead installs its direct volume; a saturated phase remains at the last table cell.
            const int previous = phase < duration ? phase - 1 : duration;
            const int fraction = std::min(15, previous * 16 / duration);
            const int shape = m.rd8(0xD000, uint16_t(0x01E0 + (envelope | fraction))) & 0xF0;
            full = m.rd8(0xD000, uint16_t(0xCF36 + (shape | original)));
        }
        const int cap = wsc.ram[b + 0x12], heard = wsc.ram[b + 0x28];
        const auto power = [cap](int volume) {
            const int left = std::min(volume >> 4, cap >> 4);
            const int right = std::min(volume >> 4, cap & 15);
            return left * left + right * right;
        };
        const int reference = power(full);
        out.reference_volume[size_t(slot)] =
            uint8_t(std::min(full >> 4, cap >> 4) << 4 | std::min(full >> 4, cap & 15));
        // Follow the volume actually heard, including envelope quantization and pan ceilings.
        // A volume-index change alone need not change a held note's cached hardware volume yet.
        out.slots[size_t(slot)] =
            active && reference ? std::min(1.0f, std::sqrt(float(power(heard)) / reference)) : 0;
        out.gain = std::max(out.gain, out.slots[size_t(slot)]);
    }
    return out;
}

float driver_terminal_music_gain(const sawgod::emu::Wsc &wsc, uint8_t slot) {
    if (slot >= 8)
        return 1;
    const size_t b = size_t(kChannels + slot * kChannelSize);
    const int step = wsc.ram[b] | wsc.ram[b + 1] << 8;
    if (step == 0xFFFF)
        return 0;
    if (!step)
        return 1;
    game::Machine m(const_cast<emu::Wsc &>(wsc));
    uint16_t next = uint16_t((wsc.ram[b + 3] | wsc.ram[b + 4] << 8) + step * 2);
    for (int words = 0; words < 256; ++words, next = uint16_t(next + 2)) {
        const int op = m.rd8(0xD000, next);
        if (op == 0xFF) {
            const int cap = wsc.ram[b + 0x12], level = wsc.ram[b + 8];
            const auto power = [cap](int volume) {
                const int left = std::min(volume, cap >> 4), right = std::min(volume, cap & 15);
                return left * left + right * right;
            };
            const int reference = power(level);
            return reference ? std::min(1.0f, std::sqrt(float(power(wsc.ram[b + 0x28] >> 4)) / reference))
                             : 0;
        }
        if ((op < 0xA0 && ((op & 15) < 12 || op == 0x7F)) || (op >= 0xB0 && op < 0xC0) ||
            (op >= 0xF0 && op != 0xFD))
            return 1;
    }
    return 1;
}

void apply_channel_gains(sawgod::emu::Wsc &wsc, float music_gain, float sfx_gain, uint8_t music_slots,
                         uint8_t mapped_effect_slots, float gb_weight) {
    const uint8_t sfx = effect_channels(wsc, music_slots);
    uint8_t claimed = 0, mapped = 0;
    for (int slot = 0; slot < 8; ++slot) {
        const size_t b = size_t(kChannels + slot * kChannelSize);
        if ((wsc.ram[b] | wsc.ram[b + 1] << 8) == 0xFFFF)
            continue;
        const uint8_t hw = uint8_t(1 << (wsc.ram[b + 2] & 3));
        if (!(claimed & hw) && (mapped_effect_slots & (1 << slot)))
            mapped |= hw;
        claimed |= hw;
    }
    for (int ch = 0; ch < 4; ++ch)
        wsc.apu.output_gain[ch] =
            (sfx & (1 << ch)) ? sfx_gain * ((mapped & (1 << ch)) ? 1 - gb_weight : 1) : music_gain;
}

void paused_tick(sawgod::emu::Wsc &wsc) {
    // Call sound_tick as irq_vblank does (far call, AX = DS = DX = 0), from outside the game: save the
    // registers and the 4 stack bytes below SP the far call's return address uses, restore both after.
    const sawgod::emu::CpuState saved = wsc.cpu.s;
    sawgod::game::Machine m(wsc);
    const uint16_t ss = m.ss(), sp = m.sp();
    const uint8_t below[4] = {m.rd8(ss, uint16_t(sp - 4)), m.rd8(ss, uint16_t(sp - 3)),
                              m.rd8(ss, uint16_t(sp - 2)), m.rd8(ss, uint16_t(sp - 1))};
    m.wr16(ss, uint16_t(sp - 2), m.cs());
    m.wr16(ss, uint16_t(sp - 4), sawgod::game::Machine::kTrampolineIp);
    m.sp() = uint16_t(sp - 4);
    m.cs() = 0xD000;
    m.ax() = 0;
    m.ds() = 0;
    m.dx() = 0;
    sawgod::game::sound_tick(m); // ends with RETF
    wsc.cpu.s = saved;
    for (int i = 0; i < 4; ++i)
        m.wr8(ss, uint16_t(sp - 4 + i), below[i]);
    wsc.apu.run(kCyclesPerFrame);
}

void apply_cue_gain(sawgod::emu::Wsc &wsc, uint8_t cue_slots, float music_gain) {
    uint8_t claimed = 0;
    for (int slot = 0; slot < 8; ++slot) {
        const size_t b = size_t(kChannels + slot * kChannelSize);
        if ((wsc.ram[b] | wsc.ram[b + 1] << 8) == 0xFFFF)
            continue;
        const int hw = wsc.ram[b + 2] & 3;
        if (!(claimed & (1 << hw)) && (cue_slots & (1 << slot)))
            wsc.apu.output_gain[size_t(hw)] = music_gain;
        claimed |= uint8_t(1 << hw);
    }
}

} // namespace sawgod::app
