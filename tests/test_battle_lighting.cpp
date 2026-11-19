#include "engine/battle_lighting.h"
#include "engine/settings.h"
#include <doctest/doctest.h>

TEST_CASE("battle lighting target pulse crossfade enemy lift and Enhanced option gate") {
    using namespace sawgod;
    CHECK(BattleLighting::pulse(0.3) == doctest::Approx(2.2));
    CHECK(BattleLighting::pulse(0.9) == doctest::Approx(1));
    CHECK(BattleLighting::pulse(1.5) == doctest::Approx(2.2));
    BattleTargetLight target;
    const auto first = target.gains(1, 0.3);
    CHECK(first == std::array{1.f, 2.2f, 1.f});
    const auto moved = target.gains(0, 0.3);
    CHECK(moved == first);
    const auto half = target.gains(0, 0.425);
    const float amplitude = BattleLighting::pulse(0.425) - 1;
    CHECK(half[0] == doctest::Approx(1 + amplitude / 2));
    CHECK(half[1] == doctest::Approx(half[0]));
    const auto end = target.gains(0, 0.55);
    CHECK(end[0] == doctest::Approx(BattleLighting::pulse(0.55)));
    CHECK(end[1] == 1);
    // A rapid second move starts at the current weights, without a jump.
    CHECK(target.gains(2, 0.55) == end);
    CHECK(target.gains(2, 0.80)[2] == doctest::Approx(BattleLighting::pulse(0.80)));
    CHECK(target.gains(-1, 0.81) == std::array{1.f, 1.f, 1.f});
    CHECK(target.gains(1, 0.82)[1] == doctest::Approx(BattleLighting::pulse(0.82)));
    CHECK(BattleLighting::enemy_lift(2.2f, 0, 0) == doctest::Approx(0.18));
    CHECK(BattleLighting::enemy_lift(1, 0, 0) == 0);
    CHECK(BattleLighting::enemy_lift(2.2f, 400, 480) < 0.000001f);
    BattleLighting light;
    light.groups[1] = {112, 40, 60, 2.2f};
    const std::array color{0.4f, 0.3f, 0.2f};
    Config config;
    Settings settings;
    settings.register_all(config);
    REQUIRE(settings.battle_lighting->on());
    // Same gate as the app: zero outside Enhanced or when the option is disabled.
    for (float enhanced : {0.f, 0.5f, 1.f}) {
        for (bool enabled : {false, true}) {
            config.set(*settings.battle_lighting, enabled ? 1 : 0);
            light.weight = BattleLighting::strength(settings.battle_lighting->on(), true, enhanced);
            CHECK(BattleLighting::strength(settings.battle_lighting->on(), false, enhanced) == 0);
            CHECK(BattleLighting::strength(settings.battle_lighting->on(), true, enhanced, 1) == 0);
            const auto result = light.enemy(color, 112, 40);
            if (!enabled || enhanced == 0)
                CHECK(result == color);
            else {
                CHECK(result[0] == doctest::Approx(0.4f + (0.4f * 0.18f + 0.18f * 0.25f * 0.12f) * enhanced));
                CHECK(result[1] == doctest::Approx(0.3f + (0.3f * 0.18f + 0.18f * 0.25f * 0.06f) * enhanced));
                CHECK(result[2] == doctest::Approx(0.2f + 0.2f * 0.18f * enhanced));
            }
        }
    }
}

