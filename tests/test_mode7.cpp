#include "game/session.h"
#include "test_mode7_support.h"
#include <cstring>

TEST_CASE(
    "Mode 7 gates every world map, the world copies, the false paradise and fidelity and falls back for "
    "layouts") {
    // 199 and 24: the later-story copies of World 3 (122) and World 4 (136), shown as their worlds.
    CHECK(game::GameConfig::world_of(199) == 122);
    CHECK(game::GameConfig::world_of(24) == 136);
    CHECK(game::GameConfig::shown_world(199));
    CHECK(game::GameConfig::shown_world(24));
    CHECK_FALSE(game::GameConfig::world_map(199)); // floor cards keep the native world IDs
    CHECK_FALSE(game::GameConfig::world_map(24));
    for (int map = 0; map < game::kMapCount; ++map)
        for (auto level : {FidelityMode::Gb, FidelityMode::Wsc, FidelityMode::Enhanced, FidelityMode::Swap}) {
            CHECK(game::mode7_allowed(map, level, true, false) ==
                  ((map == 24 || map == 74 || map == 88 || map == 102 || map == 122 || map == 136 ||
                    map == 199) &&
                   level == FidelityMode::Enhanced));
            CHECK_FALSE(game::mode7_allowed(map, level, false, false));
            CHECK_FALSE(game::mode7_allowed(map, level, true, true));
        }
    Config config;
    Settings settings;
    settings.register_all(config);
    // World geometry stays fixed; field pull-out is configurable.
    const game::Mode7Look look;
    CHECK(look.tilt == 16);
    CHECK(look.lens == 24);
    CHECK(look.distance == 1.5f);
    CHECK(look.field_zoom == 1.5f);
    CHECK(settings.enhanced_field_zoom->get() == 125);
    CHECK(settings.enhanced_mode7->label == "Mode 7 World Map");
    CHECK(settings.enhanced_field_zoom->help == "Enhanced GFX: Town and dungeon camera distance");
    for (const char *retired : {"enhanced.mode7_tilt", "enhanced.mode7_lens", "enhanced.mode7_distance"})
        CHECK(config.find(retired) == nullptr);
    CHECK(settings.enhanced_mode7_focus->get() == 30);
    CHECK(settings.enhanced_mode7_focus_size->get() == 50);
    // Depth of Field switches the focus (its two values stay as hidden tuning); Mode 7 the tilt; Bloom 0-5,
    // default 2
    for (const auto *v : {settings.enhanced_mode7_focus, settings.enhanced_mode7_focus_size}) {
        CHECK(v->category == "Video");
        CHECK(v->hidden);
    }
    for (const auto *v : {settings.enhanced_mode7, settings.enhanced_dof}) {
        CHECK(v->category == "Video");
        CHECK(v->on());
        CHECK_FALSE(v->hidden);
    }
    CHECK(settings.enhanced_bloom->get() == 40);
    CHECK(settings.enhanced_bloom->min == 0);
    CHECK(settings.enhanced_bloom->max == 40);
    CHECK_FALSE(settings.enhanced_bloom->hidden);
    CHECK(config.overlong_help().empty());
}

