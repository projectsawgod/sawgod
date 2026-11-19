// GB music maps, repeated loop state, and following the WSC clock (app/gb_music.h).
#include <doctest/doctest.h>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>

#include "emu/gb_song.h"
#include "emu/wsc.h"

#include "app/gb_cues.h"
#include "app/gb_music.h"
#include "app/sound_mix.h"
#include "game/bank3d_sound.h"

using namespace sawgod::app;

TEST_CASE("gb music: the field song's tick map") {
    const GbSongMap *m = gb_song_map(138);
    REQUIRE(m != nullptr);
    CHECK(m->gb_song == 1);
    REQUIRE(gb_song_map(139) != nullptr);
    CHECK(gb_song_map(139)->gb_song == 2);
    CHECK(m->gb_ticks_by(1) == 2); // the WSC's first tick: the GB's start tick and its first notes
    // the run-up: the GB's notes (ticks 2, 5, 8, 11, 14) with the WSC's (1, 5, 9, 13, 17)
    CHECK(m->gb_ticks_by(5) == 5);
    CHECK(m->gb_ticks_by(9) == 8);
    CHECK(m->gb_ticks_by(13) == 11);
    CHECK(m->gb_ticks_by(17) == 14);
    CHECK(m->gb_ticks_by(18) == 15);
    CHECK(m->gb_ticks_by(2417) == 2414); // and three behind for good
}

namespace {
// The slot's tempo phase as sound_tick leaves it: + rate each frame after the first, wrapping at 16 (a skip)
struct Driver {
    int rate = 5, phase = 0, frames = 0;
    uint8_t next() {
        if (frames++ > 0)
            phase = (phase + rate) & 15;
        return uint8_t(phase);
    }
};
} // namespace

TEST_CASE("gb music: following the WSC song") {
    GbMusicFollower f;
    CHECK(f.frame(0xFFFF, 0).ticks == 0); // nothing requested
    f.request(138);
    // the request's frame: the driver hasn't started the song yet (step counter 0)
    GbMusicFollower::Step s = f.frame(0, 0);
    CHECK_FALSE(s.start);
    CHECK(f.playing() == nullptr);
    Driver d;
    s = f.frame(6, d.next()); // started: the WSC's tick 1
    CHECK(s.start);
    CHECK(s.ticks == 2);
    REQUIRE(f.playing() != nullptr);
    // 16 frames more: 11 of them tick (rate 5); the GB follows the map
    int gb = 2;
    for (int i = 0; i < 16; ++i)
        gb += f.frame(7, d.next()).ticks;
    CHECK(f.wsc_ticks() == 12);
    CHECK(gb == f.playing()->gb_ticks_by(12));
    for (int i = 0; i < 3200; ++i)
        gb += f.frame(9, d.next()).ticks;
    CHECK(gb == f.wsc_ticks() - 3);

    // another song: the GB stops on the next frame and stays silent (no GB version)
    f.request(155);
    s = f.frame(0, 0);
    CHECK(s.stop);
    CHECK_FALSE(s.start);
    CHECK(f.playing() == nullptr);
    CHECK(f.frame(5, 3).ticks == 0);

    // the field song again: starts over; requested again while it plays: stops, then starts over
    f.request(138);
    CHECK(f.frame(6, 0).start);
    f.request(138);
    s = f.frame(0, 0);
    CHECK(s.stop);
    CHECK_FALSE(s.start);
    CHECK(f.frame(6, 0).start);

    // the driver silenced (sound_init: every slot inactive): stop
    CHECK(f.frame(0xFFFF, 0).stop);
    CHECK(f.playing() == nullptr);
}

TEST_CASE("gb music: periodic maps preserve rests and the half-tick phase") {
    GbSongMap m;
    m.anchors = {{0, 1}, {1, 2}, {6, 10}};
    m.slope = 1.5;
    m.loop_start = 6;
    m.loop_ticks = 1544;
    m.gb_loop_ticks = 2304;
    m.loop_anchors = {{0, 0}, {1536, 2304}, {1544, 2304}};
    CHECK(m.gb_loop_start() == 10);
    CHECK(m.gb_ticks_by(7) == 11);
    CHECK(m.gb_ticks_by(8) == 13);
    CHECK(m.gb_ticks_by(1542) == 2314);
    CHECK(m.gb_ticks_by(1549) == 2314); // eight extra WSC ticks: the GB holds
    CHECK(m.gb_ticks_by(1550) == 2314);
    CHECK(m.gb_ticks_by(1551) == 2315);
    CHECK(m.gb_ticks_by(6 + 1544 * 10000) == 10 + 2304 * 10000);
    for (int w = 6; w < 1550; ++w)
        CHECK(m.gb_ticks_by(w + 1544) - m.gb_ticks_by(w) == 2304);

    m.loop_start = 7;
    m.anchors = {{0, 1}, {1, 2}, {7, 11}};
    m.loop_phase = 0.5;
    CHECK(m.gb_ticks_by(8) == 13); // the 3:2 rhythm starts with two GB ticks
    CHECK(m.gb_ticks_by(9) == 14);
    CHECK(m.gb_ticks_by(8 + 1544 * 10000) == 13 + 2304 * 10000);
}

