#include "app/world_card_overlay.h"
#include "emu/wsc.h"
#include "game/gfx_swap.h"
#include "game/host_picture.h"
#include "game/machine.h"
#include "game/session.h"
#include "game/text.h"
#include "game/text_fit.h"
#include "game/text_render.h"
#include "game/ui_chrome.h"
#include "game/world_card_lettering.h"
#include <algorithm>
#include <doctest/doctest.h>
using namespace sawgod;
TEST_CASE("GB opening replaces WSC art, a press completes the typed page and still reaches the game") {
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    game::GfxSwap gfx;
    gfx.set_enabled(w, true);
    std::string error;
    REQUIRE(game::Session::of(w).text().load("key,text\n", game::Charset::system(), &error, nullptr, nullptr,
                                             true));
    // Deliberately hostile picture data: no native illustration may show through.
    std::fill(w.ram.begin() + 0x4000, w.ram.end(), 0xAB);
    const auto before = w.ram;
    const auto framebuffer = w.framebuffer();
    game::Machine m(w);
    auto stage = [&](game::IntroStage s, int b = 0, int c = 0) {
        game::hooks::emit(game::hooks::Event::IntroPresentation, m, {int(s), b, c});
        gfx.frame(w);
    };
    stage(game::IntroStage::White);
    REQUIRE(w.second_scene_image.size() == 224 * 144);
    CHECK(std::all_of(w.second_scene_image.begin(), w.second_scene_image.end(),
                      [](auto p) { return p == 0xFFF; }));
    stage(game::IntroStage::Caption, 1, 192);
    w.frame_count += 10;
    gfx.frame(w);
    const auto typing = w.second_scene_image;
    CHECK(gfx.intro_keys(w, emu::KEY_A, false) == emu::KEY_A); // WSC selection never consumes it
    gfx.intro_keys(w, 0, true);
    CHECK(gfx.intro_keys(w, emu::KEY_A, true) == emu::KEY_A); // never swallowed: native timing stays
    CHECK(w.second_scene_image != typing);
    CHECK(gfx.intro_keys(w, emu::KEY_A, true) == emu::KEY_A);
    CHECK(gfx.intro_keys(w, 0, true) == 0);
    CHECK(std::all_of(w.second_scene_image.begin(), w.second_scene_image.end(),
                      [](auto p) { return p == 0 || p == 0xFFF; }));
    CHECK(w.ram == before);
    CHECK(w.framebuffer() == framebuffer);
    stage(game::IntroStage::Field);
    CHECK(w.second_scene_image.empty());
    gfx.intro_keys(w, 0, true);
    stage(game::IntroStage::Caption, 7, 96);
    stage(game::IntroStage::Pans);
    CHECK(gfx.intro_keys(w, emu::KEY_A, true) == emu::KEY_A);
    gfx.intro_keys(w, 0, true);
    stage(game::IntroStage::Prologue);
    stage(game::IntroStage::Fade, 15);
    stage(game::IntroStage::Content);
    const auto first = w.second_scene_image;
    w.frame_count += 437;
    gfx.frame(w);
    const auto first_complete = w.second_scene_image;
    CHECK(first_complete != first);
    w.frame_count += 1;
    gfx.frame(w);
    CHECK(w.second_scene_image == first); // page two begins blank on the exact boundary
    stage(game::IntroStage::Off);
    CHECK(w.second_scene_image.empty());
}
TEST_CASE("GB opening defaults fit their real wrapped pages in both languages") {
    for (const auto &info : game::kHostText) {
        const std::string key = info.key;
        if (!key.starts_with("intro_"))
            continue;
        for (auto text : {info.english, info.japanese}) {
            const auto value = game::with_wsc_glyphs(game::utf8_to_u32(text));
            const auto pages =
                key == "intro_prologue" ? game::GbIntro::pages(value) : std::vector<std::u32string>{value};
            for (const auto &page : pages) {
                const auto layout =
                    game::GbIntro::layout(page, key == "intro_prologue",
                                          key.starts_with("intro_caption_") ? std::stoi(key.substr(14)) : 0);
                CHECK(layout.too_wide.empty());
                CHECK(layout.lines.size() <= (key == "intro_prologue" ? 10 : 8));
                for (const auto &line : layout.lines)
                    CHECK(game::Chrome::text_width(line.text) <=
                          (key == "intro_prologue"
                               ? 144
                               : game::GbIntro::caption_width(
                                     key.starts_with("intro_caption_") ? std::stoi(key.substr(14)) : 0)));
            }
        }
    }
    CHECK(game::font_has(U'©'));
}