TEST_CASE("Mode 7 tilt transitions take half a second and interrupted fades are continuous") {
    game::Mode7Fade fade;
    constexpr uint64_t start = 1'000'000'000;
    CHECK(fade.update(true, start) == 0);
    CHECK(fade.update(true, start + 250'000'000) == doctest::Approx(0.5));
    CHECK(fade.update(true, start + Fidelity::kFadeNs) == 1);
    CHECK(fade.update(false, start + Fidelity::kFadeNs) == 1);
    CHECK(fade.update(false, start + 750'000'000) == doctest::Approx(0.5));
    CHECK(fade.update(true, start + 750'000'000) == doctest::Approx(0.5));
    CHECK(fade.update(true, start + 1'000'000'000) == doctest::Approx(0.75));
    CHECK(fade.update(true, start + 1'250'000'000) == 1);
    CHECK(fade.update(false, start + 1'250'000'000) == 1);
    CHECK(fade.update(false, start + 1'750'000'000) == 0);
    // settle: an endpoint at once, and later changes fade from it.
    fade.settle(true);
    CHECK(fade.amount(start + 1'750'000'000) == 1);
    CHECK(fade.update(true, start + 1'750'000'000) == 1);
    CHECK(fade.update(false, start + 1'750'000'000) == 1);
    CHECK(fade.update(false, start + 2'000'000'000) == doctest::Approx(0.5));
    fade.settle(false);
    CHECK(fade.update(false, start + 2'000'000'000) == 0);
    CHECK(fade.update(true, start + 2'000'000'000) == 0);
    CHECK(fade.update(true, start + 2'250'000'000) == doctest::Approx(0.5));
}

TEST_CASE("Flat field zoom arrives settled and animates only for a fidelity step") {
    using Step = game::FieldZoomEntry::Step;
    constexpr uint64_t frame = 13'250'000; // one 75.47 Hz game frame
    // One presented frame, as the app runs it: the request, the entry decision, the shared fade.
    struct Camera {
        game::FieldZoomEntry entry;
        game::Mode7Fade fade;
        int source = -1; // the map the camera has loaded
        uint64_t now = 1'000'000'000;
        float frame_amount(int map, bool field, bool enhanced, bool hidden = false) {
            now += frame;
            const bool flat = source >= 0 && !game::GameConfig::mode7_map(source);
            const auto step = entry.step(map, field, enhanced, hidden, source, flat, fade.amount(now));
            bool requested = field && enhanced;
            if (step != Step::Fade)
                fade.settle(step == Step::Settle);
            else if (source != map && fade.amount(now) > 0)
                requested = false;
            const float amount = fade.update(requested, now);
            if (requested && (amount == 0 || source != map || step == Step::Settle))
                source = map;
            return amount;
        }
    };
    SUBCASE("entering a town in Enhanced: zoomed on the first frame, from any previous scene") {
        for (int previous : {-1, 88, 79}) { // a load / a new session, the world map, another town
            Camera c;
            c.source = previous;
            if (previous >= 0)
                c.fade.settle(true);
            // the warp's frames: not this map's field yet
            for (int i = 0; i < 3; ++i)
                c.frame_amount(1, false, true);
            CHECK(c.frame_amount(1, true, true) == 1);
            CHECK(c.source == 1);
            for (int i = 0; i < 60; ++i)
                CHECK(c.frame_amount(1, true, true) == 1);
        }
    }
    SUBCASE("a fidelity step in the town takes the half-second fade both ways") {
        Camera c;
        CHECK(c.frame_amount(1, true, false) == 0); // entered in WSC
        for (int i = 0; i < 10; ++i)
            CHECK(c.frame_amount(1, true, false) == 0);
        float previous = c.frame_amount(1, true, true); // step up
        CHECK(previous == 0);
        bool middle = false;
        for (int i = 0; i < 40; ++i) {
            const float amount = c.frame_amount(1, true, true);
            CHECK(amount >= previous);
            CHECK(amount - previous < 0.03f); // 13.25 ms of 500
            middle |= amount > 0.4f && amount < 0.6f;
            previous = amount;
        }
        CHECK(middle);
        CHECK(previous == 1);
        previous = c.frame_amount(1, true, false); // step down
        CHECK(previous == 1);
        for (int i = 0; i < 40; ++i) {
            const float amount = c.frame_amount(1, true, false);
            CHECK(amount <= previous);
            CHECK(previous - amount < 0.03f);
            previous = amount;
        }
        CHECK(previous == 0);
    }
    SUBCASE("dialogue, small windows and Options keep the field: the zoom stays put") {
        Camera c;
        CHECK(c.frame_amount(1, true, true) == 1);
        for (int i = 0; i < 100; ++i) // the app keeps `field` true under them
            CHECK(c.frame_amount(1, true, true) == 1);
    }
    SUBCASE("a full-screen menu drops the zoom at once and it is back settled") {
        Camera c;
        CHECK(c.frame_amount(1, true, true) == 1);
        CHECK(c.frame_amount(1, false, true, true) == 0);
        for (int i = 0; i < 20; ++i)
            CHECK(c.frame_amount(1, false, true, true) == 0);
        CHECK(c.frame_amount(1, true, true) == 1);
    }
    SUBCASE("leaving for another map never leaves the old zoom over it") {
        Camera c;
        CHECK(c.frame_amount(1, true, true) == 1);
        CHECK(c.frame_amount(88, false, true) == 0); // the world's first frames
        CHECK(c.frame_amount(88, false, true) == 0);
        Camera d;
        CHECK(d.frame_amount(1, true, true) == 1);
        CHECK(d.frame_amount(79, true, true) == 1); // town to town: no frame in between
        CHECK(d.source == 79);
    }
    SUBCASE("a scene that takes the field picture away returns at its settled framing") {
        Camera c;
        CHECK(c.frame_amount(1, true, true) == 1);
        float amount = 1;
        for (int i = 0; i < 10; ++i)
            amount = c.frame_amount(1, false, true);
        CHECK(amount > 0.6f);
        CHECK(amount < 0.9f);
        const float back = c.frame_amount(1, true, true);
        CHECK(back == 1);
        // once fully lowered (a battle), the field returns settled
        for (int i = 0; i < 60; ++i)
            c.frame_amount(1, false, true);
        CHECK(c.frame_amount(1, true, true) == 1);
    }
}

TEST_CASE("Field reconstruction hides cleared rasters without changing camera ownership") {
    emu::Wsc w(world_rom(true), 0);
    REQUIRE(tools::prepare_field_preview(w, 88, game::MapData{}, 10, 10));
    w.capture_layers(true);
    w.run_frame();
    game::SceneObserver observer;
    observer.install(w);
    game::Machine m(w);
    game::hooks::emit(game::hooks::Event::PresentationField, m, {1});
    REQUIRE(observer.describe(w, false, false, false).kind == game::PresentationScene::Kind::Field);
    const auto field = w.ram;
    const auto display = w.io;
    SUBCASE("cleared screens under a host dialogue window") {
        // load_field_map's blank screen: the previous descriptor and palette remain loaded.
        for (size_t at = 0x1000; at < 0x1800; at += 2) {
            w.ram[at] = 0xEF;
            w.ram[at + 1] = 0x1F;
        }
        std::array<uint8_t, emu::FrameLayers::Pixels> coverage{};
        std::fill(coverage.begin() + 92 * 224, coverage.end(), 1);
        w.ui_coverage = &coverage;
        w.run_frame();
        w.run_frame();
        CHECK(game::recognized_field(w));
        const auto scene = observer.describe(w, false, false, true);
        CHECK(scene.kind == game::PresentationScene::Kind::Transition);
        CHECK(scene.field_cleared);
        CHECK(observer.describe(w, false, false, false).field_cleared);
        w.ui_coverage = nullptr;
    }
    SUBCASE("display layers disabled while their map remains in memory") {
        w.io[0] = 0;
        w.run_frame();
        w.run_frame();
        CHECK(game::recognized_field(w));
        const auto scene = observer.describe(w, false, false, false);
        CHECK(scene.kind == game::PresentationScene::Kind::Transition);
        CHECK(scene.perspective_allowed);
        CHECK(scene.field_cleared); // loads retain the camera, but publish the native raster
        CHECK(observer.describe(w, false, false, true).field_cleared);
    }
    SUBCASE("field palettes faded to the blank backdrop") {
        std::fill(w.ram.begin() + 0xFE00, w.ram.begin() + 0x10000, 0);
        w.run_frame();
        w.run_frame();
        CHECK(game::recognized_field(w));
        const auto scene = observer.describe(w, false, false, false);
        CHECK(scene.kind == game::PresentationScene::Kind::Transition);
        CHECK(scene.perspective_allowed);
        CHECK(scene.field_cleared);
        CHECK(observer.describe(w, false, false, true).field_cleared);
    }
    SUBCASE("a scripted full-width overlay owns the picture while it runs") {
        game::hooks::emit(game::hooks::Event::ScriptedFieldOverlay, m, {1});
        const auto scene = observer.describe(w, false, false, false);
        CHECK(scene.kind == game::PresentationScene::Kind::Transition);
        CHECK(scene.transition == game::PresentationScene::Transition::ScriptedOverlay);
        CHECK(scene.perspective_allowed);
        CHECK(scene.field_cleared);
        game::hooks::emit(game::hooks::Event::ScriptedFieldOverlay, m, {0});
        CHECK(observer.describe(w, false, false, false).kind == game::PresentationScene::Kind::Field);
    }
    SUBCASE("a missing raster is not a cleared field") {
        w.capture_layers(false);
        w.capture_layers(true);
        const auto scene = observer.describe(w, false, false, false);
        CHECK(scene.kind == game::PresentationScene::Kind::Transition);
        CHECK(scene.perspective_allowed);
        CHECK(scene.field_cleared);
        CHECK(observer.describe(w, false, false, true).field_cleared);
    }
    w.ram = field;
    w.io = display;
    w.run_frame();
    w.run_frame();
    const auto restored = observer.describe(w, false, false, false);
    CHECK(restored.kind == game::PresentationScene::Kind::Field);
    CHECK(restored.perspective_allowed);
    CHECK_FALSE(restored.field_cleared);
}

TEST_CASE("Mode 7 real camera roundtrips at supported settings without a horizon") {
    for (float tilt : {0.f, 15.f, 30.f, 45.f})
        for (float lens : {12.f, 24.f, 40.f}) {
            game::Mode7Camera camera{tilt, lens};
            for (int y = 0; y < 144; y += 11)
                for (int x = 0; x < 224; x += 13) {
                    const auto ground = camera.ground(float(x), float(y));
                    const auto screen = camera.project(ground[0], ground[1]);
                    CHECK(std::isfinite(ground[0]));
                    CHECK(screen[0] == doctest::Approx(x).epsilon(0.001));
                    CHECK(screen[1] == doctest::Approx(y).epsilon(0.001));
                }
        }
    game::Mode7Camera camera;
    const auto far = camera.ground(112, 0), near = camera.ground(112, 143);
    CHECK(camera.project(112, far[1])[2] / camera.project(112, near[1])[2] ==
          doctest::Approx(0.85).epsilon(0.03));
}

TEST_CASE("Mode 7 yields every entry-wipe frame even with ordinary or fading world state") {
    emu::Wsc w(world_rom(true), 0);
    REQUIRE(tools::prepare_field_preview(w, 88, game::MapData{}, 10, 10));
    w.capture_layers(true);
    w.run_frame();
    game::SceneObserver observer;
    observer.install(w);
    game::WorldTiltEntry entry;
    for (int travel = 0; travel <= 3; ++travel)
        for (int fade : {0, 1}) {
            CAPTURE(travel);
            CAPTURE(fade);
            w.ram[0xD0D6] = uint8_t(travel);
            w.ram[0xD519] = uint8_t(fade);
            REQUIRE(observer.describe(w, false, false, false).perspective_allowed);
            // animate_battle_entry installs 5000:1310 before BattleStart.
            w.ram[0x90] = 0x10;
            w.ram[0x91] = 0x13;
            w.ram[0x92] = 0;
            w.ram[0x93] = 0x50;
            w.io[0xB2] |= 0x10;
            const auto ram = w.ram;
            const auto io = w.io;
            for (int frame = 0; frame < 40; ++frame) {
                const auto scene = observer.describe(w, false, false, false);
                CHECK(scene.kind == game::PresentationScene::Kind::Transition);
                CHECK_FALSE(scene.perspective_allowed);
                CHECK(entry.step(88, scene.perspective_allowed, true) == game::FieldZoomEntry::Step::Drop);
            }
            CHECK(w.ram == ram);
            CHECK(w.io == io);
            w.io[0xB2] &= ~0x10;
            CHECK(observer.describe(w, false, false, false).perspective_allowed);
        }
}

TEST_CASE("Mode 7 uses live map cells atlas palette and authored borders without changing state") {
    emu::Wsc w(world_rom(), 0);
    game::MapData map;
    map.tiles[1] = 1;
    for (int y = 8; y < 72; ++y)
        for (int x = 8; x < 72; ++x)
            map.cells[size_t(y * 80 + x)] = 1;
    REQUIRE(tools::prepare_field_preview(w, 88, map, 0, 0));
    w.capture_layers(true);
    w.run_frame();
    game::PresentationScene flat;
    flat.kind = game::PresentationScene::Kind::Field;
    flat.map = 88;
    game::LightTags tags;
    const auto ram = w.ram;
    const auto sram = w.sram;
    const auto io = w.io;
    const auto cpu = w.cpu.s;
    const auto frame = w.frame_count;
    auto perspective = game::mode7_picture(w, flat, tags, {}, true);
    REQUIRE(perspective.pixels.size() == 224 * 144);
    CHECK(perspective.pixels[0] == 0x112233); // genuine north/west border; never opposite playable edge
    CHECK(perspective.pixels[79 * 224 + 112] == 0xAABBCC);
    CHECK(w.ram == ram);
    CHECK(w.sram == sram);
    CHECK(w.io == io);
    CHECK(w.frame_count == frame);
    for (int i = 0; i < 8; ++i)
        CHECK(w.cpu.s.r[i] == cpu.r[i]);
    for (int i = 0; i < 4; ++i)
        CHECK(w.cpu.s.seg[i] == cpu.seg[i]);
    CHECK(w.cpu.s.ip == cpu.ip);
    CHECK(w.cpu.s.flags == cpu.flags);
    // The outgoing world remains renderable after dialogue/warps replace the machine's map/palette.
    game::Mode7Source held;
    held.latch(w, flat);
    CHECK(held.map() == 88);
    const game::Mode7Camera half{15, 24, 0.5f};
    const auto half_picture = held.picture(tags, half, true, nullptr);
    CHECK(held.picture(tags, {0, 24, 0}, true, nullptr).pixels ==
          w.framebuffer_rgb(emu::Wsc::ColorMode::Raw));
    // State-owned cell edit is observed next frame; no static exported-map cache.
    w.sram[size_t((4 + 8) * 80 + 7 + 8)] = 0;
    perspective = game::mode7_picture(w, flat, tags, {}, true);
    CHECK(perspective.pixels[79 * 224 + 112] == 0x112233);
    CHECK(held.picture(tags, half, true, nullptr).pixels == half_picture.pixels);
    w.ram[0xFE02] = 0x56;
    w.ram[0xFE03] = 4;
    perspective = game::mode7_picture(w, flat, tags, {}, true);
    CHECK(perspective.pixels[79 * 224 + 112] == 0x445566);
    flat.kind = game::PresentationScene::Kind::Battle;
    CHECK(game::mode7_picture(w, flat, tags, {}, true).pixels.empty());
    flat.kind = game::PresentationScene::Kind::Field;
    flat.map = 10;
    CHECK(game::mode7_picture(w, flat, tags, {}, true).pixels.empty());
}

TEST_CASE("Mode 7 continues the authored border past every map edge, never the filler rows "
          "[GPU]") {
    // A 2x2 checkerboard border, as maps 88 and 136 author theirs, under the two filler rows of cell 00 that
    // head every loaded grid. Past each edge the ground keeps the checkerboard's phase, so a pixel's color
    // follows its world cell's parity everywhere; the filler color never shows.
    auto rom = world_rom();
    rom[0x35B718] = 16;
    std::fill_n(rom.begin() + 0x1100, 128, 0x33);
    rom[0xF2006] = 0x0F; // color 3: 0x0000FF
    rom[0xF2007] = 0;
    emu::Wsc w(rom, 0);
    game::MapData map;
    map.tiles[1] = 1; // color 2: 0xAABBCC
    map.tiles[2] = 2; // color 3; metatile 0 (the filler) is color 1: 0x112233
    for (int y = 2; y < 80; ++y)
        for (int x = 0; x < 80; ++x)
            map.cells[size_t(y * 80 + x)] = uint8_t((x + y) & 1 ? 1 : 2);
    REQUIRE(tools::prepare_field_preview(w, 88, map, 0, 0));
    w.capture_layers(true);
    w.run_frame();
    game::PresentationScene flat;
    flat.kind = game::PresentationScene::Kind::Field;
    flat.map = 88;
    game::LightTags tags;
    const game::Mode7Camera camera{1, 24, 1, 1};
    const auto check = [&](const std::vector<uint32_t> &pixels, std::array<int, 2> scroll, int tolerance) {
        int errors = 0, filler = 0, checked = 0;
        for (int y = 0; y < 144; ++y)
            for (int x = 0; x < 224; ++x) {
                const auto p = camera.ground(float(x) + 0.5f, float(y) + 0.5f);
                const float wx = float(scroll[0]) + p[0] + 128, wy = float(scroll[1]) + p[1] + 128;
                const float fx = wx - 16 * std::floor(wx / 16), fy = wy - 16 * std::floor(wy / 16);
                if (fx < 1 || fx > 15 || fy < 1 || fy > 15)
                    continue; // filtered or rounded cell boundaries
                const int parity = (int(std::floor(wx / 16)) + int(std::floor(wy / 16))) & 1;
                const uint32_t expected = parity ? 0xAABBCC : 0x0000FF, actual = pixels[size_t(y * 224 + x)];
                bool same = true;
                for (unsigned shift : {0u, 8u, 16u})
                    same &= std::abs(int(actual >> shift & 255) - int(expected >> shift & 255)) <= tolerance;
                errors += !same;
                filler += (actual & 0xFFFFFF) == 0x112233;
                ++checked;
            }
        INFO("scroll=" << scroll[0] << "," << scroll[1] << " checked=" << checked);
        CHECK(checked > 224 * 144 / 2);
        CHECK(filler == 0);
        CHECK(errors == 0);
    };
    const auto put_scroll = [&](std::array<int, 2> scroll) {
        for (int axis = 0; axis < 2; ++axis) {
            w.ram[size_t(0xD59D + axis * 2)] = uint8_t(scroll[size_t(axis)]);
            w.ram[size_t(0xD59E + axis * 2)] = uint8_t(scroll[size_t(axis)] >> 8);
        }
    };
    // North-west corner: the filler rows (y -128..-97) and beyond; west past x -128. South-east past 1151.
    const std::array<std::array<int, 2>, 2> corners{{{-260, -220}, {1040, 1060}}};
    for (const auto scroll : corners) {
        put_scroll(scroll);
        check(game::mode7_picture(w, flat, tags, camera, true).pixels, scroll, 0);
    }
    if (!std::getenv("SAWGOD_GPU_TESTS"))
        return;
    // The shader extends the same way, including the level-0 cache its filtered levels are made from.
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    auto *window = SDL_CreateWindow("Mode 7 edges", 224, 144, SDL_WINDOW_HIDDEN);
    REQUIRE(window);
    {
        PresentationRenderer renderer;
        REQUIRE(renderer.init(window, false, false));
        REQUIRE(renderer.is_gpu());
        Mode7Renderer gpu;
        REQUIRE(gpu.init(renderer.get()));
        std::array<uint32_t, 224 * 144> ui{};
        const game::LandmarkTags landmarks;
        for (const auto scroll : corners) {
            put_scroll(scroll);
            game::Mode7World cache;
            REQUIRE(cache.update(w, flat, tags, landmarks));
            REQUIRE(gpu.draw(cache, camera, false, ui.data()));
            REQUIRE(SDL_SetRenderTarget(renderer.get(), gpu.picture()));
            auto *surface = SDL_RenderReadPixels(renderer.get(), nullptr);
            REQUIRE(surface);
            auto *rgb = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ARGB8888);
            REQUIRE(rgb);
            std::vector<uint32_t> pixels(224 * 144);
            for (int y = 0; y < 144; ++y)
                std::copy_n(
                    reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(rgb->pixels) + y * rgb->pitch), 224,
                    pixels.begin() + y * 224);
            SDL_DestroySurface(rgb);
            SDL_DestroySurface(surface);
            SDL_SetRenderTarget(renderer.get(), nullptr);
            check(pixels, scroll, 2);
        }
    }
    SDL_DestroyWindow(window);
}

TEST_CASE("Mode 7 protects screen-space UI and projects upright actor pixels and ground shadows") {
    emu::Wsc w(world_rom(), 0);
    game::MapData map;
    REQUIRE(tools::prepare_field_preview(w, 88, map, 10, 10));
    // Opaque 16x16 actor away from the focus point, plus an actual native window tile.
    std::fill_n(w.ram.begin() + 0x4200, 128, 0x11);
    w.ram[0xFF02] = 0x0F;
    for (int q = 0; q < 4; ++q) {
        w.ram[size_t(0x200 + q * 4)] = uint8_t(0x10 + q);
        w.ram[size_t(0x202 + q * 4)] = uint8_t(12 + q / 2 * 8);
        w.ram[size_t(0x203 + q * 4)] = uint8_t(24 + q % 2 * 8);
    }
    w.io[0] |= 4;
    w.io[4] = 1;
    w.io[6] = 4;
    std::fill_n(w.ram.begin() + 0x6000, 32, 0x11);
    w.ram[0x1000] = 0x00;
    w.ram[0x1001] = 0x01;
    w.capture_layers(true);
    w.run_frame();
    w.run_frame();
    game::PresentationScene flat;
    flat.kind = game::PresentationScene::Kind::Field;
    flat.map = 88;
    flat.mask = game::mode7_ui_mask(w, false, false);
    REQUIRE(flat.mask[0] == 0xFF0000);
    game::LightTags tags;
    auto picture = game::mode7_picture(w, flat, tags, {}, true);
    REQUIRE(picture.scene.actors.size() == 1);
    const auto foot = game::Mode7Camera{}.project(32, 27);
    CHECK(picture.scene.actors[0][0] == doctest::Approx(std::round(foot[0])));
    CHECK(picture.scene.actors[0][1] == doctest::Approx(std::round(foot[1])));
    CHECK(picture.scene.mask[0] == flat.mask[0]);
    const auto ordinary = w.framebuffer_rgb(emu::Wsc::ColorMode::Raw);
    for (size_t at = 0; at < ordinary.size(); ++at)
        if (flat.mask[at] & 0xFF0000)
            CHECK(picture.pixels[at] == ordinary[at]);
    int sprite_pixels = 0;
    for (auto mask : picture.scene.mask)
        sprite_pixels += (mask & 0x004000) != 0;
    CHECK(sprite_pixels >= 225); // upright card, not the ground's foreshortened height
}

TEST_CASE("Mode 7 field ownership follows its picture through scripts and vehicles") {
    emu::Wsc w(world_rom(true), 0);
    game::MapData map;
    REQUIRE(tools::prepare_field_preview(w, 88, map, 10, 10));
    w.capture_layers(true);
    w.run_frame();
    game::SceneObserver observer;
    observer.install(w);
    game::Machine m(w);
    CHECK(observer.describe(w, false, false, false).perspective_allowed);
    game::hooks::emit(game::hooks::Event::PresentationField, m, {1});
    CHECK(observer.describe(w, false, false, false).perspective_allowed);
    CHECK_FALSE(observer.describe(w, false, true, false).perspective_allowed);
    CHECK(observer.describe(w, false, false, true).perspective_allowed);
    w.ram[0xD0D6] = 1; // ordinary world vehicle layout
    CHECK(observer.describe(w, false, false, false).perspective_allowed);
    w.ram[0xD0D6] = 3; // third native vehicle
    CHECK(observer.describe(w, false, false, false).perspective_allowed);
    w.ram[0xD0D6] = 4; // travel state does not decide who draws the picture
    CHECK(observer.describe(w, false, false, false).perspective_allowed);
    w.ram[0xD0D6] = 0;
    game::hooks::emit(game::hooks::Event::PresentationField, m, {0});
    CHECK(observer.describe(w, false, false, false).perspective_allowed);
    game::hooks::emit(game::hooks::Event::PresentationField, m, {0});
    w.ram[0xD52C] = 1; // outgoing warp script: the same held field is still visible
    CHECK(observer.describe(w, false, false, false).perspective_allowed);
    w.ram[0xD52C] = 0;
    game::hooks::emit(game::hooks::Event::BattleStart, m, {});
    CHECK_FALSE(observer.describe(w, true, false, false).perspective_allowed);
    game::Session::of(w).new_generation();
    const auto loaded_bank = w.ram[0xD52D];
    w.ram[0xD52D] = 0; // reset invalidates the loaded map, not just its producer wait flag
    w.ram[0xC813] = 0;
    CHECK_FALSE(observer.describe(w, false, false, false).perspective_allowed);
    // The field fading in (fade_field_to_target's flag) is the field already, before the loop's first
    // hook and across a load's new generation: effects and the flat zoom start with the fade. The world
    // tilt is settled from its first visible frame.
    game::hooks::emit(game::hooks::Event::BattleEnd, m, {});
    CHECK(observer.describe(w, false, false, false).kind == game::PresentationScene::Kind::Unknown);
    w.ram[0xD519] = 1;
    w.ram[0xD52D] = loaded_bank; // the destination descriptor has now been loaded
    w.ram[0xC813] = 1;           // destination field frame has been built
    const auto fading = observer.describe(w, false, false, false);
    CHECK(fading.kind == game::PresentationScene::Kind::Field);
    CHECK(fading.perspective_allowed);
    CHECK(observer.describe(w, false, true, false).kind == game::PresentationScene::Kind::Unknown);
    w.ram[0xD519] = 0;
    CHECK(observer.describe(w, false, false, false).kind == game::PresentationScene::Kind::Field);
}

TEST_CASE("Mode 7 groups the native player OAM writer as one upright card") {
    emu::Wsc w(world_rom(), 0);
    game::MapData map;
    REQUIRE(tools::prepare_field_preview(w, 88, map, 10, 10));
    tools::prepare_field_party(w, 0);
    w.capture_layers(true);
    w.run_frame();
    w.run_frame();
    game::PresentationScene flat;
    flat.kind = game::PresentationScene::Kind::Field;
    flat.map = 88;
    game::LightTags tags;
    const auto picture = game::mode7_picture(w, flat, tags, {}, true);
    REQUIRE(picture.scene.actors.size() == 1);
    CHECK(picture.scene.actors[0][0] == 112);
    CHECK(picture.scene.actors[0][1] == 79);
}

TEST_CASE("Landmark review resources roundtrip atomically and reject bad footprints") {
    game::LandmarkTags tags, loaded;
    std::string error;
    const auto csv = tags.csv();
    bool edited = true;
    CHECK(tags.refresh(csv, &edited) == csv);
    CHECK_FALSE(edited);
    CHECK(tags.refresh(csv + "\n", &edited) == csv + "\n");
    CHECK(edited);
    // An older intact generated cache refreshes; its checksum proves the body was unedited.
    game::LandmarkTags old;
    REQUIRE(old.load(
        "record,id,kind,map,tileset,base_row,extra_shaft_rows,shaft_row,enabled,x,y,metatile,mirror\n",
        &error));
    CHECK(tags.refresh(old.csv(), &edited) == csv);
    CHECK_FALSE(edited);
    REQUIRE(loaded.load(csv, &error));
    CHECK(loaded.csv() == csv);
    CHECK(loaded.entries().size() == tags.entries().size());
    CHECK(loaded.plain(88, 15, 40));
    CHECK_FALSE(loaded.plain(88, 15, 49));
    // Standing cards only on the four main worlds: the false paradise keeps its flat Tower.
    CHECK_FALSE(loaded.plain(74, 9, 27));
    CHECK(std::none_of(loaded.entries().begin(), loaded.entries().end(),
                       [](const auto &e) { return e.map == 74; }));
    // The later-story world copies stand exactly as their worlds: 199 as 122, 24 as 136.
    for (const auto &[copy, world] : {std::pair{199, 122}, std::pair{24, 136}}) {
        int copies = 0;
        for (const auto &e : loaded.entries())
            if (e.map == world) {
                const auto id = std::to_string(copy) + e.id.substr(std::to_string(world).size());
                const auto it = std::find_if(loaded.entries().begin(), loaded.entries().end(),
                                             [&](const auto &c) { return c.id == id; });
                REQUIRE(it != loaded.entries().end());
                CHECK(it->map == copy);
                CHECK(it->tileset == e.tileset);
                CHECK(it->enabled == e.enabled);
                CHECK(it->cells.size() == e.cells.size());
                ++copies;
            }
        CHECK(copies > 0);
        CHECK(std::count_if(loaded.entries().begin(), loaded.entries().end(),
                            [&](const auto &c) { return c.map == copy; }) == copies);
    }
    CHECK(loaded.plain(199, 30, 37) == loaded.plain(122, 30, 37));
    for (const auto &entry : loaded.entries()) {
        int bottom = -1;
        for (const auto &c : entry.cells)
            bottom = std::max(bottom, c.y);
        CHECK(entry.base_row == bottom + 1);
        CHECK(entry.extra_shaft_rows == 0);
        if (entry.kind == "tower" || entry.kind == "tokyo_tower") {
            CHECK(entry.enabled);
            CHECK(entry.cells.size() == 3);
        }
    }
    auto bad = csv;
    bad += "cell,test,tower,88,15,2,0,0,1,12,10,49,0\n"; // occupied
    CHECK_FALSE(loaded.load(bad, &error));
    CHECK(loaded.csv() == csv);
    bad = "record,id,kind,map,tileset,base_row,extra_shaft_rows,shaft_row,enabled,x,y,metatile,mirror\n"
          "cell,test,tower,88,0,2,0,0,1,1,2,1,0\n"; // base above bottom
    CHECK_FALSE(loaded.load(bad, &error));
    CHECK(loaded.csv() == csv);
}

TEST_CASE("Landmark columns rise from repaired ground and share depth with actors without changing state") {
    auto rom = world_rom();
    // Three distinct painted rows, against one uniform ground metatile.
    rom[0x35B718] = 16;
    std::fill_n(rom.begin() + 0x1080, 128, 0x22);
    std::fill_n(rom.begin() + 0x1100, 128, 0x33);
    std::fill_n(rom.begin() + 0x1180, 128, 0x44);
    rom[0xF2006] = 0x0F;
    rom[0xF2007] = 0x00;
    rom[0xF2008] = 0xF0;
    rom[0xF2009] = 0x00;
    emu::Wsc w(rom, 0);
    game::MapData map;
    map.tiles[1] = 1;
    map.tiles[2] = 2;
    map.tiles[3] = 3;
    map.cells[8 * 80 + 8 + 5] = 1;
    map.cells[9 * 80 + 8 + 5] = 2;
    map.cells[10 * 80 + 8 + 5] = 3;
    REQUIRE(tools::prepare_field_preview(w, 88, map, 0, 0));
    w.capture_layers(true);
    w.run_frame();
    game::LandmarkTags landmarks;
    std::string error;
    const std::string header =
        "record,id,kind,map,tileset,base_row,extra_shaft_rows,shaft_row,enabled,x,y,metatile,mirror\n";
    const std::string cells = "cell,test,tower,88,0,3,0,1,1,5,0,1,0\n"
                              "cell,test,tower,88,0,3,0,1,1,5,1,2,0\n"
                              "cell,test,tower,88,0,3,0,1,1,5,2,3,0\n";
    REQUIRE(landmarks.load(header + "ground,,,88,0,,,,,,,0,\n" + cells, &error));
    game::PresentationScene flat;
    flat.kind = game::PresentationScene::Kind::Field;
    flat.map = 88;
    game::LightTags lights;
    const auto ram = w.ram;
    const auto io = w.io;
    const auto sram = w.sram;
    const auto frame = w.frame_count;
    const auto overhead = w.framebuffer_rgb(emu::Wsc::ColorMode::Raw);
    const auto picture = game::mode7_picture(w, flat, lights, {}, true, &landmarks);
    const auto base = game::Mode7Camera{}.project(88, 48);
    const int bx = int(std::round(base[0])), by = int(std::round(base[1]));
    CHECK(picture.pixels[size_t((by - 40) * 224 + bx)] == 0xAABBCC); // top
    CHECK(picture.pixels[size_t((by - 24) * 224 + bx)] == 0x0000FF); // shaft
    CHECK(picture.pixels[size_t((by - 8) * 224 + bx)] == 0x00FF00);  // base
    CHECK(picture.pixels[size_t((by + 1) * 224 + bx)] == 0x112233);  // repaired floor
    CHECK(w.ram == ram);
    CHECK(w.io == io);
    CHECK(w.sram == sram);
    CHECK(w.frame_count == frame);
    CHECK(w.framebuffer_rgb(emu::Wsc::ColorMode::Raw) == overhead);
    // A UI-owned pixel wins even over the painted column.
    const size_t protected_at = size_t((by - 24) * 224 + bx);
    flat.mask[protected_at] = 0xFF0000;
    CHECK(game::mode7_picture(w, flat, lights, {}, true, &landmarks).pixels[protected_at] ==
          overhead[protected_at]);
    flat.mask[protected_at] = 0;
    // Extra shaft uses real source art, remains optional and does not move the base.
    auto extended = cells;
    size_t pos = 0;
    while ((pos = extended.find(",3,0,1,1,", pos)) != std::string::npos) {
        extended.replace(pos, 9, ",3,1,1,1,");
        pos += 9;
    }
    REQUIRE(landmarks.load(header + "ground,,,88,0,,,,,,,0,\n" + extended, &error));
    const auto taller = game::mode7_picture(w, flat, lights, {}, true, &landmarks);
    CHECK(taller.pixels[size_t((by - 8) * 224 + bx)] == 0x00FF00);
    CHECK(taller.pixels[size_t((by - 52) * 224 + bx)] == 0xAABBCC); // tip remains above inserted shaft
    auto endless = cells;
    size_t infinity_pos = 0;
    while ((infinity_pos = endless.find(",3,0,1,1,", infinity_pos)) != std::string::npos) {
        endless.replace(infinity_pos, 9, ",3,-1,1,1,");
        infinity_pos += 10;
    }
    REQUIRE(landmarks.load(header + "ground,,,88,0,,,,,,,0,\n" + endless, &error));
    const auto forever = game::mode7_picture(w, flat, lights, {}, true, &landmarks);
    CHECK(std::count(forever.pixels.begin(), forever.pixels.end(), uint32_t(0xAABBCC)) == 0);
    CHECK(forever.pixels[size_t(bx)] == 0x00FF00); // whole-column stretch, not repeated shaft rows
    auto fractional = endless;
    size_t fraction_pos = 0;
    while ((fraction_pos = fractional.find(",3,-1,1,1,", fraction_pos)) != std::string::npos) {
        fractional.replace(fraction_pos, 10, ",3,1.5,1,1,");
        fraction_pos += 11;
    }
    REQUIRE(landmarks.load(header + "ground,,,88,0,,,,,,,0,\n" + fractional, &error));
    CHECK(landmarks.entries()[0].extra_shaft_rows == 1.5f);
    CHECK(game::mode7_picture(w, flat, lights, {0, 24, 1}, true, &landmarks).pixels == overhead);
    REQUIRE(landmarks.load(header + "ground,,,88,0,,,,,,,0,\n" + extended, &error));
    // An actor behind the base is covered; an actor in front covers the same base pixels.
    const auto actor = [&](int y) {
        w.ram[0xFF02] = 0x0F;
        w.ram[0xFF03] = 0x0F;
        for (int q = 0; q < 4; ++q) {
            w.ram[size_t(0x200 + q * 4)] = uint8_t(0x10 + q);
            w.ram[size_t(0x201 + q * 4)] = 0x20;
            w.ram[size_t(0x202 + q * 4)] = uint8_t(y + q / 2 * 8);
            w.ram[size_t(0x203 + q * 4)] = uint8_t(80 + q % 2 * 8);
            std::fill_n(w.ram.begin() + 0x4000 + (0x10 + q) * 32, 32, 0x11);
        }
        w.io[0] |= 4;
        w.io[4] = 1;
        w.io[6] = 4;
        w.run_frame();
        w.run_frame();
    };
    actor(32);
    CHECK(game::mode7_picture(w, flat, lights, {}, true, &landmarks).pixels[size_t((by - 8) * 224 + bx)] ==
          0x00FF00);
    actor(36);
    CHECK(game::mode7_picture(w, flat, lights, {}, true, &landmarks).pixels[size_t((by - 8) * 224 + bx)] ==
          0xFF00FF);
    w.io[0] &= uint8_t(~4);
    w.run_frame();
    w.run_frame();
    // A narrow painted tip leaves its old ground location exposed. It must show the
    // neighboring ground, not a second flattened copy of the tip.
    for (int tile = 4; tile < 16; ++tile)
        std::fill_n(w.ram.begin() + 0x8000 + tile * 32, 32, 0x11);
    w.ram[0x80FC] = 0x21; // source (8,15): visible tip pixel, everything else ground
    const auto narrow = game::mode7_picture(w, flat, lights, {}, true, &landmarks);
    const auto old_tip = game::Mode7Camera{}.project(88, 0);
    CHECK(narrow.pixels[size_t(int(std::round(old_tip[1])) * 224 + int(std::round(old_tip[0])))] == 0x112233);
    CHECK(std::count(narrow.pixels.begin(), narrow.pixels.end(), uint32_t(0xAABBCC)) >= 1);
    REQUIRE(landmarks.load(
        header + "ground,,,88,0,,,,,,,0,\n" + extended + "cutout,test,tower,88,0,3,1,1,1,,,1,\n", &error));
    const auto cutout = game::mode7_picture(w, flat, lights, {}, true, &landmarks);
    CHECK(std::count(cutout.pixels.begin(), cutout.pixels.end(), uint32_t(0xAABBCC)) == 0);
    // Changed map art invalidates the whole footprint rather than erasing the new cell.
    w.sram[8 * 80 + 8 + 5] = 0;
    CHECK(game::mode7_picture(w, flat, lights, {}, true, &landmarks).pixels != taller.pixels);
}

TEST_CASE("Mode 7 tile map changes cells only for topology and retains frozen inputs without mutation") {
    emu::Wsc w(world_rom(), 0);
    game::MapData map;
    map.tiles[1] = 1;
    map.cells.fill(1);
    REQUIRE(tools::prepare_field_preview(w, 88, map, 0, 0));
    w.capture_layers(true);
    w.run_frame();
    game::PresentationScene flat;
    flat.kind = game::PresentationScene::Kind::Field;
    flat.map = 88;
    game::LightTags tags;
    game::LandmarkTags landmarks;
    game::Mode7World cache;
    const auto ram = w.ram;
    const auto io = w.io;
    const auto sram = w.sram;
    REQUIRE(cache.update(w, flat, tags, landmarks));
    CHECK(cache.surface().dirty.size() == 6400);
    const auto revision = cache.surface().revision;
    REQUIRE(cache.update(w, flat, tags, landmarks));
    CHECK(cache.surface().dirty.empty());
    CHECK(cache.surface().revision == revision);
    CHECK(w.ram == ram);
    CHECK(w.io == io);
    CHECK(w.sram == sram);
    const int cell = 12 * 80 + 15;
    w.sram[size_t(cell)] = 0;
    REQUIRE(cache.update(w, flat, tags, landmarks));
    REQUIRE(cache.surface().dirty.size() == 1);
    CHECK(cache.surface().dirty[0] == cell);
    CHECK(cache.surface().words[size_t(12 * 2 * 160 + 15 * 2)] == 0x002000);
    CHECK(cache.surface().changed[size_t(cell)] == revision + 1);
    // A second collection before GPU submission must retain the revision of the changed cell.
    REQUIRE(cache.update(w, flat, tags, landmarks));
    CHECK(cache.surface().dirty.empty());
    CHECK(cache.surface().changed[size_t(cell)] == revision + 1);
    w.ram[0xFE04] = 0x56;
    w.ram[0xFE05] = 4;
    REQUIRE(cache.update(w, flat, tags, landmarks));
    CHECK(cache.surface().dirty.empty()); // palette animation never touches cell layout
    CHECK(cache.surface().palettes[2] == 0x445566);
    CHECK(cache.surface().revision == revision + 1);
    const auto tile_revision = cache.surface().tile_revision;
    w.ram[0x8080] ^= 0x10;
    REQUIRE(cache.update(w, flat, tags, landmarks));
    CHECK(cache.surface().dirty.empty());
    CHECK(cache.surface().tile_revision == tile_revision + 1);
    const auto terrain_revision = cache.surface().tile_revision;
    w.ram[0x4300] ^= 0x10; // actor DMA changed an unreferenced lower-bank tile
    w.ram[0xFF22] ^= 0x10; // and an unreferenced NPC palette
    REQUIRE(cache.update(w, flat, tags, landmarks));
    CHECK(cache.surface().tile_revision == terrain_revision);
    const auto colors_revision = cache.surface().palette_revision;
    w.ram[0xFF22] ^= 0x10;
    REQUIRE(cache.update(w, flat, tags, landmarks));
    CHECK(cache.surface().palette_revision == colors_revision);
    // A mod may deliberately reference that same lower-bank actor art as terrain.
    w.sram[0x1900] = 0x18;
    w.sram[0x1901] = 0x12; // lower bank, palette 9
    w.sram[12 * 80 + 15] = 0;
    REQUIRE(cache.update(w, flat, tags, landmarks));
    const auto mod_revision = cache.surface().tile_revision;
    w.ram[0x4300] ^= 0x10;
    REQUIRE(cache.update(w, flat, tags, landmarks));
    CHECK(cache.surface().tile_revision == mod_revision + 1);

    const auto held = cache.surface().words;
    flat.kind = game::PresentationScene::Kind::Unknown;
    CHECK_FALSE(cache.update(w, flat, tags, landmarks));
    CHECK(cache.map() == 88);
    CHECK(cache.surface().words == held);
    for (float distance : {1.f, 1.5f, 3.f})
        for (float tilt : {0.f, 30.f, 45.f}) {
            game::Mode7Camera camera{tilt, 24, 1, distance};
            const auto point = camera.ground(5, 5);
            const auto projected = camera.project(point[0], point[1]);
            CHECK(projected[0] == doctest::Approx(5));
            CHECK(projected[1] == doctest::Approx(5));
        }
    const game::Mode7Camera pulled{30, 24, 1, 1.5f};
    CHECK(pulled.ground(0, 0)[0] < game::Mode7Camera{}.ground(0, 0)[0]);
    for (const auto &e : landmarks.entries())
        if (e.id.find("-ruined-block-") != std::string::npos)
            CHECK_FALSE(e.enabled);
}

TEST_CASE("The crystal door vignette is only its own scene commands while the script runs") {
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0x2000);
    const auto gate = [&](int active, int kind, int argument) {
        w.ram[0xD52C] = uint8_t(active);
        w.ram[0xC826] = uint8_t(kind);
        w.ram[0xC827] = uint8_t(argument);
        return game::crystal_door_vignette(w);
    };
    for (int argument = 0; argument < 256; ++argument)
        for (int kind = 0; kind < 16; ++kind)
            for (int active : {0, 1, 2})
                CHECK(gate(active, kind, argument) ==
                      (active == 1 && kind == 2 && argument >= 0x2B && argument <= 0x2F));
}

TEST_CASE("Field zoom gates ordinary fields and chooses the nearest square output pixels") {
    for (int map = 0; map < game::kMapCount; ++map)
        for (auto level : {FidelityMode::Gb, FidelityMode::Wsc, FidelityMode::Enhanced}) {
            const bool field = map > 0 && !game::GameConfig::mode7_map(map);
            CHECK(game::field_zoom_allowed(map, level, true, false) ==
                  (field && level == FidelityMode::Enhanced));
            CHECK_FALSE(game::field_zoom_allowed(map, level, false, false));
            CHECK_FALSE(game::field_zoom_allowed(map, level, true, true));
        }
    for (const auto size : {std::array{224, 144}, std::array{672, 432}, std::array{1353, 870}})
        for (float zoom : {1.f, 1.1f, 1.25f, 1.5f})
            for (float amount : {0.f, 0.25f, 0.5f, 1.f}) {
                const auto camera = game::field_zoom_camera(zoom, amount, size[0], size[1], {160, 160});
                CHECK(camera.tilt == 0);
                const float scale = std::min(float(size[0]) / 224, float(size[1]) / 144);
                const auto settled = game::field_zoom_camera(zoom, 1, size[0], size[1], {160, 160});
                if (amount == 1) {
                    // settled: whole output pixels, the nearest to the wanted zoom
                    CHECK(camera.pixel_scale == std::floor(camera.pixel_scale));
                    const float error = std::abs(scale / camera.pixel_scale - zoom);
                    for (int k = 1; k <= 12; ++k)
                        CHECK(error <= std::abs(scale / float(k) - zoom) + 0.00001f);
                    CHECK(camera.origin[0] == std::floor(camera.origin[0]));
                    CHECK(camera.origin[1] == std::floor(camera.origin[1]));
                } else {
                    // a fidelity fade: one continuous zoom from the ordinary picture to the settled view
                    CHECK(camera.pixel_scale ==
                          doctest::Approx(scale + (settled.pixel_scale - scale) * amount));
                    for (size_t axis = 0; axis < 2; ++axis) {
                        CHECK(std::abs(camera.origin[axis]) <= std::abs(settled.origin[axis]) + 0.00001f);
                        CHECK(camera.origin[axis] * settled.origin[axis] >= 0);
                    }
                }
                if (amount == 0) { // exactly the ordinary picture: no jump when the fade starts or ends
                    CHECK(camera.pixel_scale == doctest::Approx(scale));
                    CHECK(camera.origin[0] == 0);
                    CHECK(camera.origin[1] == 0);
                    CHECK(camera.ground(112, 79)[0] == doctest::Approx(112 * camera.output_scale[0] / scale));
                }
                const auto a = camera.project(35, 29), b = camera.project(36, 30);
                CHECK((b[0] - a[0]) * camera.output_scale[0] == doctest::Approx(camera.pixel_scale));
                CHECK((b[1] - a[1]) * camera.output_scale[1] == doctest::Approx(camera.pixel_scale));
                const auto ground = camera.ground(a[0], a[1]);
                CHECK(ground[0] == doctest::Approx(35));
                CHECK(ground[1] == doctest::Approx(29));
            }
    const auto camera = game::field_zoom_camera(1.25f, 1, 1353, 870, {160, 160});
    CHECK(camera.pixel_scale == 5);
    const auto exact = game::field_zoom_camera(1.25f, 1, 1120, 720, {160, 160});
    CHECK(exact.pixel_scale == 4);
    CHECK(exact.ground(224, 144)[0] - exact.ground(0, 0)[0] == doctest::Approx(280));
    CHECK(exact.ground(224, 144)[1] - exact.ground(0, 0)[1] == doctest::Approx(180));
    for (auto scroll : {std::array{-104.f, -64.f}, std::array{1008.f, 1008.f}}) {
        const auto edge = game::field_zoom_camera(1.5f, 1, 1353, 870, scroll);
        const auto tl = edge.ground(0, 0), br = edge.ground(224, 144);
        CHECK(tl[0] + scroll[0] >= -128);
        CHECK(tl[1] + scroll[1] >= -128);
        CHECK(br[0] + scroll[0] <= 1152);
        CHECK(br[1] + scroll[1] <= 1152);
        // At a map edge the settled view is clamped: the fade slides there, with no jump at either end.
        float previous = 0;
        for (int i = 0; i <= 20; ++i) {
            const auto fading = game::field_zoom_camera(1.5f, float(i) / 20, 1353, 870, scroll);
            if (i == 0)
                CHECK(fading.origin[0] == 0);
            else
                CHECK(std::abs(fading.origin[0] - previous) <= std::abs(edge.origin[0]) / 8 + 0.01f);
            previous = fading.origin[0];
        }
        CHECK(previous == edge.origin[0]);
    }
}

TEST_CASE("GPU flat fields share an integer lattice for ground and active actor cards [GPU]") {
    if (!std::getenv("SAWGOD_GPU_TESTS"))
        return;
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    auto *window = SDL_CreateWindow("Field zoom", 1353, 870, SDL_WINDOW_HIDDEN);
    REQUIRE(window);
    PresentationRenderer renderer;
    REQUIRE(renderer.init(window, false, false));
    Mode7Renderer gpu;
    REQUIRE(gpu.init(renderer.get()));
    emu::Wsc w(world_rom(), 0);
    game::MapData map;
    map.tiles[1] = 1;
    map.cells.fill(1);
    REQUIRE(tools::prepare_field_preview(w, 10, map, 10, 10));
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; x += 2) {
            const int tile = 516 + y / 8 * 2 + x / 8;
            w.ram[size_t(0x8000 + (tile & 511) * 32 + y % 8 * 4 + x % 8 / 2)] =
                uint8_t((((x + y) % 14 + 1) << 4) | ((x + y + 1) % 14 + 1));
        }
    for (int index = 1; index < 16; ++index) {
        w.ram[size_t(0xFE00 + index * 2)] = uint8_t(index);
        w.ram[size_t(0xFE01 + index * 2)] = 0;
    }
    std::fill_n(w.ram.begin() + 0x4200, 128, 0x11);
    w.ram[0xFF02] = 0x0F;
    w.ram[0xFF03] = 0x0F; // magenta active actor
    for (int q = 0; q < 4; ++q) {
        w.ram[size_t(0x200 + q * 4)] = uint8_t(0x10 + q);
        w.ram[size_t(0x202 + q * 4)] = uint8_t(64 + q / 2 * 8);
        w.ram[size_t(0x203 + q * 4)] = uint8_t(104 + q % 2 * 8);
    }
    w.io[0] |= 4;
    w.io[4] = 1;
    w.io[6] = 4;
    w.capture_layers(true);
    w.run_frame();
    w.run_frame();
    game::PresentationScene flat;
    flat.kind = game::PresentationScene::Kind::Field;
    flat.map = 10;
    game::LightTags tags;
    game::LandmarkTags landmarks;
    game::Mode7World cache;
    std::array<uint32_t, 224 * 144> ui{};
    for (const auto size : {std::array{672, 432}, std::array{1353, 870}}) {
        REQUIRE(gpu.resize(size[0], size[1]));
        for (int step = 0; step < 6; ++step) {
            w.ram[0xD59D] = uint8_t(160 + step);
            REQUIRE(cache.update(w, flat, tags, landmarks));
            CHECK(cache.sprites().size() == 1); // no invented actors or standing field props
            const auto camera = game::field_zoom_camera(1.25f, 1, size[0], size[1], cache.scroll());
            REQUIRE(gpu.draw(cache, camera, false, ui.data()));
            REQUIRE(SDL_SetRenderTarget(renderer.get(), gpu.picture()));
            auto *surface = SDL_RenderReadPixels(renderer.get(), nullptr);
            REQUIRE(surface);
            auto *rgb = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ARGB8888);
            REQUIRE(rgb);
            int errors = 0;
            const int k = int(camera.pixel_scale);
            const int ox = int(camera.origin[0] + cache.scroll()[0]);
            const int oy = int(camera.origin[1] + cache.scroll()[1]);
            for (int y = 0; y < size[1]; ++y) {
                const auto *pixels =
                    reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(rgb->pixels) + y * rgb->pitch);
                for (int x = 0; x < size[0]; ++x) {
                    const int wx = ox + x / k, wy = oy + y / k;
                    const bool actor = wx >= 264 + step && wx < 280 + step && wy >= 224 && wy < 240;
                    const uint32_t expected =
                        actor ? 0xFF00FFu : uint32_t(((wx & 15) + (wy & 15)) % 14 + 1) * 17;
                    errors += (pixels[x] & 0xFFFFFF) != expected;
                }
            }
            INFO("size=" << size[0] << "x" << size[1] << " scroll=" << step << " errors=" << errors);
            CHECK(errors == 0);
            SDL_DestroySurface(rgb);
            SDL_DestroySurface(surface);
            // IPS: the picture itself stays clean (its snapped effects would resample lines away); the
            // border lines are a pass over the finished picture, on the last output pixel of every k x k
            // block, at the world grid's strength (2/3 of a 140/255 line).
            REQUIRE(gpu.draw(cache, camera, false, ui.data(), true));
            auto *lines = SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_XRGB8888,
                                            SDL_TEXTUREACCESS_TARGET, size[0], size[1]);
            REQUIRE(lines);
            REQUIRE(SDL_SetRenderTarget(renderer.get(), lines));
            SDL_SetRenderDrawColor(renderer.get(), 255, 255, 255, 255);
            SDL_RenderClear(renderer.get());
            const SDL_FRect whole{0, 0, float(size[0]), float(size[1])};
            REQUIRE(gpu.draw_field_grid(whole));
            surface = SDL_RenderReadPixels(renderer.get(), nullptr);
            REQUIRE(surface);
            rgb = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ARGB8888);
            REQUIRE(rgb);
            int grid_errors = 0;
            const int line = int(std::lround(255 * (1 - 2.0 / 3 * 115 / 255)));
            const int corner = int(std::lround(255 * (1 - 2.0 / 3 * 115 / 255) * (1 - 2.0 / 3 * 115 / 255)));
            for (int y = 0; y < size[1]; ++y) {
                const auto *pixels =
                    reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(rgb->pixels) + y * rgb->pitch);
                for (int x = 0; x < size[0]; ++x) {
                    const bool across = x % k == k - 1, down = y % k == k - 1;
                    // Actors already carry their own fixed grid, so the ground grid skips them
                    // (the preceding party-pacing fix). This target starts white, without actor art.
                    const int wx = ox + x / k, wy = oy + y / k;
                    const bool actor = wx >= 264 + step && wx < 280 + step && wy >= 224 && wy < 240;
                    const int expected = actor ? 255 : across && down ? corner : across || down ? line : 255;
                    // Different shader branches may round the exact card edge to either adjacent
                    // output texel. Only that one-output-pixel boundary may have either ownership.
                    const int left = (264 + step - ox) * k, top = (224 - oy) * k;
                    const bool boundary =
                        x >= left - 1 && x <= left + 16 * k && y >= top - 1 && y <= top + 16 * k &&
                        (x == left - 1 || x == left + 16 * k || y == top - 1 || y == top + 16 * k);
                    if (boundary && std::abs(int(pixels[x] & 255) - 255) <= 2)
                        continue;
                    grid_errors += std::abs(int(pixels[x] & 255) - expected) > 2;
                }
            }
            INFO("size=" << size[0] << "x" << size[1] << " scroll=" << step
                         << " grid errors=" << grid_errors);
            CHECK(grid_errors == 0);
            SDL_DestroySurface(rgb);
            SDL_DestroySurface(surface);
            SDL_SetRenderTarget(renderer.get(), nullptr);
            SDL_DestroyTexture(lines);
        }
    }
    gpu.shutdown();
    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
}

TEST_CASE("GPU Mode 7 matches the reference plane and upright cards through a swoop [GPU]") {
    if (!std::getenv("SAWGOD_GPU_TESTS"))
        return;
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    auto *window = SDL_CreateWindow("Mode 7 test", 224, 144, SDL_WINDOW_HIDDEN);
    REQUIRE(window);
    PresentationRenderer renderer;
    REQUIRE(renderer.init(window, false, false));
    REQUIRE(renderer.is_gpu());
    Mode7Renderer gpu;
    REQUIRE(gpu.init(renderer.get()));
    auto rom = world_rom();
    rom[0x35B718] = 16;
    std::fill_n(rom.begin() + 0x1080, 128, 0x22);
    std::fill_n(rom.begin() + 0x1100, 128, 0x33);
    std::fill_n(rom.begin() + 0x1180, 128, 0x44);
    rom[0xF2006] = 0x0F;
    rom[0xF2007] = 0;
    rom[0xF2008] = 0xF0;
    rom[0xF2009] = 0;
    emu::Wsc w(rom, 0);
    game::MapData map;
    for (int i = 1; i <= 3; ++i) {
        map.tiles[size_t(i)] = uint8_t(i);
        map.cells[size_t((10 + i) * 80 + 13)] = uint8_t(i);
    }
    REQUIRE(tools::prepare_field_preview(w, 88, map, 0, 0));
    // One native upright actor overlapping the landmark's ground depth.
    std::fill_n(w.ram.begin() + 0x4200, 128, 0x11);
    w.ram[0xFF02] = 0x0F;
    for (int q = 0; q < 4; ++q) {
        w.ram[size_t(0x200 + q * 4)] = uint8_t(0x10 + q);
        w.ram[size_t(0x202 + q * 4)] = uint8_t(42 + q / 2 * 8);
        w.ram[size_t(0x203 + q * 4)] = uint8_t(85 + q % 2 * 8);
    }
    w.io[0] |= 4;
    w.io[4] = 1;
    w.io[6] = 4;
    w.capture_layers(true);
    w.run_frame();
    w.run_frame();
    game::PresentationScene flat;
    flat.kind = game::PresentationScene::Kind::Field;
    flat.map = 88;
    game::LightTags tags;
    game::LandmarkTags landmarks;
    std::string error;
    REQUIRE(landmarks.load(
        "record,id,kind,map,tileset,base_row,extra_shaft_rows,shaft_row,enabled,x,y,metatile,mirror\n"
        "ground,,,88,0,,,,,,,0,\n"
        "cell,test,tower,88,0,6,0,0,1,5,3,1,0\ncell,test,tower,88,0,6,0,0,1,5,4,2,0\ncell,test,tower,88,0,6,"
        "0,0,1,5,5,3,0\n",
        &error));
    game::Mode7World cache;
    REQUIRE(cache.update(w, flat, tags, landmarks));
    const auto landmark_id = [&] {
        auto it = std::find_if(cache.sprites().begin(), cache.sprites().end(),
                               [](const auto &card) { return card.landmark; });
        REQUIRE(it != cache.sprites().end());
        return it->id;
    };
    const auto initial_card = landmark_id();
    w.ram[0xFE1E] ^= 1; // palette index 15 is absent from both landmark and repaired floor
    REQUIRE(cache.update(w, flat, tags, landmarks));
    CHECK(landmark_id() == initial_card);
    w.ram[0xFE04] ^= 1; // index 2 occurs in the painted landmark
    REQUIRE(cache.update(w, flat, tags, landmarks));
    CHECK(landmark_id() != initial_card);
    std::array<uint32_t, 224 * 144> ui{};
    const auto read = [&](SDL_Texture *t) {
        REQUIRE(SDL_SetRenderTarget(renderer.get(), t));
        auto *surface = SDL_RenderReadPixels(renderer.get(), nullptr);
        REQUIRE(surface);
        auto *rgb = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ARGB8888);
        REQUIRE(rgb);
        std::vector<uint32_t> pixels(224 * 144);
        for (int y = 0; y < 144; ++y)
            std::copy_n(reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(rgb->pixels) + y * rgb->pitch),
                        224, pixels.begin() + y * 224);
        SDL_DestroySurface(rgb);
        SDL_DestroySurface(surface);
        SDL_SetRenderTarget(renderer.get(), nullptr);
        return pixels;
    };
    for (const std::string extra : {"0", "1.5", "-1"}) {
        REQUIRE(landmarks.load(
            "record,id,kind,map,tileset,base_row,extra_shaft_rows,shaft_row,enabled,x,y,metatile,mirror\n"
            "ground,,,88,0,,,,,,,0,\n"
            "cell,test,tower,88,0,6," +
                extra +
                ",0,1,5,3,1,0\n"
                "cell,test,tower,88,0,6," +
                extra +
                ",0,1,5,4,2,0\n"
                "cell,test,tower,88,0,6," +
                extra + ",0,1,5,5,3,0\n",
            &error));
        REQUIRE(cache.update(w, flat, tags, landmarks));
        for (float amount : {0.25f, 0.5f, 1.f})
            for (float distance : {1.f, 1.5f}) {
                game::Mode7Camera camera{30 * amount, 24, amount, distance};
                REQUIRE(gpu.draw(cache, camera, false, ui.data()));
                const auto pixels = read(gpu.picture());
                const auto expected = game::mode7_picture(w, flat, tags, camera, false, &landmarks);
                int errors = 0;
                for (size_t i = 64 * 224; i < pixels.size(); ++i) {
                    const auto ground = camera.ground(float(i % 224) + 0.5f, float(i / 224) + 0.5f);
                    const auto next = camera.ground(float(i % 224) + 1.5f, float(i / 224) + 0.5f);
                    if (std::abs(next[0] - ground[0]) > 1)
                        continue; // new minification intentionally differs
                    bool same = true;
                    for (unsigned shift : {0u, 8u, 16u})
                        same &= std::abs(int(pixels[i] >> shift & 255) -
                                         int(expected.pixels[i] >> shift & 255)) <= 1;
                    if (!same)
                        ++errors;
                }
                INFO("amount=" << amount << " distance=" << distance << " differing pixels=" << errors);
                // Only float rounding at exact projected texel boundaries: llvmpipe stays under 8 pixels;
                // real GPUs (RADV) round a few more boundary texels, still under 0.2%.
                CHECK(errors < 64);
            }
    }
    // Native UI ownership stays flat without claiming adjacent scene pixels.
    ui[20 * 224 + 20] = 0xFF0000;
    REQUIRE(gpu.draw(cache, {}, true, ui.data()));
    const auto owner = read(gpu.ownership());
    CHECK((owner[20 * 224 + 20] >> 24 & 1) == 1);
    CHECK((owner[19 * 224 + 19] >> 24 & 4) == 0);
    CHECK((owner[18 * 224 + 18] >> 24 & 4) == 0);
    // Palette edits and a skipped collection still reach the GPU via per-cell revisions.
    w.ram[0xFE02] = 0x56;
    w.ram[0xFE03] = 4;
    REQUIRE(cache.update(w, flat, tags, landmarks));
    REQUIRE(cache.update(w, flat, tags, landmarks));
    REQUIRE(gpu.draw(cache, {}, true, ui.data()));
    CHECK((read(gpu.picture())[0] & 0xFFFFFF) == 0x445566);
    // GPU-generated inputs feed the existing effects directly; their final restore keeps UI flat.
    EnhancedGraphics effects;
    REQUIRE(effects.init(renderer.get(), gpu.picture(), gpu.ownership(), gpu.water()));
    std::vector<uint32_t> ordinary(224 * 144, 0x445566);
    ordinary[20 * 224 + 20] = 0xEEDDCC;
    EnhancedParameters parameters;
    parameters.grade = parameters.bloom = 3;
    parameters.lights.push_back({{20, 20, 32, 0}, {1, 1, 1, 1}});
    REQUIRE(effects.upload(ordinary, ordinary, ui.data(), parameters));
    auto *enhanced = effects.render();
    REQUIRE(enhanced);
    const auto unprotected = read(enhanced);
    auto *flat_texture =
        SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STATIC, 224, 144);
    auto *target =
        SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, 224, 144);
    REQUIRE(flat_texture);
    REQUIRE(target);
    REQUIRE(SDL_UpdateTexture(flat_texture, nullptr, ordinary.data(), 224 * 4));
    REQUIRE(SDL_SetRenderTarget(renderer.get(), target));
    REQUIRE(SDL_RenderTexture(renderer.get(), enhanced, nullptr, nullptr));
    const SDL_FRect destination{0, 0, 224, 144};
    REQUIRE(effects.restore_ui(flat_texture, destination));
    const auto restored = read(target);
    CHECK((restored[20 * 224 + 20] & 0xFFFFFF) == 0xEEDDCC);
    CHECK((restored[19 * 224 + 19] & 0xFFFFFF) == (unprotected[19 * 224 + 19] & 0xFFFFFF));
    effects.shutdown();
    REQUIRE(gpu.resize(672, 432));
    float width = 0, height = 0;
    REQUIRE(SDL_GetTextureSize(gpu.picture(), &width, &height));
    CHECK(width == 672);
    CHECK(height == 432);
    // Cheap proxy: inspect the targets actually shaded, rather than timing a software GPU.
    // The old output-resolution bloom shaded 5.76M pixels here; the ceiling allows no extra
    // full-window effect pass and catches bloom accidentally inheriting the scene's size.
    REQUIRE(gpu.resize(1728, 1111));
    REQUIRE(gpu.draw(cache, {}, true, ui.data()));
    CHECK(gpu.shaded_pixels[1] == 0);
    CHECK(gpu.shaded_pixels[0] + gpu.shaded_pixels[2] < 4100000);
    CHECK(gpu.ownership() == gpu.picture());
    CHECK(gpu.shadow_work.clear_pixels <= 672u * 432u);
    CHECK(Mode7Renderer::ground_shadow_samples == 1);
    REQUIRE(effects.init(renderer.get(), gpu.picture(), gpu.ownership(), gpu.water()));
    REQUIRE(effects.upload(ordinary, ordinary, ui.data(), parameters));
    REQUIRE(effects.render());
    CHECK(effects.last_work.passes <= 6);
    CHECK(effects.last_work.bloom_pixels <= 3 * 112u * 72u);
    CHECK(effects.last_work.shaded_pixels <= 2800000);
    REQUIRE(effects.restore_ui(flat_texture, {0, 0, 1728, 1111}));
    CHECK(effects.ui_regions.pixels < 15000);
    REQUIRE(gpu.draw_ui_grid({0, 0, 1728, 1111}));
    CHECK(gpu.ui_regions.pixels < 15000);
    CHECK(gpu.shaded_pixels[0] + gpu.shaded_pixels[2] + effects.last_work.shaded_pixels +
              effects.ui_regions.pixels + gpu.ui_regions.pixels + 1728u * 1111u <
          8900000);
    effects.shutdown();
    REQUIRE(gpu.resize(672, 432));
    // A packed row exercises all four RGBA bytes and high/low nibbles, in every mirror.
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 4; ++x)
            w.ram[size_t(0x8000 + y * 4 + x)] = uint8_t(0x12 + x * 0x22);
    for (int i = 1; i <= 8; ++i) {
        w.ram[size_t(0xFE00 + i * 2)] = uint8_t(i * 0x11);
        w.ram[size_t(0xFE01 + i * 2)] = uint8_t(i);
    }
    for (int mirror = 0; mirror < 4; ++mirror) {
        for (int q = 0; q < 4; ++q) {
            w.sram[size_t(0x1900 + q * 2)] = 0;
            w.sram[size_t(0x1901 + q * 2)] = uint8_t(0x20 | mirror << 6);
        }
        REQUIRE(cache.update(w, flat, tags, landmarks));
        REQUIRE(gpu.draw(cache, {0, 24, 0, 1}, true, ui.data()));
        REQUIRE(SDL_SetRenderTarget(renderer.get(), gpu.picture()));
        auto *surface = SDL_RenderReadPixels(renderer.get(), nullptr);
        REQUIRE(surface);
        auto *rgb = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ARGB8888);
        REQUIRE(rgb);
        for (int x = 0; x < 8; ++x) {
            const int sx = mirror & 1 ? 7 - x : x;
            const auto *row = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(rgb->pixels) + rgb->pitch);
            CHECK((row[x * 3 + 1] & 0xFFFFFF) == uint32_t((sx + 1) * 0x111111));
        }
        SDL_DestroySurface(rgb);
        SDL_DestroySurface(surface);
        SDL_SetRenderTarget(renderer.get(), nullptr);
    }
    SDL_DestroyTexture(flat_texture);
    SDL_DestroyTexture(target);
    gpu.shutdown();
    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
}

TEST_CASE("GPU Mode 7 closes padded landmark bases and quantizes actors with ground shadows and IPS [GPU]") {
    if (!std::getenv("SAWGOD_GPU_TESTS"))
        return;
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    auto *window = SDL_CreateWindow("Mode 7 contacts", 448, 288, SDL_WINDOW_HIDDEN);
    REQUIRE(window);
    PresentationRenderer renderer;
    REQUIRE(renderer.init(window, false, false));
    REQUIRE(renderer.is_gpu());
    Mode7Renderer gpu;
    REQUIRE(gpu.init(renderer.get()));
    auto rom = world_rom();
    emu::Wsc w(rom, 0);
    game::MapData map;
    map.tiles[1] = 1;
    map.cells[(8 + 5) * 80 + 8 + 5] = 1;
    REQUIRE(tools::prepare_field_preview(w, 88, map, 0, 0));
    // A landmark cutout whose final five rows are the same ground color as its perimeter.
    // This recreates a padded metatile base, without depending on any particular ROM art.
    for (int q : {2, 3})
        for (int y = 3; y < 8; ++y)
            std::fill_n(w.ram.begin() + 0x8000 + (4 + q) * 32 + y * 4, 4, 0x11);
    // Alternating actor columns, visible on the right of the landmark: each must be k pixels wide.
    std::fill_n(w.ram.begin() + 0x4200, 128, 0x12);
    w.ram[0xFF02] = 0x0F;
    w.ram[0xFF03] = 0;
    w.ram[0xFF04] = 0xF0;
    w.ram[0xFF05] = 0;
    for (int actor = 0; actor < 2; ++actor)
        for (int q = 0; q < 4; ++q) {
            const int at = 0x200 + actor * 16 + q * 4;
            w.ram[size_t(at)] = uint8_t(0x10 + q);
            w.ram[size_t(at + 2)] = uint8_t(64 + actor * 32 + q / 2 * 8);
            w.ram[size_t(at + 3)] = uint8_t(136 + actor * 32 + q % 2 * 8);
        }
    w.io[0] |= 4;
    w.io[4] = 1;
    w.io[6] = 8;
    w.capture_layers(true);
    w.run_frame();
    w.run_frame();
    game::PresentationScene flat;
    flat.kind = game::PresentationScene::Kind::Field;
    flat.map = 88;
    game::LightTags lights;
    game::LandmarkTags landmarks;
    std::string error;
    REQUIRE(landmarks.load(
        "record,id,kind,map,tileset,base_row,extra_shaft_rows,shaft_row,enabled,x,y,metatile,mirror\n"
        "ground,,,88,0,,,,,,,0,\ncell,padded,tower,88,0,6,0,0,1,5,5,1,0\n",
        &error));
    game::Mode7World world;
    REQUIRE(world.update(w, flat, lights, landmarks));
    const auto card = std::find_if(world.sprites().begin(), world.sprites().end(),
                                   [](const auto &c) { return c.landmark; });
    REQUIRE(card != world.sprites().end());
    CHECK(card->height == 11);
    CHECK(card->bottom == 11);
    CHECK(card->y == 91);
    const auto state = w.snapshot();
    std::array<uint32_t, 224 * 144> ui{};
    const auto read = [&](SDL_Texture *texture, int width, int height) {
        REQUIRE(SDL_SetRenderTarget(renderer.get(), texture));
        auto *surface = SDL_RenderReadPixels(renderer.get(), nullptr);
        REQUIRE(surface);
        auto *rgb = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ARGB8888);
        REQUIRE(rgb);
        std::vector<uint32_t> pixels(size_t(width * height));
        for (int y = 0; y < height; ++y)
            std::copy_n(reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(rgb->pixels) + y * rgb->pitch),
                        width, pixels.begin() + y * width);
        SDL_DestroySurface(rgb);
        SDL_DestroySurface(surface);
        REQUIRE(SDL_SetRenderTarget(renderer.get(), nullptr));
        return pixels;
    };
    for (int scale : {2, 3}) {
        const int width = 224 * scale, height = 144 * scale;
        REQUIRE(gpu.resize(width, height));
        for (float tilt : {0.f, 15.f, 30.f, 45.f})
            for (float lens : {12.f, 24.f, 40.f})
                for (float distance : {1.f, 1.5f, 3.f})
                    for (float amount : {0.f, 0.5f, 1.f}) {
                        const game::Mode7Camera camera{tilt, lens, amount, distance};
                        REQUIRE(gpu.draw(world, camera, false, ui.data()));
                        const auto pixels = read(gpu.picture(), width, height);
                        const auto foot = camera.project(card->x, card->y);
                        const int x = int(std::floor(foot[0] * scale));
                        // The last output pixel center above the continuous foot must touch the base.
                        const int y = int(std::ceil(foot[1] * scale - 0.5f)) - 1;
                        INFO("tilt=" << tilt << " lens=" << lens << " distance=" << distance
                                     << " amount=" << amount << " scale=" << scale);
                        CHECK((pixels[size_t(y * width + x)] & 0xFFFFFF) == 0xAABBCC);
                        CHECK((pixels[size_t((y + 1) * width + x)] & 0xFFFFFF) == 0x112233);
                        for (int actor = 0; actor < 2; ++actor) {
                            const auto actor_foot =
                                camera.project(float(144 + actor * 32), float(79 + actor * 32));
                            const int k = std::max(1, int(std::floor(actor_foot[2] * scale + 0.5f)));
                            const int ax = int(std::round(actor_foot[0] * scale)) - 8 * k;
                            const int ay = int(std::round(actor_foot[1] * scale)) - 15 * k;
                            int actor_errors = 0;
                            for (int cy = 0; cy < 16 * k; ++cy)
                                for (int cx = 0; cx < 16 * k; ++cx)
                                    actor_errors += (pixels[size_t((ay + cy) * width + ax + cx)] &
                                                     0xFFFFFF) != ((cx / k) % 2 ? 0x00FF00u : 0x0000FFu);
                            CHECK(actor_errors == 0);
                        }
                    }
    }
    REQUIRE(gpu.resize(448, 288));
    for (int scroll = 0; scroll <= 31; ++scroll) {
        w.ram[0xD59D] = uint8_t(scroll);
        w.ram[0xD59F] = uint8_t(scroll);
        REQUIRE(world.update(w, flat, lights, landmarks));
        const game::Mode7Camera camera{45, 40, 1, 1.5f};
        REQUIRE(gpu.draw(world, camera, false, ui.data()));
        const auto pixels = read(gpu.picture(), 448, 288);
        const auto foot = camera.project(float(88 - scroll), float(91 - scroll));
        const int x = int(std::floor(foot[0] * 2));
        const int y = int(std::ceil(foot[1] * 2 - 0.5f)) - 1;
        INFO("scroll=" << scroll);
        CHECK((pixels[size_t(y * 448 + x)] & 0xFFFFFF) == 0xAABBCC);
        CHECK((pixels[size_t((y + 1) * 448 + x)] & 0xFFFFFF) == 0x112233);
    }
    w.restore(state);
    REQUIRE(world.update(w, flat, lights, landmarks));
    REQUIRE(gpu.resize(672, 432));
    const game::Mode7Camera camera{30, 24, 1, 1};
    REQUIRE(gpu.draw(world, camera, false, ui.data(), false, false));
    const auto plain = read(gpu.picture(), 672, 432);
    auto owner_plain = read(gpu.ownership(), 672, 432);
    for (auto &p : owner_plain)
        p &= 0xFF000000;
    const auto water_plain = read(gpu.water(), 672, 432);
    REQUIRE(gpu.draw(world, camera, false, ui.data(), true, true));
    const auto decorated = read(gpu.picture(), 672, 432);
    auto owner_decorated = read(gpu.ownership(), 672, 432);
    for (auto &p : owner_decorated)
        p &= 0xFF000000;
    CHECK(owner_decorated == owner_plain);
    CHECK(read(gpu.water(), 672, 432) == water_plain);
    // Shadows and grid never darken protected UI; actors do not receive their own spot shadows.
    const int actor_x = 144 * 3, actor_y = 79 * 3;
    REQUIRE(gpu.draw(world, camera, false, ui.data(), true, false));
    const auto grid_only = read(gpu.picture(), 672, 432);
    CHECK(decorated[size_t((actor_y - 9) * 672 + actor_x)] ==
          grid_only[size_t((actor_y - 9) * 672 + actor_x)]);
    CHECK(grid_only != plain);
    REQUIRE(gpu.draw(world, camera, false, ui.data(), false, true));
    const auto shadows = read(gpu.picture(), 672, 432);
    CHECK((shadows[size_t((actor_y + 4) * 672 + actor_x)] & 255) <
          (plain[size_t((actor_y + 4) * 672 + actor_x)] & 255));
    CHECK(shadows[size_t((actor_y - 9) * 672 + actor_x)] == plain[size_t((actor_y - 9) * 672 + actor_x)]);
    ui[80 * 224 + 144] = 0xFF0000;
    REQUIRE(gpu.draw(world, camera, false, ui.data(), false, true));
    CHECK((read(gpu.picture(), 672, 432)[size_t(242 * 672 + actor_x)] & 0xFFFFFF) == 0x112233);
    // The UI receives exactly the ordinary flat IPS mask, with no overlay on the ground.
    auto *grid_target =
        SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, 672, 432);
    REQUIRE(grid_target);
    for (float amount : {1.f, 0.5f, 0.75f}) {
        REQUIRE(SDL_SetRenderTarget(renderer.get(), nullptr));
        REQUIRE(gpu.draw(world, camera, false, ui.data(), true, false, amount));
        REQUIRE(SDL_SetRenderTarget(renderer.get(), grid_target));
        REQUIRE(SDL_SetRenderDrawColor(renderer.get(), 255, 255, 255, 255));
        REQUIRE(SDL_RenderClear(renderer.get()));
        REQUIRE(gpu.draw_ui_grid({0, 0, 672, 432}));
        const auto ui_grid = read(grid_target, 672, 432);
        const auto across = ips_mask(672, 224, true, amount), down = ips_mask(432, 144, false, amount);
        uint32_t expected_grid = 0;
        for (unsigned shift : {0u, 8u, 16u})
            expected_grid |= (((across[434] >> shift & 255) * (down[242] >> shift & 255) + 127) / 255)
                             << shift;
        for (unsigned shift : {0u, 8u, 16u})
            CHECK(std::abs(int(ui_grid[242 * 672 + 434] >> shift & 255) -
                           int(expected_grid >> shift & 255)) <= 1);
        CHECK((ui_grid[0] & 0xFFFFFF) == 0xFFFFFF);
    }
    SDL_DestroyTexture(grid_target);
    // Water under the feet removes the entire actor shadow, even where its ellipse reaches dry land.
    std::string water_json =
        R"({"version":4,"tileset":0,"metatiles":[{"metatile":0,"mirror":-1,"water":{"enabled":true,"kind":"still","confidence":1,"evidence":"water contact test","mask":[)";
    for (int row = 0; row < 16; ++row)
        water_json +=
            (row ? "," : "") + std::string(row == 15 ? "\"1000000000000000\"" : "\"0000000000000000\"");
    water_json += "]}}]}";
    REQUIRE(lights.load(0, water_json, &error));
    game::Mode7World wet_world;
    REQUIRE(wet_world.update(w, flat, lights, landmarks));
    CHECK(wet_world.water_at(144, 79));
    CHECK_FALSE(wet_world.water_at(143, 79));
    REQUIRE(gpu.draw(wet_world, camera, false, ui.data(), false, false));
    const auto wet_plain = read(gpu.picture(), 672, 432);
    REQUIRE(gpu.draw(wet_world, camera, false, ui.data(), false, true));
    CHECK(read(gpu.picture(), 672, 432) == wet_plain);
    // The final field IPS pass must not sweep the moving ground grid across a stationary actor.
    auto *actor_grid_target =
        SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, 672, 432);
    REQUIRE(actor_grid_target);
    std::vector<uint32_t> first_actor;
    for (float fraction : {0.f, .17f, .5f, .83f, 1.f}) {
        world.set_motion({fraction, 0}, {{0xD520, {144 + fraction, 79}}});
        const auto flat_camera = game::field_zoom_camera(1.5f, 1, 672, 432, {fraction, 0}, true);
        REQUIRE(gpu.draw(world, flat_camera, false, ui.data(), true, false));
        REQUIRE(SDL_SetRenderTarget(renderer.get(), actor_grid_target));
        REQUIRE(SDL_RenderTexture(renderer.get(), gpu.picture(), nullptr, nullptr));
        REQUIRE(gpu.draw_field_grid({0, 0, 672, 432}));
        const auto composite = read(actor_grid_target, 672, 432);
        std::vector<uint32_t> actor;
        for (int y = 208; y < 240; ++y)
            actor.insert(actor.end(), composite.begin() + y * 672 + 384, composite.begin() + y * 672 + 416);
        if (first_actor.empty())
            first_actor = actor;
        else
            CHECK(actor == first_actor);
    }
    SDL_DestroyTexture(actor_grid_target);
    // A pulled-back 1x picture has subpixel ground cells: IPS must disappear exactly.
    ui.fill(0);
    REQUIRE(gpu.resize(224, 144));
    REQUIRE(gpu.draw(world, {30, 24, 1, 3}, false, ui.data(), false));
    const auto minified = read(gpu.picture(), 224, 144);
    REQUIRE(gpu.draw(world, {30, 24, 1, 3}, false, ui.data(), true));
    CHECK(read(gpu.picture(), 224, 144) == minified);
    CHECK(w.snapshot().ram == state.ram);
    CHECK(w.snapshot().sram == state.sram);
    gpu.shutdown();
    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
}

TEST_CASE("World tilt owns the first fade-in and stable field action frames") {
    emu::Wsc w(world_rom(true), 0);
    game::MapData map;
    REQUIRE(tools::prepare_field_preview(w, 88, map, 10, 10));
    w.capture_layers(true);
    w.run_frame();
    game::SceneObserver observer;
    observer.install(w);
    game::Machine m(w);
    // New game/load, town exit and battle return all run this native fade-in before the field loop.
    for (int route = 0; route < 3; ++route) {
        game::Session::of(w).new_generation();
        w.ram[0xD519] = 1;
        CHECK(observer.describe(w, false, false, false).perspective_allowed);
        w.ram[0xD519] = 0;
        game::hooks::emit(game::hooks::Event::PresentationField, m, {1});
        for (int frame = 0; frame < 20; ++frame) {
            game::hooks::emit(game::hooks::Event::PresentationField, m, {0});
            CHECK(observer.describe(w, false, false, false).perspective_allowed);
            game::hooks::emit(game::hooks::Event::PresentationField, m, {1});
            CHECK(observer.describe(w, false, false, false).perspective_allowed);
        }
    }
    CHECK_FALSE(observer.describe(w, false, true, false).perspective_allowed);
    CHECK(observer.describe(w, false, false, true).perspective_allowed);
    game::hooks::emit(game::hooks::Event::PresentationField, m, {0});
    w.ram[0xD52C] = 1; // outgoing warp script: the same held field is still visible
    CHECK(observer.describe(w, false, false, false).perspective_allowed);
    w.ram[0xD52C] = 0;
    game::hooks::emit(game::hooks::Event::BattleStart, m, {});
    CHECK_FALSE(observer.describe(w, true, false, false).perspective_allowed);
}

TEST_CASE("World entry settles across routes while fidelity retains its half-second fade") {
    game::WorldTiltEntry entry;
    game::Mode7Fade fade;
    using Step = game::FieldZoomEntry::Step;
    uint64_t now = 1'000'000'000;
    auto frame = [&](int map, bool field, bool enhanced) {
        auto step = entry.step(map, field, enhanced);
        if (step != Step::Fade)
            fade.settle(step == Step::Settle);
        return fade.update(field && enhanced, now += 13'250'000);
    };
    for (int world : {88, 102, 122, 136}) {
        CHECK(frame(world, true, true) == 1);
        for (int i = 0; i < 100; ++i)
            CHECK(frame(world, true, true) == 1);
        CHECK(frame(world, true, false) == 1);
        for (int i = 0; i < 40; ++i)
            frame(world, true, false);
        CHECK(frame(world, true, false) == 0);
        CHECK(frame(world, true, true) == 0);
        for (int i = 0; i < 40; ++i)
            frame(world, true, true);
        CHECK(frame(world, true, true) == 1);
        CHECK(frame(world, false, true) == 0); // battle/menu takes the picture away
        CHECK(frame(world, true, true) == 1);  // return: no entry tilt animation
    }
}

TEST_CASE("Party alternating DMA tile buffers share one motion identity") {
    emu::Wsc w(world_rom(), 0);
    game::MapData map;
    REQUIRE(tools::prepare_field_preview(w, 88, map, 10, 10));
    game::PresentationScene flat;
    flat.kind = game::PresentationScene::Kind::Field;
    flat.map = 88;
    game::LightTags tags;
    game::LandmarkTags landmarks;
    game::Mode7World world;
    app::MotionHistory history;
    for (int frame = 0; frame < 32; ++frame) {
        const int bank = (frame & 1) * 4;
        for (int q = 0; q < 4; ++q) {
            const int at = 0x200 + q * 4;
            w.ram[size_t(at)] = uint8_t(0x10 + bank + q);
            w.ram[size_t(at + 1)] = 0;
            w.ram[size_t(at + 2)] = uint8_t(64 + q / 2 * 8);
            w.ram[size_t(at + 3)] = uint8_t(104 + q % 2 * 8);
        }
        w.io[0] |= 4;
        w.io[4] = 1;
        w.io[6] = 4;
        w.ram[0xD59D] = uint8_t(frame);
        w.ram[0xD59E] = 0;
        w.capture_layers(true);
        w.run_frame();
        w.run_frame();
        REQUIRE(world.update(w, flat, tags, landmarks));
        auto party = std::find_if(world.sprites().begin(), world.sprites().end(),
                                  [](const auto &s) { return !s.landmark; });
        REQUIRE(party != world.sprites().end());
        CHECK(party->record == 0xD520);
        app::MotionState state;
        state.frame = uint64_t(frame);
        state.map = 88;
        state.eligible = true;
        state.camera = world.scroll();
        state.actors[party->record] = {party->x, party->y};
        history.complete(state);
        if (frame > 0)
            for (float fraction : {0.f, .17f, .5f, .91f, 1.f}) {
                const auto out = history.sample(fraction);
                CHECK(out.actors.at(0xD520)[0] - out.camera[0] == doctest::Approx(112));
                CHECK(out.actors.at(0xD520)[1] - out.camera[1] == doctest::Approx(79));
            }
    }
    for (int bank : {0x18, 0x1C}) {
        for (int q = 0; q < 4; ++q)
            w.ram[size_t(0x200 + q * 4)] = uint8_t(bank + q);
        w.terrain_cards[1] = {0, uint16_t(bank), false};
        w.publish_terrain_cards();
        w.run_frame();
        w.run_frame();
        REQUIRE(world.update(w, flat, tags, landmarks, false));
        REQUIRE(world.sprites().size() == 1);
        CHECK(world.sprites()[0].record == 0xD5AE);
    }
}

TEST_CASE("GPU world minimap draws whole tiles, darkening and the party dot only in its square [GPU]") {
    if (!std::getenv("SAWGOD_GPU_TESTS"))
        return;
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    auto *window = SDL_CreateWindow("Minimap", 320, 240, SDL_WINDOW_HIDDEN);
    REQUIRE(window);
    PresentationRenderer renderer;
    REQUIRE(renderer.init(window, false, false));
    REQUIRE(renderer.is_gpu());
    Mode7Renderer gpu;
    REQUIRE(gpu.init(renderer.get()));
    CHECK_FALSE(gpu.has_atlas()); // nothing to draw before a world: a no-op, not a failure
    emu::Wsc w(world_rom(), 0);
    game::MapData map;
    map.tiles[1] = 1;
    for (int y = 20; y < 40; ++y)
        for (int x = 10; x < 30; ++x)
            map.cells[size_t((y + 8) * 80 + x + 8)] = 1; // a block of tile 1 on tile-0 ground
    REQUIRE(tools::prepare_field_preview(w, 88, map, 0, 0));
    w.capture_layers(true);
    w.run_frame();
    game::PresentationScene flat;
    flat.kind = game::PresentationScene::Kind::Field;
    flat.map = 88;
    game::LightTags lights;
    game::LandmarkTags landmarks;
    game::Mode7World world;
    REQUIRE(world.update(w, flat, lights, landmarks));
    std::array<uint32_t, 224 * 144> ui{};
    REQUIRE(gpu.draw(world, game::Mode7Camera{16, 24, 1, 1.5f}, false, ui.data()));
    REQUIRE(gpu.has_atlas());
    auto *target =
        SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, 320, 240);
    REQUIRE(target);
    constexpr int kTile = 2, kX = 50, kY = 30;
    const SDL_FRect area{kX, kY, 64 * kTile, 64 * kTile};
    const std::array<int, 2> party{40, 30};
    const auto draw = [&](float opacity, float brightness, float marker) {
        REQUIRE(SDL_SetRenderTarget(renderer.get(), target));
        REQUIRE(SDL_SetRenderDrawColor(renderer.get(), 40, 80, 120, 255));
        REQUIRE(SDL_RenderClear(renderer.get()));
        REQUIRE(gpu.draw_minimap(area, kTile, 0, 0, kTile, party, opacity, brightness, marker, 1));
        auto *surface = SDL_RenderReadPixels(renderer.get(), nullptr);
        REQUIRE(surface);
        auto *rgb = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ARGB8888);
        REQUIRE(rgb);
        std::vector<uint32_t> pixels(320 * 240);
        for (int y = 0; y < 240; ++y)
            std::copy_n(reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(rgb->pixels) + y * rgb->pitch),
                        320, pixels.begin() + y * 320);
        SDL_DestroySurface(rgb);
        SDL_DestroySurface(surface);
        REQUIRE(SDL_SetRenderTarget(renderer.get(), nullptr));
        return pixels;
    };
    const auto at = [](const std::vector<uint32_t> &p, int x, int y) {
        return p[size_t(y * 320 + x)] & 0xFFFFFF;
    };
    const auto channel = [](uint32_t c, int i) { return int(c >> (16 - 8 * i) & 255); };
    const auto full = draw(1, 1, 1);
    // Outside the square nothing changes; inside, every tile is one color over whole window pixels.
    int outside = 0, split = 0;
    for (int y = 0; y < 240; ++y)
        for (int x = 0; x < 320; ++x) {
            const bool in = x >= kX && x < kX + 64 * kTile && y >= kY && y < kY + 64 * kTile;
            if (!in)
                outside += at(full, x, y) != 0x285078;
            else if (std::abs((x - kX) / kTile - party[0]) + std::abs((y - kY) / kTile - party[1]) > 3)
                split +=
                    at(full, x, y) != at(full, kX + (x - kX) / kTile * kTile, kY + (y - kY) / kTile * kTile);
        }
    CHECK(outside == 0);
    CHECK(split == 0);
    // The tile-1 block (world tiles 10..29 x 20..39, inside the atlas's 8-tile border) differs from the
    // ground.
    const uint32_t block = at(full, kX + 12 * kTile, kY + 22 * kTile),
                   ground = at(full, kX + 5 * kTile, kY + 50 * kTile);
    CHECK(block != ground);
    CHECK(at(full, kX + 10 * kTile, kY + 20 * kTile) == block);
    CHECK(at(full, kX + 29 * kTile, kY + 39 * kTile) == block);
    CHECK(at(full, kX + 30 * kTile, kY + 39 * kTile) == ground);
    CHECK(at(full, kX + 9 * kTile, kY + 20 * kTile) == ground);
    // The dot: the party tile only (one tile-sized block here), no outline; its neighbors keep the map.
    const int px = kX + party[0] * kTile, py = kY + party[1] * kTile;
    CHECK(at(full, px, py) == 0xFFFFFF);
    CHECK(at(full, px + kTile - 1, py + kTile - 1) == 0xFFFFFF);
    for (auto [dx, dy] : {std::pair{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {1, 1}})
        CHECK(at(full, px + dx * kTile, py + dy * kTile) != 0xFFFFFF);
    CHECK(at(draw(1, 1, 0.5f), px, py) == 0x808080); // the breath's level
    // Darkening and opacity: 60% brightness; 70% opacity over the picture.
    const auto dark = draw(1, 0.6f, 1), faint = draw(0.7f, 1, 1);
    for (const auto &[x, y] :
         {std::pair{kX + 12 * kTile, kY + 22 * kTile}, {kX + 5 * kTile, kY + 50 * kTile}})
        for (int i = 0; i < 3; ++i) {
            CHECK(std::abs(channel(at(dark, x, y), i) - channel(at(full, x, y), i) * 0.6) <= 1.5);
            CHECK(std::abs(channel(at(faint, x, y), i) -
                           (channel(at(full, x, y), i) * 0.7 + channel(0x285078, i) * 0.3)) <= 1.5);
        }
    SDL_DestroyTexture(target);
    gpu.shutdown();
    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
}

