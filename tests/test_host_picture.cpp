#include "chrome_fixture.h"
#include "engine/fidelity.h"
#include "engine/input.h"
#include "engine/version.h"
#include "game/battle_ui.h"
#include "game/bestiary_ui.h"
#include "game/creation_ui.h"
#include "game/dialogue_ui.h"
#include "game/gb_intro.h"
#include "game/host_picture.h"
#include "game/inventory_ui.h"
#include "game/item_dialog_ui.h"
#include "game/party_ui.h"
#include "game/save_ui.h"
#include "game/service_ui.h"
#include "game/session.h"
#include "game/text.h"
#include "game/text_render.h"
#include "game/title_logo.h"
#include "game/ui_chrome.h"
#include <doctest/doctest.h>
using namespace sawgod::game;
using sawgod::emu::Wsc;

TEST_CASE("Title lettering replaces only English menu words in both pictures") {
    for (bool second : {false, true}) {
        Wsc w(std::vector<uint8_t>(0x400000), 0);
        chrome_fixture::install(w);
        w.cpu.s.halted = true;
        w.cpu.s.flags &= uint16_t(~sawgod::emu::IF);
        w.io[0] = 2;    // SCR2 alone: a solid synthetic picture above a distinct backdrop.
        w.io[1] = 0xFA; // Distinct backdrop from the fixture's palette 15.
        w.io[7] = 0x10;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                chrome_fixture::put(w, 1, x, y, 5);
        for (int at = 0x800; at < 0x1000; at += 2) {
            w.ram[size_t(at)] = 1;
            w.ram[size_t(at + 1)] = 14 << 1;
        }
        w.ram[0xFE00 + 14 * 32 + 5 * 2] = 0xFF;
        w.ram[0xFE00 + 14 * 32 + 5 * 2 + 1] = 0x0F;
        if (second)
            w.set_second(true);
        HostPicture picture;
        picture.install(w, {});
        w.run_frame();
        w.run_frame();
        Machine m(w);
        hooks::emit(hooks::Event::TitleMenu, m, {3});
        const auto native = w.framebuffer_rgb(Wsc::ColorMode::Raw, second);
        REQUIRE(picture.picture(w, Wsc::ColorMode::Raw, second) == native); // Japanese
        std::string error;
        REQUIRE(Session::of(w).text().load("key,text\ntitle_new_game,Begin\n", Charset::system(), &error,
                                           nullptr, nullptr, true));
        const auto ram = w.ram;
        const auto io = w.io;
        const auto before = w.framebuffer(second);
        std::vector<Chrome::Draw> labels;
        Chrome::observe = [&](const Chrome::Draw &d) { labels.push_back(d); };
        const auto english = picture.picture(w, Wsc::ColorMode::Raw, second);
        Chrome::observe = {};
        REQUIRE(labels.size() == 3);
        CHECK(labels[0].text == U"Begin");
        CHECK(labels[1].text == U"Continue");
        CHECK(labels[2].text == U"Original");
        CHECK(labels[0].y == 97);
        CHECK(labels[1].y == 105);
        CHECK(labels[2].y == 113);
        CHECK(bool(english != native));
        for (int y = 0; y < Wsc::kHeight; ++y)
            for (int x = 0; x < Wsc::kWidth; ++x)
                if (x < 95 || y < 96 || y > 122)
                    CHECK(english[size_t(y * Wsc::kWidth + x)] == native[size_t(y * Wsc::kWidth + x)]);
        CHECK(w.ram == ram);
        CHECK(w.io == io);
        CHECK(w.framebuffer(second) == before);
        hooks::emit(hooks::Event::TitleMenu, m, {0});
        CHECK(picture.picture(w, Wsc::ColorMode::Raw, second) == native);
        hooks::emit(hooks::Event::TitleMenu, m, {3});
        Session::of(w).new_generation();
        CHECK(picture.picture(w, Wsc::ColorMode::Raw, second) == native);
    }
}