TEST_CASE("gb music: follower remains locked over many sequence-counter wraps") {
    GbMusicFollower f;
    f.request(146);
    Driver d{11};
    auto s = f.frame(6, d.next());
    REQUIRE(s.start);
    int gb = s.ticks;
    const GbSongMap *map = f.playing();
    REQUIRE(map != nullptr);
    while (f.wsc_ticks() < map->loop_start + map->loop_ticks * 20 + 1) {
        // The WSC's sequence counter rewinds; its clock and the follower do not.
        const int position = (f.wsc_ticks() - map->loop_start + map->loop_ticks) % map->loop_ticks;
        s = f.frame(uint16_t(position + 2), d.next());
        CHECK_FALSE(s.start);
        CHECK_FALSE(s.stop);
        REQUIRE(s.ticks >= 0);
        REQUIRE(s.ticks <= 4);
        gb += s.ticks;
        REQUIRE(gb == map->gb_ticks_by(f.wsc_ticks()));
    }
    CHECK(gb - map->gb_ticks_by(map->loop_start + 1) == map->gb_loop_ticks * 20);

    // When a finite song changes its clock voice, compare that voice's own previous phase.
    f.request(153);
    REQUIRE(f.frame(6, 4).start);
    CHECK(f.clock_slot() == 0);
    CHECK(f.frame(6, 9, 10).ticks == 0); // the surviving voice wrapped
    CHECK(f.wsc_ticks() == 1);
    f.frame(6, 12, 9);
    CHECK(f.wsc_ticks() == 2);
}

TEST_CASE("gb music: every table entry has a bounded monotonic tick map") {
    int pairs = 0;
    std::array<bool, 21> gb{};
    std::array<bool, 256> ids{};
    for (const auto &entry : gb_song_maps()) {
        const auto *m = &entry;
        const int song = m->wsc_song;
        CHECK_FALSE(ids[size_t(song)]);
        ids[size_t(song)] = true;
        CHECK(gb_song_map(uint8_t(song)) == m);
        ++pairs;
        CHECK(m->wsc_song == song);
        REQUIRE(m->gb_song > 0);
        REQUIRE(m->gb_song < gb.size());
        gb[m->gb_song] = true;
        CHECK((m->slope == 1 || m->slope == 1.5));
        CHECK((m->loop_phase == 0 || m->loop_phase == 0.5));
        auto increasing = [](const auto &anchors) {
            for (size_t i = 1; i < anchors.size(); ++i) {
                CHECK(anchors[i].first > anchors[i - 1].first);
                CHECK(anchors[i].second >= anchors[i - 1].second); // explicit rests can hold GB time
            }
        };
        increasing(m->anchors);
        increasing(m->loop_anchors);
        CHECK(m->voices[0] != m->voices[1]);
        CHECK(m->voices[0] != m->voices[2]);
        CHECK(m->voices[1] != m->voices[2]);
        for (uint8_t voice : m->voices)
            CHECK(voice < 8);
        int previous = m->gb_ticks_by(1);
        const int end = m->loop_ticks ? m->loop_start + m->loop_ticks * 3 : int(m->anchors.back().first) + 16;
        for (int w = 2; w <= end; ++w) {
            const int target = m->gb_ticks_by(w);
            REQUIRE(target >= previous);
            REQUIRE(target - previous <= 4);
            previous = target;
        }
        if (m->loop_ticks) {
            REQUIRE_FALSE(m->loop_anchors.empty());
            CHECK(m->loop_anchors.front() == std::make_pair(0.0, 0.0));
            CHECK(m->loop_anchors.back() == std::make_pair(double(m->loop_ticks), double(m->gb_loop_ticks)));
            for (int w = m->loop_start; w < m->loop_start + m->loop_ticks; ++w)
                REQUIRE(m->gb_ticks_by(w + m->loop_ticks) - m->gb_ticks_by(w) == m->gb_loop_ticks);
        }
    }
    CHECK(pairs == 39);
    for (int song = 1; song <= 17; ++song) {
        CHECK(gb[size_t(song)]);
        REQUIRE(gb_song_map(uint8_t(song)) != nullptr);
        REQUIRE(gb_song_map(uint8_t(137 + song)) != nullptr);
    }
    CHECK(gb_song_map(135)->gb_song == 18);
    CHECK(gb_song_map(136)->gb_song == 19);
    CHECK(gb_song_map(137)->gb_song == 20);
    CHECK(gb_song_map(163)->gb_song == 9);
    CHECK(gb_song_map(178)->gb_song == 15);
}

