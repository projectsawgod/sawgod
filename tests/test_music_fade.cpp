#include "app/enhanced_music.h"
#include "app/gb_music.h"
#include "app/sound_mix.h"
#include "emu/gb_song.h"
#include "game/bank3d_sound.h"
#include "game/session.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <doctest/doctest.h>
#include <fstream>
#include <iterator>

using namespace sawgod;

TEST_CASE("music fade observes per-slot driver volume and leaves effect slots alone") {
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    w.ram[0x624] = 2;
    for (int slot = 4; slot < 8; ++slot) {
        const size_t b = size_t(0x62C + slot * 0x32);
        w.ram[b + 8] = uint8_t(slot - 4);
        w.ram[b + 0x28] = uint8_t((slot - 4) * 0x11);
        w.ram[b + 0x2C] = 6;
        w.ram[b + 0x12] = 0xFF;
    }
    const auto ram = w.ram;
    const auto io = w.io;
    auto f = app::driver_music_fade(w);
    CHECK(f.gain == doctest::Approx(0.5));
    CHECK(f.slots[0] == 1);
    CHECK(f.slots[4] == 0);
    CHECK(f.slots[5] == doctest::Approx(1.0 / 6));
    CHECK(f.slots[6] == doctest::Approx(2.0 / 6));
    CHECK(w.ram == ram);
    CHECK(w.io == io);
    // The zero endpoint persists despite active sequence counters and arbitrarily late frames.
    for (int slot = 4; slot < 8; ++slot) {
        w.ram[size_t(0x62C + slot * 0x32 + 8)] = 0;
        w.ram[size_t(0x62C + slot * 0x32 + 0x28)] = 0;
    }
    CHECK(app::driver_music_fade(w).gain == 0);
    w.ram[0x624] = 0;
    CHECK(app::driver_music_fade(w).gain == 1);
    // The same observer follows mode one's volume restoration.
    w.ram[0x624] = 1;
    CHECK(app::driver_music_fade(w).gain == 0);
    w.ram[0x62C + 4 * 0x32 + 8] = 3;
    w.ram[0x62C + 4 * 0x32 + 0x28] = 0x33;
    CHECK(app::driver_music_fade(w).gain == doctest::Approx(0.5));
}

TEST_CASE("GB music fade gains preserve independent effect ownership") {
    const auto driver = emu::saga1_sound_driver();
    emu::GbSoundHost host(std::vector<uint8_t>(0x20000), driver);
    host.gains(1, 0.6f, 0, {0, 0.25f, 0.5f});
    CHECK(host.apu.output_gain[0] == 0);
    CHECK(host.apu.output_gain[1] == 0.25f);
    CHECK(host.apu.output_gain[2] == 0.5f);
    host.write(driver.pulse_owner, 1);
    CHECK(host.apu.output_gain[0] == 0.6f);
    host.write(driver.pulse_owner, 0);
    CHECK(host.apu.output_gain[0] == 0);
}