namespace {
struct Pictures {
    Wsc w{std::vector<uint8_t>(0x400000), 0};
    BattleUi battle;
    InventoryUi inventory;
    ItemDialogUi item;
    PartyUi party;
    CreationUi creation;
    BestiaryUi bestiary;
    DialogueUi dialogue;
    ServiceUi service;
    SaveUi save;
    HostPicture host;
    Machine m{w};
    bool second;
    Pictures(bool gb) : second(gb) {
        chrome_fixture::install(w);
        w.cpu.s.halted = true;
        w.cpu.s.flags &= uint16_t(~sawgod::emu::IF);
        std::fill(w.ram.begin() + 0xD000, w.ram.begin() + 0xD09E, 255);
        w.ram[0xD840] = w.ram[0xD841] = w.ram[0xD842] = w.ram[0xD843] = 0;
        w.ram[0xD852] = w.ram[0xD853] = w.ram[0xD854] = 0;
        if (gb) {
            w.set_second(true);
            w.upper_palette_greys = true;
        }
        battle.install(w);
        inventory.install(w);
        item.install(w, &inventory);
        party.install(w);
        creation.install(w);
        bestiary.install(w);
        dialogue.install(w);
        service.install(w);
        save.install(w);
        host.install(w,
                     {&battle, &inventory, &item, &party, &creation, &bestiary, &dialogue, &service, &save});
        step();
        step();
    }
    void emit(hooks::Event e, int a, int b = 0) { hooks::emit(e, m, {a, b}); }
    std::vector<uint32_t> pixels() { return host.picture(w, Wsc::ColorMode::Raw, second); }
    void step() {
        w.run_frame();
        host.frame(w);
    }
    std::vector<uint32_t> direct(hooks::Event e) {
        switch (e) {
        case hooks::Event::PartyMenu:
            return party.picture(w, Wsc::ColorMode::Raw, second);
        case hooks::Event::InventoryMenu:
            return inventory.picture(w, Wsc::ColorMode::Raw, second);
        case hooks::Event::ItemDialog:
        case hooks::Event::ItemMessage:
            return item.picture(w, Wsc::ColorMode::Raw, second);
        case hooks::Event::ServiceMenu:
            return service.picture(w, Wsc::ColorMode::Raw, second);
        case hooks::Event::SaveMenu:
            return save.picture(w, Wsc::ColorMode::Raw, second);
        case hooks::Event::CreationMenu:
        case hooks::Event::NameEntry:
            return creation.picture(w, Wsc::ColorMode::Raw, second);
        case hooks::Event::Bestiary:
            return bestiary.picture(w, Wsc::ColorMode::Raw, second);
        case hooks::Event::Dialogue:
            return dialogue.picture(w, Wsc::ColorMode::Raw, second);
        case hooks::Event::BattleMenu:
        case hooks::Event::MeatMenu:
        case hooks::Event::BattleRewards:
            return battle.picture(w, Wsc::ColorMode::Raw, second);
        default:
            return w.framebuffer_rgb(Wsc::ColorMode::Raw, second);
        }
    }
    // Independently composed destination, with hostile partial native state between endpoints.
    // Check every physical raster, including readiness arriving after the OAM latch or mid-picture.
    void transition(hooks::Event e, int scene, bool open = false, bool close = false, int submission = 1) {
        const auto before = pixels();
        const auto memory = w.ram;
        const auto ports = w.io;
        if (e == hooks::Event::BattleMenu && open)
            emit(hooks::Event::BattleStart, 0);
        if (e == hooks::Event::BattleMenu)
            w.ram[0xD6E2] = uint8_t(scene);
        emit(e == hooks::Event::BattleMenu && close ? hooks::Event::BattleEnd : e, scene,
             open    ? 1
             : close ? -1
                     : 0);
        if (e == hooks::Event::PartyMenu && open)
            emit(e, scene);
        for (int phase = 0; phase < 4; ++phase) {
            w.ram[0xFE00 + 15 * 32 + 4] = uint8_t(phase * 3);
            w.ram[0x4000 + 0x1D5 * 32] = 0;
            w.io[0x10] = uint8_t(phase * 8);
            w.ram[0x0202] = uint8_t(phase * 31);
            if (second)
                w.shadow_release(0, 0x10000);
            step();
            CHECK(bool(pixels() == before));
        }
        w.ram = memory;
        w.io = ports;
        if (second)
            w.shadow_release(0, 0x10000);
        emit(hooks::Event::PresentationPicture, submission);
        std::vector<uint32_t> after;
        // Advance the unchanged graphics through scanout for the independent destination picture.
        // A generation stamp is not the oracle: every intermediate pixel must match an endpoint.
        for (int phase = 0; phase < 4; ++phase) {
            step();
            const auto frame = pixels();
            after = direct(e);
            CHECK((frame == before || frame == after));
        }
        CHECK(bool(pixels() == after));
        const auto ram = w.ram;
        const auto io = w.io;
        const auto cpu = w.cpu.s;
        pixels();
        CHECK(w.ram == ram);
        CHECK(w.io == io);
        CHECK(std::equal(std::begin(w.cpu.s.r), std::end(w.cpu.s.r), std::begin(cpu.r)));
        CHECK(std::equal(std::begin(w.cpu.s.seg), std::end(w.cpu.s.seg), std::begin(cpu.seg)));
        CHECK(w.cpu.s.ip == cpu.ip);
        CHECK(w.cpu.s.flags == cpu.flags);
    }
};
} // namespace
TEST_CASE("every host scene has atomic open each submenu edge cancel and confirm-close") {
    using E = hooks::Event;
    struct Screen {
        E event;
        int count;
        const char *name;
    };
    const Screen screens[] = {{E::PartyMenu, 9, "party/status/equip/order"},
                              {E::InventoryMenu, 2, "inventory"},
                              {E::ItemDialog, 5, "target/refill/travel/overflow/crystals"},
                              {E::ItemMessage, 2, "unusable/wearing messages"},
                              {E::ServiceMenu, 12, "shops/inn/revival/guild and attached panes"},
                              {E::SaveMenu, 7, "save/load and confirmations"},
                              {E::CreationMenu, 1, "creation"},
                              {E::NameEntry, 1, "name"},
                              {E::Bestiary, 6, "bestiary"},
                              {E::Dialogue, 1, "dialogue"},
                              {E::BattleMenu, 8, "battle commands/targets/equipment"}};
    for (int mode = 0; mode < 3; ++mode) {
        INFO("graphics = ", mode == 0 ? "WSC" : mode == 1 ? "GB" : "Enhanced base");
        for (const auto &screen : screens) {
            INFO("screen = ", std::string(screen.name));
            // Test each directed scene edge, including backward/nested returns, rather
            // than only walking 1..N. Confirm and cancel are separate exits at this seam.
            for (int from = 1; from <= screen.count; ++from) {
                for (int to = 1; to <= screen.count; ++to) {
                    if (from == to)
                        continue;
                    Pictures p(mode == 1);
                    p.transition(screen.event, from, true);
                    p.transition(screen.event, to);
                    p.transition(screen.event, 0, false, true);
                }
                for (const char *exit : {"cancel", "confirm that closes"}) {
                    INFO("exit = ", exit, "; scene = ", from);
                    Pictures p(mode == 1);
                    p.transition(screen.event, from, true);
                    p.transition(screen.event, 0, false, true);
                }
            }
        }
    }
}
TEST_CASE("battle command to action is submitted by the producer without a keypad poll") {
    for (bool gb : {false, true}) {
        Pictures p(gb);
        p.transition(hooks::Event::BattleMenu, 1, true);
        const auto command = p.pixels();
        p.emit(hooks::Event::BattleRoundStart, 0);
        p.step();
        CHECK(bool(p.pixels() == command));
        // Existing action-frame uploader submits the completed battle picture.
        p.emit(hooks::Event::PresentationPicture, 5);
        for (int i = 0; i < 4; ++i)
            p.step();
        const auto action = p.pixels();
        CHECK_FALSE(p.host.holding());
        CHECK(bool(action == p.battle.picture(p.w, Wsc::ColorMode::Raw, gb)));
        // The rewards/meat windows use the same shared gate.
        p.emit(hooks::Event::BattleRewards, 0);
        p.transition(hooks::Event::MeatMenu, 1, false, false, 5);
        p.transition(hooks::Event::MeatMenu, 2, false, false, 5);
        p.transition(hooks::Event::MeatMenu, 3, false, false, 5);
        p.transition(hooks::Event::MeatMenu, 0, false, true, 5);
    }
}
TEST_CASE("party logical menu input scope survives picture handoffs and ends on final return") {
    Pictures p(false);
    p.emit(hooks::Event::PartyMenu, 0, 1);
    p.emit(hooks::Event::PartyMenu, 1);
    CHECK(p.party.input_active());
    p.emit(hooks::Event::PartyMenu, 0, 2);
    CHECK_FALSE(p.party.active());
    CHECK(p.party.input_active());
    p.emit(hooks::Event::PartyMenu, 0, -1);
    CHECK_FALSE(p.party.input_active());
}

