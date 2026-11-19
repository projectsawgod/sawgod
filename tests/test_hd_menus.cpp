#include "chrome_fixture.h"
#include "engine/hd_menus.h"
#include "engine/menu_fill.h"
#include "game/ui_chrome.h"
#include <cstring>
#include <doctest/doctest.h>
using sawgod::emu::Wsc;
using sawgod::game::Chrome;

TEST_CASE(
    "text provenance retains strokes, shadow underlay and fill, but excludes overwritten pixels and icons") {
    Wsc w(std::vector<uint8_t>(0x400000), 0x2000);
    chrome_fixture::install(w);
    for (int tile = 0x10B; tile < 0x160; ++tile) {
        chrome_fixture::put(w, tile, 0, 0, 2);
        chrome_fixture::put(w, tile, 1, 0, 1);
        chrome_fixture::put(w, tile, 0, 1, 1);
    }
    Wsc::UiText text;
    Wsc::UiFill fill;
    w.ui_text = &text;
    w.ui_fill = &fill;
    std::vector<uint32_t> px(224 * 144, 0x123456);
    Chrome chrome(w, px, Wsc::ColorMode::Raw, false);
    chrome.window(0, 0, 224, 144);
    const auto background = px;
    chrome.text(U"Test", 5, 5);
    size_t stroke = 0, shadow = 0;
    for (size_t at = 0; at < px.size(); ++at) {
        const auto kind = text.visible(at, px.data());
        if (kind == Wsc::UiText::Stroke)
            ++stroke;
        if (kind == Wsc::UiText::Shadow) {
            ++shadow;
            CHECK(text.pixels[at].beneath == background[at]);
            CHECK(fill.visible(at, text.pixels[at].beneath) == 1);
            auto other = px;
            other[at] = 0xABCDEF;
            CHECK(text.visible(at, other.data()) == Wsc::UiText::None);
        }
    }
    CHECK(stroke > 0);
    CHECK(shadow > 0);
    chrome.text(U"Label", 80, 50, Chrome::Ink::Text, 100, Chrome::Window::Normal, false);
    bool shadowless = false;
    for (int y = 50; y < 60; ++y)
        for (int x = 80; x < 120; ++x) {
            const auto &p = text.pixels[size_t(y * 224 + x)];
            if (p.kind == Wsc::UiText::None)
                continue;
            CHECK(p.kind == Wsc::UiText::Stroke);
            CHECK_FALSE(p.casts_shadow);
            shadowless = true;
        }
    CHECK(shadowless);
    const auto saved = text.pixels;
    Chrome gb(w, px, Wsc::ColorMode::Raw, true);
    gb.text(U"GB", 40, 5);
    for (size_t at = 0; at < saved.size(); ++at)
        CHECK(text.pixels[at].kind == saved[at].kind);
    chrome.window(0, 0, 100, 40);
    for (int y = 0; y < 40; ++y)
        for (int x = 0; x < 100; ++x)
            CHECK(text.pixels[size_t(y * 224 + x)].kind == Wsc::UiText::None);
    chrome.window(0, 40, 224, 80);
    chrome.cursor(6, 5);
    for (const auto &p : text.pixels)
        CHECK(p.kind == Wsc::UiText::None);
}

