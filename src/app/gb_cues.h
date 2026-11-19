// WSC-only event music, translated at load time to ORIJINARU's GB song bytecode.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sawgod::app {

struct GbCueNote {
    int pitch = -1; // MIDI; -1 is a rest
    int ticks = 0;
    uint8_t instrument = 0, volume = 0;
};
struct GbCueVoice {
    std::vector<GbCueNote> notes;
    int loop_note = -1;
};
struct GbCue {
    uint8_t song = 0;
    std::array<GbCueVoice, 3> voices; // pulse 1, pulse 2, wave
};

// Reads only these three cues' bounded WSC tracks. Unsupported commands fail closed.
std::optional<GbCue> gb_read_cue(std::span<const uint8_t> wsc, uint8_t song);
// Adds bank 8, a private copy of bank 7's audio code/data with an expanded song table.
// Original banks remain intact. No cartridge file is written. Songs 18-20 are requests 135-137.
bool gb_append_cues(std::vector<uint8_t> &gb, std::span<const uint8_t> wsc);
// The rest (ticks) cut from the end of each note of a cue's GB voice (its articulation; 0 = as the WSC).
int gb_cue_gap(uint8_t song, int channel);

struct GbEffectSource {
    GbCueVoice voice;
    int skip = 0; // WSC tempo: this many skipped ticks per 16 frames
};
std::optional<GbEffectSource> gb_read_effect(std::span<const uint8_t> wsc, uint8_t effect);
// New ordinary effects 37-41: move, confirm, cancel, invalid, generic death.
// Copies/extends only the private driver bank; original 36 scripts stay intact.
bool gb_append_effects(std::vector<uint8_t> &gb, std::span<const uint8_t> wsc);

} // namespace sawgod::app
