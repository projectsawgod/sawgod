// game::Session: the native runtime's owner for one machine (the "master loop" level that features and mods
// build on, one level above the game's own main loop).
//
// It owns what lives longer than any one native call:
// - the yielding natives suspended in call-outs (the scheduler's fibers), innermost last;
// - the run's generation: a run is everything since the game's main (0x340EE0) was last entered. The game
//   restarts by jumping to main without returning: power-on, the reset key combination (read_keypad), a
//   party wipeout (run_battle) and the ending all do. On every entry to main the Session cancels the
//   abandoned run's suspended fibers (their C++ stacks unwind; Fiber::cancel) and starts a new generation,
//   so restarting any number of times holds no old stacks. Reset Game (reset_natives) does the same.
// - the hook registry (hooks.h), which outlives generations: an option's subscriptions stay across
//   restarts and resets;
// - storage services: the extension save slots (save_slots.h);
// - resources: the loaded language (text.h) and data-table overrides (tables.h).
//
// The Session is attached to its emu::Wsc (Wsc::game_session) and created by install(); Session::of(wsc)
// returns it. Single-threaded, like Fiber.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "game/clean_scene.h"
#include "game/hooks.h"
#include "game/info.h"
#include "game/machine.h"
#include "game/player_names.h"
#include "game/save_slots.h"
#include "game/tables.h"
#include "game/text.h"

namespace sawgod::game {

class JoinedBattlePopup;

class Session {
  public:
    Session() = default;
    ~Session();
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    // This machine's session, created on first use.
    static Session &of(emu::Wsc &w);
    // This machine's session, or nullptr if it has none (no natives installed).
    static Session *find(emu::Wsc &w);

    std::vector<EnemyHpBox> &enemy_hp_boxes() { return enemy_hp_boxes_; }
    const std::vector<EnemyHpBox> &enemy_hp_boxes() const { return enemy_hp_boxes_; }

    hooks::Registry &hooks() { return hooks_; }
    CleanSceneRequests &scene_requests() { return scene_requests_; }
    void apply_scene_settings(emu::Wsc &w);
    // Inputs that produced the displayed raster, before VBlank advances the field again.
    emu::Wsc &raster_field(emu::Wsc &fallback) const { return raster_field_ ? *raster_field_ : fallback; }
    const emu::Wsc &raster_field(const emu::Wsc &fallback) const {
        return raster_field_ ? *raster_field_ : fallback;
    }
    // Optional presentation provider; the native runtime does not depend on the GB renderer.
    static inline std::map<int, std::array<uint16_t, 4>> (*raster_world_words_hook)(const emu::Wsc &) =
        nullptr;
    const auto &raster_field_words() const { return raster_field_words_; }
    SaveSlots &save_slots() { return save_slots_; }
    // The loaded language's strings (text.h); empty: the original Japanese.
    TextTable &text() { return text_; }
    PlayerNames &player_names() { return player_names_; }
    const TextTable &text() const { return text_; }
    // The data tables a mod overrides (tables.h); empty: the ROM's.
    DataTables &tables() { return tables_; }
    const DataTables &tables() const { return tables_; }

    // Optional host-only decision, after all replacement RNG and before either slot byte is written.
    struct EsperChange {
        int actor = 0, old_ability = 0, new_ability = 0;
        bool yes = true;
    };
    // A party-menu load needs the same fresh field setup as title Continue.
    bool party_load_pending = false;
    bool confirm_esper_changes = false;
    // The host battle UI's meat prompt ("Eat It! / Don't"): B picks Don't. Off, B is ignored as in the
    // original.
    bool meat_b_cancels = false;
    // Presentation-only while the original replacement announcement completes after a declined change.
    bool esper_declined = false;
    const EsperChange *esper_change() const { return esper_pending_ ? &esper_change_ : nullptr; }
    // The prompt ignores input for its first half second (host frames at ~75.47 Hz), so a press carried over
    // from the battle can't answer it unread; its arrow appears when input is accepted.
    static constexpr int kEsperInputHold = 38;
    bool esper_accepting() const { return esper_pending_ && esper_hold_ == 0; }
    bool allow_esper_change(Machine &m, int actor, int old_ability, int new_ability);
    // Host input only, edge triggered. Resolving resumes the suspended native without a CPU dispatch.
    void esper_input(emu::Wsc &w, uint16_t keys);

    // Battle-popup messages (text.h templates): text() notes each key it resolves; append_word_queue reports
    // each part it queues (message_part); open_status_popup, about to show the queue, lets the language's
    // template for the message rebuild it (apply_message_template). No effect without a template.
    void note_text(TextKey key, TextPtr where) {
        last_key_ = int(key);
        last_ptr_ = where;
    }
    void message_part(Machine &m);
    uint16_t apply_message_template(Machine &m);
    // A merged announcement changes only the host presentation. Both original popup passes still
    // execute against their original queues, including drawing, input polls and frame waits.
    bool joining_battle_popup() const { return joined_popup_ != nullptr; }
    bool present_joined_battle_popup(Machine &m);
    const std::u32string *joined_battle_popup_text() const;
    void record_popup_name(Machine &m, uint16_t buffer, NameTable table, uint8_t id);
    void record_popup_words(uint16_t buffer, std::vector<uint16_t> words);

