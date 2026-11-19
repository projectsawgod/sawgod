#include "engine/palette_fade.h"
#include "game/battle_ui.h"
#include "game/dialogue_ui.h"
#include "game/session.h"
#include "test_presentation_support.h"

TEST_CASE("native palette fades bound every Enhanced color without recolor false positives") {
    const auto packed = [](int v) { return uint16_t(v * 0x111); };
    for (int amount = -15; amount <= 15; ++amount) {
        PaletteFadeFit fade;
        for (int target = 0; target <= 15; ++target)
            fade.add(packed(target), packed(std::clamp(target + amount, 0, 15)));
        const auto range = fade.range();
        CHECK(range[0] == doctest::Approx(float(std::max(0, amount)) / 15));
        CHECK(range[1] == doctest::Approx(float(std::min(15, 15 + amount)) / 15));
    }
    for (int amount = 0; amount <= 15; ++amount) {
        PaletteFadeFit cap, floor;
        for (int target = 0; target <= 15; ++target) {
            cap.add(packed(target), packed(std::min(target, amount)));
            floor.add(packed(target), packed(std::max(target, amount)));
        }
        CHECK(cap.range()[0] == 0);
        CHECK(cap.range()[1] == doctest::Approx(float(amount) / 15));
        CHECK(floor.range()[0] == doctest::Approx(float(amount) / 15));
        CHECK(floor.range()[1] == 1);
    }
    PaletteFadeFit empty, animation;
    CHECK(empty.range() == std::array<float, 2>{0, 1});
    animation.add(0x123, 0x321);
    animation.add(0xABC, 0xABF);
    CHECK(animation.range() == std::array<float, 2>{0, 1});
}

TEST_CASE("scene fades read completed raster colors without changing the machine") {
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    display(w, 1);
    w.io[0] = 1;
    w.io[7] = 2;
    w.ram[0xD0DC] = 1;
    w.ram[0xD0D6] = 0;
    w.rom[0x35A830 + 18 + 1] = 0xE0;
    std::fill_n(w.ram.begin() + 0xD52D, 9, 0);
    w.ram[0xD52D] = 0xE0;
    const auto word = [&](int at, uint16_t value) {
        w.ram[size_t(at)] = uint8_t(value);
        w.ram[size_t(at + 1)] = uint8_t(value >> 8);
    };
    for (int i = 0; i < 16; ++i) {
        std::fill_n(w.ram.begin() + 0x4000 + (i + 2) * 32, 32, uint8_t(i * 0x11));
        word(0xC600 + i * 2, uint16_t(i * 0x111));
    }
    for (int cell = 0; cell < 32 * 32; ++cell)
        word(0x1000 + cell * 2, uint16_t(2 + cell % 16));
    w.capture_layers(true);
    game::SceneObserver observer;
    observer.install(w);
    game::Machine m(w);
    game::hooks::emit(game::hooks::Event::PresentationField, m, {1});
    for (int amount = -15; amount <= 15; ++amount) {
        CAPTURE(amount);
        for (int i = 0; i < 16; ++i)
            word(0xFE00 + i * 2, uint16_t(std::clamp(i + amount, 0, 15) * 0x111));
        w.run_frame();
        w.run_frame();
        // The live palette already belongs to the next tick; the displayed raster does not.
        std::fill_n(w.ram.begin() + 0xFE00, 32, 0xFF);
        const auto ram = w.ram;
        const auto scene = observer.describe(w, false, false, false);
        if (amount == -15 || amount == 15) {
            // A completely faded field is uniform: the native picture owns it, with no effects at all.
            CHECK(scene.kind == game::PresentationScene::Kind::Transition);
            CHECK(scene.field_cleared);
        } else {
            CHECK(scene.palette_range[0] == doctest::Approx(float(std::max(0, amount)) / 15));
            CHECK(scene.palette_range[1] == doctest::Approx(float(std::min(15, 15 + amount)) / 15));
        }
        CHECK(w.ram == ram);
    }
}

TEST_CASE("scanline planes recompose priority windows flips text and second-picture colors") {
    for (unsigned seed = 1; seed <= 8; ++seed) {
        CAPTURE(seed);
        emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
        display(w, seed);
        w.capture_layers(true);
        w.scene_hidden_palettes = 0x8000;
        w.scene_hidden_sprite_prefix = 5;
        w.scene_hidden_sprite_palettes = 0x8000;
        w.host_cells.push_back({20, 25, 0xE200});
        const uint16_t at = 0x1000;
        emu::Wsc::CellPixels host;
        host.entry = uint16_t(w.ram[at] | w.ram[at + 1] << 8);
        host.px.fill(3);
        host.over[9] = 6;
        w.cell_pixels[at] = host;
        w.set_second(true);
        w.upper_palette_greys = true;
        w.run_frame();
        w.run_frame();
        for (bool second : {false, true})
            for (bool scene : {false, true})
                exact_layers(w, second, scene);
        // Palette writes between raster lines must remain attached to the earlier line.
        const auto prior = w.frame_layers()->planes[emu::FrameLayers::Backdrop][0].color;
        w.ram[0xFE00] ^= 0xFF;
        CHECK(w.frame_layers()->planes[emu::FrameLayers::Backdrop][0].color == prior);
        w.set_second(false);
        w.set_second(true);
        CHECK(w.frame_layers(true)->valid[0] == 0);
        w.reset();
        CHECK(w.frame_layers()->valid[0] == 0);
        w.capture_layers(false);
        CHECK(w.frame_layers() == nullptr);
    }
}

TEST_CASE("first-picture-only layer capture keeps both pictures and the first picture's layers exact") {
    for (bool hidden : {false, true}) {
        CAPTURE(hidden);
        emu::Wsc all(std::vector<uint8_t>(0x400000), 0), first(std::vector<uint8_t>(0x400000), 0);
        for (auto *w : {&all, &first}) {
            display(*w, 9);
            w->scene_hidden_palettes = hidden ? 0x8000 : 0;
            w->set_second(true);
            w->upper_palette_greys = true;
        }
        all.capture_layers(true);
        first.capture_layers(true, false); // as the app: nothing reads the second picture's layers
        for (int i = 0; i < 2; ++i) {
            all.run_frame();
            first.run_frame();
        }
        for (bool second : {false, true})
            for (bool scene : {false, true})
                CHECK(first.framebuffer_rgb(emu::Wsc::ColorMode::Raw, second, scene) ==
                      all.framebuffer_rgb(emu::Wsc::ColorMode::Raw, second, scene));
        for (bool scene : {false, true})
            exact_layers(first, false, scene);
        CHECK(first.frame_layers(true) == nullptr);
        CHECK(first.frame_layers(true, true) == nullptr);
        CHECK(first.ram == all.ram);
    }
}

TEST_CASE("sprite-only scene hiding and optional metadata preserve ordinary display and machine state") {
    emu::Wsc a(std::vector<uint8_t>(0x400000), 0), b(std::vector<uint8_t>(0x400000), 0);
    display(a, 55);
    display(b, 55);
    a.capture_layers(true);
    a.scene_hidden_sprite_prefix = 128;
    for (int frame = 0; frame < 3; ++frame) {
        a.run_frame();
        b.run_frame();
        CHECK(a.framebuffer() == b.framebuffer());
        CHECK(a.ram == b.ram);
        CHECK(a.io == b.io);
        CHECK(a.sram == b.sram);
        CHECK(a.frame_count == b.frame_count);
        CHECK(a.cpu.cycles == b.cpu.cycles);
        exact_layers(a, false, true);
    }
}

TEST_CASE("UI coverage includes equal colors transparent fills glyphs cursors and host pictures") {
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    chrome_fixture::install(w);
    std::array<uint8_t, 224 * 144> coverage{};
    w.ui_coverage = &coverage;
    std::vector<uint32_t> px(224 * 144, chrome_fixture::kBorder);
    game::Chrome c(w, px, emu::Wsc::ColorMode::Raw, false);
    c.window(10, 10, 64, 32, game::Chrome::Window::Normal, 100);
    CHECK(coverage[10 * 224 + 10] == 255); // same-colored border
    CHECK(coverage[20 * 224 + 20] == 255); // fully transparent interior
    CHECK(coverage[9 * 224 + 10] == 0);
    c.text(U"Test", 20, 55);
    c.tile(0x1D5, 80, 55, true);
    CHECK(coverage[55 * 224 + 80] == 255);
    w.ui_coverage = nullptr;
}

TEST_CASE("scene observer follows the field picture through events and yields to other pictures") {
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    display(w, 1);
    w.io[7] = 2;
    w.ram[0xD0DC] = 1;
    w.rom[0x35A830 + 18 + 1] = 0xE0;
    std::fill_n(w.ram.begin() + 0xD52D, 9, 0);
    w.ram[0xD52D] = 0xE0;
    w.ram[0xD0D6] = 0;
    w.capture_layers(true);
    w.run_frame();
    game::SceneObserver observer;
    observer.install(w);
    game::Machine m(w);
    using Kind = game::PresentationScene::Kind;
    CHECK(observer.describe(w, false, false, false).kind == Kind::Unknown);
    game::hooks::emit(game::hooks::Event::PresentationField, m, {1});
    CHECK(observer.describe(w, false, false, false).kind == Kind::Field);
    CHECK(observer.describe(w, false, true, false).kind == Kind::Unknown);
    game::hooks::emit(game::hooks::Event::PresentationField, m, {0});
    // Scripted pans and outgoing warps still show the field.
    CHECK(observer.describe(w, false, false, false).kind == Kind::Field);
    w.io[0x10] ^= 8;
    CHECK(observer.describe(w, false, false, false).kind == Kind::Field);
    w.io[0x10] ^= 8;
    w.ram[0xD0DC] = 2;
    CHECK(observer.describe(w, false, false, false).kind == Kind::Field);
    CHECK(observer.describe(w, false, false, false).map == 1);
    w.ram[0xD0DC] = 1;
    game::hooks::emit(game::hooks::Event::PresentationField, m, {1});
    w.ram[0xD0DC] = 177;
    w.rom[0x35A830 + 177 * 18 + 1] = 0xE0;
    CHECK(observer.describe(w, false, false, false).kind == Kind::Field);
    w.ram[0xD0DC] = 1;
    w.ram[0xD0D6] = 1;
    CHECK(observer.describe(w, false, false, false).kind == Kind::Field);
    w.ram[0xD0D6] = 0;
    game::hooks::emit(game::hooks::Event::BattleStart, m);
    const auto battle = observer.describe(w, true, false, false);
    CHECK(battle.kind == Kind::Battle);
    CHECK((battle.mask[80 * 224] & 0xFF0000) == 0xFF0000);
    game::hooks::emit(game::hooks::Event::BattleRewards, m);
    CHECK(observer.describe(w, true, false, false).kind == Kind::Battle); // the battle UI still shows it
    CHECK(observer.describe(w, false, false, false).kind ==
          Kind::Battle); // the last native portrait still owns this raster
    game::Session::of(w).new_generation();
    CHECK(observer.describe(w, false, false, false).kind == Kind::Unknown);
}

TEST_CASE("field shadows use latched actor origins and never group window cursor sprites") {
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    display(w, 1);
    w.ram.fill(0);
    w.io[0] = 5;
    w.io[6] = 5;
    w.io[7] = 2;
    w.ram[0xD0DC] = 1;
    w.rom[0x35A830 + 18 + 1] = 0xE0;
    std::fill_n(w.ram.begin() + 0xD52D, 9, 0);
    w.ram[0xD52D] = 0xE0;
    w.ram[0xD0D6] = 0;
    const auto put = [&](uint16_t at, uint16_t value) {
        w.ram[at] = uint8_t(value);
        w.ram[at + 1] = uint8_t(value >> 8);
    };
    for (int i = 0; i < 5; ++i) {
        const int tile = i < 4 ? 0x10 + i : 0x1EE;
        std::fill_n(w.ram.begin() + 0x4000 + tile * 32, 32, 0x11);
        put(uint16_t(0x200 + i * 4), uint16_t(tile | (i == 4 ? 0x2E00 : 0)));
        w.ram[0x202 + i * 4] = uint8_t(i < 4 ? 40 + (i / 2) * 8 : 16);
        w.ram[0x203 + i * 4] = uint8_t(i < 4 ? 100 + (i % 2) * 8 : 20);
    }
    // A real field raster has terrain as well as OAM; a sprite-only cleared layer is a transition.
    put(0x1000, 1);
    put(0xFE02, 0x123);
    std::fill_n(w.ram.begin() + 0x4020, 32, 0x11);
    w.capture_layers(true);
    w.run_frame();
    w.run_frame();
    game::SceneObserver observer;
    observer.install(w);
    game::Machine m(w);
    game::hooks::emit(game::hooks::Event::PresentationField, m, {1});
    const auto scene = observer.describe(w, false, false, false);
    REQUIRE(scene.actors.size() == 1);
    CHECK(scene.actors[0][0] == 108);
    CHECK(scene.actors[0][1] == 54.5f);
    CHECK((scene.mask[16 * 224 + 20] & 0xFF0000) != 0);
    CHECK((scene.mask[40 * 224 + 100] & 0x00FF00) != 0); // shadow stays below actor coverage
    // Three transparent rows below the feet must not move the shadow down to the cell bottom.
    for (int tile : {0x12, 0x13})
        std::fill_n(w.ram.begin() + 0x4000 + tile * 32 + 5 * 4, 3 * 4, 0);
    w.run_frame();
    w.run_frame();
    CHECK(observer.describe(w, false, false, false).actors[0][1] == 51.5f);
    // The flat party veil draws those feet on native frames that omit the lower OAM cells too.
    w.terrain_cards[0] = {0, 0x10, true};
    w.publish_terrain_cards();
    w.io[6] = 2;
    w.run_frame();
    w.run_frame();
    CHECK(observer.describe(w, false, false, false).actors[0][1] == 51.5f);
    // Staging RAM changes after rendering cannot move the captured foot anchor.
    w.ram[0x203] = 1;
    CHECK(observer.describe(w, false, false, false).actors[0][0] == 108);
    game::hooks::emit(game::hooks::Event::BattleStart, m);
    // The native battle's first 27 entries belong to cursors/status icons,
    // independently of their palette or tile (party icons can use actor art).
    CHECK((observer.describe(w, true, false, false).mask[40 * 224 + 100] & 0xFF0000) != 0);
}

