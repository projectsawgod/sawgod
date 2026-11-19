#include "engine/overlay.h"
#include "engine/options_text.h"

#include <algorithm>
#include <cmath>

#include "imgui.h"
#include "imgui_impl_sdlrenderer3.h"

namespace sawgod::ui {

namespace {

// Layout in menu units (384x224; menu text 10 units per character, the help line 8):
//   categories x 0..112    the category list, like the game's own menus
//   options    x 112..384  the selected category's options, 14 units apart
//   help       y 208..224  one line of 1x text between the frame lines, as in the game's own help windows
constexpr float kT = Overlay::kTextScale;
constexpr float kChar = kGlyph * kT; // 10
constexpr int kListW = 112;          // 14 tiles
constexpr int kPaneH = 208;
constexpr int kRowsX = kListW, kRowsW = Overlay::kWidth - kListW;
constexpr int kHelpY = kPaneH, kHelpH = 16;
constexpr float kFirstY = 10, kRowStep = 14; // top of the first entry's text; entry spacing
constexpr float kHand = 2 * kChar;           // the hand is 2x2 glyphs, left of the label
// The hand's top relative to its row's text: the finger (rows 0-3 of the hand's 16, middle 2) level with
// the middle of the letters with their shadow (rows 0-7 of 8, middle 4).
constexpr float kHandDY = (4.0f - 2.0f) * kT;
constexpr float kListTextX = 8; // text close to the frame; the hand pokes out past it
constexpr float kLabelX = kRowsX + 8;
constexpr float kHandX = kLabelX - kHand - 2;
constexpr uint32_t kWscButtonPink = 0xFBE6EC;
constexpr float kEnhancedIndent = 2 * kChar;        // Enhanced rows sit two characters in
constexpr float kValueRight = Overlay::kWidth - 10; // values are right-aligned to here
// Keyboard / Gamepad: wide action labels, then two equal binding slots. Long key names
// have compact display labels and fit at 1x; WSC button badges retain their existing size.
constexpr float kSlot1X = 252;
constexpr float kSlotW = (kValueRight - kSlot1X) / 2;
constexpr float kSlot2X = kSlot1X + kSlotW;
constexpr int kVisibleRows = 13; // reserve the bottom row for scroll markers

// The menu's own one-line texts (Overlay::fixed_help() lists them for the width check).
constexpr const char *kHelpVideo = "Graphics options";
constexpr const char *kHelpAudio = "Audio options";
constexpr const char *kHelpKeyboard = "Map WSC buttons to keyboard keys";
constexpr const char *kHelpGamepad = "Menu: {A} accept, {B} back; {Options}: Options";
constexpr const char *kHelpResetKeys = "Reset key bindings to its default mapping";
constexpr const char *kHelpResetPad = "Reset controller bindings to its default";
constexpr const char *kHelpNoInput = "Nothing pressed; binding unchanged";
constexpr const char *kHelpSystem = "System options";
constexpr const char *kHelpCheats = "Cheat options";
constexpr const char *kHelpDebug = "DEBUG OPTIONS";
constexpr const char *kHelpReset = "Reset the game; unsaved progress is lost";
constexpr const char *kHelpTeleport = "Go to a floor you've already visited ({A})";
constexpr const char *kHelpDefaults = "Reset every setting back to its default";
constexpr const char *kHelpQuit = "Save the battery RAM and quit the game";
constexpr const char *kHelpNoPad = "No gamepad connected";
constexpr const char *kHelpConfirm = "Press {A} again to confirm";
constexpr const char *kHelpApply = "Press {A} to apply, {B} to cancel";

std::string help_for(const CVar &v) {
    if (v.type == CVar::Type::Enum && size_t(v.get()) < v.choice_help.size() &&
        !v.choice_help[v.get()].empty())
        return v.choice_help[v.get()];
    return v.help;
}

std::string page_help(const std::string &title) {
    if (title == "Video")
        return kHelpVideo;
    if (title == "Audio")
        return kHelpAudio;
    if (title == "Debug")
        return kHelpDebug;
    if (title == "Cheats")
        return kHelpCheats;
    return title + " options";
}

bool hovering(float x, float y, float w, float h) {
    const ImVec2 m = ImGui::GetIO().MousePos;
    return m.x >= x && m.x < x + w && m.y >= y && m.y < y + h;
}

// The choices a Setting row shows side by side (value, label), or empty for a bar.
std::vector<std::pair<int, std::string>> choices_of(const CVar &v) {
    std::vector<std::pair<int, std::string>> out;
    if (!v.values.empty() && !v.bar) {
        for (size_t i = 0; i < v.values.size(); ++i)
            out.emplace_back(v.values[i], v.choice_labels[i]);
        return out;
    }
    if (!v.choice_order.empty()) {
        for (int i : v.choice_order)
            out.emplace_back(i, v.choice_labels[i]);
        return out;
    }
    switch (v.type) {
    case CVar::Type::Bool:
        out = {{1, "On"}, {0, "Off"}};
        break;
    case CVar::Type::Enum:
        for (int i = 0; i <= v.max; ++i)
            out.emplace_back(i, size_t(i) < v.choice_labels.size() ? v.choice_labels[i] : v.choices[i]);
        break;
    case CVar::Type::Int:
        if (!v.bar && (v.max - v.min) / v.step + 1 <= 8)
            for (int i = v.min; i <= v.max; i += v.step)
                out.emplace_back(i, std::to_string(i) + v.suffix);
        break;
    }
    return out;
}

// Bars apply as they move; choices wait for A (the current one stays white meanwhile).
bool applies_live(const CVar &v) { return choices_of(v).empty(); }

// The value `delta` steps from `from` (wrapping around the choices with `wrap`).
int step_value(const CVar &v, int from, int delta, bool wrap) {
    if (v.bar && !v.values.empty()) { // a bar over listed values (Message Speed): one cell per value
        const auto at = std::find(v.values.begin(), v.values.end(), from);
        const int i = at == v.values.end() ? 0 : int(at - v.values.begin());
        return v.values[size_t(std::clamp(i + delta, 0, int(v.values.size()) - 1))];
    }
    auto choices = choices_of(v);
    if (choices.empty())
        return std::clamp(from + delta * v.step * (v.bar_reverse ? -1 : 1), v.min, v.max);
    int i = 0;
    for (size_t k = 0; k < choices.size(); ++k)
        if (choices[k].first == from)
            i = int(k);
    const int n = int(choices.size());
    i = wrap ? ((i + delta) % n + n) % n : std::clamp(i + delta, 0, n - 1);
    return choices[i].first;
}

// The menu's text for `value` of `v`.
std::string value_label(const CVar &v, int value) {
    for (auto &[n, label] : choices_of(v))
        if (n == value)
            return label;
    return std::to_string(value) + v.suffix;
}

} // namespace

std::string Overlay::localize(std::string_view text) const {
    for (const auto &entry : kOptionsText)
        if (text == entry.english) {
            const auto fallback = settings_ && settings_->language && settings_->language->get() == 1
                                      ? entry.japanese
                                      : entry.english;
            return hooks_.host_text ? hooks_.host_text(entry.key, fallback) : std::string(fallback);
        }
    return std::string(text);
}
float Overlay::text_width(std::string_view text, float scale) const {
    return Skin::text_width(localize(text), scale);
}
void Overlay::draw_text(ImDrawList *dl, float x, float y, std::string_view text, ImU32 tint,
                        float scale) const {
    skin_.text(dl, x, y, localize(text), tint, scale);
}

void Overlay::release_renderer() {
    skin_.release();
    if (ctx_ && renderer_) {
        ImGui::SetCurrentContext(ctx_);
        ImGui_ImplSDLRenderer3_Shutdown();
    }
    renderer_ = nullptr;
}
bool Overlay::restore_renderer(SDL_Renderer *renderer) {
    if (!ctx_)
        return false;
    ImGui::SetCurrentContext(ctx_);
    renderer_ = renderer;
    return ImGui_ImplSDLRenderer3_Init(renderer);
}
void Overlay::shutdown() {
    release_renderer();
    if (ctx_) {
        ImGui::SetCurrentContext(ctx_);
        ImGui::DestroyContext(ctx_);
        ctx_ = nullptr;
    }
}

bool Overlay::init(SDL_Renderer *renderer, Config &config, Settings &settings, Hooks hooks) {
    renderer_ = renderer;
    config_ = &config;
    settings_ = &settings;
    hooks_ = std::move(hooks);
    IMGUI_CHECKVERSION();
    ctx_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx_);
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(kWidth, kHeight);
    io.DisplayFramebufferScale = ImVec2(1, 1);
    io.BackendPlatformName = "sawgod";
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    ImGui::GetStyle().WindowPadding = ImVec2(0, 0);
    ImGui::GetStyle().WindowBorderSize = 0;
    if (!ImGui_ImplSDLRenderer3_Init(renderer))
        return false;
    build_pages();
    return true;
}