TEST_CASE("soft text shadow preserves exact glyph pixels and restores transparent fill shadows") {
    SDL_Surface *surface = SDL_CreateSurface(1344, 864, SDL_PIXELFORMAT_XRGB8888);
    REQUIRE(surface);
    SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
    REQUIRE(renderer);
    sawgod::HdMenus hd;
    Wsc::UiText text;
    Wsc::UiFill fill;
    fill.windows.push_back({0, 144, 0xFFFFFF, 0, 0}); // a fully transparent battle window
    std::vector<uint32_t> picture(224 * 144, 0x777777);
    const size_t stroke = 10 * 224 + 10, shadow = 11 * 224 + 11;
    picture[stroke] = 0xFFFFFF;
    picture[shadow] = 0;
    text.pixels[stroke] = {Wsc::UiText::Stroke, 0xFFFFFF, 0x777777};
    text.pixels[shadow] = {Wsc::UiText::Shadow, 0, 0x777777};
    fill.owner[shadow] = 1;
    fill.color[shadow] = 0x777777;
    fill.beneath[shadow] = 0x777777;
    SDL_SetRenderDrawColor(renderer, 119, 119, 119, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_FRect hard{66, 66, 6, 6};
    SDL_RenderFillRect(renderer, &hard);
    sawgod::MenuFill gradient;
    REQUIRE(gradient.draw(renderer, {0, 0, 1344, 864}, fill, picture, 1, &text));
    REQUIRE(hd.draw(renderer, {0, 0, 1344, 864}, text, &fill, picture, 1));
    REQUIRE(SDL_FlushRenderer(renderer));
    const auto red = [&](int x, int y) {
        Uint8 r, g, b;
        REQUIRE(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, nullptr));
        return r;
    };
    CHECK(red(60, 60) == 255);
    CHECK(red(65, 65) == 255);
    CHECK(red(67, 67) < 119);
    CHECK(red(67, 67) > 17); // soft coverage beyond the stroke
    CHECK(red(72, 72) > red(67, 67));
    CHECK(red(80, 80) == 119);
    // Fractional placement uses shared floor boundaries; blur never reaches letterbox pixels.
    text.clear();
    text.pixels[0] = {Wsc::UiText::Stroke, 0xFFFFFF, 0x777777};
    picture[0] = 0xFFFFFF;
    SDL_SetRenderDrawColor(renderer, 119, 119, 119, 255);
    SDL_RenderClear(renderer);
    REQUIRE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD));
    REQUIRE(hd.draw(renderer, {20.25f, 10.5f, 1300.1f, 835.6f}, text, nullptr, picture, .5f));
    REQUIRE(SDL_FlushRenderer(renderer));
    CHECK(red(20, 10) == 255);
    CHECK(red(19, 18) == 119);
    CHECK(red(27, 18) < 119);
    SDL_BlendMode blend;
    REQUIRE(SDL_GetRenderDrawBlendMode(renderer, &blend));
    CHECK(blend == SDL_BLENDMODE_ADD);
    hd.shutdown();
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
}

TEST_CASE("clean scene composition seeds native host text before Chrome without losing existing records") {
    Wsc w(std::vector<uint8_t>(0x400000), 0x2000);
    chrome_fixture::install(w);
    w.io[0x60] = 0xE0; // packed 4bpp
    w.io[0] = 0;       // only the host label over the backdrop
    chrome_fixture::put(w, 0x1BB, 0, 0, 2);
    chrome_fixture::put(w, 0x1BB, 1, 0, 1);
    w.host_cells.push_back({10, 10, uint16_t(0x1BB | 15 << 9)});
    w.cpu.s.halted = true;
    w.capture_layers(true);
    w.run_frame();
    w.run_frame();
    Wsc::UiText text;
    w.ui_text = &text;
    w.framebuffer_rgb(Wsc::ColorMode::Lcd); // native handoff may read both color modes first
    auto scene = w.framebuffer_rgb(Wsc::ColorMode::Raw, false, true);
    CHECK(text.visible(10 * 224 + 10, scene.data()) == Wsc::UiText::Stroke);
    CHECK(text.visible(10 * 224 + 11, scene.data()) == Wsc::UiText::Shadow);
    Chrome chrome(w, scene, Wsc::ColorMode::Raw, false);
    chrome.text(U"Host", 20, 20);
    REQUIRE(text.composed);
    const auto saved = text.pixels;
    w.framebuffer_rgb(Wsc::ColorMode::Raw, false, true);
    CHECK(text.pixels == saved);
    w.framebuffer_rgb(Wsc::ColorMode::Raw);
    CHECK(text.pixels == saved);
}

