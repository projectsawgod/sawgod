#include "emu/wsc.h"
#include "game/bank35_field.h"
#include "game/bank35_world.h"
#include "game/maps.h"
#include "game/mode7.h"
#include "game/session.h"
#include <cstdlib>
#include <doctest/doctest.h>
#include <fstream>
#include <iterator>

using namespace sawgod;
namespace {
std::vector<uint8_t> supplied_rom() {
    const char *path = std::getenv("SAWGOD_WSC_ROM");
    if (!path)
        return {};
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}
void boot(emu::Wsc &w) {
    game::install(w);
    w.render_lcd = false;
    for (int i = 0; i < 2900; ++i) {
        const bool a = (i >= 1200 && i < 1206) || (i >= 1500 && i < 1506) || (i >= 2700 && i < 2706) ||
                       (i >= 2760 && i < 2766);
        const bool start = (i >= 600 && i < 606) || (i >= 2820 && i < 2826);
        w.set_keys(a ? emu::KEY_A : start ? emu::KEY_START : 0);
        w.run_frame();
        w.apu.take();
    }
    w.ram[0xD0AE] = 255; // all four world-introduction vignettes have already been seen
}
void event(emu::Wsc &w, int kind, int argument) {
    w.ram[0xC825] = 1;
    w.ram[0xC826] = uint8_t(kind);
    w.ram[0xC827] = uint8_t(argument);
}
void reach(emu::Wsc &w, int map, bool acknowledge = false) {
    for (int i = 0; i < 1200; ++i) {
        w.set_keys(acknowledge && i % 40 < 6 ? emu::KEY_A : 0);
        w.run_frame();
        w.apu.take();
        if (w.ram[0xD0DC] == map && !w.ram[0xD52C] && !w.ram[0xD519] && game::recognized_field(w))
            return;
    }
    FAIL("destination did not become the recognized field");
}
} // namespace
TEST_CASE(
    "Enhanced world eligibility follows loaded field state after castle eviction script warp and Door") {
    const auto rom = supplied_rom();
    if (rom.empty()) {
        MESSAGE("SAWGOD_WSC_ROM unavailable");
        return;
    }
    REQUIRE(rom.size() == 0x400000);
    emu::Wsc w(rom, 0x8000);
    boot(w);
    event(w, 5, 0xFA);
    reach(w, 91);
    // Native NPC dialogue 97 (dlg_354F59) contains opcode 13, event 05/43: castle eviction.
    event(w, 1, 0x97);
    reach(w, 88, true);
    w.render_lcd = true;
    w.capture_layers(true);
    w.run_frame();
    for (int i = 0; i < 4; ++i) {
        w.run_frame();
        w.apu.take();
    }
    game::SceneObserver late;
    late.install(w); // intentionally missed EVERY entry/field hook
    auto scene = late.describe(w, false, false, false);
    CHECK(scene.kind == game::PresentationScene::Kind::Field);
    CHECK(scene.perspective_allowed);
    CHECK(game::mode7_allowed(scene.map, FidelityMode::Enhanced, scene.perspective_allowed, false));
    // Another native script/event warp: the first world's Tower exit, destination 78.
    event(w, 5, 0xFA);
    reach(w, 91);
    event(w, 5, 0x78);
    reach(w, 88);
    late.install(w);
    for (int i = 0; i < 4; ++i) {
        w.run_frame();
        w.apu.take();
    }
    CHECK(late.describe(w, false, false, false).perspective_allowed);
    // Door really selects tower floors, not world maps. Run its native charge/warp routine,
    // restore the field as the menu does, then walk through floor 1's real exit below.
    event(w, 5, 0xFA);
    reach(w, 91);
    w.ram[0xD07C] = 11;
    w.ram[0xD07D] = 3;
    w.ram[0xD84B] = 1;
    game::reset_natives(w);
    auto &s = w.cpu.s;
    s.seg[emu::CS] = 0x5000;
    s.ip = 0xBDEF;
    s.r[emu::SI] = 0xD07C;
    game::Machine m(w);
    s.r[emu::SP] = uint16_t(s.r[emu::SP] - 4);
    m.wr16(0, s.r[emu::SP], 0x0100);
    m.wr16(0, uint16_t(s.r[emu::SP] + 2), 0x5000);
    bool returned = false;
    w.set_native(0x350100, [&](emu::Wsc &hw) {
        returned = true;
        hw.cpu.s.halted = true;
        return 1;
    });
    for (int i = 0; !returned && i < 400; ++i) {
        w.run_frame();
        w.apu.take();
    }
    REQUIRE(returned);
    CHECK(w.ram[0xD07D] == 2);
    CHECK(w.ram[0xD0DC] == 1);
    // Start the native field loop with the Door's destination (same setup as menu return).
    game::reset_natives(w);
    s.halted = false;
    s.seg[emu::CS] = 0x5000;
    s.ip = 0x0564;
    reach(w, 1);
    for (int i = 0; i < 160; ++i) {
        w.set_keys(emu::KEY_X3);
        w.run_frame();
        w.apu.take();
    }
    reach(w, 88);
    late.install(w);
    for (int i = 0; i < 4; ++i) {
        w.run_frame();
        w.apu.take();
    }
    CHECK(late.describe(w, false, false, false).perspective_allowed);
    game::Machine observation(w);
    game::hooks::emit(game::hooks::Event::BattleStart, observation);
    CHECK(late.describe(w, false, false, false)
              .perspective_allowed);       // a missed battle-end hook cannot veto field state
    game::Session::of(w).new_generation(); // restored/load state can precede all observation hooks
    CHECK(late.describe(w, false, false, false).perspective_allowed);
    // A destination ID alone cannot identify an unloaded field, and scripts retain overhead.
    w.ram[0xD0DC] = 102;
    CHECK_FALSE(game::recognized_field(w));
    w.ram[0xD0DC] = 88;
    w.ram[0xD52C] = 1;
    late.install(w);
    // A running script keeps the field's camera (the field owns its picture through every event).
    CHECK(late.describe(w, false, false, false).perspective_allowed);
}
TEST_CASE(
    "Enhanced distant NPCs match native OAM art position clipping visibility and leave all state intact") {
    const auto rom = supplied_rom();
    if (rom.empty()) {
        MESSAGE("SAWGOD_WSC_ROM unavailable");
        return;
    }
    // Match the cartridge's 32 KiB SRAM: the renderer samples the complete loaded
    // field bank, so an 8 KiB mirrored fixture cannot exercise its collection path.
    emu::Wsc a(rom, 0x8000), b(rom, 0x8000);
    boot(a);
    boot(b);
    game::Mode7World extended, native_only;
    game::LightTags materials;
    materials.seed(a, &game::Session::of(a).tables());
    game::LandmarkTags landmarks;
    int visible_checks = 0, distant_checks = 0;
    for (int map_event : {0xFA, 0x78, 0xE7}) {
        event(a, 5, map_event);
        event(b, 5, map_event);
        for (int frame = 0; frame < 2200; ++frame) {
            const uint16_t keys = frame > 250 ? (frame % 400 < 200 ? emu::KEY_X2 : emu::KEY_X4) : 0;
            a.set_keys(keys);
            b.set_keys(keys);
            a.run_frame();
            b.run_frame();
            a.apu.take();
            b.apu.take();
            if (game::recognized_field(a)) {
                // Exercise the complete camera collection path on both machines, not just
                // the NPC decoder. Only the presentation-only extension differs.
                game::PresentationScene field;
                field.kind = game::PresentationScene::Kind::Field;
                field.map = a.ram[0xD0DC];
                REQUIRE(extended.update(a, field, materials, landmarks, true));
                REQUIRE(native_only.update(b, field, materials, landmarks, false));
                const auto before = a.snapshot();
                const auto cards = game::field_npc_sprites(a);
                CHECK(a.snapshot().ram == before.ram);
                CHECK(a.snapshot().sram == before.sram);
                int expected = 0;
                for (int slot = 0; slot < 16; ++slot) {
                    const int at = 0xD5AE + 20 * slot, flags = a.ram[size_t(at + 17)];
                    if (a.ram[size_t(at)] == 255 || flags & 0x08 || ((flags & 0x20) && (a.ram[0xC80C] & 8)))
                        continue;
                    ++expected;
                }
                CHECK(int(cards.size()) == expected);
                const int cx = int16_t(a.ram[0xD59D] | a.ram[0xD59E] << 8);
                const int cy = int16_t(a.ram[0xD59F] | a.ram[0xD5A0] << 8);
                for (const auto &c : cards) {
                    const int x = int(c.x) - 8 - cx, y = int(c.y) - 15 - cy;
                    if (x < -16 || x >= 240 || y < -16 || y >= 160) {
                        ++distant_checks;
                        continue;
                    }
                    // OAM is built before the field wait; its complete 16x16 source must match.
                    for (int n = 0; n < a.io[6]; ++n) {
                        const int at = 0xC000 + n * 4;
                        const int tile = (a.ram[size_t(at)] | a.ram[size_t(at + 1)] << 8) & 511;
                        if (tile < 0x18 || (tile & 3) || a.ram[size_t(at + 3)] != uint8_t(x) ||
                            a.ram[size_t(at + 2)] != uint8_t(y))
                            continue;
                        ++visible_checks;
                        for (int q = 0; q < 4; ++q)
                            for (int py = 0; py < 8; ++py)
                                for (int px = 0; px < 8; ++px) {
                                    const int byte =
                                        a.ram[size_t(0x4000 + (tile + q) * 32 + py * 4 + px / 2)];
                                    const int index = px & 1 ? byte & 15 : byte >> 4;
                                    const auto color =
                                        (*c.pixels)[size_t((q / 2 * 8 + py) * 16 + q % 2 * 8 + px)];
                                    if (q < 2 || color) {
                                        const int palette = 8 + (a.ram[size_t(at + 1)] >> 1 & 7);
                                        const uint16_t c12 =
                                            a.presentation_palette_color(palette, index, false, true);
                                        const uint32_t native =
                                            index ? ((c.veil && q >= 2 ? 0x80000000u : 0xFF000000u) |
                                                     uint32_t((c12 >> 8 & 15) * 17 << 16 |
                                                              (c12 >> 4 & 15) * 17 << 8 | (c12 & 15) * 17))
                                                  : 0;
                                        if (color != native) {
                                            CAPTURE(frame);
                                            CAPTURE(a.ram[0xD5A4]);
                                            CAPTURE(a.ram[0xD5A3]);
                                            CAPTURE(a.ram[0xC80C]);
                                            CAPTURE(q);
                                            CAPTURE(tile);
                                            REQUIRE(color == native);
                                        }
                                    }
                                }
                    }
                }
            }
            REQUIRE(a.ram == b.ram);
            REQUIRE(a.sram == b.sram);
            REQUIRE(a.io == b.io);
            REQUIRE(std::equal(std::begin(a.cpu.s.r), std::end(a.cpu.s.r), std::begin(b.cpu.s.r)));
            REQUIRE(a.cpu.s.flags == b.cpu.s.flags);
            REQUIRE(a.cpu.cycles == b.cpu.cycles);
            REQUIRE(std::equal(std::begin(a.cpu.s.seg), std::end(a.cpu.s.seg), std::begin(b.cpu.s.seg)));
            REQUIRE(a.cpu.s.ip == b.cpu.s.ip);
            REQUIRE(a.cpu.s.halted == b.cpu.s.halted);
            REQUIRE(a.scanline == b.scanline);
            REQUIRE(a.line_cycle == b.line_cycle);
        }
    }
    CHECK(visible_checks > 100);
    CHECK(distant_checks > 100);
}
TEST_CASE("Enhanced world camera stays eligible between native field waits while walking") {
    const auto rom = supplied_rom();
    if (rom.empty()) {
        MESSAGE("SAWGOD_WSC_ROM unavailable");
        return;
    }
    emu::Wsc w(rom, 0x8000);
    boot(w);
    event(w, 5, 0x43);
    reach(w, 88);
    w.ram[0xC881] = 0; // encounter-free walking fixture, identical native field production
    w.render_lcd = true;
    w.capture_layers(true);
    game::SceneObserver observer;
    observer.install(w);
    for (int i = 0; i < 4; ++i) {
        w.run_frame();
        w.apu.take();
    }
    int between_waits = 0;
    for (int frame = 0; frame < 120; ++frame) {
        w.set_keys(emu::KEY_X4);
        w.run_frame();
        w.apu.take();
        REQUIRE(w.ram[0xD0DC] == 88);
        if (!w.ram[0xC813])
            ++between_waits;
        CAPTURE(frame);
        CAPTURE(w.ram[0xC813]);
        CAPTURE(w.ram[0xD52C]);
        CAPTURE(w.ram[0xD0D6]);
        const auto scene = observer.describe(w, false, false, false);
        CHECK(scene.kind == game::PresentationScene::Kind::Field);
        CHECK(scene.perspective_allowed);
    }
    CHECK(between_waits >= 6); // exercise the native boundary that previously lowered the camera
}
TEST_CASE("Distant NPC visibility honors native conditional object loading on every map") {
    const auto rom = supplied_rom();
    if (rom.empty()) {
        MESSAGE("SAWGOD_WSC_ROM unavailable");
        return;
    }
    emu::Wsc w(rom, 0x8000);
    boot(w);
    for (int level = 0; level < 16; ++level) {
        const int progress = level * 17;
        std::fill(w.ram.begin() + 0xD0A6, w.ram.begin() + 0xD0AF, uint8_t(progress));
        std::fill(w.ram.begin() + 0xD0BB, w.ram.begin() + 0xD0D3, uint8_t(progress));
        for (int id = 1; id < game::kMapCount; ++id) {
            w.ram[0xD0DC] = uint8_t(id);
            game::Machine m(w);
            m.cs() = 0x5000;
            m.ss() = 0;
            game::initialize_map_npcs_core(m);
            for (int phase : {0, 8}) {
                w.ram[0xC80C] = uint8_t(phase);
                const auto ram = w.ram;
                const auto sram = w.sram;
                const auto io = w.io;
                const auto cards = game::field_npc_sprites(w);
                int expected = 0;
                for (int slot = 0; slot < 16; ++slot) {
                    const int at = 0xD5AE + slot * 20, flags = w.ram[size_t(at + 17)];
                    if (w.ram[size_t(at)] != 255 && !(flags & 0x08) && !(flags & 0x20 && phase))
                        ++expected;
                }
                CAPTURE(id);
                CAPTURE(progress);
                CAPTURE(phase);
                CHECK(cards.size() == unsigned(expected));
                CHECK(w.ram == ram);
                CHECK(w.sram == sram);
                CHECK(w.io == io);
                // Event removal/hiding acts immediately, including objects outside the native draw window.
                for (int slot = 0; slot < 16; ++slot)
                    w.ram[size_t(0xD5AE + slot * 20 + 17)] |= 8;
                CHECK(game::field_npc_sprites(w).empty());
                w.ram = ram;
            }
        }
    }
}
TEST_CASE("Palette 15 field objects are camera actors while dialogue and cursor art remain UI") {
    emu::Wsc w(std::vector<uint8_t>(0x400000), 0);
    w.capture_layers(true);
    w.io[7] = 2;
    w.io[0x60] = 0xE0;
    w.ram[0xD0DC] = 91;
    w.ram[0xD519] = 1;
    w.cpu.s.halted = true;
    w.cpu.s.flags = 0;
    w.io[4] = 1;
    w.io[6] = 2;
    w.ram[0x200] = 0x18;
    w.ram[0x201] = 0x0E;
    w.ram[0x204] = 0x19;
    w.ram[0x205] = 0x0E;
    w.ram[0x207] = 8;
    w.terrain_cards[1] = {0, 0x0E18, false};
    w.publish_terrain_cards();
    w.run_frame();
    w.run_frame();
    // Map 91's descriptor as load_field_map copies it: the observer only describes a loaded field.
    for (int i = 1; i < 10; ++i)
        w.rom[size_t(0x35A830 + 91 * 18 + i)] = w.ram[size_t(0xD52D + i - 1)] = uint8_t(i);
    auto *layers = const_cast<emu::FrameLayers *>(w.frame_layers());
    REQUIRE(layers);
    layers->valid[0] = 1;
    layers->planes[emu::FrameLayers::Screen1][1] = {0x123, 1, 0, 0, 1, 255, emu::RasterPixel::Present};
    auto &pixel = layers->planes[emu::FrameLayers::SpriteFront][0];
    pixel.flags = emu::RasterPixel::Present;
    pixel.palette = 15;
    pixel.tile = 0x18;
    pixel.sprite = 0;
    game::SceneObserver observer;
    observer.install(w);
    game::Machine m(w);
    game::hooks::emit(game::hooks::Event::PresentationField, m, {1});
    CHECK(game::mode7_ui_mask(w, false, false)[0] == 0);
    CHECK((observer.describe(w, false, false, false).mask[0] & 0xFF0000) == 0);
    pixel.tile = 0x1D5;
    CHECK(game::mode7_ui_mask(w, false, false)[0] == 0xFF0000);
    CHECK((observer.describe(w, false, false, false).mask[0] & 0xFF0000) == 0xFF0000);
    pixel.tile = 0x80;
    pixel.palette = 14;
    pixel.sprite = 2; // a script-only sprite has no native NPC card record
    CHECK(game::mode7_ui_mask(w, false, false)[0] == 0xFF0000);
    pixel.tile = 0x18;
    CHECK(game::mode7_ui_mask(w, true, false)[0] == 0xFF0000);
}

