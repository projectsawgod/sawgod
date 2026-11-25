// Host-only WSC driver observer and live FluidSynth mixer. No game state is written.
#pragma once
#include "app/sound_mix.h"
#include "game/hooks.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace sawgod::emu {
class Wsc;
}
namespace sawgod::game {
class Session;
}
namespace sawgod::app {
struct EnhancedAmplitudeSection {
    int tick;
    double amplitude;
    int ramp_frames;
};
struct EnhancedCast {
    int tick, program, bank, volume, transpose;
    double seat;
    bool enabled;
};
struct EnhancedGainPoint {
    int tick;
    double value, ramp_ms, offset_ms;
};
struct EnhancedGainEnvelope {
    double initial;
    int loop_tick, loop_ticks;
    std::span<const EnhancedGainPoint> points;
};
struct EnhancedVoice {
    int slot, instrument, program, volume, reverb, legato, release, bend_range;
    bool follow, noise;
    std::array<int, 8> drums;
    double amplitude = 1;
    int transpose = 0, attack = 64, release_cc = 64;
    double gate = 1;
    std::span<const EnhancedAmplitudeSection> amplitude_sections{};
    int bank = 0;
    bool enabled = true;
    std::span<const EnhancedCast> casts{};
    std::span<const EnhancedGainEnvelope> envelopes{};
    std::span<const int> note_ticks{};
    double seat = 0; // Percent, audience-left negative; resolved by the shared GM table.
};
struct EnhancedDrumHit {
    double tick;
    int channel, key, velocity;
};
struct EnhancedDrums {
    int kit = 0, loop_tick = 0, loop_ticks = 0;
    double gain = 0;
    std::span<const EnhancedDrumHit> hits{};
    double first_part_db = 0;
    int first_part_start = 0, first_part_end = 0;
    double ramp_ms = 20;
};
struct EnhancedSong {
    int id;
    float gain;
    std::span<const EnhancedVoice> voices;
    bool isolated_voices = false;
    EnhancedDrums drums{};
    double mix_gain = 1;
    bool backing_wsc = false;
    int cast_loop_tick = 0, cast_loop_ticks = 0;
    bool layered = false;
};
int enhanced_pan(int wsc_pan, double seat);
const EnhancedSong *enhanced_song(int id);
std::string default_soundfont();

struct EnhancedChannel {
    bool owned = true;
    char event = '-';
    uint16_t step = 0xFFFF, track = 0, pitch = 0, period = 0;
    int ticks = 0;
    uint8_t ceiling = 255, rate = 0, phase = 0, remaining = 0;
    uint8_t volume = 0, instrument = 0, hw = 0, noise = 0, noise_param = 0, duration = 0;
    float fade = 1;
};
struct EnhancedFrame {
    std::array<EnhancedChannel, 8> channels{};
    uint64_t generation = 0, sequence = 0;
    int song = 0;
    int cue = 0;
    uint64_t cue_generation = 0;
    uint8_t cue_slots = 0; // Still owned by this accepted request, including ended tracks for release.
    uint8_t sfx = 0;
    float gain = 0, cue_gain = 0, master = 1;
    float music_fade = 1;
    float music_fade_first = 1;
    bool slow_fade = false;
    int speed = 1;
    int lookahead = 8;            // Playback only; the inaudible synth still observes eight future frames.
    std::vector<int16_t> samples; // WSC + GB music for precisely this driver frame, already weighted
    std::vector<int16_t> effects; // WSC + GB SFX, emitted immediately; never retained for lookahead
};
class EnhancedObserver {
  public:
    void attach(game::Session &session);
    void request(int song);
    void reset();
    uint64_t generation() const { return generation_; }
    bool playing(const emu::Wsc &wsc) const;
    bool cue_playing(const emu::Wsc &wsc) const;
    uint8_t wsc_music_slots(const emu::Wsc &wsc) const;
    EnhancedFrame frame(const emu::Wsc &wsc);

