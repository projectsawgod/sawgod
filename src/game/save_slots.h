// game::SaveSlots: where the game's save slots live, and the "Save Slots: 3 / 9" option. Owned by the
// machine's Session.
//
// Storage. With a save store attached (attach_store; the app always attaches one: game/save_file.h,
// docs/SAVES.md) every slot lives here, in the store's files, and the game never reads or writes a save
// byte of SRAM -- not the slots, their backups or the last-used byte 0x3300. Without one (difftests, key
// fuzz, the interpreter) slots 0-2 are the original SRAM slots, byte for byte, and only slots 3-8 live here
// (in memory, or a test's store directory).
//
// The option. With 3 (the default) the save and load lists are the original. With 9:
// - Slots 3-8 are offered too (shown as 4-9).
// - The lists keep the original window of three rows and scroll, without wrapping. The game's selection
//   byte (0xD842) stays the ROW (0-2), so every original drawing routine places its cursor, sprites and
//   preview exactly as before; `top` is the slot in the first row. slot = top + row.
// Switching 9 -> 3 hides slots 4-9 but keeps them; 3 -> 9 shows them again.
//
// AUTO: slot kAuto, the autosave (game/autosave.h), is the lists' first entry while the
// Autosave option is on (off: hidden but kept, as 9 -> 3 does); the lists then scroll as with 9 (entry 0 is
// AUTO, entry n slot n - 1). Store only (no SRAM slot).
#pragma once

#include "game/player_names.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sawgod::game {

class Machine;

class SaveSlots {
  public:
    static constexpr int kOriginal = 3, kExtended = 9;
    static constexpr int kAuto = 9, kSlots = 10; // AUTO: the autosave's slot (after 0-8)
    static constexpr int kVisibleRows = 3;
    static constexpr size_t kPayload = 0x439;      // game state bytes per slot (as the original)
    static constexpr size_t kBlock = kPayload + 2; // + the 16-bit additive checksum

    // A save store owns every slot (see above). Without one, holds() is true only for slots 3-8.
    void attach_store() { store_ = true; }
    bool store_attached() const { return store_; }
    bool holds(int slot) const {
        return slot == kAuto ? store_ : slot >= (store_ ? 0 : kOriginal) && slot < kExtended;
    }

    int count() const { return count_; }
    // The option's value (3 or 9; anything else means 3). It takes effect at apply(), which the game calls
    // when a save/load list or the title's continue check starts, so a change made while a list is open
    // (overlay over the paused menu) can never change which slot a row means.
    void request_count(int n) { requested_ = n == kExtended ? kExtended : kOriginal; }
    // AUTO listed: the Autosave option (applied at apply(), like the count).
    void request_auto(bool on) { auto_requested_ = on; }
    void apply();
    bool auto_listed() const { return auto_; }
    int entries() const { return count_ + (auto_ ? 1 : 0); } // the lists' rows in all
    bool extended() const { return entries() > kOriginal; }

    // The list view (meaningful only when extended).
    int top() const { return top_; }
    int slot_for_row(int row) const {
        const int entry = extended() ? top_ + row : row;
        return auto_ ? (entry == 0 ? kAuto : entry - 1) : entry;
    }
    // Moves the selection one row; scrolls at the window edges; never wraps. Returns true if the list
    // scrolled (the summaries must be redrawn). `row` is the game's selection byte.
    bool move(uint8_t &row, bool down);
    // Shows `slot` (0-8, kAuto) in the window and returns its row.
    uint8_t show(int slot);

    // Slots it holds. load(): the stored block (payload + checksum) if it verifies (checksum and the two
    // magic bytes, as copy_and_verify_save_block), else false. store(): payload of kPayload bytes; computes
    // the checksum and sets the magic bytes. payload(): the stored payload, empty if none.
    bool load(int slot, std::vector<uint8_t> *block) const;
    void store(int slot, const std::vector<uint8_t> &payload, const PartyNameList &names = {});
    const PartyNameList &names(int slot) const { return names_.at(size_t(slot)); }
    bool has(int slot) const;
    std::vector<uint8_t> payload(int slot) const;

    // Last-used slot it holds, or -1 (without a store: the original byte 0x3300 decides).
    int last() const { return last_; }
    void set_last(int slot);

    // What changed since the last write (the store writes those files and clears them). A slot marked
    // `locked` (its file didn't parse) is never reported, so its file is never overwritten.
    bool dirty() const { return dirty_slots_ != 0 || last_dirty_; }
    bool slot_dirty(int slot) const { return (dirty_slots_ >> slot) & 1; }
    bool last_dirty() const { return last_dirty_; }
    void clear_dirty(int slot) { dirty_slots_ &= uint16_t(~(1u << slot)); }
    void clear_last_dirty() { last_dirty_ = false; }
    void clear_dirty() { dirty_slots_ = 0, last_dirty_ = false; }
    void lock(int slot) { locked_ |= uint16_t(1u << slot); }
    bool locked(int slot) const { return slot >= 0 && slot < kSlots && ((locked_ >> slot) & 1); }
    // Loading from files: sets a slot or the last-used slot without marking anything dirty.
    void load_payload(int slot, const std::vector<uint8_t> &payload, const PartyNameList &names = {});
    void load_last(int slot) { last_ = holds(slot) ? slot : -1; }

  private:
    int count_ = kOriginal, requested_ = kOriginal;
    bool auto_ = false, auto_requested_ = false;
    int top_ = 0;
    int last_ = -1;
    bool store_ = false;
    uint16_t dirty_slots_ = 0, locked_ = 0; // bit per slot
    bool last_dirty_ = false;
    std::array<std::vector<uint8_t>, kSlots> blocks_; // per slot, empty = never saved
    std::array<PartyNameList, kSlots> names_{};
};

// The machine's SaveSlots if the option is on (9), else nullptr: the lists keep the original behavior.
SaveSlots *extended_save_slots(Machine &m);
// The machine's SaveSlots if it holds any slot (a store is attached or the option is on), else nullptr:
// every slot is the original SRAM slot.
SaveSlots *save_store(Machine &m);
// The slot shown in list row `row` (the game's selection byte): row itself unless the option is on.
uint8_t save_slot_for_row(Machine &m, uint8_t row);
// Start of a save/load list or the continue check: apply the requested slot count.
void begin_save_list(Machine &m);
// Up/down in the save or load list: moves the selection byte (SS:0xD842, the row) or scrolls
// (SaveSlots::move). True if the list scrolled.
bool scroll_save_list(Machine &m, SaveSlots &slots, bool down);

} // namespace sawgod::game
