#include "app/enhanced_music.h"
#ifdef __ANDROID__
#include "engine/data_dir.h"
#endif
#include "app/audio_latency.h"
#include "app/ff_audio.h"
#include "app/sound_mix.h"
#include "emu/wsc.h"
#include "game/machine.h"
#include "game/session.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace sawgod::app {
int enhanced_pan(int wsc_pan, double seat) {
    return std::clamp(int(std::nearbyint(wsc_pan + 63 * seat / 100)), 0, 127);
}

namespace {
#include "enhanced_music_maps.inc"
int word(const emu::Wsc &w, int a) { return w.ram[size_t(a)] | w.ram[size_t(a + 1)] << 8; }
double note(int period) { return 69 + 12 * std::log2(96000.0 / (2048 - period) / 440.0); }
int midi(double n) { return std::clamp(int(std::nearbyint(n)), 0, 127); }
// FluidSynth's public 2.x C ABI, dynamically loaded: no headers or reference paths needed to build.
struct fluid_settings_t;
struct fluid_synth_t;
struct fluid_sfont_t;
struct Fluid {
    bool usable = false;
#ifdef _WIN32
    HMODULE library = nullptr;
#else
    void *library = nullptr;
#endif
    fluid_settings_t *(*new_fluid_settings)() = nullptr;
    void (*delete_fluid_settings)(fluid_settings_t *) = nullptr;
    int (*fluid_settings_setnum)(fluid_settings_t *, const char *, double) = nullptr;
    int (*fluid_settings_setint)(fluid_settings_t *, const char *, int) = nullptr;
    fluid_synth_t *(*new_fluid_synth)(fluid_settings_t *) = nullptr;
    void (*delete_fluid_synth)(fluid_synth_t *) = nullptr;
    int (*fluid_synth_sfload)(fluid_synth_t *, const char *, int) = nullptr;
    fluid_sfont_t *(*fluid_synth_get_sfont)(fluid_synth_t *, unsigned int) = nullptr;
    int (*fluid_synth_add_sfont)(fluid_synth_t *, fluid_sfont_t *) = nullptr;
    int (*fluid_synth_remove_sfont)(fluid_synth_t *, fluid_sfont_t *) = nullptr;
    int (*fluid_synth_noteon)(fluid_synth_t *, int, int, int) = nullptr;
    int (*fluid_synth_noteoff)(fluid_synth_t *, int, int) = nullptr;
    int (*fluid_synth_cc)(fluid_synth_t *, int, int, int) = nullptr;
    int (*fluid_synth_program_change)(fluid_synth_t *, int, int) = nullptr;
    int (*fluid_synth_unset_program)(fluid_synth_t *, int) = nullptr;
    int (*fluid_synth_pitch_bend)(fluid_synth_t *, int, int) = nullptr;
    int (*fluid_synth_pitch_wheel_sens)(fluid_synth_t *, int, int) = nullptr;
    int (*fluid_synth_all_sounds_off)(fluid_synth_t *, int) = nullptr;
    int (*fluid_synth_write_float)(fluid_synth_t *, int, void *, int, int, void *, int, int) = nullptr;
    bool load() {
#ifdef _WIN32
        for (const char *name : {"libfluidsynth-3.dll", "libfluidsynth.dll", "fluidsynth.dll"})
            if ((library = LoadLibraryA(name)))
                break;
#elif defined(__ANDROID__)
        library = dlopen("libfluidsynth.so", RTLD_NOW | RTLD_LOCAL);
#elif defined(__APPLE__)
        // Resolve the bundled library independently of Finder's working directory.
        library = dlopen("@executable_path/../Frameworks/libfluidsynth.3.dylib", RTLD_NOW | RTLD_LOCAL);
        if (!library)
            library = dlopen("libfluidsynth.3.dylib", RTLD_NOW | RTLD_LOCAL);
#else
        library = dlopen("libfluidsynth.so.3", RTLD_NOW | RTLD_LOCAL);
#endif
        if (!library)
            return false;
#define LOAD(name)                                                                                           \
    name = reinterpret_cast<decltype(name)>(symbol(#name));                                                  \
    if (!name)                                                                                               \
    return false
        LOAD(new_fluid_settings);
        LOAD(delete_fluid_settings);
        LOAD(fluid_settings_setnum);
        LOAD(fluid_settings_setint);
        LOAD(new_fluid_synth);
        LOAD(delete_fluid_synth);
        LOAD(fluid_synth_sfload);
        LOAD(fluid_synth_get_sfont);
        LOAD(fluid_synth_add_sfont);
        LOAD(fluid_synth_remove_sfont);
        LOAD(fluid_synth_noteon);
        LOAD(fluid_synth_noteoff);
        LOAD(fluid_synth_cc);
        LOAD(fluid_synth_program_change);
        LOAD(fluid_synth_unset_program);
        LOAD(fluid_synth_pitch_bend);
        LOAD(fluid_synth_pitch_wheel_sens);
        LOAD(fluid_synth_all_sounds_off);
        LOAD(fluid_synth_write_float);
#undef LOAD
        usable = true;
        return true;
    }
    void *symbol(const char *name) {
#ifdef _WIN32
        return reinterpret_cast<void *>(GetProcAddress(library, name));
#else
        return dlsym(library, name);
#endif
    }
    ~Fluid() {
        // Never unload: FluidSynth's GLib/libinstpatch threads and registrations outlive delete_fluid_synth,
        // and unmapping the library during exit crashed ~1 run in 4 (a cancelled helper thread unwinding into
        // unmapped code). The OS reclaims it at process exit.
    }
};
// libinstpatch registers process-global GLib types: keep the library loaded until process exit.
Fluid &fluid_api() {
    static Fluid api;
    static const bool loaded = api.load();
    (void)loaded;
    return api;
}
// Single producer/single consumer ring. Slot contents are published/reclaimed with release/acquire.
template <class T, size_t N> class Queue {
    std::array<std::optional<T>, N> slots_;
    std::atomic<size_t> read_{0}, write_{0};

  public:
    bool push(T &value) {
        const size_t w = write_.load(std::memory_order_relaxed), next = (w + 1) % N;
        if (next == read_.load(std::memory_order_acquire))
            return false;
        slots_[w].emplace(std::move(value));
        write_.store(next, std::memory_order_release);
        return true;
    }
    bool pop(T &value) {
        const size_t r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire))
            return false;
        value = std::move(*slots_[r]);
        slots_[r].reset();
        read_.store((r + 1) % N, std::memory_order_release);
        return true;
    }
    bool empty() const { return read_.load() == write_.load(); }
};
} // namespace
const EnhancedSong *enhanced_song(int id) {
    for (const auto &song : songs)
        if (song.id == id)
            return &song;
    return nullptr;
}
std::string default_soundfont() {
    // SDL's base path is independent of the working directory, including inside an AppImage.
#ifdef __ANDROID__
    const std::string android_base = sawgod::data_dir();
    const char *base = android_base.c_str();
#else
    const char *base = SDL_GetBasePath();
#endif
    if (base) {
        const std::string bundled = std::string(base) + "soundfonts/GeneralUser-GS.sf2";
        std::error_code ec;
        if (std::filesystem::is_regular_file(bundled, ec))
            return bundled;
    }
    return SAWGOD_DEV_SOUNDFONT;
}

