// GFX swap: the game drawn with the GB version's graphics as a second picture, computed alongside the WSC's
// every frame (GB art inside WSC layouts), so the two can be switched or cross-faded live.
// Presentation only, and the game never sees any of it: the GB picture is drawn from the emulator's shadow
// copy of RAM (emu::Wsc::set_second), where this class puts the GB graphics, plus display options of the
// second picture (palette swaps, greys, a screen-1 picture). All resources are the GB skin's (gb/).
//
// - Field (maps with GB graphics: gb/NNN_metatiles.png, GB metatiles; gb/NNN_lookup.csv, wsc,gb): the GB
//   metatiles as tiles 0x200 + 4 * metatile + quadrant in a grey palette (6), and every word of screen 1 that
//   is a quadrant of a WSC metatile replaced by its GB metatile's, frame by frame.
//   Optional lookup column `type` selects the map's collision attribute (0-31); blank is the default.
//   Typed resources use draw_map_rectangle's cell records, preserving roles and resolving shared words.
//   Optional NNN_cells.csv (x,y,gb) draws the aligned GB room cells directly, before lookups.
//   A strip can include mirrored GB art as additional metatiles (up to 128 total).
// - The player on foot there: the GB field sprite of the leader's type (gb/sprites.png, 16 x 16, 4 poses a
// row:
//   down, up, left, left stepping; gb/sprite_types.csv, type,gb), drawn right after the game draws its own
//   (Machine::player_sprite_hook); the GB animates by mirroring, so each of the WSC's 8 poses is a GB pose,
//   mirrored or not; the player's palette (8) in the GB's sprite colors.
// - Windows (menus, dialogue, battle boxes): drawn with the WSC font's tiles (0x100 + code; the space is the
//   fill, 0xD5-0xDD the frame); each that holds its WSC glyph shows the GB glyph (gb/font.png), and the four
//   window palettes (9000:0000) show in GB greys.
// - Battles: each enemy group's picture is the GB one of its type (gb/monsters.png, monster_pics.csv,
//   monster_types.csv), put in place of the WSC's right after the game draws it
//   (Machine::enemy_portrait_hook) in the group's tiles (0x300 + 0x28 * group) and cells, bottom-aligned and
//   centered, colors 1-4 of its palette (4 + group) as GB greys; it dissolves with the game's masks
//   (Machine::enemy_dissolve_hook). The backdrop shows blank white meanwhile, like the GB's battles.
// - Every sprite (party icons, the cursor, effects, NPCs) and palettes 8-15 in the GB's four greys.
// - The title: while its logo palette (ROM 0x2F6050) is on screen, screen 1 shows the GB title's logo
//   (gb/title.png) faded like the title, its subtitle is hidden and the menu text (0x2F7A08) shows black on
//   white; the WSC's menu, cursor and copyright line work as before.
#pragma once

#include "emu/wsc.h"
#include "game/gb_battle_effects.h"
#include "game/gb_intro.h"
#include "game/hooks.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <string>
#include <vector>

namespace sawgod::emu {
class Wsc;
}

