#include <algorithm>
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "app/present_clock.h"
#include "emu/wsc.h"
#include "engine/config.h"
#include "engine/input.h"
#include "engine/options_text.h"
#include "engine/overlay.h"
#include "engine/settings.h"
#include "game/setup.h"

TEST_CASE("settings default to the WSC behaviour and an empty file changes nothing") {
    sawgod::Config c;
    sawgod::Settings s;
    s.register_all(c);
    c.from_toml("");
    CHECK(s.window_width->get() == 224 * sawgod::kDefaultWindowScale);
    CHECK(s.window_height->get() == 144 * sawgod::kDefaultWindowScale);
    CHECK(!s.fullscreen->on());
    CHECK(s.screen() == sawgod::ScreenMode::Ips);
    CHECK(s.pacing_mode() == sawgod::Pacing::Smooth);
    CHECK(s.enhanced_field_zoom->get() == 125);
    CHECK(s.master_gain() == 1.0f);
    CHECK(s.music_gain() == 1.0f);
    CHECK(s.sfx_gain() == 1.0f);
    CHECK(s.enemy_hp->on());
    CHECK_FALSE(s.swap_ab->on());
    CHECK(s.swap_ab->choice_labels[0] == "Off");
    CHECK(s.pad_icons->get() == 0);
    CHECK(s.pad_icons->choices == std::vector<std::string>{"auto", "xbox", "ps", "switch"});
    CHECK(s.pad_icons->choice_labels == std::vector<std::string>{"Auto", "Xbox", "PS", "Switch"});
    CHECK(s.enhanced_lights->category == "Video");
    CHECK(s.enhanced_lights->label == "Lighting FX");
    CHECK(s.enhanced_water->category == "Video");
    CHECK(s.enhanced_water->label == "Water FX");
    CHECK(s.message_speed->get() == 1);
    CHECK(s.message_speed->values == std::vector<int>{8, 5, 3, 2, 1, 11});
    CHECK(!c.dirty());
}

TEST_CASE("config round-trips through TOML, keeps unknown keys and clamps values") {
    sawgod::Config c;
    sawgod::Settings s;
    s.register_all(c);
    c.set(*s.window_width, 900);
    c.set(*s.pacing, int(sawgod::Pacing::Exact));
    c.set(*s.mute, 1);
    std::string text = c.to_toml();
    text += "\n[future]\nthing = \"kept\"\n";

    sawgod::Config d;
    sawgod::Settings t;
    t.register_all(d);
    std::vector<std::string> warnings;
    d.from_toml(text + "[video]\nborderless = maybe\n", &warnings);
    CHECK(t.window_width->get() == 900);
    CHECK(t.pacing_mode() == sawgod::Pacing::Exact);
    CHECK(t.mute->on());
    CHECK(!t.borderless->on());
    CHECK(warnings.size() == 1);
    CHECK(d.to_toml().find("thing = \"kept\"") != std::string::npos);

    d.from_toml("[audio]\nmaster_volume = 250\n");
    CHECK(t.master_volume->get() == 100);
}

TEST_CASE("effect toggles keep their On strengths and load every nonzero percentage as On") {
    for (int old : {0, 1, 10, 40, 60, 100, 300, -1}) {
        sawgod::Config c;
        sawgod::Settings s;
        s.register_all(c);
        std::vector<std::string> warnings;
        c.from_toml("[enhanced]\nbloom = " + std::to_string(old) + "\nlights = " + std::to_string(old) +
                        "\nwater = " + std::to_string(old) + "\n",
                    &warnings);
        CHECK(warnings.empty());
        CHECK(s.enhanced_bloom->get() == (old ? 40 : 0));
        CHECK(s.enhanced_lights->get() == (old ? 100 : 0));
        CHECK(s.enhanced_water->get() == (old ? 100 : 0));
        CHECK(s.bloom_gain() == (old ? 2.f / 3 * 1.25f : 0.f));
        sawgod::Config loaded;
        sawgod::Settings other;
        other.register_all(loaded);
        loaded.from_toml(c.to_toml(), &warnings);
        CHECK(warnings.empty());
        CHECK(other.enhanced_bloom->get() == s.enhanced_bloom->get());
    }
}

