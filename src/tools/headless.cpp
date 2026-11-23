// sawgod-headless: runs the ROM in our interpreter without a window, for tests and oracle comparison.
//
//   sawgod-headless ROM --frames N [--inputs FILE] [--sram FILE] [--savesram FILE]
//                   [--goldendir DIR --goldenevery N] [--trace FILE --tracecount N] [--shot FILE] [--mod
//                   PACK]...
//
// --inputs: lines "FRAME buttons" (buttons comma-separated reference engine names: up down left right up2
// down2 left2 right2 start a b), held from that frame on, using absolute frame numbers from boot.
// --party-ui: the field/Status/Equip layouts; requires --native (app: enabled).
// --inventory-ui: the eight-row Items/Key Items layout; requires --native (app: enabled).
// --item-dialog-ui: the item target/refill/travel/overflow dialogs and item messages; requires --native
// (app: enabled).
// --creation-ui: character creation and name entry; requires --native (app: enabled).
// --bestiary-ui: the Bestiary (member history); requires --native (app: enabled).
// --goldendir writes F.ram / F.sram / F.png every --goldenevery frames, like the harness.
// --trace writes instruction registers and bytes in a line-oriented text format.
// --fingerprint FILE: per-frame RGB/PCM hashes; no GPU or PNG encoding.
//
// --states FILE (with --invoke only): per-routine ROM overrides for direct invocation, a tiny TOML subset
// (see the StateOverride/TomlValue block below for the exact grammar). Covers routines whose --invoke
// snapshots alone don't exercise them: an event handler read mid
// script, a routine that needs another one run first to populate scratch RAM (e.g. a battle-formation
// record), or a routine whose original never reaches a normal return address (an end_at override).
//
// --keyfuzz N (with --difftest, real-call mode): the menu-loop key fuzz. At each of the first
// --keyfuzz-calls C (default 1) real calls of every selected frame-spanning (yields) routine, N extra runs
// from that exact call state (registers and all, unlike --fuzz) with seeded random key sequences -- presses
// and releases over many frames, weighted by --keyfuzz-keys ("a=4,b=1,up2=2,...", reference engine names as
// in
// --inputs) -- comparing native vs original RAM (which holds VRAM, palettes and sprites), I/O and SRAM at
// every frame point (each wait_vblank / read_keypad / wait_scanline entry), until the routine returns or
// --keyfuzz-frames F (default 600) wait_vblank frame points pass. Run k uses seed --keyfuzz-seed + k - 1
// (default base 1): `--keyfuzz-seed S --keyfuzz 1` replays one run.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "app/gb_music.h"
#include "app/host_settings.h"
#include "app/sound_mix.h"
#include "emu/png.h"
#include "emu/wsc.h"
#include "engine/assets.h"
#include "engine/button_icons.h"
#include "engine/screen.h"
#include "game/autosave.h"
#include "game/bank35_world.h"
#include "game/battle_ui.h"
#include "game/bestiary_ui.h"
#include "game/cheats.h"
#include "game/creation_ui.h"
#include "game/dialogue_ui.h"
#include "game/game_config.h"
#include "game/gfx_swap.h"
#include "game/host_picture.h"
#include "game/info.h"
#include "game/inventory_ui.h"
#include "game/item_dialog_ui.h"
#include "game/light_tags.h"
#include "game/machine.h"
#include "game/map_lz.h"
#include "game/maps.h"
#include "game/party_ui.h"
#include "game/port_policy.h"
#include "game/resources.h"
#include "game/save_file.h"
#include "game/save_ui.h"
#include "game/service_ui.h"
#include "game/session.h"
#include "game/setup.h"
#include "game/tables.h"
#include "game/text.h"
#include "game/text_fit.h"
#include "game/text_render.h"
#include "game/text_tokens.h"
#include "tools/difftest_clock.h"
#include "tools/difftest_inputs.h"
#include "tools/enhanced_oracle.h"
#include "tools/field_preview.h"
#include "tools/gb_audit.h"
#include "tools/gb_effects_capture.h"
#include "tools/modifier_audit.h"

using namespace sawgod::emu;