TEST_CASE("a native window the clean scene hides leaves no host text where the backdrop shares its color") {
    // The battle's top description window is hidden in Enhanced, but its white English glyphs
    // stayed in the text record wherever a white cloud lay beneath, and the HD text pass drew those
    // fragments.
    Wsc w(std::vector<uint8_t>(0x400000), 0x2000);
    chrome_fixture::install(w);
    w.io[0x60] = 0xE0; // packed 4bpp
    w.io[0] = 1;       // SCR1 only, its map at 0000h
    w.io[0x07] = 0;
    w.io[0x01] = 0xF2; // the backdrop is palette 15 index 2: white, the glyph stroke's color
    const uint16_t word = uint16_t(0x1BB | 15 << 9), address = (1 * 32 + 1) * 2;
    w.ram[address] = uint8_t(word);
    w.ram[address + 1] = uint8_t(word >> 8);
    Wsc::CellPixels glyph;
    glyph.entry = word;
    glyph.over[0] = 2; // stroke (white) at the cell's top-left pixel
    glyph.over[1] = 1; // its shadow (black)
    w.cell_pixels[address] = glyph;
    w.scene_hidden_palettes = 1u << 15;
    w.cpu.s.halted = true;
    w.capture_layers(true);
    w.run_frame();
    w.run_frame();
    Wsc::UiText text;
    w.ui_text = &text;
    const size_t at = 8 * 224 + 8;
    auto ordinary = w.framebuffer_rgb(Wsc::ColorMode::Raw);
    CHECK(text.visible(at, ordinary.data()) == Wsc::UiText::Stroke);
    auto scene = w.framebuffer_rgb(Wsc::ColorMode::Raw, false, true);
    CHECK(scene[at] == 0xFFFFFF); // the backdrop, the same white as the hidden glyph
    CHECK(text.visible(at, scene.data()) == Wsc::UiText::None);
    CHECK(text.visible(at + 1, scene.data()) == Wsc::UiText::None);
}

TEST_CASE("HP label soft shadows keep the bar's green ink and their down-right placement") {
    SDL_Surface *surface = SDL_CreateSurface(1344, 864, SDL_PIXELFORMAT_XRGB8888);
    REQUIRE(surface);
    SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
    REQUIRE(renderer);
    Wsc::UiText text;
    std::vector<uint32_t> picture(224 * 144, chrome_fixture::kHpFull);
    const size_t stroke = 20 * 224 + 20;
    picture[stroke] = 0xFFFFFF;
    text.pixels[stroke] = {Wsc::UiText::Stroke, 0xFFFFFF, chrome_fixture::kHpFull, true,
                           chrome_fixture::kHpShadow};
    SDL_SetRenderDrawColor(renderer, 88, 136, 59, 255);
    SDL_RenderClear(renderer);
    sawgod::HdMenus hd;
    REQUIRE(hd.draw(renderer, {0, 0, 1344, 864}, text, nullptr, picture, 1));
    REQUIRE(SDL_FlushRenderer(renderer));
    Uint8 r, g, b;
    REQUIRE(SDL_ReadSurfacePixel(surface, 120, 120, &r, &g, &b, nullptr));
    CHECK(r == 255);
    CHECK(g == 255);
    CHECK(b == 255);
    REQUIRE(SDL_ReadSurfacePixel(surface, 127, 127, &r, &g, &b, nullptr));
    CHECK(r < 88);
    CHECK(g < 136);
    CHECK(b < 59);
    // Blending two shades of the same sage keeps their hue (a neutral shadow would not).
    CHECK(float(g) / r == doctest::Approx(136.f / 88).epsilon(.04));
    REQUIRE(SDL_ReadSurfacePixel(surface, 116, 116, &r, &g, &b, nullptr));
    CHECK(r == 88);
    CHECK(g == 136);
    CHECK(b == 59);
    hd.shutdown();
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
}

