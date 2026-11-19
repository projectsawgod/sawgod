#include "app/gb_cues.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace sawgod::app {
namespace {
constexpr size_t kWscBank = 0x3D0000;
constexpr size_t kGbBank = 7 * 0x4000, kNewBank = 8 * 0x4000;
constexpr int kTable = 0x6E00, kTracks = kTable + 21 * 6;
constexpr std::array<int, 15> kLengths = {96, 72, 48, 36, 32, 24, 18, 16, 12, 9, 8, 6, 4, 3, 2};

std::optional<GbCueVoice> read_voice(std::span<const uint8_t> rom, int track, int max_pitch = 96) {
    GbCueVoice out;
    if (rom[kWscBank + size_t(track + 3)] != 0)
        return {}; // no PCM/noise voices in the retained parts
    std::map<int, int> loops;
    uint8_t volume = rom[kWscBank + size_t(track + 2)] & 15;
    uint8_t instrument = rom[kWscBank + size_t(track + 1)];
    bool half = (rom[kWscBank + size_t(track)] & 0x10) != 0;
    int ceiling = 15;
    for (int pc = track + 4, words = 0; words < 2048 && pc < 0xFFFC; ++words) {
        const uint8_t op = rom[kWscBank + size_t(pc++)], arg = rom[kWscBank + size_t(pc++)];
        if (op < 0xA0) {
            if (op == 0x7F || arg < 2)
                return {}; // absolute-period ties and one-tick notes need another encoding
            const int pitch = (op & 15) >= 12 ? -1 : 36 + (op >> 4) * 12 + (op & 15) - (half ? 12 : 0);
            if (pitch >= max_pitch || (pitch >= 0 && pitch < 36))
                return {};
            out.notes.push_back({pitch, arg, instrument, uint8_t(std::min(int(volume), ceiling))});
        } else if (op == 0xFD) {
            loops[arg & 15] = int(out.notes.size());
        } else if (op == 0xB0 && (arg & 0xF0) == 0xF0) {
            const auto it = loops.find(arg & 15);
            if (it == loops.end() || it->second == int(out.notes.size()))
                return {};
            out.loop_note = it->second;
            return out;
        } else if (op == 0xFF) {
            return out;
        } else if (op >= 0xB0 && op < 0xC0) {
            return {}; // no counted/nested repeats in these cues
        } else if ((op == 0xA1 || op == 0xA4) && arg != 0) {
            return {}; // hardware sweep or PCM/noise needs a separate GB arrangement
        } else if (op == 0xA0) {
            volume = std::max(arg & 15, arg >> 4);
        } else if (op == 0xA2) {
            instrument = arg;
        } else if (op == 0xA5) {
            ceiling = std::max(arg & 15, arg >> 4);
        } else if (op == 0xAE) {
            half = (arg & 0x10) != 0;
        } else if (op == 0xAF || op == 0xA7 || op >= 0xD0) {
            return {}; // no per-voice tempo changes, duration-only notes or pitch bends here
        } else if (!(op >= 0xA0 && op <= 0xA6) && !(op >= 0xC0 && op < 0xD0)) {
            return {};
        }
    }
    return {};
}

// Per-cue articulation (cue 136, for example, wants more attack and space between notes): the pulses'
// envelope sequence, and a rest cut from the end of each pulse note at least `gap` + 4 ticks long.
struct Articulation {
    int pulse_envelope = 1, gap = 0;
};
constexpr std::array<Articulation, 3> kArticulation = {{{1, 0}, {12, 2}, {1, 0}}};

bool emit_voice(std::vector<uint8_t> &image, int &pc, const GbCueVoice &voice, int channel,
                Articulation art) {
    auto byte = [&](int v) {
        if (pc >= 0x7D00) // reserve the private ordinary-effect tables/scripts
            return false;
        image[kNewBank + size_t(pc++ - 0x4000)] = uint8_t(v);
        return true;
    };
    auto command = [&](int op, int arg) { return byte(op) && byte(arg); };
    // E1-EF volume scale; F7 selects envelope/pitch/duty/routing sequences. Envelope 1
    // sustains the pulses; envelope 0 gives the bass arpeggio a light decay. No pitch modulation.
    if (!command(0xF7, channel == 2 ? 0 : art.pulse_envelope) || !command(0xF7, 0x40) ||
        !command(0xF7, channel == 1 ? 0x83 : 0x80) || !command(0xF7, 0xC0) ||
        (channel == 2 && !command(0xF8, 0))) // existing double-period wave 0
        return false;
    int loop = -1;
    for (size_t i = 0; i < voice.notes.size(); ++i) {
        if (int(i) == voice.loop_note)
            loop = pc;
        const auto &n = voice.notes[i];
        if (!byte(0xE0 + std::clamp(int(n.volume), 1, 15)))
            return false;
        if (n.pitch >= 0 && !byte(0xF0 + (n.pitch - 36) / 12))
            return false;
        const int gap = channel < 2 && n.pitch >= 0 && n.volume != 0 && n.ticks >= art.gap + 4 ? art.gap : 0;
        int left = n.ticks - gap;
        bool first = true;
        auto split = [&](int pitch_nibble, int ticks) {
            for (int rest = ticks; rest > 0;) {
                size_t d = 0;
                while (d < kLengths.size() && (kLengths[d] > rest || rest - kLengths[d] == 1))
                    ++d;
                if (d == kLengths.size() || !byte((pitch_nibble << 4) | int(d)))
                    return false;
                rest -= kLengths[d];
            }
            return true;
        };
        while (left > 0) {
            size_t duration = 0;
            // Reserve a representable tail: the driver's shortest note/hold is two ticks.
            while (duration < kLengths.size() &&
                   (kLengths[duration] > left || left - kLengths[duration] == 1))
                ++duration;
            if (duration == kLengths.size())
                return false;
            const int pitch = n.pitch < 0 || n.volume == 0 ? 12 : first ? (n.pitch - 36) % 12 : 13;
            if (!byte((pitch << 4) | int(duration)))
                return false;
            left -= kLengths[duration];
            first = false;
        }
        if (gap && !split(12, gap))
            return false;
    }
    // FE lo hi is an unconditional jump; FF terminates a voice.
    // A rest before FF lets the final note finish: FF cuts the hardware immediately,
    // while note/rest decoding precedes its register writes by one tick.
    return loop >= 0 ? command(0xFE, loop & 255) && byte(loop >> 8) : byte(0xCE) && byte(0xFF);
}
} // namespace