TEST_CASE("battle ownership protects equal-colored enemies and anchors shadows to portrait cells") {
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    display(w, 1);
    w.ram.fill(0);
    w.io[0] = 3;
    w.io[6] = 0;
    const auto put = [&](uint16_t at, uint16_t value) {
        w.ram[at] = uint8_t(value);
        w.ram[at + 1] = uint8_t(value >> 8);
    };
    std::fill_n(w.ram.begin() + 0x4000, 32, 0x11);
    for (int at = 0x1800; at < 0x2000; at += 2)
        put(uint16_t(at), 1); // transparent screen-two cells
    for (int group = 0; group < 3; ++group) {
        const int tile = 0x300 + group * 0x28;
        std::fill_n(w.ram.begin() + 0x4000 + tile * 32, 16, 0x22);
        put(uint16_t(0x1800 + (4 * 32 + 11 + group * 2) * 2),
            uint16_t(0x2000 | ((4 + group) << 9) | (tile & 511)));
        put(uint16_t(0xFE00 + (4 + group) * 32 + 4), 0x456);
    }
    put(0xFE02, 0x456); // ownership must work even when enemy and background RGB match
    w.capture_layers(true);
    w.run_frame();
    w.run_frame();
    game::SceneObserver observer;
    observer.install(w);
    game::Machine m(w);
    game::hooks::emit(game::hooks::Event::BattleStart, m);
    const auto scene = observer.describe(w, true, false, false);
    REQUIRE(scene.actors.size() == 3);
    REQUIRE(scene.background.size() == 224 * 144);
    for (int group = 0; group < 3; ++group) {
        const int x = 88 + group * 16;
        CHECK(scene.mask[size_t(32 * 224 + x)] == 0x00FF00);
        CHECK(scene.mask[size_t(36 * 224 + x)] == 0);
        CHECK(scene.background[size_t(32 * 224 + x)] == 0x445566);
        CHECK(scene.actors[size_t(group)][0] == x + 4);
        CHECK(scene.actors[size_t(group)][1] == 37); // 2 px above the portrait's bottom row (design decision)
    }
    w.ram[0xD509] = 9; // post-raster descriptor edits cannot move captured feet
    CHECK(observer.describe(w, true, false, false).actors[0][1] == 37);

    // The Creator's breakup retains exact enemy art/lighting ownership, without
    // grounding its flying rows. An installed but disabled IRQ is not a breakup.
    put(0x9C, 0xC681);
    put(0x9E, 0x6000);
    CHECK(observer.describe(w, true, false, false).actors == scene.actors);
    w.io[0xB2] |= 0x80;
    const auto ram = w.ram;
    const auto io = w.io;
    const auto breakup = observer.describe(w, true, false, false);
    CHECK(breakup.actors.empty());
    CHECK(breakup.mask == scene.mask);
    CHECK(breakup.background == scene.background);
    CHECK(breakup.battle_groups == scene.battle_groups);
    CHECK(w.ram == ram);
    CHECK(w.io == io);
    put(0x9C, 0xC7C2); // the shutter's offset on the HBlank vector is not a breakup
    CHECK(observer.describe(w, true, false, false).actors == scene.actors);
    w.io[0xB2] &= ~0x80;
    put(0x9C, 0xC681);
    CHECK(observer.describe(w, true, false, false).actors == scene.actors);

    // Every other boss's defeat shutter slides the same rows through the line-compare IRQ.
    put(0x90, 0xC7C2);
    put(0x92, 0x6000);
    CHECK(observer.describe(w, true, false, false).actors == scene.actors);
    w.io[0xB2] |= 0x10;
    const auto shutter = observer.describe(w, true, false, false);
    CHECK(shutter.actors.empty());
    CHECK(shutter.mask == scene.mask);
    CHECK(shutter.battle_groups == scene.battle_groups);
    put(0x90, 0x4CBC); // the battle wave keeps its shadows
    CHECK(observer.describe(w, true, false, false).actors == scene.actors);
    w.io[0xB2] &= ~0x10;

    // A one-group formation (a boss such as Genbu) spreads one picture over palettes 4-6: it is one group,
    // with one shadow and one outline/light center over the whole picture.
    w.ram[0xD6D4] = 1;
    const auto boss = observer.describe(w, true, false, false);
    REQUIRE(boss.actors.size() == 1);
    CHECK(boss.actors[0][0] == 88 + 16 + 4); // centered on the cells spanning 88-127
    CHECK(boss.actors[0][2] == 40);
    CHECK(boss.battle_groups[0][0] == 108);
    CHECK(boss.battle_groups[1][3] == 0);
    CHECK(boss.battle_groups[2][3] == 0);
    w.ram[0xD6D4] = 3;
    CHECK(observer.describe(w, true, false, false).actors == scene.actors);
}

TEST_CASE("delivery glow samples mixed live palette pixels while casting stays white") {
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    display(w, 1);
    w.ram.fill(0);
    w.io[0] = 4;
    w.io[6] = 28;
    const auto put = [&](uint16_t at, uint16_t value) {
        w.ram[at] = uint8_t(value);
        w.ram[at + 1] = uint8_t(value >> 8);
    };
    std::fill_n(w.ram.begin() + 0x4000 + 0x20 * 32, 32, 0x21); // equal coverage of palette entries 1/2
    put(0x200 + 27 * 4, 0x2820);
    w.ram[0x202 + 27 * 4] = 40;
    w.ram[0x203 + 27 * 4] = 88;
    w.capture_layers(true);
    game::SceneObserver observer;
    observer.install(w);
    observer.battle_animations({false, false, false, false, false, true});
    game::Machine m(w);
    game::hooks::emit(game::hooks::Event::BattleStart, m);
    for (int rom : {0x378130, 0x378EA0, 0x3788B0, 0x3784B0}) {
        game::hooks::emit(game::hooks::Event::BattleAnimation, m, {rom, 0});
        // Change RAM palette colors during the same animation, keeping its caller/palette ID fixed.
        // Include a dim saturated pair below the old bright-pixel threshold, then black.
        for (const auto pair :
             {std::array<uint16_t, 2>{0xF20, 0x08F}, {0x04F, 0xF80}, {0x200, 0x010}, {0, 0}}) {
            CAPTURE(rom);
            CAPTURE(pair);
            put(0xFE00 + 12 * 32 + 2, pair[0]);
            put(0xFE00 + 12 * 32 + 4, pair[1]);
            w.run_frame();
            w.run_frame();
            w.run_frame();
            const auto scene = observer.describe(w, true, false, false);
            if (rom != 0x378130 && pair[0] == 0 && pair[1] == 0) {
                CHECK(scene.lights.empty()); // no lit pixels means no sampled delivery light
                continue;
            }
            REQUIRE(scene.lights.size() == 1);
            const auto &light = scene.lights.front();
            std::array<double, 3> weighted{};
            for (const auto rgb : pair) {
                const double r = rgb >> 8 & 15, g = rgb >> 4 & 15, b = rgb & 15;
                const double weight = 3 * r + 6 * g + b;
                weighted[0] += weight * r;
                weighted[1] += weight * g;
                weighted[2] += weight * b;
            }
            const auto peak = *std::max_element(weighted.begin(), weighted.end());
            for (size_t channel = 0; channel < 3; ++channel)
                CHECK(light.emission[channel] ==
                      doctest::Approx(rom == 0x378130 ? 1 : weighted[channel] / peak));
            CHECK(light.position[2] == 8);
            CHECK(light.source_pixels == std::array<uint32_t, 2>{0xFFFFFFFF, 0xFFFFFFFF});
            CHECK(light.beam[3] == 64);
        }
        game::hooks::emit(game::hooks::Event::BattleAnimationEnd, m, {rom});
    }
}

