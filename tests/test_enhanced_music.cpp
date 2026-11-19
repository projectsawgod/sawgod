#include "test_enhanced_music_support.h"

TEST_CASE("Enhanced seat pan preserves full WSC movement and clamps MIDI edges") {
    CHECK(app::enhanced_pan(64, -30) == 45);
    CHECK(app::enhanced_pan(85, -30) == 66);
    CHECK(app::enhanced_pan(1, -30) == 0);
    CHECK(app::enhanced_pan(127, 30) == 127);
    CHECK(app::enhanced_pan(64, 50) == 96);
    CHECK(app::enhanced_pan(65, 50) == 96);
}

TEST_CASE("config: soundfont paths survive quoting and saving") {
    Config c;
    c.set_string("audio.soundfont", "C:\\Soundfonts\\My \"GS\".sf2");
    Config d;
    d.from_toml(c.to_toml());
    CHECK(d.string_value("audio.soundfont", "") == "C:\\Soundfonts\\My \"GS\".sf2");
    CHECK(d.string_value("audio.other", "fallback") == "fallback");
}

TEST_CASE("enhanced music: missing soundfont preserves PCM") {
    app::EnhancedRenderer r("/missing/sawgod-test.sf2");
    CHECK_FALSE(r.available());
    app::EnhancedFrame f;
    for (int id : locked_songs) {
        f.song = f.cue = id;
        f.gain = f.cue_gain = 1;
        f.samples = {17, -23};
        r.mix(f, {});
        CHECK(f.samples == std::vector<int16_t>{17, -23});
    }
}

TEST_CASE("enhanced music: prepared replacements after another song match fresh PCM") {
    const auto rom = music_rom();
    if (rom.empty())
        return;
    app::EnhancedRenderer used(music_sf());
    if (!used.available())
        return;
    uint64_t sequence = 0;
    const auto frames_for = [&](int song) {
        emu::Wsc w(rom, 0);
        app::EnhancedObserver observer;
        observer.attach(game::Session::of(w));
        music_init(w);
        music_request(w, song);
        std::vector<app::EnhancedFrame> frames;
        for (int i = 0; i < 180 + app::EnhancedAudio::kLookahead; ++i) {
            app::paused_tick(w);
            w.apu.take();
            auto frame = observer.frame(w);
            frame.sequence = sequence++;
            frame.generation = uint64_t(song);
            frame.gain = 1;
            frame.samples.assign(636, 0);
            frames.push_back(std::move(frame));
        }
        sequence -= app::EnhancedAudio::kLookahead;
        return frames;
    };
    auto previous = frames_for(142);
    for (size_t i = 0; i < 180; ++i)
        used.mix(previous[i], std::span<const app::EnhancedFrame>(previous).subspan(i + 1, 8));
    // Battle exercises new voices AND percussion; Ending exercises a second replacement without
    // percussion. Keep absolute frame/tick coordinates identical on the fresh and used renderers.
    for (int song : {145, 140}) {
        app::EnhancedRenderer fresh(music_sf());
        REQUIRE(fresh.available());
        auto frames = frames_for(song);
        bool audible = false;
        for (size_t i = 0; i < 180; ++i) {
            auto expected = frames[i];
            const auto future = std::span<const app::EnhancedFrame>(frames).subspan(i + 1, 8);
            used.mix(frames[i], future);
            fresh.mix(expected, future);
            REQUIRE(frames[i].samples == expected.samples);
            audible |= std::any_of(expected.samples.begin(), expected.samples.end(),
                                   [](int16_t sample) { return sample != 0; });
        }
        CHECK(audible);
        CHECK(fresh.synth_stats().inline_created == 0);
    }
}