namespace sawgod::game {

class Machine;

class GfxSwap {
  public:
    GfxSwap() {
        sprite_of_type_.fill(-1);
        sprite_of_set_.fill(-1);
        pic_of_type_.fill(-1);
    }
    // The active swap's GB field sprite for a character type, facing down, as four 4bpp tiles (TL, TR, BL,
    // BR; 32 bytes each, colors 1-3 = the GB sprite greys) for host pictures; false: none loaded for it.
    static bool type_sprite_tiles(int type, std::array<uint8_t, 128> &tiles);
    // The active swap's real GB monster portrait, packed 4bpp; dimensions in tiles.
    static bool monster_picture_tiles(int type, std::vector<uint8_t> &tiles, int &width, int &height);
    // A map's GB graphics from its strip and lookup; optional cells are added afterward.
    bool add_map(int id, const std::vector<uint8_t> &metatiles_png, const std::string &lookup_csv,
                 std::string *error);
    // Optional gb/NNN_cells.csv: x,y,gb. Coordinates are WSC map cells (-8..71).
    // Exact cell art takes priority over typed/default lookups; parsing is transactional.
    bool add_cells(int id, const std::string &cells_csv, std::string *error);
    // Read one complete field-map resource triplet from root/gb. A missing map
    // returns false without an error; malformed resources report an error.
    bool load_field_map(int id, const std::string &root, std::string *error);
    // The same from any store (gb.sgz): `read` returns a file's bytes by its path ("gb/001_lookup.csv"), or
    // nothing when it's absent.
    bool load_field_map(int id, const std::function<std::vector<uint8_t>(const std::string &)> &read,
                        std::string *error);
    // Replace the field maps; a derived cache holds decoded pixels/lookups, keyed by the four layered file
    // types for each map. Returns cache hit. Errors are recomputed rather than cached. Empty path disables
    // caching.
    bool load_field_maps_cached(const std::map<std::string, std::vector<uint8_t>> &files,
                                const std::string &cache_path, std::vector<std::string> *errors);
    // Optional audit observer: actual field sprite requests and replacement availability.
    std::function<void(int, int, int, bool)> observe_sprite; // map, WSC set, pose, replaced
    // The GB field sprites from their two files; false and `error` if they don't decode.
    bool add_sprites(const std::vector<uint8_t> &sprites_png, const std::string &types_csv,
                     std::string *error);
    // Field objects: 128 px wide, eight 16x16 poses per row; CSV set,row,poses.
    bool add_objects(const std::vector<uint8_t> &png, const std::string &csv, std::string *error);
    bool add_corpse(const std::vector<uint8_t> &png, std::string *error);
    bool add_hand(const std::vector<uint8_t> &png, std::string *error);
    // Fill NPC-only rows missing from older 16-row resources, from the same embedded GB sprite atlas.
    // Existing resource rows remain authoritative. Set mappings follow the runtime character table.
    void import_field_sprites(emu::Wsc &w);
    // The GB font for the windows from its file; false and `error` if it doesn't decode.
    bool add_font(const std::vector<uint8_t> &font_png, std::string *error);
    // The GB monster pictures from their three files; false and `error` if they don't decode.
    bool add_monsters(const std::vector<uint8_t> &png, const std::string &pics_csv,
                      const std::string &types_csv, std::string *error);
    // The GB title's logo (gb/title.png, 224 x 144 GB shades); false and `error` if it doesn't decode.
    bool add_title(const std::vector<uint8_t> &png, std::string *error);
    bool load_battle_effects(const std::function<std::vector<uint8_t>(const std::string &)> &read,
                             std::string *error);
    // Own the source bytes on a worker. The first effect joins before reading any result; no frame skips.
    void preload_battle_effects(std::map<std::string, std::vector<uint8_t>> files);
    void wait_battle_effects();
    bool has_data() const {
        return !maps_.empty() || !sprites_.empty() || !objects_.empty() || has_hand_ || has_font_ ||
               !pics_.empty() || !title_.empty();
    }
    ~GfxSwap();
    // Live field ring descriptors in world coordinates, for wider host cameras. Only tracked game cells;
    // screen-space replacements are excluded. Key: (y+8)*80+x+8, including the authored border.
    static std::map<int, std::array<uint16_t, 4>> field_world_words(const emu::Wsc &w);
    // The GB picture computed (the machine's second picture, emu::Wsc::framebuffer(true)) or not.
    void set_enabled(emu::Wsc &w, bool on);
    bool enabled() const { return enabled_; }
    // Every frame (between frames): the shadow follows what the game shows.
    void frame(emu::Wsc &w);
    uint16_t intro_keys(emu::Wsc &w, uint16_t keys, bool selected) {
        return intro_.keys(w, keys, selected && enabled_);
    }

