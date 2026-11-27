// Text resources: every string the game code uses has a key
// (game::TextKey), and natives get strings only through text(). A language is a
// resource file of key -> real (UTF-8) string; TextTable::load() encodes it through a charset (the font's
// character -> code table) and keeps the results host-side.
//
// How a key resolves: text(m, key) gives a segment:offset the string consumers (draw_text,
// append_word_queue, ...) read through Machine as usual. A key the loaded language doesn't override is the
// original string in the ROM (so with no language loaded the game is byte-identical to the WSC); an
// overridden one lives in the Session's TextTable, mapped into the machine as the host segment
// kTextSegment (Machine::rd8 reads it from the table). Nothing is written into the ROM image, there is no
// size limit beyond 64 KB per language, and switching languages takes effect at the next string drawn.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "game/text_keys.h"

namespace sawgod::emu {
class Wsc;
}

namespace sawgod::game {

class Machine;

// The segment value text() returns for a loaded language's strings. No WSC code uses it as a data segment
// (linear 0x0FFF0, the last 16 bytes of internal RAM); only natives see it, through Machine::rd8.
inline constexpr uint16_t kTextSegment = 0x0FFF;

struct TextPtr {
    uint16_t segment, offset;
};

// Character -> code, for encoding resource strings. The default is the WSC system charset
// (tools/sawgod/charset.py); a font pack may bring its own (text/charset.csv: code,character lines).
class Charset {
  public:
    static Charset system();
    // Lines "code,character" (code in hex, e.g. "A1,Ａ"); an optional header line "code,character".
    bool add_csv(std::string_view csv, std::string *error);
    void set(uint8_t code, std::string utf8) {
        chars_.emplace(code, utf8);
        codes_[std::move(utf8)] = code;
    }
    // Encodes UTF-8 text: each character through the table (half-width ASCII falls back to its full-width
    // form, then to upper case), "[XX]" for a raw byte. False (and a message) for a character it can't
    // encode.
    bool encode(std::string_view utf8, std::vector<uint8_t> &out, std::string *error) const;
    // The character for one code (the first one set for it), or "[XX]" when it has none or it wouldn't
    // encode back to the same code: decode then encode gives the bytes back exactly.
    std::string decode(uint8_t code) const;

