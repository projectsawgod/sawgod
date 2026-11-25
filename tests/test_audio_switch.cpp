#include "app/audio_latency.h"
#include "app/display_motion.h"
#include "app/enhanced_music.h"
#include "app/gb_music.h"
#include "app/host_settings.h"
#include "app/present_clock.h"
#include "app/sound_mix.h"
#include "emu/gb_song.h"
#include "game/bank3d_sound.h"
#include "game/fiber.h"
#include "game/setup.h"
#include "tools/difftest_inputs.h"
#include <cstdlib>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
using namespace sawgod;
namespace {
std::vector<uint8_t> audio_rom(const char *name) {
    const char *path = std::getenv(name);
    if (!path)
        return {};
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
void request(emu::Wsc &w, int id) {
    const auto saved = w.cpu.s;
    game::Machine m(w);
    m.ds() = 0;
    m.cs() = 0xD000;
    m.ax() = uint16_t(id);
    game::sound_request_core(m);
    w.cpu.s = saved;
}
void init(emu::Wsc &w) {
    const auto saved = w.cpu.s;
    game::Machine m(w);
    m.ds() = 0;
    m.cs() = 0xD000;
    game::sound_init(m);
    w.cpu.s = saved;
    w.apu.take();
}
std::string sf() {
    const char *path = std::getenv("SAWGOD_SOUNDFONT");
    return path ? path : app::default_soundfont();
}
} // namespace

TEST_CASE("audio: victory driver advances through a host Esper decision without advancing gameplay") {
    const auto rom = audio_rom("SAWGOD_WSC_ROM");
    if (rom.empty()) {
        MESSAGE("Set SAWGOD_WSC_ROM for the Esper decision audio check");
        return;
    }
    for (bool accept : {false, true}) {
        CAPTURE(accept);
        emu::Wsc w(rom, 0);
        app::GbMusic gb;
        REQUIRE(gb.load(rom));
        auto &session = game::Session::of(w);
        gb.attach(session);
        app::EnhancedObserver observer;
        observer.attach(session);
        init(w);
        request(w, 149); // Victory, including its repeating tail.
        session.confirm_esper_changes = true;
        w.rom[0x340200] = 0xF4; // HLT after the suspended native returns.
        w.io[0x60] = 0x80;
        w.cpu.s.seg[emu::CS] = 0x4000;
        w.cpu.s.seg[emu::SS] = 0;
        w.cpu.s.r[emu::SP] = 0x2000;
        w.cpu.s.flags = 0;
        w.cpu.s.ip = 0x100;
        w.ram[0x2000] = 0;
        w.ram[0x2001] = 2;
        w.ram[0xD00F] = 128;
        w.ram[0xD010] = 17;
        w.set_native(0x340100, [&session](emu::Wsc &hw) {
            return session.continue_fiber(
                game::start_yielding(hw,
                                     [](game::Machine &m) {
                                         if (game::Session::of(m.wsc()).allow_esper_change(m, 0, 128, 129)) {
                                             m.wr8(0, 0xD00F, 129);
                                             m.wr8(0, 0xD010, 25);
                                         }
                                         m.ret();
                                     }),
                16);
        });
        app::run_game_frame(w);
        REQUIRE(w.host_paused);
        REQUIRE(session.esper_change());
        const auto frozen = w.snapshot();
        const auto generation = observer.generation();
        const auto *map = app::gb_song_map(149);
        REQUIRE(map);
        std::vector<app::EnhancedFrame> packets;
        int last_tick = 0;
        int previous_step = 0, loops = 0;
        bool audible_wsc_tail = false, audible_gb_tail = false;
        for (int frame = 0; frame < 2400; ++frame) {
            const auto before = w.ram[0x604]; // sound_tick's frame counter
            app::run_game_frame(w, false);    // Battle Speed's extra game step must not clock music.
            CHECK(w.ram[0x604] == before);
            app::run_game_frame(w);
            REQUIRE(w.ram[0x604] == uint8_t(before + 1));
            auto samples = w.apu.take();
            if (frame > 0)
                REQUIRE(samples.size() == 636);
            if (frame >= 2000)
                audible_wsc_tail |=
                    std::any_of(samples.begin(), samples.end(), [](int16_t v) { return v != 0; });
            std::fill(samples.begin(), samples.end(), 0);
            gb.frame(w, samples, 1, 1);
            REQUIRE(gb.playing());
            if (frame >= 2000)
                audible_gb_tail |=
                    std::any_of(samples.begin(), samples.end(), [](int16_t v) { return v != 0; });
            auto packet = observer.frame(w);
            REQUIRE(packet.song == 149);
            REQUIRE(packet.generation == generation);
            CHECK(packet.channels[4].ticks >= last_tick);
            last_tick = packet.channels[4].ticks;
            const auto step = packet.channels[4].step;
            REQUIRE(step != 0xFFFF);
            if (step < previous_step)
                ++loops;
            previous_step = step;
            packet.samples.assign(samples.size(), 0);
            packet.gain = 1;
            packets.push_back(std::move(packet));
        }
        CHECK(last_tick > map->loop_start + 2 * map->loop_ticks);
        CHECK(loops >= 2); // The real WSC track jumps back while GB and Enhanced keep their absolute clocks.
        CHECK(audible_wsc_tail);
        CHECK(audible_gb_tail);
        app::EnhancedRenderer renderer(sf());
        if (renderer.available()) {
            bool audible_enhanced_tail = false;
            for (size_t i = 0; i < packets.size(); ++i) {
                renderer.mix(packets[i], std::span<const app::EnhancedFrame>(packets).subspan(
                                             i + 1, std::min<size_t>(8, packets.size() - i - 1)));
                if (i >= 2000)
                    audible_enhanced_tail |= std::any_of(packets[i].samples.begin(), packets[i].samples.end(),
                                                         [](int16_t v) { return v != 0; });
            }
            CHECK(audible_enhanced_tail);
        } else {
            MESSAGE(
                "Enhanced PCM check needs FluidSynth and SAWGOD_SOUNDFONT; driver observation still checked");
        }
        REQUIRE(session.esper_change());
        CHECK(session.suspended() == 1);
        CHECK(w.ram[0xD00F] == 128);
        CHECK(w.ram[0xD010] == 17);
        CHECK(w.cpu.cycles == frozen.cpu.cycles);
        CHECK(w.frame_count == frozen.frame_count);
        CHECK(w.scanline == frozen.scanline);
        CHECK(w.line_cycle == frozen.line_cycle);
        CHECK(std::equal(std::begin(w.cpu.s.r), std::end(w.cpu.s.r), std::begin(frozen.cpu.s.r)));
        CHECK(std::equal(std::begin(w.cpu.s.seg), std::end(w.cpu.s.seg), std::begin(frozen.cpu.s.seg)));
        CHECK(w.cpu.s.ip == frozen.cpu.s.ip);
        CHECK(w.cpu.s.flags == frozen.cpu.s.flags);
        CHECK(std::equal(w.ram.begin() + 0x1FF0, w.ram.begin() + 0x2004, frozen.ram.begin() + 0x1FF0));
        const std::vector<uint8_t> driver(w.ram.begin() + 0x600, w.ram.begin() + 0x7BC);
        for (int hold = 0; hold < game::Session::kEsperInputHold; ++hold)
            session.esper_input(w, 0);
        session.esper_input(w, 0);
        session.esper_input(w, accept ? emu::KEY_A : emu::KEY_B);
        CHECK_FALSE(w.host_paused);
        CHECK_FALSE(session.esper_change());
        CHECK(session.suspended() == 0);
        CHECK(w.ram[0xD00F] == (accept ? 129 : 128));
        CHECK(w.ram[0xD010] == (accept ? 25 : 17));
        CHECK(std::equal(driver.begin(), driver.end(), w.ram.begin() + 0x600));
        CHECK(observer.generation() == generation);
        app::run_game_frame(w, false); // Finish the original partial frame after either answer.
        CHECK(w.frame_count == frozen.frame_count + 1);
        CHECK(std::equal(driver.begin(), driver.end(), w.ram.begin() + 0x600));
    }
}

TEST_CASE(
    "audio switch: real mixer preserves every sample and driver tick through fades loops tempo and cues") {
    if (!std::getenv("SAWGOD_WSC_ROM") || !std::getenv("SAWGOD_GB_ROM")) {
        MESSAGE("ROM check skipped: set SAWGOD_WSC_ROM and SAWGOD_GB_ROM");
        return;
    }
    const auto rom = audio_rom("SAWGOD_WSC_ROM"), gb_rom = audio_rom("SAWGOD_GB_ROM");
    REQUIRE_FALSE(rom.empty());
    REQUIRE(gb_rom.size() == 131072);
    for (const int song : {138, 145, 142}) {
        CAPTURE(song);
        emu::Wsc w(rom, 0);
        app::GbMusic gb;
        REQUIRE(gb.load(gb_rom));
        gb.attach(game::Session::of(w));
        app::EnhancedObserver observer;
        observer.attach(game::Session::of(w));
        init(w);
        request(w, song);
        w.apu.effect_channels = [&] {
            return app::sfx_channels(w, gb.wsc_music_slots(w) | observer.wsc_music_slots(w));
        };
        app::AudioLatency timing;
        std::vector<app::EnhancedFrame> frames;
        std::vector<Fidelity::Weights> weights;
        FidelityMode mode = FidelityMode::Wsc;
        const auto *casting = app::enhanced_song(song);
        const int seam = casting->drums.loop_tick + casting->drums.loop_ticks;
        bool seam_switch = false, tempo_switch = false, cue_switch = false;
        int previous_tick = 0;
        constexpr int count = 7500;
        for (int i = 0; i < count; ++i) {
            if (i % 302 == 0) {
                const FidelityMode modes[] = {FidelityMode::Wsc, FidelityMode::Enhanced,
                                              FidelityMode::Gb,  FidelityMode::Enhanced,
                                              FidelityMode::Wsc, FidelityMode::Gb};
                mode = modes[(i / 302) % 6];
            }
            if (i == 604) {
                // A controlled real-driver tempo change on the exact frame of a fidelity switch.
                for (int slot = 4; slot < 8; ++slot)
                    w.ram[size_t(0x62C + slot * 0x32 + 6)] = 1;
                tempo_switch = true;
            }
            if (i == 302) {
                request(w, 136);
                cue_switch = true;
            }
            if (i == 1510)
                request(w, 23); // mapped ordinary effect, independent native GB clock
            if (song != 138 && !seam_switch && previous_tick >= seam - 4) {
                mode = mode == FidelityMode::Enhanced ? FidelityMode::Wsc : FidelityMode::Enhanced;
                seam_switch = true;
            }
            const auto plan = timing.frame(mode, true, uint64_t(i) * 13'250'000, true, observer.generation());
            REQUIRE(plan.lookahead == 8);
            const auto mix = app::music_mix(plan.weights, gb.loaded(), gb.playing(), observer.playing(w));
            const auto cue = app::music_mix(plan.weights, gb.loaded(), gb.playing(), observer.cue_playing(w));
            const auto slots = observer.wsc_music_slots(w);
            app::apply_channel_gains(w, mix.wsc, 1, gb.wsc_music_slots(w) | slots, gb.mapped_wsc_slots(w),
                                     mix.gb);
            app::apply_cue_gain(w, slots, cue.wsc);
            app::paused_tick(w);
            auto music = w.apu.take(), effects = w.apu.take_effects();
            gb.frame(w, music, mix.gb, mix.gb, &effects);
            auto f = observer.frame(w);
            previous_tick = f.channels[4].ticks;
            REQUIRE(music.size() == 636);
            REQUIRE(effects.size() == 636);
            f.samples = std::move(music);
            f.effects = std::move(effects);
            f.gain = plan.weights[2];
            f.cue_gain = cue.enhanced;
            f.lookahead = plan.lookahead;
            frames.push_back(std::move(f));
            weights.push_back(plan.weights);
        }
        CHECK(tempo_switch);
        CHECK(cue_switch);
        if (song != 138)
            CHECK(seam_switch);
        app::EnhancedRenderer direct(sf());
        if (!direct.available()) // no FluidSynth library here (e.g. the Windows build under Wine)
            continue;
        size_t drums = 0, layered_notes = 0;
        direct.set_drum_sink([&](const auto &e) {
            if (e.velocity)
                ++drums;
        });
        direct.set_event_sink([&](const auto &e) {
            if (e.velocity && e.voice >= 4)
                ++layered_notes;
        });
        app::EnhancedAudio worker(sf());
        worker.wait_initialized();
        REQUIRE(worker.available());
        uint64_t samples_checked = 0, crossfade_samples = 0, gb_wsc_samples = 0;
        std::vector<app::EnhancedAudio::PlaybackEvent> playback_events;
        worker.set_playback_sink([&](const auto &e) { playback_events.push_back(e); });
        const auto check_event = [&](const auto &e) {
            REQUIRE(e.enhanced_rendered);
            REQUIRE(e.sequence < frames.size());
            REQUIRE(e.samples == 318);
            // Every output sample carries the same immutable source frame for WSC, GB, synth,
            // percussion and layers. A cursor slip or lost lookahead changes this identity or PCM.
            for (size_t sample = 0; sample < e.samples; ++sample) {
                CHECK(e.enhanced_sequence == e.sequence);
                CHECK(frames[e.enhanced_sequence].channels[4].ticks == frames[e.sequence].channels[4].ticks);
                ++samples_checked;
                if (weights[e.sequence][1] > 0 && weights[e.sequence][2] > 0)
                    ++crossfade_samples;
                if (weights[e.sequence][0] > 0 && weights[e.sequence][1] > 0)
                    ++gb_wsc_samples;
            }
        };
        std::vector<std::vector<int16_t>> rendered;
        rendered.reserve(frames.size());
        for (size_t i = 0; i < frames.size(); ++i) {
            auto f = frames[i];
            direct.mix(f, std::span<const app::EnhancedFrame>(frames).subspan(
                              i + 1, std::min<size_t>(8, frames.size() - i - 1)));
            rendered.push_back(std::move(f.samples));
            REQUIRE(worker.submit(frames[i]));
            worker.wait_processed(i + 1);
            auto actual = worker.take();
            REQUIRE(actual.size() == 636); // No rate conversion, stretching, skipped or repeated samples.
            std::vector<int16_t> expected(636, 0);
            if (i >= 8)
                expected = rendered[i - 8];
            for (size_t j = 0; j < expected.size(); ++j)
                expected[j] =
                    int16_t(std::clamp(int(expected[j]) + int(frames[i].effects[j]), -32768, 32767));
            CHECK(actual == expected); // Every sample, including SFX at current rather than delayed time.
        }
        worker.finish();
        std::vector<int16_t> tail;
        for (size_t i = frames.size() - 8; i < frames.size(); ++i)
            tail.insert(tail.end(), rendered[i].begin(), rendered[i].end());
        CHECK(worker.take() == tail);
        for (const auto &event : playback_events)
            check_event(event);
        CHECK(samples_checked == count * 318);
        CHECK(crossfade_samples > 0);
        CHECK(gb_wsc_samples > 0);
        CHECK(worker.dropped() == 0);
        if (song != 138)
            CHECK(drums > 100);
        if (song == 142)
            CHECK(layered_notes > 100);
    }
}

TEST_CASE("audio switch: separated chip outputs preserve the original GB WSC mix and effect clock") {
    if (!std::getenv("SAWGOD_WSC_ROM") || !std::getenv("SAWGOD_GB_ROM")) {
        MESSAGE("ROM check skipped: set SAWGOD_WSC_ROM and SAWGOD_GB_ROM");
        return;
    }
    const auto rom = audio_rom("SAWGOD_WSC_ROM"), gb_rom = audio_rom("SAWGOD_GB_ROM");
    REQUIRE_FALSE(rom.empty());
    REQUIRE(gb_rom.size() == 131072);
    emu::Wsc split(rom, 0), original(rom, 0);
    app::GbMusic a, b;
    REQUIRE(a.load(gb_rom));
    REQUIRE(b.load(gb_rom));
    a.attach(game::Session::of(split));
    b.attach(game::Session::of(original));
    init(split);
    init(original);
    request(split, 138);
    request(original, 138);
    split.apu.effect_channels = [&] { return app::sfx_channels(split, a.wsc_music_slots(split)); };
    uint64_t effect_samples = 0;
    for (int i = 0; i < 480; ++i) {
        if (i == 100) {
            request(split, 23);
            request(original, 23);
        }
        const float gb = i < 240 ? float(i) / 240 : 1 - float(i - 240) / 240;
        app::apply_channel_gains(split, 1 - gb, 1, a.wsc_music_slots(split), a.mapped_wsc_slots(split), gb);
        app::apply_channel_gains(original, 1 - gb, 1, b.wsc_music_slots(original),
                                 b.mapped_wsc_slots(original), gb);
        app::paused_tick(split);
        app::paused_tick(original);
        auto music = split.apu.take(), effects = split.apu.take_effects(), full = original.apu.take();
        a.frame(split, music, gb, gb, &effects);
        b.frame(original, full, gb, gb);
        REQUIRE(music.size() == full.size());
        REQUIRE(effects.size() == full.size());
        for (size_t j = 0; j < full.size(); ++j) {
            if (i < 100)
                CHECK(full[j] == music[j]); // GB/WSC fades are bit exact without ordinary SFX.
            CHECK(std::abs(int(full[j]) - std::clamp(int(music[j]) + int(effects[j]), -32768, 32767)) <= 2);
            if (effects[j])
                ++effect_samples;
        }
        CHECK(split.ram == original.ram);
        CHECK(split.io == original.io);
        CHECK(a.sound_host()->effect_frames() == b.sound_host()->effect_frames());
    }
    CHECK(effect_samples > 0);
}

TEST_CASE("Lo-Spec keeps recorded-input machine state and every native audio packet identical to Smooth") {
    const auto rom = audio_rom("SAWGOD_WSC_ROM");
    if (rom.empty()) {
        MESSAGE("Set SAWGOD_WSC_ROM for the Lo-Spec native clock comparison");
        return;
    }
    auto smooth = game::make_decomp_game(rom), lospec = game::make_decomp_game(rom);
    app::EnhancedObserver smooth_audio, lospec_audio;
    smooth_audio.attach(game::Session::of(*smooth));
    lospec_audio.attach(game::Session::of(*lospec));
    const tools::MovieInputs recording(
        {{0, 0}, {180, emu::KEY_A}, {182, 0}, {250, emu::KEY_A}, {252, 0}, {330, emu::KEY_Y2}, {345, 0}}, {},
        {});
    app::LoSpecCadence cadence;
    app::MotionHistory motion;
    uint64_t next_tick = 0, tick = 0;
    int presentations = 0;
    for (uint64_t now = 0; tick < 400; now += 1'000'000) {
        while (next_tick <= now && tick < 400) {
            const auto keys = recording.keys_at(tick);
            smooth->set_keys(keys);
            lospec->set_keys(keys);
            app::run_game_frame(*smooth);
            app::run_game_frame(*lospec);
            const auto samples = smooth->apu.take(), reduced = lospec->apu.take();
            REQUIRE(samples == reduced);
            REQUIRE(smooth->apu.take_effects() == lospec->apu.take_effects());
            const auto a = smooth_audio.frame(*smooth), b = lospec_audio.frame(*lospec);
            CHECK(a.sequence == b.sequence);
            CHECK(a.song == b.song);
            CHECK(a.cue == b.cue);
            CHECK(a.generation == b.generation);
            CHECK(a.cue_generation == b.cue_generation);
            for (size_t i = 0; i < a.channels.size(); ++i) {
                CHECK(a.channels[i].event == b.channels[i].event);
                CHECK(a.channels[i].pitch == b.channels[i].pitch);
                CHECK(a.channels[i].volume == b.channels[i].volume);
                CHECK(a.channels[i].step == b.channels[i].step);
            }
            app::MotionState state;
            state.frame = ++tick;
            motion.complete(state);
            next_tick += game::kWscFrameNs;
        }
        if (cadence.due(now, 120)) {
            ++presentations;
            (void)motion.sample(double(int64_t(now) + int64_t(game::kWscFrameNs) - int64_t(next_tick)) /
                                double(game::kWscFrameNs));
        }
    }
    const auto a = smooth->snapshot(), b = lospec->snapshot();
    CHECK(a.ram == b.ram);
    CHECK(a.sram == b.sram);
    CHECK(a.io == b.io);
    CHECK(a.fb == b.fb);
    CHECK(a.sprites == b.sprites);
    CHECK(a.frame_count == b.frame_count);
    CHECK(a.scanline == b.scanline);
    CHECK(a.line_cycle == b.line_cycle);
    CHECK(a.keys == b.keys);
    CHECK(a.prev_keys == b.prev_keys);
    CHECK(a.keypad_frame == b.keypad_frame);
    CHECK(a.htimer == b.htimer);
    CHECK(a.vtimer == b.vtimer);
    CHECK(a.stall == b.stall);
    CHECK(a.frame_done == b.frame_done);
    CHECK(a.frame_in_progress == b.frame_in_progress);
    CHECK(a.line_parked == b.line_parked);
    CHECK(std::equal(std::begin(a.cpu.s.r), std::end(a.cpu.s.r), std::begin(b.cpu.s.r)));
    CHECK(std::equal(std::begin(a.cpu.s.seg), std::end(a.cpu.s.seg), std::begin(b.cpu.s.seg)));
    CHECK(a.cpu.s.ip == b.cpu.s.ip);
    CHECK(a.cpu.s.flags == b.cpu.s.flags);
    CHECK(a.sprite_count == b.sprite_count);
    CHECK(a.cpu.s.halted == b.cpu.s.halted);
    CHECK(a.cpu.cycles == b.cpu.cycles);
    CHECK(a.cpu.seg_override == b.cpu.seg_override);
    CHECK(a.cpu.rep == b.cpu.rep);
    CHECK(a.cpu.start_ip == b.cpu.start_ip);
    CHECK(a.cpu.in_prefix == b.cpu.in_prefix);
    CHECK(a.cpu.irq_inhibit == b.cpu.irq_inhibit);
    CHECK(a.cpu.q_head == b.cpu.q_head);
    CHECK(a.cpu.q_fetch == b.cpu.q_fetch);
    CHECK(a.cpu.after_branch == b.cpu.after_branch);
    CHECK(a.apu.lfsr == b.apu.lfsr);
    CHECK(a.apu.sweep_cycles == b.apu.sweep_cycles);
    CHECK(a.apu.sweep_ticks == b.apu.sweep_ticks);
    CHECK(a.apu.sample_cycles == b.apu.sample_cycles);
    CHECK(a.apu.out_left == b.apu.out_left);
    CHECK(a.apu.out_right == b.apu.out_right);
    CHECK(a.apu.dc_l == b.apu.dc_l);
    CHECK(a.apu.dc_r == b.apu.dc_r);
    CHECK(a.apu.prev_l == b.apu.prev_l);
    CHECK(a.apu.prev_r == b.apu.prev_r);
    CHECK(a.apu.effect_dc_l == b.apu.effect_dc_l);
    CHECK(a.apu.effect_dc_r == b.apu.effect_dc_r);
    CHECK(a.apu.effect_prev_l == b.apu.effect_prev_l);
    CHECK(a.apu.effect_prev_r == b.apu.effect_prev_r);
    CHECK(a.apu.effects == b.apu.effects);
    CHECK(a.apu.samples == b.apu.samples);
    CHECK(std::equal(std::begin(a.apu.counter), std::end(a.apu.counter), std::begin(b.apu.counter)));
    CHECK(std::equal(std::begin(a.apu.index), std::end(a.apu.index), std::begin(b.apu.index)));
    CHECK(presentations == 159);
}
