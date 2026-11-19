// GB music beside the WSC's (the audio swap): the GB sound driver runs by the same clock as the WSC audio.
// The GB version of a song plays from
// ORIJINARU image in the player's WSC ROM through our own GB core (emu/gb_song.h), live, ticked on the
// WonderSwan driver's frames: each WSC frame runs the GB sequencer ticks that a per-song tick map (GbSongMap)
// says the WSC's ticks so far correspond to. The two versions so stay at the same musical point for good, and
// the GB plays at the WSC's tempo (a few % off a Game Boy; accepted). Nothing of the game changes: the WSC
// driver runs as always; this only reads its state and listens to its song requests
// (hooks::Event::SoundRequest, after the WSC driver reserves its slots).
//
// The front end mixes the GB output into the WSC's samples with the picture's cross-fade weight, while the
// WSC's music channels get the opposite weight (sound_mix.h). Accepted mapped effects fade
// to native-clock GB effects; unmatched effects keep WSC playback and mute the paired GB music voice.
// WSC-only cues 135-137 are transcribed into appended in-memory GB driver data (gb_cues.h).
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "app/sound_mix.h"
#include "game/hooks.h"

namespace sawgod::emu {
class GbSoundHost;
class Wsc;
} // namespace sawgod::emu

namespace sawgod::game {
class Session;
}

namespace sawgod::app {

// How far the GB version of a song is when the WSC version has ticked w times: piecewise linear through
// the anchors (WSC tick, GB tick), then `slope` GB ticks per WSC tick (1, or 1.5 where the WSC re-quantized
// a song to 2 ticks for the GB's 3). Found by aligning the pitches each version plays, tick by tick.
struct GbSongMap {
    uint8_t wsc_song = 0, gb_song = 0;
    std::vector<std::pair<double, double>> anchors;
    double slope = 1;
    int loop_start = 0, loop_ticks = 0, gb_loop_ticks = 0;
    double loop_phase = 0; // preserves which half-tick starts a 3:2 loop
    // Relative to loop_start and gb_ticks_by(loop_start); includes (0,0) and the loop's end.
    std::vector<std::pair<double, double>> loop_anchors;
    std::array<uint8_t, 3> voices = {4, 5, 6}; // GB pulse 1, pulse 2, wave -> WSC logical slot
    uint8_t clock_slot = 4;
    // A repeating map also repeats the GB sequencer state at this point, since its voices may have
    // different natural periods. The oscillators and envelopes keep running across the seam.
    int gb_loop_start() const;
    // The GB ticks due once the WSC has ticked w times (w >= 1: the WSC's first tick starts the song).
    int gb_ticks_by(int wsc_ticks) const;
};

// The map for a WSC song id, or null: no GB version (the GB stays silent, the WSC plays as always).
const GbSongMap *gb_song_map(uint8_t wsc_song);
std::span<const GbSongMap> gb_song_maps();

// GB voices to mute while lower-priority-number WSC effect slots own their hardware channels.
uint8_t gb_music_voice_mutes(const emu::Wsc &wsc, const GbSongMap &map, uint8_t mapped_effect_slots = 0);

// Action identity comes from the game scopes; item sounds come from the supplied GB action byte.
uint8_t gb_effect_for(uint8_t wsc_effect, game::hooks::AudioContext context, uint8_t action_byte,
                      bool private_effects = false);

// Follows the WSC driver's song frame by frame (pure logic, testable without ROMs).
class GbMusicFollower {
  public:
    // The driver was asked for a song (the MusicRequest hook).
    void request(uint8_t wsc_song);
    struct Step {
        bool start = false; // a GB song starts this frame (before running `ticks`)
        bool stop = false;  // the GB song stops
        int ticks = 0;      // GB sequencer ticks to run this frame
    };
    // After each driver frame: the clock slot's step counter and tempo phase (+0x07). If a finite
    // song changes clock voices, previous_phase is that voice's phase before this frame.
    Step frame(uint16_t slot_step, uint8_t slot_phase, int previous_phase = -1);
    const GbSongMap *playing() const { return running_ ? map_ : nullptr; }
    const GbSongMap *selected() const { return map_; }
    bool pending() const { return pending_; }
    int wsc_ticks() const { return wsc_ticks_; }
    uint8_t clock_slot() const { return map_ ? map_->clock_slot : 4; }