namespace {

// FNV-1a over canonical RGB bytes / signed PCM16 little-endian, never host object bytes.
// Lengths are written alongside the hashes so missing samples cannot pass as silence.
template <class T> uint64_t fingerprint_hash(const std::vector<T> &values, int bytes) {
    uint64_t hash = 14695981039346656037ull;
    for (auto value : values)
        for (int byte = 0; byte < bytes; ++byte) {
            hash ^= (uint64_t(value) >> (8 * byte)) & 255;
            hash *= 1099511628211ull;
        }
    return hash;
}

std::vector<uint8_t> read_file(const std::string &path) {
    std::vector<uint8_t> data;
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
        return data;
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
        data.insert(data.end(), buf, buf + n);
    std::fclose(f);
    return data;
}

uint16_t parse_buttons(const std::string &s) {
    static const std::map<std::string, uint16_t> names = {
        {"up", KEY_X1},       {"right", KEY_X2},  {"down", KEY_X3},  {"left", KEY_X4},
        {"up2", KEY_Y1},      {"right2", KEY_Y2}, {"down2", KEY_Y3}, {"left2", KEY_Y4},
        {"start", KEY_START}, {"a", KEY_A},       {"b", KEY_B},
    };
    uint16_t keys = 0;
    size_t i = 0;
    while (i < s.size()) {
        size_t j = s.find_first_of(", ", i);
        if (j == std::string::npos)
            j = s.size();
        auto it = names.find(s.substr(i, j - i));
        if (it != names.end())
            keys |= it->second;
        i = j + 1;
    }
    return keys;
}

// --states: a tiny TOML subset for direct routine invocation. A value is an integer (decimal or
// 0x-hex), a "string" (used only for "ram16:0xADDR", a register value read from the start RAM at that
// address), an inline {a = v, b = v} table or a [v, v, ...] array; keys inside { } may be bare words or
// "quoted" (RAM addresses are quoted so leading-0x isn't mistaken for an identifier). Files are a sequence
// of [[state]] headers each followed by its own key = value lines; # starts a line comment. No multi-line
// strings, no dotted keys, no other TOML feature.
// `tab`'s entries are two parallel vectors (keys[i] => tab[i]), not vector<pair<string, TomlValue>>: a
// self-referential vector<TomlValue> is fine (libstdc++ supports incomplete types there), but a
// std::pair wrapping one isn't, one layer of indirection further than it supports.
struct TomlValue {
    enum class Kind { Int, Str, Arr, Tab } kind = Kind::Int;
    long long i = 0;
    std::string s;
    std::vector<TomlValue> arr;
    std::vector<std::string> keys;
    std::vector<TomlValue> tab;
    const TomlValue *find(const std::string &key) const {
        for (size_t k = 0; k < keys.size(); ++k)
            if (keys[k] == key)
                return &tab[k];
        return nullptr;
    }
    void add(const std::string &key, TomlValue v) {
        keys.push_back(key);
        tab.push_back(std::move(v));
    }
    // (key, value) pairs, for callers that want to iterate every entry (regs, setup.regs)
    std::vector<std::pair<std::string, TomlValue>> entries() const {
        std::vector<std::pair<std::string, TomlValue>> out;
        for (size_t k = 0; k < keys.size(); ++k)
            out.emplace_back(keys[k], tab[k]);
        return out;
    }
};

struct TomlParser {
    const std::string &s;
    size_t i = 0;
    explicit TomlParser(const std::string &src) : s(src) {}
    void skip_ws() {
        for (;;) {
            while (i < s.size() &&
                   (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r' || s[i] == ','))
                ++i;
            if (i < s.size() && s[i] == '#') {
                while (i < s.size() && s[i] != '\n')
                    ++i;
                continue;
            }
            break;
        }
    }
    std::string ident() {
        skip_ws();
        size_t j = i;
        while (i < s.size() && (std::isalnum(uint8_t(s[i])) || s[i] == '_'))
            ++i;
        return s.substr(j, i - j);
    }
    std::string quoted() {
        skip_ws();
        ++i; // opening quote
        size_t j = i;
        while (i < s.size() && s[i] != '"')
            ++i;
        std::string r = s.substr(j, i - j);
        if (i < s.size())
            ++i; // closing quote
        return r;
    }
    TomlValue value() {
        skip_ws();
        TomlValue v;
        if (i < s.size() && s[i] == '"') {
            v.kind = TomlValue::Kind::Str;
            v.s = quoted();
        } else if (i < s.size() && s[i] == '{') {
            ++i;
            v.kind = TomlValue::Kind::Tab;
            skip_ws();
            while (i < s.size() && s[i] != '}') {
                std::string key = s[i] == '"' ? quoted() : ident();
                skip_ws();
                if (i < s.size() && s[i] == '=')
                    ++i;
                v.add(key, value());
                skip_ws();
            }
            if (i < s.size())
                ++i; // '}'
        } else if (i < s.size() && s[i] == '[') {
            ++i;
            v.kind = TomlValue::Kind::Arr;
            skip_ws();
            while (i < s.size() && s[i] != ']') {
                v.arr.push_back(value());
                skip_ws();
            }
            if (i < s.size())
                ++i; // ']'
        } else {
            size_t j = i;
            if (i < s.size() && (s[i] == '-' || s[i] == '+'))
                ++i;
            while (i < s.size() && (std::isalnum(uint8_t(s[i])) || s[i] == 'x' || s[i] == 'X'))
                ++i;
            v.kind = TomlValue::Kind::Int;
            v.i = std::strtoll(s.substr(j, i - j).c_str(), nullptr, 0);
        }
        return v;
    }
    // Top level: [[state]] headers, each owning the key = value lines that follow it.
    std::vector<TomlValue> parse_states() {
        std::vector<TomlValue> states;
        for (;;) {
            skip_ws();
            if (i >= s.size())
                break;
            if (s.compare(i, 9, "[[state]]") == 0) {
                i += 9;
                states.emplace_back();
                states.back().kind = TomlValue::Kind::Tab;
                continue;
            }
            std::string key = ident();
            skip_ws();
            if (i < s.size() && s[i] == '=')
                ++i;
            TomlValue val = value();
            if (!states.empty())
                states.back().add(key, val);
        }
        return states;
    }
};

// A parsed [[state]] entry: explicit per-routine start state and invocation overrides.
struct StateOverride {
    uint32_t rom = 0;
    std::map<uint16_t, uint8_t> ram;                     // ram = { "0xADDR" = N, ... }
    std::vector<std::pair<std::string, TomlValue>> regs; // regs = { reg = N | "ram16:0xADDR", ... }
    struct Setup {
        uint32_t rom = 0;
        std::vector<std::pair<std::string, TomlValue>> regs;
    };
    std::vector<Setup> setup;                              // native routines run once before the passes
    std::vector<std::pair<uint16_t, uint16_t>> low_ram_ok; // [[lo, hi), ...] exempt from the low-RAM guard
    bool has_end_at = false;                               // end_at = { cs = ..., ip = ... }
    uint16_t end_cs = 0, end_ip = 0;
    uint64_t max_frames = 0; // optional yielding-fuzz budget; zero uses the default
};

std::map<uint32_t, StateOverride> load_states(const std::string &path) {
    std::map<uint32_t, StateOverride> out;
    std::vector<uint8_t> data = read_file(path);
    std::string text(data.begin(), data.end());
    TomlParser p(text);
    for (TomlValue &st : p.parse_states()) {
        StateOverride ov;
        if (const TomlValue *rom = st.find("rom"))
            ov.rom = uint32_t(rom->i);
        if (const TomlValue *ram = st.find("ram"))
            for (const auto &[k, v] : ram->entries())
                ov.ram[uint16_t(std::strtoul(k.c_str(), nullptr, 0))] = uint8_t(v.i);
        if (const TomlValue *regs = st.find("regs"))
            ov.regs = regs->entries();
        if (const TomlValue *frames = st.find("max_frames"); frames && frames->i > 0)
            ov.max_frames = uint64_t(frames->i);
        if (const TomlValue *setup = st.find("setup"))
            for (const TomlValue &se : setup->arr) {
                StateOverride::Setup su;
                if (const TomlValue *r = se.find("rom"))
                    su.rom = uint32_t(r->i);
                if (const TomlValue *rg = se.find("regs"))
                    su.regs = rg->entries();
                ov.setup.push_back(std::move(su));
            }
        if (const TomlValue *lro = st.find("low_ram_ok"))
            for (const TomlValue &pr : lro->arr)
                if (pr.arr.size() == 2)
                    ov.low_ram_ok.emplace_back(uint16_t(pr.arr[0].i), uint16_t(pr.arr[1].i));
        if (const TomlValue *ea = st.find("end_at")) {
            ov.has_end_at = true;
            if (const TomlValue *cs = ea->find("cs"))
                ov.end_cs = uint16_t(cs->i);
            if (const TomlValue *ip = ea->find("ip"))
                ov.end_ip = uint16_t(ip->i);
        }
        out[ov.rom] = std::move(ov);
    }
    return out;
}

// A regs/setup.regs value: either a literal or "ram16:0xADDR" (a word read from the start-state RAM, set
// after any `ram` overrides for the same state).
uint16_t resolve_reg_value(const TomlValue &v, const std::array<uint8_t, 0x10000> &ram) {
    if (v.kind == TomlValue::Kind::Str && v.s.rfind("ram16:", 0) == 0) {
        uint32_t addr = uint32_t(std::strtoul(v.s.c_str() + 6, nullptr, 0)) & 0xFFFF;
        return uint16_t(ram[addr] | (ram[(addr + 1) & 0xFFFF] << 8));
    }
    return uint16_t(v.i);
}
void set_named_reg(CpuState &c, const std::string &name, uint16_t val) {
    if (name == "ax")
        c.r[AX] = val;
    else if (name == "bx")
        c.r[BX] = val;
    else if (name == "cx")
        c.r[CX] = val;
    else if (name == "dx")
        c.r[DX] = val;
    else if (name == "sp")
        c.r[SP] = val;
    else if (name == "bp")
        c.r[BP] = val;
    else if (name == "si")
        c.r[SI] = val;
    else if (name == "di")
        c.r[DI] = val;
    else if (name == "cs")
        c.seg[CS] = val;
    else if (name == "ds")
        c.seg[DS] = val;
    else if (name == "es")
        c.seg[ES] = val;
    else if (name == "ss")
        c.seg[SS] = val;
    else
        std::fprintf(stderr, "--states: unknown register '%s'\n", name.c_str());
}

} // namespace

int main(int argc, char **argv) {
    if (argc == 2 &&
        std::string(argv[1]) == "--list-routines") { // registered routine names (difftest --jobs)
        size_t n = 0;
        const sawgod::game::Routine *rs = sawgod::game::routines(&n);
        for (size_t i = 0; i < n; ++i)
            std::printf("%s\n", rs[i].name);
        return 0;
    }
    std::string replay_path, features_path, ramtrace_path, irqdir, wav_path;
    std::string fingerprint_path;
    std::vector<int16_t> audio;
    std::string porttrace_path, gb_rom_path;
    std::vector<std::string> archives; // --mod: asset archives / override packs laid over the ROM, in order
    std::string
        difftest; // --difftest NAME[,NAME..]|all: original vs native from the same state at every call
    // --fuzz N: with --difftest, N extra runs per call from the same state, random registers
    int fuzz_runs = 0;
    // --invoke F1,F2..: with --difftest and --fuzz, call the routines directly at the end of those frames
    std::string invoke_frames;
    std::string states_path; // --states FILE: per-routine ROM overrides, --invoke mode only (see above)
    // --keyfuzz (see the header comment): runs per real call, calls per routine, frame cap, seed base,
    // weights
    int kf_runs = 0, kf_calls = 1, kf_frames = 600;
    long kf_seed_base = 1;
    std::string kf_keys = "up=3,right=3,down=3,left=3,up2=2,right2=2,down2=2,left2=2,a=4,b=1,start=1";
    uint64_t fuzz_state = 0x9E3779B97F4A7C15ull;
    auto fuzz_next = [&fuzz_state]() { // xorshift64*, fixed seed: runs are reproducible
        fuzz_state ^= fuzz_state >> 12;
        fuzz_state ^= fuzz_state << 25;
        fuzz_state ^= fuzz_state >> 27;
        return uint32_t((fuzz_state * 0x2545F4914F6CDD1Dull) >> 32);
    };
    bool difftrace = false;   // --difftrace: print the original's path through the first mismatching call
    std::string native;       // --native NAME|all: run these routines natively
    std::string profile_path; // --profile: "rom_offset count" of every executed instruction address
    int irqevery = 1;
    std::string traceaddrs = "0xC808,0xC809,0xC80A,0xC80B,0xC80C,0xC80D,0xC80E,0xC800,0xC801";
    std::string slot_toml; // --slot FILE.toml: a save slot file (docs/SAVES.md) written into SRAM slot 1
    std::string rom_path, inputs_path, sram_path, savesram_path, goldendir, trace_path, shot_path;
    std::string shot_screen; // --shotscreen off|lcd[:AMOUNT]|ips:SCALE: --shot as the app shows it (Raw
                             // color), not reference engine's
    std::string store_dir, store_import; // --store, --store-import
    int save_slots = 3;
    std::vector<std::pair<uint16_t, uint8_t>>
        holds; // --hold ADDR=VAL: internal RAM byte rewritten every frame
    using Poke = sawgod::tools::MovieInputs::Poke;
    std::vector<Poke> pokes; // --poke ADDR=VAL@FRAME: one internal RAM byte written before that frame
    std::string text_path;   // --text FILE (with --native): a language's text resource (game/text.h)
    std::string text_base;   // --text-base FILE: the base language (ja.csv) under --text, as the app loads it
    bool proportional = false; // --proportional: draw it in the port's font (game/text_render.h), as the app
    bool hide_orij = false;
    // --tables DIR (repeatable, layered on each other): data tables from CSVs (game/tables.h);
    // --tables-verify: check the loaded rows equal the ROM's, then exit
    std::vector<std::string> tables_dirs;
    std::string export_tables; // --export-tables DIR
    std::string export_text;   // --export-text FILE
    std::string export_tokens; // --export-text-tokens FILE
    bool text_icons =
        false; // --text-icons: the base language's button letters as icons, as the app draws them
    std::string text_fit, text_fit_report;       // --text-fit LANG.csv [--text-fit-report OUT.csv]
    std::string text_fit_context;                // runtime fallback names; only LANG.csv produces findings
    std::string text_budgets;                    // --text-budgets OUT.csv
    std::string text_budget_sample;              // English/TR calibration and template arguments
    bool text_surfaces = false;                  // export every surface instead of the tightest summary
    bool text_budget_japanese = false;           // --text-budget-japanese: measure the Japanese column
    std::string export_resources, resources_dir; // --export-resources DIR, --resources DIR
    bool resources_verify = false;
    std::string rom_reads_path; // --rom-reads FILE
    bool rom_reads_all = false;
    std::string watch_writes; // --watch-writes FROM-TO
    std::string gfx_swap_dir, gfx_swap_at, gb_audit_dir, enhanced_oracle_dir;
    int gb_audit_every = 1;
    bool oracle_terrain_veil = false;
    std::string gb_effects_dir;
    std::set<int> gb_audit_shots;
    std::string cheats_list; // --cheats
    std::string teleport;    // --teleport FLOOR@FRAME
    bool enemy_hp = false;   // --enemy-hp
    bool battle_layout = false, inventory_layout = false, party_layout = false, item_dialog_layout = false;
    bool creation_layout = false, bestiary_layout = false;
    bool dialogue_layout = false, save_layout = false, service_layout = false;
    bool player_names = false; // --player-names
    int battle_transparency = 30;
    bool autosave_on = false;    // --autosave
    bool game_config_on = false; // --game-config // --gfx-swap DIR, --gfx-swap-at F1,F2...
    bool maps_check = false, maps_verify = false, map_previews = false;
    std::string field_preview;         // --field-preview ID,X,Y: halted-CPU display fixture
    bool field_preview_full = false;   // stitch the complete field through the runtime renderer
    std::string export_maps, maps_dir; // --export-maps DIR, --maps DIR
    bool tables_verify = false, tables_poison = false, text_poison = false;
    sawgod::game::hooks::Subscription hide_orij_sub;
    std::string modifier_config, modifier_audit_path, modifier_route_in, modifier_route_out;
    int frames = 0, goldenevery = 0;
    long tracecount = 0;
    int tracestart = 0; // first frame (0-based, as reference engine's frameCount) to trace
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--frames")
            frames = std::atoi(next().c_str());
        else if (a == "--inputs")
            inputs_path = next();
        else if (a == "--sram")
            sram_path = next();
        else if (a ==
                 "--slot") // a slot file (docs/SAVES.md) as the original's SRAM slot 1: chapter recordings
            slot_toml = next();
        else if (a == "--hold") { // debug, e.g. --hold C881=0 (no random encounters) for scripted test walks
            const std::string v = next();
            holds.emplace_back(uint16_t(std::strtoul(v.c_str(), nullptr, 16)),
                               uint8_t(std::strtoul(v.c_str() + v.find('=') + 1, nullptr, 16)));
        } else if (a == "--poke") { // debug, e.g. --poke C827=5@3000 (with C825/C826: show dialogue script 5)
            const std::string v = next();
            pokes.push_back({std::atoi(v.c_str() + v.find('@') + 1),
                             uint16_t(std::strtoul(v.c_str(), nullptr, 16)),
                             uint8_t(std::strtoul(v.c_str() + v.find('=') + 1, nullptr, 16))});
        } else if (a == "--savesram")
            savesram_path = next();
        else if (a ==
                 "--autosave") // with --native --store: Gameplay > Autosave (game/autosave.h), as the app
            autosave_on = true;
        else if (a == "--game-config") // with --native: the game's Config in Options, defaults
                                       // (game/game_config.h)
            game_config_on = true;
        else if (a == "--enemy-hp") // with --native: Info's enemy HP boxes (game/info.h), as the app
            enemy_hp = true;
        else if (a == "--save-ui")
            save_layout = true;
        else if (a == "--service-ui")
            service_layout = true;
        else if (a == "--dialogue-ui")
            dialogue_layout = true;
        else if (a == "--party-ui")
            party_layout = true;
        else if (a == "--inventory-ui")
            inventory_layout = true;
        else if (a == "--item-dialog-ui") // item target/refill/travel/overflow dialogs and item messages
            item_dialog_layout = true;
        else if (a == "--player-names") // with --native: full player names, as the layouts turn them on
            player_names = true;
        else if (a == "--button-icons" &&
                 i + 1 < argc) { // key | xbox | ps | switch: button tokens as the default bindings
            const std::string kind = argv[++i];
            const int column = kind == "switch" ? 2 : kind == "ps" ? 1 : 0; // kPad: Xbox, PS, Switch
            const std::string family = kind == "switch" ? "n:" : kind == "ps" ? "p:" : "x:";
            static constexpr const char *kKeys[sawgod::kButtonTokenCount] = {
                "key Up", "key Right", "key Down", "key Left", "key:W",    "key:D",
                "key:S",  "key:A",     "key:X",    "key:Z",    "key Enter"};
            static constexpr const char *kPad[sawgod::kButtonTokenCount][3] = {
                {"d-pad up", "d-pad up", "d-pad up"},
                {"d-pad right", "d-pad right", "d-pad right"},
                {"d-pad down", "d-pad down", "d-pad down"},
                {"d-pad left", "d-pad left", "d-pad left"},
                {"R stick up", "R stick up", "R stick up"},
                {"R stick right", "R stick right", "R stick right"},
                {"R stick down", "R stick down", "R stick down"},
                {"R stick left", "R stick left", "R stick left"},
                {"pad A", "PS cross", "pad B"},
                {"pad B", "PS circle", "pad A"},
                {"Start", "Start", "Switch +"}};
            const bool keys = kind == "key";
            sawgod::set_button_token_icons([keys, column, family](int t) {
                if (t == sawgod::kButtonTokenX || t == sawgod::kButtonTokenY)
                    return sawgod::pad_group_icon(t == sawgod::kButtonTokenY, keys);
                return sawgod::named_icon(keys ? std::string(kKeys[t]) : family + kPad[t][column]);
            });
        } else if (a == "--creation-ui")
            creation_layout = true;
        else if (a == "--bestiary-ui")
            bestiary_layout = true;
        else if (a == "--battle-ui")
            battle_layout = true;
        else if (a == "--battle-transparency")
            battle_transparency = std::atoi(next().c_str());
        else if (a == "--teleport") // with --native: FLOOR@FRAME, Teleport to Floor requested at FRAME
            teleport = next();
        else if (
            a ==
            "--cheats") // with --native:
                        // no_damage,one_hit_kills,unbreakable_gear,unlimited_hearts,stat_growth=150,encounters=1|2,gold=0|50|100|200|400
                        // (percent)
            cheats_list = next();
        else if (a == "--modifier-config")
            modifier_config = next();
        else if (a == "--modifier-audit")
            modifier_audit_path = next();
        else if (a == "--modifier-route-in")
            modifier_route_in = next();
        else if (a == "--modifier-route-out")
            modifier_route_out = next();
        else if (a == "--hide-orijinaru") // with --native: as the app (game/port_policy.h)
            hide_orij = true;
        else if (a == "--save-slots") // with --native: the Save Slots option (3 or 9; game::SaveSlots)
            save_slots = std::atoi(next().c_str());
        else if (a == "--text") // with --native: load this text resource (key,text CSV; game/text.h)
            text_path = next();
        else if (a == "--text-base")
            text_base = next();
        else if (a == "--tables") // data tables from extracted or mod CSVs (game/tables.h)
            tables_dirs.push_back(next());
        else if (a == "--export-tables") // write the tables as the app's first run extracts them, then exit
            export_tables = next();
        else if (a ==
                 "--export-text") // write the text resource (ja.csv) as the first run extracts it, then exit
            export_text = next();
        else if (a == "--export-text-tokens") // key,tokens: each text key's token form (game/text_tokens.h)
            export_tokens = next();
        else if (a == "--text-icons")
            text_icons = true;
        else if (a == "--text-fit") // check a translation against the space on screen (game/text_fit.h), then
            text_fit = next(); // exit: 0 fits, 1 errors (warnings alone pass); base: --text-base or the ROM's
        else if (a == "--text-fit-report") // with --text-fit: write every issue as CSV
            text_fit_report = next();
        else if (a == "--text-fit-context")
            text_fit_context = next();
        else if (a ==
                 "--text-budgets") // write each key's space on screen (key,surface,width,lines), then exit
            text_budgets = next();
        else if (a == "--text-budget-sample")
            text_budget_sample = next();
        else if (a == "--text-budget-japanese")
            text_budget_japanese = true;
        else if (a == "--text-surfaces")
            text_surfaces = true;
        else if (a == "--export-resources") // write the resource regions' files (game/resources.h), then exit
            export_resources = next();
        else if (a ==
                 "--resources") // load the resource regions from DIR (as the app does), ROM bytes replaced
            resources_dir = next();
        else if (a == "--resources-verify") // with --resources: the loaded regions equal the ROM's bytes
            resources_verify = true;
        else if (a == "--rom-reads") // write which ROM bytes the run read that no resource file covers
            rom_reads_path = next();
        else if (a == "--rom-reads-all") // with --rom-reads: list every range read, covered or not
            rom_reads_all = true;
        else if (a ==
                 "--gfx-swap") // with --native: GB graphics (game/gfx_swap.h) from DIR/gb/NNN_*, as the app
            gfx_swap_dir = next();
        else if (a == "--oracle-terrain-veil")
            oracle_terrain_veil = true;
        else if (a == "--enhanced-oracle")
            enhanced_oracle_dir = next();
        else if (a == "--gb-audit")
            gb_audit_dir = next();
        else if (a == "--gb-effects-capture")
            gb_effects_dir = next();
        else if (a == "--gb-audit-shot")
            gb_audit_shots.insert(std::atoi(next().c_str()));
        else if (a == "--gb-audit-every")
            gb_audit_every = std::max(1, std::atoi(next().c_str()));
        else if (a ==
                 "--gfx-swap-at") // with --gfx-swap: frames (comma-separated) to switch the PNGs WSC/GB at
            gfx_swap_at = next();
        else if (a == "--watch-writes") // debug: FROM-TO (linear, hex): which code (ROM offset) writes there
            watch_writes = next();
        else if (a ==
                 "--maps-check") // the maps' LZ layers: game/map_lz.h vs the game's own unpacker, repacked
            maps_check = true;
        else if (a == "--field-preview")
            field_preview = next();
        else if (a == "--field-preview-full")
            field_preview_full = true;
        else if (a == "--export-maps") // write the field maps as grid files (game/maps.h), then exit
            export_maps = next();
        else if (a == "--maps") // build the map banks from grid files in DIR (as the app does)
            maps_dir = next();
        else if (a ==
                 "--map-previews") // with --export-maps: also maps/NNN.png, each map drawn with its tileset
            map_previews = true;
        else if (a == "--maps-verify") // with --maps: the rebuilt banks equal the ROM's
            maps_verify = true;
        else if (a == "--tables-verify")
            tables_verify = true;
        else if (a == "--text-poison") // with --text-base: scramble the text's ROM bytes (as --tables-poison)
            text_poison = true;
        else if (a == "--tables-poison") // scramble the loaded tables' ROM bytes: a read that isn't routed
            tables_poison = true;        // through the store (game/tables.h) then changes the run
        else if (a == "--proportional")  // with --text: in the port's own font, as the app draws English
            proportional = true;
        else if (a == "--store") // with --native: the save store in DIR (game/save_file.h), as the app uses
            store_dir = next();  // it: every slot there, read at the start, changed slots written at the end
        else if (a == "--store-import") // with --store: a new store imports this raw SRAM image
            store_import = next();
        else if (a == "--goldendir")
            goldendir = next();
        else if (a == "--goldenevery")
            goldenevery = std::atoi(next().c_str());
        else if (a == "--trace")
            trace_path = next();
        else if (a == "--tracecount")
            tracecount = std::atol(next().c_str());
        else if (a == "--tracestart")
            tracestart = std::atoi(next().c_str());
        else if (a == "--shot")
            shot_path = next();
        else if (a == "--shotscreen") // off | lcd[:AMOUNT] | ips:SCALE (engine/screen.h; no smear)
            shot_screen = next();
        else if (a == "--replay")
            replay_path = next();
        else if (a == "--ramtrace")
            ramtrace_path = next();
        else if (a == "--irqdir")
            irqdir = next();
        else if (a == "--wav")
            wav_path = next();
        else if (a == "--fingerprint")
            fingerprint_path = next();
        else if (a == "--gb-rom") // with --native --wav: the GB music beside the WSC's (app/gb_music.h) from
            gb_rom_path = next(); // this ROM (the WSC ROM for ORIJINARU's, or a GB ROM), as the app;
                                  // --gfx-swap-at switches to it and back (0.5 s cross-fades)
        else if (a == "--porttrace")
            porttrace_path = next();
        else if (a == "--mod")
            archives.push_back(next());
        else if (a == "--difftest")
            difftest = next();
        else if (a == "--difftrace")
            difftrace = true;
        else if (a == "--fuzz")
            fuzz_runs = std::atoi(next().c_str());
        else if (a == "--invoke")
            invoke_frames = next();
        else if (a == "--states")
            states_path = next();
        else if (a == "--keyfuzz")
            kf_runs = std::atoi(next().c_str());
        else if (a == "--keyfuzz-calls")
            kf_calls = std::atoi(next().c_str());
        else if (a == "--keyfuzz-frames")
            kf_frames = std::atoi(next().c_str());
        else if (a == "--keyfuzz-seed")
            kf_seed_base = std::atol(next().c_str());
        else if (a == "--keyfuzz-keys")
            kf_keys = next();
        else if (a == "--native")
            native = next();
        else if (a == "--profile")
            profile_path = next();
        else if (a == "--irqevery")
            irqevery = std::atoi(next().c_str());
        else if (a == "--traceaddrs")
            traceaddrs = next();
        else if (a == "--features")
            features_path = next();
        else
            rom_path = a;
    }
    if (!enhanced_oracle_dir.empty() &&
        (native != "all" || frames <= 0 || !battle_layout || !dialogue_layout || !service_layout ||
         !fingerprint_path.empty() || !gb_audit_dir.empty())) {
        std::fprintf(stderr, "--enhanced-oracle requires --native all, positive --frames, --battle-ui, "
                             "--dialogue-ui and --service-ui; cannot combine with other picture audits\n");
        return 1;
    }
    std::vector<uint8_t> rom = read_file(rom_path);
    if (rom.empty() || (rom.size() & (rom.size() - 1))) {
        std::fprintf(stderr, "cannot read ROM (or size not a power of two): %s\n", rom_path.c_str());
        return 1;
    }
    {
        const sawgod::Sha1 digest = sawgod::sha1(rom.data(), rom.size());
        for (const std::string &path : archives) {
            std::string error;
            auto archive = sawgod::AssetArchive::load(path, &error);
            if (!archive || !sawgod::apply_archive(*archive, rom, digest, &error)) {
                std::fprintf(stderr, "%s\n", error.c_str());
                return 1;
            }
        }
    }
    const size_t sram_size = sawgod::game::cartridge_sram_size(rom);
    Wsc wsc(rom, sram_size);
    if (!export_resources.empty()) {
        for (const sawgod::game::ResourceRegion &r : sawgod::game::resource_regions()) {
            const std::string path = export_resources + "/" + r.file;
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
            write_file(path, sawgod::game::extract_region(r, wsc));
        }
        return 0;
    }
    if (!resources_dir.empty()) { // the regions' files decoded over the ROM's bytes, as the app loads them
        int loaded = 0, differ = 0, bad = 0;
        for (const sawgod::game::ResourceRegion &r : sawgod::game::resource_regions()) {
            const std::vector<uint8_t> file = read_file(resources_dir + "/" + r.file);
            std::vector<uint8_t> bytes;
            std::string error;
            if (file.empty() || !sawgod::game::build_region(r, file, &bytes, &error)) {
                std::fprintf(stderr, "--resources %s: %s\n", r.file,
                             file.empty() ? "missing" : error.c_str());
                ++bad;
                continue;
            }
            for (uint32_t i = 0; i < r.size; ++i) {
                uint8_t &b = wsc.rom[(r.rom + i) & (wsc.rom.size() - 1)];
                if (b != bytes[i] && (differ++ < 5))
                    std::fprintf(stderr, "--resources %s: +%X differs from the ROM\n", r.file, unsigned(i));
                b = bytes[i];
            }
            ++loaded;
        }
        std::fprintf(stderr, "resources: %d regions loaded from %s, %d bytes differ from the ROM, %d bad\n",
                     loaded, resources_dir.c_str(), differ, bad);
        if (resources_verify)
            return differ || bad ? 1 : 0;
    }
    // --rom-reads: every ROM byte read through the bus (natives' reads and DMA alike), reported at the end
    std::vector<bool> rom_read;
    if (!rom_reads_path.empty()) {
        rom_read.assign(wsc.rom.size(), false);
        wsc.on_bus = [&rom_read, &wsc](char kind, uint32_t addr) {
            if (kind != 'R')
                return;
            const int64_t off = wsc.rom_offset_of(addr);
            if (off >= 0)
                rom_read[size_t(off)] = true;
        };
    }
    if (!export_maps.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(export_maps + "/maps", ec);
        sawgod::game::LightTags lights;
        lights.seed(wsc);
        std::filesystem::create_directories(export_maps + "/materials", ec);
        for (int set : lights.tilesets()) {
            const auto json = lights.json(set);
            write_file(export_maps + "/" + lights.file(set), std::vector<uint8_t>(json.begin(), json.end()));
        }
        int n = 0;
        for (int id = 0; id < sawgod::game::kMapCount; ++id) {
            if (!sawgod::game::map_has_data(id, wsc, nullptr))
                continue;
            const sawgod::game::MapData md = sawgod::game::extract_map(id, wsc);
            char num[8];
            std::snprintf(num, sizeof num, "%03d", id);
            const std::string base = export_maps + "/maps/" + num;
            const auto names = sawgod::game::map_file_names(id);
            const std::string made[4] = {
                sawgod::game::map_cells_csv(md), sawgod::game::map_attributes_csv(md),
                sawgod::game::map_metatiles_csv(md), sawgod::game::map_types_csv(md)};
            for (int f = 0; f < 4; ++f)
                write_file(export_maps + "/" + names[size_t(f)],
                           std::vector<uint8_t>(made[f].begin(), made[f].end()));
            if (map_previews)
                write_file(base + ".png", sawgod::game::render_map_png(id, md, wsc, nullptr));
            ++n;
        }
        std::fprintf(stderr, "maps: %d written to %s/maps\n", n, export_maps.c_str());
        return 0;
    }
    if (!maps_dir.empty()) { // the map banks rebuilt from grid files, as the app does
        const std::vector<uint8_t> before(wsc.rom.begin() + 0x200000, wsc.rom.begin() + 0x270000);
        std::vector<sawgod::game::MapData> loaded(sawgod::game::kMapCount);
        std::vector<const sawgod::game::MapData *> use(sawgod::game::kMapCount, nullptr);
        int bad = 0;
        for (int id = 0; id < sawgod::game::kMapCount; ++id) {
            if (!sawgod::game::map_has_data(id, wsc, nullptr))
                continue;
            char num[8];
            std::snprintf(num, sizeof num, "%03d", id);
            const std::string base = maps_dir + "/maps/" + num;
            const auto names = sawgod::game::map_file_names(id);
            std::string f[4];
            for (int k = 0; k < 4; ++k) {
                const auto v = read_file(maps_dir + "/" + names[size_t(k)]);
                f[k].assign(v.begin(), v.end());
            }
            std::string error;
            if (!sawgod::game::map_from_files(f[0], f[1], f[2], f[3], &loaded[size_t(id)], &error)) {
                std::fprintf(stderr, "--maps %s: %s\n", base.c_str(), error.c_str());
                ++bad;
                continue;
            }
            use[size_t(id)] = &loaded[size_t(id)];
        }
        std::vector<std::string> errors;
        const int moved =
            sawgod::game::build_map_banks(wsc, sawgod::game::Session::of(wsc).tables(), use, &errors);
        for (const std::string &e : errors)
            std::fprintf(stderr, "--maps: %s\n", e.c_str());
        size_t differ = 0;
        for (size_t i = 0; i < before.size(); ++i)
            differ += before[i] != wsc.rom[0x200000 + i];
        std::fprintf(stderr, "maps: built from %s/maps, %d moved, %zu bytes differ from the ROM, %d bad\n",
                     maps_dir.c_str(), moved, differ, bad);
        if (maps_verify)
            return differ || bad || moved || !errors.empty() ? 1 : 0;
    }
    if (maps_check) { // every map's two LZ layers (load_field_map: data0 budget 0xC80, data3 budget 0x800)
        int layers = 0, bad = 0, before = 0, exact = 0;
        size_t packed_rom = 0, packed_ours = 0;
        sawgod::game::Machine m(wsc);
        m.out8(0x60, 0x80); // color mode: the RAM above 0x4000 (the ring at D6FE) exists
        for (unsigned id = 0; id < 211; ++id) {
            const uint32_t rec = 0x35A830 + id * 18;
            const uint8_t bank = wsc.rom[rec + 1];
            if (!bank)
                continue;
            for (const auto &[field, budget, dest] :
                 {std::tuple{2u, 0xC80u, 0x0000u}, std::tuple{8u, 0x800u, 0x2100u}}) {
                const uint16_t off = uint16_t(wsc.rom[rec + field] | wsc.rom[rec + field + 1] << 8);
                const uint32_t base = uint32_t(bank & 0x3F) << 16;
                std::vector<uint8_t> src(budget);
                for (unsigned i = 0; i < budget; ++i)
                    src[i] = wsc.rom[base + uint16_t(off + i)];
                // the game's own: RAM ring zeroed, output to SRAM 1000:dest, input from the ROM0 window
                for (unsigned i = 0; i < 0x1000; ++i)
                    m.wr8(0, uint16_t(0xD6FE + i), 0);
                for (unsigned i = 0; i < 0x2000; ++i)
                    m.wr8(0x1000, uint16_t(dest + i), 0xEE);
                m.out8(0xC2, bank);
                m.ss() = 0, m.ds() = 0x2000, m.es() = 0x1000, m.si() = off, m.di() = uint16_t(dest),
                m.cx() = uint16_t(budget);
                sawgod::game::decompress_field_map_core(m);
                const auto ours = sawgod::game::lz_unpack(src.data(), budget, std::vector<uint8_t>(4096, 0));
                bool same = true;
                for (size_t i = 0; i < ours.bytes.size(); ++i)
                    same &= m.rd8(0x1000, uint16_t(dest + i)) == ours.bytes[i];
                same &= m.rd8(0x1000, uint16_t(dest + ours.bytes.size())) == 0xEE;
                const auto repacked = sawgod::game::lz_pack(ours.bytes);
                const auto back =
                    sawgod::game::lz_unpack(repacked.data(), repacked.size(), std::vector<uint8_t>(4096, 0));
                same &= back.bytes == ours.bytes;
                exact +=
                    std::equal(repacked.begin(), repacked.end(), src.begin()) && repacked.size() <= budget;
                before += ours.reads_before;
                ++layers;
                packed_rom += ours.consumed, packed_ours += repacked.size();
                if (!same && bad++ < 5)
                    std::fprintf(stderr, "map %u layer @%04X: differs\n", id, off);
                if (layers <= 3 || ours.reads_before)
                    std::fprintf(stderr, "map %u layer @%02X:%04X: %zu bytes from %zu packed%s\n", id,
                                 bank & 0x3F, off, ours.bytes.size(), ours.consumed,
                                 ours.reads_before ? " (reads the ring's prior contents)" : "");
            }
        }
        std::printf("maps: %d layers, %d differ, %d read the ring's prior contents, %d repack to the ROM's "
                    "exact bytes "
                    "(packed %zu bytes, ours %zu)\n",
                    layers, bad, before, exact, packed_rom, packed_ours);
        return bad || exact != layers ? 1 : 0;
    }
    sawgod::game::GfxSwap gfx_swap;
    bool gb_shown = false; // --gfx-swap-at: the GB picture written (golden PNGs) instead of the WSC's
    int preview_map = -1;
    if (!field_preview.empty())
        std::sscanf(field_preview.c_str(), "%d", &preview_map);
    for (int id = 0; id < sawgod::game::kMapCount && !gfx_swap_dir.empty(); ++id) {
        // A halted display fixture only uses this map; keep the normal resource loader,
        // without parsing every other map's cell table for every gallery screenshot.
        if (preview_map >= 0 && id != preview_map)
            continue;
        std::string error;
        gfx_swap.load_field_map(id, gfx_swap_dir, &error);
        if (!error.empty())
            std::fprintf(stderr, "--gfx-swap map %03d: %s\n", id, error.c_str());
    }
    if (!field_preview.empty()) {
        int id = 0, x = 0, y = 0;
        if (std::sscanf(field_preview.c_str(), "%d,%d,%d", &id, &x, &y) != 3 ||
            !sawgod::game::map_has_data(id, wsc, nullptr) || shot_path.empty() ||
            !sawgod::tools::prepare_field_preview(wsc, id, sawgod::game::extract_map(id, wsc), x, y)) {
            std::fprintf(stderr, "--field-preview needs ID (1..210),X (0..50),Y (0..55), and --shot\n");
            return 1;
        }
        if (field_preview_full) {
            const auto map = sawgod::game::extract_map(id, wsc);
            std::vector<uint32_t> canvas(1024 * 1024);
            for (int cy : {0, 9, 18, 27, 36, 45, 54, 55})
                for (int cx : {0, 14, 28, 42, 50}) {
                    gfx_swap.set_enabled(wsc, false);
                    sawgod::tools::prepare_field_preview(wsc, id, map, cx, cy);
                    if (gfx_swap.has_data())
                        gfx_swap.set_enabled(wsc, true);
                    const auto before = wsc.ram;
                    const auto sram_before = wsc.sram;
                    wsc.run_frame();
                    if (before != wsc.ram || sram_before != wsc.sram) {
                        std::fprintf(stderr, "full field preview changed machine RAM\n");
                        return 1;
                    }
                    const auto pixels = wsc.framebuffer_rgb(Wsc::ColorMode::Lcd, gfx_swap.has_data());
                    for (int py = 0; py < Wsc::kHeight && cy * 16 + py < 1024; ++py)
                        for (int px = 0; px < Wsc::kWidth && cx * 16 + px < 1024; ++px)
                            canvas[size_t((cy * 16 + py) * 1024 + cx * 16 + px)] =
                                pixels[size_t(py * Wsc::kWidth + px)];
                }
            write_file(shot_path, encode_png(canvas.data(), 1024, 1024));
            return 0;
        }
        if (gfx_swap.has_data())
            gfx_swap.set_enabled(wsc, true);
        const auto before = wsc.ram;
        const auto sram_before = wsc.sram;
        wsc.run_frame();
        if (before != wsc.ram || sram_before != wsc.sram) {
            std::fprintf(stderr, "field preview changed machine RAM\n");
            return 1;
        }
        const auto pixels = wsc.framebuffer_rgb(Wsc::ColorMode::Lcd, gfx_swap.has_data());
        write_file(shot_path, encode_png(pixels.data(), Wsc::kWidth, Wsc::kHeight));
        return 0;
    }
    if (!gfx_swap_dir.empty()) {
        const auto png = read_file(gfx_swap_dir + "/gb/sprites.png");
        const auto csv = read_file(gfx_swap_dir + "/gb/sprite_types.csv");
        std::string error;
        if (!png.empty() && !csv.empty() &&
            !gfx_swap.add_sprites(png, std::string(csv.begin(), csv.end()), &error))
            std::fprintf(stderr, "--gfx-swap %s/gb/sprites: %s\n", gfx_swap_dir.c_str(), error.c_str());
        gfx_swap.import_field_sprites(wsc);
        const auto corpse = read_file(gfx_swap_dir + "/gb/corpse.png");
        if (!corpse.empty() && !gfx_swap.add_corpse(corpse, &error))
            std::fprintf(stderr, "GB corpse: %s\n", error.c_str());
        const auto hand = read_file(gfx_swap_dir + "/gb/hand.png");
        if (!hand.empty() && !gfx_swap.add_hand(hand, &error))
            std::fprintf(stderr, "GB hand: %s\n", error.c_str());
        const auto objects = read_file(gfx_swap_dir + "/gb/objects.png"),
                   sets = read_file(gfx_swap_dir + "/gb/object_sets.csv");
        if (!objects.empty() && !sets.empty() &&
            !gfx_swap.add_objects(objects, std::string(sets.begin(), sets.end()), &error))
            std::fprintf(stderr, "GB objects: %s\n", error.c_str());
        const auto font = read_file(gfx_swap_dir + "/gb/font.png");
        if (!font.empty() && !gfx_swap.add_font(font, &error))
            std::fprintf(stderr, "--gfx-swap %s/gb/font.png: %s\n", gfx_swap_dir.c_str(), error.c_str());
        const auto monsters = read_file(gfx_swap_dir + "/gb/monsters.png");
        const auto pics = read_file(gfx_swap_dir + "/gb/monster_pics.csv"),
                   types = read_file(gfx_swap_dir + "/gb/monster_types.csv");
        if (!monsters.empty() && !pics.empty() && !types.empty() &&
            !gfx_swap.add_monsters(monsters, std::string(pics.begin(), pics.end()),
                                   std::string(types.begin(), types.end()), &error))
            std::fprintf(stderr, "--gfx-swap %s/gb/monsters: %s\n", gfx_swap_dir.c_str(), error.c_str());
        if (!gfx_swap.load_battle_effects(
                [&](const std::string &name) { return read_file(gfx_swap_dir + "/" + name); }, &error))
            std::fprintf(stderr, "GB battle effects: %s\n", error.c_str());
        const auto title = read_file(gfx_swap_dir + "/gb/title.png");
        if (!title.empty() && !gfx_swap.add_title(title, &error))
            std::fprintf(stderr, "--gfx-swap %s/gb/title.png: %s\n", gfx_swap_dir.c_str(), error.c_str());
    }
    std::map<std::string, long> writers; // --watch-writes: writer ROM offset (and callers) -> writes
    if (!watch_writes.empty()) {
        const uint32_t from = uint32_t(std::strtoul(watch_writes.c_str(), nullptr, 16)),
                       to = uint32_t(std::strtoul(watch_writes.substr(watch_writes.find('-') + 1).c_str(),
                                                  nullptr, 16));
        auto prev = wsc.on_bus;
        wsc.on_bus = [&wsc, &writers, from, to, prev](char kind, uint32_t addr) {
            if (prev)
                prev(kind, addr);
            static int64_t last_rom = -1; // SAWGOD_WATCH_SOURCE: the last ROM byte read before each write
            if (kind == 'R' && std::getenv("SAWGOD_WATCH_SOURCE"))
                last_rom = wsc.rom_offset_of(addr);
            if (kind != 'W' || addr < from || addr >= to)
                return;
            if (std::getenv("SAWGOD_WATCH_SOURCE")) {
                char k[48];
                std::snprintf(k, sizeof k, "%05X<-%06llX", addr, (unsigned long long)last_rom);
                ++writers[k];
                return;
            }
            char key[64];
            const uint32_t sp = (uint32_t(wsc.cpu.s.seg[SS]) << 4) + wsc.cpu.s.r[SP];
            const auto word = [&](uint32_t a) {
                return unsigned(wsc.ram[a & 0xFFFF] | wsc.ram[(a + 1) & 0xFFFF] << 8);
            };
            if (std::getenv(
                    "SAWGOD_WATCH_CALLERS")) // with the three words on top of the stack (return addresses)
                std::snprintf(key, sizeof key, "%06llX %04X %04X %04X",
                              (unsigned long long)wsc.rom_offset_of(wsc.cpu.pc()), word(sp), word(sp + 2),
                              word(sp + 4));
            else
                std::snprintf(key, sizeof key, "%06llX", (unsigned long long)wsc.rom_offset_of(wsc.cpu.pc()));
            ++writers[key];
        };
    }
    if (!text_budgets.empty()) {
        const std::string base = sawgod::game::export_text(wsc);
        std::string sample;
        if (!text_budget_sample.empty()) {
            const auto bytes = read_file(text_budget_sample);
            sample.assign(bytes.begin(), bytes.end());
            std::printf("English sample median: %.12g px/character\n",
                        sawgod::game::fit_sample_median(sample));
        }
        const std::string out =
            sawgod::game::fit_budgets_report(base, sample, text_surfaces, text_budget_japanese);
        write_file(text_budgets, std::vector<uint8_t>(out.begin(), out.end()));
        return 0;
    }
    if (!text_fit.empty()) {
        std::string base;
        if (text_base.empty()) {
            base = sawgod::game::export_text(wsc);
        } else {
            const std::vector<uint8_t> b = read_file(text_base);
            base.assign(b.begin(), b.end());
        }
        const std::vector<uint8_t> lang = read_file(text_fit);
        std::string context;
        if (!text_fit_context.empty()) {
            const auto bytes = read_file(text_fit_context);
            context.assign(bytes.begin(), bytes.end());
        }
        const auto issues = sawgod::game::check_fit(base, std::string(lang.begin(), lang.end()), context);
        int errors = 0;
        for (const auto &i : issues)
            errors += i.error;
        if (!text_fit_report.empty()) {
            const std::string csv = sawgod::game::fit_report(issues);
            write_file(text_fit_report, std::vector<uint8_t>(csv.begin(), csv.end()));
        }
        for (const auto &i : issues)
            if (i.error)
                std::printf(
                    "%s: %s%s\n", i.key.c_str(), i.problem.c_str(),
                    i.budget
                        ? (" (" + std::to_string(i.width) + " > " + std::to_string(i.budget) + " px)").c_str()
                        : "");
        std::printf("text-fit: %d errors, %zu warnings\n", errors, issues.size() - size_t(errors));
        return errors ? 1 : 0;
    }
    if (!export_text.empty()) {
        const std::string csv = sawgod::game::export_text(wsc);
        write_file(export_text, std::vector<uint8_t>(csv.begin(), csv.end()));
        return 0;
    }
    if (!export_tokens.empty()) {
        // The sheet's japanese column for text keys (their token form) and battle fragments; empty: the key
        // leaves the sheet (only padding, or a fragment every message using it has a template for)
        std::set<std::string> in_messages;
        for (const sawgod::game::TextMessage &msg : sawgod::game::kTextMessages)
            for (std::string_view p = msg.parts; !p.empty();) {
                const size_t plus = p.find('+');
                in_messages.emplace(p.substr(0, plus));
                p = plus == std::string_view::npos ? std::string_view() : p.substr(plus + 1);
            }
        std::string out = "key,tokens\n";
        for (const auto &row : sawgod::game::parse_csv(sawgod::game::export_text(wsc)))
            if (row.size() >= 4 && row[3] == "text")
                out += sawgod::game::csv_cell(row[0]) + "," +
                       sawgod::game::csv_cell(sawgod::game::text_tokens(row[0], row[1]).text) + "\n";
            else if (row.size() >= 4 && row[3] == "glyphs")
                out += sawgod::game::csv_cell(row[0]) + "," +
                       sawgod::game::csv_cell(in_messages.count(row[0]) ? std::string() : row[1]) + "\n";
        write_file(export_tokens, std::vector<uint8_t>(out.begin(), out.end()));
        return 0;
    }
    if (!export_tables.empty()) { // --export-tables DIR: the first-run extraction, then exit
        std::error_code ec;
        std::filesystem::create_directories(export_tables, ec);
        for (const std::string &name : sawgod::game::table_names()) {
            const std::string csv = sawgod::game::export_csv(name, wsc);
            write_file(export_tables + "/" + name + ".csv", std::vector<uint8_t>(csv.begin(), csv.end()));
        }
        return 0;
    }
    for (const std::string &dir : tables_dirs)
        for (const std::string &name : sawgod::game::table_names()) {
            const std::string path = dir + "/" + name + ".csv";
            if (FILE *f = std::fopen(path.c_str(), "rb"))
                std::fclose(f);
            else
                continue;
            const std::vector<uint8_t> data = read_file(path);
            std::string error;
            std::vector<std::string> warnings;
            int changed = 0;
            if (!sawgod::game::Session::of(wsc).tables().load_csv(name, std::string(data.begin(), data.end()),
                                                                  wsc, &error, &warnings, &changed)) {
                std::fprintf(stderr, "--tables %s: %s\n", path.c_str(), error.c_str());
                return 1;
            }
            for (const std::string &w : warnings)
                std::fprintf(stderr, "--tables %s: %s\n", path.c_str(), w.c_str());
            std::fprintf(stderr, "tables: %d rows from %s\n", changed, path.c_str());
        }
    if (tables_verify) {
        std::vector<std::string> diffs;
        const int rows = sawgod::game::Session::of(wsc).tables().compare_rom(wsc, &diffs);
        for (const std::string &d : diffs)
            std::printf("%s\n", d.c_str());
        std::printf("tables: %d rows loaded, %zu differ from the ROM\n", rows, diffs.size());
        return diffs.empty() ? 0 : 1;
    }
    // --tables-poison / --text-poison: scramble the ROM's bytes under the loaded tables / text and report
    // each place a native still reads them (machine.h read_watch), once per address. ROM offsets; the game's
    // reads are mapped to ROM offsets as the bus maps them (banked windows included).
    static std::vector<std::pair<uint32_t, uint32_t>> poisoned; // ROM [from, to)
    static Wsc *poison_wsc = &wsc;
    static std::vector<bool> scrambled;
    auto poison = [&](uint32_t from, uint32_t to) {
        scrambled.resize(wsc.rom.size());
        for (uint32_t a = from; a < to && a < wsc.rom.size(); ++a)
            if (!scrambled[a]) // once (strings share tails)
                scrambled[a] = true, wsc.rom[a] = uint8_t(wsc.rom[a] ^ 0xA5 ^ (a * 37));
        poisoned.emplace_back(from, to);
        sawgod::game::Machine::read_watch = [](uint16_t seg, uint16_t off) {
            static std::set<uint32_t> seen;
            const int64_t a = poison_wsc->rom_offset_of(((uint32_t(seg) << 4) + off) & 0xFFFFF);
            for (const auto &[from, to] : poisoned)
                if (a >= from && a < to && seen.insert(a).second)
                    std::fprintf(stderr, "poison: direct read of %04X:%04X\n", seg, off),
                        std::getenv("SAWGOD_POISON_ABORT") ? std::abort() : void();
        };
    };
    if (tables_poison) {
        for (size_t t = 0; t < size_t(sawgod::game::Table::count); ++t) {
            const auto table = sawgod::game::Table(t);
            if (!sawgod::game::Session::of(wsc).tables().overridden(table))
                continue;
            const sawgod::game::TableLayout &l = sawgod::game::table_layout(table);
            poison(l.rom, l.rom + uint32_t(l.stride) * l.rom_rows);
            if (l.per)
                poison(l.rom + uint16_t(l.data - l.offset), l.rom + uint16_t(l.data - l.offset) + l.region);
        }
        std::fprintf(stderr, "tables: ROM bytes scrambled under the loaded tables\n");
    }
    if (!sram_path.empty()) {
        std::vector<uint8_t> s = read_file(sram_path);
        std::copy_n(s.begin(), std::min(s.size(), wsc.sram.size()), wsc.sram.begin());
    }
    if (!slot_toml
             .empty()) { // primary copy of slot 1: payload, additive checksum, "valid save" bytes; last = 0
        const std::vector<uint8_t> text = read_file(slot_toml);
        std::vector<uint8_t> payload;
        std::string error;
        if (!sawgod::game::save_from_text(
                std::string_view(reinterpret_cast<const char *>(text.data()), text.size()), &payload,
                &error)) {
            std::fprintf(stderr, "--slot %s: %s\n", slot_toml.c_str(), error.c_str());
            return 1;
        }
        constexpr size_t kFirst = 0x3301, kLast = 0x3300;
        payload[0x435] = 0x1B;
        payload[0x436] = 0xE4;
        uint16_t sum = 0;
        for (uint8_t b : payload)
            sum = uint16_t(sum + b);
        std::copy(payload.begin(), payload.end(), wsc.sram.begin() + long(kFirst));
        wsc.sram[kFirst + payload.size()] = uint8_t(sum);
        wsc.sram[kFirst + payload.size() + 1] = uint8_t(sum >> 8);
        wsc.sram[kLast] = 0;
    }

