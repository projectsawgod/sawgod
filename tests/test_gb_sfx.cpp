// GB effect ownership and clocks tested through the actual supplied driver.
#include "app/gb_cues.h"
#include "app/gb_music.h"
#include "app/sound_mix.h"
#include "emu/gb_song.h"
#include "emu/wsc.h"
#include "game/bank3d_sound.h"
#include "game/session.h"
#include <cmath>
#include <cstdlib>
#include <doctest/doctest.h>
#include <fstream>
#include <iterator>
#include <memory>

using namespace sawgod;
namespace {
std::vector<uint8_t> source_rom() {
    const char *path = std::getenv("SAWGOD_WSC_ROM");
    if (!path)
        return {};
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::unique_ptr<emu::GbSoundHost> sound(const std::vector<uint8_t> &source) {
    std::vector<uint8_t> rom(source.begin() + 0x320000, source.begin() + 0x340000);
    auto h = std::make_unique<emu::GbSoundHost>(rom, emu::saga1_sound_driver());
    REQUIRE(h->init());
    h->apu.take();
    return h;
}
std::vector<uint8_t> effect_state(emu::GbSoundHost &h) {
    std::vector<uint8_t> state = {uint8_t(h.read(0xCE00) & 1)};
    for (uint16_t a = 0xCEC3; a <= 0xCEC7; ++a)
        state.push_back(h.read(a));
    for (uint16_t a = 0xFFEB; a <= 0xFFEE; ++a)
        state.push_back(h.read(a));
    return state;
}
constexpr double cycles = double(emu::GbApu::kClock) * (159 * 256) / 3072000.0;
} // namespace

TEST_CASE("gb effects: semantic mapping requires context and uses the GB action byte") {
    using C = game::hooks::AudioContext;
    using app::gb_effect_for;
    for (uint8_t id : {uint8_t(5), uint8_t(17), uint8_t(21), uint8_t(24), uint8_t(50), uint8_t(91)})
        CHECK(gb_effect_for(id, C(0), 0) == 0);
    CHECK(gb_effect_for(161, C(0), 0) == 23);
    CHECK(gb_effect_for(23, C(0), 0) == 23);
    CHECK(gb_effect_for(17, C::FieldItem, 0) == 17);
    CHECK(gb_effect_for(93, C::FieldItem, 0) == 17);
    CHECK(gb_effect_for(21, C::Warp, 0) == 21);
    CHECK(gb_effect_for(102, C::Warp, 0) == 21);
    CHECK(gb_effect_for(5, C::BattleEntry, 0) == 5);
    CHECK(gb_effect_for(24, C::DamageFloor, 0) == 24);
    CHECK(gb_effect_for(49, C::BattleAction, 0x83) == 3);
    CHECK(gb_effect_for(49, C::BattleAction, 8) == 8);
    CHECK(gb_effect_for(91, C::BattleAction, 12) == 12);
    CHECK(gb_effect_for(91, C::Death, 12) == 0);
    CHECK(gb_effect_for(50, C::BattleAction, 64) == 0);
    CHECK(gb_effect_for(146, C(0), 0, true) == 37);
    CHECK(gb_effect_for(147, C(0), 0, true) == 38);
    CHECK(gb_effect_for(148, C(0), 0, true) == 39);
    CHECK(gb_effect_for(42, C(0), 0, true) == 40);
    CHECK(gb_effect_for(91, C::Death, 12, true) == 41);
}

TEST_CASE("gb effects: one action submits once, tails stay faded, overwritten owners fall back [ROM]") {
    auto source = source_rom();
    if (source.empty())
        return;
    auto w = std::make_unique<emu::Wsc>(source, 0);
    auto &session = game::Session::of(*w);
    app::GbMusic music;
    REQUIRE(music.load(source));
    music.attach(session);
    game::Machine m(*w);
    m.ds() = 0;
    m.cs() = 0xD000;
    game::sound_init(m);
    const auto *host = music.sound_host();
    const uint32_t initial_triggers = host->triggers[0];
    CHECK(host->apu.read(0xFF24) == 0x77); // effects are audible before the first song
    auto frame = [&](float music_gain = 0, float effect_gain = 0) {
        app::paused_tick(*w);
        auto samples = w->apu.take();
        const auto ram = w->ram;
        const auto io = w->io;
        music.frame(*w, samples, music_gain, effect_gain);
        CHECK(w->ram == ram);
        CHECK(w->io == io);
    };
    {
        game::hooks::EventScope context(m, game::hooks::Event::SoundContext,
                                        {int(game::hooks::AudioContext::BattleAction), 66});
        game::play_sound_effect_core(m, 50);
        frame();
        frame();
        REQUIRE((host->effect_owners() & 1) != 0);
        const uint32_t triggered = host->triggers[0];
        CHECK(triggered == initial_triggers + 1);
        game::play_sound_effect_core(m, 24); // a later impact belongs to the same action
        frame();
        frame();
        CHECK(host->triggers[0] == triggered);
        REQUIRE(music.mapped_wsc_slots(*w) != 0);
        app::apply_channel_gains(*w, 0.4f, 0.8f, 0, music.mapped_wsc_slots(*w), 1);
        const uint8_t mapped = music.mapped_wsc_slots(*w);
        for (int slot = 0; slot < 4; ++slot)
            if (mapped & (1 << slot))
                CHECK(w->apu.output_gain[w->ram[size_t(0x62E + slot * 0x32)] & 3] == 0);
    }
    game::play_sound_effect_core(m, 50); // same track, new unscoped generation is not mapped
    frame();
    CHECK(music.mapped_wsc_slots(*w) == 0);
    {
        game::hooks::EventScope invalid_action(m, game::hooks::Event::SoundContext,
                                               {int(game::hooks::AudioContext::BattleAction), -1});
        game::play_sound_effect_core(m, 50);
        frame();
        CHECK(music.mapped_wsc_slots(*w) == 0);
    }
    app::apply_channel_gains(*w, 0.4f, 0.8f, 0, music.mapped_wsc_slots(*w), 1);
    const uint8_t sfx = app::sfx_channels(*w);
    for (int ch = 0; ch < 4; ++ch)
        if (sfx & (1 << ch))
            CHECK(w->apu.output_gain[size_t(ch)] == 0.8f);
    // A WSC tail may outlive the GB script: ownership is keyed to the accepted WSC generation.
    game::play_sound_effect_core(m, 161);
    frame();
    frame();
    const uint8_t owners = music.mapped_wsc_slots(*w);
    REQUIRE(owners != 0);
    for (int n = 0; n < 200; ++n) {
        std::vector<int16_t> samples(636);
        music.frame(*w, samples, 0, 0); // freeze WSC slots, let the GB effect finish
    }
    CHECK(host->effect_owners() == 0);
    CHECK(music.mapped_wsc_slots(*w) == owners);
    game::sound_init(m);
    CHECK(music.mapped_wsc_slots(*w) == 0);
    CHECK(music.sound_host() == host);
    CHECK(host->effect_frames() == 0);
}

TEST_CASE("gb effects: private scripts preserve original banks and transcribed note timing [ROM]") {
    auto source = source_rom();
    if (source.empty())
        return;
    const std::vector<uint8_t> original(source.begin() + 0x320000, source.begin() + 0x340000);
    auto image = original;
    REQUIRE(app::gb_append_cues(image, source));
    REQUIRE(app::gb_append_effects(image, source));
    CHECK(std::equal(original.begin(), original.end(), image.begin()));
    auto driver = emu::saga1_sound_driver();
    driver.bank = 8;
    for (int id = 37; id <= 40; ++id) {
        auto h = std::make_unique<emu::GbSoundHost>(image, driver);
        REQUIRE(h->init());
        auto voice = app::gb_read_effect(source, uint8_t(id == 40 ? 42 : 146 + id - 37));
        REQUIRE(voice.has_value());
        h->effect(uint8_t(id));
        double elapsed = 0;
        int previous = 0;
        for (const auto &note : voice->voice.notes) {
            elapsed += note.ticks * emu::GbSoundHost::kFrameRate /
                       (3072000.0 / (159 * 256) * (16 - voice->skip) / 16);
            const int end = int(std::lround(elapsed));
            for (int frame = previous; frame < end; ++frame) {
                REQUIRE(h->frame_dual(0, 70224));
                if (note.pitch < 0)
                    CHECK_FALSE(h->apu.channel_on(0));
                else {
                    const int pitch =
                        int(std::lround(69 + 12 * std::log2(131072.0 / (2048 - h->apu.frequency(0)) / 440)));
                    CHECK(pitch == note.pitch);
                }
                h->apu.take();
            }
            previous = end;
        }
        REQUIRE(h->frame_dual(0, 70224));
        CHECK(h->effect_owners() == 0);
    }
    // Extending the tables cannot change any original effect's script execution.
    for (int id = 1; id <= 36; ++id) {
        auto a = std::make_unique<emu::GbSoundHost>(original, emu::saga1_sound_driver());
        auto b = std::make_unique<emu::GbSoundHost>(image, driver);
        REQUIRE(a->init());
        REQUIRE(b->init());
        a->apu.take();
        b->apu.take();
        a->effect(uint8_t(id));
        b->effect(uint8_t(id));
        for (int frame = 0; frame < 160; ++frame) {
            REQUIRE(a->frame_dual(0, 70224));
            REQUIRE(b->frame_dual(0, 70224));
            CHECK(a->apu.take() == b->apu.take());
        }
    }
    auto invalid = original;
    CHECK_FALSE(app::gb_append_effects(invalid, {}));
    CHECK(invalid == original);
}

TEST_CASE("gb effects: runtime jingle resumes the live WSC song clock [ROM]") {
    auto source = source_rom();
    if (source.empty())
        return;
    auto a = std::make_unique<emu::Wsc>(source, 0), b = std::make_unique<emu::Wsc>(source, 0);
    app::GbMusic audible, baseline;
    REQUIRE(audible.load(source));
    REQUIRE(baseline.load(source));
    audible.attach(game::Session::of(*a));
    game::Machine am(*a), bm(*b);
    am.ds() = bm.ds() = 0;
    game::sound_init(am);
    game::sound_init(bm);
    am.ax() = bm.ax() = 144;
    game::sound_request_core(am);
    game::sound_request_core(bm);
    baseline.request(144);
    auto frame = [&] {
        for (auto pair : {std::pair{a.get(), &audible}, std::pair{b.get(), &baseline}}) {
            app::paused_tick(*pair.first);
            auto samples = pair.first->apu.take();
            pair.second->frame(*pair.first, samples, 0, 0);
        }
    };
    for (int n = 0; n < 40; ++n)
        frame();
    SUBCASE("queued jingle") {
        am.ax() = bm.ax() = 136;
        game::sound_request_core(am);
        game::sound_request_core(bm);
    }
    SUBCASE("immediate musical alias") {
        game::play_sound_effect_core(am, 118);
        game::play_sound_effect_core(bm, 118);
    }
    SUBCASE("queued after driver service, before the audio frame is mixed") {
        app::paused_tick(*a);
        app::paused_tick(*b);
        am.ax() = bm.ax() = 136;
        game::sound_request_core(am);
        game::sound_request_core(bm);
        auto as = a->apu.take(), bs = b->apu.take();
        audible.frame(*a, as, 0, 0);
        baseline.frame(*b, bs, 0, 0);
    }
    SUBCASE("queued jingle overwritten before its first tick") {
        app::paused_tick(*a);
        app::paused_tick(*b);
        am.ax() = bm.ax() = 136;
        game::sound_request_core(am);
        game::sound_request_core(bm);
        game::play_sound_effect_core(am, 0);
        game::play_sound_effect_core(bm, 0);
        auto as = a->apu.take(), bs = b->apu.take();
        audible.frame(*a, as, 0, 0);
        baseline.frame(*b, bs, 0, 0);
    }
    for (int n = 0; n < 400; ++n)
        frame();
    CHECK(audible.playing());
    CHECK(audible.wsc_music_slots(*a) == 0);
    CHECK(audible.sound_host()->driver_state() == baseline.sound_host()->driver_state());
    for (int ch = 0; ch < 3; ++ch)
        CHECK(audible.sound_host()->apu.frequency(ch) == baseline.sound_host()->apu.frequency(ch));
}

TEST_CASE("gb effects: pulse script survives jingle restoration before it ends [ROM]") {
    auto source = source_rom();
    if (source.empty())
        return;
    auto control = sound(source), active = sound(source);
    for (auto *h : {control.get(), active.get()}) {
        h->request(7);
        h->set_tick_loop(31, 1536);
        for (int tick = 0; tick < 40; ++tick)
            REQUIRE(h->frame_dual(1, cycles));
        h->effect(9);
        REQUIRE(h->frame_dual(1, cycles));
    }
    REQUIRE(active->begin_jingle(15));
    for (int frame = 0; frame < 3; ++frame) {
        REQUIRE(active->saved_music_ticks(1));
        REQUIRE(active->frame_dual(2, cycles));
        REQUIRE(control->frame_dual(1, cycles));
    }
    REQUIRE(active->end_jingle());
    REQUIRE(active->effect_owners() != 0);
    CHECK(effect_state(*active) == effect_state(*control));
    CHECK(active->apu.frequency(0) == control->apu.frequency(0));
    for (int frame = 0; frame < 160; ++frame) {
        REQUIRE(active->frame_dual(1, cycles));
        REQUIRE(control->frame_dual(1, cycles));
        active->apu.take();
        control->apu.take();
    }
    CHECK(active->driver_state() == control->driver_state());
    CHECK(active->apu.frequency(0) == control->apu.frequency(0));
}

TEST_CASE("gb effects: picture and volume changes never retrigger or rewind a script [ROM]") {
    auto source = source_rom();
    if (source.empty())
        return;
    auto fixed = sound(source), swapped = sound(source);
    fixed->effect(9);
    swapped->effect(9);
    for (int frame = 0; frame < 180; ++frame) {
        swapped->gains(frame % 2 ? 0 : 1, frame % 3 ? 0 : 1, 7);
        REQUIRE(fixed->frame_dual(frame % 3, cycles));
        REQUIRE(swapped->frame_dual(frame % 3, cycles));
        CHECK(effect_state(*fixed) == effect_state(*swapped));
        CHECK(fixed->triggers[0] == swapped->triggers[0]);
        fixed->apu.take();
        swapped->apu.take();
    }
}

TEST_CASE("gb effects: independent native clock ignores music holds and bursts [ROM]") {
    auto source = source_rom();
    if (source.empty())
        return;
    auto native = sound(source), dual = sound(source);
    native->effect(12);
    dual->effect(12);
    uint64_t previous = 0;
    for (int frame = 0; frame < 1200; ++frame) {
        const int ticks = frame % 7 == 0 ? 4 : frame % 3 == 0 ? 0 : 1;
        REQUIRE(dual->frame_dual(ticks, cycles));
        if (dual->effect_frames() != previous) {
            REQUIRE(native->frame());
            REQUIRE(effect_state(*native) == effect_state(*dual));
            previous = dual->effect_frames();
        }
        native->apu.take();
        dual->apu.take();
        CHECK(dual->effect_frames() == uint64_t((frame + 1) * cycles) / emu::GbSoundHost::kFrameCycles);
    }
    CHECK(dual->effect_owners() == 0);
}

TEST_CASE("gb effects: repeated requests survive stops and loop restoration [ROM]") {
    auto source = source_rom();
    if (source.empty())
        return;
    auto control = sound(source), active = sound(source);
    control->request(7);
    active->request(7);
    control->set_tick_loop(31, 1536);
    active->set_tick_loop(31, 1536);
    for (int tick = 1; tick <= 31 + 1536; ++tick) {
        if (tick == 31 + 1536 - 3)
            active->effect(9);
        REQUIRE(control->frame_dual(1, cycles));
        REQUIRE(active->frame_dual(1, cycles));
        if (tick == 31 + 1536) {
            REQUIRE((active->effect_owners() & 1) != 0);
            for (uint16_t a = 0xCE01; a < 0xCE60; ++a)
                CHECK(active->read(a) == control->read(a));
        }
        control->apu.take();
        active->apu.take();
    }
    for (int frame = 0; frame < 160; ++frame) {
        REQUIRE(control->frame_dual(1, cycles));
        REQUIRE(active->frame_dual(1, cycles));
        control->apu.take();
        active->apu.take();
    }
    CHECK(active->effect_owners() == 0);
    CHECK(active->driver_state() == control->driver_state());
    CHECK(active->apu.frequency(0) == control->apu.frequency(0));
    active->effect(9);
    REQUIRE(active->frame_dual(0, 70224));
    const auto first = effect_state(*active);
    REQUIRE(active->frame_dual(0, 70224));
    active->effect(9);
    REQUIRE(active->frame_dual(0, 70224));
    CHECK(effect_state(*active) == first);
    active->stop_music();
    REQUIRE(active->frame_dual(1, cycles));
    CHECK(active->read(0xFFE1) == 0);
    CHECK((active->effect_owners() & 1) != 0);
}

TEST_CASE("gb effects: jingle restores current music without reviving an expired effect [ROM]") {
    auto source = source_rom();
    if (source.empty())
        return;
    auto control = sound(source), active = sound(source);
    for (auto *h : {control.get(), active.get()}) {
        h->request(7);
        h->set_tick_loop(31, 1536);
        for (int tick = 0; tick < 40; ++tick)
            REQUIRE(h->frame_dual(1, cycles));
        h->apu.take();
    }
    active->effect(9);
    REQUIRE(active->frame_dual(1, cycles));
    REQUIRE(control->frame_dual(1, cycles));
    REQUIRE(active->begin_jingle(15));
    REQUIRE(active->read(0xFFE3) == 1);
    for (int frame = 0; frame < 140; ++frame) {
        REQUIRE(active->saved_music_ticks(1));
        REQUIRE(active->frame_dual(2, cycles));
        REQUIRE(control->frame_dual(1, cycles));
        active->apu.take();
        control->apu.take();
    }
    REQUIRE(active->effect_owners() == 0);
    REQUIRE(active->end_jingle());
    CHECK(active->read(0xFFE3) == 0);
    CHECK(active->effect_owners() == 0);
    CHECK(active->driver_state() == control->driver_state());
    REQUIRE(active->frame_dual(1, cycles));
    REQUIRE(control->frame_dual(1, cycles));
    CHECK(active->apu.frequency(0) == control->apu.frequency(0));
    CHECK(active->apu.frequency(1) == control->apu.frequency(1));
    CHECK(active->apu.frequency(2) == control->apu.frequency(2));
}

TEST_CASE("gb effects: gains follow ownership before sample integration [ROM]") {
    auto source = source_rom();
    if (source.empty())
        return;
    auto h = sound(source);
    h->gains(0, 0.75f, 1);
    h->request(1);
    REQUIRE(h->frame_dual(2, cycles));
    CHECK(h->apu.output_gain[0] == 0);
    h->effect(9);
    REQUIRE(h->frame_dual(1, 70224));
    CHECK(h->apu.output_gain[0] == 0.75f);
    CHECK(h->apu.output_gain[1] == 0);
    h->gains(0.5f, 0);
    CHECK(h->apu.output_gain[0] == 0);
    CHECK(h->apu.output_gain[1] == 0.5f);
    for (int frame = 0; frame < 160; ++frame)
        REQUIRE(h->frame_dual(1, cycles));
    CHECK(h->apu.output_gain[0] == 0.5f);
}

TEST_CASE("gb effects: per-channel gain scales the integrated waveform") {
    emu::GbApu full, quiet, silent;
    for (auto *apu : {&full, &quiet, &silent}) {
        apu->write(0xFF24, 0x77);
        apu->write(0xFF25, 0x11);
        apu->write(0xFF11, 0x80);
        apu->write(0xFF12, 0xF0);
        apu->write(0xFF13, 0xD6);
        apu->write(0xFF14, 0x86);
    }
    quiet.output_gain[0] = 0.25f;
    silent.output_gain[0] = 0;
    full.run(70224);
    quiet.run(70224);
    silent.run(70224);
    const auto a = full.take(), b = quiet.take(), c = silent.take();
    REQUIRE(a.size() == b.size());
    REQUIRE(a.size() == c.size());
    for (size_t i = 0; i < a.size(); ++i) {
        CHECK(std::abs(a[i] * 0.25 - b[i]) <= 1);
        CHECK(c[i] == 0);
    }
}

TEST_CASE("gb effects: unmapped music, stop and restart keep the persistent effect host [ROM]") {
    auto source = source_rom();
    if (source.empty())
        return;
    auto w = std::make_unique<emu::Wsc>(source, 0);
    app::GbMusic music;
    REQUIRE(music.load(source));
    music.attach(game::Session::of(*w));
    const auto *host = music.sound_host();
    game::Machine m(*w);
    m.ds() = 0;
    game::sound_init(m);
    auto frame = [&] {
        app::paused_tick(*w);
        auto samples = w->apu.take();
        music.frame(*w, samples, 1, 1);
    };
    m.ax() = 144;
    game::sound_request_core(m);
    frame();
    frame();
    REQUIRE(music.playing());
    {
        game::hooks::EventScope context(m, game::hooks::Event::SoundContext,
                                        {int(game::hooks::AudioContext::BattleAction), 66});
        game::play_sound_effect_core(m, 50);
        frame();
        frame();
    }
    REQUIRE((host->effect_owners() & 1) != 0);
    m.ax() = 155;
    game::sound_request_core(m); // WSC-only ambient music slots
    frame();
    frame();
    CHECK_FALSE(music.playing());
    CHECK(music.sound_host() == host);
    CHECK((host->effect_owners() & 1) != 0);
    game::play_sound_effect_core(m, 0); // the actual WSC effect-stop record
    frame();
    CHECK(host->effect_owners() == 0);
    CHECK(music.mapped_wsc_slots(*w) == 0);
    const auto clock = host->effect_frames();
    m.ax() = 144;
    game::sound_request_core(m);
    frame();
    frame();
    CHECK(music.playing());
    CHECK(music.sound_host() == host);
    CHECK(host->effect_frames() > clock);
}

TEST_CASE("gb effects: notifications preserve repeated identity and post-reservation tracks [ROM]") {
    auto source = source_rom();
    if (source.empty())
        return;
    auto w = std::make_unique<emu::Wsc>(source, 0);
    auto &session = game::Session::of(*w);
    game::Machine m(*w);
    m.ds() = 0;
    int resets = 0;
    std::vector<int> ids, masks;
    auto reset = session.hooks().subscribe(game::hooks::Event::SoundReset,
                                           [&](game::Machine &, const game::hooks::Args &) { ++resets; });
    auto sub = session.hooks().subscribe(
        game::hooks::Event::SoundRequest, [&](game::Machine &caller, const game::hooks::Args &a) {
            ids.push_back(a.a);
            masks.push_back(a.c);
            CHECK(a.b == (a.a == 138 ? 0 : 1));
            for (int slot = 0; slot < 8; ++slot)
                if (a.c & (1 << slot)) {
                    CHECK(caller.rd16(0, uint16_t(0x62C + slot * 0x32)) == 0);
                    CHECK(caller.rd16(0, uint16_t(0x62F + slot * 0x32)) != 0);
                }
        });
    game::sound_init(m);
    m.ax() = 138;
    game::sound_request_core(m);
    game::play_sound_effect_core(m, 146);
    game::play_sound_effect_core(m, 146);
    game::play_sound_effect_core(m, 42);
    CHECK(resets == 1);
    CHECK(ids == std::vector<int>{138, 146, 146, 42});
    CHECK((masks[0] & 0xF0) != 0);
    CHECK((masks[1] & 0x0F) != 0);
}