TEST_CASE("GB full pages supersede retained menus without changing the WSC handoff") {
    Pictures p(true);
    const auto wsc = p.host.picture(p.w, Wsc::ColorMode::Raw, false);
    p.emit(hooks::Event::InventoryMenu, 1);
    CHECK(p.host.picture(p.w, Wsc::ColorMode::Raw, false) == wsc);
    CHECK(p.host.holding());
    p.w.second_scene_image.assign(Wsc::kWidth * Wsc::kHeight, 0xFFF);
    p.w.second_scene_above_host = true;
    p.step();
    p.step();
    const auto ram = p.w.ram;
    const auto native = p.w.framebuffer();
    const auto page = p.pixels();
    CHECK_FALSE(p.host.holding());
    CHECK(std::all_of(page.begin(), page.end(), [](auto pixel) { return pixel == 0xFFFFFF; }));
    CHECK(p.host.picture(p.w, Wsc::ColorMode::Raw, false) == wsc);
    CHECK(p.host.holding());
    CHECK(p.w.ram == ram);
    CHECK(p.w.framebuffer() == native);
    p.w.second_scene_image.clear();
    p.w.second_scene_above_host = false;
    CHECK(p.pixels() == page); // retain the page's black/white exit until the next native picture is ready
    CHECK(p.host.holding());
}

TEST_CASE("a hidden picture retains its own endpoint before a handoff") {
    Pictures p(true);
    p.w.second_visible = false;
    p.step();
    p.step();
    const auto wsc = p.host.picture(p.w, Wsc::ColorMode::Raw, false); // only the WSC picture is on screen
    p.emit(hooks::Event::InventoryMenu, 1);
    p.w.second_visible = true; // a step down during the handoff
    p.step();
    CHECK(p.host.picture(p.w, Wsc::ColorMode::Raw, true) == p.w.framebuffer_rgb(Wsc::ColorMode::Raw, true));
    CHECK(p.host.holding());
    (void)wsc;
}

TEST_CASE("Native world cards supersede retained Continue menus in Japanese and English") {
    Pictures p(false);
    p.transition(hooks::Event::SaveMenu, 1, true);
    const auto menu = p.pixels();
    p.emit(hooks::Event::SaveMenu, 0);
    CHECK(p.pixels() == menu);
    CHECK(p.host.holding());
    const auto native = p.w.framebuffer_rgb(Wsc::ColorMode::Raw);
    CHECK(native != menu);
    const auto ram = p.w.ram;
    const auto io = p.w.io;
    p.emit(hooks::Event::IntroPresentation, int(IntroStage::WorldCard), 1);
    CHECK(p.pixels() == native); // Japanese keeps the native card, not the old menu.
    CHECK_FALSE(p.host.holding());
    std::string error;
    REQUIRE(Session::of(p.w).text().load("key,text\n", Charset::system(), &error, nullptr, nullptr, true));
    p.emit(hooks::Event::IntroPresentation, int(IntroStage::Fade), 15);
    p.emit(hooks::Event::IntroPresentation, int(IntroStage::WorldCardContent));
    for (int row = 0; row < 32; ++row)
        hooks::emit(hooks::Event::IntroPresentation, p.m, {int(IntroStage::WorldCardRow), row, 224});
    auto expected = p.w.framebuffer_rgb(Wsc::ColorMode::Raw, false, false, true);
    p.host.world_cards().composite(p.w, expected);
    CHECK(p.pixels() == expected);
    CHECK(p.pixels() == expected);
    CHECK(p.w.ram == ram);
    CHECK(p.w.io == io);
}

TEST_CASE("Opening lettering follows native column copies and window, without changing the machine") {
    Wsc w(std::vector<uint8_t>(0x400000), 0);
    chrome_fixture::install(w);
    w.cpu.s.halted = true;
    w.cpu.s.flags &= uint16_t(~sawgod::emu::IF);
    w.run_frame();
    HostPicture host;
    host.install(w, {});
    Machine m(w);
    m.bp() = 0x46C0;
    // Fake native strip descriptor: 20 tile columns, four plane copies each.
    w.ram[0x46C4] = 20;
    w.ram[0xFE00 + 4 * 32 + 2] = 0xFF;
    w.ram[0xFE00 + 4 * 32 + 3] = 0x0F;
    m.cs() = 0;
    auto emit = [&](IntroStage stage, int b = 0) {
        hooks::emit(hooks::Event::IntroPresentation, m, {int(stage), b});
    };
    emit(IntroStage::Caption, 1);
    hooks::emit(hooks::Event::IntroCaptionStrip, m, {1, 40, 80});
    const auto native = w.framebuffer_rgb(Wsc::ColorMode::Raw);
    CHECK(host.picture(w, Wsc::ColorMode::Raw) == native); // Japanese bypass
    std::string error;
    REQUIRE(Session::of(w).text().load("key,text\n", Charset::system(), &error, nullptr, nullptr, true));
    emit(IntroStage::Caption, 1);
    hooks::emit(hooks::Event::IntroCaptionStrip, m, {1, 40, 80});
    const auto ram = w.ram;
    const auto io = w.io;
    const auto cpu = w.cpu.s;
    const auto half = host.picture(w, Wsc::ColorMode::Raw);
    hooks::emit(hooks::Event::IntroCaptionStrip, m, {1, 80, 80});
    const auto full = host.picture(w, Wsc::ColorMode::Raw);
    CHECK(bool(half != full));
    CHECK(host.picture(w, Wsc::ColorMode::Raw) == full);         // drawing cannot advance it
    CHECK(host.picture(w, Wsc::ColorMode::Raw, true) == native); // GB bypass
    CHECK(w.ram == ram);
    CHECK(w.io == io);
    CHECK(w.cpu.s.ip == cpu.ip);
    CHECK(w.cpu.s.flags == cpu.flags);
    CHECK(w.framebuffer_rgb(Wsc::ColorMode::Raw) == native);
    emit(IntroStage::White);
    CHECK(host.picture(w, Wsc::ColorMode::Raw) == native);
    emit(IntroStage::Prologue);
    w.io[0] = 0x23;
    w.io[9] = 0;
    w.io[11] = 0;
    const auto hidden = host.picture(w, Wsc::ColorMode::Raw);
    w.io[11] = 143;
    const auto shown = host.picture(w, Wsc::ColorMode::Raw);
    CHECK(hidden != shown);
    CHECK(host.picture(w, Wsc::ColorMode::Raw) == shown);
    emit(IntroStage::Off);
    CHECK(host.picture(w, Wsc::ColorMode::Raw) == native);
    emit(IntroStage::Prologue);
    w.reset();
    CHECK(host.picture(w, Wsc::ColorMode::Raw) == w.framebuffer_rgb(Wsc::ColorMode::Raw));
}