TEST_CASE("GB full pages cover host cells with raster provenance enabled") {
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    w.cpu.s.halted = true;
    w.cpu.s.flags = 0;
    w.io.fill(0);
    w.io[0x16] = 158;
    w.io[0x60] = 0xE0;
    std::fill_n(w.ram.begin() + 0x4000, 32, 0x22);
    w.ram[0xFE04] = 0;
    w.ram[0xFE05] = 0x0F;
    w.host_cells.push_back({20, 25, 0});
    w.capture_layers(true);
    w.set_second(true);
    w.second_scene_image.assign(224 * 144, 0xFFF);
    w.run_frame();
    w.run_frame();
    const auto ordinary = w.framebuffer();
    CHECK(w.framebuffer(true)[25 * 224 + 20] == 0xF00); // battlefield labels remain above it by default
    w.second_scene_above_host = true;
    w.run_frame();
    w.run_frame();
    CHECK(w.framebuffer() == ordinary);
    CHECK(std::all_of(w.framebuffer(true).begin(), w.framebuffer(true).end(),
                      [](auto p) { return p == 0xFFF; }));
    const auto *layers = w.frame_layers(true);
    REQUIRE(layers);
    for (size_t at = 0; at < 224 * 144; ++at)
        REQUIRE(layers->color(at) == 0xFFF);
}

TEST_CASE("GB opening translation fit checks use wrapped pages and preserve the split") {
    std::string base = "key,text\n", english = base;
    const auto cell = [](std::string value) {
        size_t pos = 0;
        while ((pos = value.find('"', pos)) != value.npos) {
            value.insert(pos, 1, '"');
            pos += 2;
        }
        return "\"" + value + "\"";
    };
    for (const auto &info : game::kHostText)
        if (std::string(info.key).starts_with("intro_")) {
            base += std::string(info.key) + "," + cell(info.japanese) + "\n";
            english += std::string(info.key) + "," + cell(info.english) + "\n";
        }
    CHECK(game::check_fit(base, english).empty());
    CHECK(game::check_fit(base, base).empty());
    const auto broken = game::check_fit(base, "key,text\nintro_prologue,Missing the split\n");
    REQUIRE(broken.size() == 1);
    CHECK(broken[0].problem.find("page boundary") != std::string::npos);
    CHECK(game::fit_budget("intro_caption_1", "").width == 192);
    CHECK(game::fit_budget("intro_prologue", "").width == 144);
}

TEST_CASE("GB world cards share the typed page, native fades and unmodified input") {
    for (int card = 1; card <= 4; ++card) {
        emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
        game::GfxSwap gfx;
        gfx.set_enabled(w, true);
        std::string error;
        REQUIRE(game::Session::of(w).text().load("key,text\n", game::Charset::system(), &error, nullptr,
                                                 nullptr, true));
        std::fill(w.ram.begin(), w.ram.end(), 0xAB);
        const auto before = w.ram;
        const auto framebuffer = w.framebuffer();
        game::Machine m(w);
        auto stage = [&](game::IntroStage s, int b = 0) {
            game::hooks::emit(game::hooks::Event::IntroPresentation, m, {int(s), b});
            gfx.frame(w);
        };
        stage(game::IntroStage::WorldCard, card);
        CHECK(std::all_of(w.second_scene_image.begin(), w.second_scene_image.end(),
                          [](auto p) { return p == 0; }));
        w.frame_count += 64;
        stage(game::IntroStage::Fade, 15);
        stage(game::IntroStage::WorldCardContent);
        const auto blank = w.second_scene_image;
        CHECK(std::all_of(blank.begin(), blank.end(), [](auto p) { return p == 0xFFF; }));
        w.frame_count += 2;
        gfx.frame(w);
        const auto partial = w.second_scene_image;
        CHECK(partial != blank);
        CHECK(gfx.intro_keys(w, emu::KEY_A, false) == emu::KEY_A);
        CHECK(w.second_scene_image == partial); // WSC-selected input cannot complete the GB page
        gfx.intro_keys(w, 0, true);
        CHECK(gfx.intro_keys(w, emu::KEY_A, true) == emu::KEY_A);
        const auto complete = w.second_scene_image;
        CHECK(complete != partial);
        for (auto key : {emu::KEY_A, emu::KEY_B, emu::KEY_START}) {
            CHECK(gfx.intro_keys(w, 0, true) == 0);
            CHECK(gfx.intro_keys(w, key, true) == key);
        }
        w.frame_count += 522;
        gfx.frame(w);
        CHECK(w.second_scene_image == complete); // hold doesn't replace or move the completed page
        stage(game::IntroStage::Fade, 5);
        CHECK(std::all_of(w.second_scene_image.begin(), w.second_scene_image.end(),
                          [](auto p) { return p == 0 || p == 0x555; }));
        stage(game::IntroStage::Fade, 0);
        CHECK(std::all_of(w.second_scene_image.begin(), w.second_scene_image.end(),
                          [](auto p) { return p == 0; }));
        stage(game::IntroStage::Off);
        CHECK(w.second_scene_image.empty());
        CHECK_FALSE(w.second_scene_above_host);
        CHECK(w.ram == before);
        CHECK(w.framebuffer() == framebuffer);
    }
}

