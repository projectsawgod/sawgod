// The save store (the port's own format, not WSC SRAM): every save slot
// is its own readable, versioned text file, one directory per ROM:
//
//   <user data>/saves/slot1.toml ... slot9.toml   one per saved slot (docs/SAVES.md)
//   <user data>/saves/store.toml                  format version, last-used slot
//
// A slot file holds the game's save payload (the 0x439-byte game state the original copies to SRAM) as named
// fields -- party members, inventory, gold, location, flags -- with the few spans nobody has decoded yet as
// hex. Format 2 stores full host names; the machine's four-byte names are a projection for the native menus.
// Other payload fields round-trip exactly. The checksum and the two
// "valid save" marker bytes aren't in the file (they're made when it loads), so a hand-edited file loads
// as long as it parses.
#pragma once

#include "game/player_names.h"
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace sawgod::game {

class SaveSlots;

inline constexpr int kSaveFileFormat = 2;

// The payload (SaveSlots::kPayload bytes) as a slot file, and back. save_from_text fails (with a message
// naming the line or field) on a syntax error, an unknown, missing or out-of-range field, or a different
// format version.
std::string save_to_text(const std::vector<uint8_t> &payload, const PartyNameList *names = nullptr);
bool save_from_text(std::string_view text, std::vector<uint8_t> *payload, std::string *error,
                    PartyNameList *names = nullptr);

// The store directory. read_save_store loads every slot file and store.toml into `slots` (which then owns
// every slot: SaveSlots::attach_store); a slot file that doesn't parse is reported in `warnings` and that
// slot is never written this session (so a hand edit with a typo isn't lost). False only if the directory
// has no store.toml (a new store).
bool read_save_store(const std::string &dir, SaveSlots &slots, std::vector<std::string> *warnings);
// Writes the slots that changed (and store.toml when the last-used slot did), each through `write`
// (atomic, e.g. sawgod::WriteFileAtomic). False and `error` if any write failed; those stay pending.
using SaveFileWriter =
    std::function<bool(const std::string &path, const std::string &data, std::string *error)>;
bool write_save_store(const std::string &dir, SaveSlots &slots, const SaveFileWriter &write,
                      std::string *error);

// Loads a raw WSC SRAM image into the slots (slots 1-3 from their blocks, or the backup copies, as the
// original loads them; the last-used byte 0x3300), for headless runs and tests (--store-import). Returns how
// many slots it held.
int import_wsc_sram(const std::vector<uint8_t> &sram, SaveSlots &slots);

// For tests: true if the slot file's fields cover every payload byte exactly once (all but the two marker
// bytes); else false and the first byte that isn't.
bool save_fields_cover_payload(std::string *error);

} // namespace sawgod::game