TEST_CASE("held host pictures retain fill and text metadata and world cards clear it") {
    Pictures p(false);
    Wsc::UiFill fill;
    p.w.ui_fill = &fill;
    Wsc::UiText text;
    p.w.ui_text = &text;
    p.transition(hooks::Event::SaveMenu, 1, true);
    fill.clear();
    const auto menu = p.pixels();
    REQUIRE_FALSE(fill.windows.empty());
    const auto saved = fill;
    const auto saved_text = text;
    REQUIRE(std::any_of(text.pixels.begin(), text.pixels.end(),
                        [](auto p) { return p.kind == Wsc::UiText::Stroke; }));
    text.clear();
    p.emit(hooks::Event::SaveMenu, 0);
    fill.clear();
    CHECK(p.pixels() == menu);
    REQUIRE(p.host.holding());
    CHECK(fill.windows == saved.windows);
    CHECK(fill.owner == saved.owner);
    CHECK(fill.color == saved.color);
    CHECK(text.pixels == saved_text.pixels);
    p.emit(hooks::Event::IntroPresentation, int(IntroStage::WorldCard), 1);
    p.pixels();
    CHECK(fill.windows.empty());
    CHECK(std::all_of(text.pixels.begin(), text.pixels.end(),
                      [](auto p) { return p.kind == Wsc::UiText::None; }));
    CHECK(std::all_of(fill.owner.begin(), fill.owner.end(), [](auto owner) { return owner == 0; }));
}

TEST_CASE("held battle pictures mirror bar rectangles and shadow underlays with the shown pixels") {
    Pictures p(false);
    Wsc::UiText text;
    p.w.ui_text = &text;
    p.transition(hooks::Event::BattleMenu, 1, true);
    text.clear();
    const auto battle = p.pixels();
    const auto bars = text.bars;
    REQUIRE(bars.rectangles.size() == 4);
    CHECK(std::any_of(bars.pixels.begin(), bars.pixels.end(), [](auto p) { return p.shadow; }));
    text.clear();
    p.emit(hooks::Event::BattleEnd, 0);
    CHECK(p.pixels() == battle);
    REQUIRE(p.host.holding());
    CHECK(text.bars.rectangles == bars.rectangles);
    CHECK(text.bars.pixels == bars.pixels);
    p.emit(hooks::Event::IntroPresentation, int(IntroStage::WorldCard), 1);
    p.pixels();
    CHECK(text.bars.rectangles.empty());
    CHECK(std::all_of(text.bars.pixels.begin(), text.bars.pixels.end(), [](auto p) { return !p.owner; }));
}

TEST_CASE("Title hints sit on the copyright notice in every menu state with fixed host-font labels in both "
          "languages") {
    using namespace sawgod;
    for (bool second : {false, true}) {
        Wsc w(std::vector<uint8_t>(0x400000), 0);
        chrome_fixture::install(w);
        w.ram[0xFE00 + 14 * 32 + 5 * 2] = 0xFF;
        w.ram[0xFE00 + 14 * 32 + 5 * 2 + 1] = 0x0F;
        w.ram[0xFE00 + 14 * 32 + 1 * 2] = 0;
        w.ram[0xFE00 + 14 * 32 + 1 * 2 + 1] = 0;
        if (second) {
            w.set_second(true);
            w.shadow_write16(0xFE00 + 14 * 32 + 5 * 2, 0);
        }
        TitleMenuLettering title;
        title.attach(w);
        Machine m(w);
        for (bool english : {false, true}) {
            if (english) {
                std::string error;
                REQUIRE(Session::of(w).text().load("key,text\ntitle_new_game,New Game\n", Charset::system(),
                                                   &error, nullptr, nullptr, true));
            }
            for (int rows : {0, 1, 2, 3}) {
                w.frame_count = 0;
                hooks::emit(hooks::Event::TitleMenu, m, {rows, rows == 0});
                for (auto level : {FidelityMode::Gb, FidelityMode::Wsc, FidelityMode::Enhanced}) {
                    std::vector<uint32_t> steady_version;
                    for (int phase = 0; phase < 16; ++phase) {
                        w.frame_count = uint64_t(phase * 15);
                        const int beat = int(w.frame_count % 128),
                                  pulse = std::min(15, (beat < 64 ? beat : 128 - beat) / 4);
                        // PUSH ANY BUTTON reads its pulse from palette 13; the menu follows its clock.
                        w.ram[0xC5A0] = uint8_t(pulse);
                        w.ram[0xC7A0] = 15;
                        // the title page: white on GB, black on WSC/Enhanced
                        std::vector<uint32_t> base(224 * 144, second ? 0xFFFFFF : 0x000000);
                        title.composite(w, base, Wsc::ColorMode::Raw, second);
                        auto pixels = base;
                        std::vector<Chrome::Draw> labels;
                        Chrome::observe = [&](const auto &d) { labels.push_back(d); };
                        title.composite(w, pixels, Wsc::ColorMode::Raw, second,
                                        {level, "{icon:key:G}", "{icon:key:H}"});
                        Chrome::observe = {};
                        std::vector<bool> version_mask(224 * 144);
                        std::vector<uint32_t> version_pixels;
                        for (const auto &d : labels)
                            if (d.text == utf8_to_u32(kVersionString)) {
                                for (auto at : d.ink) {
                                    version_mask[at] = true;
                                    version_pixels.push_back(pixels[at]);
                                }
                                for (auto at : d.strokes)
                                    CHECK(pixels[at] == (second ? 0x555555 : 0x808080));
                            }
                        bool left = false, right = false, middle = false;
                        for (int y = 0; y < 144; ++y)
                            for (int x = 0; x < 224; ++x) {
                                if (pixels[size_t(y * 224 + x)] == base[size_t(y * 224 + x)])
                                    continue;
                                REQUIRE(((x >= 4 && x < 76) ||
                                         (version_mask[size_t(y * 224 + x)] && rows < 3) ||
                                         (x >= 148 && x < 220)));
                                REQUIRE(y >= 112);
                                REQUIRE(y < 128); // on the copyright notice, which begins at 128
                                if (version_mask[size_t(y * 224 + x)])
                                    middle = true;
                                else {
                                    left |= x < 76;
                                    right |= x >= 148;
                                }
                            }
                        // Like PUSH ANY BUTTON, fully out at the pulse's trough: GB steps
                        // through its greys to the white page, so its faintest steps are out too.
                        const bool shown = second ? pulse * 100 / 15 >= 25 : pulse > 0;
                        CHECK(left == (shown && level != FidelityMode::Gb));
                        CHECK(right == (shown && level != FidelityMode::Enhanced));
                        // Only the side hints pulse. The version stays identical even at their trough;
                        // the third menu row still owns the middle.
                        CHECK(middle == (rows < 3));
                        if (phase == 0)
                            steady_version = version_pixels;
                        else
                            CHECK(version_pixels == steady_version);
                        int names = 0, versions = 0;
                        for (const auto &d : labels) {
                            if (d.x >= 76 && d.x < 148 && d.y >= 114) {
                                CHECK(d.y == 115);
                                CHECK(d.text == utf8_to_u32(kVersionString));
                                const int width =
                                    std::min(Chrome::text_width(utf8_to_u32(kVersionString)), 72);
                                CHECK(d.x == (224 - width) / 2 + 9);
                                CHECK(d.cut_rows == 0);
                                CHECK(d.left >= 76);
                                CHECK(d.right <= 158);
                                ++versions;
                            }
                            if (d.x < 76 || d.x >= 148) {
                                for (auto at : d.ink)
                                    CHECK_FALSE(version_mask[at]);
                                CHECK(d.y == 115);
                                CHECK(d.cut_rows == 0);
                                CHECK(d.right - d.left <= d.width);
                                const auto expected =
                                    d.x < 76 ? (level == FidelityMode::Wsc ? U"GB" : U"WSC")
                                             : (level == FidelityMode::Gb ? U"WSC" : U"Enhanced");
                                CHECK(d.text == expected);
                                ++names;
                            }
                        }
                        CHECK(names == (!shown ? 0 : level == FidelityMode::Wsc ? 2 : 1));
                        CHECK(versions == (rows < 3 ? 1 : 0));
                    }
                }
            }
        }
    }
}

