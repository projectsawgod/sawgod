// The options overlay: a Dear ImGui menu skinned with the game's font, window frames and pointing hand
// (engine/ui_skin.h). It is laid out in menu units -- half a game pixel -- on a 384x224 area: the game
// picture less 2 tiles on every side. The front end places that area over the game picture at the game's
// scale and renders the menu straight at the window's resolution (render(renderer, scale)).
//
// Layout, like the game's own menus: the categories in a list on the left (Video, Audio, Keyboard, Gamepad,
// Gameplay, Cheats, System, and Debug in builds with SAWGOD_DEBUG_MENU), the selected category's
// options on the right, and a one-line help window along the bottom with the description of whatever the
// cursor (or the mouse) is on. Menu text is 1.25x the game font; the help line is 1x, so it holds kHelpWidth
// (46) characters.
//
// Controls: Up/Down move the hand; A (X key, Enter, Space, the effective gamepad A binding) or Right enters a
// category; Left/Right pick an option's new value, which only takes effect when A is pressed (the pending
// choice blinks and the help line says so; B or moving off the option drops it). The volume bars are the
// exception, along with Icon Type, which also applies on mouse hover: they apply as they move. A on an option
// with nothing pending toggles / cycles it at once. B (Z, Backspace, effective gamepad B) goes back,
// and from the category list closes the menu. Q/E, Tab, PageUp/PageDown or the pad's shoulder buttons switch
// category from anywhere. The mouse works too: hover and click categories and rows, click a choice or a point
// on a volume bar, wheel to scroll. Esc / F1 / the pad's Select button open and close it (handled by the
// front end).
//
// Keyboard and Gamepad pages: a row per WSC button with two binding slots. Left/Right pick a slot, A
// waits for the next key (or pad button / stick / trigger) and binds it -- whatever opens Options cancels,
// and it gives up after kCaptureSeconds; an input already on another button swaps with this slot's old
// binding. Delete or the pad's north button clears a slot (active Accept/Cancel take priority). Swap A/B
// exchanges effective gamepad slots without rewriting them; confirming a change returns category focus.
// A/B precede the X/Y pads on both binding pages. Keyboard controls and menu directions stay
// fixed; edits preserve usable gamepad Accept/Cancel/Options buttons, with recovery for legacy mappings.
#pragma once

#include <SDL3/SDL.h>

#include <functional>
#include <string>
#include <vector>

#include "engine/config.h"
#include "engine/input.h"
#include "engine/settings.h"
#include "engine/ui_skin.h"

struct ImGuiContext;

namespace sawgod::ui {

class Overlay {
  public:
    static constexpr int kWidth = 384, kHeight = 224; // menu units (half a game pixel)
    static constexpr float kTextScale = 1.25f;        // menu text vs the game font; the help line is 1x
    // The canvas also has this many units left of the menu, where the pointing hand may stick out.
    static constexpr int kPadLeft = 16;

    struct Hooks {
        std::function<void(CVar &)> changed; // a setting changed (apply it now)
        std::function<void()> reset;         // System > Reset Game, confirmed
        std::function<void()> quit;          // System > Quit Game, confirmed
        std::function<void()> close;         // the menu closed itself (B on the category list, after a reset)
        Input *input = nullptr;              // the pads (their button names) and binding capture
        InputMap *map = nullptr;             // the bindings the Keyboard and Gamepad pages edit
        // Modifiers > Teleport: the highest floor it offers (0: none, e.g. no game running), its
        // starting pick (the current floor), and the request (then the menu closes)
        std::function<int()> teleport_max, teleport_current;
        std::function<void(int)> teleport;
        // Whether the picture is in GB graphics right now (the app knows the Swap track's level). Unset:
        // the Graphics setting itself. The menu then draws in the GB style, black on white.
        std::function<bool()> gb_style, enhanced_style;
        // Host localization lookup; fallback is used by ROM-free pilots.
        std::function<std::string(std::string_view, std::string_view)> host_text;
    };

    ~Overlay() { shutdown(); }
    void shutdown();         // frees ImGui and the textures; call before destroying the renderer
    void release_renderer(); // retain the open page, cursor and pending edits across GPU loss
    bool restore_renderer(SDL_Renderer *renderer);
    bool init(SDL_Renderer *renderer, Config &config, Settings &settings, Hooks hooks);
    // Builds the skin from the ROM image (after start-up).
    bool build_skin(const std::vector<uint8_t> &rom, const std::vector<uint8_t> &gb_hand = {});
    bool ready() const { return skin_.ready(); }
    const Skin &skin() const { return skin_; } // (the on-screen badges draw with it too)
    bool gb_style() const;                     // the style the menu and the badges use now

    bool is_open() const { return open_; }
    void set_open(bool open);
    // Puts the cursor on a category (by index or case-insensitive title) and, with row >= 0, on one of its
    // rows (for --shot-overlay screenshots). Returns false for an unknown category.
    bool show(const std::string &page, int row);