    // Field dialogue (text.h): read_dialogue_frame_byte calls dialogue_frame with the current frame record
    // (SS:si) before it reads -- at the start of a run the language replaces, the frame is pointed at the
    // host text, and at its end back to the ROM script. draw_field_dialogue_token calls dialogue_glyph for a
    // glyph it is about to type: true if it drew it itself (a proportional language's text, text_render.h).
    void dialogue_frame(Machine &m, uint16_t frame);
    bool dialogue_glyph(Machine &m, uint8_t code);
    // Read-only lookahead after the last stream byte, through the current run, with real party names.
    std::u32string dialogue_tail(Machine &m) const;
    // Lookahead from the token awaiting native typing (before a name argument; a clear token is skipped).
    // Only meaningful once a byte of the current dialogue has been read.
    std::u32string dialogue_pending(Machine &m) const;
    // Stream bytes read so far (each dialogue_frame call), to tell whether the cursor belongs to a new box.
    uint32_t dialogue_reads() const { return dialogue_reads_; }
    std::u32string dialogue_name(Machine &m, int member) const;
    char32_t dialogue_character(uint8_t code) const;
    // A dialogue frame is reading the language's text: changing the language now would strand it (the app
    // waits until this is false).
    bool reading_language_dialogue() const { return dialogue_host_; }

    // Bumped at every entry to main and every reset_natives.
    uint64_t generation() const { return generation_; }
    // Yielding natives currently suspended in a call-out.
    size_t suspended() const { return suspended_.size() + (host_fiber_ ? 1 : 0); }
    // Suspended fibers cancelled at generation changes so far (diagnostics, tests).
    uint64_t abandoned() const { return abandoned_; }

    // Start a new generation: cancel every suspended fiber (innermost first). Host stack only (between
    // natives), which is where main's entry and reset_natives run.
    void new_generation();

    // Scheduler (routines.cpp): run a native's fiber until it finishes or suspends in a call-out.
    int continue_fiber(std::unique_ptr<Fiber> f, int cycles);
    // The innermost suspended fiber if the call-out it waits on has returned (CS and SP as recorded), else
    // nullptr.
    std::unique_ptr<Fiber> take_returned(uint16_t cs, uint16_t sp);

  private:
    std::u32string dialogue_text(Machine &m, size_t at) const; // the dialogue lookahead from stream byte at
    std::unique_ptr<emu::Wsc> raster_field_;
    std::map<int, std::array<uint16_t, 4>> raster_field_words_;
    friend class JoinedBattlePopup;
    JoinedBattlePopup *joined_popup_ = nullptr;
    std::vector<uint16_t> popup_name_words(Machine &m, NameTable table, uint8_t id) const;
    struct Suspended {
        std::unique_ptr<Fiber> fiber;
        Machine::Callout at;
    };
    std::vector<EnemyHpBox> enemy_hp_boxes_;
    hooks::Registry hooks_;
    CleanSceneRequests scene_requests_;
    SaveSlots save_slots_;
    PlayerNames player_names_;
    TextTable text_;
    DataTables tables_;
    struct MessagePart {
        int key;                     // a text key, or -1: a RAM buffer, -2: a string not from a key
        uint16_t ram;                // the buffer (DS = 0)
        uint16_t count;              // AX: at most this many words
        std::vector<uint16_t> words; // captured when queued, before scratch buffers are reused
    };
    // Original queue length captured before translated fragments/templates replace it.
    uint16_t message_timing_words_ = 0;
    bool message_battle_ = false;
    std::vector<MessagePart> message_;
    bool dialogue_host_ = false; // the frame being read is host text
    uint16_t dialogue_cursor_ = 0;
    uint16_t dialogue_pending_cursor_ = 0; // the token being typed, before a name's argument byte
    uint16_t dialogue_segment_ = 0;        // the segment of the frame being read
    uint32_t dialogue_reads_ = 0;
    char32_t dialogue_char_ = 0;                   // the character of the host glyph just read (0: none)
    uint16_t line_start_ = 0, line_next_ = 0xFFFF; // the dialogue line being typed (cursor values, 0xDD28)
    std::u32string line_;
    int last_key_ = -1;
    TextPtr last_ptr_{};
    std::vector<Suspended> suspended_;
    uint64_t generation_ = 0;
    uint64_t abandoned_ = 0;
    EsperChange esper_change_;
    bool esper_pending_ = false, esper_answer_ = true;
    uint16_t esper_held_ = 0;
    int esper_hold_ = 0;
    std::unique_ptr<Fiber> host_fiber_;
    emu::Wsc *host_machine_ = nullptr;
};

// Lives on the yielding routine's fiber, so cancellation/restart also restores the outer scope.
class JoinedBattlePopup {
  public:
    JoinedBattlePopup(Machine &m, std::string_view routine);
    ~JoinedBattlePopup();
    JoinedBattlePopup(const JoinedBattlePopup &) = delete;
    JoinedBattlePopup &operator=(const JoinedBattlePopup &) = delete;

  private:
    friend class Session;
    Machine &machine_;
    bool original_text_;
    Session *session_;
    JoinedBattlePopup *outer_ = nullptr;
    std::string routine_;
    std::u32string text_;
    std::vector<std::pair<uint16_t, std::vector<uint16_t>>> names_;
    bool presented_ = false;
};

} // namespace sawgod::game
