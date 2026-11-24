#include "app/battle_wipe.h"
#include "app/picture_draw.h"
#include "engine/fidelity.h"
#include "engine/input.h"
#include "engine/overlay.h"
#include "engine/touch_controls.h"
#include <algorithm>
#include <cmath>
#include <doctest/doctest.h>
using namespace sawgod;
using namespace sawgod::ui;
namespace {
TouchLayout reference() { return TouchLayout::make(2400, 1080, 1680, {0, 0, 2400, 1080}); }
void check_disjoint(const TouchLayout &layout) {
    // Distance between capsule center segments, including the 20% hit margin.
    for (size_t i = 0; i < layout.areas.size(); ++i)
        for (size_t j = i + 1; j < layout.areas.size(); ++j) {
            const auto a = layout.areas[i], b = layout.areas[j];
            const float dx = std::max(0.f, std::abs(a.x - b.x) - (a.w - a.h + b.w - b.h) / 2);
            INFO("controls ", i, " and ", j);
            const float ar = a.hit_radius > 0 ? a.hit_radius : a.h * .6f;
            const float br = b.hit_radius > 0 ? b.hit_radius : b.h * .6f;
            CHECK(std::hypot(dx, a.y - b.y) >= ar + br - .001f);
        }
}
} // namespace
TEST_CASE("touch: cardinal sectors, center and diagonal dead zones") {
    CHECK(TouchState::direction(0, 0, 160) == -1);
    CHECK(TouchState::direction(28, 0, 160) == -1);
    CHECK(TouchState::direction(30, 0, 160) == 1);
    for (float radius : {40.f, 160.f, 220.f}) {
        CHECK(TouchState::direction(0, -radius, 160) == 0);
        CHECK(TouchState::direction(radius, 0, 160) == 1);
        CHECK(TouchState::direction(0, radius, 160) == 2);
        CHECK(TouchState::direction(-radius, 0, 160) == 3);
        for (float x : {-radius, radius})
            for (float y : {-radius, radius})
                CHECK(TouchState::direction(x, y, 160) == -1);
        CHECK(TouchState::direction(radius, radius * .69f, 160) == 1);
        CHECK(TouchState::direction(radius, radius * .71f, 160) == -1);
    }
}
TEST_CASE("touch: fingers own pads while sliding and independent buttons share releases") {
    TouchState t;
    t.layout = reference();
    const auto x = t.layout.areas[size_t(TouchControl::X)];
    const auto a = t.layout.areas[size_t(TouchControl::A)];
    t.down(101, x.x, x.y - 100);
    t.down(202, a.x, a.y);
    t.down(303, a.x, a.y);
    CHECK(t.held(TouchControl::X, 0));
    CHECK(t.held(TouchControl::A));
    t.move(101, x.x + 200, x.y); // remains owned outside the initial hit radius
    CHECK_FALSE(t.held(TouchControl::X, 0));
    CHECK(t.held(TouchControl::X, 1));
    t.move(101, x.x + 100, x.y + 100);
    for (int d = 0; d < 4; ++d)
        CHECK_FALSE(t.held(TouchControl::X, d));
    t.move(101, x.x, x.y);
    for (int d = 0; d < 4; ++d)
        CHECK_FALSE(t.held(TouchControl::X, d));
    t.up(202);
    CHECK(t.held(TouchControl::A));
    t.up(303);
    CHECK_FALSE(t.held(TouchControl::A));
    t.move(101, a.x, a.y); // cannot turn a pad finger into an A press
    CHECK_FALSE(t.held(TouchControl::A));
    t.release();
    for (int d = 0; d < 4; ++d)
        CHECK_FALSE(t.held(TouchControl::X, d));
    t.move(101, x.x, x.y - 100); // released fingers cannot reappear on motion
    CHECK_FALSE(t.held(TouchControl::X, 0));
}
TEST_CASE("touch: hit margins, host edges and fixed reference coordinates") {
    TouchState t;
    t.layout = reference();
    const auto picture = app::GameRect(2400, 1080);
    CHECK(picture.w == 1680);
    CHECK(picture.h == 1080);
    CHECK(picture.x == 360);
    CHECK(picture.y == 0);
    const auto menu = app::MenuRect(2400, 1080);
    CHECK(menu.scale == 3.75f);
    CHECK(menu.x == picture.x + 16 * picture.scale);
    const auto &areas = t.layout.areas;
    const auto a = areas[size_t(TouchControl::A)];
    CHECK(a.x == doctest::Approx(2296.8f));
    CHECK(a.y == 700);
    CHECK(areas[size_t(TouchControl::B)].y == 870);
    CHECK(areas[size_t(TouchControl::X)].x == 192);
    CHECK(areas[size_t(TouchControl::Y)].y == 415);
    CHECK(a.contains(a.x + 100, a.y)); // art radius 86, hit radius 103.2
    CHECK_FALSE(a.contains(a.x + 104, a.y));
    const auto start = areas[size_t(TouchControl::Start)];
    CHECK(start.contains(start.x + 130, start.y));
    CHECK_FALSE(start.contains(start.x + 133, start.y));
    check_disjoint(t.layout);
    const auto options = areas[size_t(TouchControl::Options)];
    t.down(1, options.x, options.y);
    CHECK(t.take_options());
    CHECK_FALSE(t.take_options());
    t.move(1, options.x, options.y);
    CHECK_FALSE(t.take_options());
    const auto mode = areas[size_t(TouchControl::Mode)];
    t.down(2, mode.x, mode.y);
    CHECK(t.take_mode());
    t.release();
    CHECK_FALSE(t.take_mode());
    t.down(3, a.x + 104, a.y);
    t.move(3, a.x, a.y); // a touch that starts outside never captures a control
    CHECK_FALSE(t.held(TouchControl::A));
}
TEST_CASE("touch: all sizes follow height with physical floors and safe disjoint targets") {
    struct Case {
        float w, h, density, left, bottom;
    };
    for (const auto c : {Case{1112, 720, 1, 0, 0}, Case{1920, 1080, 2.25f, 0, 0},
                         Case{2400, 1080, 2.625f, 128, 0}, Case{2400, 1080, 2.625f, 128, 63},
                         Case{2400, 1080, 2.625f, 0, 0}, Case{1920, 1080, 2.25f, 40, 40}}) {
        const float s = c.h / 1080;
        const auto picture = app::GameRect(int(c.w), int(c.h));
        const auto l = TouchLayout::make(c.w, c.h, picture.w, {c.left, 0, c.w - c.left, c.h - c.bottom},
                                         57 * c.density / 120);
        check_disjoint(l);
        for (size_t i = 0; i < l.areas.size(); ++i) {
            const auto a = l.areas[i];
            const bool top = i <= size_t(TouchControl::Start);
            const bool pad = i == size_t(TouchControl::X) || i == size_t(TouchControl::Y);
            const float expected = std::max((top ? 120 : pad ? 320 : 172) * s, (top ? 48 : 57) * c.density);
            CHECK(a.h == doctest::Approx(expected));
            CHECK(a.w == doctest::Approx(i == size_t(TouchControl::Start) ? 2 * expected : expected));
            const float r = a.hit_radius > 0 ? a.hit_radius : a.h * .6f;
            const float extent = (a.w - a.h) / 2 + r;
            CHECK(a.x - extent >= doctest::Approx(c.left));
            CHECK(a.x + extent <= doctest::Approx(c.w));
            CHECK(a.y - r >= doctest::Approx(0));
            CHECK(a.y + r <= doctest::Approx(c.h - c.bottom));
        }
        CHECK(l.areas[size_t(TouchControl::Y)].y == doctest::Approx(415 * s));
        CHECK(l.areas[size_t(TouchControl::X)].y == doctest::Approx(800 * s));
        CHECK(l.areas[size_t(TouchControl::A)].y == doctest::Approx(700 * s));
        CHECK(l.areas[size_t(TouchControl::B)].y == doctest::Approx(870 * s));
        if (c.left > 0 || c.w / c.h < 2)
            CHECK(l.areas[size_t(TouchControl::X)].x + 192 * s > picture.x);
    }
    CHECK(TouchLayout::make(960, 540, 840, {0, 0, 960, 540}).areas[size_t(TouchControl::X)].w == 160);
    CHECK(TouchLayout::make(1920, 1080, 1680, {0, 0, 1920, 1080}, 3).areas[size_t(TouchControl::X)].w == 360);
}