TEST_CASE("enhanced music: read-only observer follows live driver, loops and resets") {
    const char *path = std::getenv("SAWGOD_WSC_ROM");
    if (!path) {
        MESSAGE("Set SAWGOD_WSC_ROM for the live driver check");
        return;
    }
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> rom{std::istreambuf_iterator<char>(in), {}};
    REQUIRE(rom.size() == 0x400000);
    emu::Wsc w(rom, 0);
    app::EnhancedObserver observer;
    observer.attach(game::Session::of(w));
    game::Machine m(w);
    auto saved = w.cpu.s;
    m.ds() = 0;
    m.cs() = 0xD000;
    game::sound_init(m);
    w.cpu.s = saved;
    saved = w.cpu.s;
    m.ax() = 138;
    m.ds() = 0;
    m.cs() = 0xD000;
    game::sound_request_core(m);
    w.cpu.s = saved;
    app::EnhancedRenderer r(std::getenv("SAWGOD_SOUNDFONT") ? std::getenv("SAWGOD_SOUNDFONT")
                                                            : app::default_soundfont());
    // The default development path may not exist in isolated worktrees; observing never needs it.
    std::vector<app::EnhancedFrame> frames;
    for (int i = 0; i < 7009; ++i) {
        app::paused_tick(w);
        const auto ram = w.ram;
        const auto io = w.io;
        auto f = observer.frame(w);
        f.samples = w.apu.take();
        f.gain = 1;
        CHECK(w.ram == ram);
        CHECK(w.io == io);
        frames.push_back(std::move(f));
    }
    CHECK(observer.playing(w));
    CHECK(frames.back().channels[4].ticks > 4800);
    if (r.available()) {
        for (size_t i = 0; i < frames.size() - 1; ++i)
            r.mix(frames[i],
                  std::span<const app::EnhancedFrame>(frames).subspan(
                      i + 1, std::min<size_t>(app::EnhancedAudio::kLookahead, frames.size() - i - 1)));
        const auto stats = r.stats();
        CHECK(stats.notes == 1216); // v4: 231 + 323 + 321 + 195 + 146, intro and two loops
        CHECK(stats.bridges == 96);
        CHECK(stats.releases == 340);
        CHECK(stats.unmapped == 0);
    }
    observer.request(142);
    CHECK(observer.playing(w));
    observer.request(134);
    CHECK_FALSE(observer.playing(w));
    observer.reset();
    CHECK_FALSE(observer.playing(w));
}

TEST_CASE("enhanced music: inaudible synth stays at the same sample when fading in") {
    const std::string sf =
        std::getenv("SAWGOD_SOUNDFONT") ? std::getenv("SAWGOD_SOUNDFONT") : app::default_soundfont();
    app::EnhancedRenderer always(sf), fading(sf);
    if (!always.available() || !fading.available()) {
        MESSAGE("Set SAWGOD_SOUNDFONT for synth comparison");
        return;
    }
    for (int i = 0; i < 180; ++i) {
        app::EnhancedFrame f;
        f.song = 138;
        f.generation = 1;
        f.sequence = uint64_t(i);
        f.gain = 1;
        f.samples.assign(636, 0);
        auto &c = f.channels[4];
        c.event = i % 30 == 0 ? 'N' : '-';
        c.instrument = 7;
        c.hw = 0;
        c.step = uint16_t(2 + i / 30);
        c.pitch = c.period = 1829;
        c.volume = 0xAA;
        c.ticks = i;
        auto fade = f;
        fade.gain = i < 90 ? 0 : 1;
        always.mix(f, {});
        fading.mix(fade, {});
        if (i >= 90)
            for (size_t j = 0; j < f.samples.size(); ++j)
                CHECK(std::abs(int(f.samples[j]) - int(fade.samples[j])) <= 1);
        else
            CHECK(std::all_of(fade.samples.begin(), fade.samples.end(), [](int16_t s) { return s == 0; }));
    }
    CHECK(always.stats().notes == fading.stats().notes);
}