int gb_cue_gap(uint8_t song, int channel) {
    return song >= 135 && song <= 137 && channel < 2 ? kArticulation[size_t(song - 135)].gap : 0;
}

std::optional<GbCue> gb_read_cue(std::span<const uint8_t> wsc, uint8_t song) {
    if (wsc.size() < 0x3E0000 || song < 135 || song > 137)
        return {};
    auto word = [&](int a) { return int(wsc[kWscBank + size_t(a)] | wsc[kWscBank + size_t(a + 1)] << 8); };
    const int record = word(0x02E0 + song * 3);
    if (record > 512 || wsc[kWscBank + 0x02E2 + size_t(song) * 3] != 4)
        return {};
    // 135: drop slot 7, another delayed melody double. Slots 4/6 carry the melody/harmony,
    // slot 5 the low arpeggio. 136/137 have only three tonal parts; their fourth is all rests.
    constexpr std::array<std::array<int, 3>, 3> parts = {{{0, 2, 1}, {1, 0, 2}, {1, 2, 0}}};
    GbCue cue;
    cue.song = uint8_t(song - 117);
    for (size_t ch = 0; ch < 3; ++ch) {
        const int track = word(0x0504 + (record + parts[size_t(song - 135)][ch]) * 6);
        if (track >= 0xFFFC)
            return {};
        auto voice = read_voice(wsc, track);
        if (!voice || voice->notes.empty())
            return {};
        cue.voices[ch] = std::move(*voice);
    }
    return cue;
}

bool gb_append_cues(std::vector<uint8_t> &gb, std::span<const uint8_t> wsc) {
    if (gb.size() != 0x20000)
        return false;
    std::array<GbCue, 3> cues;
    for (size_t i = 0; i < cues.size(); ++i) {
        auto cue = gb_read_cue(wsc, uint8_t(135 + i));
        if (!cue)
            return false;
        cues[i] = std::move(*cue);
    }
    auto image = gb;
    image.insert(image.end(), gb.begin() + std::ptrdiff_t(kGbBank), gb.end());
    // Relocate the 18-entry table (including silent song 0), preserving all existing pointers.
    std::copy_n(gb.begin() + std::ptrdiff_t(kGbBank + 0x062F), 18 * 6,
                image.begin() + std::ptrdiff_t(kNewBank + kTable - 0x4000));
    image[kNewBank + 0x0070] = kTable & 255; // LD HL,song table at 7:406F
    image[kNewBank + 0x0071] = kTable >> 8;
    int pc = kTracks;
    for (const auto &cue : cues)
        for (size_t ch = 0; ch < 3; ++ch) {
            const size_t entry = kNewBank + kTable - 0x4000 + cue.song * 6 + ch * 2;
            image[entry] = uint8_t(pc);
            image[entry + 1] = uint8_t(pc >> 8);
            if (!emit_voice(image, pc, cue.voices[ch], int(ch), kArticulation[size_t(cue.song - 18)]))
                return false;
        }
    gb = std::move(image);
    return true;
}

