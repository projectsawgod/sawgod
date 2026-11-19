#include "game/text_render.h"
#include "test_battle_ui_support.h"

TEST_CASE("battle equipment scroll markers follow the native page and stay out of other command states") {
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    fx::install(w);
    Machine m(w);
    BattleUi ui;
    ui.install(w);
    hooks::emit(hooks::Event::BattleStart, m);
    hooks::emit(hooks::Event::BattleMenu, m);
    for (bool second : {false, true}) {
        w.set_second(second);
        for (int member : {0, 3})
            for (int state : {0, 1, 2, 4, 6})
                for (int page : {0, 2, 5}) {
                    w.ram[0xD6E1] = uint8_t(member);
                    w.ram[0xD6E2] = uint8_t(state);
                    w.ram[0xDC6E + member] = uint8_t(page);
                    std::vector<int> markers;
                    Chrome::observe = [&](const auto &d) {
                        if (d.kind == Chrome::Draw::Kind::ScrollMarker) {
                            CHECK(d.x == 212);
                            CHECK_FALSE(d.pointer);
                            markers.push_back(d.y);
                        }
                    };
                    const auto ram = w.ram;
                    ui.picture(w, sawgod::emu::Wsc::ColorMode::Raw, second);
                    Chrome::observe = {};
                    std::vector<int> expected;
                    if (state == 1 && page > 0)
                        expected.push_back(83);
                    if (state == 1 && page + 3 < 8)
                        expected.push_back(104);
                    CHECK(markers == expected);
                    CHECK(w.ram == ram);
                }
    }
}

TEST_CASE("battle action data uses handler-specific masks and loaded item tables, never flavor") {
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    auto &s = Session::of(w);
    std::string error;
    REQUIRE(s.text().load("key,text\nitemdesc_106,Do not show this flavor\n", Charset::system(), &error,
                          nullptr, nullptr, true));
    // Extracted records from items_full.csv; table overrides exercise the native mod read path too.
    REQUIRE(s.tables().load_csv("items",
                                "id,action,target,passive_bits\n"
                                "106,26,128,1\n33,6,160,0\n71,22,192,100\n111,27,128,32\n"
                                "17,7,160,255\n38,8,160,1\n52,9,160,8\n56,13,160,255\n"
                                "58,18,128,33\n114,29,160,5\n147,30,160,2\n175,32,128,16\n"
                                "155,31,160,8\n191,34,192,128\n28,48,192,224\n0,24,0,20\n131,37,64,0\n",
                                w, &error));
    CHECK(battle_item_details(w, 106) == U"Fire / Ranged / One group");
    CHECK(battle_item_details(w, 33) == U"Melee / One group");
    CHECK(battle_item_details(w, 71) == U"Ranged / All enemies"); // 64h is damage, not elements
    CHECK(battle_item_details(w, 111) == U"Status / Ranged / One group");
    CHECK(battle_item_details(w, 17) == U"Multiple / Melee / One group");
    for (int id : {38, 52, 56, 114, 147}) {
        CAPTURE(id);
        CHECK(battle_item_details(w, uint8_t(id)) == U"Melee / One group");
    }
    CHECK(battle_item_details(w, 58) == U"Ranged / One group");
    CHECK(battle_item_details(w, 175) == U"Ranged / One group");
    CHECK(battle_item_details(w, 155) == U"Poison / Melee / One group");
    CHECK(battle_item_details(w, 191) == U"Quake / Ranged / All enemies");
    CHECK(battle_item_details(w, 28) == U"Multiple / Ranged / All enemies");
    CHECK(battle_item_details(w, 0) == U"Ally"); // healing magnitude is not an attack element/range
    CHECK(battle_item_details(w, 131) == U"Self");
    // All named single attack kinds, an unidentified bit, and an unused target encoding.
    for (const auto &[bits, label] : std::vector<std::pair<int, std::u32string>>{{2, U"Ice"},
                                                                                 {4, U"Thunder"},
                                                                                 {8, U"Poison"},
                                                                                 {16, U"Stone"},
                                                                                 {32, U"Status"},
                                                                                 {128, U"Quake"}}) {
        REQUIRE(
            s.tables().load_csv("items", "id,passive_bits\n106," + std::to_string(bits) + "\n", w, &error));
        CHECK(battle_item_details(w, 106) == label + U" / Ranged / One group");
    }
    REQUIRE(s.tables().load_csv("items", "id,passive_bits,target\n106,64,32\n", w, &error));
    CHECK(battle_item_details(w, 106).empty());
}

