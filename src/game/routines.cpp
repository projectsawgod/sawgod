// The registry of decompiled routines and their installation into the interpreter.
#include <cstring>
#include <iterator>
#include <memory>
#include <span>
#include <vector>

#include "game/bank34.h"
#include "game/bank35_field.h"
#include "game/bank3d_sound.h"
#include "game/bank3f_orijinaru.h"
#include "game/machine.h"
#include "game/session.h"

namespace sawgod::game {

namespace {
// Cycles charged for a native routine while the rest of the game is interpreted (nominal: native code
// makes the game's timing differ from the original's anyway; native equivalence compares interpreter and
// compiled routines).
constexpr int kNativeCycles = 16;
// main (0x340EE0): where the game starts and restarts (Session generations)
constexpr uint32_t kMainEntry = 0x340EE0;

const Routine kRoutines[] = {
    {"guard_reserved_sound_channel", 0x3DCE0D, guard_reserved_sound_channel, true, true},
    {"wait_frames", 0x340000, wait_frames, true, false, true}, // yields: calls wait_vblank
    {"wait_vblank", 0x340009, wait_vblank, true, false, true}, // yields: parks instead of spinning
    {"wait_visible_scanline", 0x340051, wait_visible_scanline, true, false, true}, // yields: parks on a line
    {"read_keypad", 0x34005C, read_keypad, true},
    {"random_byte", 0x3401BD, random_byte, true},
    {"random_range", 0x3401D7, random_range, true},
    {"copy_tiles", 0x34028E, copy_tiles, true},
    {"clear_tiles", 0x3402A3, clear_tiles, true},
    {"fill_screen_rect", 0x3402BC, fill_screen_rect, true},
    {"draw_tilemap", 0x34032D, draw_tilemap, true},
    {"draw_tilemap_offset", 0x340369, draw_tilemap_offset, true},
    {"draw_window_alt", 0x34047F, draw_window_alt, true},
    {"draw_window", 0x340503, draw_window, true},
    {"copy_palettes", 0x340582, copy_palettes, true},
    {"fill_palette", 0x34059F, fill_palette, true},
    // brighten_palette_step (0x3405C5) is implemented in bank34.cpp but not registered here: none of the
    // movies call it, so it can't be difftested. Add it once a movie exercises it.
    {"darken_palette_step", 0x340607, darken_palette_step, true},
    {"palette_step_up_toward", 0x34063D, palette_step_up_toward, true},
    {"palette_step_toward", 0x3406EF, palette_step_toward, true},
    {"dma_sprite_table", 0x34075B, dma_sprite_table, true},
    {"dma_palettes", 0x34078A, dma_palettes, true},
    {"set_sprite_16x16", 0x340839, set_sprite_16x16, true},
    {"draw_text", 0x34091D, draw_text, true},
    {"number_to_digits", 0x340B73, number_to_digits, true},
    {"clear_game_state", 0x34106D, clear_game_state, true, true},
    {"char_type_to_gfx_index", 0x34129A, char_type_to_gfx_index, true},
    {"lookup_name_record", 0x3412F6, lookup_name_record, true},
    {"append_word_queue", 0x34CA20, append_word_queue, true},
    {"draw_status_icons", 0x34CB44, draw_status_icons, true},

    // bank34_engine.cpp (character creation / play-time cluster, all near)
    {"draw_race_select_cursor", 0x341A80, draw_race_select_cursor, true, true},
    {"draw_play_time", 0x341F5A, draw_play_time, true, true},
    {"draw_text_n_white", 0x347D06, draw_text_n_white, true, true},
    {"copy_bytes_terminated", 0x347F06, copy_bytes_terminated, true, true},
    {"format_stat_2digit", 0x347FEE, format_stat_2digit, true, true},
    {"format_seconds_2digit", 0x348025, format_seconds_2digit, true, true},
    {"play_time_to_minutes_seconds", 0x3480CB, play_time_to_minutes_seconds, true, true},
    {"frames_to_minutes_seconds", 0x3480FC, frames_to_minutes_seconds, true, true},
    {"draw_class_select_cursor", 0x3461C1, draw_class_select_cursor, true, true},
    {"draw_gender_select_cursor", 0x3461E0, draw_gender_select_cursor, true, true},
    {"draw_race_icon_cursor", 0x34AEB6, draw_race_icon_cursor, true, true},
    {"draw_class_gender_icon_cursor", 0x34B566, draw_class_gender_icon_cursor, true, true},

    // bank34_engine.cpp (character-creation cursors, field menu, BCD/race formatters, save slot
    // verification, event flags, sound trigger; all near)
    {"draw_creation_step_cursor", 0x34453B, draw_creation_step_cursor, true, true},
    {"draw_class_menu_cursor", 0x344F15, draw_class_menu_cursor, true, true},
    {"draw_class_or_gender_cursor", 0x3428E4, draw_class_or_gender_cursor, true, true},
    {"draw_field_menu_cursor", 0x347204, draw_field_menu_cursor, true, true},
    {"draw_field_menu_label", 0x347261, draw_field_menu_label, true, true},
    {"draw_class_review_cursor", 0x3454F0, draw_class_review_cursor, true, true},
    {"format_stat_1digit", 0x347F84, format_stat_1digit, true, true},
    {"write_race_abbreviation", 0x347F4D, write_race_abbreviation, true, true},
    {"decimal_to_bcd_clamped", 0x347EBE, decimal_to_bcd_clamped, true, true},
    {"draw_bcd_number", 0x347D78, draw_bcd_number, true, true},
    {"copy_and_verify_save_block", 0x3469E1, copy_and_verify_save_block, false, true},
    {"load_save_slot", 0x3469A4, load_save_slot, false, true},
    {"test_event_flag", 0x345F29, test_event_flag, false, true},
    {"draw_flag_gated_icon", 0x345DB0, draw_flag_gated_icon, true, true},
    // trigger_sound_effect (0x348518): now that play_sound_effect (bank 0x3D) is a native routine too, this
    // calls its core directly instead of the interpreter-hand-back tail jump the earlier comment here
    // described (see bank34_engine.cpp).
    {"trigger_sound_effect", 0x348518, trigger_sound_effect, true, true},

    // bank35_field.cpp (0x350000-0x35FFFF, field: map, player, NPCs)
    {"advance_player_animation_timers", 0x350163, advance_player_animation_timers, true},
    {"draw_player_sprite", 0x3501C4, draw_player_sprite, true},
    {"bitstream_read_bits", 0x351801, bitstream_read_bits, true, true},
    {"bitstream_read_bit", 0x35180E, bitstream_read_bit, true, true},
    {"map_cell_id_at", 0x351A18, map_cell_id_at, true},
    {"map_tile_flags_word_at", 0x351A41, map_tile_flags_word_at, true},
    {"map_tile_flags_byte_at", 0x351A71, map_tile_flags_byte_at, true},
    {"npc_ai_trigger_move", 0x35285A, npc_ai_trigger_move, true},
    {"advance_and_draw_npcs", 0x35297E, advance_and_draw_npcs, true},
    // bank3d_sound.cpp (0x3D0000-0x3DFFFF, sound driver)
    {"sound_request_end", 0x3DC5EA, sound_request_end, true},
    {"sound_request_begin", 0x3DC5F5, sound_request_begin, true},
    {"sound_init_fade_state", 0x3DC5FE, sound_init_fade_state, true},
    {"sound_init", 0x3DC60C, sound_init, true},
    {"sound_reserve_channels_4", 0x3DC682, sound_reserve_channels_4, true},
    {"sound_reserve_channels_3", 0x3DC69F, sound_reserve_channels_3, true},
    {"sound_reserve_channels_2", 0x3DC6BA, sound_reserve_channels_2, true},
    {"sound_reserve_channels_1", 0x3DC6D3, sound_reserve_channels_1, true},
    {"sound_reset_pending_duty_overrides", 0x3DC400, sound_reset_pending_duty_overrides, true, true},
    {"sound_mark_note_ended", 0x3DC41A, sound_mark_note_ended, true},
    {"sound_apply_pending_duty_overrides", 0x3DC42F, sound_apply_pending_duty_overrides, true},
    {"sound_update_channel_volume", 0x3DC466, sound_update_channel_volume, true},
    {"sound_update_channel_vibrato", 0x3DC5AC, sound_update_channel_vibrato, true},
    {"sound_compute_tied_note_volume", 0x3DC55A, sound_compute_tied_note_volume, true},
    {"sound_decode_track_commands", 0x3DCA75, sound_decode_track_commands, true, true},
    {"sound_silence_channel", 0x3DCDE3, sound_silence_channel, true, true},
    {"sound_deactivate_channel", 0x3DCE19, sound_deactivate_channel, true, true},
    {"sound_update_noise_gate", 0x3DCE3C, sound_update_noise_gate, true, true},
    {"sound_reset_driver_state", 0x3DCE73, sound_reset_driver_state, true, true},
    {"sound_clear_channel_slot", 0x3DCEF4, sound_clear_channel_slot, true, true},
    {"sound_advance_shared_portamento", 0x3DCCFE, sound_advance_shared_portamento, true},
    {"sound_tick", 0x3DC825, sound_tick, true},
    {"sound_mute_output", 0x3DD050, sound_mute_output, true},
    {"sound_unmute_output", 0x3DD05A, sound_unmute_output, true},
    {"sound_channel_reset", 0x3DD0DB, sound_channel_reset, true},
    {"sound_request", 0x3DD36F, sound_request, true},
    {"play_sound_effect", 0x3DD434, play_sound_effect, true},
    {"sound_dispatch_record", 0x3DD45F, sound_dispatch_record, true, true},

    // bank3f_orijinaru.cpp (ORIJINARU engine): archived and not registered (docs/ORIJINARU.md); its 12
    // routines passed the difftest over the ORIJINARU movies when archived (git tag orijinaru-archive).
    // orij_wait_vblank (0x3F0E35) not registered: spins on an interrupt-set flag, same as bank34's
    // wait_vblank (can't be sandboxed/run natively).
    // L_3F1130 not implemented: the opcode dispatcher orij_read_script_byte feeds -- effectively the whole
    // ORIJINARU script/text bytecode interpreter; it is not decompiled.
};
} // namespace

// All registered routines: this file's list plus the per-bank lists kept in their own files
// (routines_bankXX.cpp, so several people/agents can add routines without editing the same file).
const Routine *routines(size_t *count) {
    static const std::vector<Routine> all = [] {
        std::vector<Routine> v(std::begin(kRoutines), std::end(kRoutines));
        for (auto bank : {routines_bank34, routines_bank35, routines_bank36, routines_bank36x,
                          routines_bank37, routines_bank3c, routines_system}) {
            size_t n = 0;
            const Routine *r = bank(&n);
            v.insert(v.end(), r, r + n);
        }
        return v;
    }();
    *count = all.size();
    return all.data();
}

void reset_natives(emu::Wsc &w, const char *only) {
    w.host_paused = false;
    // after a machine reset: a new generation (the natives suspended in call-outs are cancelled, their stacks
    // unwound) and a reinstall, which drops any per-run native state. Hook subscriptions stay.
    Session::of(w).new_generation();
    w.clear_natives();
    install(w, only);
}

std::unique_ptr<Fiber> start_yielding(emu::Wsc &w, void (*fn)(Machine &)) {
    return std::make_unique<Fiber>([&w, fn] {
        Machine m(w);
        fn(m);
    });
}

void install(emu::Wsc &w, const char *only) {
    // The scheduler state (suspended fibers, generation, hooks) belongs to this machine's Session. A second
    // Wsc (tests, a new game) has its own and never resumes a fiber referencing the old one's registers.
    Session *s = &Session::of(w);
    s->new_generation();
    size_t count = 0;
    const Routine *list = routines(&count);
    for (const Routine &r : std::span<const Routine>(list, count)) {
        if (only && std::strcmp(only, r.name) != 0)
            continue;
        void (*fn)(Machine &) = r.fn;
        if (r.rom == 0x34005C) {
            // Keep read_keypad's proven scan body synchronous for isolated differential tests. Only
            // the installed scheduler paces repeated polls; ordinary once-per-frame calls scan now.
            w.set_native(r.rom, [fn, s](emu::Wsc &wsc) {
                auto f = std::make_unique<Fiber>([&wsc, fn] {
                    Machine m(wsc);
                    while (!wsc.begin_keypad_scan())
                        m.wait_next_line();
                    fn(m);
                });
                return s->continue_fiber(std::move(f), kNativeCycles);
            });
            continue;
        }
        if (r.rom == kMainEntry) {
            // main: every (re)start of the game enters here, by a jump that abandons the previous run's
            // native callers (Session). Runs on the host stack: the jumping native has already returned.
            w.set_native(r.rom, [fn, s](emu::Wsc &wsc) {
                s->new_generation();
                return s->continue_fiber(start_yielding(wsc, fn), kNativeCycles);
            });
            continue;
        }
        if (r.yields) {
            w.set_native(r.rom, [fn, s](emu::Wsc &wsc) {
                return s->continue_fiber(start_yielding(wsc, fn), kNativeCycles);
            });
            continue;
        }
        w.set_native(r.rom, [fn](emu::Wsc &wsc) {
            Machine m(wsc);
            fn(m);
            return kNativeCycles;
        });
    }
    // The call-out trampolines (Machine::kTrampolineIp in each bank's segment): resume the innermost
    // suspended native once its callee has returned there with the stack back where it was.
    for (uint32_t bank = 0x34; bank <= 0x3F; ++bank)
        w.set_native((bank << 16) | Machine::kTrampolineIp, [s](emu::Wsc &wsc) {
            std::unique_ptr<Fiber> f = s->take_returned(wsc.cpu.s.seg[emu::CS], wsc.cpu.s.r[emu::SP]);
            if (!f)
                return -1; // not ours: interpret
            return s->continue_fiber(std::move(f), kNativeCycles);
        });
}

} // namespace sawgod::game
