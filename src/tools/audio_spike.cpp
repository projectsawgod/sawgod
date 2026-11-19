// Audio-swap spike: renders one song from each version with its
// loop found from the sound driver's state repeating, and logs per-frame channel state for the position
// map between the two arrangements.
//
//   sawgod-audio-spike pair WSC_ROM GB_ROM INPUTS FRAME WSC_SONG
//       compare a musical request against all 17 GB songs to check uncertain counterparts
//   sawgod-audio-spike align WSC_ROM GB_ROM INPUTS FRAME WSC_SONG GB_SONG [DEMO.wav] [MAP.json]
//       derive a periodic pitch map and voice pairing, report agreement over two WSC loops, and optionally
//       render WSC -> GB -> WSC swaps (0.5 s crossfades, including a loop seam) and write the map as JSON
//   sawgod-audio-spike sfx-survey WSC_ROM GB_ROM INPUTS FRAME OUT.csv
//       isolated effect pitch/channel traces: all 163 WSC effect records and 36 GB effects
//   sawgod-audio-spike gb-cues WSC_ROM OUTPUT_DIRECTORY
//       render the three load-time GB transcriptions with WSC/GB swaps (no game execution)
//   sawgod-audio-spike gb-sfx WSC_ROM OUTPUT_DIRECTORY
//       isolated runtime effect comparisons: WSC, separator, GB; includes a song loop seam
//   sawgod-audio-spike gb GB_ROM SONG OUT.wav [NOTES.csv] [LOOPS]
//       the GB song through our SM83 + APU core (emu/gb_song.h), 48 kHz
//   sawgod-audio-spike gb-locked GB_ROM SONG RATE FRAMES OUT.wav [NOTES.csv]
//       the GB song tick-locked to the WSC driver: FRAMES frames of 1/75.47 s, ticking as a WSC song whose
//       channels have tempo rate RATE (+0x06 low nibble: of every 16 frames, RATE skip their tick); with
//       RATE -1, FRAMES is a file of per-frame GB tick counts (a schedule from the position map)
//   sawgod-audio-spike gb-all GB_ROM
//       every song in the GB driver's table: loop points (no files)
//   sawgod-audio-spike wsc WSC_ROM INPUTS FRAME SONG OUT.wav [NOTES.csv] [LOOPS]
//       the native game run to FRAME with INPUTS (as sawgod-headless --inputs), then only the sound
//       driver (sound_request SONG, then sound_tick + the sound chip per frame, as the pause does), 24 kHz
//   sawgod-audio-spike wsc-songs WSC_ROM INPUTS FRAME FIRST LAST
//       tempo and loop of songs FIRST..LAST (no files)
//   sawgod-audio-spike wsc-events WSC_ROM SONG OUT_PREFIX [LOOPS [SLOT INSTR]]
//       the song's per-frame note events for the Enhanced-audio MIDI rendering (audio_enhanced.inc)
//       optional SLOT INSTR renders only that pair, with all other pairs muted
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "app/gb_cues.h"
#include "app/gb_music.h"
#include "app/sound_mix.h"
#include "emu/gb_song.h"
#include "emu/wsc.h"
#include "game/bank3d_sound.h"
#include "game/machine.h"
#include "game/session.h"

using namespace sawgod;

