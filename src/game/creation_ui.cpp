// Character-creation snapshots, name entry, and host-drawn selection panels.
#include "game/creation_ui.h"

#include <algorithm>
#include <cmath>

#include "game/player_names.h"
#include "game/session.h"
#include "game/tables.h"
#include "game/text_render.h"
#include "game/ui_chrome.h"
#include "game/ui_sprites.h"

namespace sawgod::game {
namespace {
using C = Chrome;
using S = std::u32string;
// Native creation and name-entry state (bank34_yields.cpp: new_game_setup_core, edit_creation_name_core).
constexpr uint16_t kTypeSelection = 0xD841, kTypeRow = 0xD853, kNameLength = 0xD855, kNameBytes = 0xD856;
constexpr uint16_t kCreationTypes = 0xC703; // 4000:, eight type bytes per row of kTypeRow
constexpr uint16_t kWindowPalette = 0x0000, kScreenPalette = 0xFE00 + 15 * 32; // 9000:, and its place
constexpr uint16_t kStart = 2, kA = 4, kB = 8, kUp = 0x10, kRight = 0x20, kDown = 0x40, kLeft = 0x80;
constexpr uint16_t kY2 = 0x200, kY4 = 0x800;
// The grids' pixel layout (columns widened so the 8 px arrow has its own gutter left
// of every key, clear of its neighbor's ink): pen x and pitch per column, first row's y, button pens.
constexpr int kLatinX = 14, kLatinPitch = 16, kKanaX = 22, kKanaPitch = 18, kGutter = 8;
constexpr int kLatinY = 34, kKanaY = 54, kPitchY = 12;
constexpr int kKanaColumns = 10, kKanaRows = 5;
constexpr std::array<int, 3> kButtonX = {17, 89, 165};
constexpr std::array<const char *, 3> kButtons = {"ui_name_entry_space", "ui_name_entry_delete",
                                                  "ui_name_entry_done"};
// English rows: uppercase on two, lowercase from a new row, digits and punctuation.
constexpr std::array<std::u32string_view, 6> kLatinRows = {
    U"ABCDEFGHIJKLM", U"NOPQRSTUVWXYZ", U"abcdefghijklm", U"nopqrstuvwxyz", U"0123456789", U".,!?'-:/()"};

S number(int n) { return utf8_to_u32(std::to_string(n)); }
void right(C &c, const S &s, int edge, int y, C::Ink ink = C::Ink::Text) {
    c.text(s, edge - C::text_width(s), y, ink, C::text_width(s));
}
char32_t folded(char32_t c) { return c >= U'！' && c <= U'～' ? c - U'！' + U'!' : c; }
S decoded(Machine &m, TextPtr p, int length) { return decoded_text(m, p.segment, p.offset, length); }
CreationUi::Type read_type(emu::Wsc &w, Machine &m, uint8_t id) {
    CreationUi::Type t;
    t.id = id;
    t.name = type_name(m, id);
    // creation_template_core: the monster record at 6000:[D1FB + 2 * type].
    const uint16_t rec = table_at16(m, Table::monsters, 0x6000, uint16_t(0xD1FB + id * 2));
    const uint8_t kind = table_at(m, Table::monsters, 0x6000, rec);
    t.monster = kind & 0x80;
    t.mutant = !t.monster && (kind & 0x40);
    t.hearts = 3; // the template's flags byte: 3 | class bits
    t.hp = table_at16(m, Table::monsters, 0x6000, uint16_t(rec + 3));
    for (int i = 0; i < 4; ++i)
        t.stats[size_t(i)] = std::min(99, int(table_at(m, Table::monsters, 0x6000, uint16_t(rec + 5 + i))));
    t.gear_count = std::min(8, int(table_at(m, Table::monsters, 0x6000, uint16_t(rec + 2))));
    for (int i = 0; i < t.gear_count; ++i) {
        const uint8_t item = table_at(m, Table::monsters, 0x6000, uint16_t(rec + 11 + i));
        t.gear[size_t(i)] = item_row(w, item, item >= 252 ? 255 : table_u8(m, Table::item_records, item, 3));
    }
    t.sprite = type_sprite(m, id);
    return t;
}
// How many steps darker the screen is than its window palette (new_game_setup_core loads palette 15 from
// 9000:0000; fade_palettes_16 darkens every channel a step a frame): the host-read sprites fade with it.
int fade_steps(emu::Wsc &w, Machine &m) {
    int steps = 0;
    for (int i = 0; i < 16; ++i) {
        const int own = m.rd16(0x9000, uint16_t(kWindowPalette + i * 2)) & 0x0FFF;
        const int now = w.ram[size_t(kScreenPalette + i * 2)] | w.ram[size_t(kScreenPalette + i * 2 + 1)]
                                                                    << 8;
        for (int shift = 0; shift < 12; shift += 4) {
            if ((now >> shift & 15) > (own >> shift & 15))
                return 0; // another palette there: no fade to follow
            steps = std::max(steps, (own >> shift & 15) - (now >> shift & 15));
        }
    }
    return steps;
}
void sprite(C &c, const CreationUi::Type &t, int fade, int x, int y, bool second) {
    draw_type_sprite(c, t.sprite, x, y, second, fade);
}
void stats(C &c, emu::Wsc &w, const CreationUi::Type &t, int x, int y, int edge) {
    constexpr const char *labels[] = {"ui_party_str", "ui_party_def", "ui_party_agi", "ui_party_mag"};
    c.text(host_text(w, "ui_creation_hp"), x, y, C::Ink::Text, 20);
    right(c, number(t.hp), edge, y);
    for (int i = 0; i < 4; ++i) {
        c.text(host_text(w, labels[i]), x, y + 12 * (i + 1), C::Ink::Text, 38);
        right(c, number(t.stats[size_t(i)]), edge, y + 12 * (i + 1));
    }
}
int key_pitch(const CreationUi::Page &p, int row) {
    // The added native-symbol row needs room for full-width glyphs and their cursor gutters.
    return p.kana ? kKanaPitch : row == 6 ? 2 * kLatinPitch : kLatinPitch;
}
int key_x(const CreationUi::Page &p, int column, int row) {
    return (p.kana ? kKanaX : kLatinX) + column * key_pitch(p, row);
}
int key_y(const CreationUi::Page &p, int row) { return (p.kana ? kKanaY : kLatinY) + row * kPitchY; }
} // namespace

void CreationUi::reset() {
    creation_ = naming_ = detail_ = false;
    first_ = gear_ = page_ = row_ = column_ = button_ = 0;
    line_.reset();
    pending_ = -1;
    scene_ = Scene::Hidden;
}

void CreationUi::install(emu::Wsc &w) {
    subscriptions_.clear();
    reset();
    pages_.clear();
    generation_ = Session::of(w).generation();
    Session::of(w).player_names().enabled = true; // eight-glyph host names, as the other screens show them
    auto &h = Session::of(w).hooks();
    subscriptions_.push_back(h.subscribe(hooks::Event::CreationMenu, [this, &w](Machine &, const auto &a) {
        renew(w);
        // Name entry suspends the list (0, NameEntry 1 .. 0, then 1): the list resumes as it was left.
        if (a.a && !resuming_) {
            detail_ = false;
            first_ = gear_ = 0;
        }
        resuming_ = false;
        creation_ = a.a != 0;
        scene_ = naming_ ? Scene::Name : creation_ ? Scene::Types : Scene::Hidden;
    }));
    subscriptions_.push_back(h.subscribe(hooks::Event::NameEntry, [this, &w](Machine &, const auto &a) {
        renew(w);
        naming_ = a.a != 0;
        resuming_ = !naming_;
        if (naming_) {
            build_pages(w);
            page_ = row_ = column_ = button_ = 0;
        }
        pending_ = -1;
        scene_ = naming_ ? Scene::Name : creation_ ? Scene::Types : Scene::Hidden;
    }));
    subscriptions_.push_back(
        h.subscribe(hooks::Value::CreationKeys, [this, &w](Machine &, int32_t v, const auto &) {
            return int32_t(creation_keys(w, uint16_t(v)));
        }));
    subscriptions_.push_back(
        h.subscribe(hooks::Value::NameEntryKeys, [this, &w](Machine &, int32_t v, const auto &) {
            return int32_t(name_keys(w, uint16_t(v)));
        }));
    subscriptions_.push_back(h.subscribe(hooks::Value::NameEntryGlyph,
                                         [this](Machine &, int32_t v, const auto &) { return glyph(v); }));
}

void CreationUi::renew(emu::Wsc &w) {
    if (generation_ != Session::of(w).generation()) {
        generation_ = Session::of(w).generation();
        reset();
        resuming_ = false;
    }
}

void CreationUi::frame(emu::Wsc &w) {
    renew(w);
    if (scene_ == Scene::Types && detail_)
        types(w); // Advance on game frames even when headless is not drawing a picture.
}

void CreationUi::build_pages(emu::Wsc &w) {
    static const auto charset = Charset::system();
    Machine m(w);
    pages_.clear();
    latin_only_ = Session::of(w).text().proportional();
    // The native sheets (9 x 10 codes a page): every glyph a key here adds is the byte the original's key
    // for the same glyph stores.
    std::array<std::vector<uint8_t>, 3> sheets;
    for (int page = 0; page < 3; ++page) {
        const TextPtr p = text_at(m, name_entry_sheet(uint8_t(page)));
        for (int i = 0; i < 90; ++i)
            sheets[size_t(page)].push_back(m.rd8(p.segment, uint16_t(p.offset + i)));
    }
    const auto shown = [&](uint8_t code) {
        const S u = utf8_to_u32(charset.decode(code));
        return u.size() == 1 ? folded(u[0]) : char32_t(0);
    };
    const auto native = [&](char32_t glyph) -> int {
        for (const auto &sheet : sheets)
            for (uint8_t code : sheet)
                if (code != 255 && shown(code) == glyph)
                    return code;
        return -1;
    };
    space_ = native(U'　');
    if (space_ < 0)
        space_ = 0xD9;
    if (!latin_only_)
        for (int kana = 1; kana <= 2; ++kana) {
            std::vector<Key> keys;
            for (uint8_t code : sheets[size_t(kana - 1)])
                if (code != 255 && int(code) != space_)
                    keys.push_back({char32_t(0xE000 + code), code, 0});
            const int per_page = kKanaColumns * kKanaRows;
            const int parts = std::max(1, int(keys.size() + per_page - 1) / per_page);
            for (int part = 0; part < parts; ++part) {
                Page p;
                p.kana = kana;
                p.part = part;
                p.parts = parts;
                p.columns = kKanaColumns;
                for (int i = part * per_page; i < std::min<int>(int(keys.size()), (part + 1) * per_page);
                     ++i) {
                    if ((i - part * per_page) % kKanaColumns == 0)
                        p.rows.emplace_back();
                    p.rows.back().push_back(keys[size_t(i)]);
                }
                pages_.push_back(std::move(p));
            }
        }
    // Glyphs the sheets lack keep the glyph in the host name; the machine bytes hold the nearest one.
    constexpr std::pair<char32_t, char32_t> kNearest[] = {{U'.', U'。'}, {U',', U'、'}, {U'\'', U'　'},
                                                          {U'-', U'ー'}, {U'(', U'「'}, {U')', U'」'}};
    Page latin;
    for (auto row : kLatinRows) {
        latin.rows.emplace_back();
        for (char32_t g : row) {
            Key k{g, 0, 0};
            if (const int code = native(g); code >= 0) {
                k.code = uint8_t(code);
            } else {
                char32_t near = g >= U'a' && g <= U'z' ? g - U'a' + U'A' : U'　';
                for (auto [from, to] : kNearest)
                    if (from == g)
                        near = to;
                const int projected = native(near);
                k.code = uint8_t(projected >= 0 ? projected : space_);
                k.text = g;
            }
            latin.rows.back().push_back(k);
        }
    }
    pages_.push_back(std::move(latin));
    // Add the remaining original Latin-sheet codes in a seventh row inside the existing grid.
    // Use the WSC glyph directly, including the two codes without a Unicode mapping.
    constexpr uint8_t kExtraLatin[] = {0xC5, 0xCA, 0xCD, 0xCF, 0xD0, 0xD1};
    pages_.back().rows.emplace_back();
    for (uint8_t code : kExtraLatin)
        pages_.back().rows.back().push_back({char32_t(0xE000 + code), code, 0});
}

const std::vector<CreationUi::Page> &CreationUi::pages(emu::Wsc &w) {
    if (pages_.empty())
        build_pages(w);
    return pages_;
}

void CreationUi::clamp_cursor() {
    page_ = std::clamp(page_, 0, int(pages_.size()) - 1);
    row_ = std::clamp(row_, 0, int(pages_[size_t(page_)].rows.size()));
    button_ = std::clamp(button_, 0, 2);
}

const CreationUi::Key *CreationUi::key_at() const {
    const auto &rows = pages_[size_t(page_)].rows;
    if (row_ >= int(rows.size()))
        return nullptr;
    const auto &row = rows[size_t(row_)];
    return &row[size_t(std::min(column_, int(row.size()) - 1))];
}

uint16_t CreationUi::creation_keys(emu::Wsc &w, uint16_t keys) {
    renew(w);
    // Rows of six and more use Left/Right natively (new_game_setup_core): no detail page there.
    if (scene_ != Scene::Types || w.ram[kTypeRow] >= 6)
        return keys;
    // The native loop acts on B, then A, then Up/Down; Left/Right do nothing in it.
    if (detail_) {
        if (keys & kB || (!(keys & (kA | kUp | kDown)) && keys & kLeft)) {
            detail_ = false;
            return 0;
        }
        // Up/Down walk the gear rows here, around the ends as the inventory list does; the type changes
        // on the list only, so the native loop never sees them (nor moves or sounds for them).
        if (!(keys & kA) && keys & (kUp | kDown)) {
            Machine m(w);
            const uint8_t id = m.rd8(0x4000, uint16_t(kCreationTypes + w.ram[kTypeRow] * kTypes +
                                                      std::clamp(int(w.ram[kTypeSelection]), 0, kTypes - 1)));
            if (const int rows = read_type(w, m, id).gear_count)
                gear_ = (std::min(gear_, rows - 1) + (keys & kUp ? rows - 1 : 1)) % rows;
        }
        return uint16_t(keys & ~(kLeft | kRight | kUp | kDown));
    }
    if (!(keys & (kB | kA | kUp | kDown)) && keys & kRight) {
        detail_ = true;
        gear_ = 0;
        return 0;
    }
    return keys;
}

uint16_t CreationUi::name_keys(emu::Wsc &w, uint16_t keys) {
    renew(w);
    pending_ = -1;
    if (scene_ != Scene::Name || pages_.empty())
        return keys;
    clamp_cursor();
    const auto &page = pages_[size_t(page_)];
    const int rows = int(page.rows.size());
    const auto button_for = [&](int column) {
        const int x = key_x(page, column, row_);
        int best = 0;
        for (int b = 1; b < 3; ++b)
            if (std::abs(kButtonX[size_t(b)] - x) < std::abs(kButtonX[size_t(best)] - x))
                best = b;
        return best;
    };
    const auto column_for = [&](int button) {
        const int pitch = key_pitch(page, row_);
        return std::clamp(int(std::lround(double(kButtonX[size_t(button)] - key_x(page, 0, row_)) / pitch)),
                          0, page.columns - 1);
    };
    // The native editor acts on the first of Start, B, A, Up/Down, Left/Right, Y4/Y2 its keys hold; every
    // key goes on to it (its own hidden cursor moves and sounds as the original's), A on a button becoming
    // that button's key.
    if (keys & (kStart | kB))
        return keys;
    if (keys & kA) {
        if (row_ < rows) {
            const Key *k = key_at();
            pending_ = int32_t(k->code | uint32_t(k->text) << 8);
            return keys;
        }
        if (button_ == 0) {
            pending_ = int32_t(space_ | (latin_only_ ? uint32_t(U' ') << 8 : 0));
            return keys;
        }
        if (button_ == 1) // Delete: never the native B's leave-when-empty
            return w.ram[kNameLength] ? uint16_t((keys & ~kA) | kB) : 0;
        return uint16_t((keys & ~kA) | kStart);
    }
    if (keys & (kUp | kDown)) {
        const bool up = keys & kUp;
        if (row_ == rows) {
            row_ = up ? rows - 1 : 0;
            column_ = column_for(button_);
        } else if ((up && row_ == 0) || (!up && row_ == rows - 1)) {
            button_ = button_for(std::min(column_, int(page.rows[size_t(row_)].size()) - 1));
            row_ = rows;
        } else {
            row_ += up ? -1 : 1;
        }
        return keys;
    }
    if (keys & (kLeft | kRight)) {
        const int step = keys & kLeft ? -1 : 1;
        if (row_ == rows) {
            button_ = (button_ + step + 3) % 3;
        } else {
            const int length = int(page.rows[size_t(row_)].size());
            column_ = (std::min(column_, length - 1) + step + length) % length;
        }
        return keys;
    }
    if (keys & (kY2 | kY4)) {
        const int n = int(pages_.size());
        const bool on_buttons = row_ == rows;
        page_ = (page_ + (keys & kY4 ? -1 : 1) + n) % n;
        const int now = int(pages_[size_t(page_)].rows.size());
        row_ = on_buttons ? now : std::min(row_, now - 1);
    }
    return keys;
}

int32_t CreationUi::glyph(int32_t original) {
    if (scene_ != Scene::Name || pending_ < 0)
        return original;
    const int32_t chosen = pending_;
    pending_ = -1;
    return chosen;
}

CreationUi::TypesView CreationUi::types(emu::Wsc &w) {
    renew(w);
    Machine m(w);
    TypesView snapshot;
    const int base = w.ram[kTypeRow] * kTypes;
    snapshot.selected = std::clamp(int(w.ram[kTypeSelection]), 0, kTypes - 1);
    if (snapshot.selected < first_)
        first_ = snapshot.selected;
    if (snapshot.selected >= first_ + kVisibleTypes)
        first_ = snapshot.selected - kVisibleTypes + 1;
    snapshot.first = first_ = std::clamp(first_, 0, kTypes - kVisibleTypes);
    snapshot.detail = detail_;
    for (int i = 0; i < kTypes; ++i)
        snapshot.types[size_t(i)] = read_type(w, m, m.rd8(0x4000, uint16_t(kCreationTypes + base + i)));
    snapshot.fade = fade_steps(w, m);
    const auto &selected = snapshot.types[size_t(snapshot.selected)];
    snapshot.gear = gear_ = std::clamp(gear_, 0, std::max(0, selected.gear_count - 1));
    if (snapshot.gear < selected.gear_count)
        snapshot.hovered = selected.gear[size_t(snapshot.gear)];
    if (detail_)
        line_.update(w.frame_count, selected.id * 8 + snapshot.gear, snapshot.hovered.flavor,
                     snapshot.hovered.stats);
    else
        line_.reset();
    return snapshot;
}

CreationUi::NameView CreationUi::name(emu::Wsc &w) {
    NameView snapshot;
    pages(w);
    clamp_cursor();
    snapshot.page = page_;
    snapshot.pages = int(pages_.size());
    snapshot.row = row_;
    snapshot.buttons = row_ == int(pages_[size_t(page_)].rows.size());
    snapshot.button = button_;
    if (!snapshot.buttons)
        snapshot.column = std::min(column_, int(pages_[size_t(page_)].rows[size_t(row_)].size()) - 1);
    const auto &names = Session::of(w).player_names();
    if (names.enabled) {
        snapshot.name = names.draft_text();
        snapshot.length = int(names.draft.size());
    } else {
        Machine m(w);
        snapshot.length = std::min(4, int(w.ram[kNameLength]));
        snapshot.name = decoded(m, {0, kNameBytes}, snapshot.length);
    }
    return snapshot;
}

std::vector<uint32_t> CreationUi::picture(emu::Wsc &w, emu::Wsc::ColorMode mode, bool second) {
    renew(w);
    auto px = w.framebuffer_rgb(mode, second);
    if (!active())
        return px;
    C c(w, px, mode, second);
    if (!c.ready())
        return px;
    // Opaque full screen; the native fills also back the GB's rounded corners.
    c.window(0, 0, 224, 144);
    if (scene_ == Scene::Name) {
        const auto snapshot = name(w);
        const auto &page = pages_[size_t(snapshot.page)];
        c.window(0, 0, 80, 28);
        c.text(host_text(w, "ui_name_entry_label"), 5, 5, C::Ink::Text, 68);
        c.window(80, 0, 144, 28);
        c.text(snapshot.name, 85, 5, C::Ink::Text, 132);
        right(c, number(snapshot.length) + U"/" + number(int(kPlayerNameGlyphs)), 217, 17);
        c.window(0, 28, 224, 100);
        if (page.kana) {
            const S label = host_text(w, page.kana == 1 ? "ui_name_entry_hiragana" : "ui_name_entry_katakana",
                                      number(page.part + 1) + U"/" + number(page.parts));
            const int width = std::min(C::text_width(label), 180);
            c.text(label, (224 - width) / 2, 34, C::Ink::Text, 180);
            c.tile(uint16_t(0x1EE | 7 << 9 | 0x4000), 6, 36, false, true); // the list arrow, mirrored
            c.cursor(208, 36, false);
        }
        for (size_t r = 0; r < page.rows.size(); ++r)
            for (size_t k = 0; k < page.rows[r].size(); ++k)
                c.text(S(1, page.rows[r][k].shown), key_x(page, int(k), int(r)), key_y(page, int(r)),
                       C::Ink::Text, 12);
        c.window(0, 128, 224, 16);
        constexpr int kButtonY = 132; // 1 px higher than first drawn, centering the row in its window
        for (int b = 0; b < 3; ++b)
            c.text(host_text(w, kButtons[size_t(b)]), kButtonX[size_t(b)], kButtonY, C::Ink::Text,
                   b == 2 ? 50 : 56);
        if (snapshot.buttons)
            c.cursor(kButtonX[size_t(snapshot.button)] - 12, kButtonY);
        else {
            // Its own 8 px gutter: 1 px before the key. Latin keys are 16 px apart, so after a seven-pixel
            // letter (W, w, M, m) there is one pixel in all: there the arrow keeps clear of that letter
            // and meets its own key instead (N, n, X, x).
            const bool after_wide =
                !page.kana && snapshot.column > 0 && key_pitch(page, snapshot.row) == kLatinPitch &&
                C::text_width(S(1, page.rows[size_t(snapshot.row)][size_t(snapshot.column - 1)].shown)) > 8;
            c.cursor(key_x(page, snapshot.column, snapshot.row) - kGutter + after_wide,
                     key_y(page, snapshot.row));
        }
        return px;
    }
    Machine m(w);
    const auto snapshot = types(w);
    const auto &selected = snapshot.types[size_t(snapshot.selected)];
    c.window(0, 0, 224, 16);
    c.text(native_text(m, TextKey::draw_creation_type_screen_1), 5, 5, C::Ink::Text, 212);
    if (snapshot.detail) {
        // The selected type's starting stats and every starting gear (or ability) slot, on the inventory's
        // row rhythm, with its own cursor and the shared description line under it.
        c.window(0, 16, 60, DetailLine::kY - 16);
        c.window(60, 16, 164, DetailLine::kY - 16);
        sprite(c, selected, snapshot.fade, 21, 24, second);
        stats(c, w, selected, 5, 50, 54);
        for (int i = 0; i < selected.gear_count; ++i) {
            const auto &row = selected.gear[size_t(i)];
            const int y = 26 + i * 12;
            const bool fixed = selected.monster || (selected.mutant && i < 4);
            const auto ink = fixed ? C::Ink::Muted : C::Ink::Text;
            const bool unlimited = row.count == host_text(w, "ui_battle_no_uses");
            const int count_width =
                unlimited ? C::text_width(row.count) : C::numerals_width(name_utf8(row.count));
            c.item(row.id, row.name, 81, y, ink, 216 - 81 - count_width - 3);
            if (unlimited)
                right(c, row.count, 216, y, ink);
            else if (!row.count.empty())
                c.numerals(name_utf8(row.count), 216 - count_width, y + 1, ink);
        }
        if (selected.gear_count)
            c.cursor(66, 26 + snapshot.gear * 12, true, 8);
        line_.draw(c);
        return px;
    }
    // The list pane is 5 px wider than half the screen: two-word type names fit in 74 px.
    c.window(0, 16, 117, 128);
    c.window(117, 16, 107, 128);
    for (int i = 0; i < kVisibleTypes; ++i) {
        const auto &t = snapshot.types[size_t(snapshot.first + i)];
        sprite(c, t, snapshot.fade, 17, 20 + i * 16, second);
        c.text(t.name, 37, 24 + i * 16, C::Ink::Text, 74);
    }
    c.cursor(6, 24 + (snapshot.selected - snapshot.first) * 16);
    // The list's other choice: below while the first seven show, above once it scrolls.
    c.tile(uint16_t(0x1F0 | 15 << 9 | (snapshot.first + kVisibleTypes < kTypes ? 0x8000 : 0)), 101, 134);
    sprite(c, selected, snapshot.fade, 122, 20, second);
    c.text(selected.name, 142, 24, C::Ink::Text, 74);
    stats(c, w, selected, 122, 68, 171);
    c.tile(uint16_t(15 << 9 | 0x1D1), 188, 68);
    c.text(U"×", 195, 68, C::Ink::Text, 10); // 10 px, then the count, as on the party rows
    c.numerals(std::to_string(selected.hearts), 205, 69);
    return px;
}

} // namespace sawgod::game