TEST_CASE("Title bindings share Options first-slot resolution and refresh without advancing the game") {
    using namespace sawgod;
    using namespace sawgod::ui;
    InputMap map = InputMap::defaults();
    auto bound = [&](WscButton b, bool pad, int family) {
        return bound_button_text(map, b, pad, family, SDL_GAMEPAD_TYPE_PS5);
    };
    CHECK(bound(WscButton::FidelityDown, false, 1) == "{icon:key:G}");
    CHECK(bound(WscButton::FidelityUp, true, 1) == "{icon:x:RB}");
    CHECK(bound(WscButton::FidelityUp, true, 2) == "{icon:p:R1}");
    CHECK(bound(WscButton::FidelityUp, true, 3) == "{icon:n:Switch R}");
    map.assign(WscButton::FidelityDown, 0, {Binding::Kind::Key, SDL_SCANCODE_J});
    CHECK(bound(WscButton::FidelityDown, false, 1) == "{icon:key:J}");
    map.assign(WscButton::FidelityUp, 1, {Binding::Kind::PadButton, SDL_GAMEPAD_BUTTON_NORTH});
    map.clear(WscButton::FidelityUp, true, 0);
    CHECK(bound(WscButton::FidelityUp, true, 2) == "{icon:p:PS triangle}");
    map.clear(WscButton::FidelityUp, true, 1);
    CHECK(bound(WscButton::FidelityUp, true, 2) == "-"); // never borrow the keyboard binding

    Wsc w(std::vector<uint8_t>(0x400000), 0);
    chrome_fixture::install(w);
    w.io[0] = 2;
    w.io[1] = 0xFA;
    w.io[7] = 0x10;
    w.ram[0xFE00 + 14 * 32 + 5 * 2] = 0xFF;
    w.ram[0xFE00 + 14 * 32 + 5 * 2 + 1] = 0x0F;
    w.ram[0xFE00 + 14 * 32 + 1 * 2] = 0;
    w.ram[0xFE00 + 14 * 32 + 1 * 2 + 1] = 0;
    w.cpu.s.halted = true;
    w.cpu.s.flags &= uint16_t(~emu::IF);
    HostPicture picture;
    picture.install(w, {});
    w.run_frame();
    w.run_frame();
    w.frame_count = 64; // the hints' pulse at its peak (at its trough they are not drawn)
    Machine m(w);
    hooks::emit(hooks::Event::TitleMenu, m, {2});
    Fidelity fidelity;
    int requests = 0;
    picture.set_title_step_hint([&] {
        ++requests;
        return TitleMenuLettering::StepHint{fidelity.step_mode(), bound(WscButton::FidelityDown, false, 1),
                                            bound(WscButton::FidelityUp, false, 1)};
    });
    const auto ram = w.ram;
    const auto frame = w.frame_count;
    const auto before = picture.picture(w, Wsc::ColorMode::Raw);
    REQUIRE(requests == 1);
    map.assign(WscButton::FidelityDown, 0, {Binding::Kind::Key, SDL_SCANCODE_K});
    CHECK(bool(picture.picture(w, Wsc::ColorMode::Raw) != before));
    fidelity.step(-1, 0);
    const auto middle = picture.picture(w, Wsc::ColorMode::Raw);
    fidelity.settings(FidelityMode::Wsc, FidelityMode::Wsc, 0);
    CHECK(bool(picture.picture(w, Wsc::ColorMode::Raw) != middle));
    CHECK(bool(picture.picture(w, Wsc::ColorMode::Raw) == w.framebuffer_rgb(Wsc::ColorMode::Raw)));
    CHECK(w.ram == ram);
    CHECK(w.frame_count == frame);
}

TEST_CASE("Enhanced title logo: an opaque 8x canvas on the art's own pixels, their glow and the re-lettered "
          "kanji line") {
    using sawgod::game::TitleLogo;
    // A synthetic title: one red stroke and a white bar on the kanji line, over black.
    std::vector<uint32_t> native(224 * 144, 0);
    for (int y = 10; y < 40; ++y)
        for (int x = 60; x < 70; ++x)
            native[size_t(y * 224 + x)] = 0xDD0000;
    for (int x = 60; x < 120; ++x)
        native[size_t(70 * 224 + x)] = 0xFFFFFF;
    const auto logo = TitleLogo::build(native);
    REQUIRE(logo.width == TitleLogo::kWidth * TitleLogo::kScale);
    REQUIRE(logo.height == TitleLogo::kHeight * TitleLogo::kScale);
    REQUIRE(!TitleLogo::kanji().pixels.empty());
    CHECK(TitleLogo::kanji().width == logo.width);
    const auto at = [&](int x, int y) { return logo.pixels[size_t(y * logo.width + x)]; };
    // inside the stroke: its own red, opaque; outside: black
    const int sx = (65 - TitleLogo::kX) * TitleLogo::kScale, sy = 25 * TitleLogo::kScale;
    CHECK((at(sx, sy) >> 24) == 255);
    CHECK((at(sx, sy) >> 16 & 255) >= 200);
    CHECK((at(sx, sy) & 0xFFFF) < 0x1000);
    CHECK((at(4, 4) & 0xFFFFFF) == 0);
    // the stroke's edge is the art's pixel edge exactly; outside it, a smooth glow in the stroke's color
    const int edge = (60 - TitleLogo::kX) * TitleLogo::kScale;
    CHECK((at(edge, sy) >> 16 & 255) >= 200);
    CHECK((at(edge, sy) >> 8 & 255) < 10);
    CHECK(at(edge, sy) == at(edge + TitleLogo::kScale - 1, sy));
    const int near_red = int(at(edge - 1, sy) >> 16 & 255),
              far_red = int(at(edge - TitleLogo::kScale / 2, sy) >> 16 & 255);
    CHECK(near_red > far_red);
    CHECK(far_red > 8);
    CHECK((at(edge - 1, sy) >> 8 & 255) < 10);
    CHECK((at(edge - 3 * TitleLogo::kScale, sy) >> 16 & 255) < 8);
    // the last native rows fade out into the native picture
    CHECK((at(4, logo.height - 1) >> 24) < 40);
    // a stroke closed around a gap: no glow inside it, and the glow still outside
    std::vector<uint32_t> ring(224 * 144, 0);
    for (int y = 10; y < 40; ++y)
        for (int x = 60; x < 90; ++x)
            ring[size_t(y * 224 + x)] = x >= 68 && x < 82 && y >= 18 && y < 32 ? 0 : 0xDD0000;
    const auto closed = TitleLogo::build(ring);
    const auto closed_at = [&](int x, int y) {
        return closed
            .pixels[size_t((y * TitleLogo::kScale) * closed.width + (x - TitleLogo::kX) * TitleLogo::kScale)];
    };
    CHECK((closed_at(75, 25) & 0xFFFFFF) == 0);
    CHECK((closed_at(69, 25) & 0xFFFFFF) == 0);
    CHECK((closed_at(90, 25) >> 16 & 255) > 8);
}

