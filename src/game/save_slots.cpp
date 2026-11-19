// Manages manual save slots, autosave, and native save-menu integration.
#include "game/save_slots.h"

#include <algorithm>
#include <cstring>

#include "game/machine.h"
#include "game/session.h"

namespace sawgod::game {

namespace {
// The two "magic" bytes copy_and_verify_save_block checks, at these payload offsets (written by
// write_save_slot before the copy).
constexpr size_t kMagic1At = 0x435, kMagic2At = 0x436;
constexpr uint8_t kMagic1 = 0x1B, kMagic2 = 0xE4;

uint16_t checksum(const uint8_t *p, size_t n) {
    uint16_t sum = 0;
    for (size_t i = 0; i < n; ++i)
        sum = uint16_t(sum + p[i]);
    return sum;
}

bool verifies(const std::vector<uint8_t> &block) {
    if (block.size() != SaveSlots::kBlock)
        return false;
    const uint16_t stored = uint16_t(block[SaveSlots::kPayload] | block[SaveSlots::kPayload + 1] << 8);
    return checksum(block.data(), SaveSlots::kPayload) == stored && block[kMagic1At] == kMagic1 &&
           block[kMagic2At] == kMagic2;
}
} // namespace

void SaveSlots::apply() {
    const bool listed = store_ && auto_requested_; // off: hidden, kept (as 9 -> 3 does)
    if (requested_ != count_ || listed != auto_) {
        count_ = requested_;
        auto_ = listed;
        top_ = 0;
    }
}

bool SaveSlots::move(uint8_t &row, bool down) {
    if (down) {
        if (row + 1 < kVisibleRows) {
            ++row;
            return false;
        }
        if (top_ + kVisibleRows < entries()) {
            ++top_;
            return true;
        }
        return false; // last slot: stays
    }
    if (row > 0) {
        --row;
        return false;
    }
    if (top_ > 0) {
        --top_;
        return true;
    }
    return false; // first slot: stays
}

uint8_t SaveSlots::show(int slot) {
    int entry = auto_ ? (slot == kAuto ? 0 : slot + 1) : slot;
    entry = std::clamp(entry, 0, entries() - 1);
    if (!extended())
        return uint8_t(entry);
    if (entry < top_)
        top_ = entry;
    else if (entry >= top_ + kVisibleRows)
        top_ = entry - kVisibleRows + 1;
    return uint8_t(entry - top_);
}

bool SaveSlots::has(int slot) const { return holds(slot) && !blocks_[size_t(slot)].empty(); }

bool SaveSlots::load(int slot, std::vector<uint8_t> *block) const {
    if (!has(slot))
        return false;
    const std::vector<uint8_t> &b = blocks_[size_t(slot)];
    if (!verifies(b))
        return false;
    *block = b;
    return true;
}

std::vector<uint8_t> SaveSlots::payload(int slot) const {
    if (!has(slot))
        return {};
    const std::vector<uint8_t> &b = blocks_[size_t(slot)];
    return std::vector<uint8_t>(b.begin(), b.begin() + long(kPayload));
}

void SaveSlots::load_payload(int slot, const std::vector<uint8_t> &payload, const PartyNameList &names) {
    if (!holds(slot) || payload.size() != kPayload)
        return;
    std::vector<uint8_t> b = payload;
    b[kMagic1At] = kMagic1;
    b[kMagic2At] = kMagic2;
    const uint16_t sum = checksum(b.data(), kPayload);
    b.push_back(uint8_t(sum));
    b.push_back(uint8_t(sum >> 8));
    blocks_[size_t(slot)] = std::move(b);
    names_[size_t(slot)] = names;
}

void SaveSlots::store(int slot, const std::vector<uint8_t> &payload, const PartyNameList &names) {
    if (!holds(slot) || payload.size() != kPayload)
        return;
    load_payload(slot, payload, names);
    if (!((locked_ >> slot) & 1))
        dirty_slots_ |= uint16_t(1u << slot);
}

void SaveSlots::set_last(int slot) {
    const int l = holds(slot) ? slot : -1;
    if (l != last_) {
        last_ = l;
        last_dirty_ = true;
    }
}

SaveSlots *extended_save_slots(Machine &m) {
    Session *s = Session::find(m.wsc());
    return s && s->save_slots().extended() ? &s->save_slots() : nullptr;
}

SaveSlots *save_store(Machine &m) {
    Session *s = Session::find(m.wsc());
    return s && (s->save_slots().extended() || s->save_slots().store_attached()) ? &s->save_slots() : nullptr;
}

uint8_t save_slot_for_row(Machine &m, uint8_t row) {
    const SaveSlots *s = extended_save_slots(m);
    return s ? uint8_t(s->slot_for_row(row)) : row;
}

void begin_save_list(Machine &m) {
    if (Session *s = Session::find(m.wsc()))
        s->save_slots().apply();
}

bool scroll_save_list(Machine &m, SaveSlots &slots, bool down) {
    constexpr uint16_t kSelection = 0xD842;
    uint8_t row = m.rd8(m.ss(), kSelection);
    const bool scrolled = slots.move(row, down);
    m.wr8(m.ss(), kSelection, row);
    return scrolled;
}

} // namespace sawgod::game