    if (!replay_path.empty()) {
        // Timing research: re-run every instruction of a captured instruction trace from the
        // oracle's registers and log the bus accesses it makes: "cs:ip accesses..." (F fetch, R/W memory,
        // I/O ports), one line per trace line. Memory contents are not synced, only addresses matter.
        FILE *in = std::fopen(replay_path.c_str(), "r");
        FILE *out = std::fopen(features_path.c_str(), "w");
        if (!in || !out)
            return 1;
        std::string acc;
        wsc.on_bus = [&](char kind, uint32_t addr) {
            char b[16];
            std::snprintf(b, sizeof b, " %c%x", kind, addr);
            acc += b;
        };
        char line[512];
        uint32_t predicted = ~0u;
        while (std::fgets(line, sizeof line, in)) {
            if (std::strncmp(line, "frame", 5) == 0)
                continue;
            unsigned long long cyc;
            unsigned cs, ip, r[8], ds, es, ss, fl, sl;
            if (std::sscanf(line, "%llu %x:%x %x %x %x %x %x %x %x %x %x %x %x %x %u", &cyc, &cs, &ip, &r[0],
                            &r[3], &r[1], &r[2], &r[4], &r[5], &r[6], &r[7], &ds, &es, &ss, &fl, &sl) != 16)
                continue;
            CpuState &st = wsc.cpu.s;
            if (((cs << 4) + ip) != predicted)
                wsc.cpu.clear_prefixes();
            for (int k = 0; k < 8; ++k)
                st.r[k] = uint16_t(r[k]);
            st.seg[CS] = uint16_t(cs);
            st.ip = uint16_t(ip);
            st.seg[DS] = uint16_t(ds);
            st.seg[ES] = uint16_t(es);
            st.seg[SS] = uint16_t(ss);
            st.flags = uint16_t(fl);
            st.halted = false;
            acc.clear();
            int base = wsc.cpu.step();
            predicted = (uint32_t(st.seg[CS]) << 4) + st.ip;
            std::fprintf(out, "%04x:%04x %d %05x%s\n", cs, ip, base, predicted & 0xFFFFF, acc.c_str());
        }
        std::fclose(in);
        std::fclose(out);
        return 0;
    }

