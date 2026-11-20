#include "game/session.h"
#include "game/setup.h"
#include <doctest/doctest.h>

TEST_CASE("original cartridge setup has no port policies or host resources") {
    using namespace sawgod;
    std::vector<uint8_t> rom(4 * 1024 * 1024);
    rom[rom.size() - 16 + 0x0B] = 2;
    auto w = game::make_decomp_game(std::move(rom));
    auto &s = game::Session::of(*w);
    game::Machine m(*w);
    CHECK(w->sram.size() == 0x8000);
    CHECK(s.hooks().size() == 0);
    CHECK(s.text().empty());
    CHECK_FALSE(s.player_names().enabled);
    CHECK_FALSE(s.save_slots().store_attached());
    CHECK(s.save_slots().count() == 3);
    CHECK_FALSE(s.save_slots().auto_listed());
    CHECK_FALSE(s.confirm_esper_changes);
    CHECK_FALSE(s.meat_b_cancels);
    CHECK(s.hooks().filter(game::hooks::Value::TitleMenuOptions, m, 3) == 3);
    CHECK(s.hooks().filter(game::hooks::Value::PartyMenuRows, m, 0) == 0);
    CHECK(s.hooks().should(game::hooks::VB::ConfigTab, m, true));
    CHECK(s.hooks().should(game::hooks::VB::PlayIntro, m, true));
    CHECK_FALSE(w->frame_layers());
    CHECK_FALSE(w->apu.effect_channels);
}
