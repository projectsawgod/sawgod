// Bestiary snapshots, paging, and host-drawn monster details.
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

// The Bestiary (the field menu's per-member transformation history, run_monster_history_menu and
// browse_bestiary_list) as a host picture over the native screens. The native loops keep the member
// (0xD846), the history cursor (0xD847) and scroll (0xD853) and every key; the picture shows seven rows of
// the history around the native selection.
class BestiaryUi {
  public:
    // History rows are read-only; A does not open a detail page. Native selectors clamp,
    // rather than wrapping, in History; member selection wraps and skips non-monsters.
    static constexpr UiElement kElements[] = {{"catalog.categories", "activate cancel wrap ignored"},
                                              {"catalog.pages", "wrap"},
                                              {"catalog.grid", "wrap cancel refused"},
                                              {"catalog.preview", "seen unseen"},
                                              {"catalog.layout", "render"},
                                              {"detail", "activate cancel refresh"},
                                              {"detail.rows", "wrap"},
                                              {"detail.pages", "wrap skip"},
                                              {"entry", "activate cancel refused"},
                                              {"members", "activate cancel wrap skip"},
                                              {"member.0", "activate cancel skip"},
                                              {"member.1", "activate cancel skip"},
                                              {"member.2", "activate cancel skip"},
                                              {"member.3", "activate cancel skip"},
                                              {"history", "activate cancel clamp ignored scroll reset empty"},
                                              {"notice", "render dismiss"},
                                              {"title", "render overflow"},
                                              {"member.summary", "render overflow"},
                                              {"generation", "render overflow"},
                                              {"history.row.0", "render content"},
                                              {"history.row.1", "render content"},
                                              {"history.row.2", "render content"},
                                              {"history.row.3", "render content"},
                                              {"history.row.4", "render content"},
                                              {"history.row.5", "render content"},
                                              {"history.row.6", "render content"},
                                              {"history.arrows", "render scroll"},
                                              {"history.cursor", "render clamp"},
                                              {"discovery.flags", "preserve independent"},
                                              {"session", "reset"}};
    static constexpr int kCatalogEntries = 150, kCatalogPages = 5, kCatalogColumns = 6, kCatalogRows = 5;
    enum class Scene { Hidden, Members, History, Empty, Catalog, Grid, Detail };
    static constexpr int kRows = 7;
    struct Row {
        std::u32string name; // empty: no entry
        int generation = 0;
    };
    struct View {
        int member = 0, generation = 0, selected = -1, first = 0, total = 0;
        std::u32string name, type;
        TypeSprite sprite;
        std::array<Row, kRows> rows;
    };

    struct CatalogView {
        int page = 0, row = 0, column = 0, category = 0, selected = 0, type = 0;
        bool seen = false;
        std::u32string name, family;
        std::array<int, 5> stats{}; // HP, STR, DEF, AGI, MAG, using native display clamps
        std::array<ItemRow, 8> abilities;
        int ability_count = 0;
    };
    CatalogView catalog_view(emu::Wsc &w);
    struct GridLayout {
        std::array<int, 5> labels, icons;
        // The catalog's split (about 15 px of gaps between the six monsters, 3 px each):
        // the left pane is as wide as its stats need -- labels at kStatsX, two clear pixels, the widest
        // value, 1 px clear, the frame -- and the grid's cells take what remains: `gap` pixels apart
        // (3 at most), after an 8 px gutter for the arrow with 1 px clear on each side of it.
        // English labels (STR) leave a 96 px pane and 3 px gaps; four-kana labels need 106 px: 1 px gaps.
        int pane = 111, gap = 0;
        int x(int column) const { return pane + 13 + column * (16 + gap); }
    };
    // Tight, shadowless family-label ink bands above full-size 16 px icons. Native kana and English
    // descenders keep their full strokes; neither font metrics nor glyph shapes are changed.
    GridLayout grid_layout(emu::Wsc &w);
    // Catalog: the stats beside the 48 px portrait (which the large pictures fill, x=5..52); the title
    // after its category-arrow gutter; detail: the first ability row.
    static constexpr int kStatsX = 54, kTitleX = 16, kAbilityY = 36;
    static constexpr int kGridArrow = 9; // the grid arrow's x before its cell: 8 px and 1 px clear
    void install(emu::Wsc &w);
    void frame(emu::Wsc &w);
    bool active() const { return scene_ != Scene::Hidden; }
    Scene scene() const { return scene_; }
    View view(emu::Wsc &w);
    // The history list's keys: Down stops at the last entry (the native cursor would go on to its
    // fourteenth row, past a short history, where the seven-row list shows nothing).
    uint16_t keys(emu::Wsc &w, uint16_t original);
    std::vector<uint32_t> picture(emu::Wsc &w, emu::Wsc::ColorMode mode, bool second = false);

  private:
    std::vector<hooks::Subscription> subscriptions_;
    Scene scene_ = Scene::Hidden;
    int first_ = 0;
    uint64_t generation_ = 0;
    DetailLine detail_;
};

} // namespace sawgod::game