TEST_CASE("Mode 7 contact shadows follow padded and half-alpha actor feet without moving the card") {
    game::Mode7Sprite sprite;
    sprite.width = sprite.height = 16;
    sprite.bottom = 15;
    auto pixels = std::make_shared<std::vector<uint32_t>>(256);
    (*pixels)[12 * 16 + 6] = 0xFF112233;
    sprite.pixels = pixels;
    CHECK(sprite.shadow_offset() == -3.5f);
    (*pixels)[15 * 16 + 9] = 0x80112233; // the party veil still draws feet
    CHECK(sprite.shadow_offset() == -0.5f);
    CHECK(sprite.bottom == 15);
    CHECK(sprite.y == 0);
}

// Standing on see-through terrain the party alternates between four and two sprite entries,
// so a parked vehicle's entries move every frame. When the game's tick slips against the raster, the
// picture still shows the lines drawn from the table before the latched one.
TEST_CASE("Mode 7 UI mask leaves a field actor to the world when its sprite entries moved after its lines "
          "were drawn") {
    emu::Wsc w(world_rom(), 0);
    game::MapData map;
    REQUIRE(tools::prepare_field_preview(w, 88, map, 10, 10));
    std::fill_n(w.ram.begin() + 0x4200, 16 * 32, 0x11); // opaque tiles 0x10-0x1F
    w.ram[0xFF02] = 0x0F;
    const auto entry = [&](int index, int tile, int x, int y) {
        w.ram[size_t(0x200 + index * 4)] = uint8_t(tile);
        w.ram[size_t(0x201 + index * 4)] = 0;
        w.ram[size_t(0x202 + index * 4)] = uint8_t(y);
        w.ram[size_t(0x203 + index * 4)] = uint8_t(x);
    };
    // Four party entries, then the vehicle (NPC slot 4) at entries 4-7.
    for (int q = 0; q < 4; ++q) {
        entry(q, 0x14 + q, 24 + q % 2 * 8, 28 + q / 2 * 8);
        entry(4 + q, 0x1C + q, 24 + q % 2 * 8, 12 + q / 2 * 8);
    }
    w.io[0] |= 4;
    w.io[4] = 1;
    w.io[6] = 8;
    w.terrain_cards = {};
    w.terrain_cards[0] = {0, 0x14, true};
    w.terrain_cards[5] = {4, 0x1C, false};
    w.publish_terrain_cards();
    w.capture_layers(true);
    w.run_frame();
    w.run_frame();
    game::Mode7UiMask cache;
    const size_t top = 14 * 224 + 26, bottom = 22 * 224 + 26; // inside the vehicle's upper and lower halves
    REQUIRE(w.frame_layers()->planes[emu::FrameLayers::SpriteBehind][bottom].sprite == 6);
    CHECK(cache.get(w, false, false)[top] == 0);
    CHECK(cache.get(w, false, false)[bottom] == 0);
    // The next table: two party entries, the vehicle at 2-5 in its other tile buffer. It is latched
    // without a new picture, as on a slipped tick.
    entry(0, 0x10, 24, 28);
    entry(1, 0x11, 32, 28);
    for (int q = 0; q < 4; ++q)
        entry(2 + q, 0x18 + q, 24 + q % 2 * 8, 12 + q / 2 * 8);
    w.io[6] = 6;
    w.terrain_cards[0] = {0, 0x10, true};
    w.terrain_cards[5] = {2, 0x18, false};
    w.publish_terrain_cards();
    w.render_lcd = false;
    w.run_frame();
    REQUIRE(w.presentation_sprites().size() == 24);
    REQUIRE(w.frame_layers()->planes[emu::FrameLayers::SpriteBehind][bottom].sprite == 6);
    // By entry number alone the vehicle's lower half is nobody's and would show as the native sprite.
    CHECK(game::mode7_ui_mask(w, false, false)[bottom] == 0xFF0000);
    CHECK(cache.get(w, false, false)[top] == 0);
    CHECK(cache.get(w, false, false)[bottom] == 0);
}