namespace {
double rms(const std::vector<int16_t> &pcm, int first, int count) {
    double sum = 0;
    const size_t begin = size_t(first) * 636, end = size_t(first + count) * 636;
    // PSG steps also move a unipolar DAC's DC offset. Measure AC loudness per driver frame,
    // excluding the sub-bass DC-blocker transient rather than counting it as a sustained note.
    for (size_t frame = begin; frame < end; frame += 636)
        for (size_t channel = 0; channel < 2; ++channel) {
            double mean = 0;
            for (size_t i = frame + channel; i < frame + 636; i += 2)
                mean += pcm[i] / 318.0;
            for (size_t i = frame + channel; i < frame + 636; i += 2)
                sum += (pcm[i] - mean) * (pcm[i] - mean);
        }
    return std::sqrt(sum / (end - begin));
}
struct FadeRun {
    emu::Wsc w;
    app::GbMusic gb;
    app::EnhancedObserver observer;
    std::vector<app::EnhancedFrame> frames;
    std::vector<int16_t> native, gameboy, enhanced;
    explicit FadeRun(const std::vector<uint8_t> &rom) : w(rom, 0) {
        REQUIRE(gb.load(rom));
        gb.attach(game::Session::of(w));
        observer.attach(game::Session::of(w));
        game::Machine m(w);
        m.ds() = 0;
        m.cs() = 0xD000;
        game::sound_init(m);
        request();
    }
    void closing_fade(bool on = true) {
        game::Machine m(w);
        game::hooks::emit(game::hooks::Event::IntroClosingMusicFade, m, {on});
    }
    void request(int song = 138) {
        game::Machine m(w);
        m.ds() = 0;
        m.cs() = 0xD000;
        m.ax() = uint16_t(song);
        game::sound_request_core(m);
    }
    void hold(bool prepare) {
        // A sustained, equal-level driver fixture isolates the fade curve from the three versions'
        // different orchestration and articulation. The real driver still advances the fade each frame.
        for (int slot = 4; slot < 8; ++slot) {
            const size_t b = size_t(0x62C + slot * 0x32);
            REQUIRE((w.ram[b] | w.ram[b + 1] << 8) != 0xFFFF);
            w.ram[b + 9] = 200;
            w.ram[b + 6] &= 0xF0;
            w.ram[b + 7] = 0;
            w.ram[b + 0xD] = 0x40; // Saturated rising envelope: full level, recomputed each frame.
            w.ram[b + 0xE] = w.ram[b + 0xF] = 1;
            if (prepare) {
                w.ram[b + 8] = 10;
                w.ram[b + 0x28] = 0xAA;
                w.ram[b + 0x12] = w.ram[b + 0x15] = 0xFF;
            }
        }
    }
    void tick() {
        app::paused_tick(w);
        const auto ram = w.ram;
        const auto io = w.io;
        auto a = w.apu.take();
        std::vector<int16_t> b(a.size());
        gb.frame(w, b, 1);
        auto f = observer.frame(w);
        f.samples.assign(a.size(), 0);
        f.gain = f.cue_gain = 1;
        frames.push_back(std::move(f));
        native.insert(native.end(), a.begin(), a.end());
        gameboy.insert(gameboy.end(), b.begin(), b.end());
        CHECK(w.ram == ram);
        CHECK(w.io == io);
    }
    void render(app::EnhancedRenderer &renderer) {
        for (size_t i = 0; i < frames.size(); ++i) {
            renderer.mix(frames[i], std::span<const app::EnhancedFrame>(frames).subspan(
                                        i + 1, std::min<size_t>(8, frames.size() - i - 1)));
            enhanced.insert(enhanced.end(), frames[i].samples.begin(), frames[i].samples.end());
        }
    }
};
} // namespace

TEST_CASE("opening closing signal fades GB and Enhanced slowly while WSC retains its driver fade") {
    const char *path = std::getenv("SAWGOD_WSC_ROM");
    if (!path) {
        MESSAGE("Set SAWGOD_WSC_ROM and SAWGOD_SOUNDFONT for the live fade regression");
        return;
    }
    std::ifstream input(path, std::ios::binary);
    const std::vector<uint8_t> rom{std::istreambuf_iterator<char>(input), {}};
    REQUIRE(rom.size() == 0x400000);
    const char *sf = std::getenv("SAWGOD_SOUNDFONT");
    app::EnhancedRenderer dry(sf ? sf : app::default_soundfont());
    app::EnhancedRenderer faded(sf ? sf : app::default_soundfont());
    if (!dry.available() || !faded.available()) {
        MESSAGE("FluidSynth/soundfont unavailable");
        return;
    }
    FadeRun reference(rom), fade(rom);
    // The closing signal at 220: full level for kDelay frames, then 5 s of fade, silent before the request at
    // 890.
    constexpr int start = 220 + app::SlowMusicFade::kDelay,
                  span = app::SlowMusicFade::kFrames - app::SlowMusicFade::kDelay - 4;
    for (int frame = 0; frame < 940; ++frame) {
        if (frame >= 200 && frame < 890) {
            reference.hold(frame == 200);
            fade.hold(frame == 200);
        }
        if (frame == 220) {
            game::Machine m(fade.w);
            m.ax() = 2;
            m.cx() = 0x0201;
            game::sound_channel_reset_entry_core(m);
            fade.closing_fade();
        }
        if (frame == 890) {
            reference.request();
            fade.request();
        }
        reference.tick();
        fade.tick();
    }
    reference.render(dry);
    fade.render(faded);
    for (int frame : {216, 300, 420, 440, 520, 600, 700, 780}) {
        CAPTURE(frame);
        const double gb = rms(fade.gameboy, frame, 4) / rms(reference.gameboy, frame, 4);
        const double enhanced = rms(fade.enhanced, frame, 4) / rms(reference.enhanced, frame, 4);
        const double expected = frame < start ? 1 : std::pow(10.0, -2.5 * (frame - start) / span);
        CHECK(std::abs(gb - expected) < 0.06);
        CHECK(std::abs(enhanced - expected) <
              0.06 + 0.1 * expected); // the synth wobbles about 10% in 4-frame windows
    }
    CHECK(rms(fade.native, 260, 80) < 1);
    CHECK(rms(fade.gameboy, 850, 30) < 1);
    CHECK(rms(fade.enhanced, 850, 30) == 0);
    CHECK(rms(fade.native, 910, 20) > 10);
    CHECK(rms(fade.gameboy, 910, 20) > 10);
    CHECK(rms(fade.enhanced, 910, 20) > 10);
}