bool Overlay::build_skin(const std::vector<uint8_t> &rom, const std::vector<uint8_t> &gb_hand) {
    return skin_.build(renderer_, rom, gb_hand);
}

bool Overlay::gb_style() const {
    return hooks_.gb_style ? hooks_.gb_style() : settings_ && settings_->gfx() == GfxMode::Gb;
}

void Overlay::build_pages() {
    pages_.clear();
    auto setting_page = [&](const std::string &cat) {
        Page p;
        p.title = cat;
        p.help = page_help(cat);
        std::vector<CVar *> vars;
        if (cat == "Video")
            vars = {settings_->gfx_mode,        settings_->fullscreen,          settings_->borderless,
                    settings_->pacing,          settings_->screen_mode,         nullptr,
                    settings_->enhanced_mode7,  settings_->enhanced_field_zoom, settings_->enhanced_dof,
                    settings_->battle_lighting, settings_->enhanced_bloom,      settings_->enhanced_lights,
                    settings_->enhanced_water};
        else if (cat == "Gameplay")
            vars = {settings_->skip_intro,   settings_->message_speed,
                    settings_->battle_speed, settings_->ff_speed,
                    settings_->ff_vfx,       settings_->b_button,
                    settings_->auto_target,  settings_->enemy_hp,
                    settings_->meat_preview, settings_->confirm_esper_changes,
                    settings_->y_button[0],  settings_->y_button[1],
                    settings_->y_button[2],  settings_->y_button[3]};
        else
            for (CVar &v : config_->vars())
                if (v.category == cat && !v.hidden)
                    vars.push_back(&v);
        bool enhanced = false;
        for (CVar *v : vars) {
            Row r;
            if (!v) {
                if (cat != "Video" || !settings_->gfx_mode)
                    continue;
                r.kind = Row::Kind::Header;
                r.label = "Enhanced";
                enhanced = true;
            } else {
                r.kind = Row::Kind::Setting;
                r.var = v;
                r.label = v->label;
                r.enhanced = enhanced;
            }
            p.rows.push_back(r);
        }
        pages_.push_back(std::move(p));
    };
    // Video, Audio, Controls, then the other setting categories (Gameplay...), System, Debug
    for (const std::string &cat : config_->categories())
        if (cat == "Video" || cat == "Audio")
            setting_page(cat);

    // Keyboard and Gamepad: the binding pages
    auto bind_page = [&](bool gamepad) {
        Page page;
        page.title = gamepad ? "Gamepad" : "Keyboard";
        page.help = gamepad ? kHelpGamepad : kHelpKeyboard;
        Row head;
        head.kind = Row::Kind::Header;
        head.columns = true;
        head.gamepad = gamepad;
        page.rows.push_back(head);
        const WscButton order[] = {WscButton::A,          WscButton::B,          WscButton::X1,
                                   WscButton::X2,         WscButton::X3,         WscButton::X4,
                                   WscButton::Y1,         WscButton::Y2,         WscButton::Y3,
                                   WscButton::Y4,         WscButton::Start,      WscButton::Options,
                                   WscButton::Encounters, WscButton::Minimap,    WscButton::FidelityDown,
                                   WscButton::FidelityUp, WscButton::FastForward};
        for (WscButton button : order) {
            const int b = int(button);
            if ((WscButton(b) == WscButton::FidelityDown || WscButton(b) == WscButton::FidelityUp) &&
                !kGfxSwapBuild)
                continue;
            Row r;
            r.kind = Row::Kind::Bind;
            r.button = b;
            r.gamepad = gamepad;
            r.label = wsc_button_label(WscButton(b));
            page.rows.push_back(r);
        }
        Row gap; // Reset Bindings sits apart from the bindings
        gap.kind = Row::Kind::Header;
        page.rows.push_back(gap);
        Row reset;
        reset.kind = Row::Kind::Action;
        reset.label = "Reset Bindings";
        reset.help = gamepad ? kHelpResetPad : kHelpResetKeys;
        reset.confirm = true;
        reset.act = [this, gamepad] {
            if (hooks_.map) {
                hooks_.map->reset(gamepad);
                if (gamepad && hooks_.input)
                    hooks_.input->suppress_gamepad_until_release();
                store_bindings();
            }
        };
        page.rows.push_back(reset);
        return page;
    };
    pages_.push_back(bind_page(false));
    Page controls = bind_page(true);
    Row pad;
    pad.kind = Row::Kind::Info;
    pad.label = "Gamepad";
    pad.help = kHelpNoPad;
    pad.value = [this] {
        return hooks_.input && hooks_.input->gamepad_count() > 0 ? std::string("Connected")
                                                                 : std::string("None");
    };
    pad.dyn_help = [this] {
        std::string n = hooks_.input ? hooks_.input->gamepad_name() : "";
        if (n.empty())
            return std::string(kHelpNoPad);
        if (n.size() > kHelpWidth)
            n.resize(kHelpWidth);
        return n;
    };
    controls.rows.insert(controls.rows.begin(), pad);
    if (settings_ && settings_->pad_icons) { // Icon Type, above the bindings
        Row icons;
        icons.kind = Row::Kind::Setting;
        icons.var = settings_->pad_icons;
        icons.label = icons.var->label;
        controls.rows.insert(controls.rows.begin() + 1, icons);
    }
    if (settings_ && settings_->swap_ab) {
        Row swap;
        swap.kind = Row::Kind::Setting;
        swap.var = settings_->swap_ab;
        swap.label = swap.var->label;
        controls.rows.insert(controls.rows.begin(), swap);
    }
    pages_.push_back(std::move(controls));
    for (const std::string &cat : config_->categories())
        if (cat != "Video" && cat != "Audio" && cat != "Debug" && cat != "Gamepad" &&
            cat != "System") { // Gamepad: on its binding page
            setting_page(cat);
            if (cat == "Cheats" && hooks_.teleport) { // Teleport: up to the highest floor reached
                Row tp;
                tp.kind = Row::Kind::Action;
                tp.label = "Teleport";
                tp.help = kHelpTeleport;
                tp.pick_max = hooks_.teleport_max;
                tp.pick_suffix = "F";
                tp.pick_act = [this](int floor) {
                    hooks_.teleport(floor);
                    set_open(false);
                    if (hooks_.close)
                        hooks_.close();
                };
                pages_.back().rows.push_back(tp);
            }
        }

    Page sys;
    sys.title = "System";
    sys.help = kHelpSystem;
    Row reset;
    reset.kind = Row::Kind::Action;
    reset.label = "Reset Game";
    reset.help = kHelpReset;
    reset.confirm = true;
    reset.act = [this] {
        if (hooks_.reset)
            hooks_.reset();
        set_open(false);
        if (hooks_.close)
            hooks_.close();
    };
    Row defaults;
    defaults.kind = Row::Kind::Action;
    defaults.label = "Reset to Default Settings";
    defaults.help = kHelpDefaults;
    defaults.confirm = true;
    defaults.act = [this] {
        config_->reset_to_defaults();
        if (hooks_.input)
            hooks_.input->suppress_gamepad_until_release();
        if (hooks_.changed)
            for (CVar &v : config_->vars())
                hooks_.changed(v);
    };
    Row quit;
    quit.kind = Row::Kind::Action;
    quit.label = "Quit Game";
    quit.help = kHelpQuit;
    quit.confirm = true;
    quit.act = [this] {
        if (hooks_.quit)
            hooks_.quit();
    };
    Row language;
    language.kind = Row::Kind::Setting;
    language.var = settings_->language;
    language.label = language.var->label;
    sys.rows = {language, defaults, reset, quit};
    pages_.push_back(std::move(sys));

    for (const std::string &cat : config_->categories())
        if (cat == "Debug")
            setting_page(cat);
}

