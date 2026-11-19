#include "engine/pixel_upload.h"
#include "engine/presentation_renderer.h"
#include <doctest/doctest.h>
#include <memory>

namespace {
void check_uploads(SDL_Renderer *renderer) {
    constexpr int width = 32, height = 24;
    auto *texture =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
    REQUIRE(texture);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    auto *target =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, width, height);
    REQUIRE(target);
    std::vector<uint32_t> previous, pixels(width * height, 0x123456);
    REQUIRE(sawgod::upload_pixels(texture, pixels.data(), width, height, previous));
    REQUIRE(sawgod::upload_pixels(texture, pixels.data(), width, height, previous));
    // Tiny, strided, disjoint and full-width changes, including all four corners. Queue draws
    // between updates without a readback fence: cycling must retain every earlier source snapshot.
    for (int round = 0; round < 3; ++round) {
        std::vector<uint32_t> expected(width * height);
        REQUIRE(SDL_SetRenderTarget(renderer, target));
        for (int y = 0; y < height; ++y) {
            const auto color = uint32_t(0x203040 + round * 0x10203 + y * 0x50307);
            for (int x = 0; x < width; ++x)
                if (y % 3 == 0 || x == 0 || x == width - 1 || x == (y + round) % width)
                    pixels[size_t(y * width + x)] = color;
            REQUIRE(sawgod::upload_pixels(texture, pixels.data(), width, height, previous));
            const SDL_FRect row{0, float(y), float(width), 1};
            REQUIRE(SDL_RenderTexture(renderer, texture, &row, &row));
            std::copy_n(pixels.begin() + y * width, width, expected.begin() + y * width);
        }
        auto *raw = SDL_RenderReadPixels(renderer, nullptr);
        REQUIRE(raw);
        auto *rgb = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_XRGB8888);
        REQUIRE(rgb);
        for (int y = 0; y < height; ++y) {
            const auto *row = reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(rgb->pixels) +
                                                                 y * rgb->pitch);
            for (int x = 0; x < width; ++x)
                REQUIRE((row[x] & 0xFFFFFF) == expected[size_t(y * width + x)]);
        }
        SDL_DestroySurface(rgb);
        SDL_DestroySurface(raw);
    }
    SDL_SetRenderTarget(renderer, nullptr);
    SDL_DestroyTexture(target);
    SDL_DestroyTexture(texture);
}
} // namespace

TEST_CASE("pixel uploads preserve strided changes and queued source snapshots") {
    auto *surface = SDL_CreateSurface(32, 24, SDL_PIXELFORMAT_XRGB8888);
    REQUIRE(surface);
    auto *renderer = SDL_CreateSoftwareRenderer(surface);
    REQUIRE(renderer);
    check_uploads(renderer);
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
}

TEST_CASE("GPU pixel uploads preserve staging across queued updates and texture destruction") {
    if (!SDL_getenv("SAWGOD_GPU_TESTS"))
        return;
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    auto *window = SDL_CreateWindow("upload test", 32, 24, SDL_WINDOW_HIDDEN);
    REQUIRE(window);
    sawgod::PresentationRenderer renderer;
    REQUIRE(renderer.init(window, false, false));
    REQUIRE(renderer.is_gpu());
    check_uploads(renderer.get());
    check_uploads(renderer.get());
    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
}
