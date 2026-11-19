// Optional observations must describe retained scanout without changing it.
#include "app/frame_audit.h"
#include <doctest/doctest.h>
#include <memory>

using namespace sawgod;
TEST_CASE("frame audit distinguishes black, uniform, absent and mixed native pictures") {
    CHECK(app::native_picture_facts(nullptr).valid_pixels == 0);
    auto layers = std::make_unique<emu::FrameLayers>();
    auto facts = app::native_picture_facts(layers.get());
    CHECK(facts.valid_pixels == 0);
    CHECK_FALSE(facts.black);
    layers->valid.fill(1);
    facts = app::native_picture_facts(layers.get());
    CHECK(facts.valid_pixels == 224 * 144);
    CHECK(facts.black);
    CHECK(facts.uniform);
    for (auto &p : layers->planes[emu::FrameLayers::Backdrop])
        p.color = 0xFFF;
    facts = app::native_picture_facts(layers.get());
    CHECK(facts.uniform);
    CHECK_FALSE(facts.black);
    auto &ground = layers->planes[emu::FrameLayers::Screen1];
    for (auto &p : ground) {
        p.flags = emu::RasterPixel::Present;
        p.tile = 0x200;
    }
    ground[0].color = 0x123;
    ground[0].palette = 7;
    ground[1].palette = 15;
    ground[2].tile = 0x100;
    ground[3].tile = 0x1FF;
    facts = app::native_picture_facts(layers.get());
    CHECK_FALSE(facts.uniform);
    CHECK(facts.scr1_pixels == 224 * 144);
    CHECK(facts.terrain_pixels == 224 * 144 - 4);
    CHECK(facts.terrain_fraction == doctest::Approx(float(224 * 144 - 4) / (224 * 144)));
    // Invalid rows cannot turn a missing raster into a visible/blank field.
    layers->valid.fill(0);
    CHECK(app::native_picture_facts(layers.get()).valid_pixels == 0);
}
TEST_CASE("frame audit is disabled without a path and does not change machine state") {
    app::FrameAuditLog log(nullptr);
    CHECK_FALSE(log.enabled());
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    const auto ram = w.ram;
    const auto io = w.io;
    log.write(w, {});
    CHECK(w.ram == ram);
    CHECK(w.io == io);
    CHECK(w.frame_count == 0);
}

TEST_CASE("frame audit distinguishes the held native picture from a newer raw raster") {
    CHECK(app::composed_picture_facts({}).pixels == 0);
    std::vector<uint32_t> picture(224 * 144, 0xFF000000);
    auto facts = app::composed_picture_facts(picture);
    CHECK(facts.pixels == 224 * 144);
    CHECK(facts.uniform);
    CHECK(facts.black);
    picture.back() = 0x123456;
    CHECK_FALSE(app::composed_picture_facts(picture).uniform);
    picture.assign(224 * 144, 0xFFF);
    facts = app::composed_picture_facts(picture);
    CHECK(facts.uniform);
    CHECK_FALSE(facts.black);
}