void EnhancedObserver::attach(game::Session &session) {
    closing_fade_sub_ = session.hooks().subscribe(game::hooks::Event::IntroClosingMusicFade,
                                                  [this](game::Machine &, const game::hooks::Args &a) {
                                                      if (a.a)
                                                          slow_fade_.start();
                                                      else
                                                          slow_fade_.reset();
                                                  });
    request_sub_ = session.hooks().subscribe(
        game::hooks::Event::SoundRequest, [this](game::Machine &m, const game::hooks::Args &a) {
            const int id = a.b ? a.a + 18 : a.a;
            // Every accepted replacement revokes ownership, even
            // when it happens to reuse the same track address.
            cue_slots_ &= uint8_t(~a.c);
            if (a.c & 0xF0)
                request(id);
            else if (a.a == 0)
                end_cue(); // effect 0 stops slots 0-3 only: a song on 4-7 plays on
            else if (std::find(music_cues.begin(), music_cues.end(), id) != music_cues.end()) {
                cue_ = id;
                ++cue_generation_;
                cue_slots_ = uint8_t(a.c);
                for (int slot = 0; slot < 4; ++slot)
                    cue_tracks_[size_t(slot)] = m.rd16(0, uint16_t(0x62F + slot * 0x32));
                restart_slots(cue_slots_);
            }
        });
    reset_sub_ = session.hooks().subscribe(game::hooks::Event::SoundReset,
                                           [this](game::Machine &, const game::hooks::Args &) { reset(); });
}
void EnhancedObserver::request(int song) {
    slow_fade_.reset();
    song_ = song;
    ++generation_;
    restart_slots(0xF0);
}
void EnhancedObserver::restart_slots(uint8_t slots) {
    for (int slot = 0; slot < 8; ++slot)
        if (slots & (1 << slot)) {
            steps_[size_t(slot)] = 0xFFFF;
            tracks_[size_t(slot)] = 0;
            ticks_[size_t(slot)] = 0;
            phases_[size_t(slot)] = 0;
        }
}
void EnhancedObserver::reset() {
    request(0);
    end_cue();
}
void EnhancedObserver::end_cue() {
    cue_ = 0;
    cue_slots_ = 0;
    ++cue_generation_;
    restart_slots(0x0F);
}
uint8_t EnhancedObserver::owned_cue_slots(const emu::Wsc &w, bool active_only) const {
    uint8_t slots = 0;
    for (int slot = 0; slot < 4; ++slot) {
        const int b = 0x62C + slot * 0x32;
        const bool ended = word(w, b) == 0xFFFF;
        // sound_channel_end clears the whole record, including its track pointer.
        if ((cue_slots_ & (1 << slot)) &&
            (word(w, b + 3) == cue_tracks_[size_t(slot)] || (ended && word(w, b + 3) == 0)) &&
            (!active_only || word(w, b) != 0xFFFF))
            slots |= uint8_t(1 << slot);
    }
    return slots;
}
uint8_t EnhancedObserver::wsc_music_slots(const emu::Wsc &w) const { return owned_cue_slots(w, true); }
bool EnhancedObserver::cue_playing(const emu::Wsc &w) const {
    // Keep rendering the locked release/reverb after the driver finishes its finite tracks.
    return enhanced_song(cue_) && owned_cue_slots(w, false);
}
bool EnhancedObserver::playing(const emu::Wsc &w) const {
    (void)w;
    return enhanced_song(song_) &&
           !enhanced_song(song_)->backing_wsc; // Finite songs retain their natural synth tail until replaced.
}
EnhancedFrame EnhancedObserver::frame(const emu::Wsc &w) {
    EnhancedFrame out;
    const auto fade = driver_music_fade(w);
    const auto slow = slow_fade_.frame(w.ram[0x624]);
    out.slow_fade = slow.slow;
    out.music_fade_first = slow.first;
    out.music_fade = slow.slow ? slow.last : fade.gain;
    out.generation = generation_;
    out.sequence = sequence_++;
    out.song = song_;
    out.cue = cue_;
    out.cue_generation = cue_generation_;
    out.cue_slots = owned_cue_slots(w, false);
    out.sfx = sfx_channels(const_cast<emu::Wsc &>(w));
    // Machine's read operations do not mutate the emulator.
    game::Machine m(const_cast<emu::Wsc &>(w));
    const auto previous_steps = steps_;
    for (int slot = 0; slot < 8; ++slot) {
        const int b = 0x62C + slot * 0x32;
        auto &c = out.channels[size_t(slot)];
        c.step = uint16_t(word(w, b));
        c.track = uint16_t(word(w, b + 3));
        c.hw = w.ram[size_t(b + 2)] & 3;
        c.instrument = w.ram[size_t(b + 0x27)];
        c.noise = w.ram[size_t(b + 0xB)];
        c.noise_param = w.ram[size_t(b + 0xC)];
        c.pitch = uint16_t(word(w, b + 0x10));
        const uint8_t phase = w.ram[size_t(b + 7)];
        const bool was_active = previous_steps[size_t(slot)] != 0xFFFF && previous_steps[size_t(slot)] != 0;
        if (((c.step != 0xFFFF && c.step != 0) || was_active) &&
            (ticks_[slot] == 0 || phase >= phases_[slot]))
            ++ticks_[slot];
        phases_[slot] = phase;
        c.ticks = ticks_[slot];
        if (c.step != steps_[slot] || c.track != tracks_[slot]) {
            if (c.step == 0xFFFF)
                c.event = 'E';
            else if (c.step) {
                const auto addr = uint16_t(c.track + (c.step - 1) * 2);
                int op = m.rd8(0xD000, addr);
                c.duration = m.rd8(0xD000, uint16_t(addr + 1));
                if (c.step >= 2 && m.rd8(0xD000, uint16_t(addr - 2)) == 0x7F) {
                    c.event = 'T';
                    c.duration = m.rd8(0xD000, uint16_t(addr - 1));
                } else if (op < 0xA0)
                    c.event = (op & 15) >= 12 ? 'R' : 'N';
                else if (op == 0xA7)
                    c.event = 'H';
            }
        }
        steps_[slot] = c.step;
        tracks_[slot] = c.track;
        c.ceiling = w.ram[size_t(b + 0x12)];
        c.rate = w.ram[size_t(b + 6)] & 15;
        c.phase = phase;
        c.remaining = w.ram[size_t(b + 9)];
        c.volume = w.io[size_t(0x88 + c.hw)];
        c.period = uint16_t((w.io[size_t(0x80 + c.hw * 2)] | w.io[size_t(0x81 + c.hw * 2)] << 8) & 0x7FF);
        if (slot >= 4 && (out.sfx & (1 << c.hw))) {
            // Effects own the hardware ports. Keep observing the music's private envelope and vibrato.
            const int env = w.ram[size_t(b + 0x28)], cap = w.ram[size_t(b + 0x12)];
            c.volume = uint8_t((std::min(env & 0xF0, cap & 0xF0)) | std::min(env >> 4, cap & 15));
            int delta = 0;
            const int vib = w.ram[size_t(b + 2)] & 0xF0;
            if (vib && !w.ram[size_t(b + 0x14)])
                delta = int8_t(m.rd8(0xD000, uint16_t((vib - 0x10) | (w.ram[0x604] & 15))));
            c.period = uint16_t((c.pitch + delta) & 0x7FF);
        }
        c.fade = slow.slow && slot >= 4 ? slow.last : fade.slots[size_t(slot)];
        // Keep the authored note velocity/envelope separate from the game's shared volume ramp.
        // Otherwise following voices would apply the ramp twice, and non-following voices never would.
        if (fade.engaged && slot >= 4)
            c.volume = fade.reference_volume[size_t(slot)];
    }
    // The offline scan uses the first surviving slot before each driver update as the
    // stream clock. Ending's tempo changes can leave independent slot phases different;
    // a release tick must follow that stream clock, not the releasing slot's phase.
    for (int first : {0, 4}) {
        const EnhancedChannel *clock = nullptr;
        for (int slot = first; slot < first + 4; ++slot)
            if (previous_steps[size_t(slot)] != 0xFFFF && previous_steps[size_t(slot)] != 0) {
                clock = &out.channels[size_t(slot)];
                break;
            }
        if (!clock)
            for (int slot = first; slot < first + 4; ++slot)
                if (out.channels[size_t(slot)].step != 0xFFFF && out.channels[size_t(slot)].step != 0) {
                    clock = &out.channels[size_t(slot)];
                    break;
                }
        if (clock)
            for (int slot = first; slot < first + 4; ++slot)
                ticks_[size_t(slot)] = out.channels[size_t(slot)].ticks = clock->ticks;
    }
    return out;
}

