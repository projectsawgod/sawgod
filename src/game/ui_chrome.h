// Drawing primitives for the redesigned game UI.
//
// Every pixel comes from the game's own window tiles, cursor tiles, font and window palettes, read from the
// picture being drawn: the WSC picture gets the WSC chrome, the GB picture (Wsc's second view, GB graphics)
// gets the GB chrome, through the same calls. Layouts ask for a window, text, a bar or a cursor by role and
// never name a color or copy frame pixels.
//
// The window is the game's nine-slice: tiles 0x1D5-0x1DD (corners, edges, textured fill), drawn at any pixel
// size (a dimension under 16 takes half from each end). Normal is palette 15, the game's usual window and the
// battle panel of the member acting; Inactive is palette 7, the other members' panels. Transparency applies
// only to the fill's own colors, so frames and text stay opaque. The GB picture draws thin windows instead:
// the GB frame's outer line, a grey band and a second line (black-grey-black) over a blank fill in its
// lightest grey, opaque and the same for both styles.
//
// Drawing reads machine memory and writes only the given picture; the game's state is untouched.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "emu/wsc.h"
#include "engine/button_icons.h"

namespace sawgod::game {

class Chrome {
  public:
    enum class Window : uint8_t { Normal, Inactive };
    // Window-palette colors by role. Text and Shadow are the font's own stroke and shadow (as game text);
    // Muted: unselected rows; Accent: the selection and numbers that matter; Heal: healing.
    // Secondary: dim text derived from the font's own inks (the battle palette's Muted may be white).
    // A palette role too close to the window's fill in this picture (the GB greys) reads as Text instead.
    enum class Ink : uint8_t {
        Text,
        Shadow,
        Muted,
        Accent,
        Heal,
        Title,
        TitleShadow,
        IntroCaption,
        IntroClosing,
        IntroPrologue,
        IntroEdge,
        Secondary,
        TitleHint,       // title-palette ink on either picture, below full strength (title_hint_strength)
        TitleHintShadow, // its dark edge at the same strength (WSC/Enhanced)
        TitleVersion,    // steady gray, following the title palette's fades (GB: dark gray)
        // A value above / below the one it is compared with (the meat preview's new form): the party HP bar's
        // green and the enemy HP bar's red, each raised to text brightness. A mark beside it (trend_mark)
        // carries the meaning; the GB picture has no color and draws Text.
        Higher,
        Lower,
        KeyItem,
        SealBlack,
        SealBlue,
        SealWhite,
        SealRed,
        SealFourfold
    };

    // How much of the title's white TitleHint (and its edge) has, in percent; the rest is the title page:
    // black on WSC/Enhanced, white on GB (stepped through the GB greys). 0 is fully out.
    void title_hint_strength(int percent) { title_hint_strength_ = percent; }
    uint32_t title_hint_faded(uint32_t full) const; // a color at the current title_hint_strength
    bool title_hint_out() const { return title_hint_strength_ < (second_ ? 25 : 1); } // faded to the page

    // Optional drawing observer for layout audits. It observes actual primitive calls, never changes pixels.
    struct Draw {
        enum class Kind { Window, Text, Cursor, ScrollMarker, ItemIcon, ItemRun, Tile } kind;
        int x, y, width, height, budget;
        bool clipped;
        std::u32string text;
        // Actual visible ink bounds, separate from the string's advance/fit budget.
        int left = 0, top = 0, right = 0, bottom = 0;
        // Text: every visible pixel it set (strokes and shadow), and its stroke pixels alone, as
        // y * kWidth + x in ascending order; empty for numerals (their box is their ink).
        std::vector<uint32_t> ink = {}, strokes = {};
        // Text: cutting it at the budget is by design (a player-entered name, Chrome::name).
        bool cut_ok = false;
        int item = -1; // ItemIcon: -1 is a reserved blank cell; FF in the ROM also leaves it blank
        bool has_icon = false;
        // Cursor: it points at something (a row, a key, a picture). False for the arrows that only say
        // "more" or "next" (page and scroll marks, the dialogue's advance arrow): those have no target.
        // Cursor/Tile: `ink` is every nonzero-index pixel of its tile(s), excluding blank padding even
        // when a Tile is drawn opaque. Picture-target audits use this to find the artwork's visible top.
        bool pointer = true;
        // Text: rows of its ink that its clip box cut off above or below (a descender under a viewport's
        // edge). Columns cut at the sides are a marquee's business and are not counted.
        int cut_rows = 0;
        bool marquee = false; // Text: drawn inside Chrome::marquee, the one viewport that may cut text
    };
    static thread_local std::function<void(const Draw &)> observe;

    Chrome(emu::Wsc &w, std::vector<uint32_t> &picture, emu::Wsc::ColorMode mode, bool second);

    // The window tiles are in video memory (they are while a battle or menu screen is up). Every call draws
    // nothing otherwise.
    bool ready() const { return ready_; }