TEST_CASE("GB finite music follows terminal WSC envelopes before active tracks end") {
    const char *path = std::getenv("SAWGOD_WSC_ROM");
    if (!path) {
        MESSAGE("Set SAWGOD_WSC_ROM for the finite-song tail regression");
        return;
    }
    std::ifstream input(path, std::ios::binary);
    const std::vector<uint8_t> rom{std::istreambuf_iterator<char>(input), {}};
    REQUIRE(rom.size() == 0x400000);
    FadeRun run(rom);
    game::Machine m(run.w);
    m.ax() = 163;
    game::sound_request_core(m);
    for (int frame = 0; frame < 5100; ++frame) {
        run.tick();
        if (frame == 4930) {
            CHECK(run.gb.playing());      // WSC clock still active; this is not the slot-end stop path.
            CHECK(run.w.ram[0x624] == 0); // No shared fade request either.
        }
    }
    CHECK(rms(run.gameboy, 4800, 40) > 10);
    CHECK(rms(run.native, 4935, 15) < 1);
    CHECK(rms(run.gameboy, 4935, 15) < 1);
}

TEST_CASE("slow music fade is continuous monotonic dB-linear and latches exact silence") {
    app::SlowMusicFade fade;
    CHECK_FALSE(fade.frame(0).slow);
    CHECK_FALSE(fade.frame(2).slow); // Other mode-two sites retain the native envelope.
    fade.start();
    float previous = 1;
    for (int frame = 0; frame < app::SlowMusicFade::kFrames + 100; ++frame) {
        const auto f = fade.frame(2);
        REQUIRE(f.slow);
        CHECK(f.first == previous);
        CHECK(f.last <= f.first);
        CHECK(f.first - f.last < 0.016f); // 5 s of -50 dB: under 1.6% per frame, interpolated per sample
        constexpr int delay = app::SlowMusicFade::kDelay, span = app::SlowMusicFade::kFrames - delay - 4;
        if (frame + 1 <= delay)
            CHECK(f.last == 1);
        else if (frame + 1 - delay <= span)
            CHECK(20 * std::log10(f.last) ==
                  doctest::Approx(-50.0 * (frame + 1 - delay) / span).epsilon(0.0001));
        previous = f.last;
    }
    CHECK(previous == 0);
    CHECK_FALSE(fade.frame(1).slow); // Native mode-one restoration remains authoritative.
    CHECK_FALSE(fade.frame(2).slow); // Mode one cannot leave a stale scene envelope.
    fade.start();
    fade.frame(2);
    fade.reset(); // An accepted request cancels the envelope, even for the same song ID.
    CHECK(fade.frame(0).last == 1);
}