  private:
    std::unordered_map<std::string, uint8_t> codes_;
    std::unordered_map<uint8_t, std::string> chars_; // code -> its first character
};

// How a string sits in the original's cells (text_tokens.h): its cell count (trailing padding included: the
// renderer clears that far) and the column of each {cursor}/{tab}.
struct TextLayout {
    int cells = 0;
    std::vector<int> tabs;
};

// The loaded language: per key, an offset into `data` (or none: the original string).
class TextTable {
  public:
    // Loads a language. `csv` has a header row with "key" and "text" columns (others ignored); keys it
    // doesn't list keep the original string. A key msg_<routine>[_n] (or "msg:<signature>") is a battle-popup
    // message template: its text, with {1}, {2}... for the message's names and numbers in
    // their original order, replaces the whole message. A row with a key the game doesn't have (e.g. a file
    // made for another version) or text the charset can't encode is skipped with a message in `warnings`;
    // only a file without those columns, or more than 64 KB of strings, fails (the table is then left as it
    // was). `changed`: keys now overridden. `proportional`: drawn in the port's own font (text_render.h)
    // instead of the WSC font's codes -- the strings keep their characters (any Unicode m5x7 has),
    // battle-popup lists hold kTextWord | character. `layer`: load on top of the current table instead of
    // replacing it (the base language, then a translation: keys the translation doesn't list keep the base's
    // strings, each drawn in its own font).
    bool load(std::string_view csv, const Charset &charset, std::string *error,
              std::vector<std::string> *warnings = nullptr, int *changed = nullptr, bool proportional = false,
              bool layer = false);
    void clear();
    bool empty() const { return data_.empty(); }
    // The key's offset in kTextSegment, or -1 if it isn't overridden.
    int find(TextKey key) const { return offsets_.empty() ? -1 : offsets_[size_t(key)]; }
    uint8_t byte(uint16_t offset) const { return offset < data_.size() ? data_[offset] : 0xFF; }
    bool proportional() const { return proportional_; }
    // A run of text a battle-popup word kTextWord | id stands for (text_render.h), or nullptr.
    static constexpr uint16_t kPlayerNameRun = 0x7FF0;
    const std::u32string *run(uint16_t id) const {
        if (id == kMessageRun)
            return &message_run_;
        if (id >= kPlayerNameRun && id < kPlayerNameRun + 4)
            return party_names_[id - kPlayerNameRun].empty() ? nullptr : &party_names_[id - kPlayerNameRun];
        return id < runs_.size() ? &runs_[id] : nullptr;
    }
    static constexpr uint16_t kMessageRun = 0x7FF4;
    void set_message_run(std::u32string line) { message_run_ = std::move(line); }
    void set_player_name(size_t slot, std::u32string name) { party_names_.at(slot) = std::move(name); }
    // The battle-popup template for a message (its parts' signature, Session::message_signature), or nullptr:
    // the words to queue, kTemplatePart + n standing for the message's n-th RAM part (name, number), 1-based.
    const std::vector<uint16_t> *message_template(const std::string &signature) const {
        const auto it = templates_.find(signature);
        return it == templates_.end() ? nullptr : &it->second;
    }
    static constexpr uint16_t kTemplatePart = 0x7F00;
    // Field dialogue: the host stream replacing the run that starts at ROM `start`, or -1. Its
    // bytes are script bytes (glyph = code + 0x20, layout opcodes as in the ROM), then kDialogueEnd and the
    // segment-0x5000 offset to resume the ROM script at (the run's end).
    int dialogue(uint32_t start) const {
        const auto it = dialogue_.find(start);
        return it == dialogue_.end() ? -1 : it->second;
    }
    static constexpr uint8_t kDialogueEnd = 0xFF; // glyph code 0xDF: a blank no text uses
    bool dialogue_end(uint16_t offset) const { return dialogue_ends_.contains(offset); }
    // A name record the language overrides (name_record): its host offset, or -1; its run (-1: none).
    int name(int table, uint8_t id) const {
        const auto it = names_.find(table << 8 | id);
        return it == names_.end() ? -1 : it->second.first;
    }
    // Item descriptions (27-byte records at 9000:53E0 + 27 * id; keys itemdesc_<id>) are overlaid where the
    // game reads them: Machine::rd8 asks record_byte for DS = 0x9000 reads, so every reader (copies, sprite
    // rows, draw_text) sees the language's record -- its characters through the charset, padded with spaces.
    static constexpr uint16_t kDescriptions = 0x53E0, kDescriptionSize = 27;
    bool has_records() const { return !records_.empty(); }
    int record_byte(uint16_t offset) const {
        if (offset < kDescriptions || offset >= kDescriptions + 256 * kDescriptionSize)
            return -1;
        const auto it = records_.find(uint8_t((offset - kDescriptions) / kDescriptionSize));
        return it == records_.end() ? -1
                                    : data_[size_t(it->second + (offset - kDescriptions) % kDescriptionSize)];
    }
    // A proportional language's characters for the record starting at 9000:offset, or nullptr.
    const std::u32string *record_chars(uint16_t offset) const {
        if (offset < kDescriptions || (offset - kDescriptions) % kDescriptionSize)
            return nullptr;
        const auto it = records_.find(uint8_t((offset - kDescriptions) / kDescriptionSize));
        return it == records_.end() || !proportional_ ? nullptr : characters(uint16_t(it->second));
    }
    int name_run(int table, uint8_t id) const {
        const auto it = names_.find(table << 8 | id);
        return it == names_.end() ? -1 : it->second.second;
    }
    // The base language's own strings (a load in the WSC font that replaced the table; later layers leave
    // them): what a battle popup replays for the original's timing (Machine::original_popup_text). -1: the
    // base doesn't hold it (or no base is loaded), and the caller reads the ROM.
    int original_find(TextKey key) const {
        return original_offsets_.empty() ? -1 : original_offsets_[size_t(key)];
    }
    int original_name(int table, uint8_t id) const {
        const auto it = original_names_.find(table << 8 | id);
        return it == original_names_.end() ? -1 : it->second;
    }
    int original_record_byte(uint16_t offset) const {
        if (offset < kDescriptions || offset >= kDescriptions + 256 * kDescriptionSize)
            return -1;
        const auto it = original_records_.find(uint8_t((offset - kDescriptions) / kDescriptionSize));
        return it == original_records_.end()
                   ? -1
                   : data_[size_t(it->second + (offset - kDescriptions) % kDescriptionSize)];
    }
    // Original name record represented by a proportional popup run (table << 8 | id).
    int name_source(uint16_t run) const {
        for (const auto &[source, name] : names_)
            if (name.second == run)
                return source;
        return -1;
    }
    // A proportional table's character for the dialogue glyph at host `offset` (0: none; e.g. a layout byte).
    char32_t dialogue_char(uint16_t offset) const {
        const auto it = dialogue_chars_.find(offset);
        return it == dialogue_chars_.end() ? 0 : it->second;
    }
    // A proportional table's characters for the string at `offset` (nullptr: none there).
    const std::u32string *characters(uint16_t offset) const {
        const auto it = chars_.find(offset);
        return it == chars_.end() ? nullptr : &it->second;
    }
    // The original's layout under the string at `offset` (a proportional string over the base's, or a base
    // string drawn with button icons), or nullptr.
    const TextLayout *layout(uint16_t offset) const {
        const auto it = layouts_.find(offset);
        return it == layouts_.end() ? nullptr : &it->second;
    }
    // Button icons in the base language's strings (Ａ：けってい drawn as {A}：けってい, the rest in the WSC
    // font's glyphs, U+E000 + code). Off by default: the base then draws exactly as the ROM does.
    void set_button_icons(bool on) { button_icons_ = on; }
    bool button_icons() const { return button_icons_; }
    // The base string at `offset` with its icons, while they are on (else nullptr).
    const std::u32string *icon_text(uint16_t offset) const {
        if (!button_icons_)
            return nullptr;
        const auto it = icon_chars_.find(offset);
        return it == icon_chars_.end() ? nullptr : &it->second;
    }
    // A host string (kHostText) the loaded language gives, or nullptr.
    const std::u32string *host(std::string_view key) const {
        const auto it = host_.find(std::string(key));
        return it == host_.end() ? nullptr : &it->second;
    }