    // Intersect a temporary, half-open drawing viewport with the current clip. Nested clips and
    // exceptions restore the previous viewport; frame and text pixels use the same clipping.
    void clip(int x, int y, int width, int height, const std::function<void()> &draw);
    // clip() for a scrolling line (DetailLine): its text may be cut at the sides, and says so
    // (Draw::marquee).
    void marquee(int x, int y, int width, int height, const std::function<void()> &draw) {
        marquee_ = true;
        clip(x, y, width, height, draw);
        marquee_ = false;
    }

    void window(int x, int y, int width, int height, Window style = Window::Normal, int transparency = 0);
    // Frameless window fill, aligned to the screen tile grid; GB uses its blank window fill.
    void window_fill(int x, int y, int width, int height, Window style = Window::Normal);
    // The GB picture's blank battle backdrop (the GB's battles show no scenery): the area in the GB windows'
    // lightest grey. The WSC picture keeps its scenery: nothing is drawn there.
    void backdrop(int x, int y, int width, int height);
    // Two joined tabs; the active tab owns the rounded foreground connector to the page.
    void tabbed_window(int x, int y, int width, int height, int divider, int split, int active);
    // One joined speaker tab; the scene remains visible above the body to its right.
    void speaker_window(int x, int y, int width, int height, int tab_width);
    // Text in the game's font (m5x7, as text_render draws it; Unicode private-use U+E000+code is the WSC
    // font's own glyph, and kana fall back to it -- the WSC font in the GB picture too, without its shadow),
    // clipped at max_width. Widths are text_render's text_width.
    void text(std::u32string_view s, int x, int y, Ink ink = Ink::Text, int max_width = emu::Wsc::kWidth,
              Window style = Window::Normal, bool cast_shadow = true);
    // A player-entered name: as text(), and a name wider than max_width is cut after its last whole
    // letter by design -- translations must fit, names need not.
    void name(std::u32string_view s, int x, int y, Ink ink, int max_width, Window style = Window::Normal) {
        cut_ok_ = true;
        text(s, x, y, ink, max_width, style);
        cut_ok_ = false;
    }
    // One 8 px icon cell plus 2 px gap. total_width includes the cell; plain names stay plain in data/logs.
    static constexpr int kItemAdvance = 10;
    static constexpr char32_t kItemToken = 0xF0000; // presentation-only item argument, never a resource glyph
    static bool is_item_token(char32_t c) { return c >= kItemToken && c <= kItemToken + 256; }
    static std::u32string item_run(int id, std::u32string_view name, bool list = false);
    static int item_width(std::u32string_view name) { return kItemAdvance + text_width(name); }
    void item(int id, std::u32string_view name, int x, int y, Ink ink = Ink::Text,
              int total_width = emu::Wsc::kWidth, Window style = Window::Normal);
    // GB style: the 8x8 four-gray icon of a WSC item symbol (64 characters, see ui_chrome.cpp); null = none.
    static const char *gb_item_icon(int symbol);
    static int item_symbol(int id, int native_symbol);
    static int text_width(std::u32string_view s);
    // A button icon (engine/button_icons.h) with its 9-pixel box's top-left at (x, y), in the text's ink.
    void button_icon(const ButtonIcon &icon, int x, int y, Ink ink, Window style, bool with_shadow);
    // The character has a glyph: m5x7's, or the WSC font's (kana, U+E000 + code).
    static bool can_draw(char32_t c);
    // Compact 4x6 numerals ('0'-'9' and '/') with the font's shadow.
    void numerals(std::string_view s, int x, int y, Ink ink = Ink::Text);
    static int numerals_width(std::string_view s);
    // A horizontal meter, `value` / `maximum` of it filled (rounded, clamped), with an optional label in
    // numerals centered over it.
    struct BarColors {
        uint32_t full, empty, shadow; // the numerals' shadow
    };
    // Party HP: B4 sage; enemy HP: red, 75% from crimson toward rust. Empty portions are
    // dark, desaturated counterparts; numeral shadows a dark shade of the full color.
    static constexpr BarColors kPartyHp{0x58883B, 0x293326, 0x1F3015}, kEnemyHp{0xAA3E2A, 0x31221F, 0x3B150E};
    // WSC adds one shadow pixel right/below. Enhanced edges use the retained UiText::bars record.
    void bar(int x, int y, int width, int height, int value, int maximum, std::string_view label = {},
             const BarColors &colors = kPartyHp);
    // The game's list arrow (sprite tile 0x1EE). y is the target's top, target_height its capital/icon
    // height: for odd heights, put the black tip row (tile row 4) on the middle pixel row; for even heights,
    // straddle the two middle rows. WSC/Enhanced art is drawn 1 px below that shared anchor; GB is unchanged.
    // Non-pointer page/advance marks use y directly, with the same WSC-only art offset.
    void cursor(int x, int y, bool pointer = true, int target_height = 7);
    // The WSC list arrow's pixels as tile memory holds them now, and a pointer drawn from such a copy: for
    // native screens that reuse the arrow's tile (the meat menu's eater choice loads its target cursor
    // there).
    using ArrowPixels = std::array<uint8_t, 64>;
    ArrowPixels arrow() const;
    void cursor(int x, int y, const ArrowPixels &arrow, int target_height = 7);
    // Compact down arrow, using the list arrow's own pixels/palette in each picture. A scroll marker,
    // never a selection cursor: 8x4 lets it clear the last count and the GB frame in the Equip list.
    void more_below(int x, int y);
    void more_above(int x, int y); // the same marker pointing up, for rows hidden above
    // The same marker beside a changed value: up in Higher ink, down in Lower ink; its darkest shade (the
    // outline) stays. The GB picture draws its plain marker.
    void trend_mark(int x, int y, bool higher);
    // First pixel row (0-7) of a tile that has ink; 8 if it is empty. For placing a hand flush with a sprite.
    int tile_ink_top(uint16_t entry, bool sprite = false) const;
    // The game's 16x16 pointing hand (sprite tiles 0x1FB-0x1FE). Its fingertip is the right end of rows y+1
    // and y+2: text rows keep their settled fingertip placement. For a picture without a text target,
    // align the hand's visible top with the picture's visible top (not the tile cell's blank padding).
    void hand(int x, int y);