TEST_CASE("touch: saved Auto On Off honors controller hotplug and excludes its own joystick") {
    REQUIRE(SDL_InitSubSystem(SDL_INIT_GAMEPAD));
    {
        TouchControls touch;
        REQUIRE(touch.init());
        Config config;
        Settings settings;
        settings.register_all(config, true);
        CHECK(settings.touch_controls->get() == int(TouchMode::Auto));
        for (auto mode : {TouchMode::Auto, TouchMode::On, TouchMode::Off}) {
            config.set(*settings.touch_controls, int(mode));
            Input input(touch.id()); // launch without a real controller
            CHECK_FALSE(input.real_controller_connected());
            CHECK(settings.touch_enabled(input.real_controller_connected()) == (mode != TouchMode::Off));
            SDL_VirtualJoystickDesc desc{};
            SDL_INIT_INTERFACE(&desc);
            desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
            desc.name = "External test pad";
            desc.vendor_id = 0x054c;
            desc.product_id = 0x0ce6; // DualSense: distinct from the touch joystick's Auto fallback
            desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
            const auto id = SDL_AttachVirtualJoystick(&desc);
            REQUIRE(id);
            SDL_Event e{};
            while (SDL_PollEvent(&e))
                input.handle_event(e);
            CHECK(input.real_controller_connected());
            CHECK(settings.touch_enabled(true) == (mode == TouchMode::On));
            CHECK(settings.touch_controls->get() == int(mode));
            CHECK(input.gamepad_name() == "External test pad");
            CHECK(input.gamepad_type() == SDL_GAMEPAD_TYPE_PS5);
            {
                Input launched(touch.id()); // launch with a real controller already attached
                CHECK(launched.real_controller_connected());
                CHECK(settings.touch_enabled(launched.real_controller_connected()) ==
                      (mode == TouchMode::On));
            }
            // Touch presses may select pad prompts, but never the touch joystick's family/name.
            e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            e.gbutton.which = id;
            e.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
            input.handle_event(e);
            e.gbutton.which = touch.id();
            input.handle_event(e);
            CHECK(input.last_used_pad());
            CHECK(input.gamepad_type() == SDL_GAMEPAD_TYPE_PS5);
            CHECK(input.gamepad_name() == "External test pad");
            e.type = SDL_EVENT_KEY_DOWN;
            e.key.scancode = SDL_SCANCODE_X;
            input.handle_event(e);
            CHECK_FALSE(input.last_used_pad());
            REQUIRE(SDL_DetachVirtualJoystick(id));
            while (SDL_PollEvent(&e))
                input.handle_event(e);
            CHECK_FALSE(input.real_controller_connected());
            CHECK(settings.touch_enabled(false) == (mode != TouchMode::Off));
            CHECK(input.gamepad_type() == SDL_GAMEPAD_TYPE_UNKNOWN);
            Config loaded;
            Settings restored;
            restored.register_all(loaded, true);
            loaded.from_toml(config.to_toml());
            CHECK(restored.touch_controls->get() == int(mode));
            CHECK(restored.touch_enabled(false) == (mode != TouchMode::Off));
        }
        Settings desktop;
        Config desktop_config;
        desktop.register_all(desktop_config);
        CHECK(desktop.touch_controls->hidden == !Input::kAndroid);
        desktop_config.set(*desktop.touch_controls, int(TouchMode::On));
        CHECK(desktop.touch_enabled(false) == Input::kAndroid);
    }
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}
TEST_CASE("touch: SDL virtual gamepad follows default bindings, Swap A/B, and hide releases") {
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    REQUIRE(SDL_InitSubSystem(SDL_INIT_GAMEPAD));
    {
        TouchControls touch;
        REQUIRE(touch.init());
        REQUIRE(SDL_IsGamepad(touch.id()));
        Input input(touch.id());
        CHECK_FALSE(input.real_controller_connected());
        SDL_Event e{};
        while (SDL_PollEvent(&e))
            input.handle_event(e);
        CHECK_FALSE(input.real_controller_connected());
        const auto map = InputMap::defaults();
        // Finger input is normalized and does not require a window.
        touch.show(true);
        // A real window supplies safe bounds and the reference layout.
        REQUIRE(SDL_InitSubSystem(SDL_INIT_VIDEO));
        SDL_Window *window = SDL_CreateWindow("touch test", 2400, 1080, SDL_WINDOW_HIDDEN);
        REQUIRE(window);
        touch.layout(window, nullptr, 2400, 1080, 1568);
        auto finger = [&](Uint32 type, SDL_FingerID id, float x, float y) {
            e = {};
            e.type = type;
            e.tfinger.fingerID = id;
            e.tfinger.x = x / 2400;
            e.tfinger.y = y / 1080;
            touch.event(e, window, false);
        };
        finger(SDL_EVENT_FINGER_DOWN, 1, 208, 700);
        CHECK(input.button_held(map, WscButton::X1));
        finger(SDL_EVENT_FINGER_DOWN, 2, 2284, 700);
        CHECK(input.button_held(map, WscButton::A));
        finger(SDL_EVENT_FINGER_DOWN, 3, 208, 315);
        CHECK(input.button_held(map, WscButton::Y1));
        finger(SDL_EVENT_FINGER_MOTION, 3, 308, 415);
        CHECK_FALSE(input.button_held(map, WscButton::Y1));
        CHECK(input.button_held(map, WscButton::Y2));
        Config config;
        Settings settings;
        settings.register_all(config);
        InputMap swapped = map;
        swapped.load(config);
        config.set(*settings.swap_ab, 1);
        CHECK_FALSE(input.button_held(swapped, WscButton::A));
        CHECK(input.button_held(swapped, WscButton::B));
        touch.show(false);
        CHECK(input.held(map) == 0);
        finger(SDL_EVENT_FINGER_DOWN, 4, 2284, 700);
        CHECK(input.held(map) == 0);
        config.set(*settings.touch_controls, int(TouchMode::On));
        settings.touch_controls->hidden = false;
        touch.show(settings.touch_enabled(input.real_controller_connected()));
        finger(SDL_EVENT_FINGER_DOWN, 5, 2284, 700);
        REQUIRE(input.button_held(map, WscButton::A));
        {
            // An external gamepad and the touch joystick drive different actions simultaneously.
            SDL_VirtualJoystickDesc desc{};
            SDL_INIT_INTERFACE(&desc);
            desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
            desc.name = "Simultaneous external pad";
            desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
            const auto id = SDL_AttachVirtualJoystick(&desc);
            REQUIRE(id);
            auto *pad = SDL_OpenJoystick(id);
            REQUIRE(pad);
            while (SDL_PollEvent(&e))
                input.handle_event(e);
            REQUIRE(SDL_SetJoystickVirtualButton(pad, SDL_GAMEPAD_BUTTON_EAST, true));
            SDL_UpdateJoysticks();
            CHECK(input.real_controller_connected());
            CHECK(settings.touch_enabled(true));
            CHECK(input.button_held(map, WscButton::A));
            CHECK(input.button_held(map, WscButton::B));
            config.set(*settings.touch_controls, int(TouchMode::Off));
            touch.show(settings.touch_enabled(true));
            for (int i = 0; i < int(TouchControl::Count); ++i)
                CHECK_FALSE(touch.shown(TouchControl(i)));
            CHECK_FALSE(input.button_held(map, WscButton::A));
            CHECK(input.button_held(map, WscButton::B)); // real input survives hiding touch
            SDL_CloseJoystick(pad);
            REQUIRE(SDL_DetachVirtualJoystick(id));
            while (SDL_PollEvent(&e))
                input.handle_event(e);
        }
        config.set(*settings.touch_controls, int(TouchMode::On));
        touch.show(settings.touch_enabled(false));
        CHECK(input.held(map) == 0); // held fingers do not return after hiding
        SDL_DestroyWindow(window);
    }
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_VIDEO);
}
TEST_CASE("touch: Options exposes gamepad navigation and ignores touch bindings") {
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    REQUIRE(SDL_InitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_VIDEO));
    SDL_Window *window = SDL_CreateWindow("touch menu test", 2400, 1080, SDL_WINDOW_HIDDEN);
    REQUIRE(window);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, "software");
    REQUIRE(renderer);
    {
        TouchControls touch;
        REQUIRE(touch.init());
        Input input(touch.id());
        auto map = InputMap::defaults();
        Config config;
        Settings settings;
        settings.register_all(config);
        Overlay menu;
        Overlay::Hooks hooks;
        hooks.input = &input;
        hooks.map = &map;
        REQUIRE(menu.init(renderer, config, settings, hooks));
        touch.layout(window, renderer, 2400, 1080, 1568);
        const auto layout = reference();
        const auto finger = [&](Uint32 type, TouchControl c) {
            const auto a = layout.areas[size_t(c)];
            SDL_Event e{};
            e.type = type;
            e.tfinger.fingerID = 1 + int(c);
            e.tfinger.x = a.x / 2400;
            e.tfinger.y = (a.y - (c == TouchControl::X || c == TouchControl::Y ? 100 : 0)) / 1080;
            touch.event(e, window, false);
        };
        // The front end dispatches Options before handing the remaining menu actions to the overlay.
        const auto dispatch = [&] {
            SDL_Event e;
            while (SDL_PollEvent(&e))
                input.handle_event(e);
            if (touch.take_options())
                input.press_options();
            const auto actions = input.take_menu_actions(0);
            if (actions.toggle)
                menu.set_open(!menu.is_open());
            menu.frame(actions, 1.f / 75);
            touch.show(true, menu.is_open());
        };
        touch.show(true);
        for (auto c :
             {TouchControl::A, TouchControl::B, TouchControl::Start, TouchControl::X, TouchControl::Y})
            finger(SDL_EVENT_FINGER_DOWN, c);
        REQUIRE(input.held(map) != 0);
        finger(SDL_EVENT_FINGER_DOWN, TouchControl::Options);
        dispatch();
        REQUIRE(menu.is_open());
        CHECK(input.held(map) == 0);
        CHECK_FALSE(touch.take_mode());
        // Check actual rendered alpha as well as the shared visibility/hit-test policy.
        SDL_Texture *target =
            SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 2400, 1080);
        REQUIRE(target);
        REQUIRE(SDL_SetRenderTarget(renderer, target));
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);
        touch.render(renderer);
        SDL_Surface *pixels = SDL_RenderReadPixels(renderer, nullptr);
        REQUIRE(pixels);
        for (size_t i = 0; i < size_t(TouchControl::Count); ++i) {
            const auto c = TouchControl(i);
            CHECK(touch.shown(c) == touch_menu_control(c));
            const auto a = layout.areas[i];
            bool ink = false;
            for (int y = int(a.y - a.h / 2); y < a.y + a.h / 2; y += 4)
                for (int x = int(a.x - a.w / 2); x < a.x + a.w / 2; x += 4) {
                    float r = 0, g = 0, b = 0, alpha = 0;
                    REQUIRE(SDL_ReadSurfacePixelFloat(pixels, x, y, &r, &g, &b, &alpha));
                    ink |= alpha > 0;
                }
            CHECK(ink == touch_menu_control(c));
            // Finger-generated mouse taps cannot also activate a menu row underneath the overlay.
            SDL_Event pointer{};
            pointer.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            pointer.button.which = SDL_TOUCH_MOUSEID;
            pointer.button.button = SDL_BUTTON_LEFT;
            pointer.button.x = a.x;
            pointer.button.y = a.y;
            CHECK(touch.event(pointer, window, false) == touch_menu_control(c));
            if (!touch_menu_control(c)) {
                finger(SDL_EVENT_FINGER_UP, c);
                finger(SDL_EVENT_FINGER_DOWN, c);
            }
        }
        SDL_DestroySurface(pixels);
        SDL_SetRenderTarget(renderer, nullptr);
        SDL_DestroyTexture(target);
        CHECK(input.held(map) == 0);
        CHECK_FALSE(touch.take_mode());
        // The held fingers released on opening cannot return through motion or after closing.
        finger(SDL_EVENT_FINGER_MOTION, TouchControl::X);
        CHECK(input.held(map) == 0);
        finger(SDL_EVENT_FINGER_UP, TouchControl::Options);
        finger(SDL_EVENT_FINGER_DOWN, TouchControl::Options);
        dispatch();
        CHECK_FALSE(menu.is_open());
        CHECK(input.held(map) == 0);
        menu.set_open(true);
        touch.show(true, true);
        // Real SDL virtual events go through Input and the actual Options frame path.
        REQUIRE(menu.show("Gamepad", 0));
        const auto before = menu.view().row;
        const auto x = layout.areas[size_t(TouchControl::X)];
        SDL_Event down{};
        down.type = SDL_EVENT_FINGER_DOWN;
        down.tfinger.fingerID = 1 + int(TouchControl::X);
        down.tfinger.x = x.x / 2400;
        down.tfinger.y = (x.y + 100) / 1080;
        touch.event(down, window, false);
        dispatch();
        CHECK(menu.view().row == before + 1);
        finger(SDL_EVENT_FINGER_UP, TouchControl::X);
        dispatch();
        const int icons = settings.pad_icons->get();
        finger(SDL_EVENT_FINGER_DOWN, TouchControl::A);
        dispatch();
        CHECK(settings.pad_icons->get() != icons);
        finger(SDL_EVENT_FINGER_UP, TouchControl::A);
        finger(SDL_EVENT_FINGER_DOWN, TouchControl::B);
        dispatch();
        CHECK_FALSE(menu.view().in_rows);
        finger(SDL_EVENT_FINGER_UP, TouchControl::B);
        for (const std::string page : {"Keyboard", "Gamepad"}) {
            const auto model = menu.model();
            int bind_row = -1;
            for (const auto &p : model)
                if (p.title == page)
                    for (size_t i = 0; i < p.rows.size(); ++i)
                        if (p.rows[i].kind == Overlay::RowInfo::Kind::Bind) {
                            bind_row = int(i);
                            break;
                        }
            REQUIRE(bind_row >= 0);
            REQUIRE(menu.show(page, bind_row));
            MenuActions accept;
            accept.accept = true;
            menu.frame(accept, 1.f / 75);
            REQUIRE(menu.view().capturing);
            const auto original = map;
            for (auto c : {TouchControl::X, TouchControl::A, TouchControl::B}) {
                finger(SDL_EVENT_FINGER_DOWN, c);
                dispatch();
                CHECK(input.capturing());
                CHECK(menu.view().capturing);
                CHECK(map.keys == original.keys);
                CHECK(map.pad == original.pad);
                finger(SDL_EVENT_FINGER_UP, c);
                dispatch();
            }
            // Axis events from the same virtual device are ignored too, even if queued earlier.
            SDL_Event axis{};
            axis.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
            axis.gaxis.which = touch.id();
            axis.gaxis.axis = SDL_GAMEPAD_AXIS_RIGHTX;
            axis.gaxis.value = 0;
            input.handle_event(axis);
            axis.gaxis.value = 32767;
            input.handle_event(axis);
            CHECK(input.capturing());
            finger(SDL_EVENT_FINGER_DOWN, TouchControl::Options);
            dispatch();
            CHECK(menu.is_open());
            CHECK_FALSE(input.capturing());
            CHECK_FALSE(menu.view().capturing);
            finger(SDL_EVENT_FINGER_UP, TouchControl::Options);
        }
        touch.show(false, true);
        CHECK_FALSE(touch.shown(TouchControl::Options));
        finger(SDL_EVENT_FINGER_DOWN, TouchControl::Options);
        CHECK_FALSE(touch.take_options());
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_VIDEO);
}
TEST_CASE("input: Android Back follows Options with Touch Controls Off and cancels capture") {
    Input input;
    auto map = InputMap::defaults();
    // Both visible keyboard slots remain rebindable; system Back survives these edits.
    map.assign(WscButton::Options, 0, {Binding::Kind::Key, SDL_SCANCODE_F10});
    map.assign(WscButton::Options, 1, {Binding::Kind::Key, SDL_SCANCODE_F11});
    input.set_map(&map);
    Config config;
    Settings settings;
    settings.register_all(config, true);
    for (auto mode : {TouchMode::Auto, TouchMode::On, TouchMode::Off}) {
        config.set(*settings.touch_controls, int(mode));
        for (bool open : {false, true}) {
            input.set_menu_open(open);
            SDL_Event e{};
            e.type = SDL_EVENT_KEY_DOWN;
            e.key.scancode = SDL_SCANCODE_AC_BACK;
            input.handle_event(e);
            CHECK(input.take_menu_actions(0).toggle);
            CHECK_FALSE(input.take_menu_actions(0).toggle);
            e.key.repeat = true;
            input.handle_event(e);
            CHECK_FALSE(input.take_menu_actions(0).toggle);
            e.key.repeat = false;
            e.type = SDL_EVENT_KEY_UP;
            input.handle_event(e);
            CHECK_FALSE(input.take_menu_actions(0).toggle);
            e.type = SDL_EVENT_KEY_DOWN;
            for (bool gamepad : {false, true}) {
                input.start_capture(gamepad);
                input.handle_event(e);
                CHECK_FALSE(input.capturing());
                CHECK_FALSE(input.take_menu_actions(0).toggle);
                bool cancelled = false;
                CHECK_FALSE(input.take_captured(&cancelled));
                CHECK(cancelled);
            }
        }
    }
}
TEST_CASE("fidelity: touch cycle wraps, fades Enhanced to GB, and respects independent locks") {
    Fidelity f;
    f.cycle(1'000'000'000);
    CHECK(f.graphics() == FidelityMode::Gb);
    CHECK(f.music() == FidelityMode::Gb);
    CHECK(f.graphics_weights(1'000'000'000)[2] == 1);
    const auto w = f.graphics_weights(1'250'000'000);
    CHECK(w[0] == doctest::Approx(.5));
    CHECK(w[1] == 0);
    CHECK(w[2] == doctest::Approx(.5));
    CHECK(f.music_weights(1'250'000'000) == w);
    CHECK(f.graphics_weights(1'500'000'000)[0] == 1);
    f.cycle(2'000'000'000);
    CHECK(f.graphics() == FidelityMode::Wsc);
    f.cycle(3'000'000'000);
    CHECK(f.graphics() == FidelityMode::Enhanced);
    for (auto lock : {FidelityMode::Gb, FidelityMode::Wsc, FidelityMode::Enhanced}) {
        Fidelity graphics(lock, FidelityMode::Swap), music(FidelityMode::Swap, lock), both(lock, lock);
        for (auto mode : {FidelityMode::Gb, FidelityMode::Wsc, FidelityMode::Enhanced, FidelityMode::Gb}) {
            graphics.cycle(0);
            music.cycle(0);
            both.cycle(0);
            CHECK(graphics.graphics() == lock);
            CHECK(graphics.music() == mode);
            CHECK(music.graphics() == mode);
            CHECK(music.music() == lock);
            CHECK(both.graphics() == lock);
            CHECK(both.music() == lock);
        }
    }
}

TEST_CASE("touch: retained field clears margins after picture resizes and moves") {
    REQUIRE(SDL_InitSubSystem(SDL_INIT_VIDEO));
    auto *window = SDL_CreateWindow("retained margins", 1920, 1080, SDL_WINDOW_HIDDEN);
    REQUIRE(window);
    auto *renderer = SDL_CreateRenderer(window, "software");
    REQUIRE(renderer);
    auto *target =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, 1920, 1080);
    auto *source = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STATIC, 224, 144);
    REQUIRE(target);
    REQUIRE(source);
    std::vector<uint32_t> art(224 * 144);
    for (int y = 0; y < 144; ++y)
        for (int x = 0; x < 224; ++x)
            art[size_t(y * 224 + x)] = uint32_t(x << 16 | y << 8 | 0x80);
    REQUIRE(SDL_UpdateTexture(source, nullptr, art.data(), 224 * 4));
    REQUIRE(SDL_SetTextureScaleMode(source, SDL_SCALEMODE_NEAREST));
    REQUIRE(SDL_SetRenderTarget(renderer, target));
    app::BattleWipe wipe;
    const auto full = app::GameRect(1920, 1080);
    const auto smaller = app::GameRect(1680, 1008);
    const SDL_FRect rects[] = {{full.x, full.y, full.w, full.h},
                               {smaller.x, smaller.y, smaller.w, smaller.h},
                               {smaller.x + 20, smaller.y + 12, smaller.w - 224, smaller.h - 144}};
    for (const auto rect : rects) {
        REQUIRE(wipe.begin(renderer, 1920, 1080, 1, 1, rect));
        REQUIRE(SDL_RenderTexture(renderer, source, nullptr, &rect));
        wipe.end(renderer); // Copies the full retained target, just like Enhanced field presentation.
        auto *surface = SDL_RenderReadPixels(renderer, nullptr);
        REQUIRE(surface);
        auto *rgb = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_XRGB8888);
        REQUIRE(rgb);
        size_t dirty = 0;
        for (int y = 0; y < 1080; ++y)
            for (int x = 0; x < 1920; ++x)
                if (x < rect.x || x >= rect.x + rect.w || y < rect.y || y >= rect.y + rect.h) {
                    const auto *row = reinterpret_cast<const uint32_t *>(
                        static_cast<const uint8_t *>(rgb->pixels) + y * rgb->pitch);
                    dirty += (row[x] & 0xFFFFFF) != 0;
                }
        CHECK(dirty == 0);
        SDL_DestroySurface(rgb);
        SDL_DestroySurface(surface);
    }
    // Wipes must sample the retained resized/moved rectangle, rather than assume full-height.
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    w.cpu.s.halted = true;
    w.cpu.s.flags = 0;
    w.capture_layers(true);
    w.run_frame();
    w.run_frame();
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    wipe.draw(renderer, w, {0, 0, 224, 144}, 1);
    auto *surface = SDL_RenderReadPixels(renderer, nullptr);
    REQUIRE(surface);
    for (const auto point : {SDL_Point{20, 20}, SDL_Point{112, 72}, SDL_Point{210, 130}}) {
        Uint8 r, g, b, a;
        REQUIRE(SDL_ReadSurfacePixel(surface, point.x, point.y, &r, &g, &b, &a));
        CHECK(r == point.x);
        CHECK(g == point.y);
        CHECK(b == 0x80);
    }
    SDL_DestroySurface(surface);
    wipe.release();
    SDL_SetRenderTarget(renderer, nullptr);
    SDL_DestroyTexture(source);
    SDL_DestroyTexture(target);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
