#include "game/game_config.h"
#include "test_graphics_transitions_support.h"

TEST_CASE("[ROM] Mode 7 yields the native forty-frame battle entry outside field dispatch") {
    const auto rom = transition_rom();
    if (rom.empty())
        return;
    for (int travel = 0; travel <= 3; ++travel) {
        CAPTURE(travel);
        emu::Wsc w(rom, 0x8000);
        boot_transition(w);
        queue_event(w, 5, 120);
        reach_transition(w, 88);
        w.ram[0xD0D6] = uint8_t(travel);
        w.render_lcd = true;
        w.capture_layers(true);
        advance(w);
        game::SceneObserver observer;
        observer.install(w);
        REQUIRE(observer.describe(w, false, false, false).perspective_allowed);
        // Script-style call: invoke the actual animation without a pending field
        // event. The ordinary-field flag stays clear, and BattleStart has not run.
        game::reset_natives(w);
        game::Machine m(w);
        m.cs() = 0x5000;
        w.cpu.s.ip = 0x10C6;
        w.cpu.s.halted = false;
        m.sp() = uint16_t(m.sp() - 4);
        m.wr16(m.ss(), m.sp(), 0x0100);
        m.wr16(m.ss(), uint16_t(m.sp() + 2), 0x5000);
        bool returned = false;
        w.set_native(0x350100, [&](emu::Wsc &hw) {
            returned = true;
            hw.cpu.s.halted = true;
            return 1;
        });
        int wipe_frames = 0;
        for (int frame = 0; frame < 80 && !returned; ++frame) {
            advance(w);
            if (w.io[0xB2] & 0x10) {
                ++wipe_frames;
                CAPTURE(frame);
                CHECK_FALSE(observer.describe(w, false, false, false).perspective_allowed);
            }
        }
        CHECK(returned);
        CHECK(wipe_frames >= 39);
    }
}

TEST_CASE("[ROM] GB world entries show GB field pixels or native black on every frame") {
    const auto rom = transition_rom();
    if (rom.empty())
        return;
    struct Entry {
        int world, warp, exit, destination;
    };
    for (const auto entry : {Entry{88, 120, 1, 92}, Entry{88, 120, 4, 93}, Entry{88, 120, 5, 94},
                             Entry{102, 93, 0, 100}, Entry{122, 47, 2, 124}, Entry{136, 121, 1, 149}}) {
        CAPTURE(entry.world);
        CAPTURE(entry.destination);
        std::map<std::string, std::string> assets;
        const auto root = std::filesystem::path(SAWGOD_TEST_FIXTURES).parent_path().parent_path();
        for (int map : {entry.world, entry.destination})
            for (const char *kind : {"strip", "lookup", "cells"}) {
                char name[64];
                std::snprintf(name, sizeof name, "%03d_%s.csv", map, kind);
                std::ifstream in(root / "assets/gb_maps" / name, std::ios::binary);
                assets[name] = {std::istreambuf_iterator<char>(in), {}};
            }
        std::map<std::string, std::vector<uint8_t>> files;
        std::string error;
        REQUIRE(game::build_gb_field_maps(rom, assets, &files, &error));
        game::GfxSwap swap;
        for (int map : {entry.world, entry.destination})
            REQUIRE(swap.load_field_map(map, [&](const auto &name) { return files[name]; }, &error));
        emu::Wsc w(rom, 0x8000);
        boot_transition(w, &swap);
        queue_event(w, 5, entry.warp);
        reach_transition(w, entry.world, &swap);
        w.render_lcd = true;
        queue_event(w, 13, entry.exit);
        int destination_frames = 0;
        for (int frame = 0; frame < 180; ++frame) {
            CAPTURE(frame);
            advance(w, &swap);
            const auto pixels = w.framebuffer_rgb(emu::Wsc::ColorMode::Raw, true);
            REQUIRE(std::all_of(pixels.begin(), pixels.end(), [](uint32_t p) {
                return (p & 255) == (p >> 8 & 255) && (p & 255) == (p >> 16 & 255);
            }));
            destination_frames += w.ram[0xD0DC] == entry.destination && (w.io[0] & 1);
        }
        CHECK(w.ram[0xD0DC] == entry.destination);
        CHECK(destination_frames > 30);
    }
}