TEST_CASE("the first battle wipe after a load releases a redundant dialogue-close handoff") {
    for (bool second : {false, true}) {
        Pictures p(second);
        const auto before = p.pixels();
        p.emit(hooks::Event::Dialogue, 0); // clear_field_overlay, with no dialogue ever opened
        p.step();
        CHECK(p.pixels() == before);
        CHECK(p.host.holding());
        p.w.io[0xB2] |= 0x10;
        p.w.ram[0x90] = 0x10;
        p.w.ram[0x91] = 0x13;
        p.w.ram[0x92] = 0;
        p.w.ram[0x93] = 0x50;
        for (int frame = 0; frame < 40; ++frame) {
            p.w.io[0x10] = uint8_t(frame * 4);
            p.step();
            CHECK(p.pixels() == p.w.framebuffer_rgb(Wsc::ColorMode::Raw, second));
            CHECK_FALSE(p.host.holding());
            CHECK(p.w.presentation_epoch == 0);
        }
        // BattleStart still holds its last complete wipe raster until the host battle is ready.
        const auto wipe = p.pixels();
        p.w.io[0xB2] &= ~0x10;
        p.emit(hooks::Event::BattleStart, 0);
        p.step();
        CHECK(p.pixels() == wipe);
        CHECK(p.host.holding());
    }
}

TEST_CASE("Native full-screen owners cover a loaded field independently of menu ownership") {
    for (bool english : {false, true}) {
        Pictures p(false);
        if (english) {
            std::string error;
            REQUIRE(Session::of(p.w).text().load("key,text\n", Charset::system(), &error, nullptr, nullptr,
                                                 true));
        }
        CHECK_FALSE(p.host.native_full_screen(p.w));
        p.emit(hooks::Event::IntroPresentation, int(IntroStage::WorldCard), 1);
        CHECK(p.host.native_full_screen(p.w));
        p.emit(hooks::Event::IntroPresentation, int(IntroStage::WorldCardContent));
        CHECK(p.host.native_full_screen(p.w));
        p.emit(hooks::Event::IntroPresentation, int(IntroStage::Off));
        CHECK_FALSE(p.host.native_full_screen(p.w));
        p.emit(hooks::Event::PresentationField, 1);
        CHECK_FALSE(p.host.native_full_screen(p.w));
    }
}

TEST_CASE("retained menu endpoints keep their own pixels and metadata after an unseen update") {
    Pictures p(true);
    p.w.second_visible = false;
    p.transition(hooks::Event::SaveMenu, 1, true);
    p.w.ram[0xD842] = 2; // advance the Load cursor while only WSC is shown
    p.step();
    const auto expected = p.save.picture(p.w, Wsc::ColorMode::Raw, true);
    Wsc::UiText text;
    Wsc::UiFill fill;
    p.w.ui_text = &text;
    p.w.ui_fill = &fill;
    const auto color = p.host.picture(p.w, Wsc::ColorMode::Raw, false);
    REQUIRE(color != expected);
    const auto color_fill = fill;
    const auto color_text = text;
    const auto ram = p.w.ram;
    const auto io = p.w.io;
    p.emit(hooks::Event::SaveMenu, 0, -1);
    text.clear();
    fill.clear();
    CHECK(p.host.picture(p.w, Wsc::ColorMode::Raw, true) == expected);
    CHECK(p.host.holding());
    CHECK(fill.windows.empty()); // GB has plain fills and bitmap text
    CHECK(p.host.picture(p.w, Wsc::ColorMode::Raw, false) == color);
    CHECK(fill.windows == color_fill.windows);
    CHECK(text.pixels == color_text.pixels);
    CHECK_FALSE(fill.windows.empty());
    CHECK(p.w.ram == ram);
    CHECK(p.w.io == io);
}

TEST_CASE("restart retires the old host owner before native input can arm another hold") {
    for (bool gb : {false, true}) {
        Pictures p(gb);
        p.transition(hooks::Event::BattleMenu, 1, true);
        p.emit(hooks::Event::BattleRoundStart, 0);
        (void)p.pixels();
        REQUIRE(p.host.holding());
        Session::of(p.w).new_generation(); // scheduler entry to main after a wipe
        // Native initialization can poll before the app's next BattleUi::frame.
        p.emit(hooks::Event::PresentationPicture, 1);
        p.emit(hooks::Event::PresentationPicture, 0);
        p.battle.frame(p.w);
        for (int frame = 0; frame < 350; ++frame) {
            p.step();
            CHECK(p.pixels() == p.w.framebuffer_rgb(Wsc::ColorMode::Raw, gb));
            CHECK_FALSE(p.host.holding());
            CHECK(p.host.shown() == 0);
        }
    }
}