  private:
    std::vector<int> offsets_;
    std::vector<uint8_t> data_;
    std::unordered_map<uint16_t, std::u32string> chars_;
    std::vector<std::u32string> runs_;
    std::u32string message_run_;
    std::array<std::u32string, 4> party_names_{};
    std::unordered_map<uint32_t, int> dialogue_;
    std::unordered_map<uint16_t, uint16_t> dialogue_ends_; // end positions, separate from script arguments
    std::unordered_map<int, std::pair<int, int>> names_;   // table << 8 | id -> (record offset, run)
    std::unordered_map<uint8_t, int> records_;             // description id -> record offset in data_
    std::unordered_map<uint16_t, char32_t> dialogue_chars_;
    std::vector<int> original_offsets_;           // the base language's part of offsets_, names_ and
    std::unordered_map<int, int> original_names_; // records_ (offsets into data_)
    std::unordered_map<uint8_t, int> original_records_;
    std::unordered_map<std::string, std::vector<uint16_t>> templates_;
    std::unordered_map<std::string, std::u32string> host_;
    // Original reference-bearing rows, so a later name-only mod refreshes their displayed names.
    std::unordered_map<std::string, std::string> key_item_sources_;
    std::unordered_map<uint16_t, TextLayout> layouts_;
    std::unordered_map<uint16_t, std::u32string> icon_chars_;
    bool proportional_ = false;
    bool button_icons_ = false;
};

// Strings the port itself draws in the game picture and Options (the battle screen, the name editor, the
// Autosave slot): keys ui_*, a Japanese default (what the base language says)
// and an English default (for a translation that doesn't list the key), with limits in text_fit.cpp.
struct HostTextInfo {
    const char *key, *japanese, *english, *context;
};
inline constexpr HostTextInfo kHostText[] = {
#include "engine/options_text.inc"
    {"title_new_game", "はじめから", "New Game", "Title menu, first native row"},
    {"title_continue", "つづき", "Continue", "Title menu, second native row"},
    {"title_original", "オリジナル", "Original", "Title menu, third native row (when enabled)"},
    {"world_card_1", "たいりくせかい", "The Earth", "GB world-entry white page: 大陸世界; centered, typed"},
    {"world_card_2", "かいようせかい", "The Waters", "GB world-entry white page: 海洋世界; centered, typed"},
    {"world_card_3", "くうちゅうせかい", "The Heavens",
     "GB world-entry white page: 空中世界; centered, typed"},
    {"world_card_4", "としせかい", "The City", "GB world-entry white page: 都市世界; centered, typed"},
    {"intro_copyright", "© 1989 SQUARE SOFT", "© 1989 SQUARE SOFT", "GB opening splash, exact spelling"},
    {"intro_caption_1", "せかいの まんなかにたつ とうは",
     "It has been said that the tower in the center of the World",
     "GB opening white page; centered, typed, original caption strip width"},
    {"intro_caption_2", "らくえんに つうじている という", "is connected to Paradise.",
     "GB opening white page; centered, typed, original caption strip width"},
    {"intro_caption_3", "はるかな らくえんを ゆめみて", "Dreaming of a life in Paradise,",
     "GB opening white page; centered, typed, original caption strip width"},
    {"intro_caption_4", "おおくの ものたちが", "many have challenged",
     "GB opening white page; centered, typed, original caption strip width"},
    {"intro_caption_5", "このとうの ひみつに いどんでいった", "the secret of the tower,",
     "GB opening white page; centered, typed, original caption strip width"},
    {"intro_caption_6", "だが かれらの うんめいを", "but no one knows",
     "GB opening white page; centered, typed, original caption strip width"},
    {"intro_caption_7", "しるものはない", "what became of them.",
     "GB opening white page; centered, typed, original caption strip width"},
    {"intro_caption_8", "そして いま またひとり...", "Now, there is another who will brave the adventure....",
     "GB opening white page; centered, typed, original caption strip width"},
    {"intro_prologue",
     "せかいの まんなかにたつ とうは らくえんに つうじている という{n}{n}はるかな らくえんを ゆめみて "
     "おおくの ものたちが このとうの ひみつに いどんでいった だが かれらの うんめいを しるものはない そして "
     "いま またひとり...",
     "It has been said that the tower in the center of the World is connected to Paradise.{n}{n}Dreaming of "
     "a life in Paradise, many have challenged the secret of the tower, but no one knows what became of "
     "them. Now, there is another who will brave the adventure....",
     "GB New Game prologue; keep {n}{n} after the Paradise sentence; two 438-frame pages, 144 px wrap width"},
    {"ui_dialogue_item_obtained", "{item} を てにいれた", "Obtained {item}.", "field acquired-item message"},
    {"ui_dialogue_inventory_full", "もちものが いっぱいです", "Inventory is full.",
     "field item refused: inventory full"},
    {"ui_inventory_passive", "パッシブ", "Passive", "passive ability stats"},
    {"ui_save_unreadable", "よめません　ほかをえらんでください", "Can't read slot. Choose another.",
     "protected save slot refusal"},
    {"ui_service_buy", "かう", "Buy", "shop command"},
    {"ui_service_sell", "うる", "Sell", "shop command"},
    {"ui_service_exit", "でる", "Exit", "shop command"},
    {"ui_service_inn_rate", "1HP 1ケロ", "1 Kero/HP", "inn rate"},
    {"ui_service_revive_price", "100ケロ", "100 Kero", "revival price"},
    {"ui_service_revive_question", "だれをふっかつさせるんだ？", "Whom shall we revive?", "revival question"},
    {"ui_service_sale_offer", "{gold}ケロ", "Offer: {gold} Kero", "native calculated sale offer"},
    {"ui_party_items", "アイテム", "Items", "field menu Items category"},
    {"ui_party_menu", "メニュー", "Menu", "field menu title"},
    {"ui_party_time", "タイム", "Time", "field menu elapsed time label"},
    {"ui_party_status", "ステータス", "Status", "field menu Status category"},
    {"ui_party_equip", "そうび", "Equip", "field menu Equip category"},
    {"ui_party_order", "ならびかえ", "Order", "field menu party order category"},
    {"ui_party_bestiary", "モンスター", "Bestiary", "field menu Bestiary category"},
    {"ui_party_config", "せってい", "Config", "field menu native Config category when subscribed"},
    {"ui_party_load", "ロード", "Load", "field menu Load category"},
    {"ui_party_options", "オプション", "Options", "field menu Options category"},
    {"ui_party_save", "セーブ", "Save", "field menu Save category"},
    {"ui_party_current_floor", "いまのかい {floor}", "Current Floor {floor}",
     "field/save menu current floor"},
    {"ui_party_highest_floor", "さいこうのかい {topfloor}", "Highest Floor {topfloor}",
     "field/save menu highest floor"},
    {"ui_party_kero", "ケロ", "Kero", "shared gold pane currency"},
    {"ui_status_dead", "しぼう", "Dead", "member panel: condition shown in place of max HP"},
    {"ui_status_stone", "せきか", "Stone", "member panel: condition shown in place of max HP"},
    {"ui_status_curse", "のろい", "Curse", "member panel: condition shown in place of max HP"},
    {"ui_status_blind", "やみ", "Blind", "member panel: condition shown in place of max HP"},
    {"ui_ending_notice",
     "ずかんのデータを　セーブすることが　できます{n}ここでセーブすると　いままでのデータは{n}"
     "さくじょされます{n}{n}"
     "ロードすると　はじめからの　プレイ　になりますが{n}ずかんのデータを　ひきついだじょうたいで{n}"
     "プレイできます",
     "You can save your Bestiary data. Saving here erases the data you had until now.{n}{n}Loading it starts "
     "the game over from the beginning, with your Bestiary data carried over.",
     "ending: the New Game+ notice before the save screen ({n}{n} separates the two paragraphs)"},
    {"ui_party_str", "こうげき", "STR", "party stats strength label; also the meat preview"},
    {"ui_party_def", "ぼうぎょ", "DEF", "party stats defense label; also the meat preview"},
    {"ui_party_agi", "すばやさ", "AGI", "party stats agility label; also the meat preview"},
    {"ui_party_mag", "まりょく", "MAG", "party stats magic label; also the meat preview"},
    {"ui_party_order_source", "だれを　うごかす？", "Move whom?", "party order: choosing the member to move"},
    {"ui_party_order_target", "どこへ　うごかす？", "Swap with whom?", "party order: choosing where it goes"},
    {"ui_party_picked_hint", "{A}：つかう　{B}：もどる", "{A}: Use   {B}: Back",
     "Status: key hint on the bottom line while a row is picked"},
    {"ui_party_gear", "そうび", "Gear", "Status/Equip gear heading"},
    {"ui_party_abilities", "のうりょく", "Abilities", "Status/Equip fixed abilities heading"},
    {"ui_inventory_items", "アイテム", "Items", "inventory: ordinary-items tab"},
    {"ui_inventory_keys", "だいじなもの", "Key Items", "inventory: key-items tab"},
    {"ui_inventory_crystals", "クリスタル", "Crystals", "inventory: Crystal row and chooser heading"},
    {"ui_inventory_trash", "ゴミばこ", "Trash", "inventory: native discard destination"},
    {"ui_inventory_crystal_hint", "クリスタルを　つかう", "Use a Crystal.",
     "inventory: Crystal action description"},
    {"ui_inventory_trash_hint", "アイテムを　すてる", "Discard an item here.",
     "inventory: Trash destination description"},
    {"ui_inventory_key_item", "だいじなもの", "Key item", "inventory: key-item classification"},
    {"ui_inventory_field_item", "いどうちゅうに　つかう", "Field item",
     "inventory: field-only item classification"},
    {"ui_inventory_status", "じょうたいを　かえる", "Status effect",
     "inventory: status-effect item classification"},
    {"ui_inventory_guard", "ぼうぎょ", "Guard", "inventory: defense stance item classification"},
    {"ui_inventory_healing", "かいふく　{amount}", "Healing {amount}",
     "inventory: base potion healing before random spread"},
    {"ui_inventory_str", "ちから", "STR", "inventory: strength scaling stat"},
    {"ui_inventory_agl", "すばやさ", "AGI", "inventory: agility scaling stat"},
    {"ui_inventory_mana", "まりょく", "MAG", "inventory: magic scaling stat"},
    {"ui_inventory_power", "こうか　{amount}", "Power {amount}",
     "inventory: item's numeric effect power (not final damage)"},
    {"ui_inventory_defense", "ぼうぎょ　{amount}", "Defense {amount}", "inventory: gear defense"},
    {"ui_item_target", "だれに　つかう？", "Use on whom?", "item use: title over the party target rows"},
    {"ui_item_refill_hint", "アイテムを　えらんでください", "Select an item to refill.",
     "item refill: hint below the item list"},
    {"ui_item_floor", "{floor}かい", "Floor {floor}", "item travel: the tower floor being chosen"},
    {"ui_battle_meat", "{monster}の　にく", "{monster} Meat", "meat reward: the dropping monster's meat"},
    {"ui_battle_meat_prompt", "{monster}の　にくを　だれが　たべる？", "Who will eat the {monster} Meat?",
     "meat reward: wrapped eater prompt"},
    {"ui_battle_meat_eat", "たべる！", "Eat It!", "meat reward: accept"},
    {"ui_battle_meat_dont", "たべない", "Don't", "meat reward: decline"},
    {"ui_battle_esper_change", "{actor}の　{ability}が　{new_ability}に　かわろうとしている",
     "{actor}'s {ability} is changing to {new_ability}.", "Esper replacement confirmation: wrapped proposal"},
    {"ui_battle_esper_allow", "よろしいですか？", "Allow it?", "Esper replacement confirmation: question"},
    {"ui_battle_esper_yes", "はい", "Yes", "Esper replacement confirmation: accept"},
    {"ui_battle_esper_no", "いいえ", "No", "Esper replacement confirmation: decline"},
    {"ui_battle_esper_kept", "かわりません", "No change.",
     "Esper replacement declined: existing ability kept"},
    {"ui_battle_meat_refused", "にくを　たべられない！", "Can't eat meat!",
     "meat reward: the chosen member (a human or mutant) can't eat"},
    {"ui_battle_hp", "HP", "HP", "meat preview: hit points label"},
    {"ui_battle_fight", "たたかう", "Fight", "battle command menu, first row"},
    {"ui_battle_run", "にげる", "Run", "battle command menu, second row"},
    {"ui_battle_empty_slot", "なし", "Empty", "battle item list: an empty equipment slot"},
    {"ui_battle_no_uses", "ーー", "--", "battle item list: uses of an item that never runs out"},
    {"ui_battle_group_count", "×{count}", " x{count}", "monster plate: how many in a group of more than one"},
    {"ui_battle_preview_element_fire", "ほのお", "Fire", "battle action preview: element fire"},
    {"ui_battle_preview_element_ice", "れいき", "Ice", "battle action preview: element ice"},
    {"ui_battle_preview_element_thunder", "でんき", "Thunder", "battle action preview: element thunder"},
    {"ui_battle_preview_element_poison", "どく", "Poison", "battle action preview: element poison"},
    {"ui_battle_preview_element_stone", "せきか", "Stone", "battle action preview: element stone"},
    {"ui_battle_preview_element_status", "じょうたい", "Status", "battle action preview: element status"},
    {"ui_battle_preview_element_quake", "じしん", "Quake", "battle action preview: element quake"},
    {"ui_battle_preview_element_multiple", "ふくすうぞくせい", "Multiple",
     "battle action preview: element multiple"},
    {"ui_battle_preview_range_melee", "きんせつ", "Melee", "battle action preview: range melee"},
    {"ui_battle_preview_range_ranged", "えんかく", "Ranged", "battle action preview: range ranged"},
    {"ui_battle_preview_target_group", "１グループ", "One group", "battle action preview: target group"},
    {"ui_battle_preview_target_all", "てきぜんたい", "All enemies", "battle action preview: target all"},
    {"ui_battle_preview_target_ally", "なかま", "Ally", "battle action preview: target ally"},
    {"ui_battle_preview_target_self", "じぶん", "Self", "battle action preview: target self"},
    {"ui_battle_preview_separator", "／", " / ", "battle action preview: separator"},
    {"ui_battle_empty_member", "なし", "Empty", "battle party card: no member in this slot"},
    {"ui_save_overwrite", "このデータをかきかえますか？", "Overwrite this data?",
     "occupied save confirmation"},
    {"ui_save_inherit_first", "ずかんをひきついで", "Inherit Bestiary and start a",
     "New Game+ confirmation, first line"},
    {"ui_save_inherit_second", "あたらしくはじめますか？", "New Game+?",
     "New Game+ confirmation, second line"},
    {"ui_save_autosave", "オート", "AUTO", "save/load list: the autosave slot's label"},
    {"ui_name_entry_label", "なまえ", "Name", "name entry: label left of the name being typed"},
    {"ui_name_entry_space", "くうはく", "Space", "name entry: adds a space"},
    {"ui_name_entry_delete", "けす", "Delete", "name entry: removes the last glyph"},
    {"ui_name_entry_done", "おわり", "Done", "name entry: accepts the name"},
    {"ui_name_entry_hiragana", "ひらがな {page}", "Hiragana {page}",
     "name entry (Japanese only): page title, {page} = 1/2"},
    {"ui_name_entry_katakana", "カタカナ {page}", "Katakana {page}",
     "name entry (Japanese only): page title, {page} = 1/2"},
    {"ui_creation_hp", "HP", "HP", "character creation: hit points label"},
    {"ui_bestiary_title", "モンスターずかん", "Bestiary", "Bestiary screen title"},
    {"ui_bestiary_history", "へんしんりれき", "History", "Bestiary second category"},
    {"ui_bestiary_abilities", "のうりょく", "Abilities", "Bestiary detail heading"},
    {"ui_bestiary_unseen", "みはっけん", "Not seen", "Bestiary unseen monster"},
    {"ui_bestiary_back", "{B} もどる", "{B} Back", "Bestiary back hint"},
    {"ui_bestiary_hint", "{A} くわしく {B} もどる", "{A} Details {B} Back", "Bestiary seen catalog hint"},
    {"ui_bestiary_family", "しゅぞく", "Family", "Bestiary family label"},
    {"ui_bestiary_hp", "HP", "HP", "Bestiary HP label"},
    {"ui_bestiary_family_0", "こんちゅうじん", "Insect Man", "Bestiary family name"},
    {"ui_bestiary_family_1", "さかな", "Fish", "Bestiary family name"},
    {"ui_bestiary_family_2", "しょくぶつ", "Plant", "Bestiary family name"},
    {"ui_bestiary_family_3", "こんちゅう", "Insect", "Bestiary family name"},
    {"ui_bestiary_family_4", "カニ", "Crab", "Bestiary family name"},
    {"ui_bestiary_family_5", "とら", "Tiger", "Bestiary family name"},
    {"ui_bestiary_family_6", "サイ", "Rhino", "Bestiary family name"},
    {"ui_bestiary_family_7", "とりにんげん", "Birdman", "Bestiary family name"},
    {"ui_bestiary_family_8", "へび", "Snake", "Bestiary family name"},
    {"ui_bestiary_family_9", "タコ", "Octopus", "Bestiary family name"},
    {"ui_bestiary_family_10", "いもむし", "Caterpillar", "Bestiary family name"},
    {"ui_bestiary_family_11", "とかげ", "Lizard", "Bestiary family name"},
    {"ui_bestiary_family_12", "おおかみおとこ", "Werewolf", "Bestiary family name"},
    {"ui_bestiary_family_13", "おに", "Ogre", "Bestiary family name"},
    {"ui_bestiary_family_14", "キマイラ", "Chimera", "Bestiary family name"},
    {"ui_bestiary_family_15", "めだま", "Eyeball", "Bestiary family name"},
    {"ui_bestiary_family_16", "がいこつ", "Skeleton", "Bestiary family name"},
    {"ui_bestiary_family_17", "スライム", "Slime", "Bestiary family name"},
    {"ui_bestiary_family_18", "あくま", "Demon", "Bestiary family name"},
    {"ui_bestiary_family_19", "へびおんな", "Snake Woman", "Bestiary family name"},
    {"ui_bestiary_family_20", "ゾンビ", "Zombie", "Bestiary family name"},
    {"ui_bestiary_family_21", "ゆうれい", "Ghost", "Bestiary family name"},
    {"ui_bestiary_family_22", "ゴーレム", "Golem", "Bestiary family name"},
    {"ui_bestiary_family_23", "とり", "Bird", "Bestiary family name"},
    {"ui_bestiary_family_24", "ドラゴン", "Dragon", "Bestiary family name"},
    {"ui_bestiary_generation", "だい {generation}", "Generation {generation}",
     "Bestiary: the member's current form generation, {generation} = number up to 9999"},
};
const HostTextInfo *host_text_info(std::string_view key);
// The battle-popup message a key names: its short key (msg_<routine>[_n]) or msg:<parts>; else nullptr.
const TextMessage *text_message(std::string_view key);
// The string to draw for a host key: the loaded language's, else the default for it (English when a
// translation is loaded, else Japanese); its parameter token (host_param) replaced by `arg`. Kana come back
// as the WSC font's glyphs (U+E000 + code), which both text_render and ui_chrome draw.
std::u32string host_text(emu::Wsc &w, std::string_view key, std::u32string_view arg = {});
// The named token a host string's one parameter goes in ("{gold}" for ui_service_sale_offer), or "" when it
// has none.
std::string_view host_param(std::string_view key);

// Names from the data tables (item names: 8-byte records at 9000:4BE0 + 8 * id; monster type names: 9-byte
// records at 9000:6FE0 + 9 * id). A language overrides them with keys item_<id> / monster_<id> (decimal).
enum class NameTable : uint8_t { Item, Monster };
// Where a name's record is, as the game reads it: the ROM's, or the language's in the host segment (same
// length: its characters through the charset, padded with spaces, 0xFF last).
TextPtr name_record(Machine &m, NameTable table, uint8_t id);
// A proportional language's name as a battle-popup run (kTextWord | run), or -1.
int name_run(Machine &m, NameTable table, uint8_t id);

// Where the string for `key` is, as the consumers read it (see the header). A native that drew from a
// constant segment:offset passes these instead -- in the register(s) the original code used.
TextPtr text(Machine &m, TextKey key);
// The same place, for host presentation reading a string: the game's state (the recorder) is untouched.
TextPtr text_at(Machine &m, TextKey key);

// The name-entry sheet for page 0-2 (9 x 10 characters: hiragana, katakana, letters and symbols).
inline TextKey name_entry_sheet(uint8_t page) {
    return TextKey(uint16_t(TextKey::name_entry_sheet_0) + (page < 3 ? page : 2));
}

// After the consumer call: DS/ES back to the key's original segment if they hold kTextSegment, so the
// code after it (which may read other data through DS) sees what the original code leaves there.
void text_done(Machine &m, TextKey key);

// The original text as a resource file (key,text,routine,kind), extracted from the ROM: every
// key, battle-popup message template, name, item description and dialogue run -- the first run writes it
// (text/ja.csv) and the game reads its Japanese from it. One row per key.
std::string export_text(const emu::Wsc &w);
// A text resource with another laid over it by key (a mod's text/<lang>.csv over the extracted one): the
// base's rows with the overlay's text where it has the key, then the overlay's other keys. key,text CSV.
std::string merge_text(std::string_view base, std::string_view overlay);
// One CSV cell, quoted when it needs to be.
std::string csv_cell(std::string_view s);

// Splits CSV text into rows of fields (RFC 4180 quoting; \n or \r\n line ends).
std::vector<std::vector<std::string>> parse_csv(std::string_view csv);

} // namespace sawgod::game