TEST_CASE("battle action preview follows the acting member's selected slot and live target read-only") {
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    Machine m(w);
    fx::install(w);
    BattleUi ui;
    ui.install(w);
    hooks::emit(hooks::Event::BattleStart, m);
    hooks::emit(hooks::Event::BattleMenu, m);
    std::string error;
    auto &s = Session::of(w);
    REQUIRE(
        s.text().load("key,text\nitem_106,Fire Book\nitem_33,Long Sword\nitem_71,Missile\n"
                      "item_111,Sleep Book\nmonster_0,Goblin\nmonster_1,Jumbo Garlic\nmonster_2,Dragonfly\n",
                      Charset::system(), &error, nullptr, nullptr, true));
    REQUIRE(s.tables().load_csv("items",
                                "id,action,target,passive_bits\n106,26,128,1\n"
                                "33,6,160,0\n71,22,192,100\n111,27,128,32\n",
                                w, &error));
    w.ram[0xD6E2] = 2;
    w.ram[0xD6E1] = 2; // actor 2, selected slot 6 (page 5 + row 1), not the first visible slot
    w.ram[0xDC70] = 5;
    w.ram[0xDC64] = 1;
    w.ram[0xD6D4] = 3;
    for (int g = 0; g < 3; ++g) {
        w.ram[0xD6D5 + g * 2] = uint8_t(g);
        w.ram[0xD6D6 + g * 2] = uint8_t(g + 1);
    }
    ui.set_party_name(2, U"Willowyn");
    const std::u32string details[] = {U"Fire / Ranged / One group", U"Melee / One group",
                                      U"Ranged / All enemies", U"Status / Ranged / One group"};
    const std::u32string targets[] = {U"Goblin", U"Jumbo Garlic x2", U"Dragonfly x3"};
    int item = 0;
    for (int id : {106, 33, 71, 111}) {
        w.ram[0xD774] = uint8_t(id);
        w.ram[0xD775] = id == 111 ? 254 : 30;
        for (int g = 0; g < 3; ++g) {
            w.ram[0xDC68] = uint8_t(g);
            const auto ram = w.ram;
            const auto io = w.io;
            const auto registers = w.cpu.s.r;
            const auto preview = ui.action_preview(w);
            REQUIRE(preview);
            CHECK(preview->item == id);
            CHECK(preview->name == item_name(w, uint8_t(id)));
            CHECK(preview->actor == U"Willowyn");
            CHECK(preview->uses == (id == 111 ? U"--" : U"30"));
            CHECK(preview->target == targets[g]);
            CHECK(preview->details == details[item]);
            CHECK(Chrome::text_width(preview->details) <= 189);
            for (bool second : {false, true}) {
                w.set_second(second);
                sawgod::emu::Wsc::UiText retained;
                w.ui_text = &retained;
                std::vector<Chrome::Draw> draws;
                Chrome::observe = [&](const auto &d) { draws.push_back(d); };
                auto picture = ui.picture(w, sawgod::emu::Wsc::ColorMode::Raw, second);
                Chrome::observe = {};
                Chrome colors(w, picture, sawgod::emu::Wsc::ColorMode::Raw, second);
                bool name = false, data = false, arrow = false, uses = false;
                for (const auto &d : draws) {
                    if (d.kind == Chrome::Draw::Kind::Text && d.y >= 84 && d.y <= 102) {
                        CHECK_FALSE(d.clipped);
                        if (d.text == preview->name) {
                            name = true;
                            CHECK(d.x == 27);
                            CHECK(d.y == 84);
                        }
                        if (d.text == preview->details) {
                            data = true;
                            CHECK(d.x == 27);
                            CHECK(d.y == 102);
                            CHECK_FALSE(d.strokes.empty());
                            for (const auto at : d.strokes) {
                                CHECK(picture[at] == colors.color(Chrome::Ink::Secondary));
                                if (!second)
                                    CHECK(retained.visible(at, picture.data()) ==
                                          sawgod::emu::Wsc::UiText::Stroke);
                            }
                        }
                        if (d.text == preview->uses) {
                            uses = true;
                            CHECK(d.x + d.width == 216);
                        }
                    }
                    // The relationship arrow is a text glyph in the text's ink, never the selection cursor.
                    if (d.kind == Chrome::Draw::Kind::Text && d.text == U"\u25B8" && d.y == 93)
                        arrow = true;
                    CHECK_FALSE((d.kind == Chrome::Draw::Kind::Cursor && d.y == 92));
                }
                CHECK(name);
                CHECK(data);
                CHECK(arrow);
                CHECK(uses);
                w.ui_text = nullptr;
                CHECK(w.ram == ram);
                CHECK(w.io == io);
                CHECK(w.cpu.s.r == registers);
            }
        }
        ++item;
    }
    for (int state : {0, 1, 3, 4, 5, 6}) {
        w.ram[0xD6E2] = uint8_t(state);
        CHECK_FALSE(ui.action_preview(w));
    }
    w.ram[0xD6E2] = 2;
    w.ram[0xDC68] = 3;
    CHECK_FALSE(ui.action_preview(w)); // invalid target
    w.ram[0xDC68] = 0;
    w.ram[0xD6D6] = 0;
    CHECK_FALSE(ui.action_preview(w)); // dead group
    w.ram[0xD6D6] = 1;
    w.ram[0xDC70] = 7;
    CHECK_FALSE(ui.action_preview(w)); // invalid slot sum
    w.ram[0xDC70] = 5;
    w.ram[0xD775] = 255;
    CHECK_FALSE(ui.action_preview(w)); // empty item
    w.ram[0xD775] = 254;
    hooks::emit(hooks::Event::BattleRoundStart, m);
    CHECK_FALSE(ui.action_preview(w));
    hooks::emit(hooks::Event::BattleMenu, m);
    REQUIRE(ui.action_preview(w));
    hooks::emit(hooks::Event::BattleRewards, m);
    CHECK_FALSE(ui.action_preview(w));
    hooks::emit(hooks::Event::BattleEnd, m);
    CHECK_FALSE(ui.action_preview(w));
}

TEST_CASE("battle UI preserves the original 80-pixel battlefield without a floor extension") {
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    Machine m(w);
    fx::install(w);
    BattleUi ui;
    ui.install(w);
    hooks::emit(hooks::Event::BattleStart, m);
    hooks::emit(hooks::Event::BattleMenu, m);
    const auto ram = w.ram;
    const auto ordinary = w.framebuffer_rgb(sawgod::emu::Wsc::ColorMode::Raw);
    for (int state : {0, 1, 2, 4}) {
        w.ram[0xD6E2] = uint8_t(state);
        const auto picture = ui.picture(w, sawgod::emu::Wsc::ColorMode::Raw);
        CHECK(std::equal(picture.begin(), picture.begin() + 80 * 224, ordinary.begin()));
        CHECK(picture[80 * 224 + 128] == fx::kBorder);
        CHECK(picture[108 * 224 + 128] == fx::kFill);
    }
    w.ram[0xD6E2] = ram[0xD6E2];
    CHECK(w.ram == ram);
    w.set_second(true);
    w.screen1_image_swap = 0;
    w.screen1_image.assign(224 * 144, 0x0FFF);
    const auto gb_original = w.framebuffer_rgb(sawgod::emu::Wsc::ColorMode::Raw, true);
    const auto gb = ui.picture(w, sawgod::emu::Wsc::ColorMode::Raw, true);
    CHECK(std::equal(gb.begin(), gb.begin() + 80 * 224, gb_original.begin()));
}