TEST_CASE("enhanced music: every locked song survives fades, pause, save/load, reset and packet loss") {
    const auto rom = music_rom();
    if (rom.empty())
        return;
    for (int id : locked_songs) {
        CAPTURE(id);
        emu::Wsc w(rom, 0), original(rom, 0);
        w.out8(0x60, 0x80); // The running WSC game enables color RAM before save/load.
        original.out8(0x60, 0x80);
        app::EnhancedObserver observer;
        observer.attach(game::Session::of(w));
        music_init(w);
        music_init(original);
        const bool cue = app::enhanced_song(id)->voices.front().slot < 4;
        if (cue) {
            music_request(w, 138);
            music_request(original, 138);
        }
        music_request(w, id);
        music_request(original, id);
        app::EnhancedRenderer always(music_sf()), fading(music_sf());
        if (!always.available() || !fading.available()) {
            MESSAGE("Set SAWGOD_SOUNDFONT for full synth transitions");
            return;
        }
        std::map<std::pair<int, int>, int> held;
        always.set_event_sink([&](const auto &e) {
            const auto key = std::pair{e.song, e.voice};
            if (e.velocity) {
                CHECK_FALSE(held.contains(key));
                held[key] = e.key;
            } else {
                REQUIRE(held.contains(key));
                CHECK(held[key] == e.key);
                held.erase(key);
            }
        });
        Fidelity fidelity(FidelityMode::Wsc, FidelityMode::Swap);
        fidelity.step(-1, 0);
        std::vector<app::EnhancedFrame> frames;
        app::FastForwardAudio ff;
        for (int i = 0; i < 400; ++i) {
            const uint64_t now = uint64_t(i) * 13'250'000;
            if (i == 30)
                fidelity.step(1, now); // WSC -> Enhanced mid-note
            if (i == 110)
                fidelity.step(-1, now); // Enhanced -> WSC mid-note
            if (i == 190)
                fidelity.step(1, now);
            if (i == 210) {
                music_save_load(w);
                music_save_load(original);
            }
            if (i == 240) { // A map/load rebuild uses the ordinary driver reset and music request.
                game::install(w);
                game::install(original);
                music_init(w);
                music_init(original);
                music_request(w, id);
                music_request(original, id);
            }
            if (i == 320) {
                music_request(w, 134); // An unmapped song must resume WSC in Enhanced mode.
                music_request(original, 134);
            }
            if (i == 300) {
                music_request(w, id == 138 ? 144 : 138); // Locked -> locked; a cue keeps its own clock.
                music_request(original, id == 138 ? 144 : 138);
            }
            const auto weights = fidelity.music_weights(now);
            const auto mix = app::music_mix(weights, false, false, observer.playing(w));
            const auto cue_mix = app::music_mix(weights, false, false, observer.cue_playing(w));
            app::apply_channel_gains(w, mix.wsc, 0.7f, observer.wsc_music_slots(w));
            app::apply_cue_gain(w, observer.wsc_music_slots(w), cue_mix.wsc);
            // This is the app's Options pause service. Leaving/entering the overlay does not
            // restart the driver or the observer, and the two independent machines stay exact.
            app::paused_tick(w);
            app::paused_tick(original);
            check_hardware(w, original);
            const auto ram = w.ram;
            auto f = observer.frame(w);
            CHECK(w.ram == ram);
            w.apu.take();
            original.apu.take();
            f.samples.assign(636, 0);
            f.gain = mix.enhanced;
            f.cue_gain = cue_mix.enhanced;
            frames.push_back(std::move(f));
        }
        uint64_t normal_samples = 0, fast_samples = 0;
        for (size_t i = 0; i < frames.size(); ++i) {
            if (i == 290) // Nonblocking worker overflow: the next packet must rebuild without stuck notes.
                continue;
            const auto future = std::span<const app::EnhancedFrame>(frames).subspan(
                i + 1, std::min<size_t>(app::EnhancedAudio::kLookahead, frames.size() - i - 1));
            auto full = frames[i];
            full.gain = full.cue_gain = 1;
            always.mix(full, future);
            fading.mix(frames[i], future);
            const float weight = cue ? frames[i].cue_gain : frames[i].gain;
            for (size_t j = 0; j < full.samples.size(); ++j)
                CHECK(std::abs(int(std::nearbyint(full.samples[j] * weight)) - frames[i].samples[j]) <= 2);
            if (frames[i].song == 134) {
                CHECK_FALSE(app::enhanced_song(frames[i].song));
                CHECK(frames[i].gain == 0);
            }
            auto output = ff.process(frames[i].samples, i >= 160 && i < 230 ? 4 : 1);
            if (i >= 160 && i < 230)
                fast_samples += output.size();
            else
                normal_samples += output.size();
        }
        CHECK(normal_samples > fast_samples);
        CHECK(always.stats().notes == fading.stats().notes);
        CHECK(always.cue_stats().notes == fading.cue_stats().notes);
        app::EnhancedFrame end;
        end.sequence = frames.size();
        always.mix(end, {});
        fading.mix(end, {});
        CHECK(always.active_notes() == 0);
        CHECK(fading.active_notes() == 0);
        CHECK(held.empty());
        CHECK(always.stats().unmapped == 0);
        CHECK(always.cue_stats().unmapped == 0);
    }
}