// Sprites a script draws itself (the crystal and its ring, a boss, a beam) belong to the
// field: the camera places them, in the native table's order around the actors. UI palettes stay UI.
TEST_CASE("Mode 7 world places script-only field sprites and the app's UI mask leaves them to it") {
    emu::Wsc w(world_rom(), 0);
    game::MapData map;
    REQUIRE(tools::prepare_field_preview(w, 88, map, 10, 10));
    std::fill_n(w.ram.begin() + 0x4200, 4 * 32, 0x11); // party tiles 0x10-0x13
    std::fill_n(w.ram.begin() + 0x4600, 4 * 32, 0x11); // script tiles 0x30-0x33
    std::fill_n(w.ram.begin() + 0x5340, 32, 0x11);
    w.ram[0xFF02] = 0x0F; // sprite palettes 8, 9 and 15, color 1
    w.ram[0xFF22] = 0xF0;
    w.ram[0xFFE2] = 0xFF;
    int count = 0;
    const auto entry = [&](int word, int x, int y) {
        w.ram[size_t(0x200 + count * 4)] = uint8_t(word);
        w.ram[size_t(0x201 + count * 4)] = uint8_t(word >> 8);
        w.ram[size_t(0x202 + count * 4)] = uint8_t(y);
        w.ram[size_t(0x203 + count++ * 4)] = uint8_t(x);
    };
    entry(0x0E9A, 8, 8);   // UI palette 15, no NPC buffer tile: stays on the screen
    entry(0x0230, 40, 20); // two touching tiles before the party: one card over the actors
    entry(0x0231, 48, 20);
    for (int q = 0; q < 4; ++q)
        entry(0x10 + q, 104 + q % 2 * 8, 64 + q / 2 * 8);
    entry(0x0232, 152, 100); // after the party: under the actors
    entry(0x0232, 180, 40);  // a second card under the actors, lower on the screen yet behind the first
    for (int column = 0; column < 3; ++column) // three entries on the top edge: drawn out above the screen
        entry(0x0232, 184 + column * 8, 0);
    w.io[0] |= 4;
    w.io[4] = 1;
    w.io[6] = uint8_t(count);
    w.terrain_cards = {};
    w.terrain_cards[0] = {3, 0x10, false};
    w.publish_terrain_cards();
    w.capture_layers(true);
    w.run_frame();
    w.run_frame();
    game::PresentationScene flat;
    flat.kind = game::PresentationScene::Kind::Field;
    flat.map = 88;
    game::LightTags lights;
    game::LandmarkTags landmarks;
    game::Mode7World world;
    REQUIRE(world.update(w, flat, lights, landmarks));
    const auto scroll = world.scroll();
    std::vector<game::Mode7Sprite> script;
    for (const auto &sprite : world.sprites())
        if (sprite.layer)
            script.push_back(sprite);
    REQUIRE(script.size() == 4);
    CHECK(script[0].width == 24);
    CHECK(script[0].height == 8 + 9 * 8);
    CHECK(script[0].y - scroll[1] == 7);
    CHECK(((*script[0].pixels)[0] >> 24) == 0xFF);
    script.erase(script.begin());
    CHECK(world.sprites().front().layer == -1);
    CHECK(world.sprites().back().layer == 1);
    // Painted back to front: the later table entry first, whatever its place on the screen.
    CHECK(script[0].y - scroll[1] == 47);
    const auto &under = script[1], &over = script[2];
    CHECK(over.width == 16);
    CHECK(over.height == 8);
    CHECK(over.bottom == 7);
    CHECK(over.x - scroll[0] == 48); // the card's center column and lowest row, where the game drew it
    CHECK(over.y - scroll[1] == 27);
    CHECK(over.record == 0);
    CHECK(((*over.pixels)[0] >> 24) == 0xFF);
    CHECK(under.width == 8);
    CHECK(under.x - scroll[0] == 156);
    CHECK(under.y - scroll[1] == 107);
    const size_t ui = 10 * 224 + 10, effect = 22 * 224 + 42;
    game::Mode7UiMask cache;
    CHECK(cache.get(w, false, false)[ui] == 0xFF0000);
    CHECK(cache.get(w, false, false)[effect] == 0);
    // A picture drawn without the world keeps the script's sprite as native pixels.
    CHECK(game::mode7_ui_mask(w, false, false)[effect] == 0xFF0000);
    // So does a tick the world was not updated on (a held service screen, a cleared field): the mask follows
    // the request instead of hiding the sprite, and back once the world reads the field again.
    CHECK(cache.get(w, false, false, false, false)[effect] == 0xFF0000);
    CHECK(cache.get(w, false, false, false, false)[ui] == 0xFF0000);
    CHECK(cache.get(w, false, false)[effect] == 0);
}