TEST_CASE("GPU effects preserve black fades and restore equal-colored transparent UI [GPU]") {
    if (!std::getenv("SAWGOD_GPU_TESTS")) {
        MESSAGE("GPU check skipped: set SAWGOD_GPU_TESTS with a GPU-capable SDL driver");
        return;
    }
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    auto *window = SDL_CreateWindow("GPU test", 672, 432, SDL_WINDOW_HIDDEN);
    REQUIRE(window);
    PresentationRenderer renderer;
    REQUIRE(renderer.init(window, false, false));
    REQUIRE(renderer.is_gpu());
    EnhancedGraphics effects;
    REQUIRE(effects.init(renderer.get()));
    std::vector<uint32_t> colors(224 * 144, 0), mask(colors.size(), 0);
    EnhancedParameters params;
    params.grade = params.bloom = 3;
    params.actors.push_back({100, 60, 6, 2});
    params.water_strength = 3;
    params.water.assign(colors.size(), 0x101025);
    params.light_strength = 3;
    params.lights.push_back({{100, 60, 26, 0}, {1, 0.65f, 0.2f, 0.6f}});
    const auto read = [&](SDL_Texture *texture) {
        REQUIRE(SDL_SetRenderTarget(renderer.get(), texture));
        auto *surface = SDL_RenderReadPixels(renderer.get(), nullptr);
        REQUIRE(surface);
        auto *rgb = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_XRGB8888);
        REQUIRE(rgb);
        std::vector<uint32_t> result(size_t(rgb->w) * rgb->h);
        for (int y = 0; y < rgb->h; ++y)
            std::memcpy(result.data() + y * rgb->w, static_cast<uint8_t *>(rgb->pixels) + y * rgb->pitch,
                        size_t(rgb->w) * 4);
        SDL_DestroySurface(rgb);
        SDL_DestroySurface(surface);
        SDL_SetRenderTarget(renderer.get(), nullptr);
        return result;
    };
    for (bool snap : {true, false}) {
        params.snap = snap;
        REQUIRE(effects.upload(colors, colors, mask.data(), params));
        auto *texture = effects.render();
        REQUIRE(texture);
        const auto black = read(texture);
        CHECK(std::all_of(black.begin(), black.end(), [](uint32_t p) { return (p & 0xFFFFFF) == 0; }));
    }
    // Both endpoints, with all color-adding effects and final battle lighting enabled. UI stays exact.
    params.battle_lighting.weight = 1;
    params.battle_lighting.groups[0] = {100, 60, 70, 2};
    params.battle_lighting.selection[0] = 1;
    for (const auto endpoint : {0u, 0xFFFFFFu}) {
        const float level = endpoint ? 1.f : 0.f;
        params.palette_range = {level, level};
        colors.assign(colors.size(), endpoint);
        REQUIRE(effects.upload(colors, colors, mask.data(), params));
        const auto faded = read(effects.render());
        CHECK(
            std::all_of(faded.begin(), faded.end(), [=](uint32_t p) { return (p & 0xFFFFFF) == endpoint; }));
    }
    params.palette_range = {0, 1};
    params.battle_lighting = {};
    colors.assign(colors.size(), 0x667788);
    for (int y = 40; y < 80; ++y)
        for (int x = 80; x < 140; ++x)
            mask[size_t(y * 224 + x)] = 0xFF0000;
    REQUIRE(effects.upload(colors, colors, mask.data(), params));
    auto *texture = effects.render();
    REQUIRE(texture);
    // Refreshes of the same flat input reuse the GPU result, retaining sharp final UI scaling.
    CHECK(effects.render() == texture);
    CHECK(effects.last_work.passes == 0);
    REQUIRE(effects.upload(colors, colors, mask.data(), params));
    CHECK(effects.render() == texture);
    CHECK(effects.last_work.passes == 0); // identical Smooth upload does not dirty the cached result
    const auto ui = read(texture);
    int changed_ui = 0, changed_scene = 0;
    for (int y = 0; y < 432; ++y)
        for (int x = 0; x < 672; ++x) {
            const bool changed = (ui[size_t(y * 672 + x)] & 0xFFFFFF) != 0x667788;
            if (mask[size_t((y / 3) * 224 + x / 3)])
                changed_ui += changed;
            else
                changed_scene += changed;
        }
    CHECK(changed_ui == 0);
    CHECK(changed_scene > 0);
    params.water.clear(); // Battle scenes never collect water.
    // Bright enemies must neither receive effects nor emit background bloom.
    // Compare the background to the identical scene with no enemy on top.
    mask.assign(mask.size(), 0);
    params.shadows = false;
    for (bool snap : {true, false}) {
        params.snap = snap;
        params.background.clear();
        colors.assign(colors.size(), 0x667788);
        REQUIRE(effects.upload(colors, colors, mask.data(), params));
        const auto background = read(effects.render());
        params.background = colors;
        for (int y = 24; y < 64; ++y)
            for (int x = 40; x < 64; ++x) {
                colors[size_t(y * 224 + x)] = 0xEEFF88;
                mask[size_t(y * 224 + x)] = 0x00FF00;
            }
        REQUIRE(effects.upload(colors, colors, mask.data(), params));
        const auto with_enemy = read(effects.render());
        int changed_enemy = 0, leaked_bloom = 0;
        for (int y = 0; y < 432; ++y)
            for (int x = 0; x < 672; ++x) {
                const size_t at = size_t(y * 672 + x);
                if (mask[size_t((y / 3) * 224 + x / 3)])
                    changed_enemy += (with_enemy[at] & 0xFFFFFF) != 0xEEFF88;
                else
                    leaked_bloom += with_enemy[at] != background[at];
            }
        CHECK(changed_enemy == 0);
        CHECK(leaked_bloom == 0);
        mask.assign(mask.size(), 0);
    }
    // With every other effect off, only tagged water changes and phase is deterministic.
    params.grade = params.bloom = params.light_strength = 0;
    params.shadows = false;
    params.background.clear();
    params.snap = true;
    params.water.assign(colors.size(), 0);
    colors.assign(colors.size(), 0x226688);
    mask.assign(mask.size(), 0);
    for (int y = 30; y < 90; ++y)
        for (int x = 40; x < 100; ++x)
            params.water[size_t(y * 224 + x)] = unsigned(x) << 16 | unsigned(y) << 8 | 28u;
    mask[50 * 224 + 60] = 0xFF0000; // equal-colored UI over water
    mask[60 * 224 + 70] = 0x00FF00; // enemy ownership can never receive water
    params.water_strength = 0;
    REQUIRE(effects.upload(colors, colors, mask.data(), params));
    const auto ordinary = read(effects.render());
    params.water_strength = 1;
    params.frame = 0;
    REQUIRE(effects.upload(colors, colors, mask.data(), params));
    const auto tide0 = read(effects.render());
    params.frame = 48;
    REQUIRE(effects.upload(colors, colors, mask.data(), params));
    const auto tide48 = read(effects.render());
    REQUIRE(effects.upload(colors, colors, mask.data(), params));
    CHECK(tide48 == read(effects.render()));
    int dry_changed = 0, wet_changed = 0, motion = 0, protected_changed = 0;
    for (int y = 0; y < 432; ++y)
        for (int x = 0; x < 672; ++x) {
            const size_t at = size_t(y * 672 + x), native = size_t((y / 3) * 224 + x / 3);
            const bool changed = ordinary[at] != tide0[at];
            if (mask[native])
                protected_changed += changed;
            else if (params.water[native]) {
                wet_changed += changed;
                motion += tide0[at] != tide48[at];
            } else
                dry_changed += changed;
            CHECK(tide48[at] == tide48[size_t((y / 3 * 3) * 672 + x / 3 * 3)]);
        }
    CHECK(dry_changed == 0);
    CHECK(protected_changed == 0);
    CHECK(wet_changed > 0);
    CHECK(motion > 0);
    // Fidelity fades the complete Enhanced result, including water, into exact WSC pixels.
    params.effect_weight = 0;
    REQUIRE(effects.upload(colors, colors, mask.data(), params));
    CHECK(read(effects.render()) == ordinary);
    params.effect_weight = 0.5f;
    REQUIRE(effects.upload(colors, colors, mask.data(), params));
    const auto halfway = read(effects.render());
    for (size_t i = 0; i < halfway.size(); ++i)
        for (int shift : {0, 8, 16}) {
            const int expected = int((ordinary[i] >> shift & 255) + (tide48[i] >> shift & 255)) / 2;
            CHECK(std::abs(int(halfway[i] >> shift & 255) - expected) <= 1);
        }
    params.effect_weight = 1;
    params.gb_weight = 1;
    REQUIRE(effects.upload(colors, colors, mask.data(), params));
    const auto gb = read(effects.render());
    CHECK(gb == ordinary);
    // Directional shafts: only forward scene pixels, all styles snapped and deterministic.
    params.gb_weight = 0;
    params.water_strength = 0;
    params.light_strength = 3;
    params.lights = {{{80, 40, 56, 0}, {1, 1, 1, 0.18f}, {0, 1, 1, 1}, 4}};
    mask.assign(mask.size(), 0);
    mask[60 * 224 + 80] = 0xFF0000;
    mask[70 * 224 + 80] = 0x00FF00;
    std::vector<std::vector<uint32_t>> styles;
    for (int style = 0; style < 3; ++style) {
        params.beam_style = style;
        params.frame = 0;
        REQUIRE(effects.upload(colors, colors, mask.data(), params));
        const auto shaft = read(effects.render());
        REQUIRE(effects.upload(colors, colors, mask.data(), params));
        CHECK(shaft == read(effects.render()));
        int forward = 0, backward = 0, protected_pixels = 0, off_grid = 0;
        for (int y = 0; y < 432; ++y)
            for (int x = 0; x < 672; ++x) {
                const size_t at = size_t(y * 672 + x), native = size_t((y / 3) * 224 + x / 3);
                const bool changed = shaft[at] != ordinary[at];
                if (mask[native])
                    protected_pixels += changed;
                else if (y / 3 < 40)
                    backward += changed;
                else
                    forward += changed;
                off_grid += shaft[at] != shaft[size_t((y / 3 * 3) * 672 + x / 3 * 3)];
            }
        CHECK(forward > 0);
        CHECK(backward == 0);
        CHECK(protected_pixels == 0);
        CHECK(off_grid == 0);
        styles.push_back(shaft);
        if (style == 2) {
            params.frame = 240;
            REQUIRE(effects.upload(colors, colors, mask.data(), params));
            CHECK(shaft != read(effects.render()));
        }
        params.gb_weight = 1;
        REQUIRE(effects.upload(colors, colors, mask.data(), params));
        CHECK(read(effects.render()) == ordinary);
        params.gb_weight = 0;
        const auto lit = colors;
        colors.assign(colors.size(), 0);
        REQUIRE(effects.upload(colors, colors, mask.data(), params));
        const auto dark = read(effects.render());
        CHECK(std::all_of(dark.begin(), dark.end(), [](uint32_t p) { return (p & 0xFFFFFF) == 0; }));
        colors = lit;
    }
    CHECK(styles[0] != styles[1]);
    CHECK(styles[0] != styles[2]);
    // Broad bright surfaces emit no bloom; a small isolated spot still does.
    params = {};
    params.grade = params.light_strength = params.water_strength = 0;
    params.shadows = false;
    params.bloom = 0;
    colors.assign(colors.size(), 0xDDDDDD);
    mask.assign(mask.size(), 0);
    REQUIRE(effects.upload(colors, colors, mask.data(), params));
    const auto bright_plain = read(effects.render());
    params.bloom = 3;
    REQUIRE(effects.upload(colors, colors, mask.data(), params));
    CHECK(read(effects.render()) == bright_plain);
    colors.assign(colors.size(), 0x333333);
    for (int y = 60; y < 63; ++y)
        for (int x = 100; x < 103; ++x)
            colors[size_t(y * 224 + x)] = 0xFFFFFF;
    REQUIRE(effects.upload(colors, colors, mask.data(), params));
    const auto small_bloom = read(effects.render());
    CHECK((small_bloom[size_t(61 * 3 * 672 + 98 * 3)] & 255) > 0x33);
    // Identical overlapping sources are exactly idempotent, with no light at 32 px.
    params.bloom = 0;
    params.light_strength = 1.5f;
    params.lights = {{{100.5f, 60.5f, 32, 0}, {1, 0.6f, 0.2f, 0.18f}}};
    colors.assign(colors.size(), 0x333333);
    REQUIRE(effects.upload(colors, colors, mask.data(), params));
    const auto single = read(effects.render());
    params.lights.push_back(params.lights.front());
    REQUIRE(effects.upload(colors, colors, mask.data(), params));
    CHECK(read(effects.render()) == single);
    const auto pixel = [&](const std::vector<uint32_t> &pixels, int x, int y) {
        return pixels[size_t(y * 3 * 672 + x * 3)] & 0xFFFFFF;
    };
    CHECK(pixel(single, 132, 60) == 0x333333);
    CHECK(pixel(single, 140, 60) == 0x333333);
    // Dense rooms must not silently drop emitters after the old 64-source limit.
    const auto source_light = params.lights.front();
    params.lights.assign(100, {{{-100, -100, 32, 0}}, {{1, 0.6f, 0.2f, 0.18f}}});
    params.lights.back() = source_light;
    REQUIRE(effects.upload(colors, colors, mask.data(), params));
    CHECK(read(effects.render()) == single);
    CHECK((pixel(single, 116, 60) >> 16) - 0x33 < ((pixel(single, 100, 60) >> 16) - 0x33) / 5);
    params.lights = {source_light};
    params.lights[0].type = 3;
    REQUIRE(effects.upload(colors, colors, mask.data(), params));
    const auto orb = read(effects.render());
    CHECK((pixel(orb, 100, 60) >> 16) - 0x33 <= 2);
    CHECK((pixel(orb, 108, 60) >> 16) > (pixel(orb, 100, 60) >> 16));
    // Spell patches reach visible targets, neighbors and the scene; UI and the native effect art stay exact.
    params = {};
    params.grade = params.bloom = params.water_strength = 0;
    params.shadows = false;
    params.light_strength = 1.5f;
    params.lights = {{{100.5f, 60.5f, 32, 0}, {0.2f, 0.8f, 1, 0.36f}, {0, 0, 0, 64}, 6}};
    colors.assign(colors.size(), 0x333333);
    mask.assign(mask.size(), 0);
    mask[60 * 224 + 100] = 0x00FF00; // visible target
    mask[60 * 224 + 116] = 0x00FF00; // neighbor's facing side, one tile from the effect
    mask[60 * 224 + 132] = 0x00FF00; // another neighbor, outside the reach
    mask[61 * 224 + 100] = 0x00FE00; // effect covering an enemy
    mask[59 * 224 + 100] = 0x004000; // effect covering background
    for (int y = 80; y < 144; ++y)
        std::fill_n(mask.begin() + y * 224, 224, 0xFF0000);
    mask[62 * 224 + 100] = 0xFF0000; // UI above the panels
    REQUIRE(effects.upload(colors, colors, mask.data(), params));
    auto *spell_target = effects.render();
    REQUIRE(spell_target);
    auto *ordinary_texture =
        SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 224, 144);
    REQUIRE(ordinary_texture);
    REQUIRE(SDL_SetTextureScaleMode(ordinary_texture, SDL_SCALEMODE_NEAREST));
    REQUIRE(SDL_UpdateTexture(ordinary_texture, nullptr, colors.data(), 224 * 4));
    REQUIRE(SDL_SetRenderTarget(renderer.get(), spell_target));
    // Visible enemy lighting belongs to the final restoration pass, as in the app's draw path.
    REQUIRE(effects.restore_ui(ordinary_texture, {0, 0, 672, 432}));
    const auto spell = read(spell_target);
    CHECK(pixel(spell, 100, 60) != 0x333333);
    CHECK(pixel(spell, 116, 60) != 0x333333);
    CHECK(pixel(spell, 132, 60) == 0x333333);
    CHECK(pixel(spell, 100, 61) == 0x333333);
    CHECK(pixel(spell, 100, 59) == 0x333333);
    CHECK(pixel(spell, 100, 62) == 0x333333);
    CHECK(pixel(spell, 108, 58) != 0x333333); // background
    CHECK(pixel(spell, 100, 76) != 0x333333); // floor
    for (int y = 80; y < 144; ++y)
        for (int x = 0; x < 224; ++x)
            CHECK(pixel(spell, x, y) == 0x333333);
    // The fused final draw must retain enemy spell lighting and exact UI coverage.
    // Compare to actual legacy GPU restoration/multiply draws, including a fractional output size.
    for (const auto dimensions : {SDL_Point{672, 432}, SDL_Point{1728, 1111}}) {
        const SDL_FRect dst{0, 0, float(dimensions.x), float(dimensions.y)};
        auto *finish_target = SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_XRGB8888,
                                                SDL_TEXTUREACCESS_TARGET, dimensions.x, dimensions.y);
        REQUIRE(finish_target);
        REQUIRE(SDL_SetRenderTarget(renderer.get(), finish_target));
        REQUIRE(SDL_RenderTexture(renderer.get(), spell_target, nullptr, &dst));
        REQUIRE(effects.restore_ui(ordinary_texture, dst));
        const auto reference = read(finish_target);
        REQUIRE(SDL_SetRenderTarget(renderer.get(), finish_target));
        REQUIRE(effects.present(spell_target, ordinary_texture, dst));
        CHECK(read(finish_target) == reference);
        CHECK(effects.final_work.passes == 1);
        CHECK(effects.final_work.shaded_pixels == uint64_t(dimensions.x) * dimensions.y);
        SDL_DestroyTexture(finish_target);
    }
    // A three-tile spell reaches beyond the old shader's hard two-tile cap in both passes.
    mask[60 * 224 + 140] = 0x00FF00;
    params.lights[0].position[2] = 48;
    REQUIRE(effects.upload(colors, colors, mask.data(), params));
    spell_target = effects.render();
    REQUIRE(SDL_SetRenderTarget(renderer.get(), spell_target));
    REQUIRE(effects.restore_ui(ordinary_texture, {0, 0, 672, 432}));
    const auto wide_spell = read(spell_target);
    CHECK(pixel(wide_spell, 140, 60) != 0x333333); // enemy 40 px away
    CHECK(pixel(wide_spell, 140, 59) != 0x333333); // background 40 px away
    CHECK(pixel(wide_spell, 148, 60) == 0x333333); // zero at three tiles
    CHECK(pixel(wide_spell, 100, 61) == 0x333333); // effect over enemy stays exact
    CHECK(pixel(wide_spell, 100, 59) == 0x333333); // effect over background stays exact
    CHECK(pixel(wide_spell, 100, 62) == 0x333333); // UI stays exact
    // Death dims target, neighbors, background and floor with the same silhouette/reach.
    params.lights[0].type = 7;
    params.lights[0].position[2] = 32;
    params.lights[0].emission = {32.0f / 255, 24.0f / 255, 40.0f / 255, 0.36f};
    REQUIRE(effects.upload(colors, colors, mask.data(), params));
    spell_target = effects.render();
    REQUIRE(SDL_SetRenderTarget(renderer.get(), spell_target));
    REQUIRE(effects.restore_ui(ordinary_texture, {0, 0, 672, 432}));
    const auto gloom = read(spell_target);
    for (auto [x, y] : {std::pair{100, 60}, {116, 60}, {108, 58}, {100, 76}}) {
        const uint32_t dim = pixel(gloom, x, y);
        CHECK((dim >> 16) < 0x33);
        CHECK((dim >> 8 & 255) < 0x33);
        CHECK((dim & 255) < 0x33);
    }
    CHECK((pixel(gloom, 100, 60) >> 16) < (pixel(gloom, 116, 60) >> 16));
    for (auto [x, y] : {std::pair{132, 60}, {100, 61}, {100, 59}, {100, 62}, {100, 80}})
        CHECK(pixel(gloom, x, y) == 0x333333); // out of reach, effect art, UI
    // Maximum-combined overlapping patches cannot multiply darkness.
    params.lights.push_back(params.lights.front());
    REQUIRE(effects.upload(colors, colors, mask.data(), params));
    spell_target = effects.render();
    REQUIRE(SDL_SetRenderTarget(renderer.get(), spell_target));
    REQUIRE(effects.restore_ui(ordinary_texture, {0, 0, 672, 432}));
    CHECK(read(spell_target) == gloom);
    // The restoration pass must also honor WSC/GB fidelity and Enhanced's fade weight.
    for (auto [effect, gb] : {std::pair{0.0f, 0.0f}, {1.0f, 1.0f}, {0.5f, 0.0f}}) {
        params.effect_weight = effect;
        params.gb_weight = gb;
        REQUIRE(effects.upload(colors, colors, mask.data(), params));
        spell_target = effects.render();
        REQUIRE(SDL_SetRenderTarget(renderer.get(), spell_target));
        REQUIRE(effects.restore_ui(ordinary_texture, {0, 0, 672, 432}));
        const auto faded = read(spell_target);
        for (auto [x, y] : {std::pair{100, 60}, {108, 58}}) {
            const auto value = pixel(faded, x, y);
            if (effect == 0 || gb == 1)
                CHECK(value == 0x333333);
            else {
                CHECK((value >> 16) > (pixel(gloom, x, y) >> 16));
                CHECK((value >> 16) < 0x33);
            }
        }
    }
    params.effect_weight = 1;
    params.gb_weight = 0;
    // Already-black pixels remain black; gloom never adds light.
    colors.assign(colors.size(), 0);
    REQUIRE(SDL_UpdateTexture(ordinary_texture, nullptr, colors.data(), 224 * 4));
    REQUIRE(effects.upload(colors, colors, mask.data(), params));
    spell_target = effects.render();
    REQUIRE(SDL_SetRenderTarget(renderer.get(), spell_target));
    REQUIRE(effects.restore_ui(ordinary_texture, {0, 0, 672, 432}));
    const auto black = read(spell_target);
    CHECK(std::all_of(black.begin(), black.end(), [](uint32_t p) { return (p & 0xFFFFFF) == 0; }));
    colors.assign(colors.size(), 0x333333);
    SDL_DestroyTexture(ordinary_texture);
    params.gb_weight = 1;
    REQUIRE(effects.upload(colors, colors, mask.data(), params));
    const auto spell_gb = read(effects.render());
    CHECK(
        std::all_of(spell_gb.begin(), spell_gb.end(), [](uint32_t p) { return (p & 0xFFFFFF) == 0x333333; }));
    // Ice's hidden portrait uses the real background; effect art and neighbors still restore exactly.
    params.gb_weight = params.light_strength = 0;
    params.background.assign(colors.size(), 0x111111);
    mask[60 * 224 + 100] = 0;        // no visible enemy ownership while engulfed
    mask[60 * 224 + 101] = 0x008000; // palette-locked ice fill from the Enhanced source
    params.background[60 * 224 + 101] = 0x22CCFF;
    mask[61 * 224 + 101] = 0x008000; // fill next to protected UI's expanded filter footprint
    params.background[61 * 224 + 101] = 0x88EEFF;
    mask[60 * 224 + 102] = 0x00FF00; // fill also touches a restored enemy
    REQUIRE(effects.upload(colors, colors, mask.data(), params));
    spell_target = effects.render();
    ordinary_texture =
        SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 224, 144);
    REQUIRE(ordinary_texture);
    REQUIRE(SDL_SetTextureScaleMode(ordinary_texture, SDL_SCALEMODE_NEAREST));
    REQUIRE(SDL_UpdateTexture(ordinary_texture, nullptr, colors.data(), 224 * 4));
    REQUIRE(SDL_SetRenderTarget(renderer.get(), spell_target));
    REQUIRE(effects.restore_ui(ordinary_texture, {0, 0, 672, 432}));
    const auto hidden_portrait = read(spell_target);
    CHECK(pixel(hidden_portrait, 100, 60) == 0x111111);
    CHECK(pixel(hidden_portrait, 101, 60) == 0x22CCFF);
    CHECK(pixel(hidden_portrait, 101, 61) == 0x88EEFF);
    CHECK(pixel(hidden_portrait, 116, 60) == 0x333333);
    CHECK(pixel(hidden_portrait, 100, 61) == 0x333333);
    CHECK(pixel(hidden_portrait, 100, 62) == 0x333333);
    SDL_DestroyTexture(ordinary_texture);
    params.grade = params.bloom = 1;
    params.light_strength = 3;
    REQUIRE(effects.upload(colors, colors, mask.data(), params));
    CHECK(pixel(read(effects.render()), 101, 60) == 0x22CCFF); // never grade/bloom/dim filled art
    params.effect_weight = 0;
    REQUIRE(effects.upload(colors, colors, mask.data(), params));
    const auto native_wall = read(effects.render());
    CHECK(std::all_of(native_wall.begin(), native_wall.end(),
                      [](uint32_t p) { return (p & 0xFFFFFF) == 0x333333; }));
    params.effect_weight = params.gb_weight = 1;
    REQUIRE(effects.upload(colors, colors, mask.data(), params));
    const auto gb_wall = read(effects.render());
    CHECK(std::all_of(gb_wall.begin(), gb_wall.end(), [](uint32_t p) { return (p & 0xFFFFFF) == 0x333333; }));
    effects.shutdown();
    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
}