TEST_CASE("GB world card defaults fit and use supported proportional or WSC kana glyphs") {
    std::string base = "key,text\n", english = base;
    for (const auto &info : game::kHostText) {
        const std::string key = info.key;
        if (!key.starts_with("world_card_"))
            continue;
        base += key + "," + info.japanese + "\n";
        english += key + "," + info.english + "\n";
        CHECK(game::fit_budget(key, "").width == game::GbIntro::kCaptionWidth);
        for (auto value : {info.japanese, info.english}) {
            const auto text = game::with_wsc_glyphs(game::utf8_to_u32(value));
            const auto layout = game::GbIntro::layout(text, false);
            CHECK(layout.lines.size() == 1);
            CHECK(layout.too_wide.empty());
            for (auto c : text)
                CHECK((game::font_has(c) || (c >= 0xE000 && c < 0xE100)));
        }
    }
    CHECK(game::check_fit(base, english).empty());
    CHECK(game::check_fit(base, base).empty());
    CHECK_FALSE(
        game::check_fit(
            base,
            "key,text\nworld_card_1,This world name is much too long to fit on its centered single line\n")
            .empty());
}

TEST_CASE("English world lettering observes native raster edges, fades, language and reset") {
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    game::WorldCardLettering cards;
    cards.attach(w);
    game::Machine m(w);
    const auto ram = w.ram;
    const auto framebuffer = w.framebuffer();
    const auto io = w.io;
    auto stage = [&](game::IntroStage s, int b = 0) {
        game::hooks::emit(game::hooks::Event::IntroPresentation, m, {int(s), b});
    };
    std::string error;
    REQUIRE(game::Session::of(w).text().load("key,text\n", game::Charset::system(), &error, nullptr, nullptr,
                                             true));
    for (int card = 1; card <= 4; ++card) {
        stage(game::IntroStage::WorldCard, card);
        CHECK(cards.active(w));
        CHECK(cards.opacity(w) == 0);
        stage(game::IntroStage::Fade, 15);
        w.frame_count += 65;
        CHECK(cards.slices(false, 0).empty());
        stage(game::IntroStage::WorldCardContent);
        w.frame_count += 3000;
        CHECK(cards.slices(false, 0).empty()); // No wall clock or guessed frame starts the sweep.
        for (int row = 0; row < 32; ++row)
            game::hooks::emit(game::hooks::Event::IntroPresentation, m,
                              {int(game::IntroStage::WorldCardRow), row, 96});
        CHECK_FALSE(cards.slices(false, 0).empty()); // first 32px kanji cell finished
        CHECK_FALSE(cards.slices(false, 1).empty()); // second cell halfway through
        CHECK(cards.slices(false, 2).empty());
        CHECK(cards.slices(false, 3).empty());
        // Non-monotonic rows retain the native random order, even without a host frame tick.
        const auto &part = cards.part(card, false, 0);
        const int middle = part.ink[1] + (part.ink[3] - part.ink[1]) / 2;
        const int native_row = std::clamp(middle - 48, 0, 31);
        game::hooks::emit(game::hooks::Event::IntroPresentation, m,
                          {int(game::IntroStage::WorldCardRow), native_row, 48});
        bool gap = true;
        for (const auto &r : cards.slices(false, 0))
            if (r.y <= middle && r.y + r.h > middle)
                gap = false;
        CHECK(gap);
        // One native row at a time: both resolutions follow physical scanlines,
        // including the glow above 48 and below 79 clamping to the nearest edge.
        for (bool enhanced : {false, true}) {
            const int scale = enhanced ? 8 : 1;
            const auto &layer = cards.part(card, enhanced, 0);
            bool own_scanlines = true;
            for (int hidden = 0; hidden < 32; ++hidden) {
                for (int row = 0; row < 32; ++row)
                    game::hooks::emit(game::hooks::Event::IntroPresentation, m,
                                      {int(game::IntroStage::WorldCardRow), row, row == hidden ? 0 : 224});
                const auto slices = cards.slices(enhanced, 0);
                for (int y = layer.y; y < layer.y + layer.image.height; ++y) {
                    bool visible = false;
                    for (const auto &r : slices)
                        visible |= r.y <= y && y < r.y + r.h;
                    const int line = y < 48 * scale ? 0 : y >= 80 * scale ? 31 : y / scale - 48;
                    own_scanlines &= visible == (line != hidden);
                }
            }
            CHECK(own_scanlines);
        }
        for (int row = 0; row < 32; ++row)
            game::hooks::emit(game::hooks::Event::IntroPresentation, m,
                              {int(game::IntroStage::WorldCardRow), row, 224});
        CHECK(cards.opacity(w) == 1);
        std::vector<uint32_t> picture(224 * 144, 0x335577);
        const auto native = picture;
        cards.composite(w, picture);
        CHECK(picture != native);
        const auto &whole = cards.image(card);
        auto approved = native;
        for (size_t i = 0; i < approved.size(); ++i) {
            const uint32_t src = whole.pixels[i], dst = native[i];
            const unsigned a = src >> 24;
            auto channel = [&](int shift) {
                return (((src >> shift & 255) * a + (dst >> shift & 255) * (255 - a) + 127) / 255) << shift;
            };
            approved[i] = channel(16) | channel(8) | channel(0);
        }
        CHECK(picture == approved); // Splitting the reveal preserves the approved finished bake exactly.
        const auto &large = cards.image(card, true);
        CHECK(large.width == 1792);
        CHECK(large.height == 1152);
        stage(game::IntroStage::Fade, 5);
        CHECK(cards.opacity(w) == doctest::Approx(1.0 / 3));
        stage(game::IntroStage::Fade, 0);
        picture = native;
        cards.composite(w, picture);
        CHECK(picture == native);
        stage(game::IntroStage::Off);
        CHECK_FALSE(cards.active(w));
    }
    CHECK(cards.image(0).pixels.empty());
    CHECK(cards.image(5).pixels.empty());
    stage(game::IntroStage::WorldCard, 1);
    stage(game::IntroStage::Fade, 15);
    stage(game::IntroStage::WorldCardContent);
    w.frame_count += 100;
    game::Session::of(w).text().clear();
    CHECK(cards.opacity(w) == 0); // Japanese
    game::Session::of(w).new_generation();
    CHECK_FALSE(cards.active(w));
    CHECK(w.ram == ram);
    CHECK(w.framebuffer() == framebuffer);
    CHECK(w.io == io);
}