    // Lower level, for layouts drawing the game's own pictures (party sprites, ground tiles): one screen-map
    // entry from the picture's tile memory, index 0 skipped unless opaque.
    void tile(uint16_t entry, int x, int y, bool opaque = false, bool sprite = false, bool character = false);
    // A tile that is not in video memory (32 bytes, as tile memory packs it), in the given 12-bit colors
    // (index 0 skipped) as hardware palette `palette` would show them: 8-15 in GB greys in the GB picture.
    // For the game's own pictures shown more at once than it loads (creation's eight type sprites).
    void tile(const uint8_t *data, const std::array<uint16_t, 16> &colors, int palette, int x, int y,
              bool hflip = false, bool vflip = false);
    uint8_t memory(uint16_t at) const { return memory_data_[at]; }
    uint32_t color(Ink ink, Window style = Window::Normal) const;

  private:
    uint32_t fill_owner(int y, int height, int palette, int alpha);
    void scroll_marker(int x, int y, bool up, uint32_t tint = 0, bool tinted = false);
    // A GB-picture mark from rows ('#' black, 'L'/'D' light/dark grey, '.' clear; 8 wide), flipped vertically
    // on request; its pixels into draw.ink.
    void gb_pattern(const char *const *rows, int height, int x, int y, bool flip, Draw &draw);
    void item_icon(int id, int x, int y);
    void key_icon(int x, int y);
    void seal_orb(int seal, int x, int y);
    uint32_t seal_ink(Ink ink, int row) const;
    std::vector<uint32_t> sprite_ink(int tile, int x, int y) const; // a sprite tile's opaque pixels
    int opacity_ = 100; // scoped coverage fade for the GB HP label's black ink
    void pixel(int x, int y, uint32_t color, int alpha = 100);
    void icon_pixel(int x, int y, int palette, int index, bool sprite);
    uint8_t index(int tile, int x, int y) const; // 4bpp pixel of a tile in the picture's memory
    uint32_t rgb(int palette, int index, bool sprite) const;
    uint32_t shown(uint16_t color) const; // a 12-bit color as the picture's mode shows it
    void numerals(std::string_view s, int x, int y, uint32_t stroke, uint32_t shadow); // shadow ~0: none
    void thin_window(int x, int y, int width, int height, const std::function<int(int, int)> &at);
    void window_greys(uint32_t *light, uint32_t *band) const; // the lightest grey and the next one down

    emu::Wsc &w_;
    std::vector<uint32_t> &px_;
    emu::Wsc::ColorMode mode_;
    bool second_;
    int title_hint_strength_ = 100;
    const uint8_t *memory_data_;
    // A Chrome draws one immutable native picture. Palette/index conversion is
    // shared by its window pixels, with independent sprite and tile colors.
    mutable uint32_t colors_[512];
    mutable uint64_t colors_valid_[8]{};
    bool ready_ = false;
    bool observed_output_ = true; // false for intermediate chrome construction pictures
    bool cut_ok_ = false;         // inside name()
    bool marquee_ = false;        // inside marquee()
    bool tiles_observed_ = true;  // false inside cursor() and hand(), which report themselves
    uint16_t fill_ = 0;           // bit per palette index the window's fill tile uses (the transparent part)
    uint8_t fill_index_ = 0;
    // Set: windows texture their fill from this screen point instead of their own origin, so joined windows
    // (tabbed_window) continue one background pattern across their seams.
    bool texture_anchored_ = false;
    int texture_x_ = 0, texture_y_ = 0;
    int fill_top_ = 0, fill_bottom_ = 0; // whole joined shape while texture_anchored_
    void text_pixel(int x, int y, uint32_t color, bool shadow, bool casts_shadow = true);
    uint32_t soft_shadow_ = 0x111111; // ordinary text; HP numerals use their bar's dark green
    bool shadow_ = false;             // the picture's font has a shadow (WSC yes, GB no)
    int clip_left_ = 0, clip_top_ = 0, clip_right_ = emu::Wsc::kWidth, clip_bottom_ = emu::Wsc::kHeight;
};

} // namespace sawgod::game