int Overlay::pad_icons() const {
    return resolve_pad_icons(settings_ && settings_->pad_icons ? settings_->pad_icons->get() : 0, pad_type());
}

std::string Overlay::expand_help(std::string text) const {
    // The actual menu controls on the device the player is using, including legacy recovery.
    const bool pad = hooks_.input && hooks_.input->last_used_pad();
    auto icon = [&](SDL_GamepadButton button, const char *key) {
        return std::string("{icon:") +
               (pad ? binding_icon_name({Binding::Kind::PadButton, button}, pad_icons()) : key) + "}";
    };
    const InputMap defaults = InputMap::defaults();
    const InputMap &map = hooks_.map ? *hooks_.map : defaults;
    auto menu_bound = [&](WscButton button) {
        for (const Binding &b : map.menu_slots(button))
            if (!b.empty()) {
                const auto name = binding_icon_name(b, pad_icons());
                return name.empty() ? binding_label(b, pad_type()) : "{icon:" + name + "}";
            }
        return std::string("-");
    };
    const Binding north{Binding::Kind::PadButton, SDL_GAMEPAD_BUTTON_NORTH};
    const auto used = [&](WscButton b) {
        const auto s = map.menu_slots(b);
        return std::find(s.begin(), s.end(), north) != s.end();
    };
    const std::pair<std::string, std::string> kButtons[] = {
        {"{A}", pad ? menu_bound(WscButton::A) : icon(SDL_GAMEPAD_BUTTON_SOUTH, "key:X")},
        {"{B}", pad ? menu_bound(WscButton::B) : icon(SDL_GAMEPAD_BUTTON_EAST, "key:Z")},
        {"{Del}", pad && (used(WscButton::A) || used(WscButton::B) || used(WscButton::Options))
                      ? "-"
                      : icon(SDL_GAMEPAD_BUTTON_NORTH, "key:DEL")}};
    auto bound = [&](WscButton button) {
        return bound_button_text(hooks_.map ? *hooks_.map : defaults, button, pad, pad_icons(), pad_type());
    };
    const std::pair<std::string, std::string> actions[] = {
        {"{ModeDown}", bound(WscButton::FidelityDown)},
        {"{ModeUp}", bound(WscButton::FidelityUp)},
        {"{Options}", pad ? menu_bound(WscButton::Options) : bound(WscButton::Options)},
        {"{Fullscreen}", "{icon:key:ALT}+{icon:key:ENTER}"}};
    for (const auto &[from, to] : actions)
        for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
            text.replace(at, from.size(), to);
    for (const auto &[from, to] : kButtons)
        for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
            text.replace(at, from.size(), to);
    return text;
}