TEST_CASE("off-list values load as the nearest current setting without warnings") {
    sawgod::Config c;
    sawgod::Settings s;
    s.register_all(c);
    std::vector<std::string> warnings;
    for (const char *gold : {"1", "2", "4"}) {
        c.from_toml(std::string("[modifiers]\ngold = \"") + gold + "\"\n", &warnings);
        CHECK(s.gold_percent() == (std::string(gold) == "4" ? 400 : std::string(gold) == "2" ? 200 : 100));
    }
    for (auto [old, expected] : {std::pair{125, 100}, {150, 100}, {175, 200}, {200, 200}, {400, 400}}) {
        c.from_toml("[modifiers]\nstat_boost = " + std::to_string(old) + "\n", &warnings);
        CHECK(s.stat_growth->get() == expected);
    }
    for (auto [old, expected] : {std::pair{4, 5}, {6, 5}, {7, 8}, {11, 11}}) {
        c.from_toml("[gameplay]\nmessage_speed = " + std::to_string(old) + "\n", &warnings);
        CHECK(s.message_speed->get() == expected);
    }
    c.from_toml("[gameplay]\nbattle_transparency = 100\n", &warnings);
    CHECK(s.battle_transparency->hidden);
    CHECK(s.battle_transparency->get() == 30);
    CHECK(s.minimap->hidden);
    CHECK(warnings.empty());
}

TEST_CASE("command-line overrides are used but not saved") {
    sawgod::Config c;
    sawgod::Settings s;
    s.register_all(c);
    c.override_session(*s.window_width, 896);
    CHECK(s.window_width->get() == 896);
    CHECK(c.to_toml().find("window_width = 1120") != std::string::npos);
    c.set(*s.window_width, 1344); // a change replaces the override and is saved
    CHECK(s.window_width->get() == 1344);
    CHECK(c.to_toml().find("window_width = 1344") != std::string::npos);
}

TEST_CASE("every option description fits the overlay's one help line") {
    sawgod::Config c;
    sawgod::Settings s;
    s.register_all(c);
    for (const std::string &problem : c.overlong_help())
        FAIL_CHECK("longer than " << sawgod::kHelpWidth << " characters: " << problem);
    sawgod::ui::Overlay overlay;
    for (const std::string &h : sawgod::ui::Overlay::fixed_help())
        CHECK_MESSAGE(sawgod::ui::Skin::text_width(overlay.expand_help(h)) <= sawgod::kHelpWidth * 8, h);
}

TEST_CASE("gamepad buttons are shown with the pad's own names, never directions") {
    using sawgod::ui::pad_button_name;
    CHECK(pad_button_name(SDL_GAMEPAD_TYPE_XBOXONE, SDL_GAMEPAD_BUTTON_EAST) == "B");
    CHECK(pad_button_name(SDL_GAMEPAD_TYPE_PS5, SDL_GAMEPAD_BUTTON_EAST) == "Circle");
    CHECK(pad_button_name(SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO, SDL_GAMEPAD_BUTTON_EAST) == "A");
    CHECK(pad_button_name(SDL_GAMEPAD_TYPE_UNKNOWN, SDL_GAMEPAD_BUTTON_SOUTH) == "A");
    CHECK(pad_button_name(SDL_GAMEPAD_TYPE_PS4, SDL_GAMEPAD_BUTTON_BACK) == "Share");
    using sawgod::ui::Binding;
    for (int t = SDL_GAMEPAD_TYPE_UNKNOWN; t < SDL_GAMEPAD_TYPE_COUNT; ++t)
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
            const std::string label =
                sawgod::ui::binding_label({Binding::Kind::PadButton, b, 0}, SDL_GamepadType(t));
            for (const char *word : {"north", "south", "east", "west", "North", "South", "East", "West"})
                CHECK(label.find(word) == std::string::npos);
        }
}