TEST_CASE("bar provenance records removable hard edges and leaves the GB picture and machine untouched") {
    Wsc w(std::vector<uint8_t>(0x400000), 0x2000);
    chrome_fixture::install(w);
    Wsc::UiText text;
    Wsc::UiFill fill;
    w.ui_text = &text;
    w.ui_fill = &fill;
    std::vector<uint32_t> picture(224 * 144, 0x778899);
    Chrome chrome(w, picture, Wsc::ColorMode::Raw, false);
    chrome.window(0, 0, 224, 144);
    const auto background = picture;
    const auto ram = w.ram;
    const auto io = w.io;
    chrome.bar(20, 20, 32, 8, 1, 2, "1/2");
    REQUIRE(text.bars.rectangles == std::vector<Wsc::UiBars::Rect>{{20, 20, 32, 8}});
    unsigned shadows = 0;
    for (size_t at = 0; at < picture.size(); ++at) {
        const auto &p = text.bars.pixels[at];
        if (!p.shadow)
            continue;
        ++shadows;
        const int x = int(at % 224), y = int(at / 224);
        CHECK(((x == 52 && y >= 21 && y <= 28) || (y == 28 && x >= 21 && x <= 52)));
        CHECK(text.bars.visible(at, picture.data()) == 1);
        CHECK(p.color == chrome.color(Chrome::Ink::Shadow));
        CHECK(p.beneath == background[at]);
        CHECK(fill.visible(at, p.beneath) == 1);
    }
    CHECK(shadows == 39);
    CHECK(picture[20 * 224 + 52] == background[20 * 224 + 52]); // no top-right cap
    CHECK(picture[28 * 224 + 20] == background[28 * 224 + 20]); // no bottom-left cap
    CHECK(w.ram == ram);
    CHECK(w.io == io);
    auto gb_picture = background;
    const auto bars = text.bars;
    Chrome gb(w, gb_picture, Wsc::ColorMode::Raw, true);
    gb.bar(20, 20, 32, 8, 1, 2, "1/2");
    CHECK(text.bars.rectangles == bars.rectangles);
    CHECK(text.bars.pixels == bars.pixels);
    CHECK(gb_picture[21 * 224 + 52] == background[21 * 224 + 52]);
    const size_t halo = 21 * 224 + 19;
    CHECK(text.bars.visible(halo, picture.data()) == 1);
    const auto same_color = picture[halo];
    chrome.clip(19, 21, 1, 1, [&] { chrome.window(0, 0, 224, 144); });
    CHECK(picture[halo] == same_color);
    CHECK(text.bars.visible(halo, picture.data()) == 0);
    chrome.clip(52, 21, 1, 1, [&] { chrome.window(0, 0, 224, 144); });
    CHECK(text.bars.visible(21 * 224 + 52, picture.data()) == 0);
    text.clear();
    CHECK(text.bars.rectangles.empty());
    CHECK(std::all_of(text.bars.pixels.begin(), text.bars.pixels.end(), [](auto p) { return !p.owner; }));
}