#include "engine/enhanced_graphics.h"
#include "engine/presentation_renderer.h"
#include <cstdlib>
#include <cstring>
TEST_CASE("GPU battle lighting preserves UI and effect art and applies spatial target lift [GPU]") {
    using namespace sawgod;
    if (!std::getenv("SAWGOD_GPU_TESTS")) {
        MESSAGE("set SAWGOD_GPU_TESTS with a GPU-capable SDL driver");
        return;
    }
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    auto *window = SDL_CreateWindow("Battle lighting", 1344, 864, SDL_WINDOW_HIDDEN);
    REQUIRE(window);
    PresentationRenderer renderer;
    REQUIRE(renderer.init(window, false, false));
    REQUIRE(renderer.is_gpu());
    EnhancedGraphics pass;
    REQUIRE(pass.init(renderer.get()));
    EnhancedParameters params;
    params.shadows = false;
    params.grade = params.bloom = params.water_strength = params.light_strength = 0;
    params.battle_lighting.groups = {{{48, 40, 60, 1}, {112, 40, 60, 2.2f}, {176, 40, 60, 1}}};
    std::vector<uint32_t> source(224 * 144, 0x404040), mask(source.size(), 0);
    for (int y = 0; y < 144; ++y)
        for (int x = 0; x < 224; ++x) {
            const size_t at = size_t(y * 224 + x);
            if (y >= 80 || (y >= 62 && y < 78 && x >= 90 && x < 134) || y < 16)
                mask[at] = 0xFF0000;
            else if (y >= 30 && y < 50 && ((x >= 40 && x < 56) || (x >= 104 && x < 120)))
                mask[at] = 0x00FF00;
        }
    mask[40 * 224 + 110] = 0x00FE00; // effect over the selected enemy
    mask[40 * 224 + 100] = 0x004000; // effect over the room
    params.background = source;
    params.background[20 * 224 + 200] = 0xFFFFFF; // full scene exposure
    auto *ordinary =
        SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STATIC, 224, 144);
    auto *out =
        SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, 1344, 864);
    REQUIRE(ordinary);
    REQUIRE(out);
    REQUIRE(SDL_UpdateTexture(ordinary, nullptr, source.data(), 224 * 4));
    const auto draw = [&](float weight) {
        params.battle_lighting.weight = weight;
        REQUIRE(pass.upload(source, source, mask.data(), params));
        auto *scene = pass.render();
        REQUIRE(scene);
        REQUIRE(SDL_SetRenderTarget(renderer.get(), out));
        REQUIRE(pass.present(scene, ordinary, {0, 0, 1344, 864}));
        auto *surface = SDL_RenderReadPixels(renderer.get(), nullptr);
        REQUIRE(surface);
        auto *rgb = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_XRGB8888);
        REQUIRE(rgb);
        std::vector<uint32_t> pixels(1344 * 864);
        for (int y = 0; y < 864; ++y)
            std::memcpy(pixels.data() + y * 1344, static_cast<uint8_t *>(rgb->pixels) + y * rgb->pitch,
                        1344 * 4);
        SDL_DestroySurface(rgb);
        SDL_DestroySurface(surface);
        REQUIRE(SDL_SetRenderTarget(renderer.get(), nullptr));
        return pixels;
    };
    const auto off = draw(0), on = draw(1), half = draw(0.5f);
    const auto at = [](const auto &pixels, int x, int y) {
        return pixels[size_t((y * 6 + 3) * 1344 + x * 6 + 3)] & 0xFFFFFF;
    };
    // The Gaussian also spills a faint target lift onto nearby enemy pixels.
    CHECK(at(on, 48, 40) > at(off, 48, 40));
    CHECK(at(on, 48, 40) < at(on, 112, 40));
    CHECK(at(on, 112, 40) > at(off, 112, 40));
    CHECK(at(on, 110, 40) == at(off, 110, 40));
    CHECK(at(on, 100, 40) == at(off, 100, 40));
    CHECK(at(on, 112, 68) == at(off, 112, 68)); // plate
    CHECK(at(on, 112, 112) == at(off, 112, 112));
    CHECK(at(on, 112, 12) == at(off, 112, 12));
    CHECK(at(on, 8, 75) < at(off, 8, 75));    // corners dim
    CHECK(at(on, 48, 59) <= at(off, 48, 59)); // no floor pools: the room only dims
    params.battle_lighting.weight = 1;
    const auto expected = params.battle_lighting.enemy({64.f / 255, 64.f / 255, 64.f / 255}, 112.5f, 40.5f);
    for (int channel = 0; channel < 3; ++channel) {
        const int shift = (2 - channel) * 8;
        CHECK(float(at(on, 112, 40) >> shift & 255) ==
              doctest::Approx(expected[size_t(channel)] * 255).epsilon(0.02));
        const float mid = float((at(off, 112, 40) >> shift & 255) + (at(on, 112, 40) >> shift & 255)) / 2;
        CHECK(float(at(half, 112, 40) >> shift & 255) == doctest::Approx(mid).epsilon(0.03));
    }
    // The option/outside-Enhanced gate returns the byte-identical baseline after an active frame.
    CHECK(draw(0) == off);
    size_t changed_ui = 0;
    for (int y = 0; y < 864; ++y)
        for (int x = 0; x < 1344; ++x)
            if (mask[size_t((y / 6) * 224 + x / 6)] & 0xFF0000)
                changed_ui += on[size_t(y * 1344 + x)] != off[size_t(y * 1344 + x)];
    CHECK(changed_ui == 0);
    // The enemy lift's warm addition follows scene exposure through native black fades too.
    std::fill(source.begin(), source.end(), 0);
    params.background = source;
    REQUIRE(SDL_UpdateTexture(ordinary, nullptr, source.data(), 224 * 4));
    const auto black = draw(1);
    CHECK(std::all_of(black.begin(), black.end(), [](uint32_t c) { return (c & 0xFFFFFF) == 0; }));
    SDL_DestroyTexture(out);
    SDL_DestroyTexture(ordinary);
    pass.shutdown();
    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
}