TEST_CASE("A held Mode 7 world keeps its layout and sprites but takes the live palettes (Inn fade)") {
    emu::Wsc w(world_rom(), 0);
    game::MapData map;
    REQUIRE(tools::prepare_field_preview(w, 88, map, 10, 10));
    std::fill_n(w.ram.begin() + 0x4200, 4 * 32, 0x11); // party tiles 0x10-0x13, color 1
    w.ram[0xFF02] = 0x0F;                              // sprite palette 8, color 1
    for (int q = 0; q < 4; ++q) {
        w.ram[size_t(0x200 + q * 4)] = uint8_t(0x10 + q);
        w.ram[size_t(0x201 + q * 4)] = 0;
        w.ram[size_t(0x202 + q * 4)] = uint8_t(64 + q / 2 * 8);
        w.ram[size_t(0x203 + q * 4)] = uint8_t(104 + q % 2 * 8);
    }
    w.io[0] |= 4;
    w.io[4] = 1;
    w.io[6] = 4;
    w.capture_layers(true);
    w.run_frame();
    w.run_frame();
    game::PresentationScene flat;
    flat.kind = game::PresentationScene::Kind::Field;
    flat.map = 88;
    game::LightTags lights;
    game::LandmarkTags landmarks;
    game::Mode7World world;
    REQUIRE(world.update(w, flat, lights, landmarks));
    const auto scroll = world.scroll();
    const auto sprites = world.sprites();
    const auto palettes = world.surface().palettes;
    const auto revision = world.surface().palette_revision;
    const auto party =
        std::find_if(sprites.begin(), sprites.end(), [](const auto &s) { return s.record == 0xD520; });
    REQUIRE(party != sprites.end());
    REQUIRE(party->paint);
    const auto lit =
        std::find_if(party->pixels->begin(), party->pixels->end(), [](uint32_t c) { return c >> 24; });
    REQUIRE(lit != party->pixels->end());
    const size_t at = size_t(lit - party->pixels->begin());
    CHECK((*party->pixels)[at] != 0xFF000000u);

    // The fade blacks every palette while the menu is open; the field under it must not move.
    const std::vector<uint8_t> saved(w.ram.begin() + 0xFE00, w.ram.begin() + 0x10000);
    std::fill(w.ram.begin() + 0xFE00, w.ram.begin() + 0x10000, uint8_t(0));
    w.ram[0x0201] = 0x02; // the party's OAM changes too: a refresh reads no sprites
    w.run_frame();
    world.refresh_palettes(w);
    CHECK(world.scroll() == scroll);
    REQUIRE(world.sprites().size() == sprites.size());
    CHECK(world.surface().palette_revision == revision + 1);
    CHECK(world.surface().palettes != palettes);
    for (auto c : world.surface().palettes)
        CHECK(c == 0);
    for (size_t i = 0; i < sprites.size(); ++i) {
        const auto &held = world.sprites()[i];
        CHECK(held.record == sprites[i].record);
        CHECK(held.x == sprites[i].x);
        CHECK(held.y == sprites[i].y);
        if (held.record == 0xD520) {
            CHECK(held.id != party->id);
            CHECK((*held.pixels)[at] == 0xFF000000u); // black, still opaque
            for (size_t k = 0; k < held.pixels->size(); ++k)
                CHECK(((*held.pixels)[k] >> 24) == ((*party->pixels)[k] >> 24));
        }
    }
    // Unchanged palettes leave everything as it is.
    const auto dark = world.surface().palette_revision;
    world.refresh_palettes(w);
    CHECK(world.surface().palette_revision == dark);

    // Back to the old colors: the same pixels as before the fade.
    std::copy(saved.begin(), saved.end(), w.ram.begin() + 0xFE00);
    w.run_frame();
    world.refresh_palettes(w);
    CHECK(world.surface().palettes == palettes);
    for (const auto &held : world.sprites())
        if (held.record == 0xD520)
            CHECK(*held.pixels == *party->pixels);
}