TEST_CASE("light materials inherit mirrors reload atomically and accept future material fields") {
    game::LightTags tags;
    const std::string source = R"({"version":4,"tileset":1,"metatiles":[
      {"metatile":0,"mirror":-1,"light":{"enabled":true,"type":"flame","color":"#FFAD35",
       "radius":26,"intensity":0.18,"offset":[3,6]},"confidence":0.9,"evidence":"flame",
       "future":{"opacity":0.5},"height":12},
      {"metatile":0,"mirror":1,"light":{"enabled":false,"type":"soft_glow","color":"#FFFFCC",
       "radius":20,"intensity":0.2,"offset":[8,8]},"confidence":1,"evidence":"override"}]})";
    std::string error;
    REQUIRE(tags.load(1, source, &error));
    CHECK(tags.find(1, 0, 0)->enabled);
    CHECK_FALSE(tags.find(1, 0, 1)->enabled);
    CHECK(tags.find(1, 0, 3)->offset[0] == 3);
    CHECK(tags.find(1, 1, 0) == nullptr);
    game::LightTags roundtrip;
    REQUIRE(roundtrip.load(1, tags.json(1), &error));
    CHECK(roundtrip.find(1, 0, 2)->color == 0xFFAD35);
    for (const auto &[from, to] :
         {std::pair{"\"radius\":26", "\"radius\":1e999"}, std::pair{"\"mirror\":-1", "\"mirror\":5"},
          std::pair{"#FFAD35", "#GGAD35"}, std::pair{"\"type\":\"flame\"", "\"type\":\"torch\""}}) {
        auto bad = source;
        bad.replace(bad.find(from), std::strlen(from), to);
        CHECK_FALSE(tags.load(1, bad, &error));
        CHECK(tags.find(1, 0, 0)->color == 0xFFAD35);
    }
    CHECK_FALSE(tags.load(1, source + "trailing", &error));
}

TEST_CASE("point sources deduplicate latched quadrants reflect offsets and flicker deterministically") {
    game::LightTags tags;
    std::string error;
    REQUIRE(tags.load(1, R"({"version":4,"tileset":1,"metatiles":[
      {"metatile":0,"mirror":-1,"light":{"enabled":true,"type":"flame","color":"#FFAD35",
       "radius":26,"intensity":0.18,"offset":[3,6]},"confidence":1,"evidence":"fixture"}]})",
                      &error));
    auto layers = std::make_unique<emu::FrameLayers>();
    std::array<uint32_t, 224 * 144> mask{};
    for (int mirror = 0; mirror < 4; ++mirror) {
        *layers = {};
        for (int y = 27; y < 43; ++y) {
            layers->valid[size_t(y)] = 1;
            layers->registers[size_t(y)][0x10] = 3; // scroll misalignment is latched, not read from live IO
            layers->registers[size_t(y)][0x11] = 5;
            for (int x = 29; x < 45; ++x) {
                auto &p = layers->planes[emu::FrameLayers::Screen1][size_t(y * 224 + x)];
                const int q = ((x + 3) / 8 % 2) + ((y + 5) / 8 % 2) * 2;
                p.tile = uint16_t(0x200 + (q ^ mirror));
                p.cell = uint16_t(0x1000 + ((y + 5) / 8 * 32 + (x + 3) / 8) * 2);
                p.flags = uint8_t(emu::RasterPixel::Present | (mirror & 1 ? emu::RasterPixel::HFlip : 0) |
                                  (mirror & 2 ? emu::RasterPixel::VFlip : 0));
                p.color = 0xFFF;
            }
        }
        const auto lights = tags.visible(*layers, mask.data(), 1, 100);
        REQUIRE(lights.size() == 1);
        CHECK(lights[0].position[0] == 29 + (mirror & 1 ? 13 : 3));
        CHECK(lights[0].position[1] == 27 + (mirror & 2 ? 10 : 6));
        CHECK(lights[0].emission == tags.visible(*layers, mask.data(), 1, 100)[0].emission);
        CHECK(lights[0].emission[3] != tags.visible(*layers, mask.data(), 1, 116)[0].emission[3]);
        auto shifted = std::make_unique<emu::FrameLayers>(*layers);
        for (int y = 27; y < 43; ++y) {
            shifted->registers[size_t(y)][0x10] = 4;
            for (int x = 28; x < 44; ++x)
                shifted->planes[emu::FrameLayers::Screen1][size_t(y * 224 + x)] =
                    layers->planes[emu::FrameLayers::Screen1][size_t(y * 224 + x + 1)];
            shifted->planes[emu::FrameLayers::Screen1][size_t(y * 224 + 44)] = {};
        }
        const auto moving = tags.visible(*shifted, mask.data(), 1, 100);
        REQUIRE(moving.size() == 1);
        CHECK(moving[0].position[0] == lights[0].position[0] - 1);
        CHECK(moving[0].emission == lights[0].emission); // phase follows the cell, not screen movement
        mask.fill(0xFF0000);
        CHECK(tags.visible(*layers, mask.data(), 1, 100).empty());
        mask.fill(0);
        for (auto &p : layers->planes[emu::FrameLayers::Screen1])
            p.color = 0;
        CHECK(tags.visible(*layers, mask.data(), 1, 100)[0].emission[3] == 0);
    }
}