TEST_CASE("enhanced music: cues preserve field timing, own only their slots and release completely") {
    const auto rom = music_rom();
    if (rom.empty())
        return;
    for (int id : {136, 137, 152, 153, 178}) {
        CAPTURE(id);
        emu::Wsc w(rom, 0);
        app::EnhancedObserver observer;
        observer.attach(game::Session::of(w));
        music_init(w);
        music_request(w, 138);
        std::vector<app::EnhancedFrame> frames;
        for (int i = 0; i < 100; ++i) {
            app::paused_tick(w);
            w.apu.take();
            auto f = observer.frame(w);
            f.samples.assign(636, 0);
            f.gain = f.cue_gain = 1;
            frames.push_back(std::move(f));
        }
        const auto generation = frames.back().generation;
        const auto ticks = frames.back().channels[4].ticks;
        music_request(w, id);
        uint8_t cue_mask = 0;
        for (const auto &voice : app::enhanced_song(id)->voices)
            cue_mask |= uint8_t(1 << voice.slot);
        const uint8_t initial_slots = observer.wsc_music_slots(w);
        CHECK((initial_slots & cue_mask) == cue_mask);
        // Cue music obeys Music volume even with SFX muted, independently of an unmapped field song.
        app::apply_channel_gains(w, 0.3f, 0, observer.wsc_music_slots(w));
        for (int hw = 0; hw < 3; ++hw)
            CHECK(w.apu.output_gain[size_t(hw)] == 0.3f);
        app::apply_cue_gain(w, observer.wsc_music_slots(w), 0);
        for (int hw = 0; hw < 3; ++hw)
            CHECK(w.apu.output_gain[size_t(hw)] == ((initial_slots & (1 << hw)) ? 0 : 0.3f));
        for (int i = 0; i < 480; ++i) {
            app::paused_tick(w);
            // 153 releases unused slots on its first update. Other cues can have
            // silent rest-only tracks, so their ownership exceeds the note voices.
            if (i == 0 && id == 153)
                CHECK(observer.wsc_music_slots(w) == cue_mask);
            w.apu.take();
            auto f = observer.frame(w);
            CHECK(f.song == 138);
            CHECK(f.generation == generation);
            f.samples.assign(636, 0);
            f.gain = f.cue_gain = 1;
            frames.push_back(std::move(f));
        }
        CHECK(observer.wsc_music_slots(w) == 0); // finite cue ended, the field owns its hardware again
        CHECK(frames.back().channels[4].ticks > ticks);
        app::EnhancedRenderer r(music_sf());
        if (!r.available())
            return;
        for (size_t i = 0; i < frames.size(); ++i)
            r.mix(frames[i],
                  std::span<const app::EnhancedFrame>(frames).subspan(
                      i + 1, std::min<size_t>(app::EnhancedAudio::kLookahead, frames.size() - i - 1)));
        CHECK(r.cue_stats().notes > 0);
        // All cue note-offs happened naturally; the remaining held notes belong to the field.
        std::vector<app::EnhancedRenderer::NoteEvent> events;
        r.set_event_sink([&](const auto &e) { events.push_back(e); });
        app::EnhancedFrame end;
        end.sequence = frames.size();
        r.mix(end, {});
        CHECK(r.active_notes() == 0);
        CHECK(std::none_of(events.begin(), events.end(), [id](const auto &e) { return e.song == id; }));

        // Immediate alias 160 resolves to 178; a subsequent accepted SFX revokes only replaced slots.
        music_request(w, 160, true);
        auto alias = observer.frame(w);
        CHECK(alias.cue == 178);
        music_request(w, 1, true);
        auto interrupted = observer.frame(w);
        CHECK((interrupted.cue_slots & alias.cue_slots) != alias.cue_slots);
        CHECK(interrupted.song == 138);
        music_request(w, 136);
        auto replacement = observer.frame(w);
        CHECK(replacement.cue == 136);
        CHECK(replacement.cue_generation != alias.cue_generation);
        music_init(w);
        auto reset = observer.frame(w);
        CHECK(reset.song == 0);
        CHECK(reset.cue == 0);
        CHECK(reset.cue_slots == 0);

        // Use the valid 142 driver under an unmapped host ID to exercise fallback cue routing.
        music_request(w, 142);
        observer.request(134);
        music_request(w, id);
        std::vector<app::EnhancedFrame> interrupted_frames;
        for (int i = 0; i < 180; ++i) {
            if (i == 60)
                music_request(w, 1, true);
            if (i == 85)
                music_request(w, 136);
            if (i == 110)
                music_request(w, 160, true);
            if (i == 140)
                music_request(w, 160, true); // Same alias retrigger must stop every old note.
            if (i == 155)
                music_request(
                    w, 153); // The newly locked finite cue plays Enhanced above an unmapped field song.
            if (i == 165)
                music_init(w);
            const auto mix = app::music_mix({0, 0, 1}, false, false, observer.playing(w));
            const auto cue_mix = app::music_mix({0, 0, 1}, false, false, observer.cue_playing(w));
            if (i >= 155 && i < 165) {
                CHECK(cue_mix.wsc == 0);
                CHECK(cue_mix.enhanced == 1);
                CHECK(observer.wsc_music_slots(w) != 0);
            }
            CHECK(mix.wsc == 1); // The unmapped field song keeps its WSC gain throughout.
            app::apply_channel_gains(w, mix.wsc, 0, observer.wsc_music_slots(w));
            app::apply_cue_gain(w, observer.wsc_music_slots(w), cue_mix.wsc);
            app::paused_tick(w);
            w.apu.take();
            auto f = observer.frame(w);
            f.samples.assign(636, 0);
            f.gain = mix.enhanced;
            f.cue_gain = cue_mix.enhanced;
            interrupted_frames.push_back(std::move(f));
        }
        std::map<std::pair<int, int>, int> held;
        r.set_event_sink([&](const auto &e) {
            const auto key = std::pair{e.song, e.voice};
            if (e.velocity) {
                CHECK_FALSE(held.contains(key));
                held[key] = e.key;
            } else {
                REQUIRE(held.contains(key));
                CHECK(held[key] == e.key);
                held.erase(key);
            }
        });
        for (size_t i = 0; i < interrupted_frames.size(); ++i)
            r.mix(interrupted_frames[i],
                  std::span<const app::EnhancedFrame>(interrupted_frames)
                      .subspan(i + 1, std::min<size_t>(app::EnhancedAudio::kLookahead,
                                                       interrupted_frames.size() - i - 1)));
        CHECK(r.active_notes() == 0);
        CHECK(held.empty());
        CHECK(r.cue_stats().unmapped == 0);
    }
}