TEST_CASE("Mode 7 UI mask follows held frames, coverage provenance and scene kind") {
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0x2000);
    game::Mode7UiMask cache;
    std::array<uint8_t, emu::FrameLayers::Pixels> coverage{};
    w.ui_coverage = &coverage;
    w.capture_layers(true);
    // A deterministic provenance fixture; no machine step or native behavior is involved.
    auto *layers = const_cast<emu::FrameLayers *>(w.frame_layers());
    REQUIRE(layers);
    layers->valid.fill(1);
    CHECK(cache.get(w, false, false) == game::mode7_ui_mask(w, false, false));
    cache.get(w, false, false);
    coverage[0] = 1;
    CHECK(cache.get(w, false, false)[0] == 0xFF0000);
    auto &pixel = layers->planes[emu::FrameLayers::Screen2][1];
    pixel.flags = emu::RasterPixel::Present;
    pixel.palette = 15;
    CHECK(cache.get(w, false, false)[1] == 0xFF0000);
    CHECK(cache.get(w, true, false)[80 * 224] == 0xFF0000);
    CHECK(cache.get(w, false, true) == game::mode7_ui_mask(w, false, true));
    w.ui_coverage = nullptr;
    CHECK(cache.get(w, false, true) == game::mode7_ui_mask(w, false, true));
    w.capture_layers(false);
    CHECK(cache.get(w, false, false) == game::mode7_ui_mask(w, false, false));
}

