#pragma once
#include "enhanced_picture.h"
#include "game/text_tokens.h"
#include "pilot_support.h"
#include <algorithm>
#include <cstdlib>
#include <iterator>
namespace ui_pilot {
struct RenderResult {
    std::string id, variant, scene, file;
    std::vector<std::string> errors;
    int draws = 0;
};
inline bool intersects(const game::Chrome::Draw &a, const game::Chrome::Draw &b) {
    const int al = a.kind == game::Chrome::Draw::Kind::Text ? a.left : a.x;
    const int at = a.kind == game::Chrome::Draw::Kind::Text ? a.top : a.y;
    const int ar = a.kind == game::Chrome::Draw::Kind::Text ? a.right : a.x + a.width;
    const int ab = a.kind == game::Chrome::Draw::Kind::Text ? a.bottom : a.y + a.height;
    const int bl = b.kind == game::Chrome::Draw::Kind::Text ? b.left : b.x;
    const int bt = b.kind == game::Chrome::Draw::Kind::Text ? b.top : b.y;
    const int br = b.kind == game::Chrome::Draw::Kind::Text ? b.right : b.x + b.width;
    const int bb = b.kind == game::Chrome::Draw::Kind::Text ? b.bottom : b.y + b.height;
    return al < ar && at < ab && bl < br && bt < bb && al < br && ar > bl && at < bb && ab > bt;
}
// A pointer is over a text when a pixel of the text's ink lies inside the pointer's cell (numerals, which
// record no ink: when the boxes meet). The text's whole box is too coarse: a line's descender rows reach the
// rows of a pointer far to its side.
inline bool pointer_over_text(const game::Chrome::Draw &pointer, const game::Chrome::Draw &text) {
    if (!intersects(pointer, text))
        return false;
    if (text.ink.empty())
        return true;
    return std::any_of(text.ink.begin(), text.ink.end(), [&](uint32_t at) {
        const int x = int(at % 224), y = int(at / 224);
        return x >= pointer.x && x < pointer.x + pointer.width && y >= pointer.y &&
               y < pointer.y + pointer.height;
    });
}
// Two texts collide when their ink boxes intersect -- except lines on different rows whose boxes meet only
// by the few rows a descender and its shadow hang into the next line (9 px rows; this reads fine): those
// collide only if the later text (b) draws over a stroke of the earlier one.
// (A stroke over the earlier line's shadow changes nothing one can see.)
inline bool text_collides(const game::Chrome::Draw &a, const game::Chrome::Draw &b) {
    if (!intersects(a, b))
        return false;
    const int rows = std::min(a.bottom, b.bottom) - std::max(a.top, b.top);
    if (a.y == b.y || rows > 3 || a.ink.empty() || b.ink.empty())
        return true;
    std::vector<uint32_t> both;
    std::set_intersection(a.strokes.begin(), a.strokes.end(), b.ink.begin(), b.ink.end(),
                          std::back_inserter(both));
    return !both.empty();
}

inline void ppm(const std::filesystem::path &path, const std::vector<uint32_t> &px) {
    std::ofstream f(path, std::ios::binary);
    require(bool(f), "cannot write " + path.string());
    f << "P6\n224 144\n255\n";
    for (uint32_t p : px) {
        f.put(char(p >> 16));
        f.put(char(p >> 8));
        f.put(char(p));
    }
    require(bool(f), "picture write failed");
}

// ---- Pointer alignment and text room -------------------------------------------------------------
// "arrow looks 'uncentered' basically everywhere, that positioning check will be key". Every arrow cursor
// and every hand that points at something (Chrome::Draw::pointer) is judged against what it points at:
//   (a) a hand targeting a picture without text has exactly the same visible top row as that picture.
//       A row with a sprite/icon and a name counts as text and keeps the settled fingertip rule;
//   (b) otherwise its tip -- the middle of the ink in its rightmost column: the arrow's point, the hand's
//   fingertip --
//       is level, within 1 px, with the vertical middle of its target: the item icon's 8 px cell when the row
//       has one, otherwise the capitals of the row's text (seven rows from the text's y; kana fill the same
//       seven; descenders do not count), or the picture (sprite, portrait) it points at;
//   (c) for an odd-height target whose middle is one pixel row, the arrow's black tip row (tile row 4,
//       the corresponding WSC shade) is exactly on that row; even-height targets straddle their middle.
//       WSC/Enhanced art adds 1 px below this shared anchor; GB keeps the original midpoint;
//   (d) the column left of the pointer is clear of frames and other ink, and at least one clear column
//       separates its tip from its target. Two pointers may never share a row.
// Decided exceptions: Bestiary grid columns 2-6 may overlap their left neighbor; Latin name-grid arrows
// after W w M m may touch the preceding key. The pilots scope these spacing exceptions; alignment is still
// checked. Bestiary detail keeps eight rows; name entry keeps 13 keys/two lines per alphabet.
// ScrollMarker is not a cursor or target (the Equip down arrow only means more rows below).
// The target is the nearest thing right of the tip on the tip's own line: an item cell, a text, or a picture
// drawn as tiles. A pointer with nothing to its right (an empty slot that draws no row) is not judged.
struct Box {
    int left = 224, top = 144, right = 0, bottom = 0; // half-open
    bool empty() const { return left >= right || top >= bottom; }
    void add(int x, int y) {
        left = std::min(left, x), top = std::min(top, y);
        right = std::max(right, x + 1), bottom = std::max(bottom, y + 1);
    }
};
inline Box ink_box(const std::vector<uint32_t> &ink) {
    Box b;
    for (auto at : ink)
        b.add(int(at % 224), int(at / 224));
    return b;
}
// Frame pixels as the picture shows them: each window's border band (2 px in the WSC picture, 3 px in the
// GB one), in drawing order -- a later window's inside covers the frames under it.
inline std::vector<uint8_t> frame_mask(const std::vector<game::Chrome::Draw> &draws, bool gb) {
    std::vector<uint8_t> mask(224 * 144, 0);
    const int band = gb ? 3 : 2;
    for (const auto &d : draws) {
        if (d.kind != game::Chrome::Draw::Kind::Window)
            continue;
        for (int y = std::max(0, d.y); y < std::min(144, d.y + d.height); ++y)
            for (int x = std::max(0, d.x); x < std::min(224, d.x + d.width); ++x)
                mask[size_t(y * 224 + x)] = x - d.x < band || d.x + d.width - x <= band || y - d.y < band ||
                                            d.y + d.height - y <= band;
    }
    return mask;
}
struct PointerTarget {
    Box box;
    int center2 = 0; // twice the y of its vertical middle
    int left = -1;   // its first ink column, -1: nothing drawn there (an empty item cell)
    std::string what;
    int ink_top = -1; // picture art's first nonzero-index row; its tile cell may start with blank padding
};
inline std::vector<PointerTarget> pointer_targets(const std::vector<game::Chrome::Draw> &draws) {
    using Kind = game::Chrome::Draw::Kind;
    std::vector<PointerTarget> targets;
    Box group, art; // tiles drawn one after another, row by row, are one picture
    int last_x = 0, last_y = 0;
    const auto close = [&] {
        if (!group.empty())
            targets.push_back({group, group.top + group.bottom - 1, group.left, "picture",
                               art.empty() ? group.top : art.top});
        group = {}, art = {};
    };
    for (const auto &d : draws) {
        if (d.kind != Kind::Tile) {
            close();
        } else {
            if (!group.empty() &&
                !((d.x == last_x + 8 && d.y == last_y) || (d.x == group.left && d.y == last_y + 8)))
                close();
            group.add(d.x, d.y);
            group.add(d.x + 7, d.y + 7);
            for (auto at : d.ink)
                art.add(int(at % 224), int(at / 224));
            last_x = d.x, last_y = d.y;
        }
        if (d.kind == Kind::ItemIcon) {
            const Box ink = ink_box(d.ink);
            targets.push_back({{d.x, d.y, d.x + 8, d.y + 8},
                               2 * d.y + 7,
                               ink.empty() ? -1 : ink.left,
                               "item icon",
                               ink.empty() ? -1 : ink.top});
        } else if (d.kind == Kind::Text && !d.marquee && !d.text.empty() && d.left < d.right) {
            const bool numerals = d.ink.empty();
            targets.push_back({{d.left, d.top, d.right, d.bottom},
                               2 * d.y + (numerals ? 5 : 6),
                               d.left,
                               "\"" + game::name_utf8(d.text) + "\""});
        }
    }
    close();
    return targets;
}
inline std::string half(int twice) { return std::to_string(std::abs(twice) / 2) + (twice % 2 ? ".5" : ""); }
inline void pointer_rules(const std::vector<game::Chrome::Draw> &draws, const std::vector<uint32_t> &pixels,
                          bool gb, std::vector<std::string> &errors, std::u32string_view empty_member = {}) {
    using Kind = game::Chrome::Draw::Kind;
    const auto targets = pointer_targets(draws);
    const auto frames = frame_mask(draws, gb);
    std::vector<uint8_t> ink = frames; // frames and everything else that is not a pointer
    PointerTarget captioned;
    for (const auto &d : draws) {
        if (d.kind == Kind::Text || d.kind == Kind::ItemIcon) {
            for (auto at : d.ink)
                ink[at] = 1;
            if (d.kind == Kind::Text && d.ink.empty() && !d.marquee) // numerals: their box
                for (int y = std::max(0, d.top); y < std::min(144, d.bottom); ++y)
                    for (int x = std::max(0, d.left); x < std::min(224, d.right); ++x)
                        ink[size_t(y * 224 + x)] = 1;
        }
    }
    for (size_t i = 0; i < draws.size(); ++i) {
        const auto &a = draws[i];
        if (a.kind != Kind::Cursor || !a.pointer || a.ink.empty())
            continue;
        const auto tip_middle = [](const auto &p) {
            const auto b = ink_box(p.ink);
            Box tip;
            for (auto at : p.ink)
                if (int(at % 224) == b.right - 1)
                    tip.add(int(at % 224), int(at / 224));
            return tip.top + tip.bottom - 1;
        };
        for (size_t j = i + 1; j < draws.size(); ++j) {
            const auto &b = draws[j];
            if (b.kind == Kind::Cursor && b.pointer && !b.ink.empty() && a.x < b.x + b.width &&
                a.x + a.width > b.x && std::abs(tip_middle(a) - tip_middle(b)) <= 2)
                errors.push_back("two pointers share a row");
        }
    }
    for (const auto &p : draws) {
        if (p.kind != Kind::Cursor || !p.pointer || p.ink.empty())
            continue;
        const Box box = ink_box(p.ink);
        Box tip;
        for (auto at : p.ink)
            if (int(at % 224) == box.right - 1)
                tip.add(int(at % 224), int(at / 224));
        const int tip2 = tip.top + tip.bottom - 1, tip_x = box.right - 1;
        const std::string who = std::string("pointer: ") + (p.width == 16 ? "hand" : "arrow") + " at " +
                                std::to_string(p.x) + "," + std::to_string(p.y);
        const PointerTarget *target = nullptr;
        int best_v = 0, best_h = 0;
        for (const auto &t : targets) {
            if (t.box.left <= tip_x || t.box.bottom <= box.top || t.box.top >= box.bottom)
                continue;
            const int v = tip2 < 2 * t.box.top      ? 2 * t.box.top - tip2
                          : tip2 > 2 * t.box.bottom ? tip2 - 2 * t.box.bottom
                                                    : 0;
            const int h = t.box.left - tip_x;
            if (!target || v < best_v || (v == best_v && h < best_h))
                target = &t, best_v = v, best_h = h;
        }
        // A picture/icon with a name right after it counts as text, preserving the settled fingertip rule.
        // A party member's row (the 164 px windows of the member lists and of Status's header): the arrow
        // at its left end points at the member through the name line (text at the row's y+3), named or not;
        // the hand points at the member's sprite, top row level with the sprite's.
        const game::Chrome::Draw *row = nullptr;
        for (const auto &d : draws)
            if (d.kind == Kind::Window && d.x == 0 && d.width == 164 && d.height == 25 && p.x <= 6 &&
                p.y >= d.y && p.y < d.y + d.height)
                row = &d;
        const bool member_hand = row && p.width == 16;
        if (member_hand) {
            const PointerTarget *sprite = nullptr;
            for (const auto &t : targets)
                if (t.what == "picture" && t.ink_top >= 0 && t.box.left > tip_x && t.box.top >= row->y &&
                    t.box.bottom <= row->y + row->height && (!sprite || t.box.left < sprite->box.left))
                    sprite = &t;
            if (sprite)
                target = sprite;
            else if (!target || empty_member.empty() ||
                     target->what != "\"" + game::name_utf8(std::u32string(empty_member)) + "\"")
                errors.push_back(who + " is on a member row without a sprite to point at");
            // An absent recruit has no sprite: its hand points at the placeholder's name line.
        } else if (row) {
            captioned = target ? *target : PointerTarget{};
            captioned.center2 = 2 * (row->y + 3) + 6;
            captioned.box =
                target ? target->box : Box{p.x + p.width, row->y + 3, p.x + p.width + 1, row->y + 10};
            captioned.what = "the member's name line";
            target = &captioned;
        } else if (target && (target->what == "picture" || (p.width == 16 && target->what == "item icon"))) {
            const PointerTarget *caption = nullptr;
            for (const auto &t : targets)
                if (t.what != "picture" && t.what != "item icon" &&
                    (target->what == "item icon" || t.what.size() > 3) && // picture: not a lone "?"
                    t.box.left >= target->box.right && t.box.left - target->box.right <= 8 &&
                    t.box.top < target->box.bottom && t.box.bottom > target->box.top &&
                    (!caption || std::abs(tip2 - t.center2) < std::abs(tip2 - caption->center2)))
                    caption = &t;
            if (caption) {
                captioned = *caption;
                captioned.what = "the caption " + caption->what;
                captioned.left = target->left; // the gap is still to the picture
                captioned.box.left = target->box.left;
                target = &captioned;
            }
        }
        // Clear columns on its left: frames and other ink, row by row. A frame line that runs on under the
        // pointer (a hand reaching over a window's lower edge) is crossed, not "to its left".
        int clear = 8;
        for (int y = box.top; y < box.bottom; ++y)
            for (int n = 0; n < clear; ++n) {
                const int x = box.left - 1 - n;
                if (x < 0 || (ink[size_t(y * 224 + x)] && !frames[size_t(y * 224 + box.left)])) {
                    clear = n;
                    break;
                }
            }
        // WSC art is deliberately one pixel lower; GB keeps the original midpoint.
        const int center2 = target ? target->center2 + (!gb && p.width == 8 ? 2 : 0) : 0;
        const int off = target ? tip2 - center2 : 0;
        const int gap = target && target->left >= 0 ? target->left - tip_x - 1 : -1;
        const bool hand_picture = target && p.width == 16 &&
                                  (target->what == "picture" || target->what == "item icon") &&
                                  target->ink_top >= 0;
        if (hand_picture && box.top != target->ink_top)
            errors.push_back(who + " top row " + std::to_string(box.top) + " is not flush with " +
                             target->what + " top row " + std::to_string(target->ink_top));
        if (target && !hand_picture && std::abs(off) > 2)
            errors.push_back(who + " is " + half(off) + " px " + (off < 0 ? "above" : "below") +
                             " the middle of " + target->what);
        if (target && p.width == 8 && center2 % 2 == 0 && 2 * (tip.bottom - 1) != center2)
            errors.push_back(who + " black tip row is not on the middle of " + target->what);
        if (target && p.width == 8 && center2 % 2 != 0 && tip2 != center2)
            errors.push_back(who + " tip does not straddle the middle of " + target->what);
        if (target && target->left >= 0 && gap < 1)
            errors.push_back(who + " has no clear pixel before " + target->what);
        if (clear < 1)
            errors.push_back(who + " has no clear pixel to its left");
        // SAWGOD_UI_POINTERS=FILE: one line per distinct placement (style, kind, position, numbers, kind of
        // target) and that picture as FILE.d/HASH.ppm: what the pointer gallery is built from.
        if (const char *log = std::getenv("SAWGOD_UI_POINTERS")) {
            static std::set<std::string> logged;
            const std::string kind = !target                                   ? "nothing"
                                     : target->what[0] == '"'                  ? "text"
                                     : target->what.starts_with("the caption") ? "caption"
                                                                               : target->what;
            std::ostringstream key;
            key << (gb ? "gb" : "wsc") << '\t' << (p.width == 16 ? "hand" : "arrow") << '\t' << p.x << '\t'
                << p.y << '\t' << box.left << '\t' << box.top << '\t' << box.right << '\t' << box.bottom
                << '\t' << tip_x << '\t' << tip2 << '\t' << (target ? target->center2 : -1) << '\t' << off
                << '\t' << clear << '\t' << gap << '\t' << kind;
            if (logged.insert(key.str()).second) {
                uint64_t hash = 1469598103934665603ull;
                for (uint32_t q : pixels) {
                    hash ^= q;
                    hash *= 1099511628211ull;
                }
                const std::filesystem::path dir = std::string(log) + ".d";
                std::filesystem::create_directories(dir);
                ppm(dir / (std::to_string(hash) + ".ppm"), pixels);
                std::ofstream(log, std::ios::app)
                    << hash << '\t' << key.str() << '\t' << (target ? target->box.left : 0) << '\t'
                    << (target ? target->box.top : 0) << '\t' << (target ? target->box.right : 0) << '\t'
                    << (target ? target->box.bottom : 0) << '\t'
                    << (target ? target->what : std::string("nothing on its line (not judged)")) << '\t'
                    << (target ? target->ink_top : -1) << '\t' << (hand_picture ? "top" : "tip") << '\n';
            }
        }
    }
}
// Text room. A text's ink is never cut by its own viewport above or below (the detail line used to cut every
// descender); only a marquee may cut it at the sides. Text closer than 1 px to a frame is noted, not failed
// ("where possible, at least 1px clear space around all sides of text"): set SAWGOD_UI_NOTES to a file to
// collect the notes of a run.
inline void text_rules(const std::vector<game::Chrome::Draw> &draws, bool gb,
                       std::vector<std::string> &errors) {
    using Kind = game::Chrome::Draw::Kind;
    static std::set<std::string> noted;
    const char *notes = std::getenv("SAWGOD_UI_NOTES");
    const auto frames = notes ? frame_mask(draws, gb) : std::vector<uint8_t>();
    for (const auto &d : draws) {
        if (d.kind != Kind::Text)
            continue;
        if (d.cut_rows)
            errors.push_back("text ink cut by its viewport: " + game::name_utf8(d.text) + " (" +
                             std::to_string(d.cut_rows) + " rows)");
        if (!notes || d.left >= d.right)
            continue;
        // (the letters themselves: the WSC font's shadow is not counted, so both styles are judged alike)
        Box b = ink_box(d.strokes);
        if (b.empty())
            b = {d.left, d.top, d.right, d.bottom};
        std::string sides;
        const auto frame = [&](int x, int y) {
            return x >= 0 && x < 224 && y >= 0 && y < 144 && frames[size_t(y * 224 + x)];
        };
        bool top = false, bottom = false, left = false, right = false;
        for (int x = b.left; x < b.right; ++x)
            top |= frame(x, b.top - 1) || frame(x, b.top),
                bottom |= frame(x, b.bottom) || frame(x, b.bottom - 1);
        for (int y = b.top; y < b.bottom && !d.marquee; ++y)
            // (a frame line the text's own rows lie on is "top"/"bottom", not a frame at its side)
            left |= (frame(b.left - 1, y) || frame(b.left, y)) && !frame(b.left + 3, y),
                right |= (frame(b.right, y) || frame(b.right - 1, y)) && !frame(b.right - 4, y);
        for (const auto &[side, on] :
             {std::pair{"top", top}, {"bottom", bottom}, {"left", left}, {"right", right}})
            if (on)
                sides += std::string(sides.empty() ? "" : "/") + side;
        if (sides.empty())
            continue;
        const std::string note = std::string(gb ? "GB" : "WSC") + ": \"" + game::name_utf8(d.text) +
                                 "\" at " + std::to_string(d.x) + "," + std::to_string(d.y) +
                                 ": no clear pixel to the frame (" + sides + ")";
        if (noted.insert(note).second)
            std::ofstream(notes, std::ios::app) << note << '\n';
    }
}
inline std::vector<std::string> audit(const std::vector<game::Chrome::Draw> &recorded,
                                      const std::vector<uint32_t> &pixels, bool gb, LiveMenu &m) {
    using Kind = game::Chrome::Draw::Kind;
    std::vector<std::string> errors;
    auto draws = recorded;
    if (m.dialog.active() && m.dialog.view(m.w).dialog == game::ItemDialogUi::Dialog::Crystals) {
        using Ui = game::ItemDialogUi;
        const auto v = m.dialog.view(m.w);
        const int x = Ui::kCrystalX + Ui::kCrystalHandX + (v.crystal_choice % 2) * Ui::kCrystalColumnPitch,
                  y = v.crystal_y + Ui::kCrystalInsetY + (v.crystal_choice / 2) * Ui::kCrystalPitch;
        const auto popup = std::find_if(draws.begin(), draws.end(), [&](const auto &d) {
            return d.kind == Kind::Window && d.x == Ui::kCrystalX && d.y == v.crystal_y &&
                   d.width == Ui::kCrystalWidth && d.height == Ui::kCrystalHeight;
        });
        if (popup == draws.end())
            errors.push_back("Crystals has no compact pop-up beside its source row");
        else {
            // Later opaque windows cover earlier list ink. Test the visible ink for collisions while
            // retaining the full row's width/budget for fit checks (occlusion is not text truncation).
            const auto covered = [&](uint32_t at) {
                const int px = int(at % 224), py = int(at / 224);
                return px >= popup->x && px < popup->x + popup->width && py >= popup->y &&
                       py < popup->y + popup->height;
            };
            for (auto it = draws.begin(); it != popup; ++it) {
                const bool had_ink = !it->ink.empty();
                std::erase_if(it->ink, covered);
                std::erase_if(it->strokes, covered);
                if (had_ink && it->ink.empty() && it->kind == Kind::Text)
                    it->left = it->right = it->top = it->bottom = 0;
            }
        }
        if (v.rows != game::InventoryUi::kVisibleRows ||
            v.list[size_t(v.selected - v.first)].name != game::host_text(m.w, "ui_inventory_crystals"))
            errors.push_back("Crystals did not retain its inventory and source row");
        if (std::count_if(draws.begin(), draws.end(), [](const auto &d) { return d.kind == Kind::Cursor; }) !=
            1)
            errors.push_back("Crystals must have one focused hand and no inventory arrow");
        if (std::none_of(draws.begin(), draws.end(), [&](const auto &d) {
                return d.kind == Kind::Cursor && d.width == 16 && d.x == x && d.y == y;
            }))
            errors.push_back("Crystals choice has no native hand");
        if (v.crystal_choice != m.w.ram[0xD840] ||
            v.crystals != ((m.w.ram[0xD0AB] >> 4) < 5 ? m.w.ram[0xD0AB] >> 4 : 0))
            errors.push_back("Crystals view differs from native selection/availability");
        if (gb && (pixels[size_t(v.crystal_y * 224 + Ui::kCrystalX + 12)] != 0 ||
                   pixels[size_t((v.crystal_y + 1) * 224 + Ui::kCrystalX + 12)] != 0xAAAAAA ||
                   pixels[size_t((v.crystal_y + 2) * 224 + Ui::kCrystalX + 12)] != 0))
            errors.push_back("GB Crystals chrome does not have black-gray-black border");
    }
    // Independently recognize item-name rows, so replacing an item helper with plain text fails too.
    // Description/log lines and monster/name plates are outside the list pen positions.
    std::vector<std::u32string> item_names;
    for (int id = 0; id < 252; ++id)
        item_names.push_back(game::item_name(m.w, uint8_t(id)));
    for (const auto &d : draws) {
        if (d.kind != Kind::Text || d.y < 22 || d.y > 118 ||
            (d.x != 17 && d.x != 27 && d.x != 72 && d.x != 82 && d.x != 76 && d.x != 86 && d.x != 81 &&
             d.x != 91) ||
            std::find(item_names.begin(), item_names.end(), d.text) == item_names.end())
            continue;
        if (std::none_of(draws.begin(), draws.end(), [&](const auto &r) {
                return r.kind == Kind::ItemRun && r.x + game::Chrome::kItemAdvance == d.x && r.y == d.y;
            }))
            errors.push_back("item list row is missing its icon helper");
    }
    // These declarations are emitted by the shared item argument/layout helper, separately from its
    // actual pixels. A clipped-away, missing or overlapping icon therefore cannot pass as plain text.
    for (const auto &run : draws) {
        if (run.kind != Kind::ItemRun)
            continue;
        const auto icon = std::find_if(draws.begin(), draws.end(), [&](const auto &d) {
            return d.kind == Kind::ItemIcon && d.item == run.item && d.x == run.x && d.y == run.y;
        });
        if (icon == draws.end()) {
            errors.push_back("item name has no icon cell");
            continue;
        }
        game::Machine native(m.w);
        const bool expected = run.item >= 0 && native.rd8(0x9000, uint16_t(0x6EE0 + run.item)) != 255;
        if (icon->has_icon != expected || (expected && icon->ink.empty()) ||
            (!expected && !icon->ink.empty()))
            errors.push_back("item icon presence differs from native item mapping");
        if (std::none_of(draws.begin(), draws.end(), [&](const auto &d) {
                return d.kind == Kind::Window && icon->x >= d.x + 3 && icon->y >= d.y + 3 &&
                       icon->x + 8 <= d.x + d.width - 3 && icon->y + 8 <= d.y + d.height - 3;
            }))
            errors.push_back("item icon outside its row window");
        for (const auto &d : draws)
            if ((d.kind == Kind::Text || d.kind == Kind::Cursor) && intersects(*icon, d))
                errors.push_back("item icon overlaps name, count, or cursor");
        if (gb && expected) {
            // The GB picture's symbol, checked against the Game Boy game's own data (embedded GB bank 5:
            // its item-name table at ROM 0x334640, 7 characters + uses): a name that starts with the heart,
            // circle or cross character (0x42-0x44) shows that GB tile (ROM 0x32BF00 + 16 * (code - 0x40));
            // the GB game has no symbol for anything else, which shows the WSC icon's shape.
            // GB style draws the GB icon of the item's WSC symbol (Chrome::gb_item_icon), paper left
            // open
            std::vector<uint32_t> shape;
            const bool key_item = game::is_marked_key_item(run.item);
            const char *art = key_item ? game::kKeyItemArt.data()
                                       : game::Chrome::gb_item_icon(game::Chrome::item_symbol(
                                             run.item, native.rd8(0x9000, uint16_t(0x6EE0 + run.item))));
            for (int y = 0; art && y < (key_item ? 7 : 8); ++y)
                for (int x = 0; x < 8; ++x)
                    if (art[y * 8 + x] != '.')
                        shape.push_back(uint32_t((icon->y + y) * 224 + icon->x + x));
            if (shape != icon->ink)
                errors.push_back("GB item icon is not the expected GB icon for item " +
                                 std::to_string(run.item));
        }
        if (gb)
            for (auto at : icon->ink) {
                const auto p = pixels[at];
                if (game::is_marked_key_item(run.item)
                        ? p != 0xA0A0A0 && p != 0xF0F0F0
                        : p != 0 && p != 0x555555 && p != 0xAAAAAA && p != 0xFFFFFF) {
                    errors.push_back("GB item icon contains a color outside its four grays");
                    break;
                }
            }
    }
    for (const auto &d : draws) {
        if (d.kind == Kind::ItemRun || d.kind == Kind::Tile)
            continue;
        // (text never draws past its budget: Chrome cuts it after the last letter that fits)
        const int drawn = d.kind == Kind::Text ? std::min(d.width, d.budget) : d.width;
        if (!d.clipped && (d.x < 0 || d.y < 0 || d.x + drawn > 224 || d.y + d.height > 144))
            errors.push_back("primitive outside screen at " + std::to_string(d.x) + "," +
                             std::to_string(d.y));
        // a player-entered name may be cut at its budget (Chrome::name); every other text must fit
        if (d.kind == Kind::Text && !d.clipped && !d.cut_ok && d.width > d.budget)
            errors.push_back("text exceeds box: " + game::name_utf8(d.text) + " (" + std::to_string(d.width) +
                             "/" + std::to_string(d.budget) + " px)");
        if (d.clipped && !d.marquee)
            errors.push_back("unapproved clipped viewport");
    }
    for (size_t i = 0; i < draws.size(); ++i)
        for (size_t j = i + 1; j < draws.size(); ++j) {
            const auto &a = draws[i], &b = draws[j];
            // Native joined windows intentionally overlap. The detail marquee is the sole clipping exception.
            if (a.kind == Kind::Text && b.kind == Kind::Text && !a.clipped && !b.clipped &&
                text_collides(a, b))
                errors.push_back("text overlap: " + game::name_utf8(a.text) + " / " +
                                 game::name_utf8(b.text));
            if (a.kind == Kind::Cursor && b.kind == Kind::Text && !b.clipped && pointer_over_text(a, b))
                errors.push_back("cursor overlaps text");
            if (b.kind == Kind::Cursor && a.kind == Kind::Text && !a.clipped && pointer_over_text(b, a))
                errors.push_back("cursor overlaps text");
        }
    if (m.inventory.active() && !m.dialog.active()) {
        const auto v = m.inventory.view(m.w);
        if (v.selected < v.first || v.selected >= v.first + 8 || v.selected >= v.total)
            errors.push_back("cursor is not on a real list row");
        const int x = v.tabs_focused ? (v.tab ? 128 : 29) : 6;
        const int y = v.tabs_focused ? 6 : 26 + (v.selected - v.first) * 12;
        if (std::none_of(draws.begin(), draws.end(),
                         [&](const auto &d) { return d.kind == Kind::Cursor && d.x == x && d.y == y + !gb; }))
            errors.push_back("selected row/tab has no cursor");
        // Chrome probes on the opaque detail pane, away from its text: GB black/gray/black,
        // WSC drawn with the loaded game's frame tiles. Geometry is identical across variants.
        const int line = game::DetailLine::kY;
        if (gb &&
            (pixels[size_t(line * 224 + 100)] != 0 || pixels[size_t((line + 1) * 224 + 100)] != 0xAAAAAA ||
             pixels[size_t((line + 2) * 224 + 100)] != 0))
            errors.push_back("GB chrome does not have black-gray-black border");
    }
    if (gb) {
        std::vector<bool> key_ink(pixels.size(), false);
        for (const auto &d : draws) {
            if (d.kind == Kind::ItemIcon && game::is_marked_key_item(d.item))
                for (auto at : d.ink)
                    key_ink[at] = pixels[at] == 0xA0A0A0 || pixels[at] == 0xF0F0F0;
            if (d.kind == Kind::Text)
                for (auto at : d.strokes)
                    key_ink[at] = pixels[at] == 0x505050;
        }
        for (size_t at = 0; at < pixels.size(); ++at) {
            const auto p = pixels[at];
            if (p != 0 && p != 0x555555 && p != 0xAAAAAA && p != 0xFFFFFF && !key_ink[at]) {
                errors.push_back("GB picture contains a color outside its four grays or key-item inks");
                break;
            }
        }
    }
    auto pointer_draws = draws;
    if (m.dialog.active() && m.dialog.view(m.w).dialog == game::ItemDialogUi::Dialog::Crystals) {
        // Focus belongs to the icon-only pop-up, including empty slots: a count on the inventory behind
        // it cannot become the hand's target. Keep underlying windows for the visible frame mask.
        bool background = true;
        std::erase_if(pointer_draws, [&](const auto &d) {
            if (d.kind == Kind::Window && d.x == game::ItemDialogUi::kCrystalX)
                background = false;
            return background && d.kind != Kind::Window;
        });
    }
    pointer_rules(pointer_draws, pixels, gb, errors, game::host_text(m.w, "ui_battle_empty_member"));
    text_rules(draws, gb, errors);
    return errors;
}
struct Renderer {
    game::TextTable en, ja, stress;
    std::filesystem::path out;
    bool full = false, core = false;
    std::filesystem::path gb_resources;
    std::vector<RenderResult> results;
    std::string longest_name, longest_description;
    int longest_name_width = 0;
    explicit Renderer(const game::TextTable &english, const std::vector<uint8_t> &rom) : en(english) {
        emu::Wsc w(rom, 0);
        std::string error;
        require(ja.load(game::export_text(w), game::Charset::system(), &error, nullptr, nullptr, false),
                error);
        game::Session::of(w).text() = en;
        std::u32string name, description;
        for (int i = 0; i < 256; ++i) {
            auto row = game::item_row(w, uint8_t(i), 50, false);
            if (game::Chrome::text_width(row.name) > game::Chrome::text_width(name))
                name = row.name;
            if (game::Chrome::text_width(row.flavor) > game::Chrome::text_width(description))
                description = row.flavor;
        }
        longest_name = game::name_utf8(name);
        longest_name_width = game::Chrome::text_width(name);
        longest_description = game::name_utf8(description);
        stress = en;
        std::string csv = "key,text\n";
        for (int i = 0; i < 252; ++i) {
            csv += "item_" + std::to_string(i) + "," + game::csv_cell(longest_name) + "\n";
            csv += "itemdesc_" + std::to_string(i) + "," + game::csv_cell(longest_description) + "\n";
        }
        require(stress.load(csv, game::Charset::system(), &error, nullptr, nullptr, true, true), error);
    }
    void capture(LiveMenu &m, std::string id) {
        std::replace(id.begin(), id.end(), '/', '_');
        if (!m.host || m.returned)
            return;
        if (m.stage == 0 && !m.message && !m.party_scene && m.w.ram[0xD842] == 8)
            require(m.dialog.active(), "Crystals presentation did not open");
        if (!m.inventory.active() && !m.dialog.active() && !m.party.active())
            return;
        auto text = game::Session::of(m.w).text();
        const auto ram = m.w.ram;
        const auto io = m.w.io;
        const auto cpu = m.w.cpu.s;
        // Full runs prepare the actual GfxSwap before entering the menus.
        require(!full || m.gfx.enabled(), "GB matrix requires the real font/sprite resource pack");
        for (int variant = 0; variant < (full ? 5 : 1); ++variant) {
            const bool gb = variant == 1 || variant == 3;
            game::Session::of(m.w).text() = variant == 2 || variant == 3 ? ja : variant == 4 ? stress : en;
            std::vector<game::Chrome::Draw> draws;
            struct Clear {
                ~Clear() { game::Chrome::observe = {}; }
            } clear;
            game::Chrome::observe = [&](const auto &d) { draws.push_back(d); };
            std::vector<uint32_t> px;
            std::string scene;
            if (m.dialog.active()) {
                px = m.dialog.picture(m.w, emu::Wsc::ColorMode::Raw, gb);
                scene = m.dialog.view(m.w).dialog == game::ItemDialogUi::Dialog::Crystals ? "crystal-chooser"
                                                                                          : "item-dialog";
            } else if (m.inventory.active()) {
                px = m.inventory.picture(m.w, emu::Wsc::ColorMode::Raw, gb);
                scene = "inventory";
            } else {
                px = m.party.picture(m.w, emu::Wsc::ColorMode::Raw, gb);
                scene = "equip-transfer";
            }
            game::Chrome::observe = {};
            const char *labels[] = {"wsc-en", "gb-en", "wsc-ja", "gb-ja", "wsc-long"};
            RenderResult result{id, labels[variant], scene, "", audit(draws, px, gb, m), int(draws.size())};
            require(!draws.empty(), "render audit observed no drawing");
            require(m.w.ram == ram && m.w.io == io && m.w.cpu.s.ip == cpu.ip &&
                        m.w.cpu.s.flags == cpu.flags && m.w.cpu.s.halted == cpu.halted &&
                        std::equal(std::begin(m.w.cpu.s.r), std::end(m.w.cpu.s.r), std::begin(cpu.r)) &&
                        std::equal(std::begin(m.w.cpu.s.seg), std::end(m.w.cpu.s.seg), std::begin(cpu.seg)),
                    "render mutated machine state");
            if (!out.empty()) {
                result.file = id + "--" + labels[variant] + ".ppm";
                ppm(out / result.file, px);
                if (results.empty()) {
                    // Deliberate regression for a sample review, produced by the actual
                    // screen. Baseline is a candidate, never marked final by this tool.
                    uint8_t previous = m.w.ram[0xD07D];
                    m.w.ram[0xD07D] = 49;
                    auto changed = m.inventory.picture(m.w, emu::Wsc::ColorMode::Raw, false);
                    m.w.ram[0xD07D] = previous;
                    ppm(out / "demo-changed.ppm", changed);
                }
            }
            results.push_back(std::move(result));
            append_enhanced(m.w, results.back(), out, results, [&] {
                if (m.dialog.active())
                    return m.dialog.picture(m.w, emu::Wsc::ColorMode::Raw, false);
                if (m.inventory.active())
                    return m.inventory.picture(m.w, emu::Wsc::ColorMode::Raw, false);
                return m.party.picture(m.w, emu::Wsc::ColorMode::Raw, false);
            });
        }
        game::Session::of(m.w).text() = std::move(text);
    }
};
} // namespace ui_pilot