    std::vector<std::pair<int, uint16_t>> changes;
    if (!inputs_path.empty()) {
        FILE *f = std::fopen(inputs_path.c_str(), "r");
        if (!f) {
            std::fprintf(stderr, "cannot read %s\n", inputs_path.c_str());
            return 1;
        }
        char line[256];
        while (std::fgets(line, sizeof line, f)) {
            char *end;
            long fr = std::strtol(line, &end, 10);
            if (end == line)
                continue;
            std::string rest = end;
            while (!rest.empty() && (rest.back() == '\n' || rest.back() == '\r'))
                rest.pop_back();
            changes.emplace_back(int(fr), parse_buttons(rest));
        }
        std::fclose(f);
        std::stable_sort(changes.begin(), changes.end(), [](auto &x, auto &y) { return x.first < y.first; });
    }

    FILE *trace = nullptr;
    long traced = 0;
    if (!trace_path.empty()) {
        trace = std::fopen(trace_path.c_str(), "w");
        wsc.on_instruction = [&](const Wsc &w) {
            if (traced >= tracecount)
                return;
            ++traced;
            const CpuState &s = w.cpu.s;
            std::fprintf(
                trace, "%llu %04x:%04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %03x %d\n",
                (unsigned long long)w.cpu.cycles, s.seg[CS], s.ip, s.r[AX], s.r[BX], s.r[CX], s.r[DX],
                s.r[SP], s.r[BP], s.r[SI], s.r[DI], s.seg[DS], s.seg[ES], s.seg[SS], s.flags, w.scanline);
        };
    }

    // --ramtrace: per frame, "frame b0 b1 ..." for the --traceaddrs work-RAM bytes (as harness.lua --trace)
    FILE *ramtrace = ramtrace_path.empty() ? nullptr : std::fopen(ramtrace_path.c_str(), "w");
    std::vector<uint32_t> addrs;
    for (size_t i = 0; i < traceaddrs.size();) {
        size_t j = traceaddrs.find(',', i);
        if (j == std::string::npos)
            j = traceaddrs.size();
        addrs.push_back(uint32_t(std::strtoul(traceaddrs.substr(i, j - i).c_str(), nullptr, 0)));
        i = j + 1;
    }

    // --irqdir: as harness.lua: work RAM at the VBlank interrupt after every --irqevery-th frame
    int irq_armed = -1;
    if (!irqdir.empty()) {
        wsc.on_irq = [&](const Wsc &w) {
            if (irq_armed < 0 || w.scanline != 144)
                return;
            std::vector<uint8_t> r(w.ram.begin(), w.ram.end());
            for (int i = 0; i < 6; ++i)
                r[uint16_t(w.cpu.s.r[SP] + i)] = 0;
            char path[1024];
            std::snprintf(path, sizeof path, "%s/%06d.ram", irqdir.c_str(), irq_armed);
            write_file(path, r);
            irq_armed = -1;
        };
    }

    sawgod::game::Cheats cheats; // --cheats (game/cheats.h)
    std::unique_ptr<sawgod::tools::ModifierAudit> modifier_audit;
    if (!modifier_audit_path.empty()) {
        if (native != "all" || !cheats_list.empty()) {
            std::fprintf(stderr,
                         "--modifier-audit requires --native all; use --modifier-config for settings\n");
            return 1;
        }
        modifier_audit = std::make_unique<sawgod::tools::ModifierAudit>(wsc);
        modifier_audit->before_cheats();
    }
    sawgod::game::Autosave autosave;      // --autosave
    sawgod::game::GameConfig game_config; // --game-config
    sawgod::game::BattleUi battle_ui;
    sawgod::game::InventoryUi inventory_ui;
    sawgod::game::ItemDialogUi item_dialog_ui;
    sawgod::game::PartyUi party_ui;
    sawgod::game::CreationUi creation_ui;
    sawgod::game::BestiaryUi bestiary_ui;
    sawgod::game::DialogueUi dialogue_ui;
    sawgod::game::ServiceUi service_ui;
    sawgod::game::SaveUi save_ui;
    sawgod::app::GbMusic gb_music;
    std::unique_ptr<sawgod::tools::EnhancedOracle> enhanced_oracle;
    sawgod::game::HostPicture audit_picture;
    sawgod::game::HostPicture fingerprint_picture;
    std::vector<sawgod::game::hooks::Subscription> fingerprint_hooks;
    bool reward_popup_pending = false;
    std::unique_ptr<sawgod::tools::GbAudit> gb_audit;
    std::unique_ptr<sawgod::tools::GbAudit> effect_scene_audit;
    int audit_animation = 0;
    sawgod::tools::NativePictureFade audit_fade;
    uint64_t audit_fade_generation = 0;
    const auto sync_audit_fade = [&] {
        const auto generation = sawgod::game::Session::of(wsc).generation();
        if (audit_fade_generation != generation) {
            audit_fade = {};
            audit_fade_generation = generation;
        }
    };
    std::array<uint8_t, Wsc::kWidth * Wsc::kHeight> audit_ui_coverage{};
    std::unique_ptr<sawgod::tools::GbEffectsCapture> effects_capture;
    if (!gb_effects_dir.empty())
        effects_capture = std::make_unique<sawgod::tools::GbEffectsCapture>(gb_effects_dir);
    std::vector<sawgod::game::hooks::Subscription> audit_hooks;
    if (!native.empty() || (!difftest.empty() && !text_path.empty())) {
        // Differential language trials load the same resources as the app without enabling native
        // playback: the movie and the reference trial must still execute the original ROM.
        if (!native.empty())
            sawgod::game::install(wsc, native == "all" ? nullptr : native.c_str());
        if (!gb_rom_path.empty()) {
            if (!gb_music.load(read_file(gb_rom_path))) {
                std::fprintf(stderr, "--gb-rom %s: not Makai Toushi SaGa for the Game Boy\n",
                             gb_rom_path.c_str());
                return 1;
            }
            gb_music.attach(sawgod::game::Session::of(wsc));
        }
        if (save_layout)
            save_ui.install(wsc);
        if (service_layout)
            service_ui.install(wsc);
        if (dialogue_layout)
            dialogue_ui.install(wsc);
        if (party_layout)
            party_ui.install(wsc);
        if (inventory_layout)
            inventory_ui.install(wsc);
        if (item_dialog_layout)
            item_dialog_ui.install(wsc, &inventory_ui);
        if (player_names)
            sawgod::game::Session::of(wsc).player_names().enabled = true;
        if (creation_layout)
            creation_ui.install(wsc);
        if (bestiary_layout)
            bestiary_ui.install(wsc);
        if (battle_layout) {
            battle_ui.install(wsc);
            battle_ui.set_transparency(battle_transparency);
        }
        if (!enhanced_oracle_dir.empty()) {
            enhanced_oracle =
                std::make_unique<sawgod::tools::EnhancedOracle>(enhanced_oracle_dir, oracle_terrain_veil);
            enhanced_oracle->install(wsc, {&battle_ui, &inventory_ui, &item_dialog_ui, &party_ui,
                                           &creation_ui, &bestiary_ui, &dialogue_ui, &service_ui, &save_ui});
            battle_ui.set_enhanced_weight(1);
        }
        if (!fingerprint_path.empty()) {
            fingerprint_picture.install(wsc,
                                        {&battle_ui, &inventory_ui, &item_dialog_ui, &party_ui, &creation_ui,
                                         &bestiary_ui, &dialogue_ui, &service_ui, &save_ui});
            if (gfx_swap.has_data())
                gfx_swap.set_enabled(wsc, true);
            // Positive gold is credited immediately before its StatusPopup. Keep evidence that
            // the corpus actually reached the reward message, even with redesigned input/layouts.
            auto &hooks = sawgod::game::Session::of(wsc).hooks();
            fingerprint_hooks.push_back(hooks.subscribe(sawgod::game::hooks::Value::BattleGold,
                                                        [&](auto &, int32_t value, const auto &) {
                                                            reward_popup_pending |= value > 0;
                                                            return value;
                                                        }));
            fingerprint_hooks.push_back(
                hooks.subscribe(sawgod::game::hooks::Event::StatusPopup, [&](auto &, const auto &) {
                    if (reward_popup_pending) {
                        std::fprintf(stderr, "fingerprint: gold reward popup frame %llu\n",
                                     (unsigned long long)wsc.frame_count + 1);
                        reward_popup_pending = false;
                    }
                }));
        }
        if (!gb_audit_dir.empty()) {
            if (!gfx_swap.has_data()) {
                std::fprintf(stderr, "--gb-audit requires complete --gfx-swap resources\n");
                return 1;
            }
            audit_picture.install(wsc, {&battle_ui, &inventory_ui, &item_dialog_ui, &party_ui, &creation_ui,
                                        &bestiary_ui, &dialogue_ui, &service_ui, &save_ui});
            wsc.ui_coverage = &audit_ui_coverage;
            gb_audit = std::make_unique<sawgod::tools::GbAudit>(gb_audit_dir, gb_audit_every);
            effect_scene_audit =
                std::make_unique<sawgod::tools::GbAudit>(gb_audit_dir + "/effect-scene", gb_audit_every);
            gfx_swap.observe_sprite = [&](int map, int set, int pose, bool replaced) {
                gb_audit->sprite(map, set, pose, replaced);
            };
            auto &hooks = sawgod::game::Session::of(wsc).hooks();
            audit_hooks.push_back(
                hooks.subscribe(sawgod::game::hooks::Event::PresentationPicture, [&](auto &m, const auto &a) {
                    sync_audit_fade();
                    audit_fade.observe(m.wsc(), a.a);
                }));
            audit_hooks.push_back(
                hooks.subscribe(sawgod::game::hooks::Event::BattleAnimation, [&](auto &m, const auto &a) {
                    audit_animation = a.a;
                    if (effects_capture)
                        effects_capture->begin(m.wsc().frame_count, a);
                }));
            audit_hooks.push_back(
                hooks.subscribe(sawgod::game::hooks::Event::BattleAnimationEnd, [&](auto &m, const auto &) {
                    audit_animation = 0;
                    if (effects_capture)
                        effects_capture->finish(m.wsc().frame_count);
                }));
            gfx_swap.set_enabled(wsc, true); // install art hooks before the first game frame, as the app
        }
        sawgod::game::SaveSlots &slots = sawgod::game::Session::of(wsc).save_slots();
        slots.request_count(save_slots);
        if (hide_orij)
            hide_orij_sub = sawgod::game::hide_orijinaru(sawgod::game::Session::of(wsc));
        if (autosave_on)
            autosave.set(sawgod::game::Session::of(wsc), true);
        if (game_config_on)
            game_config.set(sawgod::game::Session::of(wsc), true);
        if (!cheats_list.empty() || !teleport.empty()) {
            const auto number = [&](const std::string &key, int def) { // e.g. gold=2
                const size_t at = cheats_list.find(key);
                return at == std::string::npos ? def : std::atoi(cheats_list.c_str() + at + key.size());
            };
            const auto stat_growth_pct = [&] { return number("stat_growth=", 100); };
            const auto has = [&](const char *n) {
                return ("," + cheats_list + ",").find(std::string(",") + n + ",") != std::string::npos;
            };
            cheats.set(sawgod::game::Session::of(wsc),
                       {has("no_damage"), has("one_hit_kills"), has("unbreakable_gear"),
                        has("unlimited_hearts"), stat_growth_pct(), number("encounters=", 0),
                        number("gold=", 100), has("skip_intro")});
        }
        if (!modifier_config.empty() || modifier_audit) {
            sawgod::Config config;
            sawgod::Settings settings;
            settings.register_all(config);
            std::vector<std::string> warnings;
            if (!modifier_config.empty() && (!config.load(modifier_config, &warnings) || !warnings.empty())) {
                std::fprintf(stderr, "invalid --modifier-config %s\n", modifier_config.c_str());
                return 1;
            }
            const auto flags = sawgod::app::cheat_flags(settings);
            cheats.set(sawgod::game::Session::of(wsc), flags);
            if (modifier_audit && !modifier_audit->start(flags, modifier_route_in, modifier_route_out)) {
                std::fprintf(stderr, "cannot open modifier route\n");
                return 1;
            }
        }
        for (const bool base : {true, false}) {
            const std::string &text_file = base ? text_base : text_path;
            if (text_file.empty())
                continue;
            const std::vector<uint8_t> data = read_file(text_file);
            std::string error;
            int changed = 0;
            std::vector<std::string> warnings;
            if (!sawgod::game::Session::of(wsc).text().load(std::string(data.begin(), data.end()),
                                                            sawgod::game::Charset::system(), &error,
                                                            &warnings, &changed, !base && proportional,
                                                            /*layer=*/!base)) {
                std::fprintf(stderr, "--text %s: %s\n", text_file.c_str(), error.c_str());
                return 1;
            }
            for (const std::string &w : warnings)
                std::fprintf(stderr, "--text %s: %s\n", text_file.c_str(), w.c_str());
            std::fprintf(stderr, "text: %d keys from %s\n", changed, text_file.c_str());
        }
        sawgod::game::Session::of(wsc).text().set_button_icons(text_icons);
        if (gb_audit && proportional)
            wsc.set_glyph_overrides(
                sawgod::game::m5x7_number_overrides(wsc, [&](int code) { return gfx_swap.gb_glyph(code); }));
        if (text_poison) { // every string the text resource holds (sawgod::game::export_text's rows)
            const char *only = std::getenv("SAWGOD_POISON_ONLY"); // debug: one key (bisecting)
            for (const sawgod::game::TextKeyInfo &k : sawgod::game::kTextKeys) {
                if (only && std::strcmp(only, k.name) != 0)
                    continue;
                uint32_t end = k.string;
                if (k.length)
                    end += k.length;
                else if (k.glyphs)
                    while (!(wsc.rom[end] == 0xFF && wsc.rom[end + 1] == 0xFF))
                        end += 2;
                else
                    while (wsc.rom[end] != 0xFF)
                        ++end;
                poison(k.string, end); // (the terminator stays: a shared tail's other key still ends)
            }
            if (!only) {
                poison(0x394BE0, 0x394BE0 + 8 * 256);  // item names
                poison(0x396FE0, 0x396FE0 + 9 * 200);  // monster type names
                poison(0x3953E0, 0x3953E0 + 27 * 256); // item descriptions
                for (const sawgod::game::DialogueRun &d : sawgod::game::kDialogueRuns)
                    poison(d.start, d.end);
            }
            std::fprintf(stderr, "text: ROM bytes scrambled under the text resource\n");
            // and every string a text routine gets from the ROM (not the resource): unkeyed text
            sawgod::game::Machine::text_watch = [](sawgod::game::Machine &m, const char *routine) {
                static std::set<uint32_t> seen;
                const uint16_t ds = m.ds(), si = m.si();
                const uint32_t lin = ((uint32_t(ds) << 4) + si) & 0xFFFFF;
                if (ds == sawgod::game::kTextSegment || lin < 0x40000)
                    return; // the resource's, or RAM (names, numbers)
                if (ds == 0x9000 && si >= 0x53E0 && si < 0x53E0 + 27 * 256)
                    return; // an item description: overlaid from the resource (text.h record_byte)
                if (seen.insert(lin).second) {
                    std::fprintf(stderr, "poison: %s draws ROM text at %04X:%04X\n", routine, ds, si);
                    if (const char *at = std::getenv("SAWGOD_POISON_ABORT_AT");
                        at && std::strtoul(at, nullptr, 16) == si)
                        std::abort();
                }
            };
        }
        if (!store_dir.empty()) {
            std::vector<std::string> warnings;
            if (!sawgod::game::read_save_store(store_dir, slots, &warnings) &&
                !store_import.empty()) // (attaches)
                sawgod::game::import_wsc_sram(read_file(store_import), slots);
            for (const std::string &w : warnings)
                std::fprintf(stderr, "--store: %s\n", w.c_str());
        }
    }