SDL_GamepadType Overlay::pad_type() const {
    return hooks_.input ? hooks_.input->gamepad_type() : SDL_GAMEPAD_TYPE_UNKNOWN;
}

namespace {

// The help line on a binding row: what the button is and how to rebind / clear it ({A}, {Del}: the menu's
// own buttons, Overlay::menu_buttons).
std::string bind_help(WscButton b) { return std::string(wsc_button_help(b)) + ". {A}: rebind, {Del}: clear"; }

// ... and while it waits for the key / button.
std::string capture_help(WscButton b, bool gamepad) {
    return std::string(wsc_button_help(b)) + ": press a " + (gamepad ? "button" : "key") +
           "; {Options} cancels";
}

std::string swapped_help(WscButton other) { return std::string("Swapped with ") + wsc_button_label(other); }

// A change that would leave the device with nothing that opens this menu is refused.
std::string last_options_help(bool gamepad) {
    return gamepad ? "Keep buttons for Accept, Cancel and Options" : "Options must keep one key";
}

} // namespace

std::vector<std::string> Overlay::fixed_help() {
    std::vector<std::string> out = {kHelpVideo,    kHelpAudio,   kHelpKeyboard, kHelpGamepad, kHelpResetKeys,
                                    kHelpResetPad, kHelpNoInput, kHelpSystem,   kHelpDebug,   kHelpReset,
                                    kHelpDefaults, kHelpQuit,    kHelpNoPad,    kHelpConfirm, kHelpApply,
                                    kHelpTeleport, kHelpCheats};
    // the binding rows
    for (int b = 0; b < kWscButtonCount; ++b)
        for (bool gamepad : {false, true}) {
            out.push_back(bind_help(WscButton(b)));
            out.push_back(capture_help(WscButton(b), gamepad));
            out.push_back(swapped_help(WscButton(b)));
            out.push_back(last_options_help(gamepad));
        }
    return out;
}

std::vector<Overlay::PageInfo> Overlay::model() const {
    std::vector<PageInfo> out;
    for (const Page &p : pages_) {
        PageInfo page{p.title, p.help, {}};
        for (const Row &r : p.rows)
            page.rows.push_back({RowInfo::Kind(int(r.kind)), r.label, r.var, r.button, r.gamepad, enabled(r),
                                 r.confirm, bool(r.pick_max)});
        out.push_back(std::move(page));
    }
    return out;
}

Overlay::View Overlay::view() const {
    return {page_,    row_,       slot_,        scroll_,  kVisibleRows,
            in_rows_, capturing_, pending_row_, pending_, confirm_row_};
}

void Overlay::store_bindings() {
    if (hooks_.map)
        hooks_.map->store(*config_);
}

void Overlay::start_capture() {
    if (!hooks_.input || !hooks_.map)
        return;
    const Row &r = pages_[page_].rows[row_];
    hooks_.input->start_capture(r.gamepad);
    capturing_ = true;
    capture_until_ = ImGui::GetTime() + kCaptureSeconds;
    status_.clear();
}

void Overlay::poll_capture() {
    if (!capturing_)
        return;
    bool cancelled = false;
    const std::optional<Binding> b = hooks_.input->take_captured(&cancelled);
    const Row *r = row_ >= 0 && row_ < int(pages_[page_].rows.size()) ? &pages_[page_].rows[row_] : nullptr;
    if (b && r && r->kind == Row::Kind::Bind && b->is_pad() == r->gamepad) {
        bool refused = false;
        const WscButton other = hooks_.map->assign(
            hooks_.map->effective_button(WscButton(r->button), r->gamepad), slot_, *b, &refused);
        store_bindings();
        // (a stick or trigger for Options is refused silently, as before)
        if (refused && !(WscButton(r->button) == WscButton::Options && b->kind == Binding::Kind::PadAxis)) {
            status_ = last_options_help(r->gamepad);
            status_until_ = ImGui::GetTime() + 2.5;
        } else if (other != WscButton::Count) {
            status_ = swapped_help(hooks_.map->effective_button(other, r->gamepad));
            status_until_ = ImGui::GetTime() + 2.5;
        }
        capturing_ = false;
    } else if (b || cancelled) {
        capturing_ = false;
    } else if (ImGui::GetTime() >= capture_until_) {
        hooks_.input->stop_capture();
        capturing_ = false;
        status_ = kHelpNoInput;
        status_until_ = ImGui::GetTime() + 2.5;
    }
}