#include "app/battle_wipe.h"
#include <SDL3/SDL.h>
TEST_CASE("Enhanced battle wipe shifts the retained picture under the native closing window") {
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    auto *window = SDL_CreateWindow("Battle wipe", 224, 144, SDL_WINDOW_HIDDEN);
    REQUIRE(window);
    auto *renderer = SDL_CreateRenderer(window, "software");
    REQUIRE(renderer);
    auto *source = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STATIC, 224, 144);
    REQUIRE(source);
    std::vector<uint32_t> pixels(224 * 144);
    for (int y = 0; y < 144; ++y)
        for (int x = 0; x < 224; ++x)
            pixels[size_t(y * 224 + x)] = uint32_t(x << 16 | y << 8 | 0x80);
    REQUIRE(SDL_UpdateTexture(source, nullptr, pixels.data(), 224 * 4));
    app::BattleWipe wipe;
    CHECK_FALSE(wipe.available(1, 1));
    REQUIRE(wipe.begin(renderer, 224, 144, 1, 1, {0, 0, 224, 144}));
    SDL_RenderTexture(renderer, source, nullptr, nullptr);
    wipe.end(renderer);
    CHECK(wipe.available(1, 1));
    CHECK_FALSE(wipe.available(2, 1));
    CHECK_FALSE(wipe.available(1, 2));
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    w.cpu.s.halted = true;
    w.cpu.s.flags = 0;
    w.io[0x60] = 0xE0;
    w.io[0] = 0x33;
    w.io[7] = 2;
    w.io[8] = w.io[9] = 10;
    w.io[10] = 200;
    w.io[11] = 120;
    w.io[0x10] = 4;
    w.io[0x11] = 1;
    std::fill_n(w.ram.begin() + 0x4000 + 2 * 32, 32, 0x22);
    for (int i = 0; i < 32 * 32; ++i)
        w.ram[size_t(i * 2)] = 2;
    w.capture_layers(true);
    w.run_frame();
    w.run_frame();
    const auto ram = w.ram;
    const auto io = w.io;
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    wipe.draw(renderer, w, {0, 0, 224, 144}, 1);
    auto *surface = SDL_RenderReadPixels(renderer, nullptr);
    REQUIRE(surface);
    auto *rgb = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_XRGB8888);
    REQUIRE(rgb);
    const auto pixel = [&](int x, int y) {
        return reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(rgb->pixels) +
                                                  y * rgb->pitch)[x] &
               0xFFFFFF;
    };
    CHECK(pixel(20, 20) == pixels[21 * 224 + 24]);
    CHECK(pixel(5, 20) == 0);
    CHECK(pixel(20, 5) == 0);
    CHECK(w.ram == ram);
    CHECK(w.io == io);
    SDL_DestroySurface(rgb);
    SDL_DestroySurface(surface);
    wipe.release();
    SDL_DestroyTexture(source);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

TEST_CASE("[ROM] the field camera survives chest overlays and a scripted battle return before walking") {
    const auto rom = transition_rom();
    if (rom.empty())
        return;
    emu::Wsc w(rom, 0x8000);
    game::SceneObserver observer;
    observer.install(w);
    boot_transition(w);
    w.render_lcd = true;
    w.capture_layers(true);
    advance(w);
    REQUIRE(game::recognized_field(w));
    const int map = w.ram[0xD0DC];
    REQUIRE_FALSE(game::GameConfig::world_map(map));
    game::Machine m(w);
    game::hooks::emit(game::hooks::Event::PresentationField, m, {0});
    // A chest/event changes SCR2's base and may update the scroll while dispatch is parked.
    // Neither is a change to the loaded field or a reason to lower its flat camera.
    w.ram[0xD52C] = 1;
    w.ram[0xD519] = 0;
    w.io[7] ^= 0x10;
    ++w.io[0x10];
    auto scene = observer.describe(w, false, false, true);
    CHECK(scene.kind == game::PresentationScene::Kind::Field);
    CHECK(game::field_zoom_allowed(map, FidelityMode::Enhanced, true, false));
    game::hooks::emit(game::hooks::Event::BattleStart, m);
    CHECK(observer.describe(w, true, false, true).kind == game::PresentationScene::Kind::Battle);
    game::hooks::emit(game::hooks::Event::BattleRewards, m);
    game::hooks::emit(game::hooks::Event::BattleEnd, m);
    // The story dispatcher resumes without a PresentationField hook until the next player step.
    scene = observer.describe(w, false, false, true);
    CHECK(scene.kind == game::PresentationScene::Kind::Field);
    // A destination id, lost descriptor or new session still cannot reuse that field identity.
    w.ram[0xD52D] ^= 1;
    // A mutation after scanout cannot change the displayed field. It becomes authoritative
    // when the next raster starts, as a real descriptor upload does.
    CHECK(observer.describe(w, false, false, true).kind == game::PresentationScene::Kind::Field);
    game::Session::of(w).apply_scene_settings(w);
    CHECK(observer.describe(w, false, false, true).kind == game::PresentationScene::Kind::Transition);
    w.ram[0xD52D] ^= 1;
    game::Session::of(w).new_generation();
    CHECK(observer.describe(w, false, false, true).kind == game::PresentationScene::Kind::Unknown);
}