TEST_CASE("World-card app textures keep WSC pixels, smooth Enhanced pixels and omit GB lettering") {
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    SDL_Window *window = SDL_CreateWindow("world cards", 1792, 1152, SDL_WINDOW_HIDDEN);
    REQUIRE(window);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, "software");
    REQUIRE(renderer);
    SDL_Texture *target =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, 1792, 1152);
    REQUIRE(target);
    SDL_SetRenderTarget(renderer, target);
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    game::WorldCardLettering cards;
    cards.attach(w);
    game::Machine m(w);
    std::string error;
    REQUIRE(game::Session::of(w).text().load("key,text\n", game::Charset::system(), &error, nullptr, nullptr,
                                             true));
    game::hooks::emit(game::hooks::Event::IntroPresentation, m, {int(game::IntroStage::WorldCard), 1});
    game::hooks::emit(game::hooks::Event::IntroPresentation, m, {int(game::IntroStage::Fade), 15});
    game::hooks::emit(game::hooks::Event::IntroPresentation, m, {int(game::IntroStage::WorldCardContent)});
    for (int row = 0; row < 32; ++row)
        game::hooks::emit(game::hooks::Event::IntroPresentation, m,
                          {int(game::IntroStage::WorldCardRow), row, 224});
    app::WorldCardOverlay overlay;
    const SDL_FRect dest{0, 0, 1792, 1152};
    for (int edge : {48, 72, 96, 128, 160, 224})
        for (int mode = 0; mode < 3; ++mode) {
            for (int row = 0; row < 32; ++row)
                game::hooks::emit(game::hooks::Event::IntroPresentation, m,
                                  {int(game::IntroStage::WorldCardRow), row, edge + row % 7});
            SDL_SetRenderDrawColor(renderer, 51, 85, 119, 255);
            SDL_RenderClear(renderer);
            std::array<float, 3> weights{};
            weights[size_t(mode)] = 1;
            overlay.draw(renderer, cards, w, dest, weights);
            SDL_Surface *surface = SDL_RenderReadPixels(renderer, nullptr);
            REQUIRE(surface);
            SDL_Surface *rgb = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_XRGB8888);
            REQUIRE(rgb);
            std::vector<uint32_t> expected(size_t(1792 * 1152), 0x335577);
            if (mode == 2)
                cards.composite(w, expected, true);
            else if (mode == 1) {
                std::vector<uint32_t> small(224 * 144, 0x335577);
                cards.composite(w, small);
                for (int y = 0; y < 1152; ++y)
                    for (int x = 0; x < 1792; ++x)
                        expected[size_t(y * 1792 + x)] = small[size_t(y / 8 * 224 + x / 8)];
            }
            int max_difference = 0;
            for (int y = 0; y < 1152; ++y) {
                const auto *row = reinterpret_cast<const uint32_t *>(
                    static_cast<const uint8_t *>(rgb->pixels) + y * rgb->pitch);
                for (int x = 0; x < 1792; ++x)
                    for (int shift : {0, 8, 16})
                        max_difference = std::max(
                            max_difference, std::abs(int(row[x] >> shift & 255) -
                                                     int(expected[size_t(y * 1792 + x)] >> shift & 255)));
            }
            INFO("edge = ", edge, ", mode = ", mode);
            CHECK(max_difference <= 2); // Each final pixel is drawn once; SDL truncates integer blending.
            SDL_DestroySurface(rgb);
            SDL_DestroySurface(surface);
        }
    overlay.shutdown();
    SDL_DestroyTexture(target);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

