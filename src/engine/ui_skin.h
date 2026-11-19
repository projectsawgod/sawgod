// The options overlay's look, taken from the game itself: the system font and window-frame tiles at ROM
// 0x390080 (256 4bpp tiles, the same ones main loads to tile 0x100) in the font palette at 0x390000
// (the ROM's font), so an asset pack that replaces the font restyles the overlay too. Builds one RGBA atlas
// texture and draws text, framed windows, the pointing-hand cursor and volume bars with ImGui draw lists.
// Coordinates are menu units (one unit = half a game pixel); the menu is drawn straight at the window's
// resolution, so glyphs land at non-integer sizes -- the atlas is built for smooth "sharp bilinear"
// sampling (see ui_skin.cpp), which keeps the pixels crisp at any size.
//
// Text is in m5x7 (Daniel Linssen, CC0; src/engine/font_m5x7.inc, proportional) wherever it has the
// character; the rest -- kana -- in the game's font, below.
// The font has upper-case Latin letters, digits, kana and a few symbols. Lower case comes from our own tiles
// in the same style (assets/ui/font_lowercase.txt, compiled in; not ROM-derived), and the handful of ASCII
// punctuation marks it lacks (. , ( ) + ' ; = < > ") are drawn here (2-pixel white strokes with the font's
// black shadow to the right and below).
#pragma once
#include <array>

#include <SDL3/SDL.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "engine/button_icons.h"
#include "imgui.h"

namespace sawgod::ui {

inline constexpr int kGlyph = 8; // the font's cell size, in overlay pixels

class Skin {
  public:
    enum class Frame { White, Orange, Grey };

    // Optional drawing observer for layout audits (as game::Chrome::observe): it sees the primitive calls
    // in menu units and never changes pixels.
    struct Draw {
        enum class Kind { Frame, Text, Hand, Glyph, Bar } kind;
        float x, y, width, height; // Text: the string's advance by the glyph cell
        std::string text;          // Text
        ImU32 tint = 0;            // Text, Glyph
        float scale = 1;
        int segments = 0, filled = 0; // Bar
        bool focused = false;         // Bar
    };
    static thread_local std::function<void(const Draw &)> observe;

    ~Skin() { release(); }
    void release(); // frees the texture (before its renderer goes)
    // Builds (or rebuilds) the atlas from the ROM image, colors as the game stores them (each 4-bit
    // channel * 17, like Wsc::ColorMode::Raw).
    bool build(SDL_Renderer *renderer, const std::vector<uint8_t> &rom,
               const std::vector<uint8_t> &gb_hand = {});
    bool ready() const { return texture_ != nullptr; }

    // GB style (black text on a white background, nothing fancier): while Graphics is
    // GB, windows are white with a black line, text is black without the font's shadow, and what the WSC
    // style dims is gray. The same calls draw either style.
    void set_gb(bool gb) { gb_ = gb; }
    bool gb() const { return gb_; }
    void set_enhanced(bool enhanced) { enhanced_ = enhanced; }
    static constexpr ImU32 kGbInk = IM_COL32(0x00, 0x00, 0x00, 0xFF),
                           kGbPaper = IM_COL32(0xFF, 0xFF, 0xFF, 0xFF),
                           kGbGray = IM_COL32(0x80, 0x80, 0x80, 0xFF),
                           kGbPale = IM_COL32(0xC0, 0xC0, 0xC0, 0xFF);

    // Palette colors (font palette 0) as ImGui colors; the GB style's own in its place.
    ImU32 white() const { return gb_ ? kGbInk : color(2); }
    ImU32 light() const { return gb_ ? kGbInk : color(8); } // #DDD
    ImU32 grey() const { return gb_ ? kGbGray : color(7); } // #777
    // Unselected choices: a third of the way from #777 to white (#777 alone sinks into the window blue).
    ImU32 dim() const {
        if (gb_)
            return kGbGray;
        const ImU32 a = white(), b = grey();
        auto ch = [&](int s) { return ImU32((((a >> s) & 0xFF) + 2 * ((b >> s) & 0xFF)) / 3) << s; };
        return ch(0) | ch(8) | ch(16) | (0xFFu << 24);
    }
    ImU32 dark() const { return gb_ ? kGbPale : color(6); }   // #222
    ImU32 black() const { return gb_ ? kGbPaper : color(1); } // #111
    ImU32 orange() const { return gb_ ? kGbInk : color(12); } // #FB0 (the highlighted window frame)
    ImU32 color(int index) const { return palette_[index & 15]; }