TEST_CASE("host-owned pictures follow native palette fades through black waits and fade in") {
    for (bool gb : {false, true}) {
        for (auto mode : {Wsc::ColorMode::Raw, Wsc::ColorMode::Lcd}) {
            INFO("GB = ", gb, "; LCD = ", mode == Wsc::ColorMode::Lcd);
            Pictures p(gb);
            p.transition(hooks::Event::ServiceMenu, 7, true); // Inn confirmation
            const auto pixels = [&] { return p.host.picture(p.w, mode, gb); };
            const auto full = pixels();
            CHECK(full == p.service.picture(p.w, mode, gb));
            CHECK(bool(pixels() == full)); // no fade: byte-identical, even on repeated draws
            const auto palettes = p.w.ram;
            const auto set_level = [&](int level) {
                for (size_t at = 0xFE00; at < 0x10000; at += 2) {
                    const auto channel = [&](int value) { return std::max(0, value - (15 - level)); };
                    p.w.ram[at] = uint8_t(channel(palettes[at] & 15) | channel(palettes[at] >> 4) << 4);
                    p.w.ram[at + 1] = uint8_t(channel(palettes[at + 1] & 15));
                }
                if (gb)
                    p.w.shadow_release(0, 0x10000);
            };
            const auto dimmed = [&](const std::vector<uint32_t> &base, int level) {
                auto expected = base;
                for (auto &color : expected) {
                    uint32_t result = 0;
                    for (int shift : {0, 8, 16})
                        result |= ((color >> shift & 255) * unsigned(level) / 15) << shift;
                    color = result;
                }
                return expected;
            };
            p.emit(hooks::Event::PresentationPicture, 0); // native rest invalidates, but owns no new picture
            REQUIRE(pixels() == full);
            REQUIRE(p.host.holding());
            p.emit(hooks::Event::PresentationPicture, 3);
            p.emit(hooks::Event::PresentationPicture, 3); // nested scopes do not reset the reference
            auto previous = full;
            Wsc::UiFill fills;
            Wsc::UiText text;
            p.w.ui_fill = &fills;
            p.w.ui_text = &text;
            for (int step = 1; step <= 16; ++step) {
                const int level = std::max(0, 15 - step);
                set_level(level);
                p.emit(hooks::Event::PresentationPicture, 1); // fade input polls cannot commit a layout
                p.step();
                const auto ram = p.w.ram;
                const auto io = p.w.io;
                const auto sram = p.w.sram;
                const auto current = pixels();
                CHECK(bool(current == dimmed(full, level)));
                CHECK(bool(pixels() == current)); // no accumulated shade on retained pixels or metadata
                for (size_t at = 0; at < current.size(); ++at)
                    for (int shift : {0, 8, 16})
                        if ((current[at] >> shift & 255) > (previous[at] >> shift & 255)) {
                            FAIL_CHECK("a host pixel brightened during the native darkening fade");
                            break;
                        }
                CHECK(p.w.ram == ram);
                CHECK(p.w.io == io);
                CHECK(p.w.sram == sram);
                previous = current;
                if (step == 8 || step == 16)
                    p.emit(hooks::Event::PresentationPicture, 4);
            }
            for (int wait = 0; wait < 350; ++wait) {
                p.step();
                const auto black = pixels();
                CHECK(std::all_of(black.begin(), black.end(), [](uint32_t c) { return c == 0; }));
                CHECK_FALSE(p.host.holding()); // a palette-controlled black wait is not a stalled handoff
                CHECK(std::all_of(fills.windows.begin(), fills.windows.end(),
                                  [](const auto &window) { return window.light == 0 && window.dark == 0; }));
                CHECK(std::all_of(text.pixels.begin(), text.pixels.end(), [](const auto &pixel) {
                    return pixel.color == 0 && pixel.beneath == 0 && pixel.shadow_color == 0;
                }));
            }
            for (int level = 1; level <= 15; ++level) {
                set_level(level);
                p.step();
                CHECK(pixels() ==
                      dimmed(full, level)); // brightens from native state, without a new fade event
            }
            CHECK(bool(pixels() == full));
            p.emit(hooks::Event::PresentationPicture, 1);
            for (int frame = 0; frame < 4; ++frame)
                p.step();
            CHECK(bool(pixels() == p.service.picture(p.w, mode, gb)));
            CHECK_FALSE(p.host.holding());
            // A fade with a committed active owner also shades the direct screen, not the native framebuffer.
            p.emit(hooks::Event::PresentationPicture, 3);
            set_level(8);
            p.step();
            const auto direct = p.service.picture(p.w, mode, gb);
            CHECK(bool(pixels() == dimmed(direct, 8)));
            p.emit(hooks::Event::PresentationPicture, 4);
            CHECK(bool(pixels() == dimmed(direct, 8)));
            set_level(0);
            CHECK(bool(pixels() == dimmed(direct, 0)));
            p.emit(hooks::Event::ServiceMenu, 0, -1);
            p.emit(hooks::Event::InventoryMenu, 1);
            p.emit(hooks::Event::PresentationPicture, 1);
            for (int frame = 0; frame < 4; ++frame)
                p.step();
            CHECK(bool(pixels() == p.inventory.picture(p.w, mode, gb)));
            Session::of(p.w).new_generation();
            CHECK(bool(pixels() == p.w.framebuffer_rgb(mode, gb))); // no fade state leaks into a new run
        }
    }
}

TEST_CASE("one picture collection preserves both endpoints and their metadata without composing twice") {
    for (const auto event : {hooks::Event::Dialogue, hooks::Event::BattleMenu, hooks::Event::ServiceMenu,
                             hooks::Event::PartyMenu}) {
        Pictures p(true);
        if (event == hooks::Event::Dialogue)
            p.w.io[0xB2] |= 0x10; // the native dialogue window's line split is on
        p.transition(event, 1, true);
        for (const auto mode : {Wsc::ColorMode::Raw, Wsc::ColorMode::Lcd}) {
            const auto memory = p.w.ram;
            const auto ports = p.w.io;
            Wsc::UiText text;
            Wsc::UiFill fill;
            std::array<uint8_t, Wsc::kWidth * Wsc::kHeight> coverage{};
            p.w.ui_text = &text;
            p.w.ui_fill = &fill;
            p.w.ui_coverage = &coverage;
            int ordinary_draws = 0, collected_draws = 0;
            Chrome::observe = [&](const Chrome::Draw &) { ++ordinary_draws; };
            const auto expected_wsc = p.host.picture(p.w, mode);
            const auto expected_text = text;
            const auto expected_fill = fill;
            const auto expected_coverage = coverage;
            p.w.ui_text = nullptr;
            p.w.ui_fill = nullptr;
            p.w.ui_coverage = nullptr;
            const auto expected_gb = p.host.picture(p.w, mode, true);
            Chrome::observe = [&](const Chrome::Draw &) { ++collected_draws; };
            p.w.ui_text = &text;
            p.w.ui_fill = &fill;
            p.w.ui_coverage = &coverage;
            auto collection = p.host.collect();
            CHECK(collection.picture(p.w, mode) == expected_wsc);
            CHECK(text.pixels == expected_text.pixels);
            CHECK(text.bars.pixels == expected_text.bars.pixels);
            CHECK(text.corners == expected_text.corners);
            CHECK(fill.color == expected_fill.color);
            CHECK(fill.beneath == expected_fill.beneath);
            CHECK(coverage == expected_coverage);
            p.w.ui_text = nullptr;
            p.w.ui_fill = nullptr;
            p.w.ui_coverage = nullptr;
            CHECK(collection.picture(p.w, mode, true) == expected_gb);
            Chrome::observe = {};
            CHECK(ordinary_draws > 0);
            CHECK(collected_draws * 2 == ordinary_draws);
            CHECK(p.w.ram == memory);
            CHECK(p.w.io == ports);
            // Rebinding a new collection refreshes immediately on the same machine frame.
            auto fresh = p.host.collect();
            CHECK(fresh.picture(p.w, mode, true) == p.host.picture(p.w, mode, true));
        }
    }
}