    // --difftest: whenever the interpreter reaches a decompiled routine, run the original (sandboxed: CPU
    // steps only, no display/interrupts) and the native version from the same snapshot and compare the
    // resulting registers, RAM (except dead stack below SP), I/O and SRAM; then restore the snapshot and
    // carry on with the original, so the movie plays unchanged.
    struct DiffStats {
        long calls = 0, mismatches = 0;
        long unfinished = 0; // real calls where neither side returned within the step limit (e.g. the movie
                             // ends mid-battle): not compared, reported
        std::string first;
        long fuzz_calls = 0, fuzz_mismatches = 0, fuzz_skipped = 0;
        std::string fuzz_first;
        // --keyfuzz: runs, mismatches, runs that returned / hit the frame cap / were skipped, real calls
        // fuzzed
        long kf_runs = 0, kf_mismatches = 0, kf_returned = 0, kf_capped = 0, kf_skipped = 0, kf_calls = 0;
        long kf_frame_points = 0;    // frame points compared, all runs
        uint64_t kf_first_frame = 0; // the frame of the first fuzzed call (the entry state)
        std::string kf_first;
        std::string kf_seeds; // every mismatching run's seed
    };
    std::map<std::string, DiffStats> diff_stats;
    std::map<uint32_t, const sawgod::game::Routine *> diff_routines;
    std::function<void(Wsc &, const sawgod::game::Routine &, int, int)> diff_call;
    bool in_diff = false;
    // --states: every registered routine (not just the ones under --difftest: a `setup` entry can name any
    // of them), and the per-ROM overrides file if given. cur_override is set only inside invoke_all() below
    // (so --states has no effect outside --invoke, as documented), and read by diff_call's `[&]` capture.
    std::map<uint32_t, const sawgod::game::Routine *> all_routines;
    {
        size_t n = 0;
        const sawgod::game::Routine *rs = sawgod::game::routines(&n);
        for (size_t i = 0; i < n; ++i)
            all_routines[rs[i].rom] = &rs[i];
    }
    std::map<uint32_t, StateOverride> states =
        states_path.empty() ? std::map<uint32_t, StateOverride>() : load_states(states_path);
    const StateOverride *cur_override = nullptr;
    // Routines that span frames (Routine::yields: they call original code such as wait_vblank) run
    // through whole-machine ticks, original and native alike: interrupts are only taken inside
    // wait_vblank (native code can't be interrupted, so the original's own instructions mustn't be
    // either), and each new frame gets the movie's keys for that frame.
    // Frame points: where time passes in a frame-spanning test. wait_vblank, and read_keypad, which some
    // loops poll without waiting for VBlank (e.g. "press A" after a victory): in the port such a loop must
    // hand control back to the frame loop too.
    constexpr uint32_t kWaitVblank = 0x340009, kReadKeypad = 0x34005C, kWaitScanline = 0x340051;
    constexpr int kYieldsFuzzRuns = 20;
    constexpr uint64_t kYieldsFuzzFrames = 1024;
    const sawgod::tools::MovieInputs movie_inputs(changes, holds, pokes);
    auto keys_at = [&](uint64_t frame) { return movie_inputs.keys_at(frame); };
    auto movie_ram_at = [&](Wsc &w, uint64_t frame) { movie_inputs.ram_at(w.ram, frame); };
    struct Gate {
        bool open = false;
        uint16_t cs = 0, ip = 0, sp = 0;
        int vblanks = 0;  // wait_vblank entries so far (loop routines are compared at the kLoopFrames-th)
        uint32_t rom = 0; // the frame point owning this gate
    };
    // (SAWGOD_LOOP_FRAMES overrides: e.g. 13000 runs main from power-on through the title into the field)
    const int kLoopFrames =
        std::getenv("SAWGOD_LOOP_FRAMES") ? std::atoi(std::getenv("SAWGOD_LOOP_FRAMES")) : 3;
    // --keyfuzz: called at every frame point (the gate opening), before the wait's first instruction
    std::function<void(Wsc &, const Gate &)> on_frame_point;
    // The interrupt gate opens on entry to wait_vblank and closes on its return.
    auto gate_update = [&](Wsc &w, Gate &g) {
        const CpuState &c = w.cpu.s;
        const int64_t at = g.open ? -1 : w.rom_offset_of(w.cpu.pc());
        if (at == kWaitVblank || at == kReadKeypad || at == kWaitScanline) { // (waits on the line counter)
            const uint32_t a = ((uint32_t(c.seg[SS]) << 4) + c.r[SP]) & 0xFFFF;
            g = {true,
                 uint16_t(w.ram[(a + 2) & 0xFFFF] | w.ram[(a + 3) & 0xFFFF] << 8),
                 uint16_t(w.ram[a] | w.ram[(a + 1) & 0xFFFF] << 8),
                 uint16_t(c.r[SP] + 4),
                 g.vblanks + (at == kWaitVblank),
                 uint32_t(at)};
            if (on_frame_point)
                on_frame_point(w, g);
        } else if (g.open && c.seg[CS] == g.cs && c.ip == g.ip && c.r[SP] == g.sp) {
            g.open = false;
        }
    };
    // One machine step for a frame-spanning test. Time passes only while the gate is open (inside
    // wait_vblank): native code takes none, so the original's own instructions and other call-outs mustn't
    // either -- otherwise the original's work would cross VBlanks the native version never sees.
    // Fuzz runs of frame-spanning routines get random key presses (the same sequence for the original and
    // the native run, from the pass number and frame) instead of the movie's keys, so menus and input waits
    // reached by direct invocation are navigated and left rather than waiting forever: a press on every
    // other frame (so each one is a new press), one button, B (cancel) twice as likely.
    long span_key_seed = -1; // >= 0: the fuzz pass number
    long kf_seed = -1;       // >= 0: the --keyfuzz run in progress uses this seed
    // --keyfuzz doesn't compare sound channel 3's frequency ports (0x84/0x85): its hardware pitch sweep
    // rewrites them on its own clock, and over hundreds of frames the two runs' sub-line timing drifts apart
    // (time only passes inside the frame points, but leaving one doesn't always take exactly as many cycles
    // on both sides: a cycle here and there), so the sweep steps at slightly different moments. Game
    // state (RAM) isn't affected; what the CPU writes there is checked by the ordinary difftests.
    auto kf_timing_port = [](int port) { return port == 0x84 || port == 0x85; };
    auto random_keys = [](long seed, uint64_t frame) -> uint16_t {
        uint32_t h = uint32_t(seed) * 0x9E3779B9u ^ uint32_t(frame) * 0x85EBCA6Bu;
        h ^= h >> 15;
        h *= 0x2C1B3C6Du;
        h ^= h >> 12;
        if (frame & 1)
            return 0;
        static const uint16_t pick[] = {KEY_X1, KEY_X2, KEY_X3, KEY_X4, KEY_A, KEY_B, KEY_B, KEY_START};
        return pick[h & 7];
    };
    // --keyfuzz: the current run's key schedule (keys for frame kf_base + i), empty when not key fuzzing;
    // span_sp_min: the lowest SP a frame-spanning test reached in the tested routine's stack segment
    std::vector<uint16_t> kf_sched;
    uint64_t kf_base = 0;
    uint16_t span_sp_min = 0xFFFF, span_ss = 0;
    // weighted picks from --keyfuzz-keys
    std::vector<std::pair<uint16_t, int>> kf_weights;
    int kf_weight_total = 0;
    for (size_t i = 0; i < kf_keys.size();) {
        size_t j = kf_keys.find(',', i);
        if (j == std::string::npos)
            j = kf_keys.size();
        std::string item = kf_keys.substr(i, j - i);
        size_t eq = item.find('=');
        int wgt = eq == std::string::npos ? 1 : std::atoi(item.c_str() + eq + 1);
        uint16_t key = parse_buttons(item.substr(0, eq));
        if (key && wgt > 0) {
            kf_weights.emplace_back(key, wgt);
            kf_weight_total += wgt;
        }
        i = j + 1;
    }
    // A run's key schedule: a press of one key (sometimes two at once, and now and then two opposite
    // directions, which the hardware allows and each loop resolves in its own order) held 1-4 frames
    // (sometimes 10-40, so auto-repeat kicks in), then a gap of 1-12 released frames (sometimes 30-90,
    // letting an animation or a popup finish). Same seed, same schedule: reproducible, and identical for the
    // original and native runs.
    auto make_schedule = [&](long seed, size_t frames) {
        uint64_t st = 0x9E3779B97F4A7C15ull ^ (uint64_t(seed) * 0xD1B54A32D192ED03ull);
        auto rnd = [&st](uint32_t n) {
            st ^= st >> 12;
            st ^= st << 25;
            st ^= st >> 27;
            return uint32_t(((st * 0x2545F4914F6CDD1Dull) >> 32) % n);
        };
        auto pick = [&]() {
            int x = int(rnd(uint32_t(kf_weight_total)));
            for (const auto &[k, wgt] : kf_weights)
                if ((x -= wgt) < 0)
                    return k;
            return uint16_t(0);
        };
        std::vector<uint16_t> s;
        while (s.size() < frames) {
            size_t gap = rnd(8) ? 1 + rnd(12) : 30 + rnd(61);
            s.insert(s.end(), gap, 0);
            static const uint16_t opposite[] = {KEY_X1 | KEY_X3, KEY_X2 | KEY_X4, KEY_Y1 | KEY_Y3,
                                                KEY_Y2 | KEY_Y4};
            uint16_t k = kf_weight_total ? pick() : 0;
            if (kf_weight_total && rnd(10) == 0)
                k |= pick();
            else if (rnd(25) == 0)
                k = opposite[rnd(4)];
            size_t hold = rnd(7) ? 1 + rnd(4) : 10 + rnd(31);
            s.insert(s.end(), hold, k);
        }
        s.resize(frames);
        return s;
    };
    auto span_tick = [&](Wsc &w, Gate &g) {
        gate_update(w, g);
        if (w.cpu.s.seg[SS] == span_ss && w.cpu.s.r[SP] < span_sp_min)
            span_sp_min = w.cpu.s.r[SP];
        // A native wait's setup, IRQ handlers and return consume no hardware time. Give the original
        // the same clock: only its side-effect-free polling loop may advance to the next line. Letting
        // the prologue cross VBlank can clear the flag after the IRQ and wait an extra frame; timing
        // interpreted raster handlers also makes them read different lines than the parked native.
        const bool vblank = g.open && g.rom == kWaitVblank;
        const bool scanline = g.open && g.rom == kWaitScanline;
        w.irq_gate = [&g] { return g.open; };
        if (vblank     ? sawgod::tools::tick_vblank_wait(w)
            : scanline ? sawgod::tools::tick_scanline_wait(w)
                       : w.tick(false, g.open)) {
            ++w.frame_count;
            const uint64_t ki = w.frame_count - kf_base;
            w.set_keys(!kf_sched.empty()    ? (ki < kf_sched.size() ? kf_sched[ki] : 0)
                       : span_key_seed >= 0 ? random_keys(span_key_seed, w.frame_count)
                                            : keys_at(w.frame_count));
            // Real-call trials replay the movie's external RAM inputs at the same absolute frame as
            // keys. Fuzz uses its own key schedule and only inherits RAM directives at call entry.
            if (kf_sched.empty() && span_key_seed < 0)
                movie_ram_at(w, w.frame_count);
            w.begin_frame();
        }
        w.irq_gate = nullptr;
    };
    if (!difftest.empty()) {
        size_t n = 0;
        const sawgod::game::Routine *rs = sawgod::game::routines(&n);
        for (size_t i = 0; i < n; ++i)
            if (difftest == "all" ||
                ("," + difftest + ",").find("," + std::string(rs[i].name) + ",") != std::string::npos)
                diff_routines[rs[i].rom] = &rs[i];
        // a name that matches no registered routine is an error, not an empty (passing) run
        if (difftest != "all") {
            std::string rest = difftest + ",";
            for (size_t c; (c = rest.find(',')) != std::string::npos; rest.erase(0, c + 1)) {
                const std::string want = rest.substr(0, c);
                bool found = false;
                for (size_t i = 0; i < n && !found; ++i)
                    found = want == rs[i].name;
                if (!found) {
                    std::fprintf(stderr, "--difftest: no registered routine '%s'\n", want.c_str());
                    return 2;
                }
            }
        }
        // Run the original and the native routine from the machine's current state (at the routine's entry,
        // return address on the stack) and compare. Pass 0 is that state as is; passes 1..fuzz_runs use
        // random registers. first_iter = 1 runs only the fuzz passes.
        diff_call = [&](Wsc &w, const sawgod::game::Routine &r, int first_iter, int last_iter) {
            DiffStats &st = diff_stats[r.name];
            const Wsc::Snapshot real = w.snapshot();
            // pass 0: the real call; passes 1..fuzz_runs: the same machine state with random registers
            // frame-spanning routines: fewer fuzz passes (each can run for hundreds of frames, e.g. a battle
            // message popup)
            if (r.yields && last_iter > first_iter + kYieldsFuzzRuns - 1)
                last_iter = std::max(first_iter, 1) + kYieldsFuzzRuns - 1;
            for (int iter = first_iter; iter <= last_iter; ++iter) {
                Wsc::Snapshot start = real;
                // --keyfuzz run: the real call state as is (no random registers), seeded random keys
                const bool kf = kf_seed >= 0 && iter > 0 && r.yields;
                if (kf) {
                    w.restore(start);
                    kf_sched = make_schedule(kf_seed, size_t(kf_frames) * 4 + 256);
                    kf_base = start.frame_count;
                } else if (iter > 0) {
                    for (int k : {AX, BX, CX, DX, SI, DI, BP}) {
                        // not near the stack: a pointer into the routine's own frame aliases its pushes
                        uint16_t v;
                        do
                            v = uint16_t(fuzz_next() >> 16);
                        while (uint16_t(v - real.cpu.s.r[SP] + 0x200) < 0x400);
                        start.cpu.s.r[k] = v;
                    }
                    // --states: ram overrides first (this routine's required scratch-RAM setup), then regs
                    // (which may read it back via "ram16:ADDR") -- applied after the random fuzz fill above,
                    // so they always win for the addresses/registers they name.
                    if (cur_override) {
                        for (const auto &[addr, val] : cur_override->ram)
                            start.ram[addr] = val;
                        for (const auto &[name, val] : cur_override->regs)
                            set_named_reg(start.cpu.s, name, resolve_reg_value(val, start.ram));
                    }
                    w.restore(start);
                }
                span_key_seed = r.yields && iter > 0 && !kf ? iter : -1;
                // yields: a real call can be a whole battle
                const long real_limit = std::getenv("SAWGOD_REAL_STEP_LIMIT")
                                            ? std::atol(std::getenv("SAWGOD_REAL_STEP_LIMIT"))
                                            : 150000000; // e.g. a whole battle
                const long step_limit = kf         ? long(kf_frames + 16) * 2000000
                                        : r.yields ? (iter ? 60000000 : real_limit)
                                        : iter     ? 200000
                                                   : 5000000;
                // Random counters can request tens of thousands of waits. The parked clock executes
                // few instructions per frame, so the instruction guard alone doesn't skip those.
                // Curated fixtures can budget more frames for a complete scripted battle. Decide a
                // long-wait skip from the original only; a native exceeding the same budget after the
                // original returned is still a mismatch. Real calls and key fuzz are exempt.
                const uint64_t fuzz_frames =
                    cur_override && cur_override->max_frames ? cur_override->max_frames : kYieldsFuzzFrames;
                auto fuzz_wait_limit = [&](const Wsc &x) {
                    return iter > 0 && !kf && r.yields && x.frame_count - start.frame_count >= fuzz_frames;
                };
                Gate gate;
                const CpuState &s = w.cpu.s;
                const uint16_t sp0 = s.r[SP], ss = s.seg[SS];
                auto rd16 = [&](uint16_t off) {
                    uint32_t a = ((uint32_t(ss) << 4) + off) & 0xFFFFF;
                    return uint16_t(w.ram[a & 0xFFFF] | w.ram[(a + 1) & 0xFFFF] << 8);
                };
                // near routines return with RET (IP only), far ones with RETF (IP, CS)
                // interrupt handlers return with IRET (IP, CS, FLAGS)
                const uint16_t ret_ip = rd16(sp0);
                const uint16_t ret_cs = r.near ? s.seg[CS] : rd16(uint16_t(sp0 + 2));
                const uint16_t ret_sp = uint16_t(sp0 + (r.irq ? 6 : r.near ? 2 : 4));
                // the original, to its return
                long steps = 0;
                std::vector<std::pair<uint32_t, CpuState>> path; // --difftrace: the original's steps
                // fuzz runs: a write into the live stack (between the SP at the start of the writing
                // instruction and the caller's return address) that isn't a push clobbers the routine's own
                // frame -- saved registers or the return address -- which makes the run meaningless
                bool frame_clobbered = false;
                uint16_t sp_now = sp0, sp_min = sp0;
                uint16_t loop_sp_top =
                    sp0; // loop routines: the highest SP of the original's run (main reloads SS:SP)
                bool stack_read_op =
                    false; // the current instruction reads the stack legitimately (pop, ret...)
                auto prev_bus = w.on_bus;
                if (iter > 0 && !kf)
                    w.on_bus = [&, prev_bus](char kind, uint32_t addr) {
                        if (prev_bus)
                            prev_bus(kind, addr);
                        uint16_t off = uint16_t(addr - (uint32_t(ss) << 4));
                        if (kind == 'W' && uint16_t(off - sp_now) < uint16_t(ret_sp - sp_now))
                            frame_clobbered = true; // a non-push write into the live frame
                        // a read (not a pop/return) of anything the routine's stack has held so far
                        if (kind == 'R' && !stack_read_op &&
                            uint16_t(off - sp_min) < uint16_t(ret_sp - sp_min))
                            frame_clobbered = true;
                    };
                // done: returned to the caller, or unwound past its own frame (a non-local return: `pop ax;
                // ret` from a guard, an abort that reloads SP) -- the first instruction with SP above the
                // slot
                // a routine that never returns (Routine::loop: the main loops) is compared instead when it
                // enters wait_vblank for the kLoopFrames-th time
                // --states end_at: some originals (0x358937) end with a far call matched by a near return,
                // so they land at a fixed CS:IP that isn't their caller's return address and never satisfy
                // the normal `finished` check below. Once CS:IP has reached end_at once, "finished" instead
                // means reaching that same point (CS:IP:SS:SP) a second time -- matching broken_opcode39.py.
                bool broken_return_seen = false;
                CpuState broken_exit{};
                // --keyfuzz: a run that hasn't returned is stopped (both sides) at its kf_frames-th
                // wait_vblank entry and compared there like a loop routine
                auto capped = [&](const Gate &g) { return kf && g.vblanks >= kf_frames; };
                auto finished = [&](const Wsc &x, const Gate &g) {
                    const CpuState &c = x.cpu.s;
                    if (capped(g))
                        return true;
                    if (cur_override && cur_override->has_end_at && broken_return_seen)
                        return c.seg[CS] == broken_exit.seg[CS] && c.ip == broken_exit.ip &&
                               c.seg[SS] == broken_exit.seg[SS] && c.r[SP] == broken_exit.r[SP];
                    if (r.loop)
                        return g.vblanks >= kLoopFrames;
                    return (c.seg[CS] == ret_cs && c.ip == ret_ip && c.r[SP] == ret_sp) ||
                           (c.seg[SS] == ss && int16_t(uint16_t(c.r[SP] - ret_sp)) > 0);
                };
                // --keyfuzz: the original's state at each frame point, for the native run to compare against
                struct FramePoint {
                    std::array<uint8_t, 0x10000> ram;
                    std::array<uint8_t, 0x100> io;
                    uint64_t sram_hash;
                    CpuState cpu;
                    uint64_t frame;
                    uint16_t sp_min;
                };
                auto sram_hash = [](const std::vector<uint8_t> &v) {
                    uint64_t h = 0xCBF29CE484222325ull;
                    for (uint8_t b : v)
                        h = (h ^ b) * 0x100000001B3ull;
                    return h;
                };
                std::vector<std::unique_ptr<FramePoint>> fps;
                span_ss = ss;
                span_sp_min = sp0;
                if (kf)
                    on_frame_point = [&](Wsc &x, const Gate &) {
                        auto fp = std::make_unique<FramePoint>();
                        fp->ram = x.ram;
                        fp->io = x.io;
                        fp->sram_hash = sram_hash(x.sram);
                        fp->cpu = x.cpu.s;
                        fp->frame = x.frame_count;
                        fp->sp_min = std::min(span_sp_min, x.cpu.s.r[SP]);
                        fps.push_back(std::move(fp));
                    };
                while (!finished(w, gate) && steps < step_limit && !fuzz_wait_limit(w)) {
                    if (difftrace && (iter ? st.fuzz_mismatches : st.mismatches) == 0 && path.size() < 4000)
                        path.emplace_back(w.cpu.pc(), w.cpu.s);
                    sp_now = w.cpu.s.r[SP];
                    if (uint16_t(sp0 - sp_now) > uint16_t(sp0 - sp_min) && uint16_t(sp0 - sp_now) < 0x8000)
                        sp_min = sp_now;
                    if (r.loop && sp_now > loop_sp_top)
                        loop_sp_top = sp_now;
                    if (iter > 0) {
                        uint32_t a = w.cpu.pc();
                        uint8_t op = w.read8(a);
                        while (op == 0x26 || op == 0x2E || op == 0x36 || op == 0x3E || op == 0xF0 ||
                               op == 0xF2 || op == 0xF3)
                            op = w.read8(++a & 0xFFFFF);
                        stack_read_op = (op >= 0x58 && op <= 0x5F) || op == 0x07 || op == 0x17 ||
                                        op == 0x1F || op == 0x8F || op == 0x61 || op == 0x9D || op == 0xC2 ||
                                        op == 0xC3 || op == 0xC9 || op == 0xCA || op == 0xCB || op == 0xCF;
                    }
                    const bool at_end_at = cur_override && cur_override->has_end_at &&
                                           w.cpu.s.seg[CS] == cur_override->end_cs &&
                                           w.cpu.s.ip == cur_override->end_ip;
                    if (r.yields)
                        span_tick(w, gate);
                    else
                        w.cpu.step();
                    ++steps;
                    if (at_end_at) {
                        broken_return_seen = true;
                        broken_exit = w.cpu.s;
                    }
                }
                w.on_bus = prev_bus;
                on_frame_point = nullptr;
                const bool orig_capped = capped(gate);
                const bool original_wait_limit = fuzz_wait_limit(w) && !finished(w, gate);
                const uint16_t orig_sp_min = std::min(sp_min, span_sp_min);
                const Wsc::Snapshot orig = w.snapshot();
                w.restore(start);
                // A random state that sent the original astray, or made it overwrite its own frame, return
                // address or the interrupt vectors, isn't a meaningful test -- decided before running the
                // native version, which would faithfully go astray too (e.g. loop forever on an unterminated
                // string).
                // (a loop routine abandons its caller's frame by design: main reloads SS:SP)
                bool clobbered = frame_clobbered && !r.loop && !kf;
                if (iter > 0 && !r.loop && !kf) {
                    const uint32_t slot = ((uint32_t(ss) << 4) + sp0) & 0xFFFF;
                    for (uint32_t k = 0; k < uint32_t(r.near ? 2 : 4); ++k)
                        clobbered |= orig.ram[(slot + k) & 0xFFFF] != start.ram[(slot + k) & 0xFFFF];
                    // (frame-spanning routines legitimately set interrupt vectors, e.g. the message popup;
                    // the RAM comparison still covers those writes)
                    // (0x014A-0x014B: cursor scratch coordinates some routines legitimately write)
                    for (uint32_t k = 0; k < 0x400 && !clobbered && !r.yields; ++k)
                        if (k == 0x14A || k == 0x14B)
                            continue;
                        else if (cur_override &&
                                 std::any_of(cur_override->low_ram_ok.begin(), cur_override->low_ram_ok.end(),
                                             [k](auto &lh) { return k >= lh.first && k < lh.second; }))
                            continue; // --states low_ram_ok: this routine's own intended low-RAM writes
                        else if (orig.ram[k] != start.ram[k]) {
                            clobbered = true;
                            if (std::getenv("SAWGOD_DEBUG_CLOBBER"))
                                std::fprintf(stderr, "clobber %s ram %03x %02x->%02x frame_clobbered=%d\n",
                                             r.name, k, start.ram[k], orig.ram[k], int(frame_clobbered));
                        }
                    if ((steps >= step_limit || original_wait_limit) && std::getenv("SAWGOD_DEBUG_CLOBBER"))
                        std::fprintf(stderr,
                                     "astray %s pc=%05x sp=%04x halted=%d gate=%d line=%d frame=%llu if=%d\n",
                                     r.name, orig.cpu.s.seg[CS] * 16 + orig.cpu.s.ip, orig.cpu.s.r[SP],
                                     int(orig.cpu.s.halted), int(gate.open), orig.scanline,
                                     (unsigned long long)orig.frame_count, int((orig.cpu.s.flags >> 9) & 1));
                    if (frame_clobbered && std::getenv("SAWGOD_DEBUG_CLOBBER"))
                        std::fprintf(stderr, "frame-clobber %s steps=%ld\n", r.name, steps);
                }
                // --states end_at: the original legitimately abandons its own frame at the broken return
                // point, so the ordinary clobber check above (which fires on exactly that) doesn't apply.
                if (cur_override && cur_override->has_end_at && broken_return_seen)
                    clobbered = false;
                if (iter > 0 && (steps >= step_limit || original_wait_limit || clobbered)) {
                    ++(kf ? st.kf_skipped : st.fuzz_skipped);
                    kf_sched.clear();
                    continue;
                }
                bool native_returned = true, nat_capped = false;
                // --keyfuzz: compare the native run with the original's frame point by frame point: RAM
                // (outside the stack both runs used, which native code doesn't reproduce), I/O and SRAM
                size_t fp_index = 0;
                std::string fp_why;
                span_sp_min = sp0;
                if (kf)
                    on_frame_point = [&](Wsc &x, const Gate &) {
                        const size_t k = fp_index++;
                        if (!fp_why.empty())
                            return;
                        char b[200];
                        if (k >= fps.size()) {
                            std::snprintf(b, sizeof b,
                                          "native reached frame point %zu, the original only %zu; ", k + 1,
                                          fps.size());
                            fp_why = b;
                            return;
                        }
                        const FramePoint &o = *fps[k];
                        const uint32_t base = uint32_t(ss) << 4;
                        const uint16_t lo =
                            uint16_t(std::min({o.sp_min, span_sp_min, x.cpu.s.r[SP], o.cpu.r[SP]}) - 64);
                        std::string d;
                        int n = 0;
                        for (uint32_t i = 0; i < 0x10000; ++i) {
                            if (o.ram[i] == x.ram[i] ||
                                uint16_t(uint16_t(i - base) - lo) < uint16_t(ret_sp - lo))
                                continue;
                            if (++n <= 4) {
                                std::snprintf(b, sizeof b, "RAM %04x %02x/%02x; ", i, o.ram[i], x.ram[i]);
                                d += b;
                            }
                        }
                        if (n > 4) {
                            std::snprintf(b, sizeof b, "(%d RAM bytes); ", n);
                            d += b;
                        }
                        for (int i = 0; i < 256; ++i)
                            if (o.io[i] != x.io[i] && !kf_timing_port(i)) {
                                std::snprintf(b, sizeof b, "IO %02x %02x/%02x; ", i, o.io[i], x.io[i]);
                                d += b;
                            }
                        if (o.sram_hash != sram_hash(x.sram))
                            d += "SRAM differs; ";
                        // segment registers too (a routine that leaves DS = CS behind shows up here first);
                        // not the general registers, whose incidental values at a call-out native code keeps
                        // in C++ instead (SAWGOD_KF_GPRS=1 compares them anyway, for debugging)
                        static const char *rn[] = {"AX", "CX", "DX", "BX", "SP", "BP", "SI", "DI"};
                        static const char *sn[] = {"ES", "CS", "SS", "DS"};
                        for (int i = 0; i < 8 && std::getenv("SAWGOD_KF_GPRS"); ++i)
                            if (i != SP && o.cpu.r[i] != x.cpu.s.r[i]) {
                                std::snprintf(b, sizeof b, "%s %04x/%04x; ", rn[i], o.cpu.r[i], x.cpu.s.r[i]);
                                d += b;
                            }
                        for (int i = 0; i < 4; ++i)
                            if (o.cpu.seg[i] != x.cpu.s.seg[i]) {
                                std::snprintf(b, sizeof b, "%s %04x/%04x; ", sn[i], o.cpu.seg[i],
                                              x.cpu.s.seg[i]);
                                d += b;
                            }
                        // SAWGOD_KF_DEBUG=1: on the first diverging frame point, every differing byte and the
                        // keys up to it (frame offset: keys), on stderr
                        if (!d.empty() && std::getenv("SAWGOD_KF_DEBUG")) {
                            for (uint32_t i = 0; i < 0x10000; ++i)
                                if (o.ram[i] != x.ram[i] &&
                                    !(uint16_t(uint16_t(i - base) - lo) < uint16_t(ret_sp - lo)))
                                    std::fprintf(stderr, "  RAM %04x %02x/%02x\n", i, o.ram[i], x.ram[i]);
                            uint16_t prev = 0;
                            for (uint64_t f = 0; f <= o.frame - kf_base && f < kf_sched.size(); ++f)
                                if (kf_sched[f] != prev)
                                    std::fprintf(stderr, "  keys +%llu: %03x\n", (unsigned long long)f,
                                                 prev = kf_sched[f]);
                        }
                        if (!d.empty()) {
                            const int64_t at = x.rom_offset_of(x.cpu.pc());
                            std::snprintf(b, sizeof b, "frame point %zu (frame +%llu, at %06llx): ", k + 1,
                                          (unsigned long long)(o.frame - kf_base), (unsigned long long)at);
                            fp_why = b + d;
                        }
                    };
                if (r.yields) {
                    Gate ngate;
                    // A frame point tested directly can park on its first resume. Open its time/IRQ
                    // gate before that resume replaces the entry PC with the call-out trampoline.
                    gate_update(w, ngate);
                    std::unique_ptr<sawgod::game::Fiber> f = sawgod::game::start_yielding(w, r.fn);
                    long nsteps = 0;
                    bool done = f->resume();
                    w.discard_stall(); // native code takes no machine time
                    while (!done && nsteps < step_limit && !(r.loop && finished(w, ngate)) &&
                           !capped(ngate) && !fuzz_wait_limit(w)) {
                        const sawgod::game::Machine::Callout co = sawgod::game::Machine::pending_callout();
                        while ((w.cpu.s.halted || w.line_parked() ||
                                !(w.cpu.s.seg[CS] == co.cs &&
                                  w.cpu.s.ip == sawgod::game::Machine::kTrampolineIp &&
                                  w.cpu.s.r[SP] == co.sp)) &&
                               nsteps < step_limit && !(r.loop && finished(w, ngate)) && !capped(ngate) &&
                               !fuzz_wait_limit(w)) {
                            span_tick(w, ngate);
                            ++nsteps;
                        }
                        if (nsteps < step_limit && !(r.loop && finished(w, ngate)) && !capped(ngate) &&
                            !fuzz_wait_limit(w)) {
                            gate_update(w,
                                        ngate); // back from the callee: close the gate if it was wait_vblank
                            done = f->resume();
                            w.discard_stall();
                        }
                    }
                    // a native that ends in a jump into original code (an abort path) continues there, in the
                    // interpreter, to the same end point as the original
                    while (done && !finished(w, ngate) && nsteps < step_limit && !fuzz_wait_limit(w)) {
                        span_tick(w, ngate);
                        ++nsteps;
                    }
                    native_returned = finished(w, ngate);
                    nat_capped = capped(ngate);
                } else {
                    sawgod::game::Machine m(w);
                    r.fn(m);
                }
                on_frame_point = nullptr;
                kf_sched.clear();
                const Wsc::Snapshot nat = w.snapshot();
                w.restore(start);
                if (iter == 0 && steps >= step_limit && !native_returned) {
                    ++st.unfinished;
                    continue;
                }
                ++(kf ? st.kf_runs : iter ? st.fuzz_calls : st.calls);
                // a key-fuzz run stopped at the frame cap is compared like a loop routine (global state only)
                const bool loopish = r.loop || orig_capped;
                // compare
                char buf[256];
                std::string why;
                const CpuState &a = orig.cpu.s, &b = nat.cpu.s;
                static const char *names[] = {"AX", "CX", "DX", "BX", "SP", "BP", "SI", "DI"};
                static const char *segs[] = {"ES", "CS", "SS", "DS"};
                if (steps >= step_limit)
                    why = "original didn't return; ";
                if (!native_returned)
                    why += "native didn't return; ";
                // Real-call inputs use absolute movie frames. A clock drift must not silently give
                // the two sides different keys or RAM directives, even if the final RAM happens to agree.
                if (iter == 0 && r.yields && orig.frame_count != nat.frame_count) {
                    std::snprintf(buf, sizeof buf, "frames %llu/%llu; ", (unsigned long long)orig.frame_count,
                                  (unsigned long long)nat.frame_count);
                    why += buf;
                }
                // A loop routine is compared in the middle of a call chain (inside wait_vblank, several
                // natives deep): registers and the live stack there belong to the callees' frames, which
                // native code doesn't reproduce mid-call (cores keep registers in C++, pushes aren't
                // replayed). Compare the global state: RAM outside the stack, I/O, SRAM.
                for (int k = 0; k < 8 && !loopish; ++k)
                    if (a.r[k] != b.r[k]) {
                        std::snprintf(buf, sizeof buf, "%s %04x/%04x; ", names[k], a.r[k], b.r[k]);
                        why += buf;
                    }
                for (int k = 0; k < 4 && !loopish; ++k)
                    if (a.seg[k] != b.seg[k]) {
                        std::snprintf(buf, sizeof buf, "%s %04x/%04x; ", segs[k], a.seg[k], b.seg[k]);
                        why += buf;
                    }
                if (a.ip != b.ip && !loopish) {
                    std::snprintf(buf, sizeof buf, "IP %04x/%04x; ", a.ip, b.ip);
                    why += buf;
                }
                uint16_t fmask = r.clobbers_flags ? uint16_t(DF | IF | TF) : uint16_t(0xFFFF);
                if ((a.flags & fmask) != (b.flags & fmask) && !loopish) {
                    std::snprintf(buf, sizeof buf, "FLAGS %03x/%03x; ", a.flags, b.flags);
                    why += buf;
                }
                const uint32_t sp_lin = ((uint32_t(a.seg[SS]) << 4) + a.r[SP]) & 0xFFFF;
                const uint32_t dead_depth = std::max<uint32_t>(256, uint16_t(a.r[SP] - sp_min) + 64u);
                int ram_diffs = 0;
                for (uint32_t i = 0; i < 0x10000; ++i) {
                    if (orig.ram[i] == nat.ram[i])
                        continue;
                    // dead stack: below the final SP, down to the deepest the original's stack reached during
                    // the call (and at least 256 bytes) -- a whole battle's call chain leaves stale frames
                    // far below
                    uint32_t below = (sp_lin - i) & 0xFFFF;
                    if (below >= 1 && below <= dead_depth)
                        continue;
                    if (r.loop &&
                        ((i - sp_lin) & 0xFFFF) < uint32_t(uint16_t(loop_sp_top + 4 - a.r[SP]))) // live stack
                        continue;
                    if (loopish && !r.loop) { // a capped key-fuzz run: the stack both sides used
                        const uint16_t lo =
                            uint16_t(std::min({orig_sp_min, span_sp_min, a.r[SP], b.r[SP]}) - 64);
                        if (uint16_t(uint16_t(i - (uint32_t(ss) << 4)) - lo) < uint16_t(ret_sp - lo))
                            continue;
                    }
                    if (++ram_diffs <= 4) {
                        std::snprintf(buf, sizeof buf, "RAM %04x %02x/%02x; ", i, orig.ram[i], nat.ram[i]);
                        why += buf;
                    }
                }
                for (int k = 0; k < 256; ++k)
                    if (orig.io[k] != nat.io[k] && !(kf && kf_timing_port(k))) {
                        std::snprintf(buf, sizeof buf, "IO %02x %02x/%02x; ", k, orig.io[k], nat.io[k]);
                        why += buf;
                    }
                if (orig.sram != nat.sram)
                    why += "SRAM differs; ";
                // A fuzz run that differs after a different number of frames: say so (the sound driver's
                // state and ports then differ as a consequence, not as a cause).
                if (iter > 0 && !kf && r.yields && !why.empty() && orig.frame_count != nat.frame_count) {
                    std::snprintf(buf, sizeof buf, "frames +%llu/+%llu; ",
                                  (unsigned long long)(orig.frame_count - start.frame_count),
                                  (unsigned long long)(nat.frame_count - start.frame_count));
                    why = buf + why;
                }
                if (kf) {
                    st.kf_frame_points += long(std::min(fp_index, fps.size()));
                    ++(orig_capped ? st.kf_capped : st.kf_returned);
                    std::string head;
                    if (fp_why.empty() && fp_index < fps.size()) {
                        std::snprintf(buf, sizeof buf,
                                      "native stopped at frame point %zu of the original's %zu; ", fp_index,
                                      fps.size());
                        head = buf;
                    }
                    if (orig_capped != nat_capped)
                        head += orig_capped ? "original hit the frame cap, native returned; "
                                            : "native hit the frame cap, original returned; ";
                    // the first diverging frame point says it all; the end state would only repeat it
                    why = fp_why.empty() ? head + why : fp_why + head;
                    if (!why.empty())
                        st.kf_seeds += (st.kf_seeds.empty() ? "" : ",") + std::to_string(kf_seed);
                    if (!why.empty() && ++st.kf_mismatches == 1) {
                        std::snprintf(buf, sizeof buf, "seed %ld (call %ld): ", kf_seed, st.kf_calls);
                        st.kf_first = buf + why;
                    }
                } else if (!why.empty() && iter > 0) {
                    if (++st.fuzz_mismatches == 1) {
                        char regs[128];
                        const CpuState &in = start.cpu.s;
                        std::snprintf(regs, sizeof regs,
                                      " [in: ax=%04x bx=%04x cx=%04x dx=%04x si=%04x di=%04x bp=%04x]",
                                      in.r[AX], in.r[BX], in.r[CX], in.r[DX], in.r[SI], in.r[DI], in.r[BP]);
                        st.fuzz_first = why + regs;
                        if (difftrace) {
                            std::fprintf(stderr, "difftrace (fuzz) %s: %s\n", r.name, st.fuzz_first.c_str());
                            for (const auto &[pc, cs] : path)
                                std::fprintf(stderr,
                                             "  %05x ax=%04x bx=%04x cx=%04x dx=%04x si=%04x di=%04x bp=%04x "
                                             "ds=%04x es=%04x fl=%03x\n",
                                             pc, cs.r[AX], cs.r[BX], cs.r[CX], cs.r[DX], cs.r[SI], cs.r[DI],
                                             cs.r[BP], cs.seg[DS], cs.seg[ES], cs.flags);
                        }
                    }
                } else if (!why.empty()) {
                    if (std::getenv("SAWGOD_DIFFTEST_EACH")) // every mismatch, with the time it happened
                        std::fprintf(stderr, "difftest mismatch %s at cycle %llu: %s\n", r.name,
                                     (unsigned long long)wsc.cpu.cycles, why.c_str());
                    if (++st.mismatches == 1) {
                        st.first = why;
                        if (difftrace) { // the original's path for this call, with registers before each step
                            std::fprintf(stderr, "difftrace %s: %s\n", r.name, why.c_str());
                            for (const auto &[pc, cs] : path)
                                std::fprintf(stderr,
                                             "  %05x ax=%04x bx=%04x cx=%04x dx=%04x si=%04x di=%04x bp=%04x "
                                             "ds=%04x es=%04x fl=%03x\n",
                                             pc, cs.r[AX], cs.r[BX], cs.r[CX], cs.r[DX], cs.r[SI], cs.r[DI],
                                             cs.r[BP], cs.seg[DS], cs.seg[ES], cs.flags);
                        }
                    }
                }
            } // fuzz passes
            w.restore(real);
        };
        auto prev_hook = wsc.on_instruction;
        auto last_pc = std::make_shared<uint32_t>(~0u);
        wsc.on_instruction = [&, prev_hook, last_pc](const Wsc &cw) {
            if (prev_hook)
                prev_hook(cw);
            Wsc &w = const_cast<Wsc &>(cw);
            if (in_diff) // a frame-spanning test ticks the machine, which calls this hook again
                return;
            // only entries reached by a CALL count (not jumps into the routine, e.g. a routine restarting
            // itself with `jmp`): look at the previous instruction's opcode
            const uint32_t prev = *last_pc;
            *last_pc = w.cpu.pc();
            int64_t ro = w.rom_offset_of(w.cpu.pc());
            auto it = ro >= 0 ? diff_routines.find(uint32_t(ro)) : diff_routines.end();
            if (it == diff_routines.end() || (prev == ~0u && !it->second->loop)) // (reset_vector: power-on)
                return;
            if (prev != ~0u) {
                uint32_t a = prev;
                uint8_t op = w.read8(a);
                while (op == 0x26 || op == 0x2E || op == 0x36 || op == 0x3E || op == 0xF0)
                    op = w.read8(++a & 0xFFFFF);
                bool is_call = op == 0xE8 || op == 0x9A ||
                               (op == 0xFF && (((w.read8((a + 1) & 0xFFFFF) >> 3) & 7) == 2 ||
                                               ((w.read8((a + 1) & 0xFFFFF) >> 3) & 7) == 3));
                if (!is_call && !it->second->irq &&
                    !it->second->loop) // interrupt handlers are entered by the interrupt,
                    // main by a jump (reset, soft reset, game over)
                    return;
            }
            if (it == diff_routines.end())
                return;
            in_diff = true;
            // --keyfuzz replaces the real-call comparison (the difftests' job): its runs start from the
            // call's state, and the movie carries on from there with the original as usual
            if (!kf_runs)
                diff_call(w, *it->second, 0, invoke_frames.empty() ? fuzz_runs : 0);
            DiffStats &kst = diff_stats[it->second->name];
            if (kf_runs > 0 && it->second->yields && invoke_frames.empty() && kst.kf_calls < kf_calls) {
                if (kst.kf_calls++ == 0)
                    kst.kf_first_frame = w.frame_count;
                for (int k = 0; k < kf_runs; ++k) {
                    kf_seed = kf_seed_base + k;
                    diff_call(w, *it->second, 1, 1);
                }
                kf_seed = -1;
            }
            in_diff = false;
        };
    }

