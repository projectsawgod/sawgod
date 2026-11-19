// Decompiled routines from bank 0x3D (sound driver, segment D000h). See asm/bank_3D.s.
//
// This is the per-frame music/SFX sequencer step, called once per VBlank from sound_tick. It walks 8
// logical "channels" (a struct every 0x32 bytes starting at DS:0x62C, DS = 0 = internal RAM at these call
// sites) mapped onto the WSC's 4 hardware PSG channels, advances each one's compiled note-sequence bytecode
// (opcodes 0x00-0xFF read from the bank's own D000 segment, at a pointer cached in the channel struct),
// and writes the resulting pitch/volume/sweep/noise values out to ports 0x80-0x8F.
#pragma once

#include "game/machine.h"

namespace sawgod::game {

void sound_reset_pending_duty_overrides(Machine &m); // 0x3DC400 (near)
void sound_mark_note_ended(Machine &m);              // 0x3DC41A
void sound_apply_pending_duty_overrides(Machine &m); // 0x3DC42F
void sound_update_channel_volume(Machine &m);        // 0x3DC466
void sound_update_channel_vibrato(Machine &m);       // 0x3DC5AC
void sound_compute_tied_note_volume(Machine &m);     // 0x3DC55A
void sound_decode_track_commands(Machine &m);        // 0x3DCA75 (near)
void sound_advance_shared_portamento(Machine &m);    // 0x3DCCFE
void sound_tick(Machine &m);                         // 0x3DC825

// Request/effect dispatch and channel-reservation plumbing (sound_request/play_sound_effect and what they
// call into). See asm/bank_3D.s and bank3d_sound.cpp for each one's rom comment.
void sound_init_fade_state(Machine &m);    // 0x3DC5FE
void sound_init(Machine &m);               // 0x3DC60C
void sound_request_begin(Machine &m);      // 0x3DC5F5
void sound_request_end(Machine &m);        // 0x3DC5EA
void sound_reserve_channels_4(Machine &m); // 0x3DC682
void sound_reserve_channels_3(Machine &m); // 0x3DC69F
void sound_reserve_channels_2(Machine &m); // 0x3DC6BA
void sound_reserve_channels_1(Machine &m); // 0x3DC6D3
void sound_dispatch_record(Machine &m);    // 0x3DD45F (near)
void sound_reset_driver_state(Machine &m); // 0x3DCE73 (near)
void sound_clear_channel_slot(Machine &m); // 0x3DCEF4 (near)
void sound_silence_channel(Machine &m);    // 0x3DCDE3 (near)
void sound_deactivate_channel(Machine &m); // 0x3DCE19 (near)
void sound_update_noise_gate(Machine &m);  // 0x3DCE3C (near)
void sound_mute_output(Machine &m);        // 0x3DD050
void sound_unmute_output(Machine &m);      // 0x3DD05A
void sound_channel_reset(Machine &m);      // 0x3DD0DB
void sound_request(Machine &m);            // 0x3DD36F
void play_sound_effect(Machine &m);        // 0x3DD434
// The pure side-effect body of play_sound_effect (id = the effect-id argument, its entry AL). Exposed so
// bank34_engine.cpp's trigger_sound_effect (0x348518) can call it directly now that play_sound_effect is a
// native routine too, instead of the interpreter hand-back trick a still-interpreted callee would need.
void play_sound_effect_core(Machine &m, uint8_t id);

} // namespace sawgod::game

namespace sawgod::game {
void guard_reserved_sound_channel(Machine &m);
}

namespace sawgod::game {
void play_sound_effect_entry_core(Machine &m);
void sound_channel_reset_entry_core(Machine &m);
void sound_request_core(Machine &m);
} // namespace sawgod::game
