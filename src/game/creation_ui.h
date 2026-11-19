// Character-creation snapshots, name entry, and host-drawn selection panels.
#pragma once

#include <array>
#include <string>
#include <vector>

#include "emu/wsc.h"
#include "game/hooks.h"
#include "game/ui_contract.h"
#include "game/ui_detail.h"
#include "game/ui_items.h"
#include "game/ui_sprites.h"

namespace sawgod::game {

// Character creation (the type list and its starting stats/gear) and name entry, as host pictures over the
// native screens. The native loops keep the selection bytes, the party record and every sound; the type
// list only adds a host detail page (Right opens it, Left or B closes it) whose own cursor walks the
// starting gear over the shared description line, and name entry keeps its own grid cursor, handing the
// native editor the glyph, delete or done that the chosen key stands for.
class CreationUi {
  public:
    // Logical rows stay stable across the native creation pools; fixed labels use render obligations.
    static constexpr UiElement kElements[] = {{"types", "activate cancel wrap"},
                                              {"types.pages", "activate cancel clamp"},
                                              {"types.scroll", "render"},
                                              {"selected-type", "render"},
                                              {"detail", "activate cancel proceed"},
                                              {"detail.gear", "activate wrap render empty"},
                                              {"detail.line", "render reset overflow"},
                                              {"gear.names", "overflow"},
                                              {"name.glyphs", "activate handoff projection"},
                                              {"name.cancel", "cancel"},
                                              {"name.pages", "activate wrap render"},
                                              {"name.grid", "wrap render"},
                                              {"name.buttons", "wrap cancel"},
                                              {"name.space", "activate cancel refused"},
                                              {"name.delete", "activate cancel refused"},
                                              {"name.done", "activate cancel empty"},
                                              {"name.limit", "refused"},
                                              {"name.counter", "render"},
                                              {"name.host-name", "activate overflow"},
                                              {"type.row.0", "activate cancel"},
                                              {"type.row.1", "activate cancel"},
                                              {"type.row.2", "activate cancel"},
                                              {"type.row.3", "activate cancel"},
                                              {"type.row.4", "activate cancel"},
                                              {"type.row.5", "activate cancel"},
                                              {"type.row.6", "activate cancel"},
                                              {"type.row.7", "activate cancel"}};
    enum class Scene { Hidden, Types, Name };
    static constexpr int kTypes = 8, kVisibleTypes = 7;
    // One creation type as the native preview reads it (creation_template_core, the preview's sprite).
    struct Type {
        uint8_t id = 0;
        std::u32string name;
        int hp = 0, hearts = 0;
        std::array<int, 4> stats{};
        bool monster = false, mutant = false;
        int gear_count = 0;
        std::array<ItemRow, 8> gear;
        TypeSprite sprite; // its 16x16 menu sprite
    };
    struct TypesView {
        int selected = 0, first = 0;
        bool detail = false;
        int gear = 0;    // the detail page's cursor: a row of the selected type's gear
        ItemRow hovered; // that row (its flavor and stats fill the description line)
        int fade = 0;    // the native palette fade, as steps darker (applied to the host-read sprite colors)
        std::array<Type, kTypes> types;
    };
    // A key of the name grid: what it shows, the charset code the game stores and a host-only glyph.
    struct Key {
        char32_t shown = 0;
        uint8_t code = 0;
        char32_t text = 0;
    };
    struct Page {
        int kana = 0; // 0 Latin, 1 hiragana, 2 katakana
        int part = 0, parts = 1;
        int columns = 13;
        std::vector<std::vector<Key>> rows;
    };
    struct NameView {
        int page = 0, pages = 1, row = 0, column = 0, button = 0;
        bool buttons = false; // the cursor is on Space / Delete / Done
        std::u32string name;
        int length = 0;
    };

    void install(emu::Wsc &w);
    void frame(emu::Wsc &w);
    bool active() const { return scene_ != Scene::Hidden; }
    Scene scene() const { return scene_; }
    TypesView types(emu::Wsc &w);
    NameView name(emu::Wsc &w);
    const std::vector<Page> &pages(emu::Wsc &w);
    uint16_t creation_keys(emu::Wsc &w, uint16_t original);
    uint16_t name_keys(emu::Wsc &w, uint16_t original);
    int32_t glyph(int32_t original);
    std::vector<uint32_t> picture(emu::Wsc &w, emu::Wsc::ColorMode mode, bool second = false);

  private:
    void reset();
    void renew(emu::Wsc &w); // a new game or a loaded state starts hidden
    void build_pages(emu::Wsc &w);
    const Key *key_at() const;
    void clamp_cursor();
    std::vector<hooks::Subscription> subscriptions_;
    Scene scene_ = Scene::Hidden;
    bool creation_ = false, naming_ = false, detail_ = false;
    bool resuming_ = false; // name entry just left: the list comes back as it was
    int first_ = 0, gear_ = 0;
    DetailLine line_; // the detail page's description
    uint64_t generation_ = 0;
    std::vector<Page> pages_;
    bool latin_only_ = false; // a proportional (English) language: the Latin page only
    int space_ = 0xD9;        // the native sheets' blank key
    int page_ = 0, row_ = 0, column_ = 0, button_ = 0;
    int32_t pending_ = -1; // the glyph the next native append takes
};

} // namespace sawgod::game