TEST_CASE("enhanced music: effect 0 ends a cue but not the song on slots 4-7") {
    const auto rom = music_rom();
    if (rom.empty())
        return;
    // The old man's riddle reward: victory (149) on the music slots, then the script's effect 0 on 0-3.
    emu::Wsc w(rom, 0);
    app::EnhancedObserver observer;
    observer.attach(game::Session::of(w));
    music_init(w);
    music_request(w, 152); // a cue on 0-3 first, so effect 0 has one to end
    music_request(w, 149);
    for (int i = 0; i < 10; ++i)
        app::paused_tick(w);
    CHECK(observer.cue_playing(w));
    const auto generation = observer.generation();
    music_request(w, 0, true);
    CHECK(!observer.cue_playing(w));
    CHECK(observer.playing(w));
    CHECK(observer.generation() == generation);
    app::paused_tick(w);
    CHECK(observer.frame(w).song == 149);
    CHECK(w.ram[0x62C + 4 * 0x32] != 0xFF); // the driver still plays the song (slot 4 step is live)
}

TEST_CASE("enhanced music: worker and synchronous renderer share Fast-Forward and lookahead") {
    app::EnhancedAudio worker(music_sf());
    worker.wait_initialized();
    if (!worker.available())
        return;
    app::EnhancedRenderer direct(music_sf());
    std::vector<app::EnhancedFrame> frames;
    for (int i = 0; i < 80; ++i) {
        app::EnhancedFrame f;
        f.song = 138;
        f.generation = 1;
        f.sequence = uint64_t(i);
        f.gain = 1;
        f.cue = i >= 10 && i < 40 ? 136 : i >= 40 && i < 70 ? 137 : 0;
        f.cue_generation = i < 40 ? 1 : i < 70 ? 2 : 3;
        f.cue_slots = f.cue ? 7 : 0;
        f.cue_gain = 1;
        f.sfx = f.cue ? 7 : 0;
        f.speed = i >= 25 && i < 55 ? 4 : 1;
        f.samples.assign(636, 0);
        auto &c = f.channels[4];
        c.event = i % 10 == 0 ? 'N' : '-';
        c.instrument = 7;
        c.hw = 0;
        c.step = uint16_t(i / 10 + 2);
        c.pitch = c.period = 1829;
        c.volume = 0xAA;
        c.ticks = i;
        for (int slot = 0; slot < 3; ++slot) {
            f.channels[size_t(slot)] = c;
            f.channels[size_t(slot)].hw = uint8_t(slot);
            f.channels[size_t(slot)].instrument = slot == 1 && f.cue == 136 ? 5 : 4;
        }
        frames.push_back(std::move(f));
    }
    std::vector<int16_t> actual, expected;
    const auto drain = [&] {
        auto samples = worker.take();
        actual.insert(actual.end(), samples.begin(), samples.end());
    };
    for (auto f : frames) {
        while (!worker.submit(f)) {
            drain();
            std::this_thread::yield();
        }
        drain();
    }
    worker.finish(); // 80 packets fit the output queue even without another drain.
    drain();
    app::FastForwardAudio ff;
    for (size_t i = 0; i < frames.size(); ++i)
        direct.mix(frames[i], std::span<const app::EnhancedFrame>(frames).subspan(
                                  i + 1, std::min<size_t>(8, frames.size() - i - 1)));
    for (size_t i = 0; i < frames.size() + 8; ++i) {
        const auto samples = i < 8 ? std::vector<int16_t>(636, 0) : frames[i - 8].samples;
        const int speed = i < frames.size() ? frames[i].speed : frames[i - 8].speed;
        const auto pcm = ff.process(samples, speed);
        expected.insert(expected.end(), pcm.begin(), pcm.end());
    }
    CHECK(actual == expected);
    CHECK(worker.cost().frames == frames.size());
}