TEST_CASE("music replacement during slow fade preserves new GB and Enhanced song samples [ROM]") {
    const char *path = std::getenv("SAWGOD_WSC_ROM");
    if (!path)
        return;
    std::ifstream input(path, std::ios::binary);
    const std::vector<uint8_t> rom{std::istreambuf_iterator<char>(input), {}};
    FadeRun reference(rom), fade(rom);
    for (int frame = 0; frame < 340; ++frame) {
        if (frame == 220) {
            game::Machine m(fade.w);
            m.ax() = 2;
            m.cx() = 0x201;
            game::sound_channel_reset_entry_core(m);
            fade.closing_fade();
        }
        if (frame == 252) {
            reference.request();
            fade.request();
        }
        reference.tick();
        fade.tick();
    }
    // The GB FIFO retains its existing two-frame alignment. No old music remains after it drains.
    CHECK(std::equal(reference.gameboy.begin() + 300 * 636, reference.gameboy.end(),
                     fade.gameboy.begin() + 300 * 636));
    const char *sf = std::getenv("SAWGOD_SOUNDFONT");
    app::EnhancedRenderer dry(sf ? sf : app::default_soundfont());
    app::EnhancedRenderer faded(sf ? sf : app::default_soundfont());
    if (!dry.available() || !faded.available())
        return;
    std::vector<std::array<int, 4>> expected, actual;
    dry.set_event_sink([&](const auto &e) {
        if (e.frame >= 252)
            expected.push_back({int(e.frame), e.voice, e.key, e.velocity});
    });
    faded.set_event_sink([&](const auto &e) {
        if (e.frame >= 252)
            actual.push_back({int(e.frame), e.voice, e.key, e.velocity});
    });
    reference.render(dry);
    fade.render(faded);
    CHECK(actual == expected);
    for (size_t i = 252; i < fade.frames.size(); ++i) {
        CHECK(fade.frames[i].music_fade == 1);
        CHECK_FALSE(fade.frames[i].slow_fade);
        CHECK(fade.frames[i].song == reference.frames[i].song);
        for (size_t slot = 4; slot < 8; ++slot) {
            CHECK(fade.frames[i].channels[slot].volume == reference.frames[i].channels[slot].volume);
            CHECK(fade.frames[i].channels[slot].ticks == reference.frames[i].channels[slot].ticks);
        }
    }
}

TEST_CASE("map-change fade retains the native driver path after a canceled opening signal [ROM]") {
    const char *path = std::getenv("SAWGOD_WSC_ROM");
    if (!path)
        return;
    std::ifstream input(path, std::ios::binary);
    const std::vector<uint8_t> rom{std::istreambuf_iterator<char>(input), {}};
    FadeRun reference(rom), candidate(rom);
    // A replacement must also cancel a scene notification that has not reached the next audio frame.
    candidate.closing_fade();
    candidate.closing_fade(false); // Leaving/aborting the scene revokes its envelope too.
    candidate.closing_fade();
    reference.request();
    candidate.request();
    for (int frame = 0; frame < 700; ++frame) {
        if (frame == 180)
            for (auto *run : {&reference, &candidate}) {
                game::Machine m(run->w);
                m.ax() = 2;
                m.cx() = 0x201;
                game::sound_channel_reset_entry_core(m);
            }
        // Map music changes wait 32 frames, clear, then request the new song two frames later.
        if (frame == 212 || frame == 214) {
            reference.request(frame == 212 ? 0 : 139);
            candidate.request(frame == 212 ? 0 : 139);
        }
        reference.tick();
        candidate.tick();
        const auto &f = candidate.frames.back();
        CHECK_FALSE(f.slow_fade);
        CHECK(f.music_fade == app::driver_music_fade(candidate.w).gain);
    }
    CHECK(candidate.gameboy == reference.gameboy);
    CHECK(candidate.native == reference.native);
    const char *sf = std::getenv("SAWGOD_SOUNDFONT");
    app::EnhancedRenderer expected(sf ? sf : app::default_soundfont());
    app::EnhancedRenderer actual(sf ? sf : app::default_soundfont());
    if (!expected.available() || !actual.available())
        return;
    reference.render(expected);
    candidate.render(actual);
    CHECK(candidate.enhanced == reference.enhanced);
    // The audio fade check also compares this timeline's complete PCM
    // against a separately built master-c2f35a1 probe, including the outgoing lead-in/reverb.
}

TEST_CASE("GB slow music envelope leaves an effect's samples exact") {
    emu::GbApu reference;
    reference.write(0xFF24, 0x77);
    reference.write(0xFF25, 0x11);
    reference.write(0xFF11, 0x80);
    reference.write(0xFF12, 0xF0);
    reference.write(0xFF13, 0xDC);
    reference.write(0xFF14, 0x85);
    reference.effect_channels = 1;
    auto fade = reference;
    fade.music_fade(1, 0, 100000);
    reference.run(200000);
    fade.run(200000);
    const auto expected = reference.take();
    REQUIRE(std::any_of(expected.begin(), expected.end(), [](int16_t s) { return s != 0; }));
    CHECK(fade.take() == expected);
}