    // Mouse input in menu units (the front end maps window coordinates through the menu's placement).
    void mouse_event(const SDL_Event &e, float x, float y);
    // Runs one menu frame with this frame's actions.
    void frame(const MenuActions &actions, float dt);
    // Draws it into the current render target, `scale` output pixels per menu unit; the target's origin is
    // kPadLeft units left of the menu's left edge.
    void render(SDL_Renderer *renderer, float scale, bool retain_canvas = false);
    void invalidate_canvas() { canvas_snapshot_.clear(); }
    uint64_t canvas_renders = 0;

    // The menu as built and where its cursor is, for tests and tools (the UI test pilot generates its
    // element inventory from this). Read-only.
    struct RowInfo {
        enum class Kind { Setting, Action, Info, Header, Bind } kind = Kind::Info;
        std::string label;
        const CVar *var = nullptr; // Setting
        int button = -1;           // Bind: the WscButton
        bool gamepad = false;      // Bind
        bool enabled = true;
        bool confirm = false, pick = false; // Action: needs a second press / picks a number
    };
    struct PageInfo {
        std::string title, help;
        std::vector<RowInfo> rows;
    };
    std::vector<PageInfo> model() const;
    struct View {
        int page = 0, row = 0, slot = 0, scroll = 0, visible_rows = 0;
        bool in_rows = false, capturing = false;
        int pending_row = -1, pending = 0, confirm_row = -1;
    };
    View view() const;

    // Every fixed help text the menu shows besides the settings' own (for the one-line width check).
    static std::vector<std::string> fixed_help();
    // Fixed menu controls and the current device’s bound game actions as help-line icons/keys.
    std::string expand_help(std::string text) const;
    static constexpr double kCaptureSeconds = 5; // waiting for a key to bind gives up after this

  private:
    std::vector<unsigned char> canvas_snapshot_, canvas_scratch_;
    std::string localize(std::string_view text) const;
    float text_width(std::string_view text, float scale = 1.0f) const;
    void draw_text(ImDrawList *dl, float x, float y, std::string_view text, ImU32 tint = IM_COL32_WHITE,
                   float scale = 1.0f) const;
    struct Row {
        // Header: not selectable; Bind: a WSC button's binding slots
        enum class Kind { Setting, Action, Info, Header, Bind } kind = Kind::Info;
        CVar *var = nullptr;
        std::string label, help;
        int button = -1;                       // Bind: the WscButton
        bool gamepad = false;                  // Bind, Header: the gamepad's slots (else the keyboard's)
        bool enhanced = false;                 // requires Enhanced or Swap graphics
        bool columns = false;                  // Header: the slot column titles
        std::function<std::string()> value;    // Info without a binding: the value text
        std::function<std::string()> dyn_help; // Info: overrides help when set
        std::function<void()> act;             // Action
        bool confirm = false;                  // Action: needs a second press
        std::function<int()> pick_max;     // Action: a number 1..pick_max() picked with Left/Right (0: none)
        std::function<void(int)> pick_act; // ... and A runs it
        std::string pick_suffix;           // ... shown after the number
        int pick = 0;                      // the picked number (0: not picked yet)
    };
    int pick_of(Row &row) const;
    struct Page {
        std::string title, help;
        std::vector<Row> rows;
    };

    ImGuiContext *ctx_ = nullptr;
    SDL_Renderer *renderer_ = nullptr;
    Config *config_ = nullptr;
    Settings *settings_ = nullptr;
    Hooks hooks_;
    Skin skin_;
    std::vector<Page> pages_;

    bool open_ = false;
    int page_ = 0;
    bool in_rows_ = false; // focus: false = the category list, true = the page's rows
    int row_ = 0;
    int scroll_ = 0;       // first visible row
    int confirm_row_ = -1; // an Action row waiting for its confirming second press
    int pending_row_ = -1; // a Setting row with a value picked but not applied yet (A applies it)
    int pending_ = 0;      // that value
    ImVec2 last_mouse_{-1, -1};
    int slot_ = 0;             // Bind rows: the selected slot
    bool capturing_ = false;   // waiting for a key / button to bind to slot_ of row_
    double capture_until_ = 0; // ImGui time
    std::string status_;       // a short note on the help line ("Swapped with B")
    double status_until_ = 0;

    void build_pages();
    SDL_GamepadType pad_type() const;
    int pad_icons() const; // Gameplay input.pad_icons: the Icon Type
    void start_capture();
    void poll_capture();
    void store_bindings();
    void change(Row &row, int row_index, int delta);
    void commit(CVar &v, int value);
    void cancel_pending() { pending_row_ = -1; }
    void activate(Row &row, int row_index);
    void set_page(int page);
    bool enabled(const Row &row) const;
    bool selectable(const Row &row) const;
    float label_x(const Row &row) const; // Enhanced rows are indented
    void move_row(int delta);
    void clamp_scroll();
    void draw();
    void draw_value(ImDrawList *dl, Row &row, int row_index, float y, bool focused);
};

} // namespace sawgod::ui