TEST_CASE("monster name plates appear only during enemy target selection") {
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    Machine m(w);
    fx::install(w);
    BattleUi ui;
    ui.install(w);
    hooks::emit(hooks::Event::BattleStart, m);
    hooks::emit(hooks::Event::BattleMenu, m);
    w.ram[0xD6D4] = 3;
    w.ram[0xD6D5] = 0;
    w.ram[0xD6D6] = 1;
    w.ram[0xD6D7] = 1;
    w.ram[0xD6D8] = 1;
    w.ram[0xD6D9] = 2;
    w.ram[0xD6DA] = 1;
    std::string error;
    REQUIRE(Session::of(w).text().load("key,text\nmonster_0,Crab\nmonster_1,Flower\nmonster_2,Pudding\n",
                                       Charset::system(), &error, nullptr, nullptr, true));
    const auto ordinary = w.framebuffer_rgb(sawgod::emu::Wsc::ColorMode::Raw);
    for (int state : {0, 1, 4}) {
        CAPTURE(state);
        w.ram[0xD6E2] = uint8_t(state);
        const auto picture = ui.picture(w, sawgod::emu::Wsc::ColorMode::Raw);
        CHECK(std::equal(picture.begin(), picture.begin() + 80 * 224, ordinary.begin()));
    }
    w.ram[0xD6E2] = 2;
    constexpr int centers[] = {48, 112, 176};
    for (int target = 0; target < 3; ++target) {
        CAPTURE(target);
        w.ram[0xDC66] = uint8_t(target);
        const auto targeting = ui.picture(w, sawgod::emu::Wsc::ColorMode::Raw);
        CHECK(std::equal(targeting.begin(), targeting.begin() + 62 * 224, ordinary.begin()));
        CHECK(std::equal(targeting.begin() + 78 * 224, targeting.begin() + 80 * 224,
                         ordinary.begin() + 78 * 224));
        for (int group = 0; group < 3; ++group)
            CHECK(targeting[62 * 224 + centers[group]] ==
                  (group == target ? fx::kBorder : ordinary[62 * 224 + centers[group]]));
    }
    // Enhanced changes only the plate fill to 40%; glyphs remain opaque and WSC is
    // unchanged.
    sawgod::emu::Wsc::UiFill fill;
    sawgod::emu::Wsc::UiText text;
    w.ui_fill = &fill;
    w.ui_text = &text;
    ui.set_enhanced_weight(1);
    const auto enhanced = ui.picture(w, sawgod::emu::Wsc::ColorMode::Raw);
    bool plate = false;
    for (const auto &window : fill.windows)
        if (window.top == 62 && window.bottom == 78) {
            CHECK(window.alpha == 40);
            plate = true;
        }
    CHECK(plate);
    const auto enhanced_text = text;
    fill.clear();
    text.clear();
    ui.set_enhanced_weight(0);
    const auto wsc = ui.picture(w, sawgod::emu::Wsc::ColorMode::Raw);
    for (const auto &window : fill.windows)
        if (window.top == 62 && window.bottom == 78)
            CHECK(window.alpha == 100); // opaque at WSC
    for (size_t at = 62 * 224; at < 78 * 224; ++at)
        if (enhanced_text.visible(at, enhanced.data()) == sawgod::emu::Wsc::UiText::Stroke)
            CHECK(enhanced[at] == wsc[at]);
    w.ui_fill = nullptr;
    w.ui_text = nullptr;
    CHECK(ui.enemy_target(w) == 2);
    hooks::emit(hooks::Event::BattleRoundStart, m);
    CHECK(ui.enemy_target(w) == -1);
    const auto executing = ui.picture(w, sawgod::emu::Wsc::ColorMode::Raw);
    CHECK(std::equal(executing.begin(), executing.begin() + 80 * 224, ordinary.begin()));
}

TEST_CASE("battle sprite palettes respect shadow colors without changing the ordinary view") {
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    w.io[0x60] = 0x80;
    w.ram[0xFF02] = 0x00;
    w.ram[0xFF03] = 0x0F;
    w.set_second(true);
    w.shadow_write16(0xFF02, 0x0FFF);
    w.upper_palette_greys = true;
    const auto ram = w.ram;
    const auto ordinary = w.framebuffer_rgb(sawgod::emu::Wsc::ColorMode::Raw);
    CHECK(w.presentation_palette_color(8, 1, false, true) == 0xF00);
    CHECK(w.presentation_palette_color(8, 1, true, true) == 0xFFF);
    w.shadow_write16(0xFF02, 0x0F00);
    CHECK(w.presentation_palette_color(8, 1, true, true) == 0x555);
    CHECK(w.ram == ram);
    CHECK(w.framebuffer_rgb(sawgod::emu::Wsc::ColorMode::Raw) == ordinary);
    CHECK(w.presentation_palette_color(8, 1, false, true) == 0xF00);
}

