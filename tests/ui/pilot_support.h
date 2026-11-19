#pragma once
#include "game/bank34_misc.h"
#include "game/battle_ui.h"
#include "game/bestiary_ui.h"
#include "game/creation_ui.h"
#include "game/dialogue_ui.h"
#include "game/gfx_swap.h"
#include "game/host_picture.h"
#include "game/inventory_ui.h"
#include "game/item_dialog_ui.h"
#include "game/party_ui.h"
#include "game/player_names.h"
#include "game/save_ui.h"
#include "game/service_ui.h"
#include "game/session.h"
#include "game/text_render.h"
#include "game/ui_chrome.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace ui_pilot {
namespace game = sawgod::game;
namespace emu = sawgod::emu;
inline void require(bool ok, const std::string &why) {
    if (!ok)
        throw std::runtime_error(why);
}
inline std::string read(const std::filesystem::path &p) {
    std::ifstream f(p, std::ios::binary);
    require(bool(f), "Cannot read " + p.string());
    return {std::istreambuf_iterator<char>(f), {}};
}
inline std::string quote(std::string_view s) {
    std::string r = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\')
            r += '\\';
        if (c == '\n')
            r += "\\n";
        else if (c < 32)
            r += ' ';
        else
            r += char(c);
    }
    return r + '"';
}
class StateBuilder {
    emu::Wsc &w;
    void word(int at, unsigned value) {
        w.ram[size_t(at)] = uint8_t(value);
        w.ram[size_t(at + 1)] = uint8_t(value >> 8);
    }

