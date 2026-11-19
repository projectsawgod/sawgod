// Resolves item, monster, and keyed game text for host UI rows.
#pragma once
#include "emu/wsc.h"
#include "game/text_keys.h"
#include <string>

namespace sawgod::game {
struct ItemRow {
    std::u32string name, count, stats, flavor;
    int id = -1; // empty and non-item rows reserve the same blank icon cell
};
ItemRow item_row(emu::Wsc &w, uint8_t id, uint8_t uses, bool key = false);
// Extracted battle attack kind / range / target, never flavor. Mapping: docs/DATA_TABLES.md.
std::u32string battle_item_details(emu::Wsc &w, uint8_t id);
ItemRow inventory_row(emu::Wsc &w, int tab, int slot);
// Names as the loaded language gives them (its Unicode run, else the record through the charset), trimmed.
std::u32string item_name(emu::Wsc &w, uint8_t id);
std::u32string monster_name(emu::Wsc &w, uint8_t id);
// A game string by key (0xFF-terminated text, not a battle-popup glyph list) for host layouts: the loaded
// language's characters, else the original bytes through the charset; trailing spaces trimmed. Reading it
// leaves the battle-popup message recorder alone (text() notes every key it resolves).
std::u32string key_text(emu::Wsc &w, TextKey key);
} // namespace sawgod::game