  private:
    SlowMusicFade slow_fade_;
    int song_ = 0;
    int cue_ = 0;
    uint64_t cue_generation_ = 0;
    uint8_t cue_slots_ = 0;
    std::array<uint16_t, 8> cue_tracks_{};
    uint64_t generation_ = 0, sequence_ = 0;
    std::array<uint16_t, 8> steps_{}, tracks_{};
    std::array<int, 8> ticks_{};
    std::array<uint8_t, 8> phases_{};
    game::hooks::Subscription closing_fade_sub_;
    game::hooks::Subscription request_sub_, reset_sub_;
    void restart_slots(uint8_t slots);
    void end_cue();
    uint8_t owned_cue_slots(const emu::Wsc &wsc, bool active_only) const;
};

// Synchronous renderer is also used by the worker and accelerated review/tests. Owns all FluidSynth state.
// A small lookahead keeps the offline spike's next-frame velocity and tick-based articulation exact.
// Performance Lo-Spec uses a measured per-song voice budget and lighter synthesis,
// applied when the next song starts. Explicit voice counts are for deterministic auditions.
void set_enhanced_music_low_spec(bool enabled);
void set_enhanced_music_voices(int voices);
int enhanced_music_voices();

class EnhancedRenderer {
  public:
    explicit EnhancedRenderer(const std::string &soundfont);
    ~EnhancedRenderer();
    bool available() const;
    void mix(EnhancedFrame &frame, std::span<const EnhancedFrame> future);
    struct Stats {
        uint64_t frames = 0, notes = 0, bridges = 0, releases = 0, unmapped = 0;
        uint64_t dropped_notes = 0, failed_noteons = 0;
        Stats &operator+=(const Stats &s) {
            frames += s.frames;
            notes += s.notes;
            bridges += s.bridges;
            releases += s.releases;
            unmapped += s.unmapped;
            dropped_notes += s.dropped_notes;
            failed_noteons += s.failed_noteons;
            return *this;
        }
    };
    Stats stats() const;
    Stats cue_stats() const;
    struct SynthStats {
        uint64_t prepared, inline_created;
        size_t ready, target;
    };
    SynthStats synth_stats() const;
    int active_notes() const;
    // Optional deterministic diagnostics; gameplay leaves this empty.
    struct NoteEvent {
        uint64_t frame;
        int song, voice, key, velocity; // velocity 0 is note-off
        int subframe = 0;               // Tenths of a driver frame, matching the offline MIDI clock.
    };
    struct DrumEvent {
        uint64_t frame, sample;
        double tick;
        int song, channel, program, key, velocity;
    };
    using DrumSink = std::function<void(const DrumEvent &)>;
    void set_drum_sink(DrumSink sink);
    using EventSink = std::function<void(const NoteEvent &)>;
    void set_event_sink(EventSink sink);
    struct ControlEvent {
        uint64_t frame;
        int song, voice, controller, value;
    };
    using ControlSink = std::function<void(const ControlEvent &)>;
    void set_control_sink(ControlSink sink);
    struct AmplitudeEvent {
        uint64_t frame;
        int song, voice;
        double first, last; // Actual PCM multipliers at the first/last sample of this frame.
    };
    using AmplitudeSink = std::function<void(const AmplitudeEvent &)>;
    void set_amplitude_sink(AmplitudeSink sink);

  private:
    struct Impl;
    struct Pool;
    std::unique_ptr<Impl> impl_;
    std::unique_ptr<EnhancedRenderer> cue_;
    bool with_cue_ = false;
    Stats retired_cue_stats_;
    std::vector<EnhancedFrame> cue_future_;
    EnhancedRenderer(std::shared_ptr<Pool> pool, bool with_cue);
    void mix_stream(EnhancedFrame &frame, std::span<const EnhancedFrame> future);
};

// One worker owns the synth. SPSC queues never block the game thread; only immutable snapshots cross threads.
// All music shares a fixed cursor; ordinary effects bypass the music lookahead.
class EnhancedAudio {
  public:
    static constexpr int kLookahead = 8; // 106 ms: the locked songs' longest two-tick articulation gap.
    explicit EnhancedAudio(const std::string &soundfont);
    ~EnhancedAudio();
    bool available() const;
    bool initialized() const;
    bool submit(EnhancedFrame frame);
    std::vector<int16_t> take();
    // Startup waits for initialization before opening audio; per-packet waits are offline only.
    void wait_initialized();
    void wait_processed(uint64_t count);
    struct Playback {
        uint64_t received, emitted, queued;
    };
    Playback playback() const;
    // Install before submitting frames. One event covers every stereo sample of the output packet.
    struct PlaybackEvent {
        uint64_t sequence, enhanced_sequence;
        size_t samples;
        bool enhanced_rendered;
    };
    using PlaybackSink = std::function<void(const PlaybackEvent &)>;
    void set_playback_sink(PlaybackSink sink);
    // Install before submitting any frames. Live playback hands PCM directly to the output adapter,
    // so a main-thread draw/present cannot strand an already rendered packet in the worker queue.
    using OutputSink = std::function<void(std::vector<int16_t>)>;
    void set_output_sink(OutputSink sink);
    void finish();
    double cost_ms() const;
    uint64_t dropped() const;
    struct Cost {
        uint64_t frames, total_ns, max_ns;
    };
    Cost cost() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace sawgod::app