// A prepared entry is never rendered or reset before use. Resetting a used FluidSynth instance
// leaves block/reverb timing state and does not reproduce a fresh instance's PCM.
struct EnhancedRenderer::Pool {
    struct Synth {
        Fluid &api = fluid_api();
        fluid_settings_t *settings = nullptr;
        fluid_synth_t *synth = nullptr;
        fluid_sfont_t *borrowed = nullptr;
        Synth() {
            if (!api.usable)
                return;
            settings = api.new_fluid_settings();
            if (!settings)
                return;
            api.fluid_settings_setnum(settings, "synth.sample-rate", emu::Apu::kSampleRate);
            api.fluid_settings_setnum(settings, "synth.gain", 0.2);
            api.fluid_settings_setnum(settings, "synth.reverb.room-size", 0.55);
            api.fluid_settings_setnum(settings, "synth.reverb.width", 0.8);
            api.fluid_settings_setint(settings, "synth.chorus.active", 0);
            api.fluid_settings_setint(settings, "synth.polyphony", 32);
            api.fluid_settings_setint(settings, "synth.cpu-cores", 1);
            api.fluid_settings_setint(settings, "synth.threadsafe-api", 0);
            // Sharing requires fully resident samples; preset selection must never do file I/O.
            api.fluid_settings_setint(settings, "synth.dynamic-sample-loading", 0);
            synth = api.new_fluid_synth(settings);
        }
        ~Synth() {
            if (synth) {
                // add_sfont transfers ownership, so detach every borrower before deletion.
                if (borrowed) {
                    // remove_sfont resets programs; unset them first so intentional detachment
                    // does not log sixteen missing-preset warnings per retired synth.
                    for (int ch = 0; ch < 16; ++ch)
                        api.fluid_synth_unset_program(synth, ch);
                    api.fluid_synth_remove_sfont(synth, borrowed);
                }
                api.delete_fluid_synth(synth);
            }
            if (settings)
                api.delete_fluid_settings(settings);
        }
    };
    std::unique_ptr<Synth> owner;
    fluid_sfont_t *font = nullptr;
    // FluidSynth 2.3.4 sample/preset reference counts are non-atomic. Serialize all shared-font
    // access, including retirement, with rendering. Empty synth construction needs no such lock.
    std::mutex rendering;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::vector<std::unique_ptr<Synth>> ready;
    std::deque<std::unique_ptr<Synth>> retired;
    size_t target = 0;
    bool stopping = false, failed = false;
    std::atomic<uint64_t> prepared{0}, inline_created{0};
    std::thread preparer;

    // Called only during initialization or with rendering held. Each new synth starts at SF ID 1;
    // add_sfont thus leaves the shared font's ID unchanged. No preset iteration is used.
    bool attach(Synth &entry) {
        if (!entry.synth || owner->api.fluid_synth_add_sfont(entry.synth, font) < 0)
            return false;
        entry.borrowed = font;
        return true;
    }
    std::unique_ptr<Synth> fresh() {
        auto entry = std::make_unique<Synth>();
        if (!attach(*entry))
            return {};
        return entry;
    }
    explicit Pool(const std::string &sf) : owner(std::make_unique<Synth>()) {
        const char *error = "FluidSynth library unavailable";
        if (owner->api.usable) {
            error = "FluidSynth initialization failed";
            if (owner->synth) {
                error = "soundfont unavailable";
                // Check first to avoid FluidSynth's additional missing-file diagnostics.
                if (FILE *file = std::fopen(sf.c_str(), "rb")) {
                    std::fclose(file);
                    if (owner->api.fluid_synth_sfload(owner->synth, sf.c_str(), 1) >= 0)
                        font = owner->api.fluid_synth_get_sfont(owner->synth, 0);
                }
            }
        }
        if (!font) {
            std::fprintf(stderr, "sawgod: Enhanced music uses WSC (%s)\n", error);
            return;
        }
        size_t largest = 0;
        for (const auto &song : songs) {
            size_t need = !song.drums.hits.empty();
            for (const auto &v : song.voices)
                need += song.isolated_voices || v.amplitude != 1 || !v.amplitude_sections.empty() ||
                        !v.envelopes.empty();
            largest = std::max(largest, need);
        }
        // One largest replacement for music and one for an overlapping cue. The two persistent
        // base synths retain their original lifetime/controllers/reverb across song boundaries.
        target = 2 * largest;
        ready.reserve(target);
        for (size_t i = 0; i < target; ++i) {
            auto entry = fresh();
            if (!entry) {
                failed = true;
                break;
            }
            ready.push_back(std::move(entry));
            ++prepared;
        }
        preparer = std::thread([this] {
            SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW);
            for (;;) {
                std::unique_ptr<Synth> old;
                bool replenish;
                {
                    std::unique_lock lock(mutex);
                    wake.wait(lock, [&] {
                        return stopping || !retired.empty() || (!failed && ready.size() < target);
                    });
                    if (stopping)
                        return;
                    if (!retired.empty()) {
                        old = std::move(retired.front());
                        retired.pop_front();
                    }
                    replenish = !failed && ready.size() < target;
                }
                if (old) {
                    std::lock_guard lock(rendering);
                    old.reset();
                }
                if (replenish) {
                    // The expensive construction is outside the shared-font lock.
                    auto entry = std::make_unique<Synth>();
                    bool attached;
                    {
                        std::lock_guard lock(rendering);
                        attached = attach(*entry);
                    }
                    std::lock_guard lock(mutex);
                    if (attached) {
                        ready.push_back(std::move(entry));
                        ++prepared;
                    } else
                        failed = true;
                }
            }
        });
    }
    // Audio worker never waits for preparation. Exceptional rapid replacements retain the old
    // correct-but-slow behavior using the already loaded font, and expose the shortage in stats.
    std::unique_ptr<Synth> take() {
        {
            std::lock_guard lock(mutex);
            if (!ready.empty()) {
                auto entry = std::move(ready.back());
                ready.pop_back();
                wake.notify_one();
                return entry;
            }
        }
        ++inline_created;
        return fresh();
    }
    void retire(std::unique_ptr<Synth> entry) {
        if (!entry)
            return;
        std::lock_guard lock(mutex);
        retired.push_back(std::move(entry));
        wake.notify_one();
    }
    SynthStats stats() const {
        std::lock_guard lock(mutex);
        return {prepared.load(), inline_created.load(), ready.size(), target};
    }
    ~Pool() {
        {
            std::lock_guard lock(mutex);
            stopping = true;
            wake.notify_one();
        }
        if (preparer.joinable())
            preparer.join();
        // All renderer/voice owners are gone. Release borrower voices/presets before the owner
        // unloads the font; keep FluidSynth itself loaded for its process-global GLib state.
        retired.clear();
        ready.clear();
        owner.reset();
    }
};