void Overlay::set_open(bool open) {
    if (open && !open_) {
        in_rows_ = false;
        confirm_row_ = -1;
        for (Page &p : pages_) // pick rows start again from the current value
            for (Row &r : p.rows)
                r.pick = 0;
    }
    cancel_pending();
    if (capturing_ && hooks_.input)
        hooks_.input->stop_capture();
    capturing_ = false;
    open_ = open;
    if (hooks_.input)
        hooks_.input->set_menu_open(open_);
}

bool Overlay::show(const std::string &page, int row) {
    int index = -1;
    for (int i = 0; i < int(pages_.size()); ++i) {
        std::string a = pages_[i].title, b = page;
        for (char &c : a)
            c = char(std::tolower(uint8_t(c)));
        for (char &c : b)
            c = char(std::tolower(uint8_t(c)));
        if (a == b || std::to_string(i) == page)
            index = i;
    }
    if (index < 0)
        return false;
    set_page(index);
    in_rows_ = row >= 0 && !pages_[index].rows.empty();
    if (in_rows_) {
        row_ = std::min(row, int(pages_[index].rows.size()) - 1);
        if (!selectable(pages_[index].rows[row_]))
            move_row(+1);
        clamp_scroll();
    }
    return true;
}

void Overlay::mouse_event(const SDL_Event &e, float cx, float cy) {
    ImGui::SetCurrentContext(ctx_);
    ImGuiIO &io = ImGui::GetIO();
    switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION:
        io.AddMousePosEvent(cx, cy);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        io.AddMousePosEvent(cx, cy);
        int b = e.button.button == SDL_BUTTON_LEFT ? 0 : e.button.button == SDL_BUTTON_RIGHT ? 1 : -1;
        if (b >= 0)
            io.AddMouseButtonEvent(b, e.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        io.AddMouseWheelEvent(e.wheel.x, e.wheel.y);
        break;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        break;
    default:
        break;
    }
}

void Overlay::set_page(int page) {
    const int n = int(pages_.size());
    page_ = ((page % n) + n) % n;
    row_ = 0;
    while (row_ + 1 < int(pages_[page_].rows.size()) && !selectable(pages_[page_].rows[row_]))
        ++row_;
    scroll_ = 0;
    slot_ = 0;
    confirm_row_ = -1;
    cancel_pending();
}

bool Overlay::enabled(const Row &row) const {
    return !row.enhanced || settings_->gfx() == GfxMode::Enhanced || settings_->gfx() == GfxMode::Swap;
}

float Overlay::label_x(const Row &row) const { return kLabelX + (row.enhanced ? kEnhancedIndent : 0); }

bool Overlay::selectable(const Row &row) const { return row.kind != Row::Kind::Header && enabled(row); }

void Overlay::move_row(int delta) {
    const int n = int(pages_[page_].rows.size());
    if (n == 0)
        return;
    for (int tries = 0; tries < n; ++tries) { // (skipping headers)
        row_ = ((row_ + delta) % n + n) % n;
        if (selectable(pages_[page_].rows[row_]))
            break;
    }
    confirm_row_ = -1;
    cancel_pending();
    clamp_scroll();
}

void Overlay::clamp_scroll() {
    const int n = int(pages_[page_].rows.size());
    if (row_ < scroll_)
        scroll_ = row_;
    if (row_ >= scroll_ + kVisibleRows)
        scroll_ = row_ - kVisibleRows + 1;
    scroll_ = std::clamp(scroll_, 0, std::max(0, n - kVisibleRows));
}

// Left/Right on a row: a volume bar moves at once; any other setting gets a pending value for A to apply.
void Overlay::change(Row &row, int row_index, int delta) {
    if (!enabled(row))
        return;
    if (row.kind == Row::Kind::Bind)
        slot_ = std::clamp(slot_ + delta, 0, kBindSlots - 1);
    if (row.kind == Row::Kind::Action && row.pick_max) {
        const int max = row.pick_max();
        if (max > 0)
            row.pick = std::clamp(pick_of(row) + delta, 1, max);
        return;
    }
    if (row.kind != Row::Kind::Setting || !row.var)
        return;
    CVar &v = *row.var;
    if (applies_live(v)) {
        commit(v, step_value(v, v.get(), delta, false));
        return;
    }
    pending_ = step_value(v, pending_row_ == row_index ? pending_ : v.get(), delta, false);
    pending_row_ = pending_ == v.get() ? -1 : row_index; // back on the current value: nothing pending
}

// A pick row's number: the one picked, else the current floor, kept within 1..pick_max().
int Overlay::pick_of(Row &row) const {
    const int max = row.pick_max ? row.pick_max() : 0;
    if (max <= 0)
        return 0;
    int v = row.pick;
    if (v <= 0)
        v = hooks_.teleport_current ? hooks_.teleport_current() : 1;
    return std::clamp(v, 1, max);
}

void Overlay::commit(CVar &v, int value) {
    cancel_pending();
    if (value == v.get())
        return;
    config_->set(v, value);
    if (&v == settings_->swap_ab) {
        // Return before the confirming button becomes Back under the new mapping. All input routes
        // commit here; an unconfirmed choice still cancels in place.
        in_rows_ = false;
        confirm_row_ = -1;
        if (hooks_.input)
            hooks_.input->suppress_gamepad_until_release();
    }
    if (hooks_.changed)
        hooks_.changed(v);
}

void Overlay::activate(Row &row, int row_index) {
    if (!enabled(row))
        return;
    switch (row.kind) {
    case Row::Kind::Setting:
        if (!row.var)
            break;
        if (pending_row_ == row_index)
            commit(*row.var, pending_);
        else if (!choices_of(*row.var).empty())
            commit(*row.var, step_value(*row.var, row.var->get(), +1, true));
        break;
    case Row::Kind::Action:
        if (row.pick_max) {
            if (row.pick_max() > 0 && row.pick_act)
                row.pick_act(pick_of(row));
            break;
        }
        if (row.confirm && confirm_row_ != row_index) {
            confirm_row_ = row_index;
            return;
        }
        confirm_row_ = -1;
        if (row.act)
            row.act();
        break;
    case Row::Kind::Bind:
        start_capture();
        break;
    case Row::Kind::Info:
    case Row::Kind::Header:
        break;
    }
}