TEST_CASE("button icons: the set loads, keys become keycaps, pads follow the Icon Type, tokens parse") {
    using namespace sawgod;
    using ui::Binding;
    const ButtonIcon *a = button_icon("pad A");
    REQUIRE(a != nullptr);
    CHECK(a->w == 9);
    CHECK(a->h == kButtonIconHeight);
    CHECK(button_icon("WSC X1") != nullptr);
    CHECK(keycap_icon("W").w == 7); // 3x5 letter, two columns of key each side
    CHECK(keycap_icon("F1").w == 11);
    CHECK(keycap_icon("\xC3\xA9").empty()); // outside the lettering: the name shows instead
    const Binding south{Binding::Kind::PadButton, SDL_GAMEPAD_BUTTON_SOUTH, 0};
    CHECK(ui::binding_icon_name(south, 3) == "n:pad B");
    CHECK(ui::binding_icon_name(south, 1) == "x:pad A");
    CHECK(ui::binding_icon_name(south, 2) ==
          "p:PS cross"); // by position: PlayStation's bottom button is cross
    CHECK(ui::binding_icon_name({Binding::Kind::Key, SDL_SCANCODE_W, 0}, 0) == "key:W");
    CHECK(ui::binding_icon_name({Binding::Kind::PadButton, SDL_GAMEPAD_BUTTON_GUIDE, 0}, 0).empty());
    CHECK(ui::binding_icon_name({Binding::Kind::PadAxis, SDL_GAMEPAD_AXIS_LEFTY, -1}, 3) == "n:L stick up");
    CHECK(ui::resolve_pad_icons(0, SDL_GAMEPAD_TYPE_PS5) == 2); // Auto: the pad's own
    CHECK(ui::resolve_pad_icons(0, SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO) == 3);
    CHECK(ui::resolve_pad_icons(0, SDL_GAMEPAD_TYPE_UNKNOWN) == 1);
    CHECK(ui::resolve_pad_icons(2, SDL_GAMEPAD_TYPE_XBOXONE) == 2);
    // the Icon Types' colors: the letter or shape is the disc's enclosed Symbol pixels
    CHECK(std::count(a->px.begin(), a->px.end(), ButtonIcon::Symbol) > 0);
    CHECK(!a->body);
    const ButtonIcon xa = named_icon("x:pad A"), pc = named_icon("p:PS circle"), nb = named_icon("n:pad B");
    CHECK(xa.px == a->px);
    CHECK(
        (xa.body && xa.symbol && *xa.symbol == 0x5DC21E && xa.shadow)); // dark, a green letter, a gray shadow
    CHECK((!pc.body && pc.symbol && *pc.symbol == 0xF25C54 && !pc.shadow)); // light, a red circle
    CHECK((nb.body && nb.symbol && *nb.symbol == 0xDDDDDD));                // dark, a light letter
    const ButtonIcon up = named_icon("x:L stick up");
    CHECK(std::count(up.px.begin(), up.px.end(), ButtonIcon::InkNoShadow) > 0); // the arc, in orange
    CHECK((up.motion && *up.motion == 0xF0901E));
    const ButtonIcon key_a = keycap_icon("A");
    CHECK(std::count(key_a.px.begin(), key_a.px.end(), ButtonIcon::Symbol) == 0);
    TextToken t;
    REQUIRE(text_token_at(std::string_view("{B} Back"), &t));
    CHECK(t.button == int(ui::WscButton::B));
    CHECK(t.len == 3);
    REQUIRE(text_token_at(std::u32string_view(U"{icon:key:DEL}"), &t));
    CHECK(t.icon == "key:DEL");
    CHECK(text_token_text(t) == "DEL");
    CHECK(!text_token_at(std::string_view("{1} hits"), &t)); // a template argument, not a button
}

TEST_CASE("bindings: defaults, swap on conflict, clear, reset") {
    using namespace sawgod::ui;
    InputMap m = InputMap::defaults();
    const Binding z{Binding::Kind::Key, SDL_SCANCODE_Z, 0}, x{Binding::Kind::Key, SDL_SCANCODE_X, 0};
    CHECK(m.slots(WscButton::A, false)[0] == x);
    CHECK(m.slots(WscButton::B, false)[0] == z);
    // Z on A's first slot: B had it, so B gets A's old key (a swap)
    CHECK(m.assign(WscButton::A, 0, z) == WscButton::B);
    CHECK(m.slots(WscButton::A, false)[0] == z);
    CHECK(m.slots(WscButton::B, false)[0] == x);
    // the same key on the other slot of the same button moves it (no duplicate, no "swapped" note)
    CHECK(m.assign(WscButton::A, 1, z) == WscButton::Count);
    CHECK(m.slots(WscButton::A, false)[0].empty());
    CHECK(m.slots(WscButton::A, false)[1] == z);
    // an input no one has (L2): no conflict; pads are a separate table
    const Binding lt{Binding::Kind::PadAxis, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1};
    CHECK(m.assign(WscButton::Start, 1, lt) == WscButton::Count);
    CHECK(m.slots(WscButton::Start, true)[1] == lt);
    m.clear(WscButton::Start, true, 0);
    CHECK(m.slots(WscButton::Start, true)[0].empty());
    m.reset(false);
    CHECK(m.keys == InputMap::defaults().keys);
    CHECK(m.pad != InputMap::defaults().pad);
    m.reset(true);
    CHECK(m == InputMap::defaults());
}