  private:
    friend struct GfxSwapCacheProof;
    static std::array<std::string, 4> field_map_files(int id);
    // Immutable during play. Contiguous sorted pairs avoid a heap node per cell on cache restore.
    struct Lookup {
        std::vector<std::pair<int, int>> entries;
        using const_iterator = std::vector<std::pair<int, int>>::const_iterator;
        const_iterator begin() const { return entries.begin(); }
        const_iterator end() const { return entries.end(); }
        size_t size() const { return entries.size(); }
        bool empty() const { return entries.empty(); }
        const_iterator find(int key) const {
            auto it = std::lower_bound(entries.begin(), entries.end(), key,
                                       [](const auto &entry, int value) { return entry.first < value; });
            return it != entries.end() && it->first == key ? it : entries.end();
        }
        int &operator[](int key) {
            auto it = std::lower_bound(entries.begin(), entries.end(), key,
                                       [](const auto &entry, int value) { return entry.first < value; });
            if (it == entries.end() || it->first != key)
                it = entries.insert(it, {key, 0});
            return it->second;
        }
        Lookup &operator=(std::map<int, int> &&source) {
            entries.assign(source.begin(), source.end());
            return *this;
        }
        bool append_sorted(int key, int value) {
            if (!entries.empty() && entries.back().first >= key)
                return false;
            entries.emplace_back(key, value);
            return true;
        }
        bool operator==(const Lookup &) const = default;
    };
    struct Map {
        std::vector<std::array<uint8_t, 256>> metatiles; // per GB metatile: 16 x 16 shades
        std::array<int, 256> gb{};                       // WSC metatile -> GB metatile, -1: none
        Lookup by_cell; // (y modulo 256) * 256 + (x modulo 256) -> GB metatile
        Lookup by_type; // optional (WSC id * 32 + attribute type) -> GB metatile
    };
    struct Pic {
        int w = 0, h = 0;        // in tiles
        std::vector<uint8_t> px; // w * 8 x h * 8: 0 transparent, 1-4 GB shades 0-3
    };
    struct Portrait {
        bool painted = false, defeated = false;
        uint16_t at = 0;                    // the WSC picture's top left (row << 8 | column)
        int w = 0, h = 0;                   // and size in tiles
        std::vector<uint16_t> wsc_cells;    // its cells as the game drew them
        int gx = 0, gy = 0, gw = 0, gh = 0; // the GB picture's cells
        std::vector<uint8_t> tiles;         // and tiles (dissolving)
        emu::Wsc::PaletteSwap swap;
    };
    void install_hooks();
    static void player_hook(Machine &m, uint16_t oam);
    static void field_cell_hook(Machine &m, uint16_t at, uint8_t id, int type, int x, int y);
    struct FieldCell {
        uint8_t id = 0;
        int type = -1;
        int x = 0, y = 0;
        std::array<uint16_t, 4> words{};
        std::array<bool, 4> foreground{};
    };
    std::map<uint16_t, FieldCell> field_cells_;
    std::map<uint16_t, std::pair<uint16_t, uint16_t>> field_overlay_; // original/GB words we own
    std::vector<uint16_t> sparse_tiles_; // unused WSC tile slots borrowed for strict-only maps
    const emu::Wsc *field_owner_ = nullptr;
    int tracked_map_ = -1;
    static void portrait_hook(Machine &m, int group, uint16_t at);
    static void dissolve_hook(Machine &m, int group, int step);
    static inline GfxSwap *active_ = nullptr; // the one the hooks draw for
    void shadow_font(Machine &m);
    void shadow_hand(Machine &m);
    void shadow_field(Machine &m);
    // World 3's warp sparkle (map 122) in GB shades over its cell's GB art; released when it isn't animating.
    void draw_field_sparkle(emu::Wsc &w, const Map &gm, int id);
    void release_field_sparkle(emu::Wsc &w);
    std::array<int, 4> sparkle_tiles_{-1, -1, -1, -1}; // the borrowed upper-bank tiles (0x200 + index)
    bool in_field(const emu::Wsc &w, const std::vector<uint16_t> &set) const;
    void draw_player(Machine &m, uint16_t oam);
    void release_player(emu::Wsc &w);
    static void npc_hook(Machine &m, uint16_t record, uint16_t oam, uint8_t pose);
    void draw_npc(Machine &m, uint16_t record, uint16_t oam, uint8_t pose);
    void shadow_npcs(Machine &m);
    std::array<uint8_t, 128> actor_tiles(int gb, int set, int pose) const;
    struct NpcSprite {
        int palette = 0, map = -1;
        uint16_t tile = 0;
        uint16_t oam = 0;
        std::array<uint8_t, 16> original_oam{};
        std::array<uint8_t, 128> tiles{};
        std::array<uint8_t, 128> original{};
    };
    std::map<std::pair<uint16_t, uint16_t>, NpcSprite> npcs_; // native record, tile buffer
    std::vector<uint16_t> npc_tiles_;
    std::vector<uint16_t> npc_palettes_;
    std::array<int, 64> sprite_of_set_{};
    std::map<int, std::vector<std::array<uint8_t, 128>>> objects_;
    std::array<uint8_t, 128> corpse_{}; // the GB lying body, as the GB draws it
    bool has_corpse_ = false;
    std::array<uint8_t, 128> hand_{};
    bool has_hand_ = false;
    void paint_portrait(Machine &m, int group, uint16_t at);
    void release_portrait(emu::Wsc &w, int group);
    void shadow_portraits(Machine &m);
    // The battle panels' member pictures (2x2 cells each): the member's GB sprite facing down.
    void shadow_party_icons(Machine &m);
    struct Icon {
        std::array<uint16_t, 4> cells{}; // the game's words while ours shows (0: none)
        int pal = -1;
    };
    std::array<Icon, 4> icons_{};
    void update_palette_swaps(Machine &m);
    void install_battle_hooks(emu::Wsc &w);
    void shadow_battlefield(emu::Wsc &w);
    emu::Wsc *battle_owner_ = nullptr;
    std::vector<hooks::Subscription> battle_subscriptions_;
    int battle_animation_ = 0;
    bool party_hp_loss_ = false;
    std::future<void> battle_loading_;
    std::map<int, GbBattleEffect> battle_effects_;
    std::array<int, 256> battle_effect_of_action_{};
    std::array<int, 256> battle_effect_flags_{};
    int effect_id_ = 0, effect_target_ = 0, effect_action_ = 0, effect_duration_ = 1;
    uint64_t effect_start_ = 0;
    bool effect_played_ = false;
    void begin_battle_effect(Machine &m, const hooks::Args &a);
    void apply_battle_effect(emu::Wsc &w);
    bool battle_active_ = false, battlefield_ready_ = false;
    uint64_t battle_generation_ = 0;