TEST_CASE("battle presentation leaves game state intact, keeps the rewards, exits at the end and resets") {
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    Machine m(w);
    fx::install(w);
    BattleUi ui;
    ui.install(w);
    auto &s = Session::of(w);
    const auto original = ui.picture(w, sawgod::emu::Wsc::ColorMode::Raw);
    CHECK_FALSE(ui.active());
    hooks::emit(hooks::Event::BattleStart, m);
    CHECK_FALSE(ui.active());
    hooks::emit(hooks::Event::BattleMenu, m);
    REQUIRE(ui.active());
    w.ram[0xD6D4] = 3;
    w.ram[0xD6D5] = 0;
    w.ram[0xD6D6] = 1;
    w.ram[0xD6D7] = 1;
    w.ram[0xD6D8] = 2;
    w.ram[0xD6D9] = 2;
    w.ram[0xD6DA] = 1;
    w.ram[0xD71D] = 50;
    w.ram[0xD71F] = 100;
    std::string error;
    REQUIRE(
        s.text().load("key,text\nmonster_0,Atomic Crab\nmonster_1,Poison Flower\nmonster_2,Black Pudding\n",
                      Charset::system(), &error, nullptr, nullptr, true));
    ui.set_party_name(0, U"ABCDEFGHIJ");
    const auto ram = w.ram;
    const auto io = w.io;
    ui.set_transparency(0);
    const auto solid = ui.picture(w, sawgod::emu::Wsc::ColorMode::Raw);
    ui.set_transparency(30);
    const auto blended = ui.picture(w, sawgod::emu::Wsc::ColorMode::Raw);
    ui.set_transparency(100);
    const auto clear = ui.picture(w, sawgod::emu::Wsc::ColorMode::Raw);
    // The lower UI pane is opaque: no backdrop artwork exists beneath it.
    CHECK(solid[80 * 224 + 5] == fx::kBorder);
    CHECK(blended[80 * 224 + 5] == fx::kBorder);
    CHECK(clear[80 * 224 + 5] == fx::kBorder);
    CHECK(solid[106 * 224 + 150] == fx::kFill);
    CHECK(blended[106 * 224 + 150] == fx::kFill);
    CHECK(clear[106 * 224 + 150] == fx::kFill);
    for (int i = 0; i < 4; ++i) {
        CHECK(clear[112 * 224 + i * 56] == fx::kBorder);
        CHECK(clear[143 * 224 + i * 56 + 55] == fx::kBorder);
        // As the original: the acting member's panel in the usual window, the others inactive.
        CHECK(clear[141 * 224 + i * 56 + 2] == (i == 0 ? fx::kFill : fx::kInactiveFill));
    }
    CHECK(w.ram == ram);
    CHECK(w.io == io);
    CHECK(w.framebuffer_rgb(sawgod::emu::Wsc::ColorMode::Raw) == original);
    hooks::emit(hooks::Event::BattleRoundStart, m);
    auto executing = ui.picture(w, sawgod::emu::Wsc::ColorMode::Raw);
    CHECK(executing[44 * 224 + 12] == original[44 * 224 + 12]);
    // Floating numbers above the party row (y < 112): heal in the Heal role, damage in Text.
    auto above = [](const std::vector<uint32_t> &px, uint32_t c) {
        return std::count(px.begin() + 90 * 224, px.begin() + 112 * 224, c);
    };
    w.ram[0xD71D] = 75; // healing paths may write HP directly
    auto healed = ui.picture(w, sawgod::emu::Wsc::ColorMode::Raw);
    CHECK(above(healed, fx::kHeal) > 0);
    w.frame_count += 46;
    healed = ui.picture(w, sawgod::emu::Wsc::ColorMode::Raw);
    CHECK(above(healed, fx::kHeal) == 0);
    hooks::emit(hooks::Event::BattleHpChange, m, {0, 10, 75});
    w.ram[0xD71D] = 65;
    const auto damaged = ui.picture(w, sawgod::emu::Wsc::ColorMode::Raw);
    CHECK(above(damaged, fx::kText) > 0);
    // The rewards stay in this layout: the native windows hidden, the cards and the log drawn.
    hooks::emit(hooks::Event::BattleRewards, m);
    CHECK(ui.active());
    Session::of(w).apply_scene_settings(w);
    CHECK(w.scene_hidden_palettes == 0xFF80);
    const auto rewards = ui.picture(w, sawgod::emu::Wsc::ColorMode::Raw);
    CHECK(rewards[112 * 224 + 5] == fx::kBorder);             // the party cards
    CHECK(rewards[kBattleLogTop * 224 + 100] == fx::kBorder); // the log window
    hooks::emit(hooks::Event::BattleEnd, m);
    CHECK_FALSE(ui.active());
    Session::of(w).apply_scene_settings(w);
    CHECK(w.scene_hidden_palettes == 0);
    CHECK(ui.picture(w, sawgod::emu::Wsc::ColorMode::Raw) == original);
    hooks::emit(hooks::Event::BattleStart, m);
    hooks::emit(hooks::Event::BattleMenu, m);
    REQUIRE(ui.active());
    s.new_generation();
    ui.frame(w);
    CHECK_FALSE(ui.active());
    Session::of(w).apply_scene_settings(w);
    CHECK(w.scene_hidden_palettes == 0);
    Session::of(w).apply_scene_settings(w);
    CHECK(w.scene_hidden_sprite_prefix == 0);
    Session::of(w).apply_scene_settings(w);
    CHECK(w.scene_hidden_sprite_palettes == 0);
}