void Overlay::frame(const MenuActions &a, float dt) {
    ImGui::SetCurrentContext(ctx_);
    ImGuiIO &io = ImGui::GetIO();
    io.DeltaTime = dt > 0 ? dt : 1.0f / 75;
    io.DisplaySize = ImVec2(kWidth + kPadLeft, kHeight);

    if (hooks_.input)
        hooks_.input->set_menu_open(open_);
    // keyboard / gamepad: the game-menu model (A enters/changes, B backs out)
    const bool was_capturing = capturing_;
    poll_capture();
    if (open_ && !pages_.empty() && !capturing_ && !was_capturing) {
        if (a.prev_page)
            set_page(page_ - 1);
        if (a.next_page)
            set_page(page_ + 1);
        if (!in_rows_) {
            if (a.up)
                set_page(page_ - 1);
            if (a.down)
                set_page(page_ + 1);
            if ((a.accept || a.right) && !pages_[page_].rows.empty()) {
                in_rows_ = true;
                row_ = -1;
                move_row(+1); // the first selectable row
                scroll_ = 0;
            } else if (a.back) {
                set_open(false);
                if (hooks_.close)
                    hooks_.close();
            }
        } else {
            Page &p = pages_[page_];
            if (a.up)
                move_row(-1);
            if (a.down)
                move_row(+1);
            if (row_ >= 0 && row_ < int(p.rows.size()) && !selectable(p.rows[row_]))
                move_row(+1);
            if (row_ >= 0 && row_ < int(p.rows.size())) {
                Row &r = p.rows[row_];
                if (a.left)
                    change(r, row_, -1);
                if (a.right)
                    change(r, row_, +1);
                if (a.accept)
                    activate(r, row_);
                if (a.clear && r.kind == Row::Kind::Bind && hooks_.map) {
                    if (!hooks_.map->clear(hooks_.map->effective_button(WscButton(r.button), r.gamepad),
                                           r.gamepad, slot_)) {
                        status_ = last_options_help(r.gamepad);
                        status_until_ = ImGui::GetTime() + 2.5;
                    }
                    store_bindings();
                }
            }
            if (a.back) {
                if (pending_row_ >= 0)
                    cancel_pending();
                else if (confirm_row_ >= 0)
                    confirm_row_ = -1;
                else
                    in_rows_ = false;
            }
        }
    }

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui::NewFrame();
    if (open_ && skin_.ready()) {
        // the window starts kPadLeft left of the menu so the hand can stick out; render() shifts it back
        ImGui::SetNextWindowPos(ImVec2(-kPadLeft, 0));
        ImGui::SetNextWindowSize(ImVec2(kWidth + kPadLeft, kHeight));
        ImGui::Begin("##options", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_NoBringToFrontOnFocus);
        draw();
        ImGui::End();
    }
    ImGui::Render();
    // menu units -> canvas units: the canvas begins kPadLeft left of the menu
    if (ImDrawData *data = ImGui::GetDrawData())
        for (ImDrawList *list : data->CmdLists) {
            for (ImDrawVert &v : list->VtxBuffer)
                v.pos.x += kPadLeft;
            for (ImDrawCmd &c : list->CmdBuffer) {
                c.ClipRect.x =
                    0; // (ImGui clipped the window to the display, which started at the menu's edge)
                c.ClipRect.z += kPadLeft;
            }
        }
}