TEST_CASE("water material replacement is atomic and older material versions are refused") {
    game::LightTags tags;
    std::string error;
    REQUIRE(tags.load(1, water_fixture(), &error));
    REQUIRE(tags.water(1, 0, 3));
    CHECK(tags.water(1, 0, 3)->enabled);
    const auto previous = tags.json(1);
    CHECK_FALSE(tags.load(1, water_fixture(-1, true, "lava"), &error));
    CHECK(tags.json(1) == previous);
    auto bad = water_fixture();
    bad.replace(bad.find("1111110000000000"), 16, "111111000000000x");
    CHECK_FALSE(tags.load(1, bad, &error));
    CHECK(tags.json(1) == previous);
    for (const char *old : {"1", "2", "3"}) {
        CHECK_FALSE(
            tags.load(1, std::string(R"({"version":)") + old + R"(,"tileset":1,"metatiles":[]})", &error));
        CHECK(tags.json(1) == previous);
    }
    CHECK_FALSE(tags.load(1, water_fixture(-1, true, "river"), &error));
    game::LightTags roundtrip;
    REQUIRE(roundtrip.load(1, tags.json(1), &error));
    CHECK(roundtrip.json(1) == tags.json(1));
    auto source = water_fixture();
    auto override = water_fixture(1, false);
    source.insert(source.size() - 2,
                  "," + override.substr(override.find("[{") + 1, override.size() - override.find("[{") - 3));
    REQUIRE(tags.load(1, source, &error));
    CHECK_FALSE(tags.water(1, 0, 1)->enabled);
    CHECK(tags.water(1, 0, 2)->enabled);
    REQUIRE(tags.load(1, R"({"version":4,"tileset":1,"metatiles":[]})", &error));
    CHECK(tags.water(1, 0, 0) == nullptr);
}

TEST_CASE("water masks follow mirrored quadrants scrolling and foreground ownership") {
    game::LightTags tags;
    std::string error;
    REQUIRE(tags.load(1, water_fixture(), &error));
    auto layers = std::make_unique<emu::FrameLayers>();
    std::array<uint32_t, 224 * 144> mask{};
    for (int mirror = 0; mirror < 4; ++mirror) {
        *layers = {};
        for (int y = 27; y < 43; ++y) {
            layers->valid[size_t(y)] = 1;
            layers->registers[size_t(y)][0x10] = 3;
            layers->registers[size_t(y)][0x11] = 5;
            for (int x = 29; x < 45; ++x) {
                auto &p = layers->planes[emu::FrameLayers::Screen1][size_t(y * 224 + x)];
                const int q = (x - 29) / 8 + (y - 27) / 8 * 2;
                p.tile = uint16_t(0x200 + (q ^ mirror));
                p.flags = uint8_t(emu::RasterPixel::Present | (mirror & 1 ? emu::RasterPixel::HFlip : 0) |
                                  (mirror & 2 ? emu::RasterPixel::VFlip : 0));
            }
        }
        const auto water = tags.water_pixels(*layers, mask.data(), 1);
        int wet = 0, wrong = 0;
        for (int y = 27; y < 43; ++y)
            for (int x = 29; x < 45; ++x) {
                const int tx = mirror & 1 ? 44 - x : x - 29, ty = mirror & 2 ? 42 - y : y - 27;
                const uint32_t pixel = water[size_t(y * 224 + x)];
                wet += pixel != 0;
                wrong += (pixel != 0) != (tx < 6 && ty >= 4);
                CHECK(tags.water_at(*layers, x, y, 1) == (tx < 6 && ty >= 4));
                if (pixel) {
                    CHECK((pixel >> 16) == unsigned(x + 3));
                    CHECK((pixel >> 8 & 255) == unsigned(y + 5));
                }
            }
        CHECK(wet == 72);
        CHECK(wrong == 0);
        if (mirror == 0) {
            CHECK((water[size_t(31 * 224 + 29)] & 15) == 1);
            for (int y = 27; y < 31; ++y)
                for (int x = 29; x < 35; ++x)
                    layers->planes[emu::FrameLayers::Screen1][size_t(y * 224 + x)].color = 0xFFF;
            const auto foam = tags.water_pixels(*layers, mask.data(), 1);
            CHECK((foam[size_t(31 * 224 + 29)] & 15) == 5); // foam isn't a false shore
            CHECK(foam[size_t(30 * 224 + 29)] == 0);        // its original art remains unpainted
            mask[size_t(32 * 224 + 29)] = 0x004000;
            const auto foreground = tags.water_pixels(*layers, mask.data(), 1);
            CHECK((foreground[size_t(33 * 224 + 29)] & 15) == 7);
            CHECK(foreground[size_t(32 * 224 + 29)] == 0);
        }

        mask.fill(0xFF0000);
        const auto ui = tags.water_pixels(*layers, mask.data(), 1);
        CHECK(std::all_of(ui.begin(), ui.end(), [](uint32_t p) { return p == 0; }));
        mask.fill(0x004000);
        CHECK(tags.water_at(*layers, mirror & 1 ? 44 : 29, mirror & 2 ? 27 : 42, 1));
        const auto actors = tags.water_pixels(*layers, mask.data(), 1);
        CHECK(std::all_of(actors.begin(), actors.end(), [](uint32_t p) { return p == 0; }));
        mask.fill(0);
    }
}

TEST_CASE("shared-corner fixtures merge without brightness summation and sample live colors") {
    game::LightTags tags;
    std::string error;
    REQUIRE(tags.load(3,
                      "{\"version\":4,\"tileset\":3,\"metatiles\":[" + light_record(0, "soft_glow", 3) + "," +
                          light_record(1, "soft_glow", 1) + "," + light_record(2, "soft_glow", 9) + "," +
                          light_record(3, "soft_glow", 7) + "," + light_record(4, "pulse", 5) + "]}",
                      &error));
    auto layers = std::make_unique<emu::FrameLayers>();
    std::array<uint32_t, 224 * 144> mask{};
    light_tile(*layers, 0, 32, 32, 0xF20);
    light_tile(*layers, 1, 48, 32, 0xF20);
    light_tile(*layers, 2, 32, 48, 0xF20);
    light_tile(*layers, 3, 48, 48, 0xF20);
    const auto lights = tags.visible(*layers, mask.data(), 3, 0);
    REQUIRE(lights.size() == 1);
    CHECK(lights[0].position[0] == 48);
    CHECK(lights[0].position[1] == 48);
    CHECK(lights[0].beam[3] == 4);
    CHECK(lights[0].emission[3] == doctest::Approx(0.18));
    CHECK(lights[0].emission[0] == 1);
    CHECK(lights[0].emission[2] == 0);
    // A live palette cycle changes color without changing tags or brightness.
    for (auto &p : layers->planes[emu::FrameLayers::Screen1])
        p.color = 0x02F;
    const auto blue = tags.visible(*layers, mask.data(), 3, 0);
    REQUIRE(blue.size() == 1);
    CHECK(blue[0].emission[0] == 0);
    CHECK(blue[0].emission[2] == 1);
    CHECK(blue[0].emission[3] == lights[0].emission[3]);
    // Foreground UI cannot seed a hue, and different types never merge.
    light_tile(*layers, 4, 64, 32, 0xFFF);
    CHECK(tags.visible(*layers, mask.data(), 3, 0).size() == 2);
    mask.fill(0xFF0000);
    CHECK(tags.visible(*layers, mask.data(), 3, 0).empty());
}

TEST_CASE("beam anchors and directions mirror together and material validation is atomic") {
    game::LightTags tags;
    std::string error;
    const auto source = "{\"version\":4,\"tileset\":1,\"metatiles\":[" + light_record(0, "beam", 9, 3) + "]}";
    REQUIRE(tags.load(1, source, &error));
    auto layers = std::make_unique<emu::FrameLayers>();
    std::array<uint32_t, 224 * 144> mask{};
    for (int mirror = 0; mirror < 4; ++mirror) {
        *layers = {};
        light_tile(*layers, 0, 32, 32, 0xFFF, mirror);
        const auto l = tags.visible(*layers, mask.data(), 1, 0);
        REQUIRE(l.size() == 1);
        CHECK(l[0].position[0] == (mirror & 1 ? 32 : 48));
        CHECK(l[0].position[1] == (mirror & 2 ? 48 : 32));
        CHECK(l[0].beam[0] == doctest::Approx(mirror & 1 ? -0.70710678 : 0.70710678));
        CHECK(l[0].beam[1] == doctest::Approx(mirror & 2 ? -0.70710678 : 0.70710678));
    }
    for (const auto &[a, b] :
         {std::pair{"\"anchor\":9", "\"anchor\":0"}, std::pair{"\"direction\":3", "\"direction\":5"}}) {
        auto bad = source;
        bad.replace(bad.find(a), std::strlen(a), b);
        CHECK_FALSE(tags.load(1, bad, &error));
        CHECK(tags.find(1, 0, 0)->direction == 3);
    }
    // Same-type shared anchors merge even if component tiles have different Beam directions.
    REQUIRE(tags.load(1,
                      "{\"version\":4,\"tileset\":1,\"metatiles\":[" + light_record(0, "beam", 6, 6) + "," +
                          light_record(1, "beam", 4, 4) + "]}",
                      &error));
    *layers = {};
    light_tile(*layers, 0, 32, 32, 0xFFF);
    light_tile(*layers, 1, 48, 32, 0x888);
    const auto merged = tags.visible(*layers, mask.data(), 1, 0);
    REQUIRE(merged.size() == 1);
    CHECK(merged[0].beam[3] == 2);
    CHECK(merged[0].beam[0] == 1); // brighter tile's direction survives
    CHECK(merged[0].position[0] == 48);
    REQUIRE(tags.load(1, source, &error));
    // A file in an older version never replaces the loaded tags.
    CHECK_FALSE(tags.load(1, "{\"version\":2,\"tileset\":1,\"metatiles\":[]}", &error));
    REQUIRE(tags.find(1, 0, 0));
    CHECK(tags.find(1, 0, 0)->type == "beam");
}

TEST_CASE("Flame and Pulse clocks synchronize across cells while steady lights retain exposure") {
    auto layers = std::make_unique<emu::FrameLayers>();
    std::array<uint32_t, 224 * 144> mask{};
    light_tile(*layers, 0, 32, 32, 0xFFF);
    light_tile(*layers, 0, 96, 64, 0x888);
    for (const char *type : {"flame", "pulse", "soft_glow", "harsh_electric"}) {
        game::LightTags tags;
        std::string error;
        REQUIRE(tags.load(1, "{\"version\":4,\"tileset\":1,\"metatiles\":[" + light_record(0, type, 5) + "]}",
                          &error));
        const auto baseline = tags.visible(*layers, mask.data(), 1, 0);
        REQUIRE(baseline.size() == 2);
        float minimum = 1, maximum = 0;
        for (uint64_t frame = 0; frame < 180; ++frame) {
            const auto now = tags.visible(*layers, mask.data(), 1, frame);
            REQUIRE(now.size() == 2);
            const float a = now[0].emission[3] / baseline[0].emission[3];
            const float b = now[1].emission[3] / baseline[1].emission[3];
            CHECK(a == doctest::Approx(b));
            if (std::string(type) == "soft_glow" || std::string(type) == "harsh_electric")
                CHECK(a == 1);
            minimum = std::min(minimum, a);
            maximum = std::max(maximum, a);
        }
        if (std::string(type) == "pulse") {
            CHECK(minimum == doctest::Approx(0.75));
            CHECK(maximum == doctest::Approx(1));
        }
        if (std::string(type) == "flame")
            CHECK(minimum < maximum);
    }
}