TEST_CASE("single-row battle HUD keeps full, empty and zero-maximum HP inside all four slots") {
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    Machine m(w);
    fx::install(w);
    BattleUi ui;
    ui.install(w);
    hooks::emit(hooks::Event::BattleStart, m);
    hooks::emit(hooks::Event::BattleMenu, m);
    const int hp[] = {999, 500, 0, 0};
    const int maximum[] = {999, 999, 999, 0};
    for (int i = 0; i < 4; ++i) {
        const int actor = 0xD71A + i * 32;
        w.ram[actor + 3] = uint8_t(hp[i]);
        w.ram[actor + 4] = uint8_t(hp[i] >> 8);
        w.ram[actor + 5] = uint8_t(maximum[i]);
        w.ram[actor + 6] = uint8_t(maximum[i] >> 8);
        ui.set_party_name(i, U"Willowyn");
    }
    const auto ram = w.ram;
    const auto pixels = ui.picture(w, sawgod::emu::Wsc::ColorMode::Raw);
    CHECK(w.ram == ram);
    // Bars: x+20..x+51, y 132-139.
    CHECK(pixels[132 * 224 + 51] == fx::kHpFull); // full bar reaches its right edge
    CHECK(pixels[132 * 224 + 56 + 20] == fx::kHpFull);
    CHECK(pixels[132 * 224 + 56 + 51] == fx::kHpEmpty);
    CHECK(pixels[132 * 224 + 112 + 20] == fx::kHpEmpty);
    CHECK(pixels[132 * 224 + 168 + 20] == fx::kHpEmpty);
    for (int i = 0; i < 4; ++i) {
        CHECK(pixels[143 * 224 + i * 56 + 54] == fx::kBorder); // the bar stays inside the frame
        CHECK(pixels[132 * 224 + i * 56 + 55] == fx::kBorder);
        const auto begin = pixels.begin() + 134 * 224 + i * 56 + 20;
        CHECK(std::count(begin, begin + 32, fx::kText) > 0); // the HP label
    }
}

TEST_CASE("meat menu keeps the battle layout through its stages and returns to the rewards in it") {
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    Machine m(w);
    fx::install(w);
    BattleUi ui;
    ui.install(w);
    hooks::emit(hooks::Event::BattleStart, m);
    hooks::emit(hooks::Event::BattleMenu, m);
    hooks::emit(hooks::Event::BattleRewards, m);
    CHECK(ui.active()); // the reward messages stay in the battle layout
    const auto ordinary = w.framebuffer_rgb(sawgod::emu::Wsc::ColorMode::Raw);
    std::string error;
    REQUIRE(
        Session::of(w).text().load("key,text\nmonster_1,Dragonfly\nmonster_5,MantisKing\nmonster_9,Goblin\n"
                                   "draw_transformation_menu_state_3,Eat\ndraw_transformation_menu_state_4,"
                                   "Don't\n",
                                   Charset::system(), &error, nullptr, nullptr, true));
    w.ram[0xD719] = 9;
    for (int stage = 1; stage <= 4; ++stage) {
        CAPTURE(stage);
        hooks::emit(hooks::Event::MeatMenu, m, {stage});
        CHECK(ui.active());
        CHECK(ui.meat_stage() == stage);
        Session::of(w).apply_scene_settings(w);
        CHECK(w.scene_hidden_palettes == 0xEFF0); // the battlefield (0-3) and the meat (12) stay
        Session::of(w).apply_scene_settings(w);
        CHECK(w.scene_hidden_sprite_prefix == 128);
        const auto picture = ui.picture(w, sawgod::emu::Wsc::ColorMode::Raw);
        CHECK(picture[112 * 224 + 5] == fx::kBorder);             // the four party cards
        CHECK(picture[kBattleLogTop * 224 + 100] == fx::kBorder); // every stage shares the log window
    }
    hooks::emit(hooks::Event::MeatMenu, m, {0});
    CHECK(ui.active()); // back to the reward messages, in this layout
    Session::of(w).apply_scene_settings(w);
    CHECK(w.scene_hidden_palettes == 0xFF80);
    Session::of(w).apply_scene_settings(w);
    CHECK(w.scene_hidden_sprite_prefix == 27);
    hooks::emit(hooks::Event::BattleEnd, m);
    CHECK_FALSE(ui.active());
    CHECK(ui.picture(w, sawgod::emu::Wsc::ColorMode::Raw) == ordinary);
    hooks::emit(hooks::Event::MeatMenu, m, {2});
    Session::of(w).new_generation();
    ui.frame(w);
    CHECK_FALSE(ui.active());
    Session::of(w).apply_scene_settings(w);
    CHECK(w.scene_hidden_palettes == 0);
}

TEST_CASE("meat preview compares both forms read-only, the battlefield and GB backdrop stay put") {
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    Machine m(w);
    fx::install(w);
    BattleUi ui;
    ui.install(w);
    std::string error;
    REQUIRE(Session::of(w).text().load("key,text\nmonster_1,Dragonfly\nmonster_5,MantisKing\n",
                                       Charset::system(), &error, nullptr, nullptr, true));
    hooks::emit(hooks::Event::BattleStart, m);
    hooks::emit(hooks::Event::BattleRewards, m);
    w.ram[0xD713] = 3;               // the eater
    w.ram[0xD05D + 10] = 0x43;       // a monster: the native preview
    w.ram[0xD0E7] = 8;               // Config: meat preview on
    w.ram[0xD71A + 3 * 32 + 31] = 1; // its current form
    w.ram[0xD715] = 5;               // the new form
    hooks::emit(hooks::Event::MeatMenu, m, {1});
    const auto battlefield = ui.picture(w, sawgod::emu::Wsc::ColorMode::Raw);
    hooks::emit(hooks::Event::MeatMenu, m, {2});
    const auto ram = w.ram;
    const auto io = w.io;
    const auto registers = w.cpu.s.r;
    hooks::emit(hooks::Event::MeatMenu, m, {3});
    auto picture = ui.picture(w, sawgod::emu::Wsc::ColorMode::Raw);
    // The drop announcement's battlefield shows between the two form panes, above the Eat/Don't window.
    CHECK(std::equal(battlefield.begin() + 10 * 224 + 82, battlefield.begin() + 10 * 224 + 142,
                     picture.begin() + 10 * 224 + 82));
    CHECK(picture[0] == fx::kBorder);             // the current form's pane
    CHECK(picture[142] == fx::kBorder);           // the resulting form's pane
    CHECK(picture[80 * 224 + 82] == fx::kBorder); // Eat/Don't
    CHECK(Chrome::text_width(host_text(w, "ui_battle_meat_prompt")) <= 212);
    w.ram[0xEC82] = 1; // unchanged form: the current form in both panes
    CHECK(ui.picture(w, sawgod::emu::Wsc::ColorMode::Raw) != picture);
    w.set_second(true);
    picture = ui.picture(w, sawgod::emu::Wsc::ColorMode::Raw, true);
    CHECK(picture.size() == 224 * 144);
    // The GB picture: the GB's blank backdrop, one color across the battlefield between the panes.
    CHECK(picture[10 * 224 + 90] == picture[70 * 224 + 130]);
    w.ram[0xEC82] = ram[0xEC82];
    CHECK(w.ram == ram);
    CHECK(w.io == io);
    CHECK(w.cpu.s.r == registers);
}