    // Text in the game font: 8 pixels per character. `tint` multiplies the white strokes (the black shadow
    // stays black). UTF-8; kana and the font's symbols are mapped, unknown characters draw as '?'. A button
    // token ({A}, {X1}...: engine/button_icons.h) draws as the button's icon.
    // scale: glyph size in font pixels per unit (1 = 8 units per character).
    void text(ImDrawList *dl, float x, float y, std::string_view utf8, ImU32 tint = IM_COL32_WHITE,
              float scale = 1.0f) const;
    static float text_width(std::string_view utf8, float scale = 1.0f); // units
    // A button icon with its box's top-left at (x, y): ink in `tint`, the WSC style's shadow down-right (GB:
    // none). Returns its advance in units (its width, the shadow and a 1-pixel gap). `label` (what it stands
    // for, "X1", "Enter") is what a layout audit sees: a Text draw over the icon.
    float icon(ImDrawList *dl, float x, float y, const ButtonIcon &icon, ImU32 tint = IM_COL32_WHITE,
               float scale = 1.0f, std::string_view label = {}) const;
    static float icon_advance(const ButtonIcon &icon, float scale = 1.0f) {
        return float(icon.w + 2) * scale;
    }
    // The icon box's top for text drawn at y: the 9-pixel box centered on the capitals.
    static constexpr int kIconRise = 1;
    // The same text straight through SDL (no ImGui frame), `scale` output pixels per font pixel: the
    // on-screen badges over the game picture.
    // gb: black strokes without the shadow (for a white box).
    void text_sdl(SDL_Renderer *renderer, float x, float y, std::string_view utf8, float scale,
                  float alpha = 1.0f, bool gb = false) const;
    // A window of w x h pixels (multiples of 8) at (x, y): the game's frame tiles around its blue texture.
    // sheer: the blue texture slightly see-through (the frame lines stay opaque), for the blurred backdrop.
    void frame(ImDrawList *dl, float x, float y, int w, int h, Frame style = Frame::White,
               bool sheer = false) const;
    // The game's 16x16 pointing hand (its tip at the right edge, vertically centered on y + 8).
    void hand(ImDrawList *dl, float x, float y, float scale = 1.0f) const;
    // One of the font's 256 tiles as a glyph (transparent background), e.g. 0xF0/0xF1, the up/down icons.
    void glyph_tile(ImDrawList *dl, float x, float y, int tile, ImU32 tint = IM_COL32_WHITE,
                    float scale = 1.0f) const;
    // A solid rectangle in a palette color (exact pixels; ImGui's own fills are anti-aliased).
    void rect(ImDrawList *dl, float x, float y, float w, float h, ImU32 col) const;
    // A segmented volume bar: `segments` cells `cell` units apart and `h` tall, the first `filled` lit.
    void bar(ImDrawList *dl, float x, float y, int segments, int filled, bool focused, float cell = 4,
             float h = 8) const;
    static float bar_width(int segments, float cell = 4) { return segments * cell; }

    ImTextureID texture_id() const { return ImTextureID(intptr_t(texture_)); }

  private:
    SDL_Texture *texture_ = nullptr;
    // The same atlas for the GB style: every glyph stroke white (to be tinted), no shadow.
    SDL_Texture *texture_gb_ = nullptr;
    bool gb_ = false, enhanced_ = false, has_gb_hand_ = false;
    std::array<uint64_t, 256> fill_masks_{};
    ImU32 palette_[16] = {};
    // ink: a glyph in the current style (the GB atlas in GB style); otherwise the game's own tile colors.
    void tile(ImDrawList *dl, float x, float y, int cell_x, int cell_y, float scale,
              ImU32 tint = IM_COL32_WHITE, bool ink = false) const;
    ImTextureID ink_texture() const { return ImTextureID(intptr_t(gb_ ? texture_gb_ : texture_)); }
    ImU32 ink_tint(ImU32 tint) const { return gb_ && tint == IM_COL32_WHITE ? kGbInk : tint; }
};

} // namespace sawgod::ui