TEST_CASE("enhanced music: accelerated game pause and resume leave RAM and driver state exact") {
    const auto rom = music_rom();
    if (rom.empty())
        return;
    emu::Wsc w(rom, 0x2000), original(rom, 0x2000);
    game::install(w);
    game::install(original);
    w.render_lcd = original.render_lcd = false;
    app::EnhancedObserver observer;
    observer.attach(game::Session::of(w));
    app::EnhancedRenderer renderer(music_sf());
    std::vector<app::EnhancedFrame> frames;
    int paused_notes = 0;
    for (int i = 0; i < 900; ++i) {
        app::apply_channel_gains(w, observer.playing(w) ? 0.0f : 1.0f, 1, observer.wsc_music_slots(w));
        if (i >= 500 && i < 650) {
            app::paused_tick(w);
            app::paused_tick(original);
        } else {
            w.run_frame();
            original.run_frame();
        }
        check_hardware(w, original);
        auto f = observer.frame(w);
        if (i >= 500 && i < 650)
            for (const auto &c : f.channels)
                paused_notes += c.event == 'N';
        f.samples = w.apu.take();
        f.gain = f.cue_gain = 1;
        original.apu.take();
        frames.push_back(std::move(f));
    }
    CHECK(observer.playing(w));
    CHECK(paused_notes > 0);
    CHECK(w.sram == original.sram);
    if (renderer.available()) {
        for (size_t i = 0; i < frames.size(); ++i)
            renderer.mix(frames[i],
                         std::span<const app::EnhancedFrame>(frames).subspan(
                             i + 1, std::min<size_t>(app::EnhancedAudio::kLookahead, frames.size() - i - 1)));
        CHECK(renderer.stats().unmapped == 0);
        CHECK(renderer.stats().notes > 0);
        app::EnhancedFrame end;
        end.sequence = frames.size();
        renderer.mix(end, {});
        CHECK(renderer.active_notes() == 0);
    }
}

TEST_CASE("enhanced music: locked WSC GB and unavailable Enhanced have zero music delay") {
    for (auto mode : {FidelityMode::Wsc, FidelityMode::Gb, FidelityMode::Enhanced}) {
        app::AudioLatency timing;
        app::EnhancedAudio worker(mode == FidelityMode::Enhanced ? "/missing/sawgod-test.sf2" : music_sf());
        worker.wait_initialized();
        for (uint64_t i = 0; i < 12; ++i) {
            const auto plan = timing.frame(mode, worker.available(), i * 13'250'000, false);
            CHECK(plan.lookahead == 0);
            app::EnhancedFrame f;
            f.lookahead = mode == FidelityMode::Enhanced ? 8 : plan.lookahead;
            f.samples = {int16_t(100 + i), int16_t(-100 - int(i))};
            REQUIRE(worker.submit(f));
            worker.wait_processed(i + 1);
            CHECK(worker.take() == f.samples);
            CHECK(worker.playback().queued == 0);
        }
        worker.finish();
        CHECK(worker.take().empty());
        CHECK(worker.dropped() == 0);
    }
}