struct EnhancedRenderer::Impl {
    Fluid &api = fluid_api();
    std::shared_ptr<Pool> pool;
    std::unique_ptr<Pool::Synth> instance;
    fluid_synth_t *synth = nullptr;
    // Isolated authored mixes use one synth per voice; other mixes add only an
    // amplified voice's copy. Linear PCM gain preserves its controls and reverb.
    std::array<std::unique_ptr<Impl>, 15> amplified;
    const EnhancedSong *song = nullptr;
    uint64_t generation = ~uint64_t(0), sequence = ~uint64_t(0);
    Stats stats;
    EventSink sink;
    DrumSink drum_sink;
    std::unique_ptr<Impl> percussion;
    int drum_tick = 0;
    std::vector<double> drum_amplitude;
    struct DrumOn {
        uint64_t sample;
        double tick;
        int channel, key, velocity;
    };
    std::vector<DrumOn> drum_ons;
    uint64_t drum_sample = 0;
    struct DrumOff {
        uint64_t sample;
        int channel, key;
    };
    std::vector<DrumOff> drum_offs;
    void drum_events(const EnhancedFrame &f, std::span<const EnhancedFrame> future) {
        if (!percussion || !song)
            return;
        const int tick = f.channels[4].ticks;
        const auto &d = song->drums;
        const double boosted = std::pow(10.0, d.first_part_db / 20);
        const int local = tick < d.loop_tick ? tick : d.loop_tick + (tick - d.loop_tick) % d.loop_ticks;
        const double current_gain = local >= d.first_part_start && local < d.first_part_end ? boosted : 1;
        drum_amplitude.assign(f.samples.size() / 2, current_gain);
        if (d.first_part_db != 0)
            for (size_t i = 0; i < future.size(); ++i) {
                const auto &n = future[i];
                if (n.song != f.song || n.generation != f.generation)
                    break;
                const int next_tick = n.channels[4].ticks;
                const int pos = next_tick < d.loop_tick
                                    ? next_tick
                                    : d.loop_tick + (next_tick - d.loop_tick) % d.loop_ticks;
                const double next = pos >= d.first_part_start && pos < d.first_part_end ? boosted : 1;
                if (next == current_gain)
                    continue;
                const double ramp = d.ramp_ms * emu::Apu::kSampleRate / 1000;
                const double distance = (i + 1) * 318.0;
                for (size_t sample = 0; sample < drum_amplitude.size(); ++sample)
                    drum_amplitude[sample] +=
                        (next - current_gain) * std::clamp((sample - distance + ramp) / ramp, 0.0, 1.0);
                break;
            }
        if (amplitude_sink && d.first_part_db != 0 && !drum_amplitude.empty())
            amplitude_sink({sequence, song->id, -1, drum_amplitude.front(), drum_amplitude.back()});
        // Tick interpolation uses first-observed tick anchors, including skipped
        // driver frames. Lookahead schedules fractional triplet cells within PCM.
        if (tick != drum_tick) {
            int next_tick = tick + 1;
            size_t distance = 1;
            for (size_t i = 0; i < future.size(); ++i) {
                if (future[i].song != f.song || future[i].generation != f.generation)
                    break;
                if (future[i].channels[4].ticks > tick) {
                    next_tick = future[i].channels[4].ticks;
                    distance = i + 1;
                    break;
                }
            }
            for (const auto &h : d.hits) {
                double at = h.tick;
                if (at >= d.loop_tick && tick > at)
                    at += std::ceil((tick - at) / d.loop_ticks) * d.loop_ticks;
                if (at < tick || at >= next_tick)
                    continue;
                const auto offset =
                    uint64_t(std::nearbyint((at - tick) / (next_tick - tick) * distance * 318));
                drum_ons.push_back({drum_sample + offset, at, h.channel, h.key, h.velocity});
            }
        }
        drum_tick = tick;
    }
    void render_drums(float *output, int count, int frame_begin) {
        if (!percussion || !song)
            return;
        percussion->pcm.resize(size_t(count) * 2);
        int begin = 0;
        while (begin < count) {
            for (auto it = drum_offs.begin(); it != drum_offs.end();) {
                if (it->sample <= drum_sample) {
                    api.fluid_synth_noteoff(percussion->synth, it->channel, it->key);
                    if (drum_sink)
                        drum_sink({sequence, drum_sample, double(drum_tick), song->id, it->channel,
                                   it->channel == 0 ? 47 : song->drums.kit, it->key, 0});
                    it = drum_offs.erase(it);
                } else
                    ++it;
            }
            for (auto it = drum_ons.begin(); it != drum_ons.end();) {
                if (it->sample <= drum_sample) {
                    api.fluid_synth_noteon(percussion->synth, it->channel, it->key, it->velocity);
                    drum_offs.push_back(
                        {drum_sample + uint64_t(it->channel == 0 ? 9600 : 2880), it->channel, it->key});
                    if (drum_sink)
                        drum_sink({sequence, drum_sample, it->tick, song->id, it->channel,
                                   it->channel == 0 ? 47 : song->drums.kit, it->key, it->velocity});
                    it = drum_ons.erase(it);
                } else
                    ++it;
            }
            int end = count;
            for (const auto &off : drum_offs)
                end = std::min(end, begin + int(off.sample - drum_sample));
            for (const auto &on : drum_ons)
                end = std::min(end, begin + int(on.sample - drum_sample));
            auto *buffer = percussion->pcm.data() + begin * 2;
            api.fluid_synth_write_float(percussion->synth, end - begin, buffer, 0, 2, buffer, 1, 2);
            drum_sample += uint64_t(end - begin);
            begin = end;
        }
        const double gain = song->drums.gain / song->gain;
        for (size_t i = 0; i < percussion->pcm.size(); ++i)
            output[i] += float(percussion->pcm[i] * gain * drum_amplitude[size_t(frame_begin) + i / 2]);
    }
    ControlSink control_sink;
    AmplitudeSink amplitude_sink;
    struct Playing {
        int voice = -1, key = 0, peak = 1, off_tick = -1;
        bool articulating = false;
        int expression = 127, off_frames = -1;
        uint64_t gate_at = ~uint64_t(0);
        EnhancedVoice cast{};
    };
    std::array<Playing, 15> playing{};
    std::array<std::map<int, uint64_t>, 8> tick_samples;
    std::vector<float> pcm;
    std::array<std::vector<double>, 15> gain_pcm;
    int channel(int voice) const {
        return song->voices[size_t(voice)].noise ? 9 : voice < 9 ? voice : voice + 1;
    }
    template <class F> void for_voice(int voice, const F &send) {
        if (!song->isolated_voices)
            send(synth);
        if (const auto &extra = amplified[size_t(voice)])
            send(extra->synth);
    }
    void stop(Playing &p, int subframe = 0) {
        if (p.voice >= 0) {
            for_voice(p.voice,
                      [&](auto *target) { api.fluid_synth_noteoff(target, channel(p.voice), p.key); });
            if (sink)
                sink({sequence, song->id, p.voice, p.key, 0, subframe});
        }
        p = {};
    }
    void select(int id, uint64_t gen) {
        for (auto &p : playing)
            stop(p);
        for (int ch = 0; ch < 16; ++ch)
            api.fluid_synth_all_sounds_off(synth, ch);
        for (auto &extra : amplified)
            extra.reset();
        if (song && drum_sink)
            for (const auto &off : drum_offs)
                drum_sink({sequence, drum_sample, double(drum_tick), song->id, off.channel,
                           off.channel == 0 ? 47 : song->drums.kit, off.key, 0});
        percussion.reset();
        drum_offs.clear();
        drum_ons.clear();
        drum_tick = 0;
        drum_sample = 0;
        playing = {};
        tick_samples = {};
        generation = gen;
        song = enhanced_song(id);
        if (!song)
            return;
        if (!song->drums.hits.empty()) {
            percussion = std::make_unique<Impl>(pool, pool->take());
            if (!percussion->synth) {
                song = nullptr;
                return;
            }
            api.fluid_synth_program_change(percussion->synth, 9, song->drums.kit);
            api.fluid_synth_program_change(percussion->synth, 0, 47);
            api.fluid_synth_cc(percussion->synth, 9, 7, 100);
            api.fluid_synth_cc(percussion->synth, 9, 91, 25);
            api.fluid_synth_cc(percussion->synth, 9, 10, 64);
        }
        for (size_t i = 0; i < song->voices.size(); ++i) {
            const auto &v = song->voices[i];
            if (song->isolated_voices || v.amplitude != 1 || !v.amplitude_sections.empty() ||
                !v.envelopes.empty()) {
                amplified[i] = std::make_unique<Impl>(pool, pool->take());
                if (!amplified[i]->synth) {
                    song = nullptr;
                    return;
                }
            }
            const int ch = channel(int(i));
            for_voice(int(i), [&](auto *target) {
                api.fluid_synth_program_change(target, ch, v.program);
                for (auto [cc, val] : {std::pair{0, 0},
                                       {7, v.volume},
                                       {91, v.reverb},
                                       {93, 0},
                                       {11, 127},
                                       {10, 64},
                                       {73, v.attack},
                                       {72, v.release_cc}})
                    api.fluid_synth_cc(target, ch, cc, val);
                api.fluid_synth_pitch_wheel_sens(target, ch, v.bend_range);
                api.fluid_synth_pitch_bend(target, ch, 8192);
            });
        }
    }
    Impl(std::shared_ptr<Pool> shared, std::unique_ptr<Pool::Synth> entry)
        : pool(std::move(shared)), instance(std::move(entry)), synth(instance ? instance->synth : nullptr) {}
    explicit Impl(std::shared_ptr<Pool> shared) : pool(std::move(shared)) {
        if (pool->font) {
            std::lock_guard lock(pool->rendering);
            instance = pool->fresh();
            synth = instance ? instance->synth : nullptr;
        }
    }
    ~Impl() { pool->retire(std::move(instance)); }
};
EnhancedRenderer::EnhancedRenderer(const std::string &sf)
    : EnhancedRenderer(std::make_shared<Pool>(sf), true) {}
