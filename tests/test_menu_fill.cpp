#include "engine/menu_fill.h"
#include "game/ui_chrome.h"
#include <cmath>
#include <cstdlib>
#include <doctest/doctest.h>
#include <fstream>
#include <iterator>

TEST_CASE("menu gradient interpolates within native rows and preserves foreground holes and blend state") {
    SDL_Surface *surface = SDL_CreateSurface(1344, 864, SDL_PIXELFORMAT_XRGB8888);
    REQUIRE(surface);
    SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
    REQUIRE(renderer);
    sawgod::emu::Wsc::UiFill fill;
    fill.windows.push_back({4, 12, 0xFFFFFF, 0, 100});
    std::vector<uint32_t> picture(224 * 144, 0x123456);
    for (int y = 4; y < 12; ++y)
        for (int x = 10; x < 14; ++x) {
            fill.owner[size_t(y * 224 + x)] = 1;
            fill.color[size_t(y * 224 + x)] = picture[size_t(y * 224 + x)];
        }
    picture[6 * 224 + 12] = 0xFFFFFF; // a later non-Chrome compositor wins
    sawgod::MenuFill pass;
    const auto render = [&](float weight, int alpha) {
        fill.windows[0].alpha = alpha;
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD));
        REQUIRE(pass.draw(renderer, {0, 0, 1344, 864}, fill, picture, weight));
        SDL_BlendMode blend;
        REQUIRE(SDL_GetRenderDrawBlendMode(renderer, &blend));
        CHECK(blend == SDL_BLENDMODE_ADD);
        REQUIRE(SDL_FlushRenderer(renderer));
    };
    const auto red = [&](int x, int y) {
        Uint8 r, g, b;
        REQUIRE(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, nullptr));
        CHECK(r == g);
        CHECK(g == b);
        return int(r);
    };
    render(1, 100);
    CHECK(pass.quads == 9);           // one split run at the hole
    CHECK(red(63, 31) > red(63, 34)); // smooth inside one magnified native row
    CHECK(red(63, 34) > red(63, 38));
    CHECK(red(75, 39) == 0); // foreground hole
    CHECK(red(59, 31) == 0); // outside the fill
    const int full = red(63, 31);
    render(0.5f, 50);
    CHECK(red(63, 31) == doctest::Approx(full / 4.0).epsilon(0.03));
    // The gradient replaces the native tile over the original background exactly once.
    // A white background must still contribute 20% through an 80% window.
    for (auto &c : fill.beneath)
        c = 0xFFFFFF;
    render(1, 80);
    CHECK(red(63, 31) == doctest::Approx(full * 0.8 + 255 * 0.2).epsilon(0.02));
    render(0, 100);
    CHECK(pass.quads == 0);
    CHECK(red(63, 31) == 0);
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
}

TEST_CASE("menu fill cache reuses held content and redraws changed records pictures weights and rectangles") {
    auto *surface = SDL_CreateSurface(448, 288, SDL_PIXELFORMAT_XRGB8888);
    REQUIRE(surface);
    auto *renderer = SDL_CreateSoftwareRenderer(surface);
    REQUIRE(renderer);
    bool shared = false;
    SUBCASE("standalone input check") {}
    SUBCASE("shared input check") { shared = true; }
    sawgod::MenuFill pass;
    sawgod::UiDrawCache prepared;
    sawgod::emu::Wsc::UiFill fill;
    fill.windows.push_back({0, 144, 0xFFFFFF, 0xFFFFFF, 100});
    std::vector<uint32_t> picture(224 * 144, 0);
    fill.owner[0] = 1;
    SDL_FRect dst{0, 0, 448, 288};
    const auto draw = [&](float weight) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        if (shared && weight > 0)
            prepared.update(dst, &fill, nullptr, picture, weight);
        REQUIRE(pass.draw(renderer, dst, fill, picture, weight, nullptr, shared ? &prepared : nullptr));
        REQUIRE(SDL_FlushRenderer(renderer));
        Uint8 r, g, b;
        REQUIRE(SDL_ReadSurfacePixel(surface, 0, 0, &r, &g, &b, nullptr));
        return int(r);
    };
    CHECK(draw(1) == 255);
    CHECK(pass.rebuilds == 1);
    CHECK(draw(1) == 255);
    CHECK(pass.rebuilds == 1);
    CHECK(draw(.5f) == doctest::Approx(127).epsilon(.01));
    CHECK(pass.rebuilds == 2);
    fill.windows[0].light = fill.windows[0].dark = 0x808080;
    CHECK(draw(1) == 128);
    CHECK(pass.rebuilds == 3);
    picture[0] = 1; // surviving foreground changed with identical ownership records
    CHECK(draw(1) == 0);
    CHECK(pass.rebuilds == 4);
    picture[0] = 0;
    dst.x = 2;
    CHECK(draw(1) == 0);
    CHECK(pass.rebuilds == 5);
    dst.x = 0;
    CHECK(draw(0) == 0);
    CHECK(pass.rebuilds == 5);
    CHECK(draw(1) == 128);
    CHECK(pass.rebuilds == 6);
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
}