TEST_CASE("[ROM] GB service returns keep coordinate-owned field art after the ring is rebased") {
    const auto rom = transition_rom();
    if (rom.empty())
        return;
    const auto root = std::filesystem::path(SAWGOD_TEST_FIXTURES).parent_path().parent_path();
    std::map<std::string, std::string> assets;
    for (int map : {1, 87})
        for (const char *kind : {"strip", "lookup", "cells"}) {
            char name[64];
            std::snprintf(name, sizeof name, "%03d_%s.csv", map, kind);
            std::ifstream in(root / "assets/gb_maps" / name, std::ios::binary);
            assets[name] = {std::istreambuf_iterator<char>(in), {}};
        }
    std::map<std::string, std::vector<uint8_t>> files;
    std::string error;
    REQUIRE(game::build_gb_field_maps(rom, assets, &files, &error));
    game::GfxSwap swap;
    for (int map : {1, 87})
        REQUIRE(swap.load_field_map(map, [&](const auto &name) { return files[name]; }, &error));
    emu::Wsc w(rom, 0x8000);
    boot_transition(w, &swap);
    queue_event(w, 5, 244);
    reach_transition(w, 87, &swap);
    w.set_keys(emu::KEY_X1);
    for (int frame = 0; frame < 48; ++frame)
        advance(w, &swap);
    w.set_keys(0);
    for (int frame = 0; frame < 32; ++frame)
        advance(w, &swap);
    REQUIRE((w.io[0x10] != 8 || w.io[0x11] != 0));
    const auto tile_pixels = [&](uint16_t at) {
        const int word = w.shadow_read8(at) | w.shadow_read8(uint16_t(at + 1)) << 8;
        const int tile = (word & 511) | (word & 0x2000 ? 512 : 0);
        std::array<uint8_t, 64> pixels{};
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                const int sx = word & 0x4000 ? 7 - x : x;
                const int sy = word & 0x8000 ? 7 - y : y;
                const int byte = w.shadow_read8(uint16_t(0x4000 + tile * 32 + sy * 4 + sx / 2));
                pixels[size_t(y * 8 + x)] = uint8_t(sx & 1 ? byte & 15 : byte >> 4);
            }
        return pixels;
    };
    for (int service : {240, 241, 242, 4}) {
        CAPTURE(service);
        std::vector<std::array<uint8_t, 64>> ring;
        for (int i = 0; i < 1024; ++i)
            ring.push_back(tile_pixels(uint16_t(0x1000 + i * 2)));
        const int dx = (uint8_t(w.io[0x10] - 8) & 0xF0) / 8;
        const int dy = (w.io[0x11] & 0xF0) / 8;
        queue_event(w, 4, service);
        for (int frame = 0; frame < 200; ++frame) {
            w.set_keys(frame > 60 && frame % 20 < 6 ? emu::KEY_B : 0);
            advance(w, &swap);
        }
        w.set_keys(0);
        REQUIRE(w.io[0x10] == 8);
        REQUIRE(w.io[0x11] == 0);
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) {
                const int source = ((y + dy) % 32) * 32 + (x + dx) % 32;
                const int dest = 0x1000 + (y * 32 + x) * 2;
                CHECK(tile_pixels(uint16_t(dest)) == ring[size_t(source)]);
            }
    }
}

TEST_CASE("Enhanced battle wipe clears its retained bars when the picture shrinks") {
    // Touch controls switch to whole-pixel scaling: the picture shrinks inside the same output size. The
    // retained target must not copy the old, larger picture back into the new black bars.
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    auto *window = SDL_CreateWindow("Battle wipe bars", 448, 144, SDL_WINDOW_HIDDEN);
    REQUIRE(window);
    auto *renderer = SDL_CreateRenderer(window, "software");
    REQUIRE(renderer);
    auto *out = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, 448, 144);
    REQUIRE(out);
    app::BattleWipe wipe;
    REQUIRE(SDL_SetRenderTarget(renderer, out));
    REQUIRE(wipe.begin(renderer, 448, 144, 1, 1, {0, 0, 448, 144}));
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderFillRect(renderer, nullptr); // the old picture covers the whole output
    wipe.end(renderer);
    REQUIRE(wipe.begin(renderer, 448, 144, 1, 1, {112, 0, 224, 144}));
    const SDL_FRect picture{112, 0, 224, 144};
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderFillRect(renderer, &picture); // the new, narrower picture
    wipe.end(renderer);
    SDL_Surface *s = SDL_RenderReadPixels(renderer, nullptr);
    REQUIRE(s);
    Uint8 r = 0, g = 0, b = 0;
    for (int x : {0, 50, 111, 336, 400, 447}) {
        REQUIRE(SDL_ReadSurfacePixel(s, x, 72, &r, &g, &b, nullptr));
        CHECK(int(r) + g + b == 0);
    }
    REQUIRE(SDL_ReadSurfacePixel(s, 224, 72, &r, &g, &b, nullptr));
    CHECK(r == 255);
    SDL_DestroySurface(s);
    wipe.release();
    SDL_DestroyTexture(out);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
}