TEST_CASE("bindings persist in sawgod.toml, only changes are written") {
    using namespace sawgod::ui;
    sawgod::Config c;
    InputMap m = InputMap::defaults();
    m.store(c);
    CHECK(c.to_toml().find("[keyboard]") == std::string::npos); // all defaults: nothing written
    CHECK_FALSE(c.dirty());

    m.assign(WscButton::A, 0, {Binding::Kind::Key, SDL_SCANCODE_Z, 0}); // swaps with B
    m.assign(WscButton::Y2, 1, {Binding::Kind::Key, SDL_SCANCODE_KP_6, 0});
    m.assign(WscButton::Start, 1, {Binding::Kind::PadAxis, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, +1});
    m.clear(WscButton::Y1, true, 0);
    m.store(c);
    CHECK(c.dirty());
    const std::string toml = c.to_toml();
    CHECK(toml.find("[keyboard]\na = [\"Z\"]\nb = [\"X\"]\n") != std::string::npos);
    CHECK(toml.find("y2 = [\"D\", \"Keypad 6\"]") != std::string::npos);
    CHECK(toml.find("[gamepad]") != std::string::npos);
    CHECK(toml.find("start = [\"start\", \"rt\"]") != std::string::npos);
    CHECK(toml.find("y1 = []") != std::string::npos);
    CHECK(toml.find("x1 =") == std::string::npos); // unchanged buttons stay out

    sawgod::Config c2;
    std::vector<std::string> warnings;
    c2.from_toml(toml, &warnings);
    InputMap loaded = InputMap::defaults();
    loaded.load(c2, &warnings);
    CHECK(warnings.empty());
    CHECK(loaded == m);

    // back to the defaults: the tables disappear again
    loaded.reset(false);
    loaded.reset(true);
    loaded.store(c2);
    CHECK(c2.to_toml().find("[keyboard]") == std::string::npos);
    CHECK(c2.to_toml().find("[gamepad]") == std::string::npos);
}

TEST_CASE("bindings: bad names and hand-edited duplicates") {
    using namespace sawgod::ui;
    sawgod::Config c;
    c.from_toml("[keyboard]\na = [\"Z\", \"NoSuchKey\"]\nstart = \"Return\"\n"
                "[gamepad]\nb = [\"east\", \"south\", \"north\"]\n");
    std::vector<std::string> warnings;
    InputMap m = InputMap::defaults();
    m.load(c, &warnings);
    CHECK(warnings.size() == 4); // Bad key/list, third pad binding, and menu recovery for empty A
    CHECK(m.slots(WscButton::A, false)[0] == Binding{Binding::Kind::Key, SDL_SCANCODE_Z, 0});
    CHECK(m.slots(WscButton::B, false)[0].empty()); // its default Z went to A, which the file lists
    CHECK(m.slots(WscButton::Start, false) == InputMap::defaults().slots(WscButton::Start, false));
    CHECK(m.slots(WscButton::B, true)[0] == Binding{Binding::Kind::PadButton, SDL_GAMEPAD_BUTTON_EAST, 0});
    CHECK(m.slots(WscButton::A, true)[0].empty()); // the default east went to B
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
        const Binding pb{Binding::Kind::PadButton, b, 0};
        CHECK(binding_from_id(binding_id(pb), true) == pb);
    }
}