TEST_CASE("World lettering composes after the host handoff without accumulating or changing Japanese") {
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    game::HostPicture host;
    host.install(w, {});
    game::Machine m(w);
    std::string error;
    REQUIRE(game::Session::of(w).text().load("key,text\n", game::Charset::system(), &error, nullptr, nullptr,
                                             true));
    game::hooks::emit(game::hooks::Event::IntroPresentation, m, {int(game::IntroStage::WorldCard), 1});
    game::hooks::emit(game::hooks::Event::IntroPresentation, m, {int(game::IntroStage::Fade), 15});
    game::hooks::emit(game::hooks::Event::IntroPresentation, m, {int(game::IntroStage::WorldCardContent)});
    for (int row = 0; row < 32; ++row)
        game::hooks::emit(game::hooks::Event::IntroPresentation, m,
                          {int(game::IntroStage::WorldCardRow), row, 224});
    const auto native = w.framebuffer_rgb(emu::Wsc::ColorMode::Raw);
    auto expected = native;
    host.world_cards().composite(w, expected);
    CHECK(expected != native);
    CHECK(host.picture(w, emu::Wsc::ColorMode::Raw) == expected);
    CHECK(host.picture(w, emu::Wsc::ColorMode::Raw) == expected);
    CHECK(host.picture(w, emu::Wsc::ColorMode::Raw, false, false) == native);
    CHECK(host.picture(w, emu::Wsc::ColorMode::Raw, true) ==
          w.framebuffer_rgb(emu::Wsc::ColorMode::Raw, true));
    game::Session::of(w).text().clear();
    CHECK(host.picture(w, emu::Wsc::ColorMode::Raw) == native);
    CHECK(w.framebuffer_rgb(emu::Wsc::ColorMode::Raw) == native);
}
