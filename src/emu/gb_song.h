// Runs a Game Boy game's music driver headless, to render its songs: the cartridge ROM (MBC1 banking),
// work RAM and HRAM, our SM83 core (emu/gb_cpu.h) and sound chip (emu/gb_apu.h). Nothing else of the
// Game Boy exists here: no video, no timer, no interrupts -- the host calls the driver's per-frame routine
// itself, as the game's VBlank handler does, at the Game Boy's frame rate (70224 T-cycles, 59.73 Hz).
//
// Used for the audio swap (GB music from the player's own GB ROM, rendered once and cached): the
// spike tool (src/tools/audio_spike.cpp) renders Makai Toushi SaGa's songs with it.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "emu/gb_apu.h"
#include "emu/gb_cpu.h"

namespace sawgod::emu {

// Where a game's sound driver lives and how a song is asked for.
struct GbSoundDriver {
    uint8_t bank = 0;          // ROM bank switched into 4000-7FFF while the driver runs
    uint16_t init = 0;         // reset routine (called once)
    uint16_t play = 0;         // per-frame routine (called from VBlank in the game)
    uint16_t song_request = 0; // the address a song number is written to
    // The driver's sequencer runs in ticks, a tempo's worth per frame (not one per frame): tick_pc is an
    // address reached once after every tick, tempo the byte holding ticks-per-frame x tick_divisor.
    uint16_t tick_pc = 0;
    uint16_t tempo = 0;
    uint16_t tempo_accumulator = 0;
    int tick_divisor = 1;
    // Optional independently clocked services; zero means an unsplit driver.
    uint16_t effect_call = 0, effect_service = 0, jingle_call = 0, jingle_service = 0;
    uint16_t effect_request = 0, pulse_owner = 0, noise_owner = 0;
    uint16_t music_write_gate = 0, pulse_cache = 0, empty_effect = 0;
    // The driver's own state, compared tick by tick to find where a song loops (address ranges, end
    // exclusive). Scratch bytes rewritten before every use, and the tempo accumulator (sub-tick phase,
    // which follows the frames, not the music), are left out.
    std::vector<std::pair<uint16_t, uint16_t>> state;
};

// Makai Toushi SaGa (all GB releases share the layout): bank 7, ResetAudio 7:4000, UpdateAudio 7:4003,
// song number in hFFE0 (the driver starts it on its next sequence tick when it differs from hFFE1).
GbSoundDriver saga1_sound_driver();

class GbSoundHost : public GbBus {
  public:
    static constexpr int kFrameCycles = 70224;
    static constexpr double kFrameRate = double(GbApu::kClock) / kFrameCycles; // 59.7275 Hz

    GbSoundHost(std::vector<uint8_t> rom, GbSoundDriver driver);

    uint8_t read(uint16_t addr) override;
    void write(uint16_t addr, uint8_t value) override;

    // Resets the sound chip and calls the driver's init. False if it did not return.
    bool init();
    // Writes the song number where the driver looks for it.
    void request(uint8_t song) { write(driver_.song_request, song); }
    // One Game Boy frame: the driver's per-frame routine, then the rest of the frame on the sound chip.
    bool frame();
    // A frame on someone else's clock (the audio swap's tick lock): the per-frame routine with the
    // sequencer ticking `ticks` times (whatever the song's tempo says), then `cycles` T-cycles in all on the
    // sound chip (fractions carried to the next frame). The song's own tempo byte is kept as it set it.
    bool frame_locked(int ticks, double cycles);
    // Borrowed music ticks, ordinary effects at the native GB frame rate. Services and
    // ownership changes are timestamped inside the output frame, independent of tick count.
    bool frame_dual(int ticks, double cycles);
    void effect(uint8_t id) { write(driver_.effect_request, id); }
    void gains(float music, float effects, uint8_t music_mutes = 0,
               std::array<float, 3> music_levels = {1, 1, 1});
    uint8_t effect_owners() const;
    uint64_t effect_frames() const { return effect_frames_; }
    // Runs the driver's jingle save/restore, and advances suspended music without audible writes.
    bool begin_jingle(uint8_t song);
    bool end_jingle();
    bool saved_music_ticks(int ticks);
    void stop_music();     // cancel music/jingle; keep the ordinary effect clock and scripts
    void cancel_effects(); // driver's empty-script release and music-register restoration
    // Repeat the sequencer state after start_ticks every length_ticks, on a borrowed clock. This
    // aligns arrangements whose voices have different loop lengths without resetting the sound chip.
    void set_tick_loop(int start_ticks, int length_ticks);
    // The driver's state bytes (GbSoundDriver::state), to compare frames.
    std::vector<uint8_t> driver_state() const;