TEST_CASE("gb music: sound effects mute only the paired stolen voice") {
    auto w = std::make_unique<sawgod::emu::Wsc>(std::vector<uint8_t>(0x400000), 0);
    auto slot = [&](int n, int step, int hw) {
        const size_t b = 0x62C + size_t(n) * 0x32;
        w->ram[b] = uint8_t(step);
        w->ram[b + 1] = uint8_t(step >> 8);
        w->ram[b + 2] = uint8_t(hw);
    };
    for (int n = 0; n < 8; ++n)
        slot(n, 0xFFFF, n & 3);
    for (int n = 4; n < 8; ++n)
        slot(n, 6, n & 3);
    const auto &m = *gb_song_map(138); // GB pulse 1 -> 5, pulse 2 -> 4, wave -> 6
    CHECK(gb_music_voice_mutes(*w, m) == 0);
    slot(0, 6, 1);
    CHECK(gb_music_voice_mutes(*w, m) == 1);
    slot(1, 6, 2);
    CHECK(gb_music_voice_mutes(*w, m) == 5);
    slot(0, 0xFFFF, 1);
    CHECK(gb_music_voice_mutes(*w, m) == 4);
    slot(1, 0xFFFF, 2);
    slot(3, 6, 3); // the WSC-only fourth voice has no GB partner
    CHECK(gb_music_voice_mutes(*w, m) == 0);
    // A jingle is itself in effect slots; match its actual tracks, then detect an overwrite.
    auto word = [&](size_t a, int value) {
        w->rom[a] = uint8_t(value);
        w->rom[a + 1] = uint8_t(value >> 8);
    };
    w->rom[0x3D02E2 + 152 * 3] = 3;
    for (int n = 0; n < 3; ++n) {
        word(0x3D0500 + size_t(n) * 6, n * 0x32);
        word(0x3D0504 + size_t(n) * 6, 0x1000 + n);
        slot(n, 6, n);
        w->ram[0x62F + size_t(n) * 0x32] = uint8_t(n);
        w->ram[0x630 + size_t(n) * 0x32] = 0x10;
    }
    CHECK(gb_music_voice_mutes(*w, *gb_song_map(152)) == 0);
    w->ram[0x661] ^= 1;
    CHECK(gb_music_voice_mutes(*w, *gb_song_map(152)) == 2);
}

TEST_CASE("gb music: borrowed ticks fit one audio frame and repeat driver state [ROM]") {
    const char *path = std::getenv("SAWGOD_GB_ROM");
    if (!path) {
        MESSAGE("ROM check skipped: set SAWGOD_GB_ROM to the player's GB ROM");
        return;
    }
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.good());
    const std::vector<uint8_t> rom{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    REQUIRE(rom.size() == 0x20000);
    constexpr double cycles = double(sawgod::emu::GbApu::kClock) * (159 * 256) / 3072000.0;
    auto host = std::make_unique<sawgod::emu::GbSoundHost>(rom, sawgod::emu::saga1_sound_driver());
    host->apu.set_sample_rate(24000);
    REQUIRE(host->init());
    host->apu.take();
    GbMusic music;
    REQUIRE(music.load(rom));
    int observed_ticks = 0;
    host->cpu.watch_pc = sawgod::emu::saga1_sound_driver().tick_pc;
    host->cpu.on_watch = [&] { ++observed_ticks; };
    host->request(5); // starting this song with two ticks used to truncate the driver's call
    REQUIRE(host->frame_locked(2, cycles));
    CHECK(host->read(0xFFE1) == 5);
    CHECK(observed_ticks == 2);
    CHECK(host->apu.take().size() / 2 == 318);

    host = std::make_unique<sawgod::emu::GbSoundHost>(rom, sawgod::emu::saga1_sound_driver());
    host->apu.set_sample_rate(24000);
    REQUIRE(host->init());
    host->apu.take();
    host->request(7);
    host->set_tick_loop(31, 1536); // the natural GB voice periods are unequal
    std::vector<uint8_t> loop_state;
    size_t samples = 0;
    for (int tick = 1; tick <= 31 + 1536 * 3; ++tick) {
        REQUIRE(host->frame_locked(1, cycles));
        samples += host->apu.take().size() / 2;
        if (tick == 31)
            loop_state = host->driver_state();
        if (tick > 31 && (tick - 31) % 1536 == 0)
            CHECK(host->driver_state() == loop_state);
    }
    CHECK(std::abs(double(samples) - (31 + 1536 * 3) * 318.0) <= 1);
}