TEST_CASE("HD bar edges restore hard shadows, cover fractional hairlines, and preserve bar pixels") {
    SDL_Surface *surface = SDL_CreateSurface(1344, 864, SDL_PIXELFORMAT_XRGB8888);
    REQUIRE(surface);
    SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
    REQUIRE(renderer);
    Wsc w(std::vector<uint8_t>(0x400000), 0x2000);
    chrome_fixture::install(w);
    Wsc::UiText text;
    w.ui_text = &text;
    std::vector<uint32_t> picture(224 * 144, 0x777777);
    Chrome chrome(w, picture, Wsc::ColorMode::Raw, false);
    chrome.bar(20, 20, 32, 8, 1, 2);
    SDL_Surface *native = SDL_CreateSurfaceFrom(224, 144, SDL_PIXELFORMAT_XRGB8888, picture.data(), 224 * 4);
    REQUIRE(native);
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, native);
    REQUIRE(texture);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    sawgod::HdMenus hd;
    const auto red = [&](int x, int y) {
        Uint8 r, g, b;
        REQUIRE(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, nullptr));
        return r;
    };
    const auto draw = [&](SDL_FRect dst, float weight) {
        SDL_SetRenderDrawColor(renderer, 119, 119, 119, 255);
        SDL_RenderClear(renderer);
        REQUIRE(SDL_RenderTexture(renderer, texture, nullptr, &dst));
        REQUIRE(hd.draw(renderer, dst, text, nullptr, picture, weight));
        REQUIRE(SDL_FlushRenderer(renderer));
    };
    draw({0, 0, 1344, 864}, 0);
    CHECK(red(317, 130) == 0); // original hard shadow, HD Menus off
    draw({0, 0, 1344, 864}, 1);
    // No border: left of the bar the scene is never lightened, only the blur's faint
    // edge.
    CHECK(red(118, 130) <= 119);
    CHECK(red(118, 130) > 105);
    CHECK(red(120, 130) == 88); // original bar
    CHECK(red(310, 130) == 41); // original empty portion
    CHECK(red(317, 130) > 17);  // restored scene plus soft shadow, not hard black
    CHECK(red(317, 130) < 119);
    CHECK(red(330, 130) == 119);
    draw({20.25f, 10.5f, 784, 504}, 1); // 3.5x
    CHECK(red(89, 90) <= 119);
    CHECK(red(89, 90) > 105);
    CHECK(red(90, 90) == 88);
    draw({0, 0, 1344, 864}, .5f);
    CHECK(red(317, 130) > 17); // half weight: between the restored scene and the soft shadow
    CHECK(red(317, 130) < 119);
    // The shared gradient pass must also remove hard bar shadows over transparent windows.
    Wsc::UiFill fill;
    fill.windows.push_back({0, 144, 0xFFFFFF, 0, 0});
    for (size_t at = 0; at < picture.size(); ++at)
        if (text.bars.pixels[at].shadow) {
            fill.owner[at] = 1;
            fill.color[at] = fill.beneath[at] = text.bars.pixels[at].beneath;
        }
    SDL_SetRenderDrawColor(renderer, 119, 119, 119, 255);
    SDL_RenderClear(renderer);
    REQUIRE(SDL_RenderTexture(renderer, texture, nullptr, nullptr));
    sawgod::MenuFill gradient;
    REQUIRE(gradient.draw(renderer, {0, 0, 1344, 864}, fill, picture, 1, &text));
    REQUIRE(hd.draw(renderer, {0, 0, 1344, 864}, text, &fill, picture, 1));
    REQUIRE(SDL_FlushRenderer(renderer));
    CHECK(red(317, 130) > 17);
    CHECK(red(317, 130) < 119);
    // An opaque later write, even in the original color, invalidates the edge halo.
    chrome.clip(19, 21, 1, 1, [&] { chrome.window(0, 0, 224, 144); });
    draw({0, 0, 1344, 864}, 1);
    CHECK(red(118, 130) == 119);
    hd.shutdown();
    SDL_DestroyTexture(texture);
    SDL_DestroySurface(native);
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
}