  public:
    enum Type { Human = 150, HumanFemale = 156, Mutant = 180, Monster = 0 };
    explicit StateBuilder(emu::Wsc &machine) : w(machine) {}
    // bank34_misc.cpp::append_progress_history_core: newest entry follows the ring
    // index; the companion 100-byte history is protected by the Bestiary pilot too.
    StateBuilder &monster_history(int member, int count, int generation = 1234, int ring = 95) {
        require(member >= 0 && member < 4 && count >= 0 && count <= 100 && generation >= 0 &&
                    generation <= 65535 && ring >= 0 && ring < 100,
                "invalid monster history");
        game::Machine m(w);
        const int at = m.rd16(0x4000, uint16_t(0xB92D + 2 * member));
        require(at + 204 <= 65536, "history exceeds RAM");
        word(at, unsigned(generation));
        word(at + 2, unsigned(ring));
        std::fill(w.ram.begin() + at + 4, w.ram.begin() + at + 204, 255);
        for (int i = 0; i < count; ++i) {
            const int index = (ring + 1 + i) % 100;
            w.ram[size_t(at + 4 + index)] = uint8_t(i % 150);
            w.ram[size_t(at + 104 + index)] = uint8_t((i + 17) % 150);
        }
        return *this;
    }
    // The catalog tests event bits 0..149 (3452F5 / creation_type_unlocked_core),
    // despite the separate D0BB field being called bestiary_flags in ram.toml.
    StateBuilder &bestiary_flags(int population) {
        require(population >= 0 && population <= 150, "invalid bestiary flag population");
        for (int bit = 0; bit < 150; ++bit)
            monster_seen(bit, bit < population);
        return *this;
    }
    StateBuilder &monster_seen(int type, bool value = true) {
        require(type >= 0 && type < 150, "invalid discovery type");
        game::Machine m(w);
        // The ROM's bit table is MSB first, unlike StateBuilder::flag's raw LSB bit numbering.
        const auto mask = m.rd8(0x4000, uint16_t(0x5F51 + (type & 7)));
        auto &byte = w.ram[size_t(0xD0EC + type / 8)];
        byte = uint8_t(value ? byte | mask : byte & ~mask);
        return *this;
    }
    StateBuilder &party(std::initializer_list<int> types) {
        require(types.size() <= 4, "party exceeds four members");
        for (int type : types)
            require(type >= 0 && type < 200, "invalid member type");
        std::fill(w.ram.begin() + 0xD000, w.ram.begin() + 0xD07C, 255);
        int i = 0;
        game::Machine m(w);
        const auto registers = w.cpu.s;
        for (int type : types) {
            require(type >= 0 && type < 200, "invalid member type");
            m.ss() = 0;
            m.ax() = uint16_t(type);
            m.di() = uint16_t(0xD000 + 31 * i++);
            game::initialize_creation_template(m); // the game's own type/stats/equipment initializer
        }
        w.cpu.s = registers;
        return *this;
    }
    StateBuilder &empty() {
        std::fill(w.ram.begin() + 0xD07C, w.ram.begin() + 0xD09E, 255);
        return *this;
    }
    StateBuilder &item(int slot, int id, int uses, bool key = false) {
        require(slot >= 0 && slot < (key ? 9 : 8), "invalid inventory slot");
        require(id >= 0 && id <= 255 && uses >= 0 && uses <= 255, "invalid item record");
        const int at = (key ? 0xD08C : 0xD07C) + 2 * slot;
        w.ram[size_t(at)] = uint8_t(id);
        w.ram[size_t(at + 1)] = uint8_t(uses);
        return *this;
    }
    StateBuilder &inventory_full() {
        for (int i = 0; i < 8; ++i)
            item(i, 32 + i, 50);
        return *this;
    }
    StateBuilder &gold(unsigned value) {
        require(value <= 999999, "gold exceeds game cap");
        word(0xD0A2, value);
        word(0xD0A4, value >> 16);
        return *this;
    }
    StateBuilder &flag(int bit, bool value = true) {
        require(bit >= 0 && bit < 152, "invalid event bit");
        auto &v = w.ram[size_t(0xD0EC + bit / 8)];
        v = uint8_t(value ? v | (1 << (bit % 8)) : v & ~(1 << (bit % 8)));
        return *this;
    }
    StateBuilder &crystals(int count) {
        require(count >= 0 && count <= 15, "invalid Crystals progress nibble");
        w.ram[0xD0AB] = uint8_t((w.ram[0xD0AB] & 15) | (count << 4));
        return *this;
    }
    StateBuilder &location(int map, int x, int y, int floor = 1, int highest = 1) {
        require(map >= 0 && map <= 210 && x >= 0 && x <= 255 && y >= 0 && y <= 255 && floor >= 1 &&
                    floor <= highest && highest <= 25,
                "invalid location");
        w.ram[0xD0DC] = uint8_t(map);
        w.ram[0xD0DD] = uint8_t(x);
        w.ram[0xD0DE] = uint8_t(y);
        w.ram[0xD0DB] = uint8_t(floor);
        w.ram[0xD0DA] = uint8_t(highest);
        return *this;
    }
    StateBuilder &status(int member, int bits) {
        require(member >= 0 && member < 4 && bits >= 0 && bits <= 255, "invalid status");
        w.ram[size_t(0xD000 + 31 * member)] = uint8_t(bits);
        return *this;
    }
    StateBuilder &hp(int member, int current, int maximum) {
        require(member >= 0 && member < 4 && current >= 0 && current <= maximum && maximum <= 999,
                "invalid HP");
        word(0xD006 + 31 * member, unsigned(current));
        word(0xD008 + 31 * member, unsigned(maximum));
        return *this;
    }
    StateBuilder &equipment(int member, int slot, int id = 255, int uses = 255) {
        require(member >= 0 && member < 4 && slot >= 0 && slot < 8 && id >= 0 && id <= 255 && uses >= 0 &&
                    uses <= 255,
                "invalid equipment");
        int at = 0xD00F + 31 * member + 2 * slot;
        w.ram[size_t(at)] = uint8_t(id);
        w.ram[size_t(at + 1)] = uint8_t(uses);
        return *this;
    }
    StateBuilder &standard() {
        party({Human, HumanFemale, Mutant, Monster}).empty().gold(1234).flag(5).hp(0, 1, 100);
        for (int i = 0; i < 8; ++i)
            item(i, i == 4 ? 0 : 32 + i, i == 4 ? 3 : 50);
        for (int i = 0; i < 9; ++i)
            item(i, 17 + i, 254, true);
        return *this;
    }
};
struct Region {
    const char *name;
    int start, end;
};
inline constexpr Region compared[] = {{"party: types, HP, stats, equipment", 0xD000, 0xD07C},
                                      {"inventory and key records", 0xD07C, 0xD0A2},
                                      {"gold, progression, vehicles, discovery, location", 0xD0A2, 0xD0DF},
                                      {"leader and options", 0xD0E3, 0xD0EC},
                                      {"story flags and remaining game-state block", 0xD0EC, 0xD438}};