TEST_CASE("input map: Options' first slot is fixed, A is south, B east, Y2/Y4 use the right stick") {
    using sawgod::ui::Binding;
    using sawgod::ui::InputMap;
    using sawgod::ui::WscButton;
    using K = Binding::Kind;
    InputMap m = InputMap::defaults();
    CHECK(m.pad[int(WscButton::A)][0] == Binding{K::PadButton, SDL_GAMEPAD_BUTTON_SOUTH, 0});
    CHECK(m.pad[int(WscButton::B)][0] == Binding{K::PadButton, SDL_GAMEPAD_BUTTON_EAST, 0});
    CHECK(m.pad[int(WscButton::Y2)][0] == Binding{K::PadAxis, SDL_GAMEPAD_AXIS_RIGHTX, 1});
    CHECK(m.pad[int(WscButton::Y2)][1].empty());
    CHECK(m.pad[int(WscButton::Y4)][0] == Binding{K::PadAxis, SDL_GAMEPAD_AXIS_RIGHTX, -1});
    CHECK(m.pad[int(WscButton::Y4)][1].empty());
    const Binding esc{K::Key, SDL_SCANCODE_ESCAPE, 0}, f1{K::Key, SDL_SCANCODE_F1, 0},
        f2{K::Key, SDL_SCANCODE_F2, 0};
    CHECK(m.keys[int(WscButton::Options)][0] == esc);
    CHECK(m.keys[int(WscButton::Options)][1] == f1);
    // Options is rebindable like the rest...
    m.assign(WscButton::Options, 0, f2);
    CHECK(m.keys[int(WscButton::Options)][0] == f2);
    CHECK(m.assign(WscButton::A, 1, esc) == WscButton::Count); // Esc was free by then
    CHECK(m.keys[int(WscButton::A)][1] == esc);
    // ... swapped like any other; no stick or trigger
    CHECK(m.assign(WscButton::Options, 1, Binding{K::Key, SDL_SCANCODE_X, 0}) == WscButton::A);
    CHECK(m.keys[int(WscButton::A)][0] == f1);
    bool refused = false;
    m.assign(WscButton::Options, 1, Binding{K::PadAxis, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1}, &refused);
    CHECK(refused);
    CHECK(m.pad[int(WscButton::Options)][1].empty());
    // ... but a device never loses its last Options binding: not by clearing it,
    CHECK(m.clear(WscButton::Options, false, 0));
    CHECK_FALSE(m.clear(WscButton::Options, false, 1));
    CHECK(m.keys[int(WscButton::Options)][1] == Binding{K::Key, SDL_SCANCODE_X, 0});
    CHECK_FALSE(m.clear(WscButton::Options, true, 0));
    CHECK(m.opens_options(false));
    CHECK(m.opens_options(true));
    // not by another button taking it (Options' slot would get that button's empty slot back),
    const InputMap before = m;
    CHECK(m.assign(WscButton::FastForward, 1, Binding{K::Key, SDL_SCANCODE_X, 0}, &refused) ==
          WscButton::Count);
    CHECK(refused);
    CHECK(m == before);
    // (a swap that hands Options another input is fine)
    CHECK(m.assign(WscButton::B, 0, Binding{K::Key, SDL_SCANCODE_X, 0}, &refused) == WscButton::Options);
    CHECK_FALSE(refused);
    CHECK(m.keys[int(WscButton::Options)][1] == Binding{K::Key, SDL_SCANCODE_Z, 0});
    // and not by a hand-edited file: a set binding is kept, an empty table gets the default back
    sawgod::Config c;
    c.set_raw("keyboard.options", "[\"F3\"]");
    c.set_raw("gamepad.options", "[]");
    c.set_raw("gamepad.b", "[\"back\"]");
    InputMap l = InputMap::defaults();
    std::vector<std::string> warnings;
    l.load(c, &warnings);
    CHECK(l.keys[int(WscButton::Options)][0] == Binding{K::Key, SDL_SCANCODE_F3, 0});
    CHECK(l.pad[int(WscButton::Options)][0].empty());
    CHECK(l.pad[int(WscButton::B)][0] == Binding{K::PadButton, SDL_GAMEPAD_BUTTON_BACK, 0});
    CHECK(l.menu_slots(WscButton::Options)[0] != l.menu_slots(WscButton::B)[0]);
    CHECK_FALSE(warnings.empty());
}