TEST_CASE("enhanced music: all reachable modes retain eight music frames and emit SFX immediately") {
    for (auto mode : {FidelityMode::Wsc, FidelityMode::Gb, FidelityMode::Enhanced}) {
        app::EnhancedAudio worker(music_sf());
        worker.wait_initialized();
        if (!worker.available()) // no FluidSynth library here (e.g. the Windows build under Wine)
            return;
        app::AudioLatency timing;
        for (uint64_t i = 0; i < 20; ++i) {
            const auto plan = timing.frame(mode, true, i * 13'250'000);
            REQUIRE(plan.lookahead == 8);
            app::EnhancedFrame f;
            f.song = 134; // Unmapped fallback music shares exactly the same cursor.
            f.lookahead = plan.lookahead;
            f.samples = {int16_t(100 + i), int16_t(100 + i)};
            f.effects = {int16_t(1000 + i), int16_t(1000 + i)};
            REQUIRE(worker.submit(f));
            worker.wait_processed(i + 1);
            const int16_t value = int16_t(1000 + i + (i < 8 ? 0 : 100 + i - 8));
            CHECK(worker.take() == std::vector<int16_t>{value, value});
            CHECK(worker.playback().queued == std::min<uint64_t>(i + 1, 8));
        }
        worker.finish();
        CHECK(worker.take().size() == 8 * 2);
        CHECK(worker.playback().emitted == 20);
        CHECK(worker.dropped() == 0);
    }
}