inline std::vector<uint8_t> outcome(const emu::Wsc &w) {
    std::vector<uint8_t> r;
    for (const auto &region : compared)
        r.insert(r.end(), w.ram.begin() + region.start, w.ram.begin() + region.end);
    return r;
}
inline std::string changes(const std::vector<uint8_t> &before, const std::vector<uint8_t> &after) {
    std::ostringstream out;
    size_t index = 0;
    int count = 0;
    for (const auto &region : compared)
        for (int at = region.start; at < region.end; ++at, ++index)
            if (before[index] != after[index] && count++ < 8)
                out << " 0x" << std::hex << at << ":" << int(before[index]) << "->" << int(after[index]);
    return out.str();
}
struct Coverage {
    std::map<std::string, std::map<std::string, std::set<std::string>>> evidence;
    std::string test;
    void hit(std::string id, std::string obligation) { evidence[id][obligation].insert(test); }
    void check() const {
        for (const auto &e : game::InventoryUi::kElements) {
            const auto found = evidence.find(std::string(e.id));
            require(found != evidence.end(), "UNTESTED element: " + std::string(e.id));
            std::istringstream words{std::string(e.obligations)};
            for (std::string word; words >> word;)
                require(found->second.contains(word),
                        "UNTESTED obligation: " + std::string(e.id) + "/" + word);
        }
        for (bool key : {false, true})
            for (int slot = 0; slot < (key ? 9 : 8); ++slot) {
                auto id = std::string(key ? "row.key." : "row.item.") + std::to_string(slot);
                for (const char *obligation : {"activate", "cancel", "refused"})
                    require(evidence.contains(id) && evidence.at(id).contains(obligation),
                            "UNTESTED " + id + "/" + obligation);
            }
    }
};
struct LiveMenu {
    emu::Wsc w;
    game::InventoryUi inventory;
    game::ItemDialogUi dialog;
    game::PartyUi party;
    game::GfxSwap gfx;
    game::HostPicture handoff;
    game::TitleMenuLettering title_lettering;
    game::HostPicture::Screens picture_screens;
    std::array<std::vector<uint32_t>, 2> picture_before;
    int picture_submission = 0;
    int picture_fades = 0, picture_fade_peak = 0, picture_owner = 0, picture_warps = 0;
    uint64_t picture_fade_epoch = 0;
    int active_picture_owner() const {
        const auto &s = picture_screens;
        return s.item && s.item->active()             ? 2
               : s.save && s.save->active()           ? 3
               : s.service && s.service->active()     ? 4
               : s.dialogue && s.dialogue->active()   ? 5
               : s.creation && s.creation->active()   ? 6
               : s.bestiary && s.bestiary->active()   ? 7
               : s.party && s.party->active()         ? 8
               : s.inventory && s.inventory->active() ? 9
               : s.battle && s.battle->active()       ? 1
                                                      : 0;
    }
    // Independent native-state oracle: the all-palette darkener subtracts one from
    // each RGB nibble. Its largest surviving nibble gives the remaining brightness.
    int native_picture_level() const {
        int level = 0;
        for (int palette = 0; palette < 16; ++palette)
            for (int color = 0; color < 16; ++color) {
                const int at = 0xFE00 + palette * 32 + color * 2;
                const int rgb = w.ram[size_t(at)] | w.ram[size_t(at + 1)] << 8;
                for (int shift : {0, 4, 8})
                    level = std::max(level, rgb >> shift & 15);
            }
        return level;
    }
    std::vector<uint32_t> expected_picture_fade(std::vector<uint32_t> pixels, bool owned) const {
        // The travel warp's line interrupt scrolls each row by its distortion byte; outside columns are
        // black.
        if (owned && picture_warps && pixels.size() == size_t(emu::Wsc::kWidth * emu::Wsc::kHeight)) {
            const int width = emu::Wsc::kWidth;
            for (int y = 0; y < emu::Wsc::kHeight; ++y) {
                const int by = int8_t(w.ram[size_t(0xDCFE + y)]);
                const std::vector<uint32_t> row(pixels.begin() + y * width, pixels.begin() + (y + 1) * width);
                for (int x = 0; x < width; ++x)
                    pixels[size_t(y * width + x)] = x + by >= 0 && x + by < width ? row[size_t(x + by)] : 0;
            }
        }
        if (owned && picture_fade_peak) {
            const int level = std::min(native_picture_level(), picture_fade_peak);
            for (auto &pixel : pixels) {
                const auto r = (pixel >> 16 & 255) * unsigned(level) / unsigned(picture_fade_peak);
                const auto g = (pixel >> 8 & 255) * unsigned(level) / unsigned(picture_fade_peak);
                const auto b = (pixel & 255) * unsigned(level) / unsigned(picture_fade_peak);
                pixel = (pixel & 0xFF000000) | r << 16 | g << 8 | b;
            }
        }
        return pixels;
    }
    uint64_t completed_picture_epoch = ~0ull;
    bool audit_handoffs = std::getenv("SAWGOD_MENU_HANDOFF_AUDIT") != nullptr;
    std::vector<game::hooks::Subscription> picture_watchers;
    bool requested_lcd = false;
    bool picture_frame_running = false;
    void begin_picture_frame() {
        requested_lcd = w.render_lcd;
        picture_frame_running = true;
        if (host && audit_handoffs && (completed_picture_epoch != w.presentation_epoch || picture_fade_peak))
            w.render_lcd = true;
    }
    void end_picture_frame() {
        picture_frame_running = false;
        w.render_lcd = requested_lcd;
    }
    void attach_pictures() {
        picture_watchers.clear();
        handoff.install(w, picture_screens);
        title_lettering.attach(w);
        completed_picture_epoch = ~0ull;
        picture_before = {};
        picture_fades = picture_fade_peak = picture_owner = picture_warps = 0;
        if (host && audit_handoffs) {
            // Run after HostPicture's notifications. Start scanout on the exact invalidating
            // event, including events inside run_frame; never skip a transaction frame.
            // Between-frame invalidations are picked up by begin_picture_frame instead:
            // overriding the caller's setting there would make the LCD stay on forever.
            auto &hooks = game::Session::of(w).hooks();
            for (size_t e = 0; e < size_t(game::hooks::Event::Count); ++e)
                picture_watchers.push_back(
                    hooks.subscribe(game::hooks::Event(e), [this, e](game::Machine &, const auto &a) {
                        if (game::hooks::Event(e) == game::hooks::Event::PresentationPicture) {
                            if (a.a == 3) {
                                if (!picture_fades && (picture_owner || active_picture_owner())) {
                                    picture_fade_peak = std::max(1, native_picture_level());
                                    picture_fade_epoch = w.presentation_epoch;
                                }
                                ++picture_fades;
                            } else if (a.a == 4)
                                picture_fades = std::max(0, picture_fades - 1);
                        }
                        if (game::hooks::Event(e) == game::hooks::Event::TravelWarpEffect)
                            picture_warps = std::max(0, picture_warps + (a.a ? 1 : -1));
                        if (picture_frame_running &&
                            (completed_picture_epoch != w.presentation_epoch || picture_fade_peak))
                            w.render_lcd = true;
                    }));
        }
    }
    void audit_picture() {
        if (!host || !audit_handoffs || !w.render_lcd)
            return;
        const auto &s = picture_screens;
        // A host decision (the Esper replacement prompt) parks the machine mid-frame: its own complete
        // picture owns the screen until it is answered; the native frame then finishes as before.
        if (w.host_paused && s.battle && game::Session::of(w).esper_change()) {
            for (int mode = 0; mode < 2; ++mode)
                require(handoff.picture(w, emu::Wsc::ColorMode::Raw, mode != 0) ==
                            s.battle->picture(w, emu::Wsc::ColorMode::Raw, mode != 0),
                        "host decision picture is not the battle prompt, frame " +
                            std::to_string(w.frame_count));
            return;
        }
        handoff.frame(w);
        // Independent endpoint producer: direct screen composition, bypassing retained pixels.
        // Read-only native scanout provenance bounds the transaction; it is not the pixel oracle.
        const bool complete =
            !w.presentation_epoch || (w.presentation_ready == w.presentation_epoch &&
                                      std::all_of(w.presentation_raster.begin(), w.presentation_raster.end(),
                                                  [&](uint64_t e) { return e == w.presentation_epoch; }));
        // A field producer can finish while a service's logical scope is unwinding.
        // In that interval only the previous complete endpoint may own the screen.
        const bool field_unwinding =
            complete && picture_submission == 2 &&
            ((s.item && s.item->active()) || (s.save && s.save->active()) ||
             (s.service && s.service->active()) || (s.creation && s.creation->active()) ||
             (s.bestiary && s.bestiary->active()) || (s.party && s.party->active()) ||
             (s.inventory && s.inventory->active()) || (s.battle && s.battle->active()));
        // Audit every physical frame of a transaction, then stop recomposing idle
        // pictures. Native cursor animation/text reveal after submission is outside it.
        const int destination = active_picture_owner();
        if (!picture_fades && picture_fade_peak &&
            (native_picture_level() >= picture_fade_peak ||
             (complete && !field_unwinding &&
              (w.presentation_epoch != picture_fade_epoch || destination != picture_owner))))
            picture_fade_peak = 0;
        if (complete && !field_unwinding && completed_picture_epoch == w.presentation_epoch &&
            !picture_fade_peak)
            return;
        for (int mode = 0; mode < 2; ++mode) {
            const bool gb = mode != 0;
            const auto actual = handoff.picture(w, emu::Wsc::ColorMode::Raw, gb);
            if (complete && !field_unwinding) {
                auto expected =
                    s.item && s.item->active()         ? s.item->picture(w, emu::Wsc::ColorMode::Raw, gb)
                    : s.save && s.save->active()       ? s.save->picture(w, emu::Wsc::ColorMode::Raw, gb)
                    : s.service && s.service->active() ? s.service->picture(w, emu::Wsc::ColorMode::Raw, gb)
                    : s.dialogue && s.dialogue->active()
                        ? s.dialogue->picture(w, emu::Wsc::ColorMode::Raw, gb)
                    : s.creation && s.creation->active()
                        ? s.creation->picture(w, emu::Wsc::ColorMode::Raw, gb)
                    : s.bestiary && s.bestiary->active()
                        ? s.bestiary->picture(w, emu::Wsc::ColorMode::Raw, gb)
                    : s.party && s.party->active() ? s.party->picture(w, emu::Wsc::ColorMode::Raw, gb)
                    : s.inventory && s.inventory->active()
                        ? s.inventory->picture(w, emu::Wsc::ColorMode::Raw, gb)
                    : s.battle && s.battle->active() ? s.battle->picture(w, emu::Wsc::ColorMode::Raw, gb)
                                                     : w.framebuffer_rgb(emu::Wsc::ColorMode::Raw, gb);
                title_lettering.composite(w, expected, emu::Wsc::ColorMode::Raw, gb);
                require(actual == expected_picture_fade(expected, destination != 0),
                        "handoff did not submit complete destination, frame " +
                            std::to_string(w.frame_count));
                picture_before[size_t(mode)] = expected;
            } else if (!picture_before[size_t(mode)].empty()) {
                require(actual == expected_picture_fade(picture_before[size_t(mode)], picture_owner != 0),
                        "partial handoff picture, frame " + std::to_string(w.frame_count));
            } else
                picture_before[size_t(mode)] = actual;
        }
        if (complete && !field_unwinding) {
            completed_picture_epoch = w.presentation_epoch;
            picture_owner = destination;
        }
    }
    std::vector<game::hooks::Subscription> subscriptions;
    bool host, natives, returned = false;
    int stage = 0, item_dialog = 0, message = 0, party_scene = 0;
    std::function<void(LiveMenu &)> visit;
    // decompiled = false leaves every routine to the interpreter: the ROM's own code is the judge. No hook
    // fires there, so start() waits a fixed number of frames instead of for the scene announcement.
    LiveMenu(const std::vector<uint8_t> &rom, const emu::Wsc::Snapshot &base, bool redesigned,
             const game::TextTable &english, bool decompiled = true)
        : w(rom, 0x2000), host(redesigned), natives(decompiled) {
        require(natives || !host, "the redesigned UI needs the decompiled routines");
        w.restore(base);
        if (natives)
            game::install(w);
        auto &s = game::Session::of(w);
        s.text() = english;
        if (host) {
            inventory.install(w);
            dialog.install(w, &inventory);
            party.install(w);
            picture_screens.inventory = &inventory;
            picture_screens.item = &dialog;
            picture_screens.party = &party;
            attach_pictures();
        }
        if (host && audit_handoffs) {
            if (const char *resources = std::getenv("SAWGOD_MENU_HANDOFF_GB_RES"))
                enable_gb(resources);
            subscriptions.push_back(s.hooks().subscribe(game::hooks::Event::PresentationPicture,
                                                        [this](game::Machine &, const auto &a) {
                                                            if (a.a == 1 || a.a == 2 || a.a == 5)
                                                                picture_submission = a.a;
                                                        }));
        }
        auto observe = [&](game::hooks::Event e, int &v) {
            subscriptions.push_back(
                s.hooks().subscribe(e, [&v](game::Machine &, const auto &a) { v = a.a; }));
        };
        observe(game::hooks::Event::InventoryMenu, stage);
        observe(game::hooks::Event::ItemDialog, item_dialog);
        observe(game::hooks::Event::ItemMessage, message);
        observe(game::hooks::Event::PartyMenu, party_scene);
        w.set_native(0x340000 + exit_ip(), [this](emu::Wsc &hw) {
            returned = true;
            hw.cpu.s.flags &= uint16_t(~emu::IF);
            hw.cpu.s.halted = true;
            return 1;
        });
    }
    // Where the menu routine returns to. With every routine native nothing executes ROM bytes, so any
    // address serves; interpreted, 4000:0100 is inside read_keypad, so use a data table (D_34B7B2) instead.
    uint16_t exit_ip() const { return natives ? 0x0100 : 0xB7B2; }
    void enable_gb(const std::filesystem::path &dir) {
        auto vector_read = [&](const char *name) {
            auto b = read(dir / "gb" / name);
            return std::vector<uint8_t>(b.begin(), b.end());
        };
        std::string error;
        require(gfx.add_font(vector_read("font.png"), &error), error);
        require(gfx.add_sprites(vector_read("sprites.png"), read(dir / "gb/sprite_types.csv"), &error),
                error);
        gfx.set_enabled(w, true);
    }
    void frame(uint16_t key = 0) {
        begin_picture_frame();
        w.set_keys(key);
        w.run_frame();
        if (gfx.enabled() && (!audit_handoffs || w.render_lcd))
            gfx.frame(w);
        if (host) {
            inventory.frame(w);
            dialog.frame(w);
            party.frame(w);
            audit_picture();
        }
        end_picture_frame();
    }
    void start(bool equip = false) {
        // Fresh entry to the real menu on the initialized fixture's hardware; no suspended fiber is cloned.
        w.cpu.s.seg[emu::CS] = 0x4000;
        w.cpu.s.seg[emu::SS] = w.cpu.s.seg[emu::DS] = w.cpu.s.seg[emu::ES] = 0;
        w.cpu.s.r[emu::SP] = 0xBE00;
        w.ram[0xBE00] = uint8_t(exit_ip());
        w.ram[0xBE01] = uint8_t(exit_ip() >> 8);
        w.cpu.s.ip = equip ? 0x3CBE : 0x1FB7;
        // The transfer tab (L_343BA7) zeroes the member selector before it calls 0x3CBE, which does not. The
        // fixture leaves 23 there from another screen; entering directly, do the caller's part.
        if (equip)
            w.ram[0xD853] = 0;
        w.cpu.s.flags |= emu::IF;
        w.cpu.s.halted = false;
        for (int i = 0; i < 30 && (!natives || !(equip ? party_scene == 5 : stage == 1)); ++i)
            frame();
        require(!natives || (equip ? party_scene == 5 : stage == 1), "menu did not open");
        for (int i = 0; i < 8; ++i)
            frame();
        if (visit)
            visit(*this);
    }
    void press(uint16_t key) {
        require(!returned, "button sent to a closed menu");
        for (int i = 0; i < 3; ++i)
            frame(key);
        for (int i = 0; i < 9; ++i)
            frame();
        if (visit)
            visit(*this);
    }
    bool source_window() {
        // Native sprite 4 is the second hand: show_secondary_menu_cursor / hide_secondary_menu_cursor.
        // Its X byte is E0h while hidden. Host presentation exposes source directly in its View.
        return host && inventory.active() ? inventory.view(w).source >= 0 : w.ram[0xC013] != 0xE0;
    }
    int target() { return host && dialog.active() ? dialog.view(w).target : w.ram[0xD844]; }
    int member() { return host && party.active() ? party.view(w).member : w.ram[0xD853]; }
    int tab() { return host && inventory.active() ? inventory.view(w).tab : int(w.ram[0xD852]); }
    int selected() {
        if (host && inventory.active())
            return inventory.view(w).selected;
        if (host && party.active())
            return party.view(w).selected;
        return w.ram[0xD842 + (w.ram[0xD852] ? 1 : 0)];
    }
};
// Bounded navigator: every decision observes the current cursor; no movie frame numbers or RAM pokes.
struct Navigator {
    LiveMenu &m;
    void slot(int target, int tab = 0) {
        if (m.host && m.inventory.active() && m.inventory.view(m.w).tabs_focused)
            m.press(emu::KEY_X3);
        if (m.tab() != tab)
            m.press(tab ? emu::KEY_X2 : emu::KEY_X4);
        require(m.tab() == tab, "tab switch refused");
        for (int n = 0; m.selected() != target && n < 20; ++n)
            m.press(emu::KEY_X3);
        require(m.selected() == target, "navigator did not reach slot " + std::to_string(target));
    }
    int find(std::u32string_view name, int tab = 0) {
        for (int i = 0; i < (tab ? 9 : 8); ++i)
            if (game::inventory_row(m.w, tab, i).name == name)
                return i;
        throw std::runtime_error("item intent cannot find name");
    }
    void cancel() { m.press(emu::KEY_B); }
    void move(int source, int destination, int tab = 0) {
        slot(source, tab);
        m.press(emu::KEY_A);
        slot(destination, tab);
        m.press(emu::KEY_A);
    }
    void discard(std::u32string_view name) { move(find(name), 9); }
    void use(std::u32string_view name, int target, bool accept = true) {
        const int source = find(name);
        slot(source);
        m.press(emu::KEY_A);
        m.press(emu::KEY_A);
        require(m.item_dialog == 1, "use intent did not reach target window");
        for (int n = 0; m.target() != target && n < 4; ++n)
            m.press(emu::KEY_X3);
        require(m.target() == target, "target cursor did not reach member");
        if (accept)
            m.press(emu::KEY_A);
    }
    void sort() { // Native inventory has no sort command: sorting is a sequence of native moves.
        for (int i = 0; i < 8; ++i) {
            int best = i;
            for (int j = i + 1; j < 8; ++j)
                if (m.w.ram[size_t(0xD07C + 2 * j)] < m.w.ram[size_t(0xD07C + 2 * best)])
                    best = j;
            if (best != i)
                move(best, i);
        }
    }
    void equip(int member, std::u32string_view name) {
        for (int n = 0; m.member() != member && n < 4; ++n)
            m.press(emu::KEY_Y2);
        require(m.member() == member, "equip member cursor failed");
        m.press(emu::KEY_X2);
        require(m.w.ram[0xD853] == member,
                "equip member changed on Right: " + std::to_string(m.w.ram[0xD853]));
        int source = find(name);
        slot(source, 1); // transfer menu column 1 is inventory, D855 selects Items/Keys
        require(m.w.ram[0xD853] == member,
                "equip member changed during slot selection: " + std::to_string(m.w.ram[0xD853]));
        m.press(emu::KEY_A);
        require(m.w.ram[0xD853] == member,
                "equip member changed on source A: " + std::to_string(m.w.ram[0xD853]));
        m.press(emu::KEY_A); // native auto-transfer to first compatible slot
    }
};
} // namespace ui_pilot
