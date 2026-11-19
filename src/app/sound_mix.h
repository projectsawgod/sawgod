// Front-end sound helpers on top of the game's sound driver (src/game/bank3d_sound.*):
//
// - Music vs sound-effect volume. The driver runs 8 logical channels over the WSC's 4 hardware channels.
//   Songs are wired into logical slots 4-7, sound effects into slots 0-3 (the request tables at D000:02E0
//   and D000:0316 and the slot table at D000:0500, bank 0x3D), and sound_tick lets the lowest active slot
//   on a hardware channel drive it, so an effect takes the channel over from the music until it ends.
//   channel_gains() reads that state after each frame and gives each hardware channel the music or the
//   effects gain (Apu::output_gain, playback only).
// - The pause: while the options overlay is open the game doesn't run, but the music does: paused_tick()
//   runs just the driver's per-frame service (sound_tick, as irq_vblank calls it) and one frame of the
//   sound chip. No new sound can start (nothing runs that requests one); an effect already playing runs to
//   its end. CPU registers and the stack bytes the call uses are put back, so the game resumes exactly.
#pragma once

#include <array>
#include <cstdint>

namespace sawgod::emu {
class Wsc;
}

namespace sawgod::app {

// Shared music-volume ramp (sound_channel_reset), observed after driver service. The driver calls
// this "portamento", but +8/+2C are volume indices, not pitches. Mode 2 retains its zero endpoint
// while the sequencer stays active; a new request restores the volume and clears the mode.
struct DriverMusicFade {
    std::array<float, 8> slots{1, 1, 1, 1, 1, 1, 1, 1};
    std::array<uint8_t, 8> reference_volume{};
    bool engaged = false;
    float gain = 1; // Last surviving music voice, also gates Enhanced percussion and release/reverb.
};
// Host envelope: full level until the fade, five seconds of even dB attenuation (the last four frames to
// exact zero), then about one second of silence before the title music. Only the
// closing-scene observation starts it. Requests/reset or leaving mode two discard it.
class SlowMusicFade {
  public:
    static constexpr int kDelay = 216;           // the closing scene lasts 668 frames until the title request
    static constexpr int kFrames = kDelay + 377; // 5 s of fade at the native 159-line frame rate
    static constexpr int kZeroFrames = 4;
    struct Frame {
        float first = 1, last = 1;
        bool slow = false;
    };
    void start() {
        elapsed_ = 0;
        active_ = true;
    }
    Frame frame(uint8_t mode);
    void reset() {
        elapsed_ = 0;
        active_ = false;
    }

  private:
    int elapsed_ = 0;
    bool active_ = false;
    static float gain(int frame);
};
DriverMusicFade driver_music_fade(const sawgod::emu::Wsc &wsc);
// A finite voice's final envelope/rest can become silent before its slot ends. Follow that tail
// only when a straight-line scan proves there are no more notes; loops and unknown commands opt out.
float driver_terminal_music_gain(const sawgod::emu::Wsc &wsc, uint8_t slot);

// Bit h set: hardware channel h is currently driven by a sound-effect slot.
uint8_t sfx_channels(sawgod::emu::Wsc &wsc, uint8_t music_slots = 0);
// Sets wsc.apu.output_gain from current ownership. music_slots marks cue tracks in effect slots.
void apply_channel_gains(sawgod::emu::Wsc &wsc, float music_gain, float sfx_gain, uint8_t music_slots = 0,
                         uint8_t mapped_effect_slots = 0, float gb_weight = 0);
// A concurrent Enhanced cue may replace effect-slot music while the field song falls back to WSC.
void apply_cue_gain(sawgod::emu::Wsc &wsc, uint8_t cue_slots, float music_gain);
// One paused frame: the sound driver's frame service, then one frame (159 lines) of the sound chip.
void paused_tick(sawgod::emu::Wsc &wsc);

} // namespace sawgod::app