TEST_CASE("battle log: wraps within three visible rows and scrolls only the overflow") {
    using sawgod::game::battle_log_rows;
    using sawgod::game::Chrome;
    // short: the previous message above the newest
    auto rows = battle_log_rows(U"Grunt attacks with Long Sword", U"Bee Woman takes 12 damage");
    REQUIRE(rows.size() == 2);
    CHECK(rows[0] == U"Bee Woman takes 12 damage");
    CHECK(rows[1] == U"Grunt attacks with Long Sword");
    // long (God's attack with an eight-letter name): wrapped at a space, the previous line gives way
    rows = battle_log_rows(U"WWWWWWWW attacks with Let There Be Light!", U"Bee Woman takes 12 damage");
    REQUIRE(rows.size() == 3);
    CHECK(rows[0] == U"Bee Woman takes 12 damage");
    CHECK(rows[1] + U" " + rows[2] == U"WWWWWWWW attacks with Let There Be Light!");
    for (const auto &r : rows)
        CHECK(Chrome::text_width(r) <= sawgod::game::kBattleLogWidth);
    // no previous message
    rows = battle_log_rows(U"Shinsengumi counters with Matchlock!", U"");
    REQUIRE(rows.size() == 1);
}

TEST_CASE("enemy HP bars preserve visibility, picture centers and compact geometry") {
    using Wsc = sawgod::emu::Wsc;
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    fx::install(w);
    w.io[7] = 0x10;
    constexpr int columns[3][3] = {{12}, {7, 17}, {4, 12, 20}};
    for (int groups : {1, 2, 3}) {
        for (int picture_width : {3, 6, 9}) {
            CAPTURE(groups);
            CAPTURE(picture_width);
            w.ram[0xD6D4] = uint8_t(groups);
            w.io[0x12] = 2;
            w.io[0x13] = 1;
            std::fill(w.ram.begin() + 0x800, w.ram.begin() + 0x1000, 0);
            for (int g = 0; g < groups; ++g) {
                w.ram[0xD6D5 + g * 2] = uint8_t(g + 7);
                w.ram[0xD6D6 + g * 2] = 1;
                w.ram[0xD509 + g * 12] = uint8_t(picture_width);
                w.ram[0xD50A + g * 12] = 3;
                const int col = columns[groups - 1][g] - (picture_width == 6   ? 1
                                                          : picture_width == 9 ? 2
                                                                               : 0);
                const int row = picture_width == 9 ? 1 : 3;
                const int cell = 0x800 + (row * 32 + col) * 2;
                w.ram[cell + 1] = uint8_t(0x20 | (4 + g) << 1);
                const int actor = 0xD71A + (4 + g) * 32;
                w.ram[actor + 0x1F] = uint8_t(g + 7);
                w.ram[actor + 3] = uint8_t(12 + g);
                w.ram[actor + 5] = 99;
            }
            const auto ram = w.ram;
            const auto ordinary = w.framebuffer_rgb(Wsc::ColorMode::Raw);
            update_enemy_hp_boxes(w, true);
            const auto &boxes = Session::of(w).enemy_hp_boxes();
            REQUIRE(boxes.size() == size_t(groups));
            for (int g = 0; g < groups; ++g) {
                const auto &box = boxes[size_t(g)];
                CHECK(box.text == std::to_string(12 + g) + "/99");
                CHECK(box.width == std::max(32, Chrome::numerals_width(box.text) + 5));
                CHECK(box.hp == 12 + g);
                CHECK(box.max == 99);
                const int col = columns[groups - 1][g] - (picture_width == 6   ? 1
                                                          : picture_width == 9 ? 2
                                                                               : 0);
                CHECK(box.x + box.width / 2 == col * 8 + picture_width * 4 - 2);
                CHECK(box.y == (picture_width == 9 ? 2 : 15));
            }
            auto picture = ordinary;
            Wsc::UiFill fill;
            Wsc::UiText text;
            w.ui_fill = &fill;
            w.ui_text = &text;
            draw_enemy_hp_boxes(w, picture, Wsc::ColorMode::Raw);
            CHECK(picture != ordinary);
            for (const auto &box : boxes) { // red bars: a little full HP at the left, empty at the right
                const size_t row = size_t((box.y + 4) * Wsc::kWidth);
                CHECK(picture[row + size_t(box.x)] == Chrome::kEnemyHp.full);
                CHECK(picture[row + size_t(box.x + box.width - 1)] == Chrome::kEnemyHp.empty);
            }
            CHECK(w.ram == ram);
            CHECK(w.framebuffer_rgb(Wsc::ColorMode::Raw) == ordinary);
            w.ui_fill = nullptr;
            w.ui_text = nullptr;
            // Even a scroll that carries the picture's anchor off screen leaves the complete meter visible.
            w.io[0x12] = w.io[0x13] = 255;
            update_enemy_hp_boxes(w, true);
            for (const auto &box : Session::of(w).enemy_hp_boxes()) {
                CHECK(box.x >= 0);
                CHECK(box.x + box.width < Wsc::kWidth);
                CHECK(box.y >= 2);
                CHECK(box.y + 8 < Wsc::kHeight);
            }
            // The party-hit shake scrolls screen 2 to FDh/FEh (-3/-2): the bars move with their enemies
            // never to the clamped top-left corner.
            w.io[0x12] = 0xFD;
            w.io[0x13] = 0xFE;
            update_enemy_hp_boxes(w, true);
            REQUIRE(Session::of(w).enemy_hp_boxes().size() == size_t(groups));
            for (int g = 0; g < groups; ++g) {
                const auto &shaken = Session::of(w).enemy_hp_boxes()[size_t(g)];
                const int col = columns[groups - 1][g] - (picture_width == 6   ? 1
                                                          : picture_width == 9 ? 2
                                                                               : 0);
                CHECK(shaken.x + shaken.width / 2 == col * 8 + picture_width * 4 + 3);
                CHECK(shaken.y == (picture_width == 9 ? 2 : 18));
            }
            w.io[0x12] = 2;
            w.io[0x13] = 1;
            update_enemy_hp_boxes(w, true);
            // Dead group, no picture, or no living actor: no plate.
            w.ram[0xD6D6] = 0;
            update_enemy_hp_boxes(w, true);
            CHECK(Session::of(w).enemy_hp_boxes().size() == size_t(groups - 1));
            w.ram[0xD6D6] = 1;
            w.ram[0xD71A + 4 * 32 + 2] = 3;
            update_enemy_hp_boxes(w, true);
            CHECK(Session::of(w).enemy_hp_boxes().size() == size_t(groups - 1));
            w.ram[0xD71A + 4 * 32 + 2] = 0;
            std::fill(w.ram.begin() + 0x800, w.ram.begin() + 0x1000, 0);
            update_enemy_hp_boxes(w, true);
            CHECK(Session::of(w).enemy_hp_boxes().empty());
            update_enemy_hp_boxes(w, false);
            picture = ordinary;
            draw_enemy_hp_boxes(w, picture, Wsc::ColorMode::Raw);
            CHECK(picture == ordinary);
        }
    }
}