void Overlay::draw() {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    // smooth sampling of the enlarged atlas = sharp bilinear (ui_skin.cpp)
    dl->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerLinear, nullptr);
    ImGuiIO &io = ImGui::GetIO();
    const bool moved = io.MousePos.x != last_mouse_.x || io.MousePos.y != last_mouse_.y;
    last_mouse_ = io.MousePos;
    const bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    skin_.set_gb(gb_style());
    skin_.set_enhanced(hooks_.enhanced_style ? hooks_.enhanced_style()
                                             : settings_ && settings_->gfx() == GfxMode::Enhanced);
    const bool sheer = true; // the blurred game always shows faintly through the menu's windows
    std::string help;

    // --- category list ---
    skin_.frame(dl, 0, 0, kListW, kPaneH, Skin::Frame::White, sheer);
    for (int i = 0; i < int(pages_.size()); ++i) {
        const float y = kFirstY + i * kRowStep;
        if (hovering(0, y - 2, kListW, kRowStep)) {
            if (click) {
                set_page(i);
                in_rows_ = false;
            } else if (moved && !in_rows_ && i != page_) {
                set_page(i);
            }
        }
        const bool current = i == page_;
        draw_text(dl, kListTextX, y, pages_[i].title, current ? skin_.orange() : skin_.white(), kT);
        // One hand at a time, in the pane that has focus; the orange text marks the current page.
        if (current && !in_rows_) // (sticks out past the menu's left edge: the canvas has room for it,
                                  // Overlay::kPadLeft)
            skin_.hand(dl, kListTextX - kHand - 2, y + kHandDY, kT);
    }
    if (!pages_.empty())
        help = pages_[page_].help;

    // --- the page's rows ---
    skin_.frame(dl, kRowsX, 0, kRowsW, kPaneH, Skin::Frame::White, sheer);
    Page &p = pages_[page_];
    const int n = int(p.rows.size());
    if (hovering(kRowsX, 0, kRowsW, kPaneH) && io.MouseWheel != 0) {
        scroll_ = std::clamp(scroll_ - int(io.MouseWheel), 0, std::max(0, n - kVisibleRows));
        if (in_rows_) {
            row_ = std::clamp(row_, scroll_, std::min(n, scroll_ + kVisibleRows) - 1);
            if (!selectable(p.rows[row_]))
                move_row(+1);
        }
    }
    const SDL_GamepadType type = pad_type();
    const bool blink_on = std::fmod(ImGui::GetTime(), 0.6) < 0.4;
    // a binding slot's text: 1.25x like all menu text, 1x if that doesn't fit the column
    auto slot_text = [&](float x, float y, const std::string &t, ImU32 col) {
        if (text_width(t, kT) <= kSlotW)
            draw_text(dl, x, y, t, col, kT);
        else
            draw_text(dl, x, y + 1, t, col, 1.0f);
    };
    for (int i = scroll_; i < n && i < scroll_ + kVisibleRows; ++i) {
        const float y = kFirstY + (i - scroll_) * kRowStep;
        Row &r = p.rows[i];
        if (r.kind == Row::Kind::Header) {
            if (r.columns) {
                draw_text(dl, kLabelX, y, "WSC", skin_.dim(), kT);
                draw_text(dl, kSlot1X, y, r.gamepad ? "Pad 1" : "Key 1", skin_.dim(), kT);
                draw_text(dl, kSlot2X, y, r.gamepad ? "Pad 2" : "Key 2", skin_.dim(), kT);
            } else {
                draw_text(dl, kLabelX, y, r.label, skin_.dim(), kT);
            }
            continue;
        }
        if (enabled(r) && !capturing_ && hovering(kRowsX, y - 2, kRowsW, kRowStep) && (moved || click)) {
            if (r.kind == Row::Kind::Bind && ImGui::GetIO().MousePos.x >= kSlot1X - 4)
                slot_ = ImGui::GetIO().MousePos.x >= kSlot2X - 4 ? 1 : 0;
            if (!in_rows_ || row_ != i) {
                confirm_row_ = -1;
                cancel_pending();
            }
            in_rows_ = true;
            row_ = i;
        }
        const bool focused = in_rows_ && row_ == i;
        std::string row_help = r.help;
        if (r.kind == Row::Kind::Bind && hooks_.map) {
            // (as a slot's text: 1x when the label would reach the first slot)
            if (const ButtonIcon *badge = wsc_button_icon(WscButton(r.button))) {
                ButtonIcon wsc =
                    *badge; // the WSC's own buttons in a light pink, apart from pad and key icons
                wsc.body = kWscButtonPink;
                skin_.icon(dl, kLabelX, y - Skin::kIconRise * kT, wsc, skin_.white(), kT, r.label);
            } else if (text_width(r.label, kT) <= kSlot1X - kLabelX - 4)
                draw_text(dl, kLabelX, y, r.label, skin_.white(), kT);
            else
                draw_text(dl, kLabelX, y + 1, r.label, skin_.white(), 1.0f);
            const BindSlots &slots = hooks_.map->effective_slots(WscButton(r.button), r.gamepad);
            for (int k = 0; k < kBindSlots; ++k) {
                const float x = k == 0 ? kSlot1X : kSlot2X;
                const bool sel = focused && slot_ == k;
                if (sel && capturing_)
                    slot_text(x, y, "...", blink_on ? skin_.orange() : skin_.dim());
                else if (ButtonIcon icon = binding_icon(slots[k], pad_icons()); !icon.empty()) {
                    // The selected slot's button turns orange, as a light one takes the tint; a dark type's
                    // letter is then cut out of it, so the selected column always shows.
                    if (sel && icon.body)
                        icon.body = icon.shadow = icon.symbol = std::nullopt;
                    skin_.icon(dl, x, y - Skin::kIconRise * kT, icon, sel ? skin_.orange() : skin_.light(),
                               kT, binding_label(slots[k], type));
                } else
                    slot_text(x, y, binding_label(slots[k], type),
                              sel                ? skin_.orange()
                              : slots[k].empty() ? skin_.dim()
                                                 : skin_.light());
                if (click && !capturing_ && hovering(x - 4, y - 2, kSlotW + 8, kRowStep)) {
                    slot_ = k;
                    start_capture();
                }
            }
            row_help = capturing_ && focused ? capture_help(WscButton(r.button), r.gamepad)
                                             : bind_help(WscButton(r.button));
        } else {
            draw_text(dl, label_x(r), y, r.label, enabled(r) ? skin_.white() : skin_.dim(), kT);
            draw_value(dl, r, i, y, focused);
        }
        if (focused) {
            skin_.hand(dl, kHandX + label_x(r) - kLabelX, y + kHandDY, kT);
            if (pending_row_ == i)
                help = kHelpApply;
            else if (r.kind == Row::Kind::Setting && r.var) {
                help = help_for(*r.var);
            } else
                help = r.dyn_help ? r.dyn_help() : row_help;
            if (confirm_row_ == i)
                help = kHelpConfirm;
        }
    }
    if (!status_.empty() && ImGui::GetTime() < status_until_ && !capturing_)
        help = status_;
    if (scroll_ > 0)
        skin_.glyph_tile(dl, kValueRight - 2 * kChar - 4, kHelpY - kChar - 2, 0xF0, IM_COL32_WHITE, kT);
    if (scroll_ + kVisibleRows < n)
        skin_.glyph_tile(dl, kValueRight - kChar, kHelpY - kChar - 2, 0xF1, IM_COL32_WHITE, kT);

    // --- help line (same translucent chrome, 1x text) ---
    skin_.frame(dl, 0, kHelpY, kWidth, kHelpH, Skin::Frame::White, sheer);
    help = expand_help(localize(help));
    draw_text(dl, 8, kHelpY + 4, help);
}

