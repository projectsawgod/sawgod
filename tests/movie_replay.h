// Replays the input recordings (tests/movies/*.txt, tools/movie.py's format) on the native game inside a
// doctest, with the host state the app keeps around it: full player names enabled and a save store attached
// (every slot in the store, none in SRAM). Shared by the host-name and save sweep tests.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "emu/wsc.h"
#include "game/machine.h"
#include "game/player_names.h"
#include "game/save_file.h"
#include "game/save_slots.h"
#include "game/session.h"
#include "game/text.h"
#include "game/text_render.h"
#include "tools/difftest_inputs.h"

namespace movie_replay {

namespace fs = std::filesystem;
using namespace sawgod;

inline constexpr uint16_t kGameState = 0xD000, kMapId = 0xD0DC;
inline constexpr size_t kMemberStride = 31;

inline fs::path repo_root() { return fs::path(SAWGOD_TEST_FIXTURES).parent_path().parent_path(); }

inline std::vector<uint8_t> read_bytes(const fs::path &path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

// The WSC ROM from SAWGOD_WSC_ROM, or empty (the test then skips).
inline std::vector<uint8_t> wsc_rom() {
    const char *path = std::getenv("SAWGOD_WSC_ROM");
    if (!path)
        return {};
    auto rom = read_bytes(path);
    if (rom.size() != 0x400000)
        throw std::runtime_error("SAWGOD_WSC_ROM must name the 4 MiB WSC ROM");
    return rom;
}

// A recording: key changes, RAM holds/pokes and the slot file it starts from (tools/movie.py read_movie and
// movie_args: 'F b*' is a 6-frame tap, '+N' shifts later frames, # starts a comment).
struct Recording {
    std::vector<std::pair<int, uint16_t>> keys;
    tools::MovieInputs::Holds holds;
    std::vector<tools::MovieInputs::Poke> pokes;
    fs::path slot;
    int last = 0; // last scheduled key change or poke
};

inline uint16_t parse_buttons(const std::string &s) {
    static const std::pair<const char *, uint16_t> names[] = {
        {"up", emu::KEY_X1},       {"right", emu::KEY_X2},  {"down", emu::KEY_X3},  {"left", emu::KEY_X4},
        {"up2", emu::KEY_Y1},      {"right2", emu::KEY_Y2}, {"down2", emu::KEY_Y3}, {"left2", emu::KEY_Y4},
        {"start", emu::KEY_START}, {"a", emu::KEY_A},       {"b", emu::KEY_B},
    };
    uint16_t keys = 0;
    std::string word;
    std::istringstream in(s);
    while (std::getline(in, word, ',')) {
        std::istringstream words(word);
        for (std::string w; words >> w;)
            for (const auto &[name, key] : names)
                if (w == name)
                    keys |= key;
    }
    return keys;
}

inline Recording read_recording(const fs::path &path) {
    Recording r;
    std::ifstream f(path);
    if (!f)
        throw std::runtime_error("Missing recording: " + path.string());
    int base = 0;
    for (std::string raw; std::getline(f, raw);) {
        std::string line = raw.substr(0, raw.find('#'));
        std::istringstream in(line);
        std::string first;
        if (!(in >> first))
            continue;
        std::string value;
        if (first == "slot" || first == "poke" || first == "hold") {
            in >> value;
            if (first == "slot")
                r.slot = repo_root() / value;
            else {
                const auto eq = value.find('=');
                const auto addr = uint16_t(std::strtoul(value.c_str(), nullptr, 16));
                const auto byte = uint8_t(std::strtoul(value.c_str() + eq + 1, nullptr, 16));
                if (first == "hold")
                    r.holds.emplace_back(addr, byte);
                else {
                    const int at = std::atoi(value.c_str() + value.find('@') + 1);
                    r.pokes.push_back({at, addr, byte});
                    r.last = std::max(r.last, at);
                }
            }
            continue;
        }
        if (first[0] == '+') {
            base += std::atoi(first.c_str() + 1);
            continue;
        }
        const int frame = std::atoi(first.c_str()) + base;
        std::string buttons;
        std::getline(in, buttons);
        while (!buttons.empty() && buttons.back() == ' ')
            buttons.pop_back();
        const bool tap = !buttons.empty() && buttons.back() == '*';
        if (tap)
            buttons.pop_back();
        r.keys.emplace_back(frame, parse_buttons(buttons));
        if (tap)
            r.keys.emplace_back(frame + 6, 0);
        r.last = std::max(r.last, frame + (tap ? 6 : 0));
    }
    return r;
}

// The four machine bytes a host name stands for: its first four glyphs in the game's charset (space 0xD9
// past its end or for a glyph the charset lacks; U+E0xx is the WSC glyph xx), as a save file loads it.
inline std::array<uint8_t, 4> name_projection(const std::string &name) {
    std::array<uint8_t, 4> bytes{0xD9, 0xD9, 0xD9, 0xD9};
    const auto glyphs = game::utf8_to_u32(name);
    for (size_t n = 0; n < 4 && n < glyphs.size(); ++n) {
        const char32_t cp = glyphs[n];
        if (cp >= 0xE000 && cp <= 0xE0FF) {
            bytes[n] = uint8_t(cp - 0xE000);
            continue;
        }
        static const game::Charset charset = game::Charset::system();
        std::vector<uint8_t> code;
        if (charset.encode(game::name_utf8(std::u32string(1, cp)), code, nullptr) && code.size() == 1)
            bytes[n] = code[0];
    }
    return bytes;
}

inline std::vector<uint8_t> game_state(const emu::Wsc &w) {
    return {w.ram.begin() + kGameState, w.ram.begin() + kGameState + game::SaveSlots::kPayload};
}

// One machine as the app runs it: natives, full names, the save store.
struct Machine {
    std::unique_ptr<emu::Wsc> w;
    explicit Machine(const std::vector<uint8_t> &rom) {
        static const size_t kSram[] = {0, 0x2000, 0x8000, 0x20000, 0x40000, 0x80000};
        const uint8_t type = rom[rom.size() - 16 + 0x0B];
        w = std::make_unique<emu::Wsc>(rom, type <= 5 ? kSram[type] : 0);
        game::install(*w);
        w->render_lcd = false;
        session().player_names().enabled = true;
        session().save_slots().attach_store();
    }
    game::Session &session() { return game::Session::of(*w); }
    game::PlayerNames &names() { return session().player_names(); }
    // Slot 1 of the lists (slot 0), last used, as the recordings' Continue expects.
    void put_slot(const std::vector<uint8_t> &payload, const game::PartyNameList &names) {
        session().save_slots().load_payload(0, payload, names);
        session().save_slots().load_last(0);
    }
    void frame(uint16_t keys) {
        w->set_keys(keys);
        w->run_frame();
        w->apu.take();
    }
};

// A slot file as payload and names (false: it does not parse).
inline bool load_slot_file(const fs::path &path, std::vector<uint8_t> *payload, game::PartyNameList *names,
                           std::string *error) {
    const auto text = read_bytes(path);
    return game::save_from_text(std::string_view(reinterpret_cast<const char *>(text.data()), text.size()),
                                payload, error, names);
}

} // namespace movie_replay