TEST_CASE("gb music: WSC cue bytecode round-trips pitches and durations [ROM]") {
    const char *path = std::getenv("SAWGOD_WSC_ROM");
    if (!path) {
        MESSAGE("ROM check skipped: set SAWGOD_WSC_ROM to the player's WSC ROM");
        return;
    }
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.good());
    const std::vector<uint8_t> source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    REQUIRE(source.size() >= 0x3E0000);
    std::vector<uint8_t> image(source.begin() + 0x320000, source.begin() + 0x340000);
    const auto original = image;
    REQUIRE(gb_append_cues(image, source));
    CHECK(image.size() == 0x24000);
    CHECK(std::equal(original.begin(), original.end(), image.begin()));
    GbMusic music;
    REQUIRE(music.load(source));
    auto driver = sawgod::emu::saga1_sound_driver();
    driver.bank = 8;
    for (uint8_t song : {135, 136, 137}) {
        INFO("WSC request ", int(song));
        auto cue = gb_read_cue(source, song);
        REQUIRE(cue.has_value());
        const auto *map = gb_song_map(song);
        auto host = std::make_unique<sawgod::emu::GbSoundHost>(image, driver);
        REQUIRE(host->init());
        host->apu.take();
        host->request(cue->song);
        auto w = std::make_unique<sawgod::emu::Wsc>(source, 0);
        sawgod::game::Machine m(*w);
        m.ds() = 0;
        m.cs() = 0xD000;
        sawgod::game::sound_init(m);
        m.ax() = song;
        sawgod::game::sound_request_core(m);
        int length = 0;
        for (const auto &voice : cue->voices) {
            int n = 0;
            for (const auto &note : voice.notes)
                n += note.ticks;
            length = std::max(length, n);
        }
        std::array<int, 3> cut{}; // ticks the GB voice has rested inside a sounding WSC note (articulation)
        for (int tick = 1; tick <= length * (song == 135 ? 2 : 1) + 4; ++tick) {
            sawgod::app::paused_tick(*w);
            REQUIRE(host->frame_locked(tick == 1 ? 2 : 1, 55574));
            for (size_t ch = 0; ch < 3; ++ch) {
                const int b = 0x62C + map->voices[ch] * 0x32;
                const int wp = w->ram[size_t(b + 0x10)] | w->ram[size_t(b + 0x11)] << 8;
                const bool active = (w->ram[size_t(b)] | w->ram[size_t(b + 1)] << 8) != 0xFFFF;
                const int gb = 0xCE00 + int(ch) * 0x20;
                const int gp = host->read(uint16_t(gb + 9)) | host->read(uint16_t(gb + 10)) << 8;
                const bool ga = (host->read(uint16_t(gb)) & 0x80) == 0;
                const int wn = !active || wp == 0
                                   ? -1
                                   : int(std::lround(69 + 12 * std::log2(96000.0 / (2048 - wp) / 440)));
                const int gn = !ga || gp == 0xFFFF
                                   ? -1
                                   : int(std::lround(69 + 12 * std::log2(131072.0 / (2048 - gp) / 440)));
                INFO("tick ", tick, ", GB voice ", ch);
                // An articulated voice rests for up to its gap at the end of each note, then must agree
                // again.
                cut[ch] = gn < 0 && wn >= 0 ? cut[ch] + 1 : 0;
                if (cut[ch] && cut[ch] <= sawgod::app::gb_cue_gap(song, int(ch)))
                    continue;
                REQUIRE(wn == gn);
            }
            // Compare sequencer time independent of frame tempo; native note/envelope parsing remains.
            for (int slot = 0; slot < 8; ++slot) {
                w->ram[0x62C + size_t(slot) * 0x32 + 6] &= 0xF0;
                w->ram[0x62C + size_t(slot) * 0x32 + 7] = 0;
            }
            w->apu.take();
            host->apu.take();
        }
    }
    // An older song's sequencer state must be identical in the copied driver bank.
    auto native = std::make_unique<sawgod::emu::GbSoundHost>(original, sawgod::emu::saga1_sound_driver());
    auto extended = std::make_unique<sawgod::emu::GbSoundHost>(image, driver);
    REQUIRE(native->init());
    REQUIRE(extended->init());
    native->request(1);
    extended->request(1);
    for (int tick = 0; tick < 128; ++tick) {
        REQUIRE(native->frame_locked(1, 55574));
        REQUIRE(extended->frame_locked(1, 55574));
        CHECK(native->driver_state() == extended->driver_state());
        CHECK(native->apu.take() == extended->apu.take());
    }
}