void Overlay::draw_value(ImDrawList *dl, Row &r, int row_index, float y, bool focused) {
    ImGuiIO &io = ImGui::GetIO();
    const bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    if (r.kind == Row::Kind::Info) {
        const std::string v = r.value ? r.value() : "";
        draw_text(dl, kValueRight - text_width(v, kT), y, v, skin_.light(), kT);
        return;
    }
    if (r.kind == Row::Kind::Action && r.pick_max) { // <4F>, or "-" with nothing to pick
        const int max = r.pick_max();
        const std::string v = max > 0 ? "<" + std::to_string(pick_of(r)) + r.pick_suffix + ">" : "-";
        draw_text(dl, kValueRight - text_width(v, kT), y, v, focused ? skin_.orange() : skin_.white(), kT);
        if (click && max > 0 && hovering(kRowsX + kRowsW / 2, y - 2, kRowsW / 2, kRowStep))
            r.pick = pick_of(r) % max + 1;
        else if (click && hovering(kRowsX, y - 2, kRowsW / 2, kRowStep))
            activate(r, row_index);
        return;
    }
    if (r.kind == Row::Kind::Action) {
        const std::string v = confirm_row_ == row_index ? "Sure?" : "";
        draw_text(dl, kValueRight - text_width(v, kT), y, v, skin_.orange(), kT);
        if (click && hovering(kRowsX, y - 2, kRowsW, kRowStep))
            activate(r, row_index);
        return;
    }
    CVar &v = *r.var;
    if (!enabled(r)) { // an Enhanced row while Graphics is GB or WSC
        const std::string label = "N/A";
        draw_text(dl, kValueRight - text_width(label, kT), y, label, skin_.dim(), kT);
        return;
    }
    const ImU32 selected = focused ? skin_.orange() : skin_.white();
    // a pending value (not applied until A) blinks in orange; the current one stays white meanwhile
    const bool pending = pending_row_ == row_index;
    int shown = pending ? pending_ : v.get();
    const bool blink_on = std::fmod(ImGui::GetTime(), 0.6) < 0.4; // (as in draw())
    const ImU32 shown_col = pending ? (blink_on ? skin_.orange() : skin_.dim()) : selected;
    auto choices = choices_of(v);
    if (!choices.empty()) {
        float text_w = 0;
        for (auto &c : choices)
            text_w += text_width(c.second, kT);
        const int gaps = int(choices.size()) - 1;
        // room for a value: right of the label and a 2-character gap
        const float room = kValueRight - (label_x(r) + text_width(r.label, kT) + 2 * kChar);
        const float gap = text_w + 2 * kChar * gaps <= room ? 2 * kChar : kChar;
        if (text_w + gap * gaps <= room) {
            float x = kValueRight - (text_w + gap * gaps);
            for (auto &[value, label] : choices) {
                const float w = text_width(label, kT);
                const ImU32 col = value == shown ? shown_col : value == v.get() ? skin_.white() : skin_.dim();
                draw_text(dl, x, y, label, col, kT);
                if (click && hovering(x - 3, y - 2, w + 6, kRowStep)) // (a click is as good as A)
                    commit(v, value);
                x += w + gap;
            }
            return;
        }
        // too long to list: < value >, or <value> if even that is too wide
        std::string label = "< " + localize(value_label(v, shown)) + " >";
        if (text_width(label, kT) > room)
            label = "<" + localize(value_label(v, shown)) + ">";
        draw_text(dl, kValueRight - text_width(label, kT), y, label, shown_col, kT);
        if (click && hovering(kRowsX + kRowsW / 2, y - 2, kRowsW / 2, kRowStep))
            commit(v, step_value(v, shown, +1, true));
        return;
    }
    // a bar with the number after it; with end words ("Slow ... Fast") a cell per value, the first lit at the
    // minimum, and the words either side of it
    const bool ends = !v.bar_low.empty();
    const bool listed = !v.values.empty(); // a cell per listed value, in list order
    const auto at = std::find(v.values.begin(), v.values.end(), v.get());
    const int index = at == v.values.end() ? 0 : int(at - v.values.begin());
    const int steps = listed ? int(v.values.size()) - 1 : (v.max - v.min) / v.step;
    const int segments = std::min(steps, 10) + (ends ? 1 : 0);
    const int position_value = v.bar_reverse ? v.max + v.min - v.get() : v.get();
    const int filled = listed ? index + 1
                              : int(std::lround(float(position_value - v.min) / float(v.max - v.min) *
                                                float(segments - (ends ? 1 : 0)))) +
                                    (ends ? 1 : 0);
    const std::string num = ends ? "" : v.display(); // (with end words the bar alone shows the value)
    draw_text(dl, kValueRight - text_width(num, kT), y, num, selected, kT);
    const float kCell = ends ? 6 : 8;
    const float bw = Skin::bar_width(segments, kCell);
    float right = kValueRight;
    if (ends) {
        right -= text_width(v.bar_high, kT);
        draw_text(dl, right, y, v.bar_high, skin_.dim(), kT);
        right -= kChar / 2;
    } else {
        right -= std::max<float>(3 * kChar, text_width(num, kT)) + kChar;
    }
    const float bx = right - bw;
    skin_.bar(dl, bx, y + 1, segments, filled, focused, kCell, 9);
    if (ends)
        draw_text(dl, bx - kChar / 2 - text_width(v.bar_low, kT), y, v.bar_low, skin_.dim(), kT);
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && hovering(bx - 8, y - 2, bw + 10, kRowStep)) {
        const float mx = io.MousePos.x - bx;
        int cells = mx < 3 ? 0 : std::clamp(int(mx / kCell) + 1, 0, segments);
        if (ends)
            cells = std::max(cells - 1, 0);
        const int position = ends ? cells : int(std::lround(std::clamp(mx / bw, 0.f, 1.f) * steps));
        if (listed)
            commit(v, v.values[size_t(std::clamp(position, 0, int(v.values.size()) - 1))]);
        else
            commit(v, v.bar_reverse ? v.max - position * v.step : v.min + position * v.step);
    }
}

void Overlay::render(SDL_Renderer *renderer, float scale) {
    ImGui::SetCurrentContext(ctx_);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    SDL_SetRenderScale(renderer, scale, scale);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_SetRenderScale(renderer, 1, 1);
}

} // namespace sawgod::ui