TEST_CASE("held battle pictures publish palette changes without submitting partial layouts") {
    Pictures p(false);
    p.w.capture_layers(true);
    p.w.io[0x60] = 0xE0;
    p.w.io[0] = 3;
    p.w.io[7] = 0x32;
    for (int at = 0x1800; at < 0x2000; at += 2) {
        p.w.ram[size_t(at)] = 1;
        p.w.ram[size_t(at + 1)] = 0x29; // upper-bank enemy tile 0x301, palette 4
    }
    std::fill_n(p.w.ram.begin() + 0x8000 + 0x101 * 32, 32, 0x11);
    p.w.ram[0xFE82] = 0x34;
    p.w.ram[0xFE83] = 2;
    p.step();
    p.step();
    p.transition(hooks::Event::BattleMenu, 1, true);
    const auto before = p.pixels();
    REQUIRE(before[20 * 224 + 20] == 0x223344);
    p.emit(hooks::Event::BattleRoundStart, 0);
    p.emit(hooks::Event::PresentationPicture, 0);
    for (bool flash : {true, false}) {
        p.w.ram[0xFE82] = flash ? 0xFF : 0x34;
        p.w.ram[0xFE83] = flash ? 0x0F : 2;
        p.step();
        p.step();
        const auto memory = p.w.snapshot();
        const auto picture = p.pixels();
        CHECK(picture[20 * 224 + 20] == (flash ? 0xFFFFFF : 0x223344));
        for (size_t at = 80 * 224; at < picture.size(); ++at)
            CHECK(picture[at] == before[at]);
        CHECK(p.w.ram == memory.ram);
        CHECK(p.w.io == memory.io);
        CHECK(p.w.sram == memory.sram);
    }
    p.w.ram[0x1800] = 2; // geometry changed, so a palette DMA cannot publish it
    p.w.ram[0xFE82] = 0xFF;
    p.w.ram[0xFE83] = 0x0F;
    p.step();
    p.step();
    CHECK(p.pixels() == before);
    CHECK(p.host.holding());
    p.w.ram[0x1800] = 1;
    // The native escape result starts an outgoing fade before BattleEnd is emitted.
    p.w.ram[0xD70E] = 4;
    p.step();
    p.step();
    CHECK(p.pixels() == before);
    // Rewards retain a cached battlefield, even if a native palette happens to share its colors.
    p.w.ram[0xD70E] = 0;
    p.emit(hooks::Event::BattleRewards, 0);
    p.step();
    p.step();
    CHECK(p.pixels() == before);
}

TEST_CASE("native defeat rasters advance held battle geometry and retain host chrome") {
    for (bool second : {false, true}) {
        Pictures p(second);
        p.w.capture_layers(true, false); // production captures WSC provenance only
        p.w.io[0x60] = 0xE0;
        p.w.io[0] = 3;
        p.w.io[7] = 0x32;
        for (int at = 0x1800; at < 0x2000; at += 2) {
            p.w.ram[size_t(at)] = 1;
            p.w.ram[size_t(at + 1)] = 0x29;
        }
        std::fill_n(p.w.ram.begin() + 0x8000 + 0x101 * 32, 32, 0x11);
        p.w.ram[0xFE82] = 0x34;
        p.w.ram[0xFE83] = 2;
        p.step();
        p.step();
        p.transition(hooks::Event::BattleMenu, 1, true);
        const auto before = p.pixels();
        p.emit(hooks::Event::BattleRoundStart, 0);
        p.emit(hooks::Event::PresentationPicture, 0);
        p.emit(hooks::Event::BattleDefeatEffect, 1);
        CHECK(p.host.native_battle_effect());
        p.w.scene_hidden_palettes |= 1 << 4; // ordinary raster art omitted by the clean-scene pass
        for (uint8_t tile : {2, 1, 2}) {
            p.w.ram[0x1800] = tile;
            if (second)
                p.w.shadow_release(0x1800, 1);
            p.step();
            p.step();
            const auto state = p.w.snapshot();
            const auto picture = p.pixels();
            const auto native = p.w.framebuffer_rgb(Wsc::ColorMode::Raw, second);
            if (tile == 2)
                CHECK(native[0] != before[0]);
            CHECK(picture[0] == native[0]);
            CHECK_FALSE(p.host.holding());
            for (size_t at = 80 * 224; at < picture.size(); ++at)
                CHECK(picture[at] == before[at]);
            CHECK(p.w.ram == state.ram);
            CHECK(p.w.io == state.io);
            CHECK(p.w.sram == state.sram);
        }
        const auto last = p.pixels();
        p.emit(hooks::Event::BattleDefeatEffect, 3);
        CHECK(p.host.native_battle_effect());
        p.emit(hooks::Event::BattleDefeatEffect, 2);
        CHECK(p.host.native_battle_effect()); // clearing planes does not end the native restoration fade
        p.emit(hooks::Event::BattleDefeatEffect, 0);
        CHECK_FALSE(p.host.native_battle_effect());
        p.w.ram[0x1800] = 3;
        if (second)
            p.w.shadow_release(0x1800, 1);
        p.step();
        p.step();
        CHECK(p.pixels() == last);
        CHECK(p.host.holding());
        p.emit(hooks::Event::PresentationPicture, 5);
        p.step();
        p.step();
        const auto submitted = p.pixels();
        CHECK_FALSE(p.host.holding());
        CHECK(submitted == p.battle.picture(p.w, Wsc::ColorMode::Raw, second));
    }
}

TEST_CASE("ending notice ownership is independent of the lettering language") {
    Pictures p(false);
    REQUIRE_FALSE(Session::of(p.w).text().proportional());
    p.emit(hooks::Event::EndingNotice, 1);
    CHECK(p.host.native_full_screen(p.w));
    CHECK(p.pixels() == p.w.framebuffer_rgb(Wsc::ColorMode::Raw));
    CHECK_FALSE(p.host.holding());
    std::string error;
    REQUIRE(Session::of(p.w).text().load("key,text\nui_ending_notice,Save this game.\n", Charset::system(),
                                         &error, nullptr, nullptr, true));
    CHECK(p.host.native_full_screen(p.w));
    p.pixels();
    CHECK_FALSE(p.host.holding());
    p.emit(hooks::Event::SaveMenu, 1);
    CHECK_FALSE(p.host.native_full_screen(p.w));
}