TEST_CASE("gb music: missing or unsupported WSC cues leave the source image intact") {
    std::vector<uint8_t> gb(0x20000, 0xFF);
    const auto before = gb;
    CHECK_FALSE(gb_append_cues(gb, {}));
    CHECK(gb == before);
    CHECK_FALSE(gb_read_cue({}, 135).has_value());
}

TEST_CASE("gb music: synthetic WSC cue tracks preserve repeats, rests, and reject unsupported data") {
    std::vector<uint8_t> source(0x400000);
    auto word = [&](size_t a, int v) {
        source[a] = uint8_t(v);
        source[a + 1] = uint8_t(v >> 8);
    };
    for (int song = 135; song <= 137; ++song) {
        word(0x3D02E0 + size_t(song) * 3, 0);
        source[0x3D02E2 + size_t(song) * 3] = 4;
    }
    for (int slot = 0; slot < 4; ++slot)
        word(0x3D0504 + size_t(slot) * 6, 0x7000);
    const std::vector<uint8_t> track = {0x0B, 0, 9,    0, 0x1F, 3,  0xFD, 0xF0,
                                        0xA2, 5, 0x34, 5, 0x32, 20, 0xB0, 0xF0};
    std::copy(track.begin(), track.end(), source.begin() + 0x3D7000);
    auto cue = gb_read_cue(source, 135);
    REQUIRE(cue.has_value());
    for (const auto &voice : cue->voices) {
        REQUIRE(voice.notes.size() == 3);
        CHECK(voice.loop_note == 1);
        CHECK(voice.notes[0].pitch == -1);
        CHECK(voice.notes[0].ticks == 3);
        CHECK(voice.notes[1].pitch == 76);
        CHECK(voice.notes[1].ticks == 5);
        CHECK(voice.notes[1].instrument == 5);
        CHECK(voice.notes[2].pitch == 74);
        CHECK(voice.notes[2].ticks == 20);
    }
    std::vector<uint8_t> gb(0x20000, 0xFF);
    REQUIRE(gb_append_cues(gb, source));
    source[0x3D700A] = 0x7F; // an absolute-period tie, not supported by this transcription
    CHECK_FALSE(gb_read_cue(source, 135).has_value());
    gb.resize(0x20000);
    const auto original = gb;
    CHECK_FALSE(gb_append_cues(gb, source));
    CHECK(gb == original);
}

TEST_CASE("gb music: transcribed effect-slot cues fade as music without fading an overriding effect [ROM]") {
    const char *path = std::getenv("SAWGOD_WSC_ROM");
    if (!path)
        return;
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.good());
    const std::vector<uint8_t> source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    auto w = std::make_unique<sawgod::emu::Wsc>(source, 0);
    sawgod::game::Machine m(*w);
    m.cs() = 0xD000;
    m.ds() = 0;
    sawgod::game::sound_init(m);
    m.ax() = 136;
    sawgod::game::sound_request_core(m);
    GbMusic music;
    REQUIRE(music.load(source));
    music.request(136);
    paused_tick(*w);
    auto samples = w->apu.take();
    music.frame(*w, samples, 1);
    REQUIRE(music.playing());
    CHECK(music.wsc_music_slots(*w) == 7);
    apply_channel_gains(*w, 0, 1, music.wsc_music_slots(*w));
    CHECK(w->apu.output_gain[0] == 0);
    CHECK(w->apu.output_gain[1] == 0);
    CHECK(w->apu.output_gain[2] == 0);
    // Replace a cue track in slot 0: only that new effect keeps its effects gain.
    w->ram[0x62F] ^= 1;
    CHECK(music.wsc_music_slots(*w) == 6);
    CHECK(gb_music_voice_mutes(*w, *gb_song_map(136)) == 2);
    apply_channel_gains(*w, 0, 1, music.wsc_music_slots(*w));
    CHECK(w->apu.output_gain[0] == 1);
    CHECK(w->apu.output_gain[1] == 0);
    CHECK(w->apu.output_gain[2] == 0);
    w->ram[0x661] ^= 1; // slot 1 track pointer
    w->ram[0x693] ^= 1; // slot 2 track pointer
    CHECK(music.wsc_music_slots(*w) == 0);
    samples.assign(636, 0);
    music.frame(*w, samples, 1);
    CHECK_FALSE(music.playing());
}