EnhancedRenderer::EnhancedRenderer(std::shared_ptr<Pool> pool, bool with_cue)
    : impl_(std::make_unique<Impl>(pool)) {
    if (with_cue && impl_->synth)
        cue_.reset(new EnhancedRenderer(std::move(pool), false));
}
EnhancedRenderer::~EnhancedRenderer() = default;
bool EnhancedRenderer::available() const { return impl_->synth && (!cue_ || cue_->available()); }
EnhancedRenderer::Stats EnhancedRenderer::stats() const { return impl_->stats; }
EnhancedRenderer::SynthStats EnhancedRenderer::synth_stats() const { return impl_->pool->stats(); }
EnhancedRenderer::Stats EnhancedRenderer::cue_stats() const { return cue_ ? cue_->stats() : Stats{}; }
int EnhancedRenderer::active_notes() const {
    return int(std::count_if(impl_->playing.begin(), impl_->playing.end(),
                             [](const auto &p) { return p.voice >= 0; })) +
           (cue_ ? cue_->active_notes() : 0);
}
void EnhancedRenderer::set_drum_sink(DrumSink sink) { impl_->drum_sink = std::move(sink); }
void EnhancedRenderer::set_event_sink(EventSink sink) {
    impl_->sink = sink;
    if (cue_)
        cue_->set_event_sink(std::move(sink));
}
void EnhancedRenderer::set_control_sink(ControlSink sink) {
    impl_->control_sink = sink;
    if (cue_)
        cue_->set_control_sink(std::move(sink));
}
void EnhancedRenderer::set_amplitude_sink(AmplitudeSink sink) {
    impl_->amplitude_sink = sink;
    if (cue_)
        cue_->set_amplitude_sink(std::move(sink));
}
void EnhancedRenderer::mix(EnhancedFrame &f, std::span<const EnhancedFrame> future) {
    if (!available())
        return;
    std::lock_guard lock(impl_->pool->rendering);
    mix_stream(f, future);
    if (!cue_)
        return;
    const auto cue_frame = [](const EnhancedFrame &source) {
        EnhancedFrame out;
        out.song = source.cue;
        out.generation = source.cue_generation;
        out.sequence = source.sequence;
        out.gain = source.cue_gain;
        out.channels = source.channels;
        for (int slot = 0; slot < 8; ++slot)
            if (!(source.cue_slots & (1 << slot))) {
                auto &c = out.channels[size_t(slot)];
                c.event = 'E';
                c.owned = false;
                c.ceiling = 0;
                c.step = 0xFFFF;
            }
        return out;
    };
    auto current = cue_frame(f);
    current.samples = std::move(f.samples);
    cue_future_.clear();
    cue_future_.reserve(future.size());
    for (const auto &n : future)
        cue_future_.push_back(cue_frame(n));
    cue_->mix_stream(current, cue_future_);
    f.samples = std::move(current.samples);
}
void EnhancedRenderer::mix_stream(EnhancedFrame &f, std::span<const EnhancedFrame> future) {
    auto &p = *impl_;
    if (!p.synth)
        return;
    ++p.stats.frames;
    const bool discontinuity = p.sequence != ~uint64_t(0) && f.sequence != p.sequence + 1;
    const bool fresh = f.generation != p.generation || discontinuity;
    p.sequence = f.sequence;
    if (fresh || (p.song ? p.song->id : 0) != (enhanced_song(f.song) ? f.song : 0))
        p.select(f.song, f.generation);
    p.drum_events(f, future);
    if (p.song && p.song->layered)
        for (int slot = 0; slot < 8; ++slot) {
            auto &history = p.tick_samples[size_t(slot)];
            const int tick = f.channels[size_t(slot)].ticks;
            history.try_emplace(tick, f.sequence * 318);
            const int oldest = tick - 2 * p.song->cast_loop_ticks;
            if (p.song->cast_loop_ticks)
                for (auto it = history.lower_bound(p.song->cast_loop_tick);
                     it != history.end() && it->first < oldest;)
                    it = history.erase(it);
        }
    if (p.song) {
        const int lanes = p.song->layered ? int(p.song->voices.size()) : 8;
        for (int lane = 0; lane < lanes; ++lane) {
            const int slot = p.song->layered ? p.song->voices[size_t(lane)].slot : lane;
            if (std::none_of(p.song->voices.begin(), p.song->voices.end(),
                             [slot](const auto &v) { return v.slot == slot; }))
                continue;
            auto c = f.channels[size_t(slot)];
            if (discontinuity && c.event == '-' && c.step && c.step != 0xFFFF && c.pitch)
                c.event = 'N';
            auto &active = p.playing[size_t(lane)];
            if (!c.owned) {
                p.stop(active); // A replaced cue slot has no release over its new owner's sound.
                continue;
            }
            if (active.gate_at <= f.sequence * 10)
                p.stop(active);
            if (active.voice >= 0)
                p.for_voice(active.voice, [&](auto *target) {
                    p.api.fluid_synth_cc(
                        target, p.channel(active.voice), 11,
                        (f.sfx & (1 << c.hw)) || (!c.ceiling && c.step != 0xFFFF) ? 0 : active.expression);
                });
            if (active.off_frames == 0)
                p.stop(active);
            if (active.off_frames > 0)
                --active.off_frames;
            if (active.off_tick >= 0 && c.ticks >= active.off_tick)
                p.stop(active);
            if ((c.event == 'R' || c.event == 'E') && active.voice >= 0) {
                const auto &v = active.cast;
                const EnhancedChannel *next = nullptr;
                for (const auto &n : future) {
                    if (n.generation != f.generation || n.song != f.song)
                        break;
                    const auto &nc = n.channels[size_t(slot)];
                    if (nc.event == 'N' || nc.event == 'T' || nc.event == 'R' || nc.event == 'E') {
                        next = &nc;
                        break;
                    }
                }
                const bool bridge = c.event == 'R' && c.duration <= v.legato && next &&
                                    (next->event == 'N' || next->event == 'T') &&
                                    next->ticks - c.ticks <= v.legato;
                active.articulating = true;
                active.off_tick = bridge ? -1 : c.ticks + v.release;
                // An ended slot no longer updates its tempo phase. Finish its release tail.
                if (c.event == 'E') {
                    active.off_frames =
                        std::max(0, int(std::nearbyint(v.release * 16.0 / (16 - c.rate))) - 1);
                    for (size_t i = 0; i < future.size(); ++i) {
                        const auto &n = future[i];
                        if (n.generation != f.generation || n.song != f.song)
                            break;
                        if (n.channels[size_t(slot)].ticks >= c.ticks + v.release) {
                            active.off_frames = int(i);
                            break;
                        }
                    }
                }
                if (bridge)
                    ++p.stats.bridges;
                else if (v.release)
                    ++p.stats.releases;
                if (!bridge && !v.release)
                    p.stop(active);
                continue;
            }
            if (c.event == 'N' || c.event == 'T') {
                int vi = -1;
                const bool noise = c.hw == 3 && c.noise;
                for (size_t i = 0; i < p.song->voices.size(); ++i) {
                    if (p.song->layered && int(i) != lane)
                        continue;
                    const auto &v = p.song->voices[i];
                    if (v.slot == slot && v.instrument == c.instrument && v.noise == noise)
                        vi = int(i);
                }
                if (vi < 0 || (c.hw == 1 && c.noise) || (!noise && !c.pitch)) {
                    p.stop(active);
                    if (vi < 0 && !p.song->layered)
                        ++p.stats.unmapped;
                    continue;
                }
                auto v = p.song->voices[size_t(vi)];
                int tick = c.ticks;
                if (p.song->cast_loop_ticks && tick >= p.song->cast_loop_tick)
                    tick = p.song->cast_loop_tick + (tick - p.song->cast_loop_tick) % p.song->cast_loop_ticks;
                for (const auto &cast : v.casts)
                    if (tick >= cast.tick) {
                        v.program = cast.program;
                        v.bank = cast.bank;
                        v.volume = cast.volume;
                        v.transpose = cast.transpose;
                        v.seat = cast.seat;
                        v.enabled = cast.enabled;
                    }
                if (!v.enabled ||
                    (!v.note_ticks.empty() &&
                     std::find(v.note_ticks.begin(), v.note_ticks.end(), c.ticks) == v.note_ticks.end())) {
                    p.stop(active);
                    continue;
                }
                const int key = noise ? v.drums[size_t(c.noise & 7)] : midi(note(c.pitch)) + v.transpose;
                if (key < 0 || key > 127) {
                    p.stop(active);
                    ++p.stats.unmapped;
                    continue;
                }
                if (c.event == 'T' && active.voice == vi && active.key == key) {
                    active.articulating = false;
                    active.off_tick = -1;
                    active.off_frames = -1;
                    continue;
                }
                p.stop(active);
                const auto &n = !future.empty() && future[0].generation == f.generation
                                    ? future[0].channels[size_t(slot)]
                                    : c;
                const int left = n.volume >> 4, right = n.volume & 15, peak = std::max({left, right, 1});
                const int velocity = std::max(1, midi(127 * std::sqrt(peak / 15.0)));
                const int wsc_pan = left + right ? midi(64 + 63.0 * (right - left) / (left + right)) : 64;
                const int pan = enhanced_pan(wsc_pan, v.seat);
                const int ch = p.channel(vi);
                p.for_voice(vi, [&](auto *target) {
                    if (p.song->layered) {
                        p.api.fluid_synth_cc(target, ch, 0, v.bank);
                        p.api.fluid_synth_program_change(target, ch, v.program);
                        p.api.fluid_synth_cc(target, ch, 7, v.volume);
                    }
                    p.api.fluid_synth_cc(target, ch, 73, v.attack);
                    p.api.fluid_synth_cc(target, ch, 72, v.release_cc);
                    p.api.fluid_synth_cc(target, ch, 10, pan);
                    p.api.fluid_synth_cc(target, ch, 11, (f.sfx & (1 << c.hw)) || !c.ceiling ? 0 : 127);
                    p.api.fluid_synth_pitch_bend(target, ch, 8192);
                    p.api.fluid_synth_noteon(target, ch, key, velocity);
                });
                if (p.sink)
                    p.sink({f.sequence, p.song->id, vi, key, velocity});
                if (p.control_sink) {
                    if (p.song->layered) {
                        p.control_sink({f.sequence, p.song->id, vi, 0, v.bank});
                        p.control_sink({f.sequence, p.song->id, vi, 7, v.volume});
                        p.control_sink({f.sequence, p.song->id, vi, 128, v.program});
                    }
                    p.control_sink({f.sequence, p.song->id, vi, 10, pan});
                    p.control_sink({f.sequence, p.song->id, vi, 73, v.attack});
                    p.control_sink({f.sequence, p.song->id, vi, 72, v.release_cc});
                }
                active = {vi, key, peak, -1, false};
                active.cast = v;
                if (v.gate != 1) {
                    // Predict the decoded note's end without adding playback latency.
                    // A wrapped phase skips the sequencer tick, exactly as the WSC driver does.
                    int phase = c.phase, remaining = c.remaining ? c.remaining : 256, frames = 0;
                    while (remaining) {
                        ++frames;
                        phase += c.rate;
                        if (phase >= 16)
                            phase -= 16;
                        else
                            --remaining;
                    }
                    // A replacement inside existing lookahead caps the original gate.
                    for (size_t i = 0; i < future.size() && int(i) < frames; ++i)
                        if (future[i].generation != f.generation || future[i].song != f.song) {
                            frames = int(i) + 1;
                            break;
                        }
                    active.gate_at =
                        f.sequence * 10 + uint64_t(std::max(1.0, std::nearbyint(frames * 10 * v.gate)));
                }
                ++p.stats.notes;
                continue;
            }
            if (active.voice < 0)
                continue;
            const auto &v = active.cast;
            const int ch = p.channel(active.voice);
            if (v.follow && !v.noise && !active.articulating) {
                const double level = double(std::max(c.volume >> 4, c.volume & 15)) / active.peak;
                active.expression = midi(127 * std::sqrt(std::min(level, 1.0)));
                if (c.period && c.pitch) {
                    const int bend =
                        std::clamp(int(std::nearbyint(8192 + (note(c.period) - (active.key - v.transpose)) /
                                                                 v.bend_range * 8192)),
                                   0, 16383);
                    p.for_voice(active.voice,
                                [&](auto *target) { p.api.fluid_synth_pitch_bend(target, ch, bend); });
                }
            }
        }
    }
    // CC11 is quadratic in FluidSynth. Divide out the final PCM gain so each voice follows its
    // own driver ramp once; the PCM gain also removes release/reverb and authored percussion at zero.
    for (const auto &active : p.playing)
        if (active.voice >= 0) {
            const auto &c = f.channels[size_t(active.cast.slot)];
            const float relative = f.music_fade > 0 ? c.fade / f.music_fade : 0;
            // During the opening's slow fade the host envelope, not the driver's ceiling, silences music
            // voices.
            const bool ceiling_mute =
                !c.ceiling && c.step != 0xFFFF && !(f.slow_fade && active.cast.slot >= 4);
            const int expression =
                (f.sfx & (1 << c.hw)) || ceiling_mute ? 0 : midi(active.expression * std::sqrt(relative));
            p.for_voice(active.voice, [&](auto *target) {
                p.api.fluid_synth_cc(target, p.channel(active.voice), 11, expression);
            });
        }
    p.pcm.resize(f.samples.size());
    // Absolute observed driver ticks do not restart at the loop boundary.
    // Existing lookahead locates the section's exact frame before its rest ramp.
    if (p.song)
        for (size_t vi = 0; vi < p.song->voices.size(); ++vi) {
            const auto &v = p.song->voices[vi];
            double first = v.amplitude, end = first;
            for (const auto &section : v.amplitude_sections) {
                if (f.channels[size_t(v.slot)].ticks >= section.tick) {
                    first = end = section.amplitude;
                    continue;
                }
                for (size_t i = 0; i < future.size(); ++i) {
                    const auto &next = future[i];
                    if (next.generation != f.generation || next.song != f.song)
                        break;
                    if (next.channels[size_t(v.slot)].ticks < section.tick)
                        continue;
                    const int distance = int(i) + 1;
                    if (distance <= section.ramp_frames) {
                        const double delta = section.amplitude - end;
                        first += delta * (section.ramp_frames - distance) / section.ramp_frames;
                        end += delta * (section.ramp_frames - distance + 1) / section.ramp_frames;
                    }
                    break;
                }
            }
            const auto anchor = [&](int tick) {
                const auto &history = p.tick_samples[size_t(v.slot)];
                if (const auto it = history.find(tick); it != history.end())
                    return double(it->second);
                for (size_t i = 0; i < future.size(); ++i)
                    if (future[i].song == f.song && future[i].generation == f.generation &&
                        future[i].channels[size_t(v.slot)].ticks >= tick)
                        return double((f.sequence + i + 1) * 318);
                const auto &c = f.channels[size_t(v.slot)];
                const int remaining = tick - c.ticks;
                const int frames =
                    remaining ? int(std::floor(double(16 * (remaining - 1) + c.phase) / (16 - c.rate))) + 1
                              : 0;
                return (double(f.sequence) + frames) * 318;
            };
            struct TimedGain {
                double end, start, value;
            };
            std::vector<std::pair<double, std::vector<TimedGain>>> envelopes;
            for (const auto &env : v.envelopes) {
                std::vector<TimedGain> points;
                const int current = f.channels[size_t(v.slot)].ticks;
                const int passes =
                    env.loop_ticks ? std::max(1, (current - env.loop_tick) / env.loop_ticks + 2) : 1;
                const int first_pass = env.loop_ticks ? std::max(0, passes - 3) : 0;
                for (int pass = first_pass; pass < passes; ++pass)
                    for (const auto &point : env.points) {
                        if (pass && point.tick < env.loop_tick)
                            continue;
                        const double end_sample =
                            anchor(point.tick + pass * env.loop_ticks) + point.offset_ms * 24;
                        points.push_back({end_sample, end_sample - point.ramp_ms * 24, point.value});
                    }
                envelopes.emplace_back(first_pass ? env.points.back().value : env.initial, std::move(points));
            }
            const auto gain_at = [&](double sample) {
                double product = 1;
                for (const auto &[initial, points] : envelopes) {
                    double value = initial;
                    for (const auto &point : points) {
                        if (sample >= point.end)
                            value = point.value;
                        else if (sample >= point.start && point.end > point.start)
                            value +=
                                (point.value - value) * (sample - point.start) / (point.end - point.start);
                    }
                    product *= value;
                }
                return product;
            };
            const double start_sample = double(f.sequence * 318);
            auto &samples = p.gain_pcm[vi];
            samples.resize(f.samples.size() / 2);
            for (size_t i = 0; i < samples.size(); ++i)
                samples[i] = (first + (end - first) * double(i) / samples.size()) * gain_at(start_sample + i);
            first *= gain_at(start_sample);
            end *= gain_at(start_sample + f.samples.size() / 2);
            if (p.amplitude_sink && (!v.amplitude_sections.empty() || !v.envelopes.empty()) &&
                !f.samples.empty()) {
                const double last = p.gain_pcm[vi].back();
                p.amplitude_sink({f.sequence, p.song->id, int(vi), first, last});
            }
        }
    const auto render_segment = [&](int begin, int end) {
        if (end <= begin)
            return;
        auto *pcm = p.pcm.data() + begin * 2;
        if (p.song && p.song->isolated_voices)
            std::fill(pcm, pcm + (end - begin) * 2, 0);
        else
            p.api.fluid_synth_write_float(p.synth, end - begin, pcm, 0, 2, pcm, 1, 2);
        if (p.song)
            for (size_t vi = 0; vi < p.song->voices.size(); ++vi)
                if (auto &extra = p.amplified[vi]) {
                    extra->pcm.resize(size_t(end - begin) * 2);
                    p.api.fluid_synth_write_float(extra->synth, end - begin, extra->pcm.data(), 0, 2,
                                                  extra->pcm.data(), 1, 2);

                    for (size_t i = 0; i < extra->pcm.size(); ++i) {
                        const double amplitude =
                            p.gain_pcm[vi][size_t(begin) + i / 2] - (p.song->isolated_voices ? 0 : 1);
                        pcm[i] += float(extra->pcm[i] * amplitude);
                    }
                }
        p.render_drums(pcm, end - begin, begin);
    };
    // Keep fractional MIDI gates in the PCM as well as the diagnostic stream.
    // Events at the next frame boundary are handled before that frame's controllers.
    int begin = 0;
    for (int subframe = 1; subframe < 10; ++subframe) {
        const uint64_t tick = f.sequence * 10 + uint64_t(subframe);
        if (std::none_of(p.playing.begin(), p.playing.end(),
                         [tick](const auto &voice) { return voice.voice >= 0 && voice.gate_at == tick; }))
            continue;
        const int end = int(std::nearbyint(double(f.samples.size() / 2) * subframe / 10));
        render_segment(begin, end);
        begin = end;
        for (auto &voice : p.playing)
            if (voice.voice >= 0 && voice.gate_at == tick)
                p.stop(voice, subframe);
    }
    render_segment(begin, int(f.samples.size() / 2));
    if (!p.pcm.empty()) {
        const float gain = f.gain * (f.slow_fade ? 1 : f.music_fade) *
                           (p.song ? p.song->gain * p.song->mix_gain : 0) * 32768;
        for (size_t i = 0; i < f.samples.size(); ++i) {
            const float envelope = f.slow_fade ? f.music_fade_first + (f.music_fade - f.music_fade_first) *
                                                                          float(i / 2 + 1) /
                                                                          float(f.samples.size() / 2)
                                               : 1;
            f.samples[i] =
                int16_t(std::clamp(float(f.samples[i]) + p.pcm[i] * gain * envelope, -32768.0f, 32767.0f));
        }
    }
}