TEST_CASE("battle log: sentence breaks, three-line capacity, and Japanese breaks") {
    const auto rows = battle_log_rows(U"Name{n}attacks.\nNext sentence!{lf}Last?", U"");
    REQUIRE(rows.size() == 3);
    CHECK(rows[0] == U"Name attacks.");
    CHECK(rows[1] == U"Next sentence!");
    CHECK(rows[2] == U"Last?");
    CHECK(battle_log_rows(U"One!{n}Two!{n}Three!{n}Four!", U"", 0)[0] == U"One!");
    CHECK(battle_log_rows(U"One!{n}Two!{n}Three!{n}Four!", U"", 100)[0] == U"Two!");
    CHECK(battle_log_rows(U"あ{n}い", U"", 0, false).size() == 2);
}

TEST_CASE("three battle rows keep full descenders and shadows clear of the shared party divider") {
    sawgod::emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    fx::install(w);
    // Let Chrome recognize a WSC font with shadows and a GB font without them.
    constexpr uint16_t font_probe = 0x4000 + 0x10B * 32;
    w.ram[font_probe] = 0x12;
    w.set_second(true);
    w.shadow_write8(font_probe, 0x20);
    BattleUi ui;
    ui.install(w);
    auto &session = Session::of(w);
    std::string error;
    REQUIRE(session.text().load("key,text\nui_battle_fight,Fight\n", Charset::system(), &error, nullptr,
                                nullptr, true));
    session.text().set_message_run(U"gyp!{n}gyp!{n}gyp!");
    Machine m(w);
    hooks::emit(hooks::Event::BattleStart, m);
    hooks::emit(hooks::Event::BattleMenu, m);
    w.ram[0xD6E2] = 6;
    w.ram[0xD60E] = 1;
    m.wr16(0, 0xD55A, kTextWord | TextTable::kMessageRun);
    m.wr16(0, 0xD55C, 0xFFFF);
    hooks::emit(hooks::Event::StatusPopup, m, {0, 0xD55A, 0});
    for (bool second : {false, true}) {
        std::vector<Chrome::Draw> rows;
        Chrome::observe = [&](const auto &d) {
            if (d.kind == Chrome::Draw::Kind::Text && d.text == U"gyp!")
                rows.push_back(d);
        };
        ui.picture(w, sawgod::emu::Wsc::ColorMode::Raw, second);
        Chrome::observe = {};
        REQUIRE(rows.size() == 3);
        for (size_t i = 0; i < rows.size(); ++i) {
            CHECK(rows[i].y == 81 + int(i) * 10);
            CHECK(rows[i].cut_rows == 0);
            CHECK(rows[i].bottom <= 111); // row 111 stays clear before the party border at 112
        }
        CHECK(rows.back().bottom == (second ? 110 : 111));
    }
}

