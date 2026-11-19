// Resolves item, monster, and keyed game text for host UI rows.
#include "game/ui_items.h"
#include "game/text_tokens.h"

#include "game/session.h"
#include "game/tables.h"
#include "game/text_render.h"

namespace sawgod::game {
namespace {
constexpr uint16_t kInventory = 0xD07C, kKeyItems = 0xD08C;

void trim(std::u32string &s) {
    while (!s.empty() && (s.back() == U' ' || s.back() == U'　'))
        s.pop_back();
}
std::u32string encoded(Machine &m, uint16_t segment, uint16_t offset, int length) {
    static const auto charset = Charset::system();
    std::u32string s;
    for (int i = 0; i < length; ++i) {
        const auto code = m.rd8(segment, uint16_t(offset + i));
        if (code == 255)
            break;
        s += utf8_to_u32(charset.decode(code));
    }
    trim(s);
    return s;
}
std::u32string table_name(Machine &m, NameTable table, uint8_t id) {
    auto &t = Session::of(m.wsc()).text();
    const int run = t.name_run(int(table), id);
    if (run >= 0)
        if (const auto *s = t.run(uint16_t(run)))
            return *s;
    const auto p = name_record(m, table, id);
    return encoded(m, p.segment, p.offset, table == NameTable::Monster ? 8 : 7);
}
std::u32string item_name(Machine &m, uint8_t id) { return table_name(m, NameTable::Item, id); }
std::u32string flavor(Machine &m, uint8_t id) {
    const uint16_t offset = uint16_t(TextTable::kDescriptions + id * TextTable::kDescriptionSize);
    auto &t = Session::of(m.wsc()).text();
    if (const auto *s = t.record_chars(offset)) {
        auto result = *s;
        trim(result);
        return result;
    }
    return encoded(m, 0x9000, offset, TextTable::kDescriptionSize - 1);
}
} // namespace
std::u32string item_name(emu::Wsc &w, uint8_t id) {
    Machine m(w);
    return item_name(m, id);
}
std::u32string monster_name(emu::Wsc &w, uint8_t id) {
    Machine m(w);
    return table_name(m, NameTable::Monster, id);
}
std::u32string key_text(emu::Wsc &w, TextKey key) {
    const TextKeyInfo &k = kTextKeys[size_t(key)];
    Machine m(w);
    std::u32string s;
    const int host = Session::of(w).text().find(key);
    if (host >= 0) {
        if (const auto *chars = Session::of(w).text().characters(uint16_t(host)))
            s = *chars;
        else
            s = encoded(m, kTextSegment, uint16_t(host), 255);
    } else {
        s = encoded(m, k.segment, uint16_t(k.string - (0x340000 + (uint32_t(k.segment) << 4) - 0x40000)),
                    255);
    }
    trim(s);
    if (host < 0 || !Session::of(w).text().characters(uint16_t(host)))
        s = with_button_tokens(Session::of(w).text(), key, s);
    return with_wsc_glyphs(s);
}
ItemRow inventory_row(emu::Wsc &w, int tab, int slot) {
    ItemRow out;
    if (!tab && slot >= 8) {
        const bool crystal = slot == 8;
        out.name = host_text(w, crystal ? "ui_inventory_crystals" : "ui_inventory_trash");
        out.flavor = host_text(w, crystal ? "ui_inventory_crystal_hint" : "ui_inventory_trash_hint");
        return out;
    }
    const int at = (tab ? kKeyItems : kInventory) + slot * 2;
    const uint8_t id = w.ram[at], uses = w.ram[at + 1];
    return item_row(w, id, uses, tab != 0);
}
ItemRow item_row(emu::Wsc &w, uint8_t id, uint8_t uses, bool key) {
    ItemRow out;
    // Native classify_item_entry: FF uses or FC..FF ids are empty/special slots; FE uses are unlimited.
    if (uses == 255 || id >= 252) {
        out.name = host_text(w, "ui_battle_empty_slot");
        return out;
    }
    Machine m(w);
    out.id = id;
    out.name = item_name(m, id);
    if (!key)
        out.count = uses == 254 ? host_text(w, "ui_battle_no_uses") : utf8_to_u32(std::to_string(uses));
    out.flavor = flavor(m, id);
    if (key) {
        out.stats = host_text(w, "ui_inventory_key_item");
    } else {
        const int action = table_u8(m, Table::item_records, id, 2);
        const int power = table_u8(m, Table::item_records, id, 4);
        const int flags = table_u8(m, Table::item_records, id, 0);
        // Equipment categories 04/08/10/20 add this field to defense (replace_equipment_stats).
        // Field items and status masks also use the byte; never label those values as defense/power.
        if (!action && (flags & 0x3C))
            out.stats = host_text(w, "ui_inventory_defense", utf8_to_u32(std::to_string(power)));
        // 40/80 are passive resistance/weakness flags; 249..251 are the three foresight passives.
        else if (!action && ((flags & 0xC0) || id >= 249))
            out.stats = host_text(w, "ui_inventory_passive");
        else if (!action)
            out.stats = host_text(w, "ui_inventory_field_item");
        else if (action == 25 || action == 27 || action == 48)
            out.stats = host_text(w, "ui_inventory_status");
        else if (action == 3 || action == 4 || action == 5)
            out.stats = host_text(w, "ui_inventory_guard");
        else {
            out.stats = host_text(w, action == 24 ? "ui_inventory_healing" : "ui_inventory_power",
                                  utf8_to_u32(std::to_string(power)));
            const char *stat = nullptr;
            switch (action) {
            case 6:
            case 7:
            case 8:
            case 12:
            case 13:
            case 15:
                stat = "ui_inventory_str";
                break;
            case 11:
                stat = "ui_inventory_agl";
                break;
            case 9:
            case 10:
            case 26:
            case 28:
            case 29:
                stat = "ui_inventory_mana";
                break;
            default:
                break;
            }
            if (stat)
                out.stats += U"   " + host_text(w, stat);
        }
    }
    return out;
}

std::u32string battle_item_details(emu::Wsc &w, uint8_t id) {
    Machine m(w);
    const int action = table_u8(m, Table::item_records, id, 2);
    const int target = table_u8(m, Table::item_records, id, 1);
    const int bits = table_u8(m, Table::item_records, id, 5);
    if (!action || id >= 252)
        return {};
    std::u32string out;
    const auto add = [&](const char *key) {
        if (!out.empty())
            out += host_text(w, "ui_battle_preview_separator");
        out += host_text(w, key);
    };
    // Only these handlers interpret field 5 as an attack-kind mask. Other handlers use it for
    // species, damage, accuracy, hit count, etc. See the authoritative mapping in DATA_TABLES.md.
    switch (action) {
    case 7:
    case 26:
    case 27:
    case 31:
    case 34:
    case 48:
        switch (bits) {
        case 0x00:
            break;
        case 0x01:
            add("ui_battle_preview_element_fire");
            break;
        case 0x02:
            add("ui_battle_preview_element_ice");
            break;
        case 0x04:
            add("ui_battle_preview_element_thunder");
            break;
        case 0x08:
            add("ui_battle_preview_element_poison");
            break;
        case 0x10:
            add("ui_battle_preview_element_stone");
            break;
        case 0x20:
            add("ui_battle_preview_element_status");
            break;
        case 0x80:
            add("ui_battle_preview_element_quake");
            break;
        default:
            // 40h has no identified label. A combination of known kinds (including King's
            // Sword's FFh) is honestly summarized without claiming a single element.
            if (const int known = bits & 0xBF; known && (known & (known - 1)))
                add("ui_battle_preview_element_multiple");
            break;
        }
        break;
    default:
        break;
    }
    // Range matters for enemy actions; an ally/self item's clear bit does not mean a ranged attack.
    switch (target & 0xE0) {
    case 0x00:
        add("ui_battle_preview_target_ally");
        break;
    case 0x40:
        add("ui_battle_preview_target_self");
        break;
    case 0x80:
    case 0xA0:
        add(target & 0x20 ? "ui_battle_preview_range_melee" : "ui_battle_preview_range_ranged");
        add("ui_battle_preview_target_group");
        break;
    case 0xC0:
        add("ui_battle_preview_range_ranged");
        add("ui_battle_preview_target_all");
        break;
    default:
        break; // no guessed labels for unused target encodings
    }
    return out;
}
} // namespace sawgod::game
