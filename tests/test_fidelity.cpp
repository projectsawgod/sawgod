#include "app/host_settings.h"
#include "engine/fidelity.h"
#include "engine/input.h"
#include "game/bank35_world.h"
#include "game/game_config.h"
#include "game/machine.h"
#include <doctest/doctest.h>
#include <tuple>

using namespace sawgod;
TEST_CASE("fidelity: Swap starts Enhanced and steps stop at either end") {
    Fidelity f;
    CHECK(f.graphics() == FidelityMode::Enhanced);
    CHECK(f.music() == FidelityMode::Enhanced);
    f.step(1, 0);
    CHECK(f.graphics_weights(0)[2] == 1);
    for (auto mode : {FidelityMode::Wsc, FidelityMode::Gb, FidelityMode::Gb}) {
        f.step(-1, 1'000'000'000);
        CHECK(f.graphics() == mode);
        CHECK(f.music() == mode);
    }
    f.step(1, 2'000'000'000);
    CHECK(f.graphics() == FidelityMode::Wsc);
    f.step(1, 3'000'000'000);
    CHECK(f.graphics() == FidelityMode::Enhanced);
    f.step(1, 4'000'000'000);
    CHECK(f.graphics() == FidelityMode::Enhanced);
}
TEST_CASE("fidelity: the Enhanced title screen shows no screen effect") {
    for (auto screen : {ScreenMode::Off, ScreenMode::Lcd, ScreenMode::Ips}) {
        CHECK(app::effective_screen(FidelityMode::Enhanced, screen, true) == ScreenMode::Off);
        CHECK(app::effective_screen(FidelityMode::Enhanced, screen, false) == screen);
        CHECK(app::effective_screen(FidelityMode::Wsc, screen, true) == screen);
        CHECK(app::effective_screen(FidelityMode::Gb, screen, true) == screen);
    }
}
TEST_CASE("fidelity: every lock is independent and simultaneous fades last half a second") {
    for (auto locked : {FidelityMode::Gb, FidelityMode::Wsc, FidelityMode::Enhanced}) {
        Fidelity f(locked, FidelityMode::Swap);
        f.step(-1, 1'000'000'000);
        CHECK(f.graphics() == locked);
        CHECK(f.music() == FidelityMode::Wsc);
        CHECK(f.graphics_weights(1'250'000'000)[size_t(locked)] == 1);
        CHECK(f.music_weights(1'250'000'000)[1] == doctest::Approx(0.5));
        Fidelity g(FidelityMode::Swap, locked);
        g.step(-1, 1'000'000'000);
        CHECK(g.music() == locked);
        CHECK(g.graphics() == FidelityMode::Wsc);
        CHECK(g.graphics_weights(1'250'000'000)[2] == doctest::Approx(0.5));
        CHECK(g.graphics_weights(1'500'000'000)[1] == 1);
    }
    Fidelity f;
    f.step(-1, 0);
    CHECK(f.graphics_weights(250'000'000) == f.music_weights(250'000'000));
    const auto interrupted = f.graphics_weights(250'000'000);
    f.step(-1, 250'000'000);
    CHECK(f.graphics_weights(250'000'000) == interrupted);
    const auto midway = f.graphics_weights(500'000'000);
    CHECK(midway[0] == doctest::Approx(0.5));
    CHECK(midway[1] == doctest::Approx(0.25));
    CHECK(midway[2] == doctest::Approx(0.25));
    CHECK(f.graphics_weights(750'000'000)[0] == 1);
    f.settings(FidelityMode::Wsc, FidelityMode::Enhanced, 1'000'000'000);
    f.step(-1, 1'500'000'000);
    CHECK(f.graphics() == FidelityMode::Wsc);
    CHECK(f.music() == FidelityMode::Enhanced);
}
TEST_CASE("fidelity: defaults and independent locks round-trip through config") {
    const auto read = [](const std::string &text, FidelityMode graphics, FidelityMode music) {
        Config c;
        Settings s;
        s.register_all(c);
        c.from_toml(text);
        CHECK(s.gfx() == graphics);
        CHECK(s.music() == music);
        CHECK(c.overlong_help().empty());
        Config saved;
        Settings t;
        t.register_all(saved);
        saved.from_toml(c.to_toml());
        CHECK(t.gfx() == graphics);
        CHECK(t.music() == music);
    };
    read("", FidelityMode::Swap, FidelityMode::Swap);
    read("[video]\ngraphics = \"gb\"\n[audio]\nmusic_mode = \"enhanced\"\n", FidelityMode::Gb,
         FidelityMode::Enhanced);
}
TEST_CASE("fidelity controls: shoulders exclusively step Swap and R2 fast-forwards") {
    using namespace sawgod::ui;
    using K = Binding::Kind;
    auto m = InputMap::defaults();
    const Binding rb{K::PadButton, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER};
    const Binding lb{K::PadButton, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER};
    const Binding rt{K::PadAxis, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1};
    CHECK(m.keys[int(WscButton::FidelityDown)][0] == Binding{K::Key, SDL_SCANCODE_G});
    CHECK(m.keys[int(WscButton::FidelityUp)][0] == Binding{K::Key, SDL_SCANCODE_H});
    CHECK(m.keys[int(WscButton::FastForward)][0] == Binding{K::Key, SDL_SCANCODE_TAB});
    CHECK(m.pad[int(WscButton::FidelityDown)][0] == lb);
    CHECK(m.pad[int(WscButton::FidelityUp)][0] == rb);
    CHECK(m.pad[int(WscButton::FastForward)][0] == rt);
    CHECK(m.keys[int(WscButton::Y2)][0] == Binding{K::Key, SDL_SCANCODE_D});
    CHECK(m.keys[int(WscButton::Y4)][0] == Binding{K::Key, SDL_SCANCODE_A});
    CHECK(m.pad[int(WscButton::Y2)][0] == Binding{K::PadAxis, SDL_GAMEPAD_AXIS_RIGHTX, 1});
    CHECK(m.pad[int(WscButton::Y4)][0] == Binding{K::PadAxis, SDL_GAMEPAD_AXIS_RIGHTX, -1});
    CHECK(m.pad[int(WscButton::Y2)][1].empty());
    CHECK(m.pad[int(WscButton::Y4)][1].empty());
    for (const auto &[y, step, shoulder] : {std::tuple{WscButton::Y2, WscButton::FidelityUp, rb},
                                            std::tuple{WscButton::Y4, WscButton::FidelityDown, lb}}) {
        auto rebound = InputMap::defaults();
        CHECK(rebound.assign(y, 1, shoulder) == step);
        CHECK(rebound.pad[int(y)][1] == shoulder);
        CHECK(rebound.pad[int(step)][0].empty());
        CHECK(rebound.assign(step, 0, shoulder) == y);
        CHECK(rebound == m);
    }
    // Every default input belongs to at most one action.
    for (bool gamepad : {false, true})
        for (int i = 0; i < kWscButtonCount; ++i)
            for (const Binding &binding : m.slots(WscButton(i), gamepad))
                if (!binding.empty())
                    for (int j = i + 1; j < kWscButtonCount; ++j)
                        for (const Binding &other : m.slots(WscButton(j), gamepad))
                            CHECK(binding != other);
    // West opens Options, north also starts the game; L2 stays free.
    CHECK(m.pad[int(WscButton::Encounters)][0] == Binding{K::PadButton, SDL_GAMEPAD_BUTTON_LEFT_STICK});
    CHECK(m.pad[int(WscButton::Minimap)][0] == Binding{K::PadButton, SDL_GAMEPAD_BUTTON_RIGHT_STICK});
    CHECK(m.pad[int(WscButton::Options)][0] == Binding{K::PadButton, SDL_GAMEPAD_BUTTON_BACK});
    CHECK(m.pad[int(WscButton::Start)][1] == Binding{K::PadButton, SDL_GAMEPAD_BUTTON_NORTH});
    for (int i = 0; i < kWscButtonCount; ++i)
        for (const Binding &b : m.pad[size_t(i)]) {
            CHECK(b != Binding{K::PadAxis, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1});
        }
    Config c;
    c.set_raw("gamepad.fidelity_up", "[\"rb\"]");
    m.load(c);
    CHECK(m.pad[int(WscButton::Y2)][1].empty());
    CHECK(m.assign(WscButton::FidelityUp, 1, rb) == WscButton::Count);
    CHECK(m.pad[int(WscButton::Y2)][1].empty());
    CHECK(m.assign(WscButton::A, 1, rb) != WscButton::Count);
    CHECK(m.pad[int(WscButton::Y2)][1].empty());
    CHECK(m.pad[int(WscButton::FidelityUp)][1].empty());
    CHECK(m.assign(WscButton::FastForward, 1, lb) != WscButton::Count);
    CHECK(m.pad[int(WscButton::Y4)][1].empty());
    CHECK(m.pad[int(WscButton::FidelityDown)][0].empty());
    for (const auto action : {WscButton::FidelityDown, WscButton::FidelityUp}) {
        m = InputMap::defaults();
        const Binding shoulder = m.pad[int(action)][0];
        const Binding start{K::PadButton, SDL_GAMEPAD_BUTTON_START};
        // Swap the shoulder into Start, then persist both owners.
        CHECK(m.assign(action, 0, start) == WscButton::Start);
        CHECK(m.pad[int(WscButton::Start)][0] == shoulder);
        CHECK(m.pad[int(action)][0] == start);
        CHECK(m.pad[int(action == WscButton::FidelityDown ? WscButton::Y4 : WscButton::Y2)][1].empty());
        m.store(c);
        auto reloaded = InputMap::defaults();
        reloaded.load(c);
        CHECK(reloaded == m);
    }
}
TEST_CASE("fidelity controls: hand-edited Y shoulders use ordinary config conflicts") {
    using namespace sawgod::ui;
    for (const auto &[y, step, id] : {std::tuple{WscButton::Y2, WscButton::FidelityUp, "rb"},
                                      std::tuple{WscButton::Y4, WscButton::FidelityDown, "lb"}}) {
        CAPTURE(id);
        const Binding shoulder = *binding_from_id(id, true);
        const std::string y_key = std::string("gamepad.") + wsc_button_id(y);
        const std::string step_key = std::string("gamepad.") + wsc_button_id(step);
        Config c;
        c.set_raw(y_key, "[\"" + std::string(id) + "\"]");
        auto m = InputMap::defaults();
        m.load(c);
        // A listed Y binding takes the shoulder from the default Swap action.
        CHECK(m.pad[int(y)][0] == shoulder);
        CHECK(m.pad[int(step)][0].empty());
        m.store(c);
        auto reloaded = InputMap::defaults();
        reloaded.load(c);
        CHECK(reloaded == m);

        // With both actions listed, the later action keeps the input, just like any conflict.
        c.set_raw(step_key, "[\"" + std::string(id) + "\"]");
        m = InputMap::defaults();
        std::vector<std::string> warnings;
        m.load(c, &warnings);
        CHECK(m.pad[int(y)][0].empty());
        CHECK(m.pad[int(step)][0] == shoulder);
        CHECK(warnings.size() == 1);
    }
}
TEST_CASE("host dash: only the four world maps and their later-story copies override running_allowed") {
    using namespace sawgod::game;
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    w.io[0x60] = 0x80; // high RAM enabled, as in the running WSC game
    Machine m(w);
    m.ss() = 0;
    m.cs() = 0x5000;
    auto &session = Session::of(w);
    GameConfig config;
    for (int map = 0; map < 211; ++map) {
        w.ram[0xD0DC] = uint8_t(map);
        config.set(session, false);
        CHECK_FALSE(hooks::should(hooks::VB::RunningAllowed, m, false));
        config.set(session, true);
        CHECK(hooks::should(hooks::VB::RunningAllowed, m, false) ==
              (GameConfig::world_map(map) || map == 199 || map == 24));
        CHECK(hooks::should(hooks::VB::RunningAllowed, m, true));
    }
    w.ram[0xD0DC] = 88;
    for (int script : {0, 1})
        for (int vehicle : {0, 1, 2, 3})
            for (int b_runs : {0, 1})
                for (int held : {0, 1}) {
                    GameConfigValues v;
                    v.b_runs = b_runs;
                    GameConfig::apply(w, v);
                    w.ram[0xD52C] = uint8_t(script);
                    w.ram[0xD0D6] = uint8_t(vehicle);
                    w.ram[0xD59C] = 0;
                    w.ram[0xC800] = held ? 8 : 0;
                    m.cx() = 0; // no scroll edge writes; exercise the actual native movement speed gate
                    prepare_player_scroll_core(m);
                    // Preserve the existing native bit-4/B comparison; on foot and on the World 2 island
                    // (travel mode 1) only; the glider and the bike keep their speed.
                    const bool dash = !script && vehicle <= 1 && (held == b_runs);
                    CHECK(w.ram[0xD528] == (dash ? 4 : 2));
                    if (dash)
                        CHECK(w.ram[0xD517] == 1);
                }
    config.set(session, false);
    w.ram[0xD52C] = w.ram[0xD0D6] = 0;
    w.ram[0xD0E7] = 5;
    w.ram[0xC800] = 0;
    prepare_player_scroll_core(m);
    CHECK(w.ram[0xD528] == 2);
    // Off (headless runs, the equivalence checks): the island never runs, even where walking does.
    w.ram[0xD59C] = 1;
    w.ram[0xC800] = 8;
    prepare_player_scroll_core(m);
    CHECK(w.ram[0xD528] == 4);
    w.ram[0xD0D6] = 1;
    prepare_player_scroll_core(m);
    CHECK(w.ram[0xD528] == 2);
}

TEST_CASE("Instant message speed writes the native sentinel without changing other Config bytes") {
    sawgod::Config c;
    sawgod::Settings s;
    s.register_all(c);
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0x10000);
    for (int speed : {8, 5, 3, 2, 1, 11}) {
        c.set(*s.message_speed, speed);
        sawgod::game::GameConfig::apply(w, sawgod::app::game_config_values(s));
        CHECK(w.ram[0xD0E6] == speed - 1);
        CHECK((w.ram[0xD0E7] & 0x0F) == 0x0F);
        CHECK(w.ram[0xD0E8] == 3);
    }
}

TEST_CASE("fidelity: title hint follows the unlocked track and disappears when both tracks lock") {
    for (auto graphics : {FidelityMode::Gb, FidelityMode::Wsc, FidelityMode::Enhanced, FidelityMode::Swap})
        for (auto music : {FidelityMode::Gb, FidelityMode::Wsc, FidelityMode::Enhanced, FidelityMode::Swap}) {
            Fidelity f(graphics, music);
            const bool swap = graphics == FidelityMode::Swap || music == FidelityMode::Swap;
            CHECK(f.step_mode().has_value() == swap);
            for (auto expected : {FidelityMode::Wsc, FidelityMode::Gb, FidelityMode::Gb}) {
                f.step(-1, 0);
                if (swap)
                    CHECK(f.step_mode() == expected);
                else
                    CHECK_FALSE(f.step_mode());
            }
            f.step(1, 0);
            if (swap)
                CHECK(f.step_mode() == FidelityMode::Wsc); // target changes before the fade finishes
        }
    Fidelity f;
    f.settings(FidelityMode::Wsc, FidelityMode::Swap, 0);
    f.step(-1, 0);
    f.settings(FidelityMode::Swap, FidelityMode::Swap, 0);
    CHECK(f.step_mode() == FidelityMode::Enhanced); // independent retained Swap positions: picture wins
}

TEST_CASE("Battle Speed clocks one sound frame while held Fast-Forward keeps its audio speed") {
    Config config;
    Settings settings;
    settings.register_all(config);
    for (int choice = 0; choice < 3; ++choice) {
        config.set(*settings.battle_speed, choice);
        const auto battle = app::frame_speed(settings, false, false, true);
        int sound_frames = 0;
        for (int step = 0; step < battle.speed; ++step)
            sound_frames += battle.audio_tick(step);
        CHECK(sound_frames == 1);
        CHECK(battle.audio_speed() == 1);
        // Running late, Battle Speed keeps only its sound frame; Fast-Forward keeps every frame.
        CHECK(battle.steps(true) == 1);
        CHECK(battle.audio_tick(0, true));
        const auto held = app::frame_speed(settings, false, true, true);
        for (int step = 0; step < held.speed; ++step)
            CHECK(held.audio_tick(step));
        CHECK(held.audio_speed() == held.speed);
        CHECK(held.steps(true) == held.speed);
        const auto paused = app::frame_speed(settings, true, true, true);
        CHECK(paused.speed == 1);
        CHECK(paused.audio_tick(0));
        CHECK(paused.audio_speed() == 1);
    }
}