TEST_CASE("Pulse uses saturated center gems and touching signs share highlights across lettering") {
    game::LightTags tags;
    std::string error;
    REQUIRE(tags.load(1,
                      "{\"version\":4,\"tileset\":1,\"metatiles\":[" + light_record(0, "pulse", 5) + "," +
                          light_record(1, "soft_glow", 5) + "," + light_record(2, "soft_glow", 5) + "]}",
                      &error));
    auto layers = std::make_unique<emu::FrameLayers>();
    std::array<uint32_t, 224 * 144> mask{};
    light_tile(*layers, 0, 32, 32, 0xFFF);
    for (int y = 38; y < 42; ++y)
        for (int x = 38; x < 42; ++x)
            layers->planes[emu::FrameLayers::Screen1][size_t(y * 224 + x)].color = 0x02C;
    auto lights = tags.visible(*layers, mask.data(), 1, 0);
    REQUIRE(lights.size() == 1);
    CHECK(lights[0].emission[0] == 0);
    CHECK(lights[0].emission[1] == doctest::Approx(2.0 / 12));
    CHECK(lights[0].emission[2] == 1);
    // The sign's center tile is mostly dark lettering, but has the same emitter.
    light_tile(*layers, 1, 80, 32, 0x888);
    light_tile(*layers, 2, 96, 32, 0x111);
    light_tile(*layers, 1, 112, 32, 0x888);
    for (int x = 80; x < 128; ++x)
        layers->planes[emu::FrameLayers::Screen1][size_t(40 * 224 + x)].color = 0x2FC;
    lights = tags.visible(*layers, mask.data(), 1, 0);
    REQUIRE(lights.size() == 2);
    const auto sign =
        std::find_if(lights.begin(), lights.end(), [](const PointLight &l) { return l.type == 1; });
    REQUIRE(sign != lights.end());
    CHECK(sign->position[0] == 104);
    CHECK(sign->extent[0] == 24); // full run, including end tiles
    CHECK(sign->emission[0] == doctest::Approx(2.0 / 15));
    CHECK(sign->emission[1] == 1);
    CHECK(sign->emission[2] == doctest::Approx(12.0 / 15));
    CHECK(sign->emission[3] == doctest::Approx(0.18));
    // A file in an older version never replaces the loaded tags.
    CHECK_FALSE(tags.load(1, "{\"version\":3,\"tileset\":1,\"metatiles\":[]}", &error));
    CHECK(tags.find(1, 0, 0) != nullptr);
}

TEST_CASE("neon strips remain uniform when their segments use shared-corner anchors") {
    game::LightTags tags;
    std::string error;
    REQUIRE(tags.load(1,
                      "{\"version\":4,\"tileset\":1,\"metatiles\":[" + light_record(0, "soft_glow", 2) + "," +
                          light_record(1, "soft_glow", 8) + "," + light_record(2, "soft_glow", 8) + "]}",
                      &error));
    auto layers = std::make_unique<emu::FrameLayers>();
    std::array<uint32_t, 224 * 144> mask{};
    light_tile(*layers, 0, 32, 32, 0x2FC);
    light_tile(*layers, 1, 32, 48, 0x111);
    light_tile(*layers, 2, 32, 64, 0x2FC);
    layers->planes[emu::FrameLayers::Screen1][size_t(56 * 224 + 40)].color = 0x2FC;
    const auto lights = tags.visible(*layers, mask.data(), 1, 0);
    REQUIRE(lights.size() == 1);
    CHECK(lights[0].position[0] == 40);
    CHECK(lights[0].position[1] == 56);
    CHECK(lights[0].extent[0] == 0);
    CHECK(lights[0].extent[1] == 24); // all three full tiles, including the endpoints
    CHECK(lights[0].beam[3] == 3);
}

TEST_CASE("GPU battle glow has bounded native-filter support and preserves sparse patches and UI [GPU]") {
    if (!std::getenv("SAWGOD_GPU_TESTS")) {
        MESSAGE("set SAWGOD_GPU_TESTS with a GPU-capable SDL driver for measured glow reach");
        return;
    }
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    auto *window = SDL_CreateWindow("Battle glow reach", 672, 432, SDL_WINDOW_HIDDEN);
    REQUIRE(window);
    PresentationRenderer renderer;
    REQUIRE(renderer.init(window, false, false));
    EnhancedGraphics effects;
    REQUIRE(effects.init(renderer.get()));
    std::vector<uint32_t> colors(224 * 144, 0x333333), mask(colors.size(), 0);
    const auto read = [&] {
        auto *texture = effects.render();
        REQUIRE(texture);
        REQUIRE(SDL_SetRenderTarget(renderer.get(), texture));
        auto *surface = SDL_RenderReadPixels(renderer.get(), nullptr);
        REQUIRE(surface);
        auto *rgb = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_XRGB8888);
        REQUIRE(rgb);
        std::vector<uint32_t> result(size_t(rgb->w) * rgb->h);
        for (int y = 0; y < rgb->h; ++y)
            std::memcpy(result.data() + y * rgb->w, static_cast<uint8_t *>(rgb->pixels) + y * rgb->pitch,
                        size_t(rgb->w) * 4);
        SDL_DestroySurface(rgb);
        SDL_DestroySurface(surface);
        REQUIRE(SDL_SetRenderTarget(renderer.get(), nullptr));
        return result;
    };
    EnhancedParameters params;
    params.grade = params.bloom = params.water_strength = 0;
    params.shadows = false;
    params.snap = true;
    params.light_strength = 1;
    REQUIRE(effects.upload(colors, colors, mask.data(), params));
    const auto baseline = read();
    // Sparse occupied pixels: their enclosing rectangle must not emit light. Full patches also keep
    // their exact support, including all-ones bit patterns.
    // Exercise bit 31 in both mask words, including the float bit patterns uploaded to the GPU.
    for (bool snap : {true, false}) {
        CAPTURE(snap);
        params.snap = snap;
        for (int radius : {4, 8}) {
            CAPTURE(radius);
            for (bool dense : {false, true}) {
                CAPTURE(dense);
                for (int type : {6, 7}) {
                    CAPTURE(type);
                    PointLight light;
                    light.position = {100, 44, float(radius), 0};
                    light.extent = {3.5f, 3.5f};
                    light.source_pixels =
                        dense ? std::array<uint32_t, 2>{0xFFFFFFFF, 0xFFFFFFFF}
                              : std::array<uint32_t, 2>{1 | (uint32_t(1) << 31), uint32_t(1) << 31};
                    // Source centers: (96.5,40.5), (103.5,43.5), (103.5,47.5).
                    light.type = type;
                    light.emission =
                        type == 6 ? std::array<float, 4>{1, 1, 1, 1} : std::array<float, 4>{0, 0, 0, 1};
                    params.lights = {light};
                    REQUIRE(effects.upload(colors, colors, mask.data(), params));
                    const auto glow = read();
                    int changed = 0, outside = 0, inner_missing = 0;
                    for (int yy = 28 * 3; yy < 60 * 3; ++yy)
                        for (int xx = 84 * 3; xx < 116 * 3; ++xx) {
                            const float x = snap ? float(xx / 3) + 0.5f : (float(xx) + 0.5f) / 3;
                            const float y = snap ? float(yy / 3) + 0.5f : (float(yy) + 0.5f) / 3;
                            float distance = 1000;
                            // Independent oracle: enumerate covered native pixel centers, never rectangles.
                            for (int py = 0; py < 8; ++py)
                                for (int px = 0; px < 8; ++px) {
                                    const int bit = py * 8 + px;
                                    if (light.source_pixels[size_t(bit / 32)] & (uint32_t(1) << (bit % 32)))
                                        distance = std::min(distance,
                                                            std::hypot(x - (96.5f + px), y - (40.5f + py)));
                                }
                            const size_t at = size_t(yy * 672 + xx);
                            const bool lit = (glow[at] & 0xFFFFFF) != (baseline[at] & 0xFFFFFF);
                            changed += lit;
                            outside += lit && distance >= radius + (snap ? 0 : 1);
                            inner_missing += !lit && distance <= radius * 0.5f;
                        }
                    CHECK(changed > 0);
                    CHECK(outside == 0);
                    CHECK(inner_missing == 0);
                    // GB blending must bypass both additive light and subtractive gloom completely.
                    params.gb_weight = 1;
                    REQUIRE(effects.upload(colors, colors, mask.data(), params));
                    CHECK(read() == baseline);
                    params.gb_weight = 0;
                }
            }
        }
    }
    // Max-combining duplicates keeps the picture and scene-fragment work fixed as source count rises.
    // Include every actor slot and fully occupied patches, whose exact rounded rectangle replaces 64 discs.
    params.snap = true;
    params.shadows = true;
    params.actors.assign(20, {180, 120, 6, 2});
    PointLight peak;
    peak.type = 6;
    peak.position = {100, 44, 8, 0};
    peak.extent = {3.5f, 3.5f};
    peak.source_pixels = {0xFFFFFFFF, 0xFFFFFFFF};
    peak.emission = {1, 0.5f, 0.25f, 0.2f};
    std::vector<uint32_t> one;
    uint64_t scene_pixels = 0;
    for (size_t count : {size_t(1), size_t(20), size_t(64), size_t(256)}) {
        CAPTURE(count);
        params.lights.assign(count, peak);
        REQUIRE(effects.upload(colors, colors, mask.data(), params));
        const auto image = read();
        if (count == 1) {
            one = image;
            scene_pixels = effects.last_work.shaded_pixels;
        } else
            CHECK(image == one);
        CHECK(effects.last_work.shaded_pixels == scene_pixels);
        CHECK(effects.last_work.passes == 1); // no bloom requested
        CHECK(effects.influence_work.quads == count * 2 + 20);
        CHECK(effects.influence_work.clear_pixels <= 4 * 672u * 432u);
        CHECK(EnhancedGraphics::composite_influence_samples <= 4);
        CHECK(EnhancedGraphics::restore_influence_samples <= 2);
    }
    params.actors.clear();
    params.shadows = false;
    params.snap = true;
    // Reviewed Beam defaults were dropped, but all three explicit mod styles still retain their
    // analytic shaft/dust falloff. Check a grayscale fixture against an independent scalar oracle.
    const auto smooth = [](double a, double b, double v) {
        const double t = std::clamp((v - a) / (b - a), 0.0, 1.0);
        return t * t * (3 - 2 * t);
    };
    PointLight beam;
    beam.type = 4;
    beam.position = {100, 44, 32, 0};
    beam.beam = {0, -1, 1, 1};
    beam.emission = {1, 1, 1, 0.5f};
    params.lights = {beam};
    for (int style : {0, 1, 2})
        for (uint64_t phase : {uint64_t(0), uint64_t(256), uint64_t(960)}) {
            CAPTURE(style);
            CAPTURE(phase);
            params.beam_style = style;
            params.frame = phase;
            REQUIRE(effects.upload(colors, colors, mask.data(), params));
            const auto shaft = read();
            for (int y = 14; y < 57; y += 3)
                for (int x = 88; x < 113; x += 2) {
                    const double along = 44 - (y + 0.5), across = x + 0.5 - 100;
                    const double width = style == 1 ? 5 : 3 + std::max(along, 0.0) * 0.18;
                    const double fade = 1 - smooth(0, 32, along);
                    double falloff = along >= 0 ? fade * std::exp(-across * across / (width * width) * 2) *
                                                      (1 - smooth(width, width * 1.8, std::abs(across)))
                                                : 0;
                    if (style == 2)
                        for (int mote = 0; mote < 3; ++mote) {
                            const double z = double(phase) / 960 + double(mote) / 3;
                            const double f = z - std::floor(z), a = (0.1 + f * 0.75) * 32, w = 3 + a * 0.18;
                            const double sideways = std::sin(f * 6.2831853 + mote * 2.1) * w * 0.55;
                            const double dx = along - a, dy = across - sideways;
                            if (along >= 0)
                                falloff += fade * std::exp(-(dx * dx + dy * dy) / 0.7) *
                                           std::sin(f * 3.1415927) * 0.7;
                        }
                    if (std::hypot(along, across) >= 32)
                        falloff = 0;
                    // LCD gray = raw*16/17; inverse gray gain = 17/16. Scene exposure = 51/191.25.
                    const int expected =
                        int(std::lround(51 + (1 - 0.2 * 16 / 17) * (0.5 * 51 / 191.25) * 255 * falloff));
                    const uint32_t actual = shaft[size_t((y * 3 + 1) * 672 + x * 3 + 1)];
                    for (unsigned shift : {0u, 8u, 16u})
                        CHECK(std::abs(int(actual >> shift & 255) - expected) <=
                              4); // native stamp/phase interpolation guard
                }
        }
    params.frame = 0;
    params.beam_style = 0;
    // A delivery patch illuminates a visible monster as it approaches, then stops when it passes.
    auto *ordinary =
        SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 224, 144);
    REQUIRE(ordinary);
    REQUIRE(SDL_UpdateTexture(ordinary, nullptr, colors.data(), 224 * 4));
    REQUIRE(SDL_SetTextureScaleMode(ordinary, SDL_SCALEMODE_NEAREST));
    mask[44 * 224 + 100] = 0x00FF00;
    const auto monster = [&](int source_x) {
        PointLight light;
        light.position = {float(source_x) + 0.5f, 40.5f, 8, 0};
        light.source_pixels = {1, 0};
        light.type = 6;
        light.emission = {1, 1, 1, 1};
        params.lights = {light};
        REQUIRE(effects.upload(colors, colors, mask.data(), params));
        auto *texture = effects.render();
        REQUIRE(texture);
        REQUIRE(SDL_SetRenderTarget(renderer.get(), texture));
        REQUIRE(effects.restore_ui(ordinary, {0, 0, 672, 432}));
        auto *surface = SDL_RenderReadPixels(renderer.get(), nullptr);
        REQUIRE(surface);
        auto *rgb = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_XRGB8888);
        REQUIRE(rgb);
        const auto *row =
            reinterpret_cast<const uint32_t *>(static_cast<uint8_t *>(rgb->pixels) + 133 * rgb->pitch);
        const uint32_t pixel = row[301] & 0xFFFFFF;
        SDL_DestroySurface(rgb);
        SDL_DestroySurface(surface);
        REQUIRE(SDL_SetRenderTarget(renderer.get(), nullptr));
        return pixel;
    };
    CHECK(monster(96) != 0x333333);
    CHECK(monster(112) == 0x333333);
    SDL_DestroyTexture(ordinary);
    effects.shutdown();
    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
}