std::optional<GbEffectSource> gb_read_effect(std::span<const uint8_t> wsc, uint8_t effect) {
    if (wsc.size() < 0x3E0000)
        return {};
    auto word = [&](size_t a) { return int(wsc[a] | wsc[a + 1] << 8); };
    const size_t entry = kWscBank + 0x0316 + size_t(effect) * 3;
    if (wsc[entry + 2] != 1)
        return {};
    const int record = word(entry);
    if (record > 512)
        return {};
    const int track = word(kWscBank + 0x0504 + size_t(record) * 6);
    if (track >= 0xFFFC)
        return {};
    auto voice = read_voice(wsc, track, 128); // register scripts have no song-octave bytecode limit
    if (!voice || voice->loop_note >= 0)
        return {};
    return GbEffectSource{std::move(*voice), wsc[kWscBank + size_t(track)] & 15};
}

bool gb_append_effects(std::vector<uint8_t> &gb, std::span<const uint8_t> wsc) {
    if (gb.size() != 0x20000 && gb.size() != 0x24000)
        return false;
    auto image = gb;
    if (image.size() == 0x20000)
        image.insert(image.end(), gb.begin() + std::ptrdiff_t(kGbBank), gb.end());
    constexpr int pulse_table = 0x7D00, noise_table = pulse_table + 41 * 2;
    int pc = noise_table + 41 * 2;
    auto byte = [&](int value) {
        if (pc >= 0x8000)
            return false;
        image[kNewBank + size_t(pc++ - 0x4000)] = uint8_t(value);
        return true;
    };
    auto word = [&](int address, int value) {
        image[kNewBank + size_t(address - 0x4000)] = uint8_t(value);
        image[kNewBank + size_t(address + 1 - 0x4000)] = uint8_t(value >> 8);
    };
    std::copy_n(gb.begin() + std::ptrdiff_t(kGbBank + 0x2BC7), 36 * 2,
                image.begin() + std::ptrdiff_t(kNewBank + pulse_table - 0x4000));
    std::copy_n(gb.begin() + std::ptrdiff_t(kGbBank + 0x2C0F), 36 * 2,
                image.begin() + std::ptrdiff_t(kNewBank + noise_table - 0x4000));
    word(0x6AB5, pulse_table); // LD HL,pulse table
    word(0x6AC3, noise_table); // LD HL,noise table
    auto pulse = [&](int pitch, int frames, int volume) {
        const int frequency =
            pitch < 0 ? 0 : int(std::lround(2048 - 131072.0 / (440 * std::pow(2.0, (pitch - 69) / 12.0))));
        return frequency >= 0 && frequency <= 2047 && frames >= 1 && frames < 0xE0 && byte(frames) &&
               byte(0) && byte(0x40) && byte(volume ? (volume << 4) | 1 : 0) && byte(frequency & 255) &&
               byte((frequency >> 8) | 0x80);
    };
    const std::array<uint8_t, 4> effects = {146, 147, 148, 42};
    for (size_t i = 0; i < effects.size(); ++i) {
        auto source = gb_read_effect(wsc, effects[i]);
        if (!source || source->voice.notes.empty())
            return false;
        word(pulse_table + int(36 + i) * 2, pc);
        double elapsed = 0;
        int previous = 0;
        for (const auto &note : source->voice.notes) {
            // Preserve elapsed WSC time, quantized to native GB effect frames.
            elapsed +=
                note.ticks * (4194304.0 / 70224) / (3072000.0 / (159 * 256) * (16 - source->skip) / 16.0);
            const int end = int(std::lround(elapsed));
            if (!pulse(note.pitch, std::max(1, end - previous), note.pitch < 0 ? 0 : note.volume))
                return false;
            previous = end;
        }
        if (!byte(0))
            return false;
        word(noise_table + int(36 + i) * 2, 0x6C57); // existing empty script
    }
    word(pulse_table + 40 * 2, pc);
    // A new GB-style descending death cue, not a claim of an original GB death effect.
    for (int pitch : {60, 55, 48})
        if (!pulse(pitch, 8, 10))
            return false;
    if (!byte(0))
        return false;
    word(noise_table + 40 * 2, pc);
    if (!byte(24) || !byte(0xA2) || !byte(0x54) || !byte(0))
        return false;
    gb = std::move(image);
    return true;
}
} // namespace sawgod::app