TEST_CASE("South Town lifts onto grass rather than the mixed sand shoreline") {
    game::LandmarkTags landmarks;
    CHECK_FALSE(landmarks.plain(88, 15, 99)); // shoreline: sand with a strip of grass, not plain ground
    CHECK(landmarks.plain(88, 15, 114));      // actual plain sand remains available
    const char *path = std::getenv("SAWGOD_WSC_ROM");
    if (!path || !*path)
        return;
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.good());
    std::vector<uint8_t> rom{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    emu::Wsc w(std::move(rom), 0);
    const auto map = game::extract_map(88, w);
    REQUIRE(tools::prepare_field_preview(w, 88, map, 47, 44));
    w.capture_layers(true);
    w.run_frame();
    game::PresentationScene flat;
    flat.kind = game::PresentationScene::Kind::Field;
    flat.map = 88;
    game::LightTags lights;
    game::Mode7World world;
    REQUIRE(world.update(w, flat, lights, landmarks));
    const auto ground = world.surface().words;
    for (int x : {53, 54})
        for (int q = 0; q < 4; ++q) {
            const auto encoded = ground[size_t(((48 + 8) * 2 + q / 2) * 160 + (x + 8) * 2 + q % 2)];
            const int word = int((encoded >> 16 & 255) | (encoded >> 8 & 255) << 8);
            const int metatile = (word & 511) / 4;
            CHECK(metatile >= 36);
            CHECK(metatile <= 48);
        }
    // A cache warmed while the camera was up and one first reached by a WSC -> Enhanced step agree.
    for (int frame = 0; frame < 40; ++frame) {
        tools::animate_field_preview(w, frame);
        REQUIRE(world.update(w, flat, lights, landmarks));
    }
    game::Mode7World stepped;
    REQUIRE(stepped.update(w, flat, lights, landmarks));
    CHECK(stepped.surface().words == world.surface().words);
}

TEST_CASE("Field zoom follows loaded field pixels across every event byte and travel state") {
    auto rom = world_rom(true);
    for (int map : {103, 117, 124, 171, 177}) {
        std::copy_n(rom.begin() + 0x35A830 + 88 * 18, 18, rom.begin() + 0x35A830 + map * 18);
        rom[size_t(0x35A830 + map * 18 + 2)] = uint8_t(map); // distinct loaded descriptors
    }
    emu::Wsc w(rom, 0);
    REQUIRE(tools::prepare_field_preview(w, 103, game::MapData{}, 10, 10));
    w.capture_layers(true);
    w.run_frame();
    game::SceneObserver observer;
    observer.install(w);
    game::Machine m(w);
    game::hooks::emit(game::hooks::Event::PresentationField, m, {1});
    game::FieldZoomEntry entry;
    game::Mode7Fade fade;
    int source = -1;
    uint64_t now = 1'000'000'000;
    int shown = 103;
    for (int event = 0; event < 256; ++event) {
        CAPTURE(event);
        game::hooks::emit(game::hooks::Event::PresentationField, m, {0});
        w.ram[0xC826] = uint8_t(event);
        w.ram[0xD52C] = 1;
        w.ram[0xD0D6] = uint8_t(event); // no event or travel allow-list
        w.io[0x10] += 8;                // scripts pan while the ordinary loop is suspended
        for (int map : {103, 117, 124, 171, 177}) {
            w.ram[0xD0DC] = uint8_t(map); // script destinations before the next field-loop hook
            const auto outgoing = observer.describe(w, false, false, true);
            REQUIRE(outgoing.kind == game::PresentationScene::Kind::Field);
            CHECK(outgoing.map == shown); // includes chained warps without an intervening field hook
            std::copy_n(rom.begin() + 0x35A830 + map * 18 + 1, 9, w.ram.begin() + 0xD52D);
            const auto scene = observer.describe(w, false, false, true);
            REQUIRE(scene.kind == game::PresentationScene::Kind::Field);
            REQUIRE(scene.map == map);
            const bool field = game::field_zoom_allowed(map, FidelityMode::Enhanced, true, false);
            REQUIRE(field);
            const auto step = entry.step(map, field, true, false, source, source >= 0, fade.amount(now));
            if (step != game::FieldZoomEntry::Step::Fade)
                fade.settle(step == game::FieldZoomEntry::Step::Settle);
            CHECK(fade.update(field, now += 13'250'000) == 1);
            source = shown = map;
        }
    }
    CHECK(observer.describe(w, false, true, false).kind == game::PresentationScene::Kind::Unknown);
    game::Session::of(w).new_generation();
    CHECK(observer.describe(w, false, false, false).kind == game::PresentationScene::Kind::Unknown);
}

TEST_CASE("CPU Enhanced oracle resolves production world inputs at identity without changing state") {
    auto rom = world_rom();
    for (int at = 0; at < 128; ++at)
        rom[size_t(0x1080 + at)] = at % 7 < 3 ? 0x12 : 0x21;
    emu::Wsc w(std::move(rom), 0);
    game::MapData map;
    map.tiles[1] = 1;
    map.mirror[1] = 3; // asymmetric art exercises both quadrant and texel mirroring
    map.cells.fill(1);
    REQUIRE(tools::prepare_field_preview(w, 88, map, 0, 0));
    w.capture_layers(true, false);
    w.run_frame();
    game::PresentationScene flat;
    flat.kind = game::PresentationScene::Kind::Field;
    flat.map = 88;
    game::LightTags tags;
    game::LandmarkTags landmarks;
    game::Mode7World world;
    const auto ram = w.ram;
    const auto io = w.io;
    const auto sram = w.sram;
    REQUIRE(world.update(w, flat, tags, landmarks));
    auto neutral = game::mode7_neutral_picture(world.surface(), world.sprites(), world.scroll());
    REQUIRE(neutral.pixels.size() == 224 * 144);
    CHECK(neutral.pixels == w.framebuffer_rgb(emu::Wsc::ColorMode::Raw));
    // This must resolve the reconstruction, never use mode7_picture's native-picture shortcut.
    // Change the cached GPU input alone: the oracle must now detect a non-native picture.
    auto surface = world.surface();
    surface.palettes.fill(0x123456);
    auto changed = game::mode7_neutral_picture(surface, world.sprites(), world.scroll());
    CHECK(changed.pixels != neutral.pixels);
    CHECK(w.ram == ram);
    CHECK(w.io == io);
    CHECK(w.sram == sram);
    CHECK(game::mode7_neutral_picture({}, {}, {}).pixels.empty());

    // Fixed-color cards check ownership, foreground occlusion, and the deliberate
    // half-opacity resolve even when a movie prefix never walks through veiled terrain.
    surface.palettes.fill(0xA0A0A0);
    auto pixels = std::make_shared<std::vector<uint32_t>>(256, 0x80002060);
    std::fill_n(pixels->begin(), 128, 0xFF204060);
    game::Mode7Sprite card;
    card.width = card.height = 16;
    card.bottom = 15;
    card.x = world.scroll()[0] + 112;
    card.y = world.scroll()[1] + 79;
    card.record = 0xD520;
    card.pixels = pixels;
    auto actor = game::mode7_neutral_picture(surface, {card}, world.scroll());
    CHECK(actor.pixels[64 * 224 + 104] == 0x204060);
    CHECK(actor.pixels[72 * 224 + 104] == 0x506080);
    CHECK(actor.sources[72 * 224 + 104] == 0xD520);
    card.front = false;
    for (auto &word : surface.words)
        word |= 0x40; // the production surface's foreground ownership byte
    auto hidden = game::mode7_neutral_picture(surface, {card}, world.scroll());
    CHECK(hidden.pixels[72 * 224 + 104] == 0xA0A0A0);
    CHECK(hidden.sources[72 * 224 + 104] == 0);
}

TEST_CASE("Visible scene changes fade the camera while hidden map entries may settle") {
    using Step = game::FieldZoomEntry::Step;
    game::WorldTiltEntry world;
    game::FieldZoomEntry field;
    CHECK(world.step(88, true, true, true) == Step::Settle);
    CHECK(world.step(88, false, true, false) == Step::Fade);
    CHECK(world.step(88, true, true, false) == Step::Fade);
    CHECK(world.step(102, true, true, false) == Step::Fade);
    CHECK(world.step(122, true, true, true) == Step::Settle);
    CHECK(field.step(1, true, true, false, -1, false, 0, true) == Step::Settle);
    CHECK(field.step(1, false, true, false, 1, true, 1, false) == Step::Fade);
    CHECK(field.step(1, true, true, false, 1, true, .7f, false) == Step::Fade);
    CHECK(field.step(2, true, true, false, 1, true, 1, false) == Step::Fade);
    CHECK(field.step(3, true, true, false, 2, true, 1, true) == Step::Settle);
}

TEST_CASE("Scene effects cross neutral continuously on genuine boundaries and hold under windows") {
    game::SceneEffectFade effects;
    game::PresentationScene scene;
    scene.kind = game::PresentationScene::Kind::Field;
    scene.map = 88;
    constexpr uint64_t start = 1'000'000'000;
    CHECK(effects.update(scene, start) == 0);
    CHECK(effects.fading());
    CHECK(effects.update(scene, start + 100'000'000) == 1);
    // Dialogue and service windows keep exactly this field key.
    CHECK(effects.update(scene, start + 200'000'000) == 1);
    CHECK_FALSE(effects.fading());
    scene.kind = game::PresentationScene::Kind::Battle;
    CHECK(effects.update(scene, start + 300'000'000) == 1);
    CHECK(effects.update(scene, start + 350'000'000) == doctest::Approx(.5));
    CHECK(effects.update(scene, start + 400'000'000) == 0);
    CHECK(effects.fading());
    CHECK(effects.update(scene, start + 450'000'000) == doctest::Approx(.5));
    CHECK(effects.update(scene, start + 500'000'000) == 1);
    scene.hold_uniform_picture();
    CHECK(effects.update(scene, start + 600'000'000) == 1);
    CHECK(effects.update(scene, start + 650'000'000) == doctest::Approx(.5));
    scene.kind = game::PresentationScene::Kind::Battle;
    scene.field_cleared = false;
    CHECK(effects.update(scene, start + 650'000'000) == doctest::Approx(.5));
    CHECK(effects.update(scene, start + 750'000'000) == 1);
    // A new session or an independent still has no visible predecessor: full strength at once,
    // then ordinary fades for later boundaries.
    effects.settle_next();
    scene.kind = game::PresentationScene::Kind::Field;
    CHECK(effects.update(scene, start + 800'000'000) == 1);
    CHECK_FALSE(effects.fading());
    scene.map = 89;
    CHECK(effects.update(scene, start + 850'000'000) == 1);
    CHECK(effects.update(scene, start + 900'000'000) == doctest::Approx(.5));
}