static_assert(EnhancedAudio::kLookahead == AudioLatency::kLookahead);

struct EnhancedAudio::Impl {
    Queue<EnhancedFrame, 32> input;
    Queue<std::vector<int16_t>, 128> output;
    std::atomic<bool> ready{false}, available{false}, done{false}, stopping{false};
    std::atomic<uint64_t> dropped{0}, cost_ns{0}, total_ns{0}, max_ns{0}, rendered_frames{0};
    std::atomic<uint64_t> processed{0}, emitted_frames{0}, queued_frames{0};
    PlaybackSink playback_sink;
    OutputSink output_sink;
    std::thread worker;
    explicit Impl(std::string sf)
        : worker([this, sf = std::move(sf)] {
              EnhancedRenderer synth(sf);
              available.store(synth.available());
              ready.store(true);
              ready.notify_all();
              FastForwardAudio ff;

              struct Packet {
                  uint64_t id;
                  EnhancedFrame frame;
                  bool rendered = false;
                  uint64_t enhanced_sequence = 0;
              };
              std::deque<Packet> playback;
              std::vector<EnhancedFrame> pending;
              pending.reserve(EnhancedAudio::kLookahead + 2);
              uint64_t received = 0, pending_id = 0;
              const auto render = [&] {
                  const auto start = std::chrono::steady_clock::now();
                  synth.mix(pending.front(), std::span<const EnhancedFrame>(pending).subspan(1));
                  const uint64_t elapsed = uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                        std::chrono::steady_clock::now() - start)
                                                        .count());
                  cost_ns.store(elapsed);
                  total_ns.fetch_add(elapsed);
                  ++rendered_frames;
                  max_ns.store(std::max(max_ns.load(), elapsed));
                  // WSC/GB may already have played this frame. The synth still runs with its full
                  // future, so entering Enhanced cannot change velocities, articulation or held notes.
                  if (!playback.empty() && pending_id >= playback.front().id) {
                      const size_t index = size_t(pending_id - playback.front().id);
                      if (index < playback.size()) {
                          playback[index].frame.samples = std::move(pending.front().samples);
                          playback[index].rendered = true;
                          playback[index].enhanced_sequence = pending.front().sequence;
                      }
                  }
                  pending.erase(pending.begin());
                  ++pending_id;
              };
              const auto emit = [&](size_t samples, std::span<const int16_t> effects, int speed, float master,
                                    bool music_ready) {
                  std::vector<int16_t> audio;
                  if (music_ready) {
                      auto &p = playback.front();
                      audio = std::move(p.frame.samples);
                      if (playback_sink)
                          playback_sink(
                              {p.frame.sequence, p.enhanced_sequence, audio.size() / 2, p.rendered});
                      playback.pop_front();
                      ++emitted_frames;
                  } else
                      audio.assign(samples, 0);
                  for (size_t i = 0; i < std::min(audio.size(), effects.size()); ++i)
                      audio[i] = int16_t(std::clamp(int(audio[i]) + int(effects[i]), -32768, 32767));
                  audio = ff.process(std::move(audio), speed);
                  if (master != 1)
                      for (auto &sample : audio)
                          sample = int16_t(sample * master);
                  if (output_sink)
                      output_sink(std::move(audio));
                  else if (!output.push(audio))
                      ++dropped;
              };
              while (!stopping.load()) {
                  EnhancedFrame f;
                  if (input.pop(f)) {
                      const int delay = synth.available() ? std::clamp(f.lookahead, 0, kLookahead) : 0;
                      const size_t samples = f.samples.size();
                      const auto effects = std::move(f.effects);
                      const int speed = f.speed;
                      const float master = f.master;
                      playback.push_back({received, f});
                      pending.push_back(std::move(f));
                      if (pending.size() > EnhancedAudio::kLookahead)
                          render();
                      // A lock removes its delay only on a song replacement, never on a fidelity
                      // step. Old-song queued frames end at that replacement boundary.
                      while (playback.size() > size_t(delay + 1))
                          playback.pop_front();
                      emit(samples, effects, speed, master, playback.size() > size_t(delay));
                      queued_frames.store(playback.size());
                      processed.store(++received);
                      processed.notify_all();
                  } else if (done.load()) {
                      // Drain after observing done, including a final submit published just before it.
                      if (!input.empty())
                          continue;
                      while (!pending.empty())
                          render();
                      while (!playback.empty()) {
                          const auto &tail = playback.front().frame;
                          emit(tail.samples.size(), {}, tail.speed, tail.master, true);
                      }
                      queued_frames.store(0);
                      break;
                  } else {
                      std::this_thread::sleep_for(std::chrono::milliseconds(1));
                  }
              }
          }) {}
    ~Impl() {
        stopping.store(true);
        if (worker.joinable())
            worker.join();
    }
};
EnhancedAudio::EnhancedAudio(const std::string &sf) : impl_(std::make_unique<Impl>(sf)) {}
EnhancedAudio::~EnhancedAudio() = default;
bool EnhancedAudio::available() const { return impl_->available.load(); }
bool EnhancedAudio::initialized() const { return impl_->ready.load(); }
bool EnhancedAudio::submit(EnhancedFrame f) {
    if (impl_->input.push(f))
        return true;
    ++impl_->dropped;
    return false;
}
std::vector<int16_t> EnhancedAudio::take() {
    std::vector<int16_t> out, next;
    while (impl_->output.pop(next))
        out.insert(out.end(), next.begin(), next.end());
    return out;
}
void EnhancedAudio::wait_initialized() { impl_->ready.wait(false); }
void EnhancedAudio::wait_processed(uint64_t count) {
    auto value = impl_->processed.load();
    while (value < count) {
        impl_->processed.wait(value);
        value = impl_->processed.load();
    }
}
EnhancedAudio::Playback EnhancedAudio::playback() const {
    return {impl_->processed.load(), impl_->emitted_frames.load(), impl_->queued_frames.load()};
}
void EnhancedAudio::set_playback_sink(PlaybackSink sink) { impl_->playback_sink = std::move(sink); }
void EnhancedAudio::set_output_sink(OutputSink sink) { impl_->output_sink = std::move(sink); }
void EnhancedAudio::finish() {
    impl_->done.store(true);
    if (impl_->worker.joinable())
        impl_->worker.join();
}
double EnhancedAudio::cost_ms() const { return impl_->cost_ns.load() / 1e6; }
uint64_t EnhancedAudio::dropped() const { return impl_->dropped.load(); }
EnhancedAudio::Cost EnhancedAudio::cost() const {
    return {impl_->rendered_frames.load(), impl_->total_ns.load(), impl_->max_ns.load()};
}
} // namespace sawgod::app
