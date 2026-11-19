#include "app/gb_music.h"
#include "app/gb_cues.h"
#include "app/sound_mix.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "emu/gb_song.h"
#include "emu/wsc.h"
#include "game/session.h"

namespace sawgod::app {

namespace {
constexpr double kWscFrameCycles = double(emu::GbApu::kClock) * (159 * 256) / 3072000.0; // GB cycles
constexpr int kWscSampleRate = 24000; // emu::Apu::kSampleRate
// The GB output is delayed to sound where the WSC's notes do. The WSC driver's frame service runs at VBlank
// (line 144) and a frame runs from line 145 to the next 145 (emu/wsc.cpp): 158 of 159 lines into the frame's
// samples. And a new note is heard a frame after its tick: sound_tick writes the volume port
// (sound_update_channel_volume) before it reads the next notes (sound_decode_track_commands). The GB's notes
// sound at once, as its frame starts. Measured on the field song: GB onsets 11-12 ms early with only the
// VBlank part, about 0 with both. (In the pause the driver runs at the start of the frame: 13 ms late there.)
constexpr size_t kVblankDelay = 2 * (318 + 318 * 158 / 159);

// Derived from the player's ROMs by sawgod-audio-spike align (tools/audio_maps.py).
const GbSongMap kMaps[] = {
#include "app/gb_song_maps.inc"
};
} // namespace

namespace {
double mapped_tick(double w, const std::vector<std::pair<double, double>> &anchors, double slope) {
    if (anchors.empty())
        return w * slope;
    if (w <= anchors.front().first)
        return anchors.front().second;
    if (w >= anchors.back().first)
        return anchors.back().second + (w - anchors.back().first) * slope;
    for (size_t i = 1; i < anchors.size(); ++i)
        if (w <= anchors[i].first) {
            const auto &[w0, g0] = anchors[i - 1];
            const auto &[w1, g1] = anchors[i];
            return g0 + (w - w0) * (g1 - g0) / (w1 - w0);
        }
    return 0;
}
} // namespace

int GbSongMap::gb_loop_start() const {
    return int(std::floor(mapped_tick(loop_start, anchors, slope) + 1e-9));
}

int GbSongMap::gb_ticks_by(int wsc_ticks) const {
    double g;
    if (loop_ticks > 0 && wsc_ticks >= loop_start) {
        const int elapsed = wsc_ticks - loop_start;
        g = gb_loop_start() + double(elapsed / loop_ticks) * gb_loop_ticks +
            mapped_tick(elapsed % loop_ticks, loop_anchors, slope) + loop_phase;
    } else {
        g = mapped_tick(wsc_ticks, anchors, slope);
    }
    return int(std::floor(g + 1e-9));
}

std::span<const GbSongMap> gb_song_maps() { return kMaps; }

const GbSongMap *gb_song_map(uint8_t wsc_song) {
    for (const GbSongMap &m : kMaps)
        if (m.wsc_song == wsc_song)
            return &m;
    return nullptr;
}

void GbMusicFollower::request(uint8_t wsc_song) {
    if (running_)
        stop_pending_ = true; // any request ends the song playing (the same one restarts from its start)
    running_ = false;
    map_ = gb_song_map(wsc_song);
    pending_ = map_ != nullptr;
}

GbMusicFollower::Step GbMusicFollower::frame(uint16_t slot_step, uint8_t slot_phase, int previous_phase) {
    Step r;
    if (stop_pending_) {
        r.stop = true;
        stop_pending_ = false;
    }
    const bool active = slot_step != 0xFFFF;
    if (pending_) {
        if (!active) {
            pending_ = false;
            running_ = false;
            r.stop = true; // all accepted tracks ended or were overwritten before their first tick
            return r;
        }
        // after the request the slot's step counter is 0 until the driver's next frame starts the song
        if (active && slot_step != 0) {
            pending_ = false;
            running_ = true;
            wsc_ticks_ = 1;
            phase_ = slot_phase;
            gb_ticks_ = map_->gb_ticks_by(1);
            r.start = true;
            r.ticks = gb_ticks_;
        }
        return r;
    }
    if (!running_)
        return r;
    if (!active) { // the driver silenced the song (sound_init, a reset)
        running_ = false;
        r.stop = true;
        return r;
    }
    // the slot ticks unless its tempo phase wrapped (sound_tick: of every 16 frames, rate skip their tick)
    if (slot_phase >= (previous_phase >= 0 ? previous_phase : phase_))
        ++wsc_ticks_;
    phase_ = slot_phase;
    const int target = map_->gb_ticks_by(wsc_ticks_);
    r.ticks = std::max(0, target - gb_ticks_);
    gb_ticks_ = std::max(gb_ticks_, target);
    return r;
}

namespace {
uint8_t cue_music_slots(const emu::Wsc &wsc, const GbSongMap *map);
}

uint8_t gb_music_voice_mutes(const emu::Wsc &wsc, const GbSongMap &map, uint8_t mapped_effect_slots) {
    uint8_t mask = 0;
    const bool cue = map.clock_slot < 4 && wsc.rom.size() >= 0x3E0000;
    const uint8_t cue_slots = cue_music_slots(wsc, &map);
    for (int ch = 0; ch < 3; ++ch) {
        const int song_base = 0x62C + map.voices[size_t(ch)] * 0x32;
        const int hw = wsc.ram[size_t(song_base + 2)] & 3;
        bool stolen = false;
        for (int slot = 0; slot < 4; ++slot) {
            if (mapped_effect_slots & (1 << slot))
                continue;
            if (slot == map.voices[size_t(ch)])
                continue; // jingles themselves live in the effect slots
            const int b = 0x62C + slot * 0x32;
            stolen |= (wsc.ram[size_t(b)] | wsc.ram[size_t(b + 1)] << 8) != 0xFFFF &&
                      (wsc.ram[size_t(b + 2)] & 3) == hw && slot < map.voices[size_t(ch)];
        }
        if (stolen || (cue && !(cue_slots & (1 << map.voices[size_t(ch)]))))
            mask |= uint8_t(1 << ch);
    }
    return mask;
}

GbMusic::GbMusic() = default;
GbMusic::~GbMusic() = default;

uint8_t gb_effect_for(uint8_t id, game::hooks::AudioContext context, uint8_t action, bool private_effects) {
    using C = game::hooks::AudioContext;
    if (private_effects && id >= 146 && id <= 148)
        return uint8_t(37 + id - 146);
    if (private_effects && id == 42)
        return 40;
    if (id == 161 || id == 23)
        return 23;
    if (context == C::Death)
        return private_effects ? 41 : 0;
    if (context == C::BattleAction)
        return (action & 0x7F) >= 1 && (action & 0x7F) <= 36 ? action & 0x7F : 0;
    if (context == C::FieldItem && (id == 17 || id == 93))
        return 17;
    if (context == C::Warp && (id == 21 || id == 102))
        return 21;
    if (context == C::BattleEntry && id == 5)
        return 5;
    if (context == C::DamageFloor && id == 24)
        return 24; // provisional hit cue, as authorized
    return 0;
}

bool GbMusic::load(std::vector<uint8_t> rom) {
    // Makai Toushi SaGa on the GB: 128 KB, the sound driver's jump table at 7:4000. The WSC cartridge carries
    // it as ORIJINARU at 0x320000 (GB Rev 1, its header blanked; the driver and songs byte for byte)
    static const uint8_t kDriver[] = {0xC3, 0x06, 0x40, 0xC3, 0x30, 0x40};
    constexpr size_t kGbSize = 0x20000, kOrijinaru = 0x320000;
    // Retain the WSC data while extracting its embedded GB cartridge image.
    const auto source = rom;
    bool cues = false;
    if (rom.size() >= kOrijinaru + kGbSize &&
        std::memcmp(rom.data() + kOrijinaru + 7 * 0x4000, kDriver, sizeof kDriver) == 0) {
        rom = std::vector<uint8_t>(rom.begin() + kOrijinaru, rom.begin() + kOrijinaru + kGbSize);
        cues = gb_append_cues(rom, source);
    } else if (rom.size() != kGbSize || std::memcmp(rom.data() + 0x134, "SAGA", 4) != 0 ||
               std::memcmp(rom.data() + 7 * 0x4000, kDriver, sizeof kDriver) != 0)
        return false;
    cue_bank_ = cues;
    private_effects_ = gb_append_effects(rom, source);
    for (size_t item = 0; item < action_sounds_.size(); ++item)
        action_sounds_[item] = rom[6 * 0x4000 + 0x3707 + item * 8];
    rom_ = std::move(rom);
    auto driver = emu::saga1_sound_driver();
    if (rom_.size() > 0x20000)
        driver.bank = 8;
    host_ = std::make_unique<emu::GbSoundHost>(rom_, driver);
    host_->apu.set_sample_rate(kWscSampleRate);
    if (!host_->init()) {
        host_.reset();
        rom_.clear();
        return false;
    }
    // ResetAudio leaves NR50 at the power-on level. Install silent song 0 once
    // so SFX before any music have the same mixer/wave setup as an in-game effect.
    host_->stop_music();
    host_->frame_dual(1, 1);
    host_->apu.take();
    fifo_.assign(kVblankDelay, 0);
    effect_fifo_.assign(kVblankDelay, 0);
    return true;
}

void GbMusic::attach(game::Session &session) {
    closing_fade_sub_ = session.hooks().subscribe(game::hooks::Event::IntroClosingMusicFade,
                                                  [this](game::Machine &, const game::hooks::Args &a) {
                                                      if (a.a)
                                                          slow_fade_.start();
                                                      else
                                                          slow_fade_.reset();
                                                  });
    sub_ =
        session.hooks().subscribe(game::hooks::Event::SoundRequest,
                                  [this](game::Machine &m, const game::hooks::Args &a) { accepted(m, a); });
    reset_sub_ = session.hooks().subscribe(game::hooks::Event::SoundReset,
                                           [this](game::Machine &, const game::hooks::Args &) { reset(); });
    context_sub_ = session.hooks().subscribe(
        game::hooks::Event::SoundContext, [this](game::Machine &, const game::hooks::Args &a) {
            if (a.a)
                contexts_.push_back(
                    {game::hooks::AudioContext(a.a), uint8_t(a.b), ++generation_, false, a.b >= 0});
            else if (!contexts_.empty())
                contexts_.pop_back();
        });
}

void GbMusic::accepted(game::Machine &m, const game::hooks::Args &a) {
    Request r{uint8_t(a.a), uint8_t(a.c), a.b != 0, ++generation_, {}};
    for (size_t slot = 0; slot < r.tracks.size(); ++slot)
        r.tracks[slot] = m.rd16(0, uint16_t(0x62F + slot * 0x32));
    const uint8_t id = r.immediate ? r.id : r.id >= 18 ? uint8_t(r.id - 18) : r.id;
    const auto kind = contexts_.empty() ? game::hooks::AudioContext(0) : contexts_.back().kind;
    const uint8_t action =
        contexts_.empty() || !contexts_.back().valid_item ? 0 : action_sounds_[contexts_.back().item];
    const bool musical_alias = r.immediate && ((r.id >= 117 && r.id <= 140) || r.id == 145 || r.id == 160);
    const uint8_t music_id = musical_alias ? uint8_t(r.id + 18) : r.id;
    if (musical_alias || (!r.immediate && gb_song_map(music_id)) || (r.slots & 0xF0))
        r.music = music_id;
    r.cancel = id == 0 && r.music < 0;
    if (r.music < 0 && !r.cancel) {
        r.effect = gb_effect_for(id, kind, action, private_effects_);
        if (r.effect) {
            const bool scoped = kind != game::hooks::AudioContext(0) && id != 161 && id != 23 && id != 42 &&
                                !(id >= 146 && id <= 148);
            r.group = scoped ? contexts_.back().group : r.generation;
            r.submit = !scoped || !contexts_.back().submitted;
            if (scoped)
                contexts_.back().submitted = true;
        }
    }
    if ((r.slots & 0xF0) || (r.cancel && !r.immediate))
        slow_fade_.reset();
    requests_.push_back(r);
}

void GbMusic::reset() {
    slow_fade_.reset();
    requests_.clear();
    follower_ = {};
    suspended_ = {};
    jingle_ = false;
    contexts_.clear();
    owners_ = {};
    mapped_group_ = 0;
    phases_ = {};
    ++generation_;
    if (host_) {
        host_->init();
        host_->stop_music();
        host_->frame_dual(1, 1);
        host_->apu.take();
        host_->apu.take_effects();
    }
    fifo_.assign(kVblankDelay, 0);
    effect_fifo_.assign(kVblankDelay, 0);
}

namespace {
uint8_t cue_music_slots(const emu::Wsc &wsc, const GbSongMap *map) {
    if (!map || map->clock_slot >= 4 || wsc.rom.size() < 0x3E0000)
        return 0;
    auto word = [&](size_t a) { return int(wsc.rom[a] | wsc.rom[a + 1] << 8); };
    const int record = word(0x3D02E0 + size_t(map->wsc_song) * 3);
    if (record > 512)
        return 0;
    uint8_t slots = 0;
    for (int slot : map->voices) {
        const size_t b = 0x62C + size_t(slot) * 0x32;
        const int track = wsc.ram[b + 3] | wsc.ram[b + 4] << 8;
        const int count = wsc.rom[0x3D02E2 + size_t(map->wsc_song) * 3];
        for (int i = 0; i < std::min(count, 4); ++i)
            if (word(0x3D0500 + size_t(record + i) * 6) / 0x32 == slot &&
                track == word(0x3D0504 + size_t(record + i) * 6))
                slots |= uint8_t(1 << slot);
    }
    return slots;
}
} // namespace

uint8_t GbMusic::wsc_music_slots(const emu::Wsc &wsc) const {
    return playing() ? cue_music_slots(wsc, follower_.playing()) : 0;
}

uint8_t GbMusic::mapped_wsc_slots(const emu::Wsc &wsc) const {
    uint8_t slots = 0;
    for (size_t slot = 0; slot < owners_.size(); ++slot) {
        const size_t b = 0x62C + slot * 0x32;
        if (mapped_group_ != 0 && owners_[slot].group == mapped_group_ &&
            (wsc.ram[b] | wsc.ram[b + 1] << 8) != 0xFFFF &&
            (wsc.ram[b + 3] | wsc.ram[b + 4] << 8) == owners_[slot].track)
            slots |= uint8_t(1 << slot);
    }
    return slots;
}

void GbMusic::frame(const emu::Wsc &wsc, std::vector<int16_t> &samples, float gain) {
    frame(wsc, samples, gain, gain);
}

void GbMusic::frame(const emu::Wsc &wsc, std::vector<int16_t> &samples, float gain, float effect_gain,
                    std::vector<int16_t> *effects) {
    if (!host_)
        return;
    for (const auto &r : requests_) {
        for (size_t slot = 0; slot < owners_.size(); ++slot)
            if (r.slots & (1 << slot))
                owners_[slot] = {r.tracks[slot], r.group};
        if (r.submit && r.effect) {
            mapped_group_ = r.group;
            host_->effect(r.effect);
        }
        if (r.cancel)
            host_->cancel_effects();
        // Queued ordinary effects and ambient requests must not restart the song follower.
        const auto *next = r.music >= 0 ? gb_song_map(uint8_t(r.music)) : nullptr;
        if (next && next->clock_slot < 4) {
            if (!jingle_)
                suspended_ = follower_;
            jingle_ = true;
            request(uint8_t(r.music));
        } else if (r.music >= 0) {
            if (jingle_) {
                host_->end_jingle();
                jingle_ = false;
            }
            suspended_ = {};
            request(uint8_t(r.music));
        }
    }
    requests_.clear();
    int background_ticks = 0;
    if (jingle_ && suspended_.playing()) {
        const int slot = suspended_.clock_slot(), b = 0x62C + slot * 0x32;
        const auto background = suspended_.frame(uint16_t(wsc.ram[size_t(b)] | wsc.ram[size_t(b + 1)] << 8),
                                                 wsc.ram[size_t(b + 7)], phases_[size_t(slot)]);
        background_ticks = background.ticks;
    }
    const GbSongMap *map = follower_.selected();
    // Pending requests choose their clock before the first frame too
    int clock_slot = follower_.clock_slot();
    const bool cue = map && map->clock_slot < 4;
    const uint8_t cue_slots = cue_music_slots(wsc, map);
    bool clock_active = !cue;
    const int end_slot = clock_slot < 4 ? 4 : 8;
    for (int slot = clock_slot; slot < end_slot; ++slot) {
        const int b = 0x62C + slot * 0x32;
        if (cue && !(cue_slots & (1 << slot)))
            continue;
        if ((wsc.ram[size_t(b)] | wsc.ram[size_t(b + 1)] << 8) != 0xFFFF) {
            clock_slot = slot;
            clock_active = true;
            break;
        }
    }
    const uint16_t base = uint16_t(0x62C + clock_slot * 0x32);
    const uint16_t step = clock_active ? uint16_t(wsc.ram[base] | wsc.ram[base + 1] << 8) : 0xFFFF;
    const GbMusicFollower::Step s = follower_.frame(step, wsc.ram[base + 7], phases_[size_t(clock_slot)]);
    for (size_t slot = 0; slot < phases_.size(); ++slot)
        phases_[slot] = wsc.ram[0x62C + slot * 0x32 + 7];
    if (jingle_ && !s.start)
        host_->saved_music_ticks(background_ticks);
    const bool waiting_jingle = jingle_ && follower_.pending();
    const bool ended_before_start =
        jingle_ && s.stop && !s.start && !waiting_jingle && host_->read(0xFFE3) == 0;
    if (s.stop && !s.start && !waiting_jingle) {
        if (jingle_) {
            host_->end_jingle();
            follower_ = suspended_;
            suspended_ = {};
            jingle_ = false;
            if (!follower_.playing())
                host_->stop_music();
        } else {
            host_->stop_music();
        }
    }
    if (s.start) {
        map = follower_.playing();
        if (jingle_)
            host_->begin_jingle(map->gb_song);
        else {
            host_->stop_music();
            host_->request(map->gb_song);
        }
        host_->set_tick_loop(map->gb_loop_start(), map->gb_loop_ticks);
        if (jingle_)
            host_->saved_music_ticks(background_ticks);
    }
    map = follower_.playing();
    if (!map && waiting_jingle)
        map = suspended_.playing();
    const uint8_t mutes = map ? gb_music_voice_mutes(wsc, *map, mapped_wsc_slots(wsc)) : 0;
    host_->apu.split_effects = effects != nullptr;
    std::array<float, 3> levels{1, 1, 1};
    const auto fade = driver_music_fade(wsc);
    const auto slow = slow_fade_.frame(wsc.ram[0x624]);
    if (map)
        for (size_t voice = 0; voice < levels.size(); ++voice) {
            levels[voice] = fade.engaged ? fade.slots[map->voices[voice]]
                                         : driver_terminal_music_gain(wsc, map->voices[voice]);
            if (slow.slow && map->voices[voice] >= 4)
                levels[voice] = 1;
        }
    host_->gains(gain, effect_gain, mutes, levels);
    host_->apu.music_fade(slow.slow && map && map->clock_slot >= 4 ? slow.first : 1,
                          slow.slow && map && map->clock_slot >= 4 ? slow.last : 1, kWscFrameCycles);
    // If stopped with no replacement, a single tick installs the silent song.
    host_->frame_dual(waiting_jingle || ended_before_start ? background_ticks
                      : s.stop && !s.start && !map         ? 1
                                                           : s.ticks,
                      kWscFrameCycles);
    const auto effect_out = host_->apu.take_effects();
    if (effects) {
        effect_fifo_.insert(effect_fifo_.end(), effect_out.begin(), effect_out.end());
        const size_t n = std::min(effects->size(), effect_fifo_.size());
        for (size_t i = 0; i < n; ++i)
            (*effects)[i] = int16_t(
                std::clamp(float((*effects)[i]) + float(effect_fifo_[i]) * kLevel, -32768.0f, 32767.0f));
        effect_fifo_.erase(effect_fifo_.begin(), effect_fifo_.begin() + std::ptrdiff_t(n));
        if (effect_fifo_.size() > kVblankDelay + 64)
            effect_fifo_.erase(effect_fifo_.begin(), effect_fifo_.end() - std::ptrdiff_t(kVblankDelay + 2));
    }
    const std::vector<int16_t> out = host_->apu.take();
    fifo_.insert(fifo_.end(), out.begin(), out.end());
    const size_t n = std::min(samples.size(), fifo_.size());
    for (size_t i = 0; i < n; ++i)
        samples[i] = int16_t(std::clamp(float(samples[i]) + float(fifo_[i]) * kLevel, -32768.0f, 32767.0f));
    fifo_.erase(fifo_.begin(), fifo_.begin() + std::ptrdiff_t(n));
    if (fifo_.size() > kVblankDelay + 64) // never more than a few samples beyond the delay
        fifo_.erase(fifo_.begin(), fifo_.end() - std::ptrdiff_t(kVblankDelay + 2));
}

} // namespace sawgod::app