TEST_CASE("equal-depth field cards obey native party and NPC slot priority") {
    game::Mode7Surface surface;
    surface.words.resize(160 * 160);
    surface.indices.resize(8192);
    surface.palettes.fill(0xFF000000);
    game::Mode7Sprite party, npc;
    party.record = 0xD520;
    npc.record = 0xD5AE;
    for (auto *card : {&party, &npc}) {
        card->x = 40;
        card->y = 50;
        card->width = card->height = 16;
        card->bottom = 15;
    }
    party.pixels = std::make_shared<std::vector<uint32_t>>(256, 0xFFFF0000);
    npc.pixels = std::make_shared<std::vector<uint32_t>>(256, 0xFF00FF00);
    std::vector<game::Mode7Sprite> cards{party, npc};
    std::stable_sort(cards.begin(), cards.end(), game::mode7_depth_less);
    CHECK(game::mode7_neutral_picture(surface, cards, {0, 0}).pixels[40 * 224 + 40] == 0xFF0000);
    cards[0].y = 51;
    std::stable_sort(cards.begin(), cards.end(), game::mode7_depth_less);
    CHECK(game::mode7_neutral_picture(surface, cards, {0, 0}).pixels[40 * 224 + 40] == 0x00FF00);
}

TEST_CASE("field renderer inputs remain on the raster clock across a VBlank update") {
    emu::Wsc w(world_rom(), 0);
    game::MapData map;
    REQUIRE(tools::prepare_field_preview(w, 88, map, 0, 0));
    auto &session = game::Session::of(w);
    w.capture_layers(true);
    w.run_frame();
    w.run_frame();
    game::PresentationScene flat;
    flat.kind = game::PresentationScene::Kind::Field;
    flat.map = 88;
    game::LightTags lights;
    game::LandmarkTags landmarks;
    game::Mode7World world;
    REQUIRE(world.update(w, flat, lights, landmarks));
    const auto before = game::mode7_neutral_picture(world.surface(), world.sprites(), world.scroll());
    const auto old_scroll = world.scroll();
    // Simulate the next field update, after the displayed raster was completed.
    w.ram[0xD59D] = 20;
    w.ram[0xFE02] = 0xFF;
    w.ram[0xFE03] = 0x0F;
    std::fill_n(w.ram.begin() + 0x8000, 128, 0x11);
    const auto memory = w.snapshot();
    REQUIRE(world.update(w, flat, lights, landmarks));
    CHECK(world.scroll() == old_scroll);
    CHECK(game::mode7_neutral_picture(world.surface(), world.sprites(), world.scroll()).pixels ==
          before.pixels);
    CHECK(w.ram == memory.ram);
    CHECK(w.io == memory.io);
    CHECK(w.sram == memory.sram);
    w.run_frame(); // now those inputs have produced a new native raster
    REQUIRE(world.update(w, flat, lights, landmarks));
    CHECK(world.scroll()[0] == 20);
    CHECK(game::mode7_neutral_picture(world.surface(), world.sprites(), world.scroll()).pixels !=
          before.pixels);
    CHECK(&session.raster_field(w) != &w);
}

TEST_CASE("field world scroll follows uploaded registers across pending steps and ring wrapping") {
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    w.ram[0xD59D] = 0;
    w.ram[0xD59E] = 2; // prepared world x=512, while SCR1 still shows x=511
    w.ram[0xD511] = 0;
    w.io[0x10] = 255;
    w.ram[0xD59F] = 255;
    w.ram[0xD5A0] = 1; // prepared y=511, while SCR1 still shows y=512
    w.ram[0xD512] = 255;
    w.io[0x11] = 0;
    CHECK(game::field_raster_scroll(w) == std::array<float, 2>{511, 512});
    w.io[0x10] = 0;
    w.io[0x11] = 255;
    CHECK(game::field_raster_scroll(w) == std::array<float, 2>{512, 511});
}

TEST_CASE("field subpixels belong to the displayed whole coordinate instead of a pending NPC step") {
    CHECK(game::field_subpixel_position(72, 129, 8) == 72.5f);
    CHECK(game::field_subpixel_position(72, 131, 8) == 72);
    CHECK(game::field_subpixel_position(408, 0, 8) == 408);
    CHECK(game::field_subpixel_position(-1, 0, 15) == -1);
}

TEST_CASE("magnified atlas footprints preserve colors across edits borders and perspective recovery [GPU]") {
    if (!std::getenv("SAWGOD_GPU_TESTS"))
        return;
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    auto *window = SDL_CreateWindow("Atlas footprint", 672, 432, SDL_WINDOW_HIDDEN);
    REQUIRE(window);
    PresentationRenderer renderer;
    REQUIRE(renderer.init(window, false, false));
    Mode7Renderer partial, complete;
    REQUIRE(partial.init(renderer.get()));
    REQUIRE(complete.init(renderer.get()));
    REQUIRE(partial.resize(672, 432));
    REQUIRE(complete.resize(672, 432));
    emu::Wsc w(world_rom(), 0);
    game::MapData map;
    map.tiles[1] = 1;
    map.cells.fill(1);
    REQUIRE(tools::prepare_field_preview(w, 10, map, 10, 10));
    w.capture_layers(true);
    w.run_frame();
    game::PresentationScene flat;
    flat.kind = game::PresentationScene::Kind::Field;
    flat.map = 10;
    game::Mode7World world;
    game::LightTags tags;
    game::LandmarkTags landmarks;
    REQUIRE(world.update(w, flat, tags, landmarks));
    std::array<uint32_t, 224 * 144> ui{};
    const auto read = [&](SDL_Texture *texture) {
        REQUIRE(SDL_SetRenderTarget(renderer.get(), texture));
        auto *raw = SDL_RenderReadPixels(renderer.get(), nullptr);
        REQUIRE(raw);
        auto *rgba = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_ARGB8888);
        REQUIRE(rgba);
        std::vector<uint32_t> pixels(size_t(rgba->w * rgba->h));
        for (int y = 0; y < rgba->h; ++y)
            std::memcpy(pixels.data() + y * rgba->w,
                        static_cast<const uint8_t *>(rgba->pixels) + y * rgba->pitch, size_t(rgba->w) * 4);
        SDL_DestroySurface(rgba);
        SDL_DestroySurface(raw);
        REQUIRE(SDL_SetRenderTarget(renderer.get(), nullptr));
        return pixels;
    };
    game::Mode7Camera camera{0, 24, 1, 1};
    camera.pixel_scale = 3;
    camera.output_scale = {3, 3};
    camera.origin = {-20, -8};
    for (auto scroll : {std::array<float, 2>{160, 160}, {700, 600}, {-256, -256}, {1300, 1300}}) {
        CAPTURE(scroll[0]);
        CAPTURE(scroll[1]);
        world.set_motion(scroll, {});
        // The reference always resolves every atlas texel and every mip before the flat view.
        REQUIRE(complete.draw(world, {}, true, ui.data()));
        REQUIRE(complete.draw(world, camera, true, ui.data()));
        REQUIRE(partial.draw(world, camera, true, ui.data()));
        CHECK(partial.atlas_pixels < 1280u * 1280u);
        CHECK(read(partial.picture()) == read(complete.picture()));
        world.set_motion({scroll[0] + 0.59f, scroll[1]}, {});
        REQUIRE(complete.draw(world, camera, true, ui.data()));
        REQUIRE(partial.draw(world, camera, true, ui.data()));
        CHECK(partial.atlas_pixels == 0); // reuse the slack through an interpolated refresh
        CHECK(read(partial.picture()) == read(complete.picture()));
        const auto palette_revision = world.surface().palette_revision;
        w.ram[0xFE02] ^= 0x0F;
        w.ram[0xFE04] ^= 0x0F;
        REQUIRE(world.update(w, flat, tags, landmarks));
        CHECK(world.surface().palette_revision > palette_revision);
        world.set_motion(scroll, {});
        REQUIRE(complete.draw(world, {}, true, ui.data()));
        REQUIRE(complete.draw(world, camera, true, ui.data()));
        REQUIRE(partial.draw(world, camera, true, ui.data()));
        CHECK(read(partial.picture()) == read(complete.picture()));
    }
    // Recover the entire pyramid after several cropped palette updates; no stale minimap texels.
    REQUIRE(complete.draw(world, {}, true, ui.data()));
    REQUIRE(partial.draw(world, {}, true, ui.data()));
    CHECK(partial.atlas_pixels > 1280u * 1280u);
    CHECK(read(partial.picture()) == read(complete.picture()));
    partial.shutdown();
    complete.shutdown();
    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
}

TEST_CASE("world palette regions match fresh pyramids including filler borders and the minimap [GPU]") {
    if (!std::getenv("SAWGOD_GPU_TESTS"))
        return;
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    auto *window = SDL_CreateWindow("Palette regions", 672, 432, SDL_WINDOW_HIDDEN);
    REQUIRE(window);
    PresentationRenderer renderer;
    REQUIRE(renderer.init(window, false, false));
    Mode7Renderer partial, complete;
    REQUIRE(partial.init(renderer.get()));
    REQUIRE(partial.resize(672, 432));
    emu::Wsc w(world_rom(), 0);
    game::MapData map;
    REQUIRE(tools::prepare_field_preview(w, 88, map, 10, 10));
    w.capture_layers(true);
    w.run_frame();
    game::PresentationScene scene;
    scene.kind = game::PresentationScene::Kind::Field;
    scene.map = 88;
    game::Mode7World world;
    REQUIRE(world.update(w, scene, {}, {}));
    // Synthetic inputs exercise per-index dependencies, flips, and every mip independent of ROM art.
    auto &surface = const_cast<game::Mode7Surface &>(world.surface());
    for (int y = 0; y < 160; ++y)
        for (int x = 0; x < 160; ++x) {
            const unsigned word = (x % 9 < 3 && y % 11 < 4 ? 0x201 : 0) | ((x + y) % 4) << 14;
            surface.words[size_t(y * 160 + x)] = ((word & 255) << 16) | ((word >> 8) << 8);
        }
    surface.indices.assign(8192, 0);
    for (int row = 8; row < 16; ++row)
        surface.indices[size_t(row)] = 0x12345678;
    for (size_t i = 0; i < surface.palettes.size(); ++i)
        surface.palettes[i] = uint32_t((i * 0x732157) & 0xFFFFFF);
    ++surface.revision;
    ++surface.tile_revision;
    ++surface.palette_revision;
    std::array<uint32_t, 224 * 144> ui{};
    const auto read = [&](SDL_Texture *texture) {
        REQUIRE(SDL_SetRenderTarget(renderer.get(), texture));
        auto *raw = SDL_RenderReadPixels(renderer.get(), nullptr);
        REQUIRE(raw);
        auto *rgba = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_ARGB8888);
        REQUIRE(rgba);
        std::vector<uint32_t> pixels(size_t(rgba->w * rgba->h));
        for (int y = 0; y < rgba->h; ++y)
            std::memcpy(pixels.data() + y * rgba->w,
                        static_cast<const uint8_t *>(rgba->pixels) + y * rgba->pitch, size_t(rgba->w) * 4);
        SDL_DestroySurface(rgba);
        SDL_DestroySurface(raw);
        REQUIRE(SDL_SetRenderTarget(renderer.get(), nullptr));
        return pixels;
    };
    auto *minimap =
        SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, 672, 432);
    REQUIRE(minimap);
    const auto map_pixels = [&](Mode7Renderer &gpu) {
        REQUIRE(SDL_SetRenderTarget(renderer.get(), minimap));
        REQUIRE(SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 255));
        REQUIRE(SDL_RenderClear(renderer.get()));
        REQUIRE(gpu.draw_minimap({0, 0, 256, 256}, 4, 0, 0, 4, {20, 20}, 1, 1, 1, 1));
        return read(minimap);
    };
    const game::Mode7Camera camera{16, 24, 1, 1.5f};
    REQUIRE(partial.draw(world, camera, false, ui.data()));
    for (int index : {17, 19, 16, 255, 0, 24, 18}) {
        CAPTURE(index);
        surface.palettes[size_t(index)] ^= 0xFFFFFF;
        ++surface.palette_revision;
        complete.shutdown();
        REQUIRE(complete.init(renderer.get()));
        REQUIRE(complete.resize(672, 432));
        for (auto scroll : {std::array<float, 2>{0, 0}, {-256, -256}, {700, 800}, {1300, 1300}}) {
            world.set_motion(scroll, {});
            REQUIRE(complete.draw(world, camera, false, ui.data()));
            REQUIRE(partial.draw(world, camera, false, ui.data()));
            CHECK(partial.atlas_pixels < 2182400u);
            if (index == 16 || index == 255)
                CHECK(partial.atlas_pixels == 0); // absent color index, even in a used palette
            CHECK(read(partial.picture()) == read(complete.picture()));
            CHECK(map_pixels(partial) == map_pixels(complete));
        }
    }
    SDL_DestroyTexture(minimap);
    partial.shutdown();
    complete.shutdown();
    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
}

TEST_CASE("capped Mode 7 actor sizes retain the output grid [GPU]") {
    if (!std::getenv("SAWGOD_GPU_TESTS"))
        return;
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    auto *window = SDL_CreateWindow("Actor output grid", 2240, 1440, SDL_WINDOW_HIDDEN);
    REQUIRE(window);
    PresentationRenderer renderer;
    REQUIRE(renderer.init(window, false, false));
    Mode7Renderer full, scaled;
    REQUIRE(full.init(renderer.get()));
    REQUIRE(scaled.init(renderer.get()));
    REQUIRE(full.resize(2240, 1440));
    REQUIRE(scaled.resize(1120, 720));
    emu::Wsc w(world_rom(), 0);
    game::MapData map;
    REQUIRE(tools::prepare_field_preview(w, 88, map, 10, 10));
    w.capture_layers(true);
    w.run_frame();
    game::PresentationScene scene;
    scene.kind = game::PresentationScene::Kind::Field;
    scene.map = 88;
    game::Mode7World world;
    REQUIRE(world.update(w, scene, {}, {}));
    auto &sprites = const_cast<std::vector<game::Mode7Sprite> &>(world.sprites());
    sprites.clear();
    game::Mode7Sprite actor;
    actor.id = 1;
    actor.record = 0xD520;
    actor.x = world.scroll()[0] + 112;
    actor.y = world.scroll()[1] + 79;
    actor.width = 8;
    actor.height = actor.bottom = 12;
    actor.pixels = std::make_shared<const std::vector<uint32_t>>(8 * 12, 0xFFFFFFFFu);
    sprites.push_back(actor);
    std::array<uint32_t, 224 * 144> ui{};
    const game::Mode7Camera camera{16, 24, 1, 1.5f};
    REQUIRE(full.draw(world, camera, false, ui.data()));
    REQUIRE(scaled.draw(world, camera, false, ui.data(), false, false, 1, 1, 2));
    const auto bounds = [&](SDL_Texture *texture) {
        REQUIRE(SDL_SetRenderTarget(renderer.get(), texture));
        auto *raw = SDL_RenderReadPixels(renderer.get(), nullptr);
        REQUIRE(raw);
        auto *rgba = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_ARGB8888);
        REQUIRE(rgba);
        int left = rgba->w, top = rgba->h, right = -1, bottom = -1;
        for (int y = 0; y < rgba->h; ++y) {
            const auto *row = reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(rgba->pixels) +
                                                                 y * rgba->pitch);
            for (int x = 0; x < rgba->w; ++x)
                if ((row[x] >> 24) & 8) {
                    left = std::min(left, x);
                    top = std::min(top, y);
                    right = std::max(right, x);
                    bottom = std::max(bottom, y);
                }
        }
        SDL_DestroySurface(rgba);
        SDL_DestroySurface(raw);
        REQUIRE(SDL_SetRenderTarget(renderer.get(), nullptr));
        REQUIRE(right >= left);
        return SDL_Rect{left, top, right - left + 1, bottom - top + 1};
    };
    const auto original = bounds(full.picture()), capped = bounds(scaled.picture());
    // A 2/3 perspective scale at 10x native rounds to seven output pixels per art pixel.
    // Rounding on the 5x scene grid instead would shrink this to six after the 2x copy.
    CHECK(original.w == 56);
    CHECK(original.h == 84);
    CHECK(capped.w * 2 == original.w);
    CHECK(capped.h * 2 == original.h);
    CHECK(capped.x * 2 == original.x);
    CHECK(capped.y * 2 == original.y);
    full.shutdown();
    scaled.shutdown();
    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
}