TEST_CASE("HD menus cache holds both shadow inks and invalidates every presentation input") {
    auto *surface = SDL_CreateSurface(448, 288, SDL_PIXELFORMAT_XRGB8888);
    REQUIRE(surface);
    auto *renderer = SDL_CreateSoftwareRenderer(surface);
    REQUIRE(renderer);
    Wsc w(std::vector<uint8_t>(0x400000), 0x2000);
    chrome_fixture::install(w);
    Wsc::UiText text;
    Wsc::UiFill fill;
    w.ui_text = &text;
    std::vector<uint32_t> picture(224 * 144, 0x777777);
    Chrome chrome(w, picture, Wsc::ColorMode::Raw, false);
    chrome.bar(20, 20, 32, 8, 1, 2);
    for (int x : {10, 70}) {
        const size_t at = size_t(10 * 224 + x);
        picture[at] = 0xFFFFFF;
        text.pixels[at] = {Wsc::UiText::Stroke, 0xFFFFFF, 0x777777, true,
                           x == 10 ? 0x111111u : chrome_fixture::kHpShadow};
    }
    SDL_FRect dst{0, 0, 448, 288};
    sawgod::HdMenus hd;
    sawgod::UiDrawCache prepared;
    const auto draw = [&](sawgod::HdMenus &pass, float weight, std::span<const uint32_t> blended = {}) {
        auto *native = SDL_CreateSurfaceFrom(224, 144, SDL_PIXELFORMAT_XRGB8888, picture.data(), 224 * 4);
        REQUIRE(native);
        auto *texture = SDL_CreateTextureFromSurface(renderer, native);
        REQUIRE(texture);
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
        SDL_SetRenderDrawColor(renderer, 119, 119, 119, 255);
        SDL_RenderClear(renderer);
        REQUIRE(SDL_RenderTexture(renderer, texture, nullptr, &dst));
        if (weight > 0)
            prepared.update(dst, &fill, &text, picture, weight, blended);
        REQUIRE(pass.draw(renderer, dst, text, &fill, picture, weight, blended, &prepared));
        REQUIRE(SDL_FlushRenderer(renderer));
        std::vector<uint8_t> pixels(size_t(surface->pitch * surface->h));
        std::memcpy(pixels.data(), surface->pixels, pixels.size());
        SDL_DestroyTexture(texture);
        SDL_DestroySurface(native);
        return pixels;
    };
    const auto initial = draw(hd, 1);
    CHECK(draw(hd, 1) == initial);
    CHECK(hd.rebuilds == 1);
    const auto fresh_matches = [&](float weight, std::span<const uint32_t> blended = {}) {
        const auto cached = draw(hd, weight, blended);
        sawgod::HdMenus fresh;
        CHECK(cached == draw(fresh, weight, blended));
        fresh.shutdown();
        return cached;
    };
    CHECK(fresh_matches(.5f) != initial);
    CHECK(hd.rebuilds == 2);
    text.pixels[10 * 224 + 10].casts_shadow = false; // record only, same picture
    fresh_matches(.5f);
    CHECK(hd.rebuilds == 3);
    picture[10 * 224 + 70] = 0x777777; // compositor hides a recorded stroke
    fresh_matches(.5f);
    CHECK(hd.rebuilds == 4);
    auto blended = picture;
    blended[10 * 224 + 10] = 0x888888;
    fresh_matches(.5f, blended);
    CHECK(hd.rebuilds == 5);
    fill.owner[29 * 224 + 30] = 1;
    fresh_matches(.5f, blended);
    CHECK(hd.rebuilds == 6);
    dst = {1.25f, 2.5f, 420, 270};
    fresh_matches(.5f, blended);
    CHECK(hd.rebuilds == 7);
    draw(hd, 0, blended); // On/Off leaves a valid cache for returning to the same held picture
    fresh_matches(.5f, blended);
    CHECK(hd.rebuilds == 7);
    hd.shutdown();
    fresh_matches(.5f, blended);
    CHECK(hd.rebuilds == 8);
    auto *target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 672, 432);
    REQUIRE(target);
    REQUIRE(SDL_SetRenderTarget(renderer, target));
    REQUIRE(hd.draw(renderer, dst, text, &fill, picture, .5f, blended, &prepared));
    CHECK(hd.rebuilds == 9); // output size changes invalidate masks even with the same prepared inputs
    REQUIRE(SDL_SetRenderTarget(renderer, nullptr));
    fresh_matches(.5f, blended);
    CHECK(hd.rebuilds == 10);
    SDL_DestroyTexture(target);
    hd.shutdown();
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
}

TEST_CASE("tab connectors retain HD frame provenance through speaker cutouts and foreground writes") {
    for (int active : {0, 1})
        for (bool speaker : {false, true}) {
            if (speaker && active)
                continue;
            Wsc w(std::vector<uint8_t>(0x400000), 0);
            chrome_fixture::install(w, 2);
            Wsc::UiText text;
            Wsc::UiFill fill;
            w.ui_text = &text;
            w.ui_fill = &fill;
            std::vector<uint32_t> picture(224 * 144, 0x123456);
            Chrome chrome(w, picture, Wsc::ColorMode::Raw, false);
            if (speaker)
                chrome.speaker_window(0, 60, 224, 84, 96);
            else
                chrome.tabbed_window(0, 0, 224, 128, 20, 112, active);
            REQUIRE(text.corners.size() == 1);
            const auto corner = text.corners.front();
            CHECK(corner.mirror == bool(active));
            size_t visible = 0;
            for (size_t at = 0; at < picture.size(); ++at)
                if (text.visible(at, picture.data()) == Wsc::UiText::FrameCorner) {
                    ++visible;
                    CHECK(text.pixels[at].corner == 1);
                    CHECK(int(at % 224) >= corner.x);
                    CHECK(int(at % 224) < corner.x + 8);
                    CHECK(int(at / 224) >= corner.y);
                    CHECK(int(at / 224) < corner.y + 8);
                }
            CHECK(visible > 0);
            CHECK(visible <= 64);
            chrome.window(corner.x, corner.y, 8, 8);
            for (size_t at = 0; at < picture.size(); ++at)
                CHECK(text.visible(at, picture.data()) != Wsc::UiText::FrameCorner);
            text.clear();
            CHECK(text.corners.empty());
            chrome.tabbed_window(0, 0, 224, 128, 20, 112, active);
            auto native = picture;
            Chrome gb(w, native, Wsc::ColorMode::Raw, true);
            const auto records = text.pixels;
            gb.tabbed_window(0, 0, 224, 128, 20, 112, active);
            CHECK(text.pixels == records);
        }
}

