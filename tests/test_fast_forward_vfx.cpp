#include "test_fast_forward_vfx_support.h"

TEST_CASE("Fast-Forward VFX defaults On and both values round-trip through config") {
    for (bool on : {false, true}) {
        sawgod::Config c, d;
        sawgod::Settings s, t;
        s.register_all(c);
        t.register_all(d);
        CHECK(s.ff_vfx->on());
        CHECK(s.ff_vfx->category == "Gameplay");
        c.set(*s.ff_vfx, on);
        const std::string saved = c.to_toml();
        CHECK(saved.find(on ? "fast_forward_vfx = true" : "fast_forward_vfx = false") != std::string::npos);
        d.from_toml(saved);
        CHECK(t.ff_vfx->on() == on);
        CHECK(t.fast_forward_speed() == 4);
    }
}

TEST_CASE("Fast-Forward envelope uses presentation time: 100 ms in, 120 ms out") {
    sawgod::FastForwardEnvelope e;
    CHECK(e.update(true, false, 0) == 0);
    CHECK(e.update(true, true, 600'000'000) == 0);
    CHECK(e.update(true, true, 650'000'000) == doctest::Approx(.5));
    CHECK(e.update(true, true, 700'000'000) == 1);
    CHECK(e.update(true, true, 3'199'999'999) == 1);
    CHECK(e.update(true, false, 3'200'000'000) == 1);
    CHECK(e.update(true, false, 3'260'000'000) == doctest::Approx(.5));
    CHECK(e.update(true, false, 3'320'000'000) == 0);
    CHECK(e.update(false, true, 4'000'000'000) == 0);
    CHECK(e.update(true, true, 5'000'000'000) == 0);
    CHECK(e.update(true, true, 5'100'000'000) == 1);
    CHECK(e.update(false, true, 5'110'000'000) == 0); // Off is immediate
}

TEST_CASE("FF pixels protect arbitrary live pane bounds and never accumulate trails") {
    const int w = 448, h = 288;
    std::vector<uint32_t> clean(size_t(w * h)), previous(size_t(w * h), 0x444444);
    std::array<uint32_t, 224 * 144> ui{};
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            clean[size_t(y * w + x)] = uint32_t((x + y) % 256) * 0x010101;
    // Nonstandard pane and speaker-tab bounds; no scene-name or fixed bottom guard.
    for (int y = 51; y < 102; ++y)
        for (int x = 23; x < 187; ++x)
            ui[size_t(y * 224 + x)] = 0xFF0000;
    for (float gb : {0.f, 1.f}) {
        auto out = clean;
        sawgod::fast_forward_pixels(out, previous, w, h, {0, 0, float(w), float(h)}, ui.data(), 0, gb, .85);
        CHECK(out == clean);
        sawgod::fast_forward_pixels(out, previous, w, h, {0, 0, float(w), float(h)}, ui.data(), 1, gb, .85);
        CHECK(bool(out != clean));
        for (int y = 102; y < 204; ++y)
            for (int x = 46; x < 374; ++x)
                REQUIRE(out[size_t(y * w + x)] == clean[size_t(y * w + x)]);
        if (gb == 1)
            for (uint32_t p : out) {
                CHECK((p & 255) == ((p >> 8) & 255));
                CHECK((p & 255) == ((p >> 16) & 255));
            }
        // Running it again with the SAME clean inputs cannot feed the effect back.
        auto again = clean;
        sawgod::fast_forward_pixels(again, previous, w, h, {0, 0, float(w), float(h)}, ui.data(), 1, gb, .85);
        CHECK(again == out);
    }
}