TEST_CASE("speaker connector fill stays HD at fractional scales and every tab tile phase") {
    const char *path = std::getenv("SAWGOD_WSC_ROM");
    if (!path || !*path)
        return;
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.good());
    std::vector<uint8_t> rom{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    REQUIRE(rom.size() == 0x400000);
    sawgod::emu::Wsc w(std::move(rom), 0);
    // The native field's font/window upload and palette, without running a game or changing its data.
    std::copy_n(w.rom.begin() + 0x390080, 0x2000, w.ram.begin() + 0x6000);
    std::copy_n(w.rom.begin() + 0x390000, 32, w.ram.begin() + 0xFFE0);
    w.io[0x60] = 0xE0;
    sawgod::emu::Wsc::UiFill fill;
    w.ui_fill = &fill;
    for (const auto size : {std::pair{1120, 720}, std::pair{1111, 715}, std::pair{853, 549}}) {
        auto *surface = SDL_CreateSurface(size.first, size.second, SDL_PIXELFORMAT_XRGB8888);
        REQUIRE(surface);
        auto *renderer = SDL_CreateSoftwareRenderer(surface);
        REQUIRE(renderer);
        sawgod::MenuFill pass;
        const float sx = float(size.first) / 224, sy = float(size.second) / 144;
        const auto pixel = [&](int x, int y) {
            std::array<Uint8, 3> rgb{};
            REQUIRE(SDL_ReadSurfacePixel(surface, x, y, &rgb[0], &rgb[1], &rgb[2], nullptr));
            return rgb;
        };
        for (int split : {16, 17, 54, 55, 56, 57, 70, 74, 75, 76, 127, 128, 200, 208}) {
            CAPTURE(size.first);
            CAPTURE(size.second);
            CAPTURE(split);
            fill.clear();
            std::vector<uint32_t> picture(224 * 144, 0x123456);
            sawgod::game::Chrome chrome(w, picture, sawgod::emu::Wsc::ColorMode::Raw, false);
            chrome.speaker_window(0, 76, 224, 68, split);
            REQUIRE(fill.windows.size() == 1);
            REQUIRE(pass.draw(renderer, {0, 0, float(size.first), float(size.second)}, fill, picture, 1));
            REQUIRE(SDL_FlushRenderer(renderer));
            int checked = 0;
            for (int y = 86; y < 95; ++y)
                for (int x = split + 2; x < split + 10 && x < 224; ++x) {
                    const auto at = size_t(y * 224 + x);
                    if (!fill.visible(at, picture[at]))
                        continue;
                    const int ox = int(std::floor((x + .5f) * sx));
                    const int top = int(std::floor(y * sy)), bottom = int(std::floor((y + 1) * sy)) - 1;
                    for (int oy : {top, bottom}) {
                        const auto actual = pixel(ox, oy), expected = pixel(int(10 * sx), oy);
                        for (int channel = 0; channel < 3; ++channel)
                            CHECK(std::abs(int(actual[channel]) - int(expected[channel])) <= 1);
                    }
                    ++checked;
                }
            CHECK(checked > 0);
        }
        SDL_DestroyRenderer(renderer);
        SDL_DestroySurface(surface);
    }
}