TEST_CASE("Esper replacement confirmation defaults on and persists independently of meat preview") {
    sawgod::Config c;
    sawgod::Settings s;
    s.register_all(c);
    CHECK(s.confirm_esper_changes->on());
    c.set(*s.confirm_esper_changes, 0);
    CHECK_FALSE(s.confirm_esper_changes->on());
    c.set(*s.confirm_esper_changes, 1);
    CHECK(s.meat_preview->on());
    const auto toml = c.to_toml();
    CHECK(toml.find("confirm_esper_changes = true") != std::string::npos);
    // Its label and help are keyed Options text, as every other option's.
    const auto keyed = [](std::string_view english) {
        return std::any_of(std::begin(sawgod::ui::kOptionsText), std::end(sawgod::ui::kOptionsText),
                           [&](const auto &e) { return english == e.english; });
    };
    CHECK(keyed(s.confirm_esper_changes->label));
    CHECK(keyed(s.confirm_esper_changes->help));
}

TEST_CASE("Options GB font filtering leaves the gap under each Latin row blank") {
    SDL_Surface *surface = SDL_CreateSurface(300, 70, SDL_PIXELFORMAT_XRGB8888);
    REQUIRE(surface);
    SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
    REQUIRE(renderer);
    {
        sawgod::ui::Skin skin;
        std::vector<uint8_t> rom(0x400000);
        rom[0x390004] = 0xFF;
        rom[0x390005] = 0x0F; // font ink white; the shadow black
        REQUIRE(skin.build(renderer, rom));
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderClear(renderer);
        skin.text_sdl(renderer, 4.3f, 4.3f, "Video Audio", 3.5f, 1, true);
        REQUIRE(SDL_FlushRenderer(renderer));
        // Capitals/non-descending lowercase finish in row 6; the entire following gap is paper.
        for (int y = 30; y < 39; ++y)
            for (int x = 3; x < 220; ++x) {
                Uint8 r, g, b;
                REQUIRE(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, nullptr));
                CHECK((r == 255 && g == 255 && b == 255));
            }
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
}