TEST_CASE("native auto-target selector retains the clean battlefield and observes the native choice") {
    using Wsc = sawgod::emu::Wsc;
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    Machine m(w);
    fx::install(w);
    BattleUi ui;
    ui.install(w);
    hooks::emit(hooks::Event::BattleStart, m);
    hooks::emit(hooks::Event::BattleMenu, m);
    const auto clean = ui.picture(w, Wsc::ColorMode::Raw);
    hooks::emit(hooks::Event::BattleAutoTargetSetting, m, {1});
    REQUIRE(ui.auto_target_setting());
    // The native panel may replace any of the battlefield's cells while the host keeps its picture.
    std::fill(w.ram.begin() + 0x1000, w.ram.begin() + 0x1800, 0xFF);
    for (bool on : {false, true}) {
        w.ram[0xD843] = on;
        const auto before = w.snapshot();
        std::vector<Chrome::Draw> draws;
        Chrome::observe = [&](const auto &d) { draws.push_back(d); };
        const auto picture = ui.picture(w, Wsc::ColorMode::Raw);
        Chrome::observe = {};
        CHECK(std::equal(picture.begin(), picture.begin() + kBattleLogTop * 224, clean.begin()));
        CHECK(std::any_of(draws.begin(), draws.end(), [on](const auto &d) {
            return d.kind == Chrome::Draw::Kind::Cursor && d.x == (on ? 102 : 6) && d.y == 101;
        }));
        CHECK(w.ram == before.ram);
        CHECK(w.io == before.io);
        CHECK(w.frame_count == before.frame_count);
    }
    hooks::emit(hooks::Event::BattleAutoTargetSetting, m, {0});
    CHECK_FALSE(ui.auto_target_setting());
}

TEST_CASE("battle end logs survive damage presentation and the native victory A-wait") {
    using Wsc = sawgod::emu::Wsc;
    for (bool second : {false, true}) {
        Wsc w(std::vector<uint8_t>(0x400000), 0);
        fx::install(w);
        w.set_second(true);
        BattleUi ui;
        ui.install(w);
        Machine m(w);
        hooks::emit(hooks::Event::BattleStart, m);
        hooks::emit(hooks::Event::BattleMenu, m);
        hooks::emit(hooks::Event::BattleRoundStart, m);
        w.ram[0xD6E2] = 6;
        hooks::emit(hooks::Event::BattleHpChange, m, {4, 1, 0});
        w.ram[0xDBFF] = 4;
        w.ram[0xD71A + 4 * 32 + 2] = 1; // knockout precedes the battle-end flag
        w.ram[0xD60F] = 0x82;
        w.ram[0xD610] = 0;
        w.ram[0xD611] = w.ram[0xD612] = 255;
        auto visible = [&] {
            const auto before = w.snapshot();
            std::vector<Chrome::Draw> draws;
            Chrome::observe = [&](const auto &draw) { draws.push_back(draw); };
            ui.picture(w, Wsc::ColorMode::Raw, second);
            Chrome::observe = {};
            CHECK(std::any_of(draws.begin(), draws.end(), [](const auto &draw) {
                return draw.kind == Chrome::Draw::Kind::Text && draw.y == 81 && !draw.text.empty();
            }));
            CHECK(w.ram == before.ram);
            CHECK(w.frame_count == before.frame_count);
        };
        hooks::emit(hooks::Event::StatusPopup, m, {0, 0xD60F, 0});
        visible(); // last death, while DCC2 and the popup row counter are still zero
        w.ram[0xDCC2] = 2;
        hooks::emit(hooks::Event::StatusPopup, m, {0, 0xD60F, 0});
        visible(); // victory typing
        hooks::emit(hooks::Event::BattleRewards, m);
        for (int frame = 0; frame < 120; ++frame) {
            ++w.frame_count;
            ui.frame(w);
        }
        visible(); // waiting for A must retain the victory message
    }
}

TEST_CASE("a party stat over 99 shows no change icon while the victory message restores it") {
    // The battle caps a stat above 99 at 99 in the party record and restores the original before the victory
    // message (store_party_battle_records). The card must not compare with the restored value and alternate.
    using Wsc = sawgod::emu::Wsc;
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    fx::install(w);
    BattleUi ui;
    ui.install(w);
    Machine m(w);
    hooks::emit(hooks::Event::BattleStart, m);
    hooks::emit(hooks::Event::BattleMenu, m);
    hooks::emit(hooks::Event::BattleRewards, m);
    // Member 0's native status icon: four visible sprite pieces whose tile is solid ink.
    const uint16_t icon = 0x1F0;
    std::fill_n(w.ram.begin() + 0x4000 + icon * 32, 32, uint8_t(0x11));
    for (int i = 0; i < 4; ++i) {
        w.ram[size_t(0xC02C + i * 4)] = uint8_t(icon);
        w.ram[size_t(0xC02C + i * 4 + 1)] = uint8_t(icon >> 8);
        w.ram[size_t(0xC02C + i * 4 + 2)] = uint8_t(120 + (i / 2) * 8);
        w.ram[size_t(0xC02C + i * 4 + 3)] = uint8_t(8 + (i % 2) * 8);
    }
    w.ram[0xD71A + 2] = 0; // present, no ailment
    for (int stat = 0; stat < 3; ++stat)
        w.ram[size_t(0xD71A + 7 + stat)] = 99; // the battle record: capped at 99
    w.frame_count = 30;                        // the icon half of the alternation
    const auto picture = [&](uint8_t defense) {
        w.ram[0xD000 + 11] = w.ram[0xD000 + 13] = 99;
        w.ram[0xD000 + 12] = defense;
        return ui.picture(w, Wsc::ColorMode::Raw);
    };
    const auto capped = picture(99);
    CHECK(picture(123) == capped); // the restored over-99 original: still no change
    CHECK(picture(50) != capped);  // a real difference keeps its icon
}