    GbIntro intro_;
    void draw_intro_map(int id, int x, int y, std::vector<uint16_t> &image, int repeat_top) const;
    bool enabled_ = false;
    std::map<int, Map> maps_;
    int field_map_ = -1; // the map whose GB tiles are in the shadow
    size_t field_tile_bytes_ = 0;
    std::vector<std::array<uint8_t, 4 * 256>> sprites_; // per GB sprite: 4 poses of 16 x 16 colors
    std::array<int, 256> sprite_of_type_{};             // character type -> GB sprite, -1: none
    bool player_shown_ = false;
    void release_icon(emu::Wsc &w, int member);
    bool has_font_ = false;

  public:
    // The GB picture's glyph for a WSC font code (a WSC tile), or nullptr.
    const std::array<uint8_t, 32> *gb_glyph(int code) const {
        return has_font_ && code >= 0 && code < 0xE0 && font_has_[size_t(code)] ? &font_[size_t(code)]
                                                                                : nullptr;
    }

  private:
    std::array<std::array<uint8_t, 32>, 0xE0> font_{}; // per WSC code: the GB glyph as a WSC tile
    std::array<bool, 0xE0> font_has_{};                // a GB glyph for the code
    std::map<int, Pic> pics_;
    std::array<int, 256> pic_of_type_{};
    std::array<Portrait, 3> portraits_;
    std::vector<emu::Wsc::PaletteSwap> backdrop_swaps_; // the battle backdrop's palettes, shown white
    std::vector<uint16_t> title_;                       // the GB title picture (12-bit greys), or empty
};

} // namespace sawgod::game