    GbCpu cpu{*this};
    GbApu apu;
    // Each channel's NRx4 trigger writes so far (note starts, for note logs).
    uint32_t triggers[4] = {};

  private:
    std::vector<uint8_t> rom_;
    GbSoundDriver driver_;
    std::vector<uint8_t> wram_ = std::vector<uint8_t>(0x2000); // C000-DFFF (and E000-FDFF echo)
    std::vector<uint8_t> xram_ = std::vector<uint8_t>(0x2000); // A000-BFFF cartridge RAM
    std::vector<uint8_t> vram_ = std::vector<uint8_t>(0x2000); // 8000-9FFF (unused by drivers; kept)
    uint8_t hram_[0x80] = {};                                  // FF80-FFFF
    uint8_t io_[0x80] = {};                                    // FF00-FF7F (sound regs go to the APU)
    int bank_ = 1;
    uint64_t apu_cycles_ = 0; // the CPU cycle count the sound chip has been run up to
    double cycle_carry_ = 0;  // frame_locked's fraction of a T-cycle
    int song_tempo_ = -1;     // frame_locked: the tempo byte as the song last set it

    struct ApuWrite {
        uint64_t cycle;
        uint16_t addr;
        uint8_t value;
        std::array<float, 4> gains;
        uint8_t effect_channels;
    };
    std::vector<ApuWrite> *locked_writes_ = nullptr;
    int loop_start_ = 0, loop_length_ = 0, locked_ticks_ = 0;
    std::vector<uint8_t> loop_state_;
    struct Loop {
        int start, length, ticks;
        std::vector<uint8_t> state;
    };
    Loop saved_loop_{};
    std::array<uint8_t, 16> saved_wave_{};
    double effect_carry_ = 0;
    uint64_t effect_frames_ = 0;
    bool split_services_ = false, preserve_pulse_ = false;
    uint8_t live_pulse_ = 0, music_mutes_ = 0;
    float music_gain_ = 1, effect_gain_ = 1;
    std::array<float, 3> music_levels_{1, 1, 1};
    void update_gains();
    bool timed_call(uint16_t pc, int cycles);
    void loop_tick();
    void sync_apu();
};

// A song rendered from a driver: samples (48 kHz stereo int16) and where it loops, found from the
// driver's state repeating at a sequencer tick.
struct GbSongRender {
    std::vector<int16_t> samples;   // interleaved stereo
    int intro_ticks = -1;           // the loop starts at this tick (-1: no loop found)
    int loop_ticks = 0;             // ticks per loop
    int tempo = 0;                  // the tempo byte at the loop start
    int loop_start_frame = -1;      // the frame the loop's first tick falls in
    double loop_frames = 0;         // loop length in frames: loop_ticks x divisor / tempo (not whole)
    int64_t loop_start_sample = -1; // stereo-sample index of loop_start_frame
    int64_t loop_length_samples = 0;
    // Per frame, per channel: frequency register, envelope volume, triggered this frame (for note logs);
    // and per frame the number of ticks run so far.
    struct ChannelFrame {
        uint16_t freq;
        uint8_t volume;
        bool on, trigger;
    };
    std::vector<std::array<ChannelFrame, 4>> frames;
    std::vector<int> ticks_at_frame_end;
};

// Renders `song`: init, request, then frames until the driver state repeats at a tick (searching up to
// max_frames), then continues to the end of the frame that holds the intro plus `loops` whole loops.
// Without a loop it renders max_frames.
GbSongRender render_gb_song(const std::vector<uint8_t> &rom, const GbSoundDriver &driver, uint8_t song,
                            int loops = 2, int max_frames = 60 * 600);

// Renders `song` tick-locked to another clock: frame i lasts frame_cycles T-cycles and runs schedule[i]
// sequencer ticks (e.g. the WSC driver's frames at 75.47 Hz and its tempo's ticks). Loop fields as above
// (loop_frames in the schedule's frames: measured, so whole).
GbSongRender render_gb_song_locked(const std::vector<uint8_t> &rom, const GbSoundDriver &driver, uint8_t song,
                                   const std::vector<uint8_t> &schedule, double frame_cycles);

} // namespace sawgod::emu