#ifdef SAWGOD_GPU_TIMESTAMPS
TEST_CASE("GPU timestamps are delayed nonblocking and associated with the submitted frame [GPU]") {
    if (!std::getenv("SAWGOD_GPU_TESTS"))
        return;
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    auto *window = SDL_CreateWindow("Timestamp test", 448, 288, SDL_WINDOW_HIDDEN);
    REQUIRE(window);
    PresentationRenderer renderer;
    REQUIRE(renderer.init(window, false, false));
    if (std::string(SDL_GetGPUDeviceDriver(SDL_GetGPURendererDevice(renderer.get()))) == "vulkan") {
        Uint64 frame = 0;
        double ms = -1;
        REQUIRE(SawGodRendererTimingStart(renderer.get(), 17));
        REQUIRE(SawGodRendererTimingStart(renderer.get(), 17)); // same recording is not double counted
        CHECK_FALSE(SawGodRendererTimingPoll(renderer.get(), &frame, &ms));
        SDL_SetRenderDrawColor(renderer.get(), 100, 60, 30, 255);
        REQUIRE(SDL_RenderClear(renderer.get()));
        const SDL_Rect pixel{0, 0, 1, 1};
        auto *readback = SDL_RenderReadPixels(renderer.get(), &pixel); // test-only fence wait
        REQUIRE(readback);
        REQUIRE(SawGodRendererTimingPoll(renderer.get(), &frame, &ms));
        CHECK(frame == 17);
        CHECK(std::isfinite(ms));
        CHECK(ms >= 0);
        CHECK_FALSE(SawGodRendererTimingPoll(renderer.get(), &frame, &ms));
        SDL_DestroySurface(readback);
    }
    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
}
#endif

TEST_CASE("GPU patch falloff fields preserve shape without occupied-pixel overdraw [GPU]") {
    if (!std::getenv("SAWGOD_GPU_TESTS"))
        return;
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    auto *window = SDL_CreateWindow("Patch distance field", 672, 432, SDL_WINDOW_HIDDEN);
    REQUIRE(window);
    PresentationRenderer renderer;
    REQUIRE(renderer.init(window, false, false));
    InfluenceBuffer field;
    REQUIRE(field.init(renderer.get()));
    PointLight light;
    light.type = 7;
    light.position = {100, 44, 8, 0};
    light.extent = {3.5f, 3.5f};
    light.emission = {0, 0, 0, 1};
    const std::array<std::array<uint32_t, 2>, 5> masks{{{1, 0},
                                                        {1 | (uint32_t(1) << 31), uint32_t(1) << 31},
                                                        {0xA5A55A5A, 0x55AA55AA},
                                                        {0xFFFFFFFF, 0xFFFFFFFE},
                                                        {0xFFFFFFFF, 0xFFFFFFFF}}};
    for (bool snap : {false, true})
        for (int radius : {4, 8}) {
            uint64_t sparse_pixels = 0;
            for (const auto &mask : masks) {
                CAPTURE(snap);
                CAPTURE(radius);
                CAPTURE(mask[0]);
                CAPTURE(mask[1]);
                light.position[2] = float(radius);
                light.source_pixels = mask;
                InfluenceBuffer::Parameters params;
                params.options[3] = float(snap);
                field.work = {};
                REQUIRE(field.begin(2));
                REQUIRE(field.light(light, params, 2));
                REQUIRE(field.end());
                CHECK(field.work.quads == 1);
                CHECK(field.work.patch_allocations == 0);
                // Occupancy from 1 to 64 pixels cannot increase submitted fragments per patch.
                if (!sparse_pixels)
                    sparse_pixels = field.work.pixels;
                CHECK(field.work.pixels == sparse_pixels);
                REQUIRE(SDL_SetRenderTarget(renderer.get(), field.texture(2)));
                const SDL_Rect region{84 * 3, 28 * 3, 36 * 3, 36 * 3};
                auto *surface = SDL_RenderReadPixels(renderer.get(), &region);
                REQUIRE(surface);
                std::vector<std::array<float, 4>> pixels(size_t(region.w) * region.h);
                REQUIRE(SDL_ConvertPixels(region.w, region.h, surface->format, surface->pixels,
                                          surface->pitch, SDL_PIXELFORMAT_RGBA128_FLOAT, pixels.data(),
                                          region.w * 16));
                SDL_DestroySurface(surface);
                REQUIRE(SDL_SetRenderTarget(renderer.get(), nullptr));
                double max_error = 0;
                for (int y = 0; y < region.h; ++y)
                    for (int x = 0; x < region.w; ++x) {
                        const double px = snap ? double((x + region.x) / 3) + 0.5 : (x + region.x + 0.5) / 3;
                        const double py = snap ? double((y + region.y) / 3) + 0.5 : (y + region.y + 0.5) / 3;
                        double distance = 1000;
                        // Independent brute-force scalar oracle, including almost-full masks.
                        for (int bit = 0; bit < 64; ++bit)
                            if (mask[size_t(bit / 32)] & (uint32_t(1) << (bit % 32)))
                                distance = std::min(distance,
                                                    std::hypot(px - (96.5 + bit % 8), py - (40.5 + bit / 8)));
                        const double d = distance / radius;
                        const double t = std::clamp((d - 0.6) / 0.4, 0.0, 1.0);
                        const double expected = std::exp(-d * d * 3) * (1 - t * t * (3 - 2 * t));
                        max_error =
                            std::max(max_error, std::abs(pixels[size_t(y * region.w + x)][3] - expected));
                    }
                CHECK(max_error <= 0.001); // float16 field/texture rounding, below one RGB code value
            }
        }
    field.shutdown();
    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
}

TEST_CASE("Screen Mode LCD: Enhanced's wash meets the exact response at black and white") {
    const sawgod::FstnWash w = sawgod::fstn_wash();
    std::vector<uint32_t> px = {0x000000, 0xFFFFFF};
    sawgod::fstn_color(px);
    for (int i = 0; i < 3; ++i) {
        const int shift = 16 - 8 * i;
        const float black = w.offset[size_t(i)] * 255,
                    white = (w.scale[size_t(i)] + w.offset[size_t(i)]) * 255;
        CHECK(std::abs(black - float(px[0] >> shift & 255)) <= 1);
        CHECK(std::abs(white - float(px[1] >> shift & 255)) <= 1);
    }
    // an IPS pixel's stripes keep white bright: the across mask averages >= 90% per
    // channel
    const std::vector<uint32_t> across = sawgod::ips_mask(224 * 6, 224, true);
    for (int c = 0; c < 3; ++c) {
        double sum = 0;
        for (int j = 0; j < 6; ++j)
            sum += double(across[size_t(j)] >> (16 - 8 * c) & 255);
        CHECK(sum / 6 / 255 >= 0.9);
    }
}

TEST_CASE("Screen IPS strength blends both masks toward white at every scale") {
    for (int width : {224, 448, 672, 784, 1344})
        for (bool across : {false, true}) {
            const auto full = sawgod::ips_mask(width, 224, across);
            for (float amount : {0.f, 0.5f, 0.75f, 1.f}) {
                const auto faded = sawgod::ips_mask(width, 224, across, amount);
                for (size_t i = 0; i < full.size(); ++i)
                    for (int shift : {0, 8, 16}) {
                        const float expected = 255 - amount * (255 - float(full[i] >> shift & 255));
                        CHECK(std::abs(float(faded[i] >> shift & 255) - expected) <= 1);
                    }
            }
        }
}

TEST_CASE("native battle wipe ownership follows the displayed raster handler") {
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    display(w, 1);
    w.io[7] = 2;
    w.ram[0xD0DC] = 1;
    w.rom[0x35A830 + 18 + 1] = 0xE0;
    std::fill_n(w.ram.begin() + 0xD52D, 9, 0);
    w.ram[0xD52D] = 0xE0;
    w.ram[0xD0D6] = 0;
    w.capture_layers(true);
    w.run_frame();
    game::SceneObserver observer;
    observer.install(w);
    game::Machine m(w);
    game::hooks::emit(game::hooks::Event::PresentationField, m, {1});
    CHECK(observer.describe(w, false, false, false).kind == game::PresentationScene::Kind::Field);
    auto &session = game::Session::of(w);
    session.apply_scene_settings(w);
    w.ram[0x90] = 0x10;
    w.ram[0x91] = 0x13;
    w.ram[0x92] = 0;
    w.ram[0x93] = 0x50;
    CHECK_FALSE(game::presented_battle_entry_wipe(w));
    w.io[0xB2] |= 0x10;
    CHECK(game::battle_entry_wipe(w));
    // Arming the next raster cannot drop the camera over the still-visible field.
    const auto ram = w.ram;
    const auto io = w.io;
    CHECK_FALSE(game::presented_battle_entry_wipe(w));
    CHECK(observer.describe(w, false, false, false).kind == game::PresentationScene::Kind::Field);
    CHECK(w.ram == ram);
    CHECK(w.io == io);
    session.apply_scene_settings(w);
    CHECK(game::presented_battle_entry_wipe(w));
    const auto scene = observer.describe(w, false, false, false);
    CHECK(scene.kind == game::PresentationScene::Kind::Transition);
    CHECK(scene.transition == game::PresentationScene::Transition::BattleWipe);
    w.ram[0x91] ^= 1;
    CHECK_FALSE(game::battle_entry_wipe(w));
    // Disarming the next raster likewise cannot release the displayed wipe early.
    CHECK(game::presented_battle_entry_wipe(w));
    CHECK(observer.describe(w, false, false, false).transition ==
          game::PresentationScene::Transition::BattleWipe);
    session.apply_scene_settings(w);
    CHECK_FALSE(game::presented_battle_entry_wipe(w));
}

#include "game/battle_ui.h"
TEST_CASE("battle equipment windows retain the top backdrop in both pictures and Enhanced effects") {
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    w.cpu.s.halted = true;
    w.cpu.s.flags = 0;
    chrome_fixture::install(w);
    w.io[0x60] = 0xE0;
    w.io[0] = 1;
    w.io[7] = 2;
    w.ram[0xFE02] = 0x23;
    w.ram[0xFE03] = 0x01;
    std::fill_n(w.ram.begin() + 0x4000 + 32, 32, 0x11);
    for (int cell = 0; cell < 32 * 32; ++cell)
        w.ram[size_t(0x1000 + cell * 2)] = 1;
    w.set_second(true);
    for (unsigned at = 0; at < 65536; ++at)
        w.shadow_write8(uint16_t(at), w.ram[at]);
    w.capture_layers(true);
    game::BattleUi ui;
    ui.install(w);
    game::SceneObserver observer;
    observer.install(w);
    game::Machine m(w);
    game::hooks::emit(game::hooks::Event::BattleStart, m);
    game::hooks::emit(game::hooks::Event::BattleMenu, m, {0});
    w.run_frame();
    w.run_frame();
    observer.describe(w, true, false, true);
    std::array<std::vector<uint32_t>, 2> initial;
    for (bool second : {false, true})
        initial[size_t(second)] = ui.picture(w, emu::Wsc::ColorMode::Raw, second);
    // Native equipment/target selectors draw an opaque SCR1 window in the top two rows.
    w.ram[0xD6E2] = 1;
    for (int cell = 0; cell < 32 * 2; ++cell) {
        const auto at = uint16_t(0x1000 + cell * 2);
        w.ram[at] = 0xD5;
        w.ram[at + 1] = 0x1F;
        w.shadow_write8(at, 0xD5);
        w.shadow_write8(uint16_t(at + 1), 0x1F);
    }
    w.run_frame();
    const auto ram = w.ram;
    const auto io = w.io;
    for (bool second : {false, true}) {
        const auto px = ui.picture(w, emu::Wsc::ColorMode::Raw, second);
        CHECK(std::equal(px.begin(), px.begin() + 224 * 16, initial[size_t(second)].begin()));
    }
    const auto scene = observer.describe(w, true, false, true);
    REQUIRE(scene.background.size() == 224 * 144);
    for (size_t at = 0; at < 224 * 16; ++at) {
        CHECK(scene.background[at] == 0x112233);
        CHECK(scene.mask[at] == 0);
    }
    CHECK(w.ram == ram);
    CHECK(w.io == io);
    // A new battle forgets the old backdrop: cells only a window has covered since show nothing beneath.
    game::hooks::emit(game::hooks::Event::BattleStart, m);
    w.run_frame();
    CHECK(observer.describe(w, true, false, true).background[0] != 0x112233);
}