TEST_CASE("Options fills retain transparency in GB and use a continuous Enhanced gradient") {
    SDL_Surface *surface = SDL_CreateSurface(64, 64, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(surface);
    SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
    REQUIRE(renderer);
    auto *context = ImGui::CreateContext();
    {
        sawgod::ui::Skin skin;
        std::vector<uint8_t> rom(0x400000);
        // All frame tiles contain fill, with three distinct palette shades.
        std::fill(rom.begin() + 0x390080, rom.begin() + 0x390080 + 256 * 32, 0x99);
        for (int i = 9; i <= 11; ++i) {
            rom[0x390000 + i * 2] = uint8_t(i * 17);
            rom[0x390001 + i * 2] = uint8_t(i);
        }
        REQUIRE(skin.build(renderer, rom));
        for (bool gb : {false, true}) {
            skin.set_gb(gb);
            skin.set_enhanced(!gb);
            for (bool sheer : {false, true}) {
                ImDrawList list(ImGui::GetDrawListSharedData());
                list._ResetForNewFrame();
                skin.frame(&list, 0, 0, 32, 32, sawgod::ui::Skin::Frame::White, sheer);
                REQUIRE(list.VtxBuffer.Size >= 4);
                const auto color = list.VtxBuffer.back().col;
                CHECK((color >> 24) == (sheer ? 230 : 255));
                if (!gb) {
                    // The first fill quad follows the 16 atlas frame quads.
                    const auto top = list.VtxBuffer[64].col;
                    CHECK((top & 255) > (color & 255));
                    CHECK((top >> 24) == (color >> 24));
                }
            }
        }
    }
    ImGui::DestroyContext(context);
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
}

TEST_CASE("gamepad swap resolves live without rewriting stored custom slots") {
    using namespace sawgod;
    using namespace sawgod::ui;
    Config config;
    Settings settings;
    settings.register_all(config);
    config.from_toml("[gamepad]\na = [\"\", \"west\"]\nb = [\"north\", \"rt\"]\n");
    auto map = InputMap::defaults();
    map.load(config);
    const auto original = map.pad;
    CHECK(map.effective_slots(WscButton::A, true) == original[int(WscButton::A)]);
    config.set(*settings.swap_ab, 1);
    CHECK(map.effective_slots(WscButton::A, true) == original[int(WscButton::B)]);
    CHECK(map.effective_slots(WscButton::B, true) == original[int(WscButton::A)]);
    CHECK(map.effective_slots(WscButton::A, false) == map.keys[int(WscButton::A)]);
    CHECK(bound_button_text(map, WscButton::A, true, 2, SDL_GAMEPAD_TYPE_PS5) == "{icon:p:PS triangle}");
    map.store(config);
    Config loaded;
    Settings other;
    other.register_all(loaded);
    loaded.from_toml(config.to_toml());
    auto restored = InputMap::defaults();
    restored.load(loaded);
    CHECK(other.swap_ab->on());
    CHECK(restored.pad == original);
    CHECK(restored.effective_slots(WscButton::A, true) == original[int(WscButton::B)]);
    config.set(*settings.swap_ab, 0);
    CHECK(map.effective_slots(WscButton::A, true) == original[int(WscButton::A)]);
    CHECK(map.pad == original);
}

TEST_CASE("gamepad menu edits protect Accept Cancel and Options as a transaction") {
    using namespace sawgod::ui;
    auto map = InputMap::defaults();
    const auto original = map;
    for (auto b : {WscButton::A, WscButton::B, WscButton::Options}) {
        CHECK_FALSE(map.clear(b, true, 0));
        CHECK(map == original);
        bool refused = false;
        // Taking a required control from an empty slot cannot give its owner a usable button back.
        map.assign(WscButton::FastForward, 1, map.slots(b, true)[0], &refused);
        CHECK(refused);
        CHECK(map == original);
        map.assign(b, 0, {Binding::Kind::PadAxis, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1}, &refused);
        CHECK(refused);
        CHECK(map == original);
        map.assign(b, 0, {Binding::Kind::PadButton, SDL_GAMEPAD_BUTTON_DPAD_UP}, &refused);
        CHECK(refused);
        CHECK(map == original);
    }
    CHECK(map.assign(WscButton::A, 0, map.slots(WscButton::B, true)[0]) == WscButton::B);
    CHECK(map.menu_slots(WscButton::A)[0] == original.pad[int(WscButton::B)][0]);
    CHECK(map.menu_slots(WscButton::B)[0] == original.pad[int(WscButton::A)][0]);
    // Multiple buttons can move within an action without losing its last usable control.
    CHECK(map.assign(WscButton::A, 1, map.slots(WscButton::A, true)[0]) == WscButton::Count);
    CHECK(map.slots(WscButton::A, true)[0].empty());
    CHECK(map.clear(WscButton::A, true, 0));
    CHECK_FALSE(map.clear(WscButton::A, true, 1));
}

TEST_CASE("legacy unusable gamepad menu slots recover without changing gameplay or persistence") {
    using namespace sawgod;
    using namespace sawgod::ui;
    Config config;
    Settings settings;
    settings.register_all(config);
    config.from_toml("[gamepad]\na = []\nb = [\"\", \"lt\"]\noptions = [\"dpad_up\", \"rt\"]\n");
    auto map = InputMap::defaults();
    std::vector<std::string> warnings;
    map.load(config, &warnings);
    CHECK(warnings.size() == 3);
    const auto original = map.pad;
    const auto a = map.menu_slots(WscButton::A), b = map.menu_slots(WscButton::B),
               opt = map.menu_slots(WscButton::Options);
    CHECK(a[0] == Binding{Binding::Kind::PadButton, SDL_GAMEPAD_BUTTON_SOUTH});
    CHECK(b[0] == Binding{Binding::Kind::PadButton, SDL_GAMEPAD_BUTTON_EAST});
    CHECK(opt[0] == Binding{Binding::Kind::PadButton, SDL_GAMEPAD_BUTTON_BACK});
    CHECK(map.opens_options(true));
    CHECK(map.effective_slots(WscButton::A, true)[0].empty());
    CHECK(map.effective_slots(WscButton::B, true)[1].kind == Binding::Kind::PadAxis);
    config.set(*settings.swap_ab, 1);
    CHECK(map.menu_slots(WscButton::A) == a);
    CHECK(map.menu_slots(WscButton::B) == b);
    map.store(config);
    Config loaded;
    loaded.from_toml(config.to_toml());
    auto restored = InputMap::defaults();
    restored.load(loaded);
    CHECK(restored.pad == original);
}

TEST_CASE("Options gamepad actions follow effective buttons and consume overlapping shortcuts once") {
    using namespace sawgod;
    using namespace sawgod::ui;
    Config config;
    Settings settings;
    settings.register_all(config);
    auto map = InputMap::defaults();
    map.load(config);
    Input input;
    input.set_map(&map);
    input.set_menu_open(true);
    const auto press = [&](SDL_GamepadButton b) {
        SDL_Event e{};
        e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        e.gbutton.button = b;
        input.handle_event(e);
        auto actions = input.take_menu_actions(0);
        e.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
        input.handle_event(e);
        return actions;
    };
    CHECK(press(SDL_GAMEPAD_BUTTON_SOUTH).accept);
    CHECK(press(SDL_GAMEPAD_BUTTON_EAST).back);
    config.set(*settings.swap_ab, 1);
    CHECK(press(SDL_GAMEPAD_BUTTON_EAST).accept);
    CHECK(press(SDL_GAMEPAD_BUTTON_SOUTH).back);
    config.set(*settings.swap_ab, 0);
    CHECK(press(SDL_GAMEPAD_BUTTON_SOUTH).accept);
    for (auto button :
         {SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER}) {
        map.assign(WscButton::A, 0, {Binding::Kind::PadButton, button});
        const auto actions = press(button);
        CHECK(actions.accept);
        CHECK_FALSE(
            (actions.back || actions.clear || actions.prev_page || actions.next_page || actions.toggle));
        CHECK_FALSE(input.take_menu_actions(0).accept);
    }
    input.start_capture(true);
    CHECK_FALSE(press(SDL_GAMEPAD_BUTTON_SOUTH).accept);
    bool cancelled = false;
    CHECK(input.take_captured(&cancelled) == Binding{Binding::Kind::PadButton, SDL_GAMEPAD_BUTTON_SOUTH});
    CHECK_FALSE(cancelled);
    // Keyboard remains fixed even when gamepad A/B is swapped or rebound.
    config.set(*settings.swap_ab, 1);
    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_X;
    input.handle_event(key);
    CHECK(input.take_menu_actions(0).accept);
}

TEST_CASE("Lo-Spec parsing preserves saved pacing; Smooth is the default everywhere") {
    using namespace sawgod;
    Config config;
    Settings settings;
    settings.register_all(config);
    CHECK(settings.pacing->choice_labels == std::vector<std::string>{"Exact", "Smooth", "Lo-Spec"});
    CHECK(settings.pacing->choice_order == std::vector<int>{1, 0, 2});
    for (auto [name, value] :
         {std::pair{"lospec", Pacing::LoSpec}, {"exact", Pacing::Exact}, {"smooth", Pacing::Smooth}}) {
        std::vector<std::string> warnings;
        config.from_toml(std::string("[video]\npacing=\"") + name + "\"\n", &warnings);
        CHECK(warnings.empty());
        CHECK(settings.pacing_mode() == value);
        Config saved;
        Settings restored;
        restored.register_all(saved);
        saved.from_toml(config.to_toml());
        CHECK(restored.pacing_mode() == value);
    }
}

TEST_CASE("Lo-Spec preserves menu key events that begin and end between presentations") {
    using namespace sawgod;
    ui::Input input;
    auto map = ui::InputMap::defaults();
    input.set_map(&map);
    app::LoSpecCadence cadence;
    int presented = 0, accepted = 0;
    bool carry = false;
    for (uint64_t now = 0; now < 35'000'000; now += 1'000'000) {
        if (now == 10'000'000 || now == 20'000'000) {
            SDL_Event event{};
            event.type = now == 10'000'000 ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            event.key.scancode = SDL_SCANCODE_X;
            input.handle_event(event);
        }
        carry = input.take_menu_actions(double(now) / 1e9).accept || carry;
        if (cadence.due(now)) {
            ++presented;
            accepted += carry;
            carry = false;
        }
    }
    CHECK(presented == 2);
    CHECK(accepted == 1);
}