TEST_CASE("HD tab connector strokes are antialiased at exact and fractional scales") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    chrome_fixture::install(w, 2);
    Wsc::UiText text;
    Wsc::UiFill fill;
    w.ui_text = &text;
    w.ui_fill = &fill;
    std::vector<uint32_t> picture(224 * 144, 0x123456);
    Chrome chrome(w, picture, Wsc::ColorMode::Raw, false);
    chrome.tabbed_window(0, 0, 224, 128, 20, 112, 0);
    const auto corner = text.corners.front();
    SDL_Surface *surface = SDL_CreateSurface(1400, 900, SDL_PIXELFORMAT_XRGB8888);
    REQUIRE(surface);
    SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
    REQUIRE(renderer);
    sawgod::HdMenus hd;
    for (float scale : {3.f, 5.f, 5.3f}) {
        const SDL_FRect dst{.25f, .5f, 224 * scale, 144 * scale};
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        REQUIRE(hd.draw(renderer, dst, text, &fill, picture, 1));
        REQUIRE(SDL_FlushRenderer(renderer));
        int white = 0, edge = 0;
        // Inspect the stroke's curved quarter, where native nearest-neighbor drawing has only whole pixels.
        for (int y = int((corner.y + 4) * scale + dst.y); y < int((corner.y + 7) * scale + dst.y); ++y)
            for (int x = int(corner.x * scale + dst.x); x < int((corner.x + 3) * scale + dst.x); ++x) {
                Uint8 r, g, b;
                REQUIRE(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, nullptr));
                white += r == 255 && g == 255 && b == 255;
                // Fill is pure blue; native bevel/shadow are 119/34 gray. Intermediate gray
                // above 119 can only be antialiasing of the white stroke itself.
                edge += r > 119 && r < 255 && r == g && g == b;
            }
        CHECK(white > 0);
        CHECK(edge > 0);
    }
    hd.shutdown();
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
}

TEST_CASE("Enhanced HP bar soft shadows disappear at the end of the native black fade") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    chrome_fixture::install(w);
    std::fill(w.ram.begin() + 0xFE00, w.ram.end(), 0);
    std::vector<uint32_t> picture(224 * 144, 0);
    Wsc::UiText text;
    w.ui_text = &text;
    Chrome chrome(w, picture, Wsc::ColorMode::Raw, false);
    chrome.bar(20, 20, 32, 8, 1, 2, "1/2");
    REQUIRE(text.bars.rectangles.size() == 1);
    CHECK(text.bars.rectangles.front().opacity == 0);
    SDL_Surface *surface = SDL_CreateSurface(672, 432, SDL_PIXELFORMAT_XRGB8888);
    REQUIRE(surface);
    SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
    REQUIRE(renderer);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    sawgod::HdMenus hd;
    REQUIRE(hd.draw(renderer, {0, 0, 672, 432}, text, nullptr, picture, 1));
    REQUIRE(SDL_FlushRenderer(renderer));
    for (int y = 55; y < 90; ++y)
        for (int x = 55; x < 165; ++x) {
            Uint8 r, g, b;
            REQUIRE(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, nullptr));
            CHECK((r | g | b) == 0);
        }
    hd.shutdown();
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
}
