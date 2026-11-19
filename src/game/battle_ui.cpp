// Battle presentation snapshots and host-drawn battle windows.
#include "game/battle_ui.h"
#include "game/presentation_ownership.h"
#include "game/text_wrap.h"

#include <algorithm>
#include <cmath>

#include "game/session.h"
#include "game/tables.h"
#include "game/text_render.h"
#include "game/ui_chrome.h"
#include "game/ui_items.h"
#include "game/ui_sprites.h"

namespace sawgod::game {
namespace {
constexpr uint16_t kActors = 0xD71A, kGroups = 0xD6D5, kGroupCount = 0xD6D4;
constexpr uint16_t kMember = 0xD6E1, kMenu = 0xD6E2, kRow = 0xDC62, kPage = 0xDC6E, kTarget = 0xDC66;
// run_transformation_menu: the eater, Eat (0) / Don't (1), the new form, "no change", the dropping enemy's
// type.
constexpr uint16_t kEater = 0xD713, kEatChoice = 0xD714, kNewForm = 0xD715, kUnchanged = 0xEC82;
constexpr uint16_t kMeatType = 0xD719, kConfig = 0xD0E7, kBattleEndState = 0xDCC2, kBattleFlags = 0xD70E;
// The meat preview: two 82 px panes from the top to the party cards; the
// name's line, the picture's 48 px box, then five stat lines at a 9 px pitch ending 2 px above the cards.
// Values end kMeatMarkRoom short of the text edge: room for the 8 px trend mark after a 2 px gap.
constexpr int kMeatPane = 82, kMeatPaneHeight = 112, kMeatNameY = 4, kMeatPictureTop = 15,
              kMeatPictureBox = 48;
constexpr int kMeatStatPitch = 9, kMeatStatsY = kMeatPaneHeight - 2 - 5 * kMeatStatPitch + 1,
              kMeatMarkRoom = 10;
// Labels stop 2 px before a four-digit value (19 px of numerals).
constexpr int kMeatLabelWidth = kMeatPane - 5 - kMeatMarkRoom - 19 - 2 - 5;
uint16_t word(const emu::Wsc &w, uint16_t at) { return uint16_t(w.ram[at] | w.ram[uint16_t(at + 1)] << 8); }
std::u32string ascii(const std::string &s) { return utf8_to_u32(s); }
// Wrapped host prompts. Kana text has no ASCII spaces: its full-width spaces (the game's glyph after
// with_wsc_glyphs) separate words, so a line may also break after one (dropped at the end of a line).
std::vector<std::u32string> prompt_lines(const std::u32string &text, int width) {
    static const char32_t kWide = with_wsc_glyphs(U"\u3000").front();
    const auto wide = [](char32_t c) { return c == U'\u3000' || c == kWide; };
    const auto trim = [&](std::u32string &line) {
        while (!line.empty() && wide(line.back()))
            line.pop_back();
    };
    std::vector<std::u32string> lines;
    if (std::none_of(text.begin(), text.end(), wide)) {
        for (auto &line : wrap_text(text, width).lines)
            lines.push_back(std::move(line.text));
        return lines;
    }
    std::u32string line;
    for (size_t at = 0; at < text.size();) {
        size_t end = at;
        while (end < text.size() && !wide(text[end]))
            ++end;
        while (end < text.size() && wide(text[end]))
            ++end;
        const std::u32string word = text.substr(at, end - at);
        at = end;
        std::u32string trimmed = line + word;
        trim(trimmed);
        if (!line.empty() && Chrome::text_width(trimmed) > width) {
            trim(line);
            lines.push_back(line);
            line = word;
        } else
            line += word;
    }
    trim(line);
    if (!line.empty() || lines.empty())
        lines.push_back(line);
    return lines;
}
std::u32string decode(uint8_t code) {
    static const Charset charset = Charset::system();
    return utf8_to_u32(charset.decode(code));
}
std::u32string name(emu::Wsc &w, NameTable table, uint8_t id) {
    auto &text = Session::of(w).text();
    const int run = text.name_run(int(table), id);
    if (run >= 0)
        if (const auto *s = text.run(uint16_t(run)))
            return *s;
    const int host = text.name(int(table), id);
    std::u32string out;
    const uint32_t start = table == NameTable::Monster ? 0x396FE0 + 9 * id : 0x394BE0 + 8 * id;
    for (int i = 0; i < (table == NameTable::Monster ? 8 : 7); ++i) {
        const uint8_t c = host >= 0 ? text.byte(uint16_t(host + i)) : w.rom[(start + i) & (w.rom.size() - 1)];
        if (c == 0xFF)
            break;
        out += decode(c);
    }
    while (!out.empty() && (out.back() == U' ' || out.back() == U'　'))
        out.pop_back();
    return out;
}

// The battle's own pictures (party sprites, status icons) through the shared chrome (game/ui_chrome.h).
struct Canvas : Chrome {
    emu::Wsc &w;
    Canvas(emu::Wsc &wsc, std::vector<uint32_t> &px, emu::Wsc::ColorMode mode, bool second)
        : Chrome(wsc, px, mode, second), w(wsc) {}
    void sprite(int member, int x, int y, bool conditions = true) {
        // Alternate the original condition/stat icon with the portrait, retaining status feedback.
        const uint16_t status = uint16_t(0xC02C + member * 16);
        const uint16_t actor = uint16_t(kActors + member * 32), party = uint16_t(0xD000 + member * 31);
        bool condition = conditions && (w.ram[actor + 2] & 0xFC) != 0;
        // The battle caps a party stat above 99 at 99 for its record and restores the original once it is won
        // (store_party_battle_records, before the victory message): compare with the capped value, or a
        // member with a stat over 99 shows a change only while the victory message is up.
        for (int stat = 0; stat < 3 && conditions; ++stat)
            condition |= w.ram[actor + 7 + stat] != std::min<uint8_t>(w.ram[party + 11 + stat], 99);
        if (condition && w.ram[status + 2] < 144 && (w.frame_count / 30) % 2) {
            // Each of the four 8x8 pieces goes where the game positions its sprite (entry: tile word, Y, X).
            int left = 255, top = 255;
            for (int i = 0; i < 4; ++i) {
                top = std::min<int>(top, memory(uint16_t(status + i * 4 + 2)));
                left = std::min<int>(left, memory(uint16_t(status + i * 4 + 3)));
            }
            for (int i = 0; i < 4; ++i) {
                const uint16_t at = uint16_t(status + i * 4);
                const uint16_t entry = uint16_t(memory(at) | memory(uint16_t(at + 1)) << 8);
                tile(entry, x + (memory(uint16_t(at + 3)) - left), y + (memory(uint16_t(at + 2)) - top),
                     false, true);
            }
            return;
        }
        const uint16_t map = uint16_t(((w.io[7] >> 4) & 15) << 11);
        for (int yy = 0; yy < 2; ++yy)
            for (int xx = 0; xx < 2; ++xx) {
                const uint16_t at = uint16_t(map + ((16 + yy) * 32 + 1 + member * 7 + xx) * 2);
                tile(uint16_t(memory(at) | memory(uint16_t(at + 1)) << 8), x + xx * 8, y + yy * 8);
            }
    }
};

// A monster form's template HP and four stats (STR, DEF, AGI, MAG), as the native preview reads them.
std::array<int, 5> form_stats(emu::Wsc &w, uint8_t type) {
    Machine m(w);
    const uint16_t record = table_at16(m, Table::monsters, 0x6000, uint16_t(0xD1FB + type * 2));
    std::array<int, 5> out{table_at16(m, Table::monsters, 0x6000, uint16_t(record + 3))};
    for (int i = 0; i < 4; ++i)
        out[size_t(i + 1)] = table_at(m, Table::monsters, 0x6000, uint16_t(record + 5 + i));
    return out;
}

std::u32string popup(Machine &m, uint16_t segment, uint16_t offset) {
    std::u32string result;
    for (int i = 0; i < 64; ++i) {
        const uint16_t code = m.rd16(segment, uint16_t(offset + i * 2));
        if (code == 0xFFFF)
            break;
        if (code & kTextWord) {
            if (const auto *s = Session::of(m.wsc()).text().run(uint16_t(code & 0x7FFF)))
                result += *s;
        } else if (code < 0x1C0) {
            result += decode(uint8_t(code / 2));
        }
    }
    return result;
}
} // namespace

void BattleUi::install(emu::Wsc &w) {
    subscriptions_.clear();
    active_ = ready_ = executing_ = rewards_ = auto_target_ = false;
    selector_battlefield_ = {};
    meat_ = 0;
    battlefield_ = {};
    numbers_.clear();
    message_.clear();
    previous_message_.clear();
    w.reset_scene_underlay();
    auto &session = Session::of(w);
    session.player_names().enabled = true;
    session.meat_b_cancels = true;
    auto &h = session.hooks();
    subscriptions_.push_back(h.subscribe(hooks::Event::SceneFrame, [this](Machine &m, const auto &) {
        if (generation_ == Session::of(m.wsc()).generation())
            Session::of(m.wsc()).scene_requests().battle = {active_,    enabled_, ready_,
                                                            executing_, rewards_, meat_};
    }));
    subscriptions_.push_back(h.subscribe(hooks::Event::BattleStart, [this, &w](Machine &, const auto &) {
        active_ = true;
        auto_target_ = false;
        selector_battlefield_ = {};
        rewards_ = false;
        // The native equipment/target windows overwrite SCR1's top rows with palette 7-15 cells, the
        // same cells the clean scene hides: keep the backdrop beneath them for every picture.
        w.reset_scene_underlay();
        meat_ = 0;
        message_.clear();
        previous_message_.clear();
        ready_ = executing_ = false;
        generation_ = Session::of(w).generation();
        numbers_.clear();
        last_hp_.fill(-1);
        notified_.fill(false);
    }));
    subscriptions_.push_back(h.subscribe(hooks::Event::BattleEnd, [this, &w](Machine &, const auto &) {
        active_ = ready_ = auto_target_ = false;
        selector_battlefield_ = {};
        meat_ = 0;
        battlefield_ = {};
        w.reset_scene_underlay();
    }));
    subscriptions_.push_back(h.subscribe(hooks::Event::BattleMenu, [this](Machine &, const auto &a) {
        // The Fight/Run menu opens a new command phase. A battle chained after a victory without a new
        // BattleStart (the ROM's debug auto-win) must not keep that victory's reward state, which holds the
        // picture and hides the command menu's layout.
        if (a.a == 0)
            rewards_ = false;
        if (active_ && !rewards_ && enabled_) {
            ready_ = true;
            executing_ = false;
            notified_.fill(false);
        }
    }));
    subscriptions_.push_back(h.subscribe(hooks::Event::BattleRoundStart, [this](Machine &, const auto &) {
        executing_ = true;
        // Actions reuse the cursor sprite slots for their animations (the fire burst's top rows are sprites
        // 0-19): from here only the native status icons in the panel area stay hidden.
    }));
    subscriptions_.push_back(
        h.subscribe(hooks::Event::BattleAutoTargetSetting, [this, &w](Machine &, const auto &a) {
            if (!active() || !enabled_)
                return;
            auto_target_ = a.a != 0;
            if (auto_target_) {
                // The native Config panel rewrites battlefield tiles. Keep the clean picture before it
                // draws, just as the meat selector does; never restore tiles into the live machine.
                w.refresh_second();
                for (int i = 0; i < 4; ++i)
                    selector_battlefield_[size_t(i)] = w.framebuffer_rgb(
                        i & 1 ? emu::Wsc::ColorMode::Lcd : emu::Wsc::ColorMode::Raw, i >= 2, true);
            } else
                selector_battlefield_ = {};
        }));
    // The victory and reward messages stay in this layout (the cards and log as in the
    // battle): the native windows and status icons stay hidden; the windows fade with the game's palettes.
    subscriptions_.push_back(h.subscribe(hooks::Event::BattleRewards, [this](Machine &, const auto &) {
        rewards_ = true;
        ready_ = false;
        // This event precedes the native A-wait for victory; the next popup replaces the line.
    }));
    subscriptions_.push_back(h.subscribe(hooks::Event::BattleAction, [this](Machine &, const auto &) {
        message_.clear();
        previous_message_.clear();
    }));
    subscriptions_.push_back(h.subscribe(hooks::Event::MeatMenu, [this, &w](Machine &, const auto &a) {
        if (!active_ || !enabled_)
            return;
        if (a.a == 5) { // A on a member who can't eat meat
            refused_eater_ = w.ram[kEater];
            return;
        }
        refused_eater_ = -1;
        const int stage = std::clamp(a.a, 0, 4);
        if (stage == 1 || stage == 4) {
            message_.clear(); // the drop announcement / the result arrive as native popups
            previous_message_.clear();
        }
        if (stage == 1 || stage == 0)
            for (auto &picture : battlefield_)
                picture.clear();
        else if (battlefield_[0].empty())
            // The native Eat/Don't and preview windows replace the battlefield's tiles: keep its last
            // complete picture (the drop announcement's) to show beneath this layout's windows.
            for (int i = 0; i < 4; ++i)
                if (i < 2 || w.second()) {
                    if (i == 2)
                        w.refresh_second(); // a kept GB battlefield must be the current one
                    battlefield_[size_t(i)] = w.framebuffer_rgb(
                        i & 1 ? emu::Wsc::ColorMode::Lcd : emu::Wsc::ColorMode::Raw, i >= 2, true);
                }
        // Comparison portraits reuse the enemy tile storage. Once the menu closes, remembered
        // cells from that picture no longer describe the restored battlefield beneath reward windows.
        if (stage == 0)
            w.reset_scene_underlay();
        meat_ = stage;
    }));
    subscriptions_.push_back(h.subscribe(hooks::Event::StatusPopup, [this, &w](Machine &m, const auto &a) {
        if (active_) {
            previous_message_ = a.c ? message_ : std::u32string{};
            const auto *joined = Session::of(w).joined_battle_popup_text();
            message_ = joined ? *joined : popup(m, uint16_t(a.a), uint16_t(a.b));
            if (Session::of(w).esper_declined)
                message_ = host_text(w, "ui_battle_esper_kept");
            message_frame_ = w.frame_count;
        }
    }));
    subscriptions_.push_back(h.subscribe(hooks::Event::BattleHpChange, [this, &w](Machine &, const auto &a) {
        if (active_ && a.b) {
            numbers_.push_back({a.a, a.b, w.frame_count});
            if (a.a >= 0 && a.a < 31)
                notified_[size_t(a.a)] = true;
        }
    }));
}

void BattleUi::set_enabled(bool on) {
    enabled_ = on;
    if (!on) {
        ready_ = auto_target_ = false;
        selector_battlefield_ = {};
        meat_ = 0;
    }
}
bool BattleUi::live_palette(const emu::Wsc &w) const {
    return active() && executing_ && !rewards_ && !meat_ && !auto_target_ && !(w.ram[kBattleFlags] & 4) &&
           !(w.ram[kBattleEndState] & 6);
}
int BattleUi::enemy_target(const emu::Wsc &w) const {
    if (!active() || meat_ || rewards_ || executing_ || w.ram[kMenu] != 2)
        return -1;
    const int target = w.ram[uint16_t(kTarget + std::min<int>(w.ram[kMember], 3))];
    return target < std::clamp<int>(w.ram[kGroupCount], 0, 3) ? target : -1;
}
std::u32string BattleUi::party_name(emu::Wsc &w, int member) const {
    auto label = names_[size_t(member)];
    if (label.empty())
        label = utf8_to_u32(Session::of(w).player_names().values[size_t(member)]);
    if (label.size() > kPlayerNameGlyphs)
        label.resize(kPlayerNameGlyphs);
    if (label.empty())
        for (int n = 0; n < 4; ++n) {
            const uint8_t code = w.ram[uint16_t(0xD001 + member * 31 + n)];
            if (code == 255)
                break;
            label += decode(code);
        }
    return label;
}
std::optional<BattleActionPreview> BattleUi::action_preview(emu::Wsc &w) const {
    const int target = enemy_target(w);
    if (target < 0 || !w.ram[uint16_t(kGroups + target * 2 + 1)])
        return std::nullopt;
    const int member = std::min<int>(w.ram[kMember], 3);
    // Same source as selected_battle_equipment_core / the battle action announcement, not the
    // item-list's first visible row. Read directly so drawing cannot change machine registers.
    const int slot = int(w.ram[uint16_t(kPage + member)]) + w.ram[uint16_t(kRow + member)];
    if (slot >= 8)
        return std::nullopt;
    const uint16_t at = uint16_t(kActors + member * 32 + 14 + slot * 2);
    const uint8_t id = w.ram[at], uses = w.ram[uint16_t(at + 1)];
    if (uses == 255 || id >= 252)
        return std::nullopt;
    const auto count = w.ram[uint16_t(kGroups + target * 2 + 1)];
    auto group = name(w, NameTable::Monster, w.ram[uint16_t(kGroups + target * 2)]);
    if (count > 1)
        group += host_text(w, "ui_battle_group_count", ascii(std::to_string(count)));
    return BattleActionPreview{id,
                               name(w, NameTable::Item, id),
                               uses == 254 ? host_text(w, "ui_battle_no_uses") : ascii(std::to_string(uses)),
                               party_name(w, member),
                               std::move(group),
                               battle_item_details(w, id)};
}
void BattleUi::set_transparency(int percent) { transparency_ = std::clamp(percent, 0, 100); }
void BattleUi::set_party_name(size_t slot, std::u32string name) {
    if (slot < names_.size()) {
        if (name.size() > kPlayerNameGlyphs)
            name.resize(kPlayerNameGlyphs);
        names_[slot] = std::move(name);
    }
}
void BattleUi::frame(emu::Wsc &w) {
    if (active_ && Session::of(w).generation() != generation_) {
        active_ = ready_ = auto_target_ = false;
        selector_battlefield_ = {};
        meat_ = 0;
        battlefield_ = {};
        numbers_.clear();
        w.reset_scene_underlay();
    }
    std::erase_if(numbers_, [&](const Number &n) { return w.frame_count - n.frame > 45; });
    // Healing, regeneration and a few scripted effects write HP directly rather than using the meter.
    // Observe those changes too; notifications already covering a change prevent duplicate numbers.
    if (active())
        for (int a = 0; a < 31; ++a) {
            const uint16_t at = uint16_t(kActors + a * 32);
            const int hp = w.ram[at + 2] == 255 ? -1 : word(w, uint16_t(at + 3));
            auto &old = last_hp_[size_t(a)];
            if (old >= 0 && hp >= 0 && old != hp) {
                if (!notified_[size_t(a)])
                    numbers_.push_back({a, old - hp, w.frame_count});
                notified_[size_t(a)] = false;
            }
            old = hp;
        }
}

// Three visible rows. Scroll longer localized messages once per 30 game frames, then hold the end.
std::vector<std::u32string> battle_log_rows(std::u32string_view newest, std::u32string_view previous,
                                            size_t scroll, bool localized) {
    const auto wrap = [localized](std::u32string_view text) {
        std::vector<std::u32string> rows;
        if (text.empty())
            return rows;
        for (const auto &line : wrap_text(text, kBattleLogWidth, true, !localized).lines)
            rows.push_back(line.text);
        return rows;
    };
    auto rows = wrap(newest);
    if (rows.size() > kBattleLogLines) {
        const size_t start = std::min(scroll, rows.size() - kBattleLogLines);
        return {rows.begin() + start, rows.begin() + start + kBattleLogLines};
    }
    if (rows.size() < kBattleLogLines && !previous.empty()) {
        const auto before = wrap(previous);
        if (!before.empty())
            rows.insert(rows.begin(), before.back());
    }
    return rows;
}

std::vector<uint32_t> BattleUi::picture(emu::Wsc &w, emu::Wsc::ColorMode mode, bool second) {
    frame(w);
    if (!active())
        return w.framebuffer_rgb(mode, second);
    auto pixels = w.framebuffer_rgb(mode, second, true);
    Canvas c{w, pixels, mode, second};
    using Ink = Chrome::Ink;
    using Window = Chrome::Window;
    const int state = w.ram[kMenu], member = std::min<int>(w.ram[kMember], 3);
    const bool selecting = !meat_ && !executing_ && (state == 0 || state == 1 || state == 2 || state == 4);
    const auto log = [&](int top) {
        const auto rows =
            battle_log_rows(message_, previous_message_, size_t((w.frame_count - message_frame_) / 30),
                            Session::of(w).text().proportional());
        for (size_t i = 0; i < rows.size(); ++i)
            c.text(rows[i], 5, top + int(i) * kBattleLogPitch);
    };
    // As the original's panels: the acting (or targeted) member's in the usual window, the others dim.
    const auto cards = [&](int highlighted) {
        for (int i = 0; i < 4; ++i) {
            const int x = i * 56, y = 112;
            const uint16_t actor = uint16_t(kActors + i * 32);
            const bool present = w.ram[actor + 2] != 255;
            const Window style = i == highlighted ? Window::Normal : Window::Inactive;
            c.window(x, y, 56, 32, style);
            if (!present) {
                c.text(host_text(w, "ui_battle_empty_member"), x + 5, y + 4, Ink::Muted, 48, style);
                continue;
            }
            // Choosing the eater: the arrow points at the chosen member's name.
            const int pointer = meat_ == 3 && i == highlighted ? 11 : 0;
            if (pointer && arrow_kept_)
                c.cursor(x + 4, y + 4, arrow_);
            else if (pointer)
                c.cursor(x + 4, y + 4);
            // (After the battle a transformed member's new stats differ from its battle record's: no icon.)
            c.sprite(i, x + (second ? 3 : 2), y + 13,
                     !meat_); // 1 px clear of the HP numerals (GB sprites are wider)
            c.name(party_name(w, i), x + 4 + pointer, y + 4, Ink::Text, 49 - pointer, style);
            const int hp = word(w, uint16_t(actor + 3)), maximum = word(w, uint16_t(actor + 5));
            c.bar(x + 20, y + 20, 32, 8, hp, maximum, std::to_string(hp) + "/" + std::to_string(maximum));
        }
    };
    if (const auto *change = Session::of(w).esper_change()) {
        auto text = host_text(w, "ui_battle_esper_change");
        for (const auto &[token, value] :
             {std::pair{U"{actor}", party_name(w, change->actor)},
              std::pair{U"{ability}", item_name(w, uint8_t(change->old_ability))},
              std::pair{U"{new_ability}", item_name(w, uint8_t(change->new_ability))}}) {
            size_t at = 0;
            while ((at = text.find(token, at)) != text.npos) {
                text.replace(at, std::char_traits<char32_t>::length(token), value);
                at += value.size();
            }
        }
        // The ordinary three-line battle box: the change on lines 1-2, the question and choice on line 3.
        // Only a sentence that cannot wrap into two lines grows the box upward, keeping every word.
        const auto rows = prompt_lines(text, 212);
        const int extra = std::max(0, int(rows.size()) - 2) * kBattleLogPitch;
        const int top = kBattleLogTop - extra, last = kBattleLogTop + 4 + 2 * kBattleLogPitch;
        c.window(0, top, 224, kBattleLogHeight + extra);
        for (size_t i = 0; i < rows.size(); ++i)
            c.text(rows[i], 5, top + 4 + int(i) * kBattleLogPitch, Ink::Text, 212);
        c.text(host_text(w, "ui_battle_esper_allow"), 5, last, Ink::Text, 110);
        c.text(host_text(w, "ui_battle_esper_yes"), 131, last, Ink::Text, 31);
        c.text(host_text(w, "ui_battle_esper_no"), 185, last, Ink::Text, 31);
        if (Session::of(w).esper_accepting())
            c.cursor(change->yes ? 120 : 174, last);
        cards(change->actor);
        return pixels;
    }
    if (auto_target_) {
        const auto &battlefield =
            selector_battlefield_[size_t((second ? 2 : 0) + (mode == emu::Wsc::ColorMode::Lcd))];
        if (battlefield.size() == pixels.size())
            std::copy_n(battlefield.begin(), kBattleLogTop * 224, pixels.begin());
        c.window(0, kBattleLogTop, 224, kBattleLogHeight, Window::Normal);
        c.text(key_text(w, TextKey::draw_options_screen_4), 5, 81, Ink::Text, 212);
        const bool on = w.ram[0xD843] != 0; // native save_choice, read only
        c.text(key_text(w, TextKey::draw_option_values_1), 17, 101, Ink::Text, 65);
        auto yes = key_text(w, TextKey::draw_option_values_5);
        while (!yes.empty() && (yes.front() == U' ' || yes.front() == U'　'))
            yes.erase(yes.begin());
        c.text(yes, 113, 101, Ink::Text, 65);
        c.cursor(on ? 102 : 6, 101);
        cards(-1);
        return pixels;
    }
    if (meat_) {
        const auto &battlefield = battlefield_[size_t((second ? 2 : 0) + (mode == emu::Wsc::ColorMode::Lcd))];
        if ((meat_ == 2 || meat_ == 3) && battlefield.size() == pixels.size())
            std::copy(battlefield.begin(), battlefield.begin() + 80 * 224, pixels.begin());
        // The GB picture: the GB's blank battle backdrop, with the native meat graphic (the palette 12 cells
        // draw_meat_reward puts on screen 2) redrawn over it in GB greys while the game shows it. The cells,
        // their tiles and colors are the WSC machine's: the GB picture's memory holds its own battle layer
        // there.
        if (second) {
            c.backdrop(0, 0, 224, 80);
            const uint16_t map = uint16_t(((w.io[7] >> 4) & 15) << 11);
            std::array<uint16_t, 16> colors{};
            for (int i = 0; i < 16; ++i)
                colors[size_t(i)] = word(w, uint16_t(0xFE00 + 12 * 32 + i * 2)) & 0x0FFF;
            for (int cell = 0; cell < 32 * 32; ++cell) {
                const uint16_t entry = word(w, uint16_t(map + cell * 2));
                if ((entry >> 9 & 15) != 12 || (entry & 0x1FF) < 0xC0 || (entry & 0x1FF) > 0xC3)
                    continue;
                const int x = ((cell % 32) * 8 - w.io[0x12]) & 255, y = ((cell / 32) * 8 - w.io[0x13]) & 255;
                const uint8_t *tile =
                    &w.ram[size_t((entry & 0x2000 ? 0x8000 : 0x4000) + (entry & 0x1FF) * 32)];
                if (x < 224 && y < 80)
                    c.clip(0, 0, 224, 80,
                           [&] { c.tile(tile, colors, 12, x, y, entry & 0x4000, entry & 0x8000); });
            }
        }
        const int eater = std::min<int>(w.ram[kEater], 3);
        const int choice = w.ram[kEatChoice] ? 1 : 0;
        const auto meat = host_text(w, "ui_battle_meat", monster_name(w, w.ram[kMeatType]));
        const auto eat = host_text(w, "ui_battle_meat_eat");
        const auto dont = host_text(w, "ui_battle_meat_dont");
        // The native preview (Config option) is drawn for a monster eater: its current and new forms.
        const bool preview =
            meat_ == 3 && (w.ram[kConfig] & 8) && (w.ram[uint16_t(0xD000 + eater * 31 + 10)] & 0x40);
        if (meat_ == 4) {
            // The transformation (or "no effect") and its native result message.
            c.window(0, kBattleLogTop, 224, kBattleLogHeight, Window::Normal);
            log(kBattleLogTop + 4);
        } else if (preview) {
            // The original's layout: a pane per form at the screen's sides, from the top down to the party
            // cards (current form left, resulting form right), the battlefield with the dropped meat between
            // them and the Eat/Don't window under it. Each pane: the name centered on its top text line, the
            // battle picture in a 48 px box under it (feet on the box's bottom), then the five stats one per
            // line, label left, values on one right edge. A changed value in the right pane takes Higher or
            // Lower ink with the matching mark in its own column after the values; no verdict line.
            const int before = w.ram[uint16_t(kActors + eater * 32 + 31)];
            const int after = w.ram[kUnchanged] == 1 ? before : w.ram[kNewForm];
            Machine m(w);
            const auto pane = [&](int x, int type, const std::array<int, 5> *compare) {
                const auto stats = form_stats(w, uint8_t(type));
                c.window(x, 0, kMeatPane, kMeatPaneHeight, Window::Normal);
                const auto name = monster_name(w, uint8_t(type));
                const int name_width = std::min(kMeatPane - 9, Chrome::text_width(name));
                c.text(name, x + (kMeatPane - name_width) / 2, kMeatNameY, Ink::Text, kMeatPane - 9);
                const auto art = monster_picture(m, uint8_t(type), second);
                c.clip(x, kMeatPictureTop, kMeatPane, kMeatPictureBox, [&] {
                    draw_monster_picture(c, art, x + kMeatPane / 2 - art.width / 2,
                                         kMeatPictureTop + kMeatPictureBox - art.height);
                });
                constexpr const char *labels[] = {"ui_battle_hp", "ui_party_str", "ui_party_def",
                                                  "ui_party_agi", "ui_party_mag"};
                for (int i = 0; i < 5; ++i) {
                    const int y = kMeatStatsY + i * kMeatStatPitch, value = stats[size_t(i)];
                    const int was = compare ? (*compare)[size_t(i)] : value;
                    const auto digits = std::to_string(value);
                    // Values end a mark's width (and a gap) short of the text edge in both panes.
                    const int edge = x + kMeatPane - 5 - kMeatMarkRoom;
                    c.text(host_text(w, labels[i]), x + 5, y, Ink::Text, kMeatLabelWidth);
                    c.numerals(digits, edge - Chrome::numerals_width(digits), y + 1,
                               value == was  ? Ink::Text
                               : value > was ? Ink::Higher
                                             : Ink::Lower);
                    if (value != was) // the mark's 4 rows centered on the 6-row numerals
                        c.trend_mark(x + kMeatPane - 5 - 8, y + 2, value > was);
                }
                return stats;
            };
            const auto old_stats = pane(0, before, nullptr);
            pane(224 - kMeatPane, after, &old_stats);
            // The native preview keeps its Eat/Don't window up while the eater is chosen.
            constexpr int middle = kMeatPane, width = 224 - 2 * kMeatPane, box = 80, line = 87;
            c.window(middle, box, width, kMeatPaneHeight - box, Window::Normal);
            if (arrow_kept_)
                c.cursor(middle + 4, line + choice * 10, arrow_);
            else
                c.cursor(middle + 4, line + choice * 10);
            c.text(eat, middle + 14, line, Ink::Text, width - 18);
            c.text(dont, middle + 14, line + 10, Ink::Text, width - 18);
        } else {
            // Every meat screen uses the battle log's window, so the drop message, the Eat/Don't choice and
            // the eater prompt line up and no row between the battlefield (to y=80) and the window shows
            // through.
            const int top = kBattleLogTop, line = top + 4;
            c.window(0, top, 224, kBattleLogHeight, Window::Normal);
            if (meat_ == 1) { // the drop announcement waits for A
                log(line);
                c.tile(uint16_t((15 << 9) | 0x1F0), 194, 102);
            } else if (meat_ == 2) {
                if (!second) {
                    arrow_ = c.arrow();
                    arrow_kept_ = true;
                }
                c.text(meat, 5, line, Ink::Text, 130);
                c.cursor(141, line + choice * 12);
                c.text(eat, 152, line, Ink::Text, 65);
                c.text(dont, 152, line + 12, Ink::Text, 65);
            } else {
                const bool refused = refused_eater_ >= 0 && refused_eater_ == w.ram[kEater];
                const auto prompt = host_text(w, refused ? "ui_battle_meat_refused" : "ui_battle_meat_prompt",
                                              monster_name(w, w.ram[kMeatType]));
                const auto lines = prompt_lines(prompt, 212);
                for (size_t i = 0; i < lines.size(); ++i)
                    c.text(lines[i], 5, line + int(i) * kBattleLogPitch, Ink::Text, 212);
            }
        }
        cards(meat_ == 3 ? eater : -1);
        return pixels;
    }
    if (rewards_) { // the victory, gold, item and growth messages, one after another
        c.window(0, kBattleLogTop, 224, kBattleLogHeight, Window::Normal);
        log(kBattleLogTop + 4);
        cards(-1);
        return pixels;
    }
    // The original backdrop ends at y=80. The party cards cover this pane's lower frame,
    // sharing their top divider and leaving room for three item rows without extending the floor.
    // The native row counter advances only after its typing wait returns. The first
    // victory popup must already be visible during that wait, before rewards clear it.
    const bool showing_log = !selecting && !message_.empty();
    c.window(0, showing_log ? kBattleLogTop : 80, 224, showing_log ? kBattleLogHeight : 40, Window::Normal);
    const int groups = std::clamp<int>(w.ram[kGroupCount], 0, 3);
    constexpr int centers[3][3] = {{112, 0, 0}, {72, 152, 0}, {48, 112, 176}};
    // Enemy names belong to enemy-target selection, not command/item or party-target menus.
    if (selecting && state == 2 && groups) {
        for (int g = 0; g < groups; ++g) {
            if (w.ram[uint16_t(kTarget + member)] != g)
                continue;
            const uint8_t count = w.ram[uint16_t(kGroups + g * 2 + 1)];
            if (!count)
                continue;
            auto label = name(w, NameTable::Monster, w.ram[uint16_t(kGroups + g * 2)]);
            if (count > 1)
                label += host_text(w, "ui_battle_group_count", ascii(std::to_string(count)));
            const int width = std::min(220, c.text_width(label) + 12);
            const int x = std::clamp(centers[groups - 1][g] - width / 2, 2, 222 - width);
            const int y = 62; // The hovered plate sits at the enemy's feet, above the lower panel.
            const bool selected = state == 2 && w.ram[uint16_t(kTarget + member)] == g;
            c.window(x, y, width, 16, selected ? Window::Normal : Window::Inactive,
                     // GB: see-through; WSC: opaque, fading to 60% see-through toward Enhanced
                     second ? transparency_ : int(std::lround(60.f * enhanced_weight_)));
            c.text(label, x + 5, y + 4, selected ? Ink::Accent : Ink::Text, width - 9,
                   selected ? Window::Normal : Window::Inactive);
        }
    }
    if (selecting && (state == 0 || state == 1)) {
        int row =
            state == 0 ? (w.ram[0xDC61] == 2 ? 1 : 0) : std::min<int>(w.ram[uint16_t(kRow + member)], 2);
        c.cursor(6, 84 + row * 9, true, state == 0 ? 7 : 8);
        for (int i = 0; i < 3; ++i) {
            const int y = 84 + i * 9;
            if (state == 0) {
                if (i < 2)
                    c.text(host_text(w, i == 0 ? "ui_battle_fight" : "ui_battle_run"), 17, y,
                           i == row ? Ink::Text : Ink::Muted);
                continue;
            }
            const int slot = std::min<int>(7, w.ram[uint16_t(kPage + member)] + i);
            const uint16_t at = uint16_t(kActors + member * 32 + 14 + slot * 2);
            const int uses = w.ram[uint16_t(at + 1)];
            auto label =
                uses == 255 ? host_text(w, "ui_battle_empty_slot") : name(w, NameTable::Item, w.ram[at]);
            c.item(uses == 255 ? -1 : w.ram[at], label, 17, y, i == row ? Ink::Text : Ink::Muted, 170);
            auto amount = uses >= 254 ? host_text(w, "ui_battle_no_uses") : ascii(std::to_string(uses));
            c.text(amount, 206 - c.text_width(amount), y, i == row ? Ink::Accent : Ink::Muted);
        }
        if (state == 1) {
            const int first = w.ram[uint16_t(kPage + member)];
            if (first > 0)
                c.more_above(212, 83);
            if (first + 3 < 8)
                c.more_below(212, 104); // above the party cards; counts leave the same right gutter
        }
    } else if (const auto preview = action_preview(w)) {
        c.item(preview->item, preview->name, 17, 84, Ink::Text, 170);
        c.text(preview->uses, 216 - c.text_width(preview->uses), 84, Ink::Accent);
        // The list's name pen is 17 + the 10 px icon cell: first text ink at x=28.
        c.name(preview->actor, 27, 93, Ink::Text, 66);
        const int arrow = 27 + std::min(66, c.text_width(preview->actor)) + 4;
        c.text(U"\u25B8", arrow, 93, Ink::Text);
        const int target = arrow + c.text_width(U"\u25B8") + 3;
        c.text(preview->target, target, 93, Ink::Text, 216 - target);
        c.text(preview->details, 27, 102, Ink::Secondary, 189);
    } else if (showing_log) {
        log(kBattleLogTop + 4);
    }
    cards(!selecting ? -1 : state == 4 ? int(w.ram[uint16_t(kTarget + member)]) : member);
    for (const auto &n : numbers_) {
        const int age = int(w.frame_count - n.frame);
        int x = 0, y = 0;
        if (n.actor < 4) {
            x = 10 + n.actor * 56;
            y = 108;
        } else {
            const uint8_t type = w.ram[uint16_t(kActors + n.actor * 32 + 31)];
            int group = 0;
            for (int g = 0; g < groups; ++g)
                if (w.ram[uint16_t(kGroups + g * 2)] == type)
                    group = g;
            x = groups ? centers[groups - 1][group] : 112;
            y = 25;
        }
        auto label = ascii(std::to_string(std::abs(n.amount)));
        c.text(label, std::max(0, x - c.text_width(label) / 2), y - age / 5,
               n.amount > 0 ? Ink::Text : Ink::Heal);
    }
    return pixels;
}
} // namespace sawgod::game