namespace {

std::vector<uint8_t> read_file(const std::string &path) {
    std::vector<uint8_t> data;
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
        return data;
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
        data.insert(data.end(), buf, buf + n);
    std::fclose(f);
    return data;
}

bool write_wav(const std::string &path, const std::vector<int16_t> &stereo, int rate) {
    std::vector<uint8_t> w;
    auto le = [&](uint32_t x, int n) {
        for (int i = 0; i < n; ++i)
            w.push_back(uint8_t(x >> (8 * i)));
    };
    auto tag = [&](const char *s) { w.insert(w.end(), s, s + 4); };
    const uint32_t bytes = uint32_t(stereo.size() * 2);
    tag("RIFF");
    le(36 + bytes, 4);
    tag("WAVE");
    tag("fmt ");
    le(16, 4);
    le(1, 2);
    le(2, 2);
    le(uint32_t(rate), 4);
    le(uint32_t(rate * 4), 4);
    le(4, 2);
    le(16, 2);
    tag("data");
    le(bytes, 4);
    for (int16_t s : stereo)
        le(uint16_t(s), 2);
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    std::fwrite(w.data(), 1, w.size(), f);
    std::fclose(f);
    return true;
}

uint16_t parse_buttons(const std::string &s) {
    static const std::map<std::string, uint16_t> names = {
        {"up", emu::KEY_X1},       {"right", emu::KEY_X2}, {"down", emu::KEY_X3}, {"left", emu::KEY_X4},
        {"start", emu::KEY_START}, {"a", emu::KEY_A},      {"b", emu::KEY_B},
    };
    uint16_t keys = 0;
    size_t i = 0;
    while (i < s.size()) {
        size_t j = s.find_first_of(", \r\n", i);
        if (j == std::string::npos)
            j = s.size();
        auto it = names.find(s.substr(i, j - i));
        if (it != names.end())
            keys |= it->second;
        i = j + 1;
    }
    return keys;
}

void write_gb_notes(const char *path, const emu::GbSongRender &r);

int gb(int argc, char **argv) {
    if (argc < 5)
        return 2;
    const std::vector<uint8_t> rom = read_file(argv[2]);
    const int song = std::atoi(argv[3]);
    const int loops = argc > 6 ? std::atoi(argv[6]) : 2;
    const emu::GbSongRender r = emu::render_gb_song(rom, emu::saga1_sound_driver(), uint8_t(song), loops);
    std::printf(
        "gb song %d: tempo %d, loop after tick %d, %d ticks; loop starts in frame %d (%.3f s), %.3f frames "
        "(%.3f s); loop start sample %lld, length %lld samples @ %d Hz; %zu frames rendered\n",
        song, r.tempo, r.intro_ticks, r.loop_ticks, r.loop_start_frame,
        r.loop_start_frame / emu::GbSoundHost::kFrameRate, r.loop_frames,
        r.loop_frames / emu::GbSoundHost::kFrameRate, (long long)r.loop_start_sample,
        (long long)r.loop_length_samples, emu::GbApu::kSampleRate, r.frames.size());
    write_wav(argv[4], r.samples, emu::GbApu::kSampleRate);
    if (argc > 5)
        write_gb_notes(argv[5], r);
    return 0;
}

void write_gb_notes(const char *path, const emu::GbSongRender &r) {
    FILE *f = std::fopen(path, "w");
    std::fprintf(f, "frame,ticks,ch,freq,volume,on,trigger\n");
    for (size_t i = 0; i < r.frames.size(); ++i)
        for (int ch = 0; ch < 4; ++ch) {
            const auto &c = r.frames[i][size_t(ch)];
            std::fprintf(f, "%zu,%d,%d,%u,%u,%d,%d\n", i, r.ticks_at_frame_end[i], ch, c.freq, c.volume,
                         c.on ? 1 : 0, c.trigger ? 1 : 0);
        }
    std::fclose(f);
}

int gb_locked(int argc, char **argv) {
    if (argc < 7)
        return 2;
    const std::vector<uint8_t> rom = read_file(argv[2]);
    const int song = std::atoi(argv[3]), rate = std::atoi(argv[4]);
    std::vector<uint8_t> schedule;
    if (rate < 0) { // RATE -1: FRAMES names a file with each frame's tick count, one per line
        if (FILE *f = std::fopen(argv[5], "r")) {
            int n;
            while (std::fscanf(f, "%d", &n) == 1)
                schedule.push_back(uint8_t(n));
            std::fclose(f);
        }
    } else {
        const int frames = std::atoi(argv[5]);
        schedule.assign(size_t(frames), 0);
        int phase = 0;
        for (int f = 0; f < frames; ++f) { // frame 0 starts the song (the WSC driver's first notes)
            if (f > 0)
                phase += rate;
            schedule[size_t(f)] = phase >= 16 ? 0 : 1;
            if (phase >= 16)
                phase -= 16;
        }
    }
    constexpr double kWscFrameCycles = double(emu::GbApu::kClock) * (159 * 256) / 3072000.0;
    const emu::GbSongRender r =
        emu::render_gb_song_locked(rom, emu::saga1_sound_driver(), uint8_t(song), schedule, kWscFrameCycles);
    std::printf(
        "gb song %d locked (rate %d): loop after tick %d, %d ticks; loop starts in frame %d, %.0f frames; "
        "loop start sample %lld @ %d Hz\n",
        song, rate, r.intro_ticks, r.loop_ticks, r.loop_start_frame, r.loop_frames,
        (long long)r.loop_start_sample, emu::GbApu::kSampleRate);
    write_wav(argv[6], r.samples, emu::GbApu::kSampleRate);
    if (argc > 7)
        write_gb_notes(argv[7], r);
    return 0;
}

int gb_all(int argc, char **argv) {
    if (argc < 3)
        return 2;
    const std::vector<uint8_t> rom = read_file(argv[2]);
    for (int song = 0; song < 18; ++song) {
        const emu::GbSongRender r = emu::render_gb_song(rom, emu::saga1_sound_driver(), uint8_t(song), 1);
        std::printf("%2d tempo %3d intro %5d ticks loop %5d ticks = %.2f s + %.2f s\n", song, r.tempo,
                    r.intro_ticks, r.loop_ticks, r.loop_start_frame / emu::GbSoundHost::kFrameRate,
                    r.loop_frames / emu::GbSoundHost::kFrameRate);
    }
    return 0;
}

// The WSC driver's musical state: globals 0x600-0x62B and the 8 logical channels 0x62C-0x7AB, without
// the free-running frame counter 0x604 and each channel's tempo phase (+0x07: of every 16 frames, the
// channel's rate +0x06 & 0xF skip their tick), which follow the frames, not the music.
std::vector<uint8_t> wsc_music_state(const emu::Wsc &w) {
    std::vector<uint8_t> s(w.ram.begin() + 0x600, w.ram.begin() + 0x7AC);
    s[4] = 0;
    for (int slot = 0; slot < 8; ++slot)
        s[size_t(0x2C + slot * 0x32 + 7)] = 0;
    return s;
}

// Runs the native game to `start` with the inputs file (as sawgod-headless --inputs).
std::unique_ptr<emu::Wsc> wsc_boot(const char *rom_path, const char *inputs, int start) {
    std::vector<uint8_t> rom = read_file(rom_path);
    std::vector<std::pair<int, uint16_t>> changes;
    if (FILE *f = std::fopen(inputs, "r")) {
        char line[256];
        while (std::fgets(line, sizeof line, f)) {
            char *end;
            const long fr = std::strtol(line, &end, 10);
            if (end != line)
                changes.emplace_back(int(fr), parse_buttons(end));
        }
        std::fclose(f);
    }
    static const size_t kSram[] = {0, 0x2000, 0x8000, 0x20000, 0x40000, 0x80000};
    const uint8_t save_type = rom[rom.size() - 16 + 0x0B];
    auto w = std::make_unique<emu::Wsc>(rom, save_type <= 5 ? kSram[save_type] : 0);
    game::install(*w, nullptr);
    size_t next = 0;
    uint16_t held = 0;
    for (int frame = 0; frame < start; ++frame) {
        w->set_keys(held);
        w->run_frame();
        w->apu.take();
        while (next < changes.size() && changes[next].first <= frame + 1)
            held = changes[next++].second;
    }
    return w;
}

// sound_request(song) from outside the game, as wait_vblank forwards a request (registers put back).
void wsc_request(emu::Wsc &w, int song) {
    const emu::CpuState saved = w.cpu.s;
    game::Machine m(w);
    m.ax() = uint16_t(song);
    m.cs() = 0xD000;
    game::sound_request_core(m);
    w.cpu.s = saved;
}

// Where each song channel is in its sequence: step counter, track pointer, note countdown.
std::vector<uint8_t> wsc_positions(const emu::Wsc &w, int first, int count) {
    std::vector<uint8_t> s;
    for (int slot = first; slot < first + count; ++slot) {
        const size_t base = size_t(0x62C + slot * 0x32);
        for (size_t i : {0, 1, 3, 4, 9})
            s.push_back(w.ram[base + i]);
    }
    return s;
}

struct WscScan {
    int rate = -1, first_slot = 4, channels = 4;
    bool ended = false;
    int state_intro_ticks = -1;           // the whole music state first repeats after this tick
    int intro_ticks = -1, loop_ticks = 0; // the musical loop: positions after tick intro_ticks repeat
                                          // loop_ticks later (the earliest such tick)
    int loop_start_frame = -1, loop_end_frame = -1;
    int exact_loop_frames = 0; // whole driver state (with the phases) repeating
};

// Offline pair isolation: mirror paused_tick's driver call, then select playback
// gains AFTER the driver updates its instrument and BEFORE the APU runs. Muting
// ports or disabling channels would change oscillator/noise phases and readbacks.
// Keep the independent DC filter running across instrument changes and rests.
void wsc_stem_tick(emu::Wsc &w, int slot, int instrument) {
    const emu::CpuState saved = w.cpu.s;
    game::Machine m(w);
    const uint16_t ss = m.ss(), sp = m.sp();
    const uint8_t below[4] = {m.rd8(ss, uint16_t(sp - 4)), m.rd8(ss, uint16_t(sp - 3)),
                              m.rd8(ss, uint16_t(sp - 2)), m.rd8(ss, uint16_t(sp - 1))};
    m.wr16(ss, uint16_t(sp - 2), m.cs());
    m.wr16(ss, uint16_t(sp - 4), game::Machine::kTrampolineIp);
    m.sp() = uint16_t(sp - 4);
    m.cs() = 0xD000;
    m.ax() = m.ds() = m.dx() = 0;
    game::sound_tick(m);
    w.cpu.s = saved;
    for (int i = 0; i < 4; ++i)
        m.wr8(ss, uint16_t(sp - 4 + i), below[i]);
    w.apu.output_gain.fill(0.0f);
    const size_t base = size_t(0x62C + slot * 0x32);
    if (w.ram[base + 0x27] == instrument)
        w.apu.output_gain[w.ram[base + 2] & 3] = 1.0f;
    w.apu.run(159 * 256); // same driver-frame clock as app::paused_tick
}

// Runs only the driver (as the pause does) for up to max_frames after a request; the musical loop from the
// state repeating at a tick (the request's first surviving slot: its phase did not wrap). on_frame(frame,
// ticks) after each.
template <class F>
WscScan scan_wsc_song(emu::Wsc &w, int song, int max_frames, int loops, F on_frame, bool exact_ticks = false,
                      int isolate_slot = -1, int isolate_instrument = -1) {
    WscScan r;
    // Isolate the request: effect-slot jingles must not inherit the field song from the boot.
    const emu::CpuState saved = w.cpu.s;
    game::Machine m(w);
    m.cs() = 0xD000;
    m.ds() = 0;
    game::sound_init(m);
    w.cpu.s = saved;
    const int record = m.rd16(0xD000, uint16_t(0x02E0 + song * 3));
    r.channels = m.rd8(0xD000, uint16_t(0x02E2 + song * 3));
    if (r.channels < 1 || r.channels > 4)
        return r;
    r.first_slot = m.rd16(0xD000, uint16_t(0x0500 + record * 6)) / 0x32;
    int clock = 0x62C + r.first_slot * 0x32;
    wsc_request(w, song);
    std::map<std::vector<uint8_t>, std::pair<int, int>> seen; // music state -> tick, frame
    std::map<std::vector<uint8_t>, int> seen_exact;
    int ticks = 0, end = max_frames;
    std::vector<int> tick_frame;
    std::vector<std::vector<uint8_t>> positions(1); // after each tick (index = tick)
    for (int f = 0; f < end; ++f) {
        // A finite jingle can end its first voice before the others. Continue on a surviving voice,
        // whose tempo phase the driver still updates, rather than counting an inactive slot every frame.
        for (int slot = r.first_slot; slot < r.first_slot + r.channels; ++slot) {
            const int b = 0x62C + slot * 0x32;
            if ((w.ram[size_t(b)] | w.ram[size_t(b + 1)] << 8) != 0xFFFF) {
                clock = b;
                break;
            }
        }
        const int phase_before = w.ram[size_t(clock + 7)];
        if (isolate_slot >= 0)
            wsc_stem_tick(w, isolate_slot, isolate_instrument);
        else
            app::paused_tick(w);
        if (f == 0)
            r.rate = w.ram[size_t(clock + 6)] & 0xF;
        const bool ticked = f == 0 || w.ram[size_t(clock + 7)] >= phase_before;
        if (ticked) {
            ++ticks;
            tick_frame.push_back(f);
            positions.push_back(wsc_positions(w, r.first_slot, r.channels));
        }
        on_frame(f, ticks);
        bool active = false;
        for (int slot = r.first_slot; slot < r.first_slot + r.channels; ++slot) {
            const int b = 0x62C + slot * 0x32;
            active |= (w.ram[size_t(b)] | w.ram[size_t(b + 1)] << 8) != 0xFFFF;
        }
        if (!active) {
            r.ended = true;
            break;
        }
        if (ticked && r.intro_ticks < 0) {
            auto [it, fresh] = seen.emplace(wsc_music_state(w), std::make_pair(ticks, f));
            if (!fresh) {
                r.state_intro_ticks = it->second.first;
                r.loop_ticks = ticks - it->second.first;
                // the musical loop starts earlier: bytes like a loop anchor saved on the first pass make the
                // state differ though the music doesn't; take the earliest tick whose positions repeat from
                // there on
                int t = r.state_intro_ticks;
                while (t > 1 && positions[size_t(t - 1)] == positions[size_t(t - 1 + r.loop_ticks)])
                    --t;
                r.intro_ticks = t;
                r.loop_start_frame = tick_frame[size_t(r.intro_ticks)]; // the frame of tick intro_ticks + 1
                if (!exact_ticks)
                    end = std::min(max_frames, tick_frame[size_t(r.intro_ticks)] +
                                                   int(loops * r.loop_ticks * 16.0 / (16 - r.rate)) + 2);
            }
        }
        if (r.exact_loop_frames == 0) {
            std::vector<uint8_t> st(w.ram.begin() + 0x600, w.ram.begin() + 0x7AC);
            auto [it, fresh] = seen_exact.emplace(std::move(st), f);
            if (!fresh)
                r.exact_loop_frames = f - it->second;
        }
        // Enhanced renders stop on the actual sequencer clock, including tempo changes within a song.
        if (exact_ticks && r.loop_ticks && ticks >= r.intro_ticks + loops * r.loop_ticks + 1)
            break;
    }
    if (r.intro_ticks >= 0 && size_t(r.intro_ticks + r.loop_ticks) < tick_frame.size())
        r.loop_end_frame = tick_frame[size_t(r.intro_ticks + r.loop_ticks)];
    return r;
}

constexpr double kWscFrameRate = 3072000.0 / (159 * 256);

int wsc(int argc, char **argv) {
    if (argc < 7)
        return 2;
    const int start = std::atoi(argv[4]);
    const int song = std::atoi(argv[5]);
    const int loops = argc > 8 ? std::atoi(argv[8]) : 2;
    std::unique_ptr<emu::Wsc> wp = wsc_boot(argv[2], argv[3], start);
    emu::Wsc &w = *wp;
    game::Machine machine(w);
    const int record = machine.rd16(0xD000, uint16_t(0x02E0 + song * 3));
    const int first = machine.rd16(0xD000, uint16_t(0x0500 + record * 6)) / 0x32;
    const int channels = machine.rd8(0xD000, uint16_t(0x02E2 + song * 3));
    std::vector<int16_t> audio;
    FILE *log = argc > 7 ? std::fopen(argv[7], "w") : nullptr;
    if (log)
        std::fprintf(log, "frame,ticks,slot,step,hw,pitch,period,volume,rate,phase,dur\n");
    const WscScan r = scan_wsc_song(w, song, int(kWscFrameRate * 900), loops, [&](int f, int ticks) {
        std::vector<int16_t> a = w.apu.take();
        audio.insert(audio.end(), a.begin(), a.end());
        if (log)
            for (int slot = first; slot < first + channels; ++slot) {
                const int base = 0x62C + slot * 0x32;
                const int hw = w.ram[size_t(base + 2)] & 3;
                const int step = w.ram[size_t(base)] | w.ram[size_t(base + 1)] << 8;
                const int pitch = w.ram[size_t(base + 0x10)] | w.ram[size_t(base + 0x11)] << 8;
                const int period = (w.io[size_t(0x80 + hw * 2)] | w.io[size_t(0x81 + hw * 2)] << 8) & 0x7FF;
                std::fprintf(log, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", f, ticks, slot, step, hw, pitch,
                             period, w.io[size_t(0x88 + hw)], w.ram[size_t(base + 6)] & 0xF,
                             w.ram[size_t(base + 7)], w.ram[size_t(base + 9)]);
            }
    });
    if (log)
        std::fclose(log);
    const double loop_frames = r.rate < 16 ? r.loop_ticks * 16.0 / (16 - r.rate) : 0;
    std::printf(
        "wsc song %d: rate %d (%.4f ticks/frame), whole state repeats after tick %d; musical loop after "
        "tick %d, %d ticks; loop starts in frame %d "
        "(%.3f s, sample %d @ %d Hz), %.3f frames (%.3f s, %.1f samples)\n",
        song, r.rate, (16 - r.rate) / 16.0, r.state_intro_ticks, r.intro_ticks, r.loop_ticks,
        r.loop_start_frame, r.loop_start_frame / kWscFrameRate, r.loop_start_frame * 318,
        emu::Apu::kSampleRate, loop_frames, loop_frames / kWscFrameRate, loop_frames * 318);
    write_wav(argv[6], audio, emu::Apu::kSampleRate);
    return 0;
}

// Every song from FIRST to LAST: tempo rate and loop, from the same booted state.
int wsc_songs(int argc, char **argv) {
    if (argc < 7)
        return 2;
    std::unique_ptr<emu::Wsc> wp = wsc_boot(argv[2], argv[3], std::atoi(argv[4]));
    const emu::Wsc::Snapshot base = wp->snapshot();
    for (int song = std::atoi(argv[5]); song <= std::atoi(argv[6]); ++song) {
        wp->restore(base);
        const WscScan r =
            scan_wsc_song(*wp, song, int(kWscFrameRate * 900), 1, [&](int, int) { wp->apu.take(); });
        const double loop_frames = r.rate < 16 ? r.loop_ticks * 16.0 / (16 - r.rate) : 0;
        std::printf("%3d rate %2d intro %5d ticks loop %5d ticks = %.2f s + %.2f s\n", song, r.rate,
                    r.intro_ticks, r.loop_ticks, r.loop_start_frame / kWscFrameRate,
                    loop_frames / kWscFrameRate);
    }
    return 0;
}

#include "audio_align.inc"
#include "audio_enhanced.inc"
#include "audio_gb_cues.inc"
#include "audio_gb_sfx.inc"
#include "audio_sfx_survey.inc"

} // namespace

int main(int argc, char **argv) {
    const std::string cmd = argc > 1 ? argv[1] : "";
    int rc = 2;
    if (cmd == "gb")
        rc = gb(argc, argv);
    else if (cmd == "gb-locked")
        rc = gb_locked(argc, argv);
    else if (cmd == "gb-cues")
        rc = gb_cues(argc, argv);
    else if (cmd == "gb-sfx")
        rc = gb_sfx(argc, argv);
    else if (cmd == "gb-all")
        rc = gb_all(argc, argv);
    else if (cmd == "wsc")
        rc = wsc(argc, argv);
    else if (cmd == "wsc-songs")
        rc = wsc_songs(argc, argv);
    else if (cmd == "sfx-survey")
        rc = sfx_survey(argc, argv);
    else if (cmd == "align")
        rc = align(argc, argv);
    else if (cmd == "wsc-events")
        rc = wsc_events(argc, argv);
    else if (cmd == "pair")
        rc = pair_songs(argc, argv);
    if (rc == 2)
        std::fprintf(stderr, "usage: see the comment at the top of src/tools/audio_spike.cpp\n");
    return rc;
}