    std::vector<uint32_t> profile;
    if (!profile_path.empty()) {
        profile.assign(wsc.rom.size(), 0);
        auto prev_hook = wsc.on_instruction;
        wsc.on_instruction = [&profile, prev_hook](const Wsc &w) {
            if (prev_hook)
                prev_hook(w);
            int64_t ro = w.rom_offset_of(w.cpu.pc());
            if (ro >= 0)
                ++profile[size_t(ro)];
        };
    }

    // --invoke: at the end of each listed frame, call every selected routine directly from that game state:
    // push a return address (the paused CS:IP; for near routines, IP only), jump to the entry and run the
    // fuzz passes of diff_call. Covers routines the movies never call. The state is restored afterwards.
    std::set<int> invoke_at;
    for (size_t i = 0; i < invoke_frames.size();) {
        size_t j = invoke_frames.find(',', i);
        if (j == std::string::npos)
            j = invoke_frames.size();
        invoke_at.insert(std::atoi(invoke_frames.substr(i, j - i).c_str()));
        i = j + 1;
    }
    auto invoke_all = [&]() {
        for (const auto &[rom_off, rp] : diff_routines) {
            const sawgod::game::Routine &r = *rp;
            const Wsc::Snapshot base = wsc.snapshot();
            auto it_override = states.find(rom_off);
            cur_override = it_override != states.end() ? &it_override->second : nullptr;
            // --states setup: run named registered routines once, directly (not through diff_call), to
            // populate scratch RAM the routine under test reads but doesn't itself build (e.g. a
            // battle-formation record). A dummy return address is pushed so the routine's own ret()/retf()
            // has something to pop; every register (including SP) is restored from `saved` afterward, so
            // only the setup routine's memory writes persist.
            if (cur_override)
                for (const StateOverride::Setup &su : cur_override->setup) {
                    auto it_su = all_routines.find(su.rom);
                    if (it_su == all_routines.end()) {
                        std::fprintf(stderr, "--states: setup rom %06x not registered\n", su.rom);
                        continue;
                    }
                    const sawgod::game::Routine &sr = *it_su->second;
                    if (sr.yields) { // setup runs synchronously; a yielding routine needs a Fiber, not this
                        std::fprintf(stderr, "--states: setup rom %06x is `yields`, unsupported, skipped\n",
                                     su.rom);
                        continue;
                    }
                    const CpuState saved = wsc.cpu.s;
                    CpuState &sc = wsc.cpu.s;
                    auto spush = [&](uint16_t v) {
                        sc.r[SP] = uint16_t(sc.r[SP] - 2);
                        uint32_t a = ((uint32_t(sc.seg[SS]) << 4) + sc.r[SP]) & 0xFFFFF;
                        wsc.write8(a, uint8_t(v));
                        wsc.write8((a + 1) & 0xFFFFF, uint8_t(v >> 8));
                    };
                    if (!sr.near)
                        spush(0); // dummy far return CS
                    spush(0);     // dummy return IP
                    for (const auto &[name, val] : su.regs)
                        set_named_reg(sc, name, resolve_reg_value(val, wsc.ram));
                    sawgod::game::Machine sm(wsc);
                    sr.fn(sm);
                    wsc.cpu.s = saved; // registers (and SP) back as they were; RAM writes persist
                }
            CpuState &c = wsc.cpu.s;
            auto push = [&](uint16_t v) {
                c.r[SP] = uint16_t(c.r[SP] - 2);
                uint32_t a = ((uint32_t(c.seg[SS]) << 4) + c.r[SP]) & 0xFFFFF;
                wsc.write8(a, uint8_t(v));
                wsc.write8((a + 1) & 0xFFFFF, uint8_t(v >> 8));
            };
            if (!r.near)
                push(c.seg[CS]);
            push(c.ip);
            c.seg[CS] = uint16_t(0x4000 + 0x1000 * ((rom_off >> 16) - 0x34)); // bank 0x34 = segment 4000h
            c.ip = uint16_t(rom_off & 0xFFFF);
            c.halted = false;
            // the frame-end state is often inside the interrupt handler (IF clear): a routine that waits for
            // VBlank could never return from there, so it starts with interrupts enabled
            if (r.yields)
                c.flags |= IF;
            wsc.cpu.clear_prefixes();
            wsc.cpu.flush_queue();
            in_diff = true;
            diff_call(wsc, r, 1, fuzz_runs);
            in_diff = false;
            wsc.restore(base);
            cur_override = nullptr;
        }
    };