TEST_CASE("enhanced music: rapid fidelity reversals never move the music cursor") {
    for (int interrupt = 1; interrupt <= 35; ++interrupt) {
        app::AudioLatency timing;
        for (int i = 0; i < 300; ++i) {
            const auto target = i >= 20 && i < 20 + interrupt ? FidelityMode::Enhanced : FidelityMode::Wsc;
            CHECK(timing.frame(target, true, uint64_t(i) * 13'250'000).lookahead == 8);
        }
        CHECK(timing.frame(FidelityMode::Wsc, true, 5'000'000'000, false, 0).lookahead == 8);
        CHECK(timing.frame(FidelityMode::Wsc, true, 6'000'000'000, false, 1).lookahead == 0);
    }
}

TEST_CASE("enhanced music: mid-song and mid-jingle switches keep full-future notes during FF and pause") {
    if (!app::EnhancedRenderer(music_sf()).available()) // no FluidSynth library here
        return;
    const auto rom = music_rom();
    REQUIRE_FALSE(rom.empty());
    for (bool jingle : {false, true})
        for (bool fast : {false, true})
            for (bool paused : {false, true}) {
                CAPTURE(jingle);
                CAPTURE(fast);
                CAPTURE(paused);
                emu::Wsc w(rom, 0), original(rom, 0);
                game::install(w);
                game::install(original);
                music_init(w);
                music_init(original);
                app::EnhancedObserver observer;
                observer.attach(game::Session::of(w));
                music_request(w, 144);
                music_request(original, 144);
                std::vector<app::EnhancedFrame> frames;
                app::AudioLatency timing;
                Config config;
                Settings settings;
                settings.register_all(config);
                config.set(*settings.ff_speed, 2);
                const int speed = app::frame_speed(settings, paused, fast, false).speed;
                CHECK(speed == (fast && !paused ? 4 : 1));
                for (int i = 0; i < 720; ++i) {
                    if (jingle && (i == 20 || i == 200 || i == 320)) {
                        const int cue = i == 320 ? 137 : 136;
                        music_request(w, cue);
                        music_request(original, cue);
                    }
                    // Pause runs only the real driver; FF supplies four of these ticks per wall tick.
                    if (paused || i < 400) {
                        app::paused_tick(w);
                        app::paused_tick(original);
                    } else {
                        w.run_frame();
                        original.run_frame();
                    }
                    check_hardware(w, original);
                    auto f = observer.frame(w);
                    w.apu.take();
                    original.apu.take();
                    const auto mode = i >= 30 && i < 320 ? FidelityMode::Enhanced
                                      : i < 30           ? FidelityMode::Wsc
                                                         : FidelityMode::Gb;
                    const auto p = timing.frame(mode, true, uint64_t(i) * 13'250'000 / speed);
                    CHECK((p.weights[2] == 0 || p.lookahead == 8));
                    f.lookahead = p.lookahead;
                    f.gain = f.cue_gain = p.weights[2];
                    f.speed = speed;
                    f.samples.assign(636, 0);
                    frames.push_back(std::move(f));
                }
                // End the field and cue streams so the held-note ledger can be checked.
                app::EnhancedFrame end;
                end.samples.assign(636, 0);
                end.lookahead = 8;
                end.sequence = frames.size();
                frames.push_back(end);
                app::EnhancedRenderer direct(music_sf());
                REQUIRE(direct.available());
                std::map<std::tuple<int, int, int>, int> held;
                direct.set_event_sink([&](const auto &e) {
                    auto key = std::tuple(e.song, e.voice, e.key);
                    if (e.velocity)
                        ++held[key];
                    else if (--held[key] == 0)
                        held.erase(key);
                });
                app::EnhancedAudio worker(music_sf());
                worker.wait_initialized();
                REQUIRE(worker.available());
                std::vector<int16_t> expected, actual;
                app::FastForwardAudio ff;
                // Use the same scheduling contract with an independent full-future renderer.
                std::deque<size_t> pending;
                for (size_t i = 0; i < frames.size(); ++i) {
                    REQUIRE(worker.submit(frames[i]));
                    worker.wait_processed(i + 1);
                    auto out = worker.take();
                    actual.insert(actual.end(), out.begin(), out.end());
                    pending.push_back(i);
                    if (pending.size() <= size_t(frames[i].lookahead)) {
                        auto silence = ff.process(std::vector<int16_t>(636, 0), frames[i].speed);
                        expected.insert(expected.end(), silence.begin(), silence.end());
                    }
                    while (pending.size() > size_t(frames[i].lookahead)) {
                        const auto index = pending.front();
                        pending.pop_front();
                        auto f = frames[index];
                        direct.mix(f, std::span<const app::EnhancedFrame>(frames).subspan(
                                          index + 1, std::min<size_t>(8, frames.size() - index - 1)));
                        // Muted PCM is exactly the bypass stream even before its future is available.
                        if (index + 8 > i)
                            REQUIRE(f.gain == 0);
                        auto compressed = ff.process(f.samples, frames[i].speed);
                        expected.insert(expected.end(), compressed.begin(), compressed.end());
                    }
                }
                while (!pending.empty()) {
                    const auto index = pending.front();
                    pending.pop_front();
                    auto f = frames[index];
                    direct.mix(f, std::span<const app::EnhancedFrame>(frames).subspan(index + 1));
                    auto pcm = ff.process(f.samples, f.speed);
                    expected.insert(expected.end(), pcm.begin(), pcm.end());
                }
                worker.finish();
                auto out = worker.take();
                actual.insert(actual.end(), out.begin(), out.end());
                CHECK(actual == expected);
                CHECK(worker.playback().emitted == frames.size());
                CHECK(worker.dropped() == 0);
                CHECK(held.empty());
                CHECK(direct.active_notes() == 0);
                CHECK(direct.stats().unmapped == 0);
                CHECK(direct.cue_stats().unmapped == 0);
            }
}

TEST_CASE("enhanced music: direct output delivers every packet while the main thread does not take") {
    app::EnhancedAudio worker("");
    worker.wait_initialized();
    std::vector<int16_t> delivered;
    worker.set_output_sink([&](std::vector<int16_t> samples) {
        delivered.insert(delivered.end(), samples.begin(), samples.end());
    });
    std::vector<int16_t> expected;
    for (uint64_t i = 0; i < 160; ++i) {
        app::EnhancedFrame frame;
        frame.sequence = i;
        frame.lookahead = 0;
        frame.samples.assign(636, int16_t(100 + i));
        expected.insert(expected.end(), frame.samples.begin(), frame.samples.end());
        REQUIRE(worker.submit(std::move(frame)));
        worker.wait_processed(i + 1);
    }
    worker.finish();
    CHECK(delivered == expected);
    CHECK(worker.take().empty());
    CHECK(worker.dropped() == 0);
}