TEST_CASE("battle top rows never replay art the game has since cleared") {
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    w.cpu.s.halted = true;
    w.cpu.s.flags = 0;
    chrome_fixture::install(w);
    w.io[0x60] = 0xE0;
    w.io[0] = 1;
    w.io[7] = 2;
    w.ram[0xFE02] = 0x23; // palette 0 color 1: the backdrop
    w.ram[0xFE03] = 0x01;
    w.ram[0xFE82] = 0x56; // palette 4 color 1: art drawn on SCR1 (an enemy breaking apart)
    w.ram[0xFE83] = 0x04;
    std::fill_n(w.ram.begin() + 0x4000 + 32, 32, 0x11);
    w.capture_layers(true);
    game::BattleUi ui;
    ui.install(w);
    game::SceneObserver observer;
    observer.install(w);
    game::Machine m(w);
    game::hooks::emit(game::hooks::Event::BattleStart, m);
    game::hooks::emit(game::hooks::Event::BattleMenu, m, {0});
    const auto put = [&](uint16_t entry) {
        for (int cell = 0; cell < 32 * 2; ++cell) {
            w.ram[size_t(0x1000 + cell * 2)] = uint8_t(entry);
            w.ram[size_t(0x1000 + cell * 2 + 1)] = uint8_t(entry >> 8);
        }
    };
    put(0x0801); // tile 1, palette 4
    w.run_frame();
    w.run_frame();
    CHECK(observer.describe(w, true, false, true).background[0] == 0x445566);
    put(0x0000); // the game clears those cells: blank tile, palette 0 (transparent)
    w.run_frame();
    w.run_frame();
    const auto scene = observer.describe(w, true, false, true);
    for (size_t at = 0; at < 224 * 16; ++at)
        CHECK(scene.background[at] != 0x445566);
}

TEST_CASE("GPU dialogue restoration keeps the adjacent scene row Enhanced [GPU]") {
    if (!std::getenv("SAWGOD_GPU_TESTS"))
        return;
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    auto *window = SDL_CreateWindow("Dialogue composition", 672, 432, SDL_WINDOW_HIDDEN);
    REQUIRE(window);
    PresentationRenderer renderer;
    REQUIRE(renderer.init(window, false, false));
    REQUIRE(renderer.is_gpu());
    EnhancedGraphics effects;
    REQUIRE(effects.init(renderer.get()));
    std::vector<uint32_t> ordinary(224 * 144, 0xAA2244), mask(ordinary.size(), 0);
    EnhancedParameters params;
    params.grade = params.bloom = params.light_strength = params.water_strength = 0;
    params.shadows = false;
    params.background.assign(ordinary.size(), 0x22AA66);
    for (int y = 92; y < 144; ++y)
        for (int x = 0; x < 224; ++x)
            mask[size_t(y * 224 + x)] = 0xFF0000;
    REQUIRE(effects.upload(ordinary, ordinary, mask.data(), params));
    auto *target = effects.render();
    REQUIRE(target);
    auto *texture =
        SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 224, 144);
    REQUIRE(texture);
    REQUIRE(SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST));
    REQUIRE(SDL_UpdateTexture(texture, nullptr, ordinary.data(), 224 * 4));
    REQUIRE(SDL_SetRenderTarget(renderer.get(), target));
    REQUIRE(effects.restore_ui(texture, {0, 0, 672, 432}));
    auto *surface = SDL_RenderReadPixels(renderer.get(), nullptr);
    REQUIRE(surface);
    for (int x : {1, 111, 223})
        for (int native_y : {91, 92}) {
            Uint8 r, g, b;
            REQUIRE(SDL_ReadSurfacePixel(surface, x * 3 + 1, native_y * 3 + 1, &r, &g, &b, nullptr));
            CHECK((uint32_t(r) << 16 | uint32_t(g) << 8 | b) == (native_y == 91 ? 0x22AA66 : 0xAA2244));
        }
    SDL_DestroySurface(surface);
    SDL_SetRenderTarget(renderer.get(), nullptr);
    SDL_DestroyTexture(texture);
    effects.shutdown();
    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
}

TEST_CASE("meat preview underlay cells expire before reward windows in both pictures") {
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    w.cpu.s.halted = true;
    w.cpu.s.flags = 0;
    chrome_fixture::install(w);
    w.io[0x60] = 0xE0;
    w.io[0] = 3;
    w.io[7] = 0x32;
    std::fill_n(w.ram.begin() + 0xA000, 32, 0x11);
    w.set_second(true);
    for (unsigned at = 0; at < 65536; ++at)
        w.shadow_write8(uint16_t(at), w.ram[at]);
    w.capture_layers(true);
    game::BattleUi ui;
    ui.install(w);
    game::Machine m(w);
    game::hooks::emit(game::hooks::Event::BattleStart, m);
    game::hooks::emit(game::hooks::Event::BattleRewards, m);
    game::hooks::emit(game::hooks::Event::MeatMenu, m, {1});
    game::hooks::emit(game::hooks::Event::MeatMenu, m, {3});
    auto cell = [&](uint16_t entry) {
        w.ram[0x1800] = uint8_t(entry);
        w.ram[0x1801] = uint8_t(entry >> 8);
        w.shadow_write8(0x1800, uint8_t(entry));
        w.shadow_write8(0x1801, uint8_t(entry >> 8));
    };
    cell(0x2900); // comparison tile 300h on SCR2
    w.run_frame();
    w.run_frame();
    cell(0x1FD5); // the native reward window covers the comparison's old cell
    w.run_frame();
    const auto ram = w.ram;
    const auto io = w.io;
    const auto ordinary = w.framebuffer();
    game::hooks::emit(game::hooks::Event::MeatMenu, m, {0});
    CHECK(w.ram == ram);
    CHECK(w.io == io);
    CHECK(w.framebuffer() == ordinary);
    w.run_frame();
    w.run_frame();
    for (bool second : {false, true}) {
        const auto *layers = w.frame_layers(second, true);
        REQUIRE(layers);
        // The reward scene must not replay the comparison tile from beneath its hidden window.
        CHECK((layers->planes[emu::FrameLayers::Screen2][0].flags & emu::RasterPixel::Present) == 0);
    }
}
TEST_CASE("UI ownership distinguishes field actors spell sources and replacement windows") {
    using namespace sawgod::game;
    sawgod::emu::RasterPixel p;
    p.palette = 15;
    p.tile = 0x18;
    p.sprite = 0;
    CHECK_FALSE(UiOwnership{true, false}.owns(p, sawgod::emu::FrameLayers::SpriteFront));
    CHECK(UiOwnership{false, true}.owns(p, sawgod::emu::FrameLayers::SpriteFront));
    p.palette = 8;
    CHECK_FALSE(UiOwnership{false, true, true}.owns(p, sawgod::emu::FrameLayers::SpriteFront));
    CHECK(UiOwnership{false, true}.owns(p, sawgod::emu::FrameLayers::SpriteFront));
    p.palette = 12; // the underlay's replacement set is deliberately larger than the UI set
    p.tile = 0x300;
    CHECK((kSceneWindowPalettes >> p.palette & 1) == 1);
    CHECK_FALSE(UiOwnership{false, true}.owns(p, sawgod::emu::FrameLayers::Screen2));
    CHECK_FALSE(UiOwnership{true, false}.panel(80));
    CHECK(UiOwnership{false, true}.panel(80));
}

TEST_CASE("clean-scene ownership samples all screens at a raster boundary") {
    using namespace sawgod::game;
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    Machine m(w);
    auto &session = Session::of(w);
    for (bool dialogue_first : {false, true}) {
        BattleUi battle;
        DialogueUi dialogue;
        if (dialogue_first) {
            dialogue.install(w);
            battle.install(w);
        } else {
            battle.install(w);
            dialogue.install(w);
        }
        session.apply_scene_settings(w);
        hooks::emit(hooks::Event::BattleStart, m);
        hooks::emit(hooks::Event::BattleMenu, m);
        hooks::emit(hooks::Event::Dialogue, m, {1});
        CHECK(w.scene_hidden_palettes == 0); // notifications never change a raster's switches
        session.apply_scene_settings(w);
        CHECK(w.scene_hidden_palettes == kSceneWindowPalettes);
        CHECK(w.scene_hidden_sprite_prefix == 27);
        const auto ram = w.ram;
        const auto io = w.io;
        hooks::emit(hooks::Event::Dialogue, m, {0});
        session.apply_scene_settings(w);
        CHECK(w.scene_hidden_palettes == kSceneWindowPalettes); // closing dialogue cannot erase battle
        CHECK(w.ram == ram);
        CHECK(w.io == io);
        session.new_generation();
        session.apply_scene_settings(w);
        CHECK(w.scene_hidden_palettes == 0);
        CHECK(w.scene_window_palettes == 0);
    }
}

TEST_CASE("battle clean scene preserves low-bank art sharing window palettes") {
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    w.cpu.s.halted = true;
    w.cpu.s.flags = 0;
    w.io[0x60] = 0xE0;
    w.io[0] = 2;
    w.io[7] = 0x30;
    w.scene_hidden_palettes = w.scene_window_palettes = 0xFF80;
    w.scene_window_tile_min = 0xC0;
    for (int pal = 8; pal < 16; ++pal) {
        w.ram[size_t(0xFE02 + pal * 32)] = 0xAB;
        w.ram[size_t(0xFE03 + pal * 32)] = 0x0C;
    }
    std::fill_n(w.ram.begin() + 0x4000 + 0x20 * 32, 32, 0x11);
    std::fill_n(w.ram.begin() + 0x4000 + 0x1D9 * 32, 32, 0x11);
    for (int pal = 8; pal < 16; ++pal) {
        const size_t at = size_t(0x1800 + (pal - 8) * 2);
        w.ram[at] = 0x20;
        w.ram[at + 1] = uint8_t(pal << 1);
    }
    w.run_frame();
    w.run_frame();
    for (int pal = 8; pal < 16; ++pal)
        CHECK(w.framebuffer_rgb(sawgod::emu::Wsc::ColorMode::Raw, false, true)[size_t((pal - 8) * 8)] ==
              0xCCAABB);
    // A window cell (or closed reward art) written over the effect cell is hidden, but the effect itself was
    // never remembered as underlay: the battle may be clearing it, and a stale effect must not come back
    // (the whirlpool stayed behind the whole battle otherwise).
    std::fill_n(w.ram.begin() + 0x4000 + 0xC0 * 32, 32, 0x22);
    for (const auto entry : {uint16_t(0x1FD9), uint16_t(0x18C0)}) {
        w.ram[0x1808] = uint8_t(entry);
        w.ram[0x1809] = uint8_t(entry >> 8);
        const size_t color = 0xFE00 + size_t(entry >> 9 & 15) * 32 + (entry == 0x18C0 ? 4 : 2);
        w.ram[color] = 0;
        w.ram[color + 1] = 0x0F;
        w.run_frame();
        CHECK(w.framebuffer_rgb(sawgod::emu::Wsc::ColorMode::Raw, false, true)[32] != 0xCCAABB);
        CHECK(w.framebuffer_rgb(sawgod::emu::Wsc::ColorMode::Raw)[32] == 0xFF0000);
    }
}

TEST_CASE("Dialogue hides palette 15 window sprites but keeps field NPCs drawn on palette 15") {
    // Palette 15 is the window's sprite palette and also the field's eighth NPC palette (Byakko's chest).
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    w.capture_layers(true);
    w.io[0x60] = 0xE0;
    w.io[0] = 0x04; // sprites on
    w.io[4] = 1;    // sprite table at 0x200
    w.io[6] = 2;
    for (int at : {0x200, 0x204})
        w.ram[size_t(at + 1)] = 0x0E; // palette 15
    w.ram[0x200] = 0x18;              // a window sprite at x 0
    w.ram[0x204] = 0x19;              // a field NPC's sprite at x 16
    w.ram[0x207] = 16;
    for (int i = 0; i < 64; ++i)
        w.ram[size_t(0x4000 + 0x18 * 32 + i)] = 0x11;
    w.ram[0xFE00 + 15 * 32 + 2] = 0x00; // palette 15 color 1: red
    w.ram[0xFE00 + 15 * 32 + 3] = 0x0F;
    w.terrain_cards[1] = {1, 0x0E19, false};
    w.publish_terrain_cards();
    w.scene_hidden_sprite_palettes = 0x8000;
    w.scene_keep_field_npcs = true; // field dialogue (clean_scene_settings)
    w.run_frame();
    w.run_frame();
    const auto shown = w.framebuffer_rgb(emu::Wsc::ColorMode::Raw);
    const auto scene = w.framebuffer_rgb(emu::Wsc::ColorMode::Raw, false, true);
    REQUIRE(scene.size() == shown.size());
    CHECK(shown[2 * 224 + 2] == 0xFF0000);
    CHECK(shown[2 * 224 + 18] == 0xFF0000);
    CHECK(scene[2 * 224 + 2] != 0xFF0000);  // the window sprite is UI: hidden
    CHECK(scene[2 * 224 + 18] == 0xFF0000); // the NPC stays
}