    size_t next_change = 0;
    // The same composition as --shot and golden PNGs, also sampled every fingerprint frame.
    auto shot_picture = [&](Wsc::ColorMode mode, bool second) {
        auto picture = item_dialog_ui.active() ? item_dialog_ui.picture(wsc, mode, second)
                       : save_ui.active()      ? save_ui.picture(wsc, mode, second)
                       : service_ui.active()   ? service_ui.picture(wsc, mode, second)
                       : dialogue_ui.active()  ? dialogue_ui.picture(wsc, mode, second)
                       : creation_ui.active()  ? creation_ui.picture(wsc, mode, second)
                       : bestiary_ui.active()  ? bestiary_ui.picture(wsc, mode, second)
                       : party_ui.active()     ? party_ui.picture(wsc, mode, second)
                       : inventory_ui.active() ? inventory_ui.picture(wsc, mode, second)
                       : battle_layout         ? battle_ui.picture(wsc, mode, second)
                                               : wsc.framebuffer_rgb(mode, second);
        sawgod::game::draw_enemy_hp_boxes(wsc, picture, mode, second);
        return picture;
    };
    std::unique_ptr<FILE, decltype(&std::fclose)> fingerprint(
        fingerprint_path.empty() ? nullptr : std::fopen(fingerprint_path.c_str(), "w"), &std::fclose);
    if (!fingerprint_path.empty() && (!fingerprint || native.empty() || frames <= 0)) {
        std::fprintf(stderr, "--fingerprint needs a writable FILE, --native and positive --frames\n");
        return 1;
    }
    if (fingerprint)
        std::fprintf(fingerprint.get(),
                     "# sawgod-fingerprint-v1 fnv1a64 rgb-bgr pcm16le 24000 stereo\n"
                     "frame picture.wsc picture.gb ui.wsc ui.gb presented.wsc presented.gb audio samples\n");
    uint16_t held = 0;
    bool music_gb = false;          // --gb-rom: the GB music switched in (--gfx-swap-at)
    int music_fade_start = -100000; // the frame it last switched
    bool tracing_on = tracestart == 0;
    if (trace && !tracing_on) {
        auto hook = wsc.on_instruction;
        wsc.on_instruction = [&tracing_on, hook](const Wsc &w) {
            if (tracing_on)
                hook(w);
        };
    }
    // --keyfuzz: done once every selected routine has had its --keyfuzz-calls calls fuzzed
    auto keyfuzz_done = [&]() {
        if (!kf_runs || diff_routines.empty())
            return false;
        for (const auto &[rom_off, r] : diff_routines) {
            auto it = diff_stats.find(r->name);
            if (it == diff_stats.end() || it->second.kf_calls < kf_calls)
                return false;
        }
        return true;
    };
    for (int frame = 0; frame < frames && !keyfuzz_done(); ++frame) {
        if (frame >= tracestart)
            tracing_on = true;
        wsc.set_keys(gfx_swap.intro_keys(wsc, held, gb_shown));
        // Holds precede this frame's pokes. Real-call trials inherit the entry writes and replay
        // future frame inputs in span_tick; random-register and key-fuzz trials inherit only entry RAM.
        movie_ram_at(wsc, uint64_t(frame));
        if (modifier_audit)
            modifier_audit->before_frame();
        if (game_config.on())
            sawgod::game::GameConfig::apply(wsc, {});
        wsc.run_frame();
        if (modifier_audit && !modifier_audit->frame(frame + 1))
            break;
        if (battle_layout)
            battle_ui.frame(wsc);
        if (party_layout)
            party_ui.frame(wsc);
        if (inventory_layout)
            inventory_ui.frame(wsc);
        if (item_dialog_layout)
            item_dialog_ui.frame(wsc);
        if (creation_layout)
            creation_ui.frame(wsc);
        if (bestiary_layout)
            bestiary_ui.frame(wsc);
        if (dialogue_layout)
            dialogue_ui.frame(wsc);
        if (service_layout)
            service_ui.frame(wsc);
        if (save_layout)
            save_ui.frame(wsc);
        uint64_t audio_hash = 0;
        size_t audio_samples = 0;
        if (!wav_path.empty() || fingerprint) {
            std::vector<int16_t> a = wsc.apu.take();
            if (gb_music.loaded()) { // as the app's tick(): GB music at the GB picture's weight
                for (size_t p = 0; p < gfx_swap_at.size();) {
                    const size_t comma = gfx_swap_at.find(',', p);
                    if (std::atoi(gfx_swap_at.c_str() + p) == frame + 1) {
                        music_gb = !music_gb;
                        music_fade_start = frame + 1;
                    }
                    p = comma == std::string::npos ? gfx_swap_at.size() : comma + 1;
                }
                constexpr double kFadeFrames = 0.5 * 3072000.0 / (159 * 256);
                const double t = std::min(1.0, (frame + 1 - music_fade_start) / kFadeFrames);
                const float w = float(music_gb ? t : 1.0 - t);
                gb_music.frame(wsc, a, w);
                sawgod::app::apply_channel_gains(wsc, gb_music.playing() ? 1.0f - w : 1.0f, 1.0f);
            }
            if (fingerprint) {
                audio_hash = fingerprint_hash(a, 2);
                audio_samples = a.size();
            }
            if (!wav_path.empty())
                audio.insert(audio.end(), a.begin(), a.end());
        } else {
            wsc.apu.take();
        }
        int done = frame + 1; // the harness's frame number at this frame's end
        if (invoke_at.count(done))
            invoke_all();
        if (!irqdir.empty() && done % irqevery == 0)
            irq_armed = done;
        if (!porttrace_path.empty()) { // sound ports 0x80-0x9B at each frame end (as read by the CPU)
            static FILE *pt = std::fopen(porttrace_path.c_str(), "w");
            std::fprintf(pt, "%d", done);
            for (int port = 0x80; port <= 0x9B; ++port) {
                uint8_t v = wsc.apu.handles_read(uint8_t(port)) ? wsc.apu.read(uint8_t(port)) : wsc.io[port];
                std::fprintf(pt, " %02x", v);
            }
            for (int ch = 0; ch < 4; ++ch)
                std::fprintf(pt, " | %d %d", wsc.apu.sample_index(ch), wsc.apu.period_counter(ch));
            std::fprintf(pt, "\n");
            std::fflush(pt);
        }
        if (ramtrace) {
            std::fprintf(ramtrace, "%d", done);
            for (uint32_t a : addrs)
                std::fprintf(ramtrace, " %02x", wsc.ram[a & 0xFFFF]);
            std::fprintf(ramtrace, "\n");
        }
        if (trace && tracing_on && traced < tracecount)
            std::fprintf(trace, "frame %d %llu %d\n", frame, (unsigned long long)wsc.cpu.cycles,
                         wsc.scanline);
        while (next_change < changes.size() && changes[next_change].first <= done)
            held = changes[next_change++].second;
        if (!teleport.empty() && teleport.find('@') != std::string::npos &&
            std::atoi(teleport.c_str() + teleport.find('@') + 1) == done)
            cheats.request_teleport(std::atoi(teleport.c_str()));
        if (enemy_hp)
            sawgod::game::update_enemy_hp_boxes(wsc, true);
        if (gfx_swap.has_data()) {
            gfx_swap.set_enabled(wsc, true); // both pictures every frame, as the app in GFX Mode GB / Swap
            for (size_t p = 0; p < gfx_swap_at.size();) { // a press at this frame?
                const size_t comma = gfx_swap_at.find(',', p);
                if (std::atoi(gfx_swap_at.c_str() + p) == done)
                    gb_shown = !gb_shown;
                p = comma == std::string::npos ? gfx_swap_at.size() : comma + 1;
            }
            gfx_swap.frame(wsc);
        }
        if (enhanced_oracle)
            enhanced_oracle->frame(wsc, done);
        if (fingerprint) {
            std::fprintf(fingerprint.get(), "%d", done);
            for (int channel = 0; channel < 3; ++channel)
                for (bool second : {false, true}) {
                    if (second && !gfx_swap.has_data()) {
                        std::fprintf(fingerprint.get(), " -");
                        continue;
                    }
                    const auto pixels = channel == 0 ? wsc.framebuffer_rgb(Wsc::ColorMode::Raw, second)
                                        : channel == 1
                                            ? shot_picture(Wsc::ColorMode::Raw, second)
                                            : fingerprint_picture.picture(wsc, Wsc::ColorMode::Raw, second);
                    std::fprintf(fingerprint.get(), " %016llx",
                                 (unsigned long long)fingerprint_hash(pixels, 3));
                }
            std::fprintf(fingerprint.get(), " %016llx %zu\n", (unsigned long long)audio_hash, audio_samples);
        }
        if (gb_audit) {
            // Compose both endpoints every tick: HostPicture keeps player-visible retained pictures.
            audit_picture.frame(wsc);
            sync_audit_fade();
            audit_ui_coverage.fill(0);
            auto original = audit_picture.picture(wsc, Wsc::ColorMode::Raw, false);
            audit_ui_coverage.fill(0);
            auto gb = audit_picture.picture(wsc, Wsc::ColorMode::Raw, true);
            if (effects_capture)
                effects_capture->frame(wsc.frame_count, gb);
            if (audit_animation && effect_scene_audit)
                effect_scene_audit->frame(done, wsc.ram[0xD0DC], audit_picture.shown(), audit_animation,
                                          wsc.framebuffer_rgb(Wsc::ColorMode::Raw, true, true),
                                          wsc.framebuffer_rgb(Wsc::ColorMode::Raw, false, true));
            gb_audit->frame(done, wsc.ram[0xD0DC], audit_picture.shown(), audit_animation, gb, original,
                            &audit_ui_coverage, audit_fade.level(wsc, audit_picture.shown()));
            if (gb_audit_shots.count(done)) {
                const auto stem = gb_audit_dir + "/frame-" + std::to_string(done);
                write_file(stem + "-gb.png", encode_png(gb.data(), Wsc::kWidth, Wsc::kHeight));
                write_file(stem + "-wsc.png", encode_png(original.data(), Wsc::kWidth, Wsc::kHeight));
            }
        }
        if (!goldendir.empty() && goldenevery && done % goldenevery == 0) {
            char base[1024];
            std::snprintf(base, sizeof base, "%s/%06d", goldendir.c_str(), done);
            write_file(std::string(base) + ".ram", std::vector<uint8_t>(wsc.ram.begin(), wsc.ram.end()));
            write_file(std::string(base) + ".sram", wsc.sram);
            write_file(std::string(base) + ".io", std::vector<uint8_t>(wsc.io.begin(), wsc.io.end()));
            write_file(
                std::string(base) + ".png",
                // With --fingerprint, the PNG is the presented picture (HostPicture, as the app shows it).
                encode_png((fingerprint ? fingerprint_picture.picture(wsc, Wsc::ColorMode::Lcd, gb_shown)
                                        : shot_picture(Wsc::ColorMode::Lcd, gb_shown))
                               .data(),
                           Wsc::kWidth, Wsc::kHeight));
        }
    }
    if (modifier_audit && !modifier_audit->finish(modifier_audit_path)) {
        std::fprintf(stderr, "cannot write --modifier-audit\n");
        return 1;
    }
    if (fingerprint && (std::fflush(fingerprint.get()) || std::ferror(fingerprint.get()))) {
        std::fprintf(stderr, "--fingerprint: write failed\n");
        return 1;
    }
    if (enhanced_oracle)
        enhanced_oracle->finish();
    if (gb_audit)
        gb_audit->finish();
    if (effect_scene_audit)
        effect_scene_audit->finish();
    // an --irqdir capture armed at the last frame happens at the next VBlank: run on to it
    if (irq_armed >= 0) {
        wsc.set_keys(gfx_swap.intro_keys(wsc, held, gb_shown));
        wsc.run_frame();
    }
    if (trace)
        std::fclose(trace);
    if (ramtrace)
        std::fclose(ramtrace);
    if (!profile_path.empty()) {
        FILE *pf = std::fopen(profile_path.c_str(), "w");
        for (size_t i = 0; i < profile.size(); ++i)
            if (profile[i])
                std::fprintf(pf, "%zx %u\n", i, profile[i]);
        std::fclose(pf);
    }
    int diff_failed = 0;
    for (const auto &[name, st] : diff_stats) {
        std::printf("difftest %-16s %6ld calls, %ld mismatch%s%s%s", name.c_str(), st.calls, st.mismatches,
                    st.mismatches == 1 ? "" : "es", st.mismatches ? ": first: " : "", st.first.c_str());
        if (st.unfinished)
            std::printf(" (%ld unfinished: neither side returned within the step limit)", st.unfinished);
        std::printf("\n");
        if (st.mismatches)
            diff_failed = 1;
        if (fuzz_runs) {
            std::printf("fuzz     %-16s %6ld runs, %ld mismatch%s (%ld skipped: original astray, exceeded "
                        "frame budget or clobbered its stack)%s%s\n",
                        name.c_str(), st.fuzz_calls, st.fuzz_mismatches, st.fuzz_mismatches == 1 ? "" : "es",
                        st.fuzz_skipped, st.fuzz_mismatches ? ": first: " : "", st.fuzz_first.c_str());
            if (st.fuzz_mismatches)
                diff_failed = 1;
        }
    }
    for (const auto &[name, st] : diff_stats)
        if (kf_runs) {
            std::printf(
                "keyfuzz  %-16s %6ld runs, %ld mismatch%s (%ld returned, %ld capped at %d frames, %ld "
                "skipped: original astray; %ld calls from frame %llu, %ld frame points compared)%s%s\n",
                name.c_str(), st.kf_runs, st.kf_mismatches, st.kf_mismatches == 1 ? "" : "es", st.kf_returned,
                st.kf_capped, kf_frames, st.kf_skipped, st.kf_calls, (unsigned long long)st.kf_first_frame,
                st.kf_frame_points, st.kf_mismatches ? (" (seeds " + st.kf_seeds + "): first: ").c_str() : "",
                st.kf_first.c_str());
            if (st.kf_mismatches)
                diff_failed = 1;
        }
    for (const auto &[rom_off, r] : diff_routines)
        if (!diff_stats.count(r->name))
            std::printf("difftest %-16s never called\n", r->name);
    for (const auto &[pc, n] : writers)
        std::fprintf(stderr, "watch-writes: %s %ld\n", pc.c_str(), n);
    if (!rom_reads_path.empty()) { // per bank: bytes read, and those no resource file covers, as ranges
        const std::vector<bool> covered =
            rom_reads_all ? std::vector<bool>(wsc.rom.size(), false) : sawgod::game::resource_coverage(wsc);
        std::string out =
            "# ROM bytes read (bus reads: code's data reads and DMA) that no resource file covers\n";
        size_t total = 0, uncovered = 0;
        for (size_t bank = 0; bank < wsc.rom.size(); bank += 0x10000) {
            size_t n = 0, u = 0;
            std::string ranges;
            for (size_t a = bank; a < bank + 0x10000 && a < wsc.rom.size(); ++a)
                n += rom_read[a];
            for (size_t a = bank; a < bank + 0x10000 && a < wsc.rom.size(); ++a) {
                if (rom_read[a] && !covered[a]) {
                    size_t e = a;
                    while (e + 1 < bank + 0x10000 && rom_read[e + 1] && !covered[e + 1])
                        ++e;
                    char r[40];
                    std::snprintf(r, sizeof r, " %06zX-%06zX", a, e);
                    ranges += r;
                    u += e - a + 1;
                    a = e;
                }
            }
            total += n, uncovered += u;
            if (n) {
                char line[96];
                std::snprintf(line, sizeof line, "bank %02zX: %zu read, %zu not from files", bank >> 16, n,
                              u);
                out += line + ranges + "\n";
            }
        }
        char line[96];
        std::snprintf(line, sizeof line, "total: %zu read, %zu not from files\n", total, uncovered);
        out += line;
        write_file(rom_reads_path, std::vector<uint8_t>(out.begin(), out.end()));
        std::fprintf(stderr, "%s", line);
    }
    // --native: the scheduler at the end of the run (game::Session)
    if (const sawgod::game::Session *gs = sawgod::game::Session::find(wsc))
        std::printf("session: generation %llu, %zu suspended, %llu abandoned\n",
                    (unsigned long long)gs->generation(), gs->suspended(),
                    (unsigned long long)gs->abandoned());
    if (!shot_path.empty()) {
        std::vector<uint32_t> rgb =
            shot_picture(shot_screen.empty() ? Wsc::ColorMode::Lcd : Wsc::ColorMode::Raw, gb_shown);
        int w = Wsc::kWidth, h = Wsc::kHeight;
        if (shot_screen.rfind("lcd", 0) == 0) // lcd or lcd:AMOUNT (0..1)
            sawgod::fstn_color(rgb,
                               shot_screen.size() > 4 ? std::strtof(shot_screen.c_str() + 4, nullptr) : 0.8f);
        if (shot_screen.rfind("ips:", 0) == 0) { // ips:SCALE, the picture scaled with the stripes
            const int k = std::max(1, std::atoi(shot_screen.c_str() + 4));
            rgb = sawgod::ips_picture(rgb, w, h, k);
            w *= k;
            h *= k;
        }
        write_file(shot_path, encode_png(rgb.data(), w, h));
    }
    if (!store_dir.empty())
        if (sawgod::game::Session *gs = sawgod::game::Session::find(wsc)) {
            std::string error;
            const auto write = [](const std::string &path, const std::string &data, std::string *) {
                return write_file(path, std::vector<uint8_t>(data.begin(), data.end()));
            };
            sawgod::game::write_save_store(store_dir, gs->save_slots(), write, &error);
        }
    if (!savesram_path.empty())
        write_file(savesram_path, wsc.sram);
    if (diff_failed)
        return 3;
    if (!wav_path.empty()) {
        // 16-bit stereo PCM WAV at the APU's rate
        auto le = [](std::vector<uint8_t> &v, uint32_t x, int n) {
            for (int i = 0; i < n; ++i)
                v.push_back(uint8_t(x >> (8 * i)));
        };
        std::vector<uint8_t> w = {'R', 'I', 'F', 'F'};
        uint32_t bytes = uint32_t(audio.size() * 2);
        le(w, 36 + bytes, 4);
        for (char c : std::string("WAVEfmt "))
            w.push_back(uint8_t(c));
        le(w, 16, 4);
        le(w, 1, 2);
        le(w, 2, 2);
        le(w, Apu::kSampleRate, 4);
        le(w, Apu::kSampleRate * 4, 4);
        le(w, 4, 2);
        le(w, 16, 2);
        for (char c : std::string("data"))
            w.push_back(uint8_t(c));
        le(w, bytes, 4);
        for (int16_t smp : audio)
            le(w, uint16_t(smp), 2);
        write_file(wav_path, w);
    }
    return 0;
}