#include "app/display_motion.h"
TEST_CASE("Smooth display clocks leave long multi-map native walk and audio bit-identical") {
    const auto rom = supplied_rom();
    if (rom.empty()) {
        MESSAGE("SAWGOD_WSC_ROM unavailable");
        return;
    }
    emu::Wsc a(rom, 0x8000), b(rom, 0x8000);
    boot(a);
    boot(b);
    std::vector<std::array<int32_t, 4>> sound_a, sound_b;
    std::vector<game::hooks::Subscription> subscriptions;
    for (auto *w : {&a, &b})
        for (auto event_id : {game::hooks::Event::SoundRequest, game::hooks::Event::MusicRequest,
                              game::hooks::Event::SoundReset})
            subscriptions.push_back(game::Session::of(*w).hooks().subscribe(
                event_id, [&, w, event_id](game::Machine &, const game::hooks::Args &args) {
                    (w == &a ? sound_a : sound_b).push_back({int32_t(event_id), args.a, args.b, args.c});
                }));
    game::LightTags tags;
    tags.seed(a, &game::Session::of(a).tables());
    game::LandmarkTags landmarks;
    game::Mode7World source;
    std::array<app::MotionHistory, 4> histories;
    constexpr std::array<int, 4> refresh{60, 120, 144, 240};
    std::array<uint64_t, 4> presents{};
    std::array<int, 4> interpolated{};
    constexpr double game_hz = 3072000.0 / (159 * 256);
    uint64_t frame = 0;
    int recognized = 0, boundaries = 0, half_pixel_records = 0;
    std::array<int, 4> directions{};
    app::MotionPoint last_camera{};
    int last_map = -1;
    for (int destination : {0xFA, 0x78, 0xE7}) {
        event(a, 5, destination);
        event(b, 5, destination);
        for (int local = 0; local < 2200; ++local, ++frame) {
            const auto keys = local < 250 ? 0
                                          : std::array<uint16_t, 4>{emu::KEY_X4, emu::KEY_X2, emu::KEY_X1,
                                                                    emu::KEY_X3}[unsigned(local / 180) % 4];
            a.ram[0xC881] = b.ram[0xC881] = 0;
            a.set_keys(uint16_t(keys));
            b.set_keys(uint16_t(keys));
            a.run_frame();
            b.run_frame();
            CHECK(a.apu.take() == b.apu.take());
            CHECK(a.apu.take_effects() == b.apu.take_effects());
            REQUIRE(sound_a == sound_b);
            app::MotionState state;
            state.frame = frame;
            state.native_step_known = true;
            state.walking_step = float(1 << std::min(2, int(a.ram[0xD517])));
            for (unsigned axis = 0; axis < 2; ++axis)
                state.native_step[axis] = float(int8_t(a.ram[0xD515 + axis])) * state.walking_step;
            state.generation = game::Session::of(a).generation();
            state.map = a.ram[0xD0DC];
            state.travel = a.ram[0xD0D6];
            state.eligible = game::recognized_field(a) && !a.ram[0xD519] && !a.ram[0xD52C];
            if (state.eligible) {
                ++recognized;
                if (!a.ram[0xC813])
                    ++boundaries;
                game::PresentationScene scene;
                scene.kind = game::PresentationScene::Kind::Field;
                scene.map = state.map;
                REQUIRE(source.update(a, scene, tags, landmarks));
                state.camera = source.scroll();
                if (last_map == state.map) {
                    const auto dx = state.camera[0] - last_camera[0], dy = state.camera[1] - last_camera[1];
                    if (dx < 0)
                        ++directions[0];
                    if (dx > 0)
                        ++directions[1];
                    if (dy < 0)
                        ++directions[2];
                    if (dy > 0)
                        ++directions[3];
                }
                last_map = state.map;
                last_camera = state.camera;
                for (const auto &sprite : source.sprites())
                    if (sprite.record)
                        state.actors[sprite.record] = {sprite.x, sprite.y};
                for (auto &[record, point] : state.actors)
                    if (record >= 0xD5AE && record < 0xD6EE && (a.ram[record] & 0x80))
                        for (unsigned axis = 0; axis < 2; ++axis) {
                            const auto at = record + 6 + axis * 2;
                            const float fraction = float(a.ram[at] & 1) * 0.5f;
                            const float displayed = point[axis];
                            point[axis] = game::field_subpixel_position(
                                displayed, uint16_t(a.ram[at] | a.ram[at + 1] << 8), axis ? 15.f : 8.f);
                            if (point[axis] != displayed)
                                ++half_pixel_records;
                            if (displayed ==
                                float((a.ram[at] | a.ram[at + 1] << 8) >> 1) + (axis ? 15.f : 8.f))
                                CHECK(point[axis] == displayed + fraction);
                            else
                                CHECK(point[axis] == displayed);
                        }
            }
            for (size_t clock = 0; clock < histories.size(); ++clock) {
                histories[clock].complete(state);
                while (double(presents[clock]) * game_hz / refresh[clock] < double(frame + 1)) {
                    const double phase = double(presents[clock]) * game_hz / refresh[clock] - double(frame);
                    const auto motion = histories[clock].sample(phase);
                    ++presents[clock];
                    if (!motion.snapped)
                        ++interpolated[clock];
                    source.set_motion(motion.camera, motion.actors);
                    for (size_t axis = 0; axis < 2; ++axis)
                        CHECK(std::abs(motion.camera[axis] - state.camera[axis]) <= 1.001f);
                    source.set_motion(state.camera, state.actors);
                }
            }
            REQUIRE(a.ram == b.ram);
            REQUIRE(a.sram == b.sram);
            REQUIRE(a.io == b.io);
            REQUIRE(std::equal(std::begin(a.cpu.s.r), std::end(a.cpu.s.r), std::begin(b.cpu.s.r)));
            REQUIRE(std::equal(std::begin(a.cpu.s.seg), std::end(a.cpu.s.seg), std::begin(b.cpu.s.seg)));
            REQUIRE(a.cpu.s.flags == b.cpu.s.flags);
            REQUIRE(a.cpu.s.ip == b.cpu.s.ip);
            REQUIRE(a.cpu.s.halted == b.cpu.s.halted);
            REQUIRE(a.cpu.cycles == b.cpu.cycles);
            REQUIRE(a.scanline == b.scanline);
            REQUIRE(a.line_cycle == b.line_cycle);
        }
    }
    for (const auto moves : directions)
        CHECK(moves > 32);
    CHECK(half_pixel_records > 20);
    CHECK(recognized > 3000);
    CHECK(boundaries > 50);
    for (size_t i = 0; i < 4; ++i) {
        CHECK(presents[i] > 5000);
        CHECK(interpolated[i] > 1000);
    }
}

TEST_CASE("Water on the horizon row starts below the sky") {
    const auto rom = supplied_rom();
    if (rom.empty()) {
        MESSAGE("SAWGOD_WSC_ROM unavailable");
        return;
    }
    emu::Wsc w(rom, 0x8000);
    game::install(w);
    game::LightTags tags;
    tags.seed(w, &game::Session::of(w).tables());
    // Tileset 13 (maps 80, 85, 86, 89): metatiles 14-18 are the row where the lake meets the sky. The sky
    // above row 9 is the lake's own blue, so only rows 9-15 may be water.
    for (int metatile = 14; metatile <= 18; ++metatile) {
        const auto *tag = tags.water(13, metatile, 0);
        REQUIRE(tag);
        for (int row = 0; row < 9; ++row)
            CHECK(tag->mask[size_t(row)] == 0);
        if (metatile != 16) { // the Tower's island: sky and rock only
            CHECK(tag->enabled);
            CHECK(tag->mask[12] != 0);
        } else
            CHECK_FALSE(tag->enabled);
    }
    // The open lake below keeps its whole tile.
    const auto *lake = tags.water(13, 66, 0);
    REQUIRE(lake);
    CHECK(lake->enabled);
    CHECK(lake->mask[0] != 0);
}