  private:
    const GbSongMap *map_ = nullptr;
    bool pending_ = false, running_ = false, stop_pending_ = false;
    int wsc_ticks_ = 0, gb_ticks_ = 0;
    uint8_t phase_ = 0;
};

class GbMusic {
  public:
    GbMusic();
    ~GbMusic();
    // The WSC ROM (the GB game inside it, ORIJINARU) or a GB ROM of Makai Toushi SaGa (any release). False:
    // neither; no GB music then.
    bool load(std::vector<uint8_t> rom);
    bool loaded() const { return !rom_.empty(); }
    // Listens to accepted requests, reset and action scopes; keep alive as long as the session runs.
    void attach(game::Session &session);
    // After each WSC driver frame (a game frame, or a paused one): follow the WSC song, run the GB's frame on
    // the WSC's clock, and add its output x gain to the samples that frame made (interleaved stereo at the
    // WSC's 24 kHz).
    void frame(const emu::Wsc &wsc, std::vector<int16_t> &samples, float gain);
    // A GB version is playing (the front end fades the WSC music only then).
    bool playing() const { return follower_.playing() || (jingle_ && suspended_.playing()); }
    // Transcribed cues in WSC effect slots count as music while their original tracks still own them.
    uint8_t wsc_music_slots(const emu::Wsc &wsc) const;
    // Requests seen without attach() (headless tools that drive the hook themselves).
    void request(uint8_t wsc_song) {
        follower_.request(!cue_bank_ && wsc_song >= 135 && wsc_song <= 137 ? 0 : wsc_song);
    }
    // Full mixer: independent music/effect gains, both scaled by picture weight.
    void frame(const emu::Wsc &wsc, std::vector<int16_t> &samples, float music_gain, float effect_gain,
               std::vector<int16_t> *effects = nullptr);
    void reset();
    uint8_t mapped_wsc_slots(const emu::Wsc &wsc) const;
    const emu::GbSoundHost *sound_host() const { return host_.get(); } // read-only diagnostics

    // The GB output's level against the WSC's (by RMS over the field song's loop).
    static constexpr float kLevel = 0.4f;

  private:
    std::vector<uint8_t> rom_;
    bool cue_bank_ = false;
    SlowMusicFade slow_fade_;
    GbMusicFollower follower_;
    std::unique_ptr<emu::GbSoundHost> host_;
    std::vector<int16_t> effect_fifo_;
    std::vector<int16_t> fifo_; // GB samples made but not yet mixed (the frames' sample counts differ by one)
    std::array<uint8_t, 8> phases_{};
    game::hooks::Subscription closing_fade_sub_;
    game::hooks::Subscription sub_;
    game::hooks::Subscription reset_sub_;
    game::hooks::Subscription context_sub_;
    struct Request {
        uint8_t id, slots;
        bool immediate;
        uint64_t generation;
        std::array<uint16_t, 8> tracks{};
        uint64_t group = 0;
        uint8_t effect = 0;
        bool submit = false;
        int music = -1;
        bool cancel = false;
    };
    std::vector<Request> requests_;
    uint64_t generation_ = 0;
    GbMusicFollower suspended_;
    bool jingle_ = false;
    struct Context {
        game::hooks::AudioContext kind;
        uint8_t item;
        uint64_t group;
        bool submitted = false;
        bool valid_item = true;
    };
    std::vector<Context> contexts_;
    struct Owner {
        uint16_t track = 0;
        uint64_t group = 0;
    };
    std::array<Owner, 8> owners_{};
    uint64_t mapped_group_ = 0;
    std::array<uint8_t, 256> action_sounds_{};
    bool private_effects_ = false;
    void accepted(game::Machine &m, const game::hooks::Args &a);
};

} // namespace sawgod::app
