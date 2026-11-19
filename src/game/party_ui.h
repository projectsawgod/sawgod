// Party, status, and equipment snapshots and host-drawn panels.
#pragma once
#include "game/hooks.h"
#include "game/text_keys.h"
#include "game/ui_contract.h"
#include "game/ui_detail.h"
#include "game/ui_items.h"
#include <array>
#include <vector>

namespace sawgod::game {
// Host picture pass for the field category menu, Status, and Equip. Native selectors/actions
// retain ownership of party records, item records and selection bytes.
std::u32string party_name(emu::Wsc &w, int member);
std::u32string party_type(emu::Wsc &w, int member);
void party_row(Chrome &c, emu::Wsc &w, int member, int y, bool selected, bool second, bool compact = false);
// Four-member stacks share their pitch and footer across field, order, targets, and services.
inline constexpr int kPartyRowHeight = 25, kPartyFooterY = 16 + 4 * kPartyRowHeight;
inline constexpr int kPartyFooterHeight = 144 - kPartyFooterY;
void party_gold(Chrome &c, emu::Wsc &w, bool member_stack = false);
void party_stats(Chrome &c, emu::Wsc &w, int member);
std::u32string menu_text(emu::Wsc &w, TextKey key);

class PartyUi {
  public:
    // Keep this declaration with the screens: adding an element without test evidence fails CTest
    // Selectors wrap; panes use render obligations.
    static constexpr UiElement kElements[] = {
        // Field menu: native categories plus opt-in Options/Load; eight in the app without Config.
        {"field.cursor", "activate cancel wrap"},
        {"field.items", "activate cancel"},
        {"field.status", "activate cancel"},
        {"field.equip", "activate cancel"},
        {"field.order", "activate cancel"},
        {"field.bestiary", "activate cancel"},
        {"field.config", "activate cancel hidden"},
        {"field.load", "activate cancel"},
        {"field.options", "activate cancel"},
        {"field.save", "activate cancel"},
        {"field.overview", "render empty"}, // member rows (empty slots), floors, gold, play time
        // Status: member chooser, the member's eight slots, and using an item or ability from them.
        {"status.member",
         "activate cancel wrap refused"}, // refused: dead, stoned and empty slots are skipped
        {"status.list", "activate cancel wrap"},
        {"status.switch", "activate wrap refused"}, // Y2/Y4
        {"status.pick", "activate cancel refused"}, // the picked row: empty slot, no uses left
        {"status.target", "activate cancel wrap refused"},
        {"status.message", "activate cancel"},
        {"status.refill", "activate cancel wrap refused"},
        {"status.travel", "activate cancel clamp"},
        {"status.abilities", "activate refused render"}, // a mutant's four and a monster's eight
        {"status.detail", "render reset"},
        // Equip: member chooser, gear and Items/Key Items lists, the destination of a picked row.
        {"equip.member", "activate cancel wrap"},
        {"equip.gear", "activate cancel wrap refused"}, // refused: a mutant's ability slots, a monster's list
        {"equip.inventory", "activate cancel wrap refused"},
        {"equip.columns", "activate wrap refused"}, // Right/Left: Gear, Items, Key Items
        {"equip.switch", "activate wrap refused"},  // Y2/Y4
        {"equip.destination", "activate cancel wrap refused"},
        {"equip.message", "activate cancel"}, // already wearing one
        {"equip.abilities", "render refused"},
        {"equip.detail", "render reset"},
        // Party order.
        {"order.source", "activate cancel wrap refused"},
        {"order.destination", "activate cancel wrap refused"},
        {"order.stats", "render"},
    };
    // Rows with their own obligations, one set each: the four members in the three choosers (activate,
    // cancel) and the eight slots of the Status and Equip lists (activate, cancel, refused).
    static constexpr std::string_view kMemberRows[] = {"status.member", "equip.member", "order.member"};
    static constexpr std::string_view kSlotRows[] = {"status.row", "equip.row"};
    static constexpr std::string_view kInventoryRows[] = {"equip.item", "equip.key"};
    enum class Scene {
        Hidden,
        Field,
        StatusMembers,
        Status,
        EquipMembers,
        Equip,
        EquipDestination,
        OrderSource,
        OrderDestination,
        StatusPicked, // a Status row picked: the same screen, a hand on the row, the key hint below
        OrderSwap,    // the two members change places: the native swap animation and its "swapped" label
    };
    struct View {
        int member = 0, selected = 0, first = 0, count = 8, base = 0;
        bool mutant = false, monster = false, inventory = false, key = false, abilities = false;
        std::array<ItemRow, 8> rows;
        ItemRow detail;
    };
    void install(emu::Wsc &w);
    void frame(emu::Wsc &w);
    bool active() const { return scene_ != Scene::Hidden; }
    // Input classification follows the logical parent scope, independently of retained pixels.
    bool input_active() const { return active() || suspended_; }
    Scene scene() const { return scene_; }
    View view(emu::Wsc &w);
    std::vector<uint32_t> picture(emu::Wsc &w, emu::Wsc::ColorMode mode, bool second = false);

  private:
    std::vector<uint32_t> picture_now(emu::Wsc &w, emu::Wsc::ColorMode mode, bool second);

    bool suspended_ = false;
    Scene held_scene_ = Scene::Hidden;
    std::vector<Scene> parents_;
    std::vector<hooks::Subscription> subscriptions_;
    Scene scene_ = Scene::Hidden;
    uint64_t generation_ = 0;
    std::array<int, 2> first_{};
    DetailLine detail_;
};
// One party row: a 164x24 window at y with the member's sprite, name, type, HP and hearts.
// `choosing` shifts the sprite and text right of the native list arrow and draws it; `picked` (a member
// being moved) shifts them further, right of the native 16x16 hand.
void draw_party_row(Chrome &c, emu::Wsc &w, int slot, int y, bool choosing, bool second, bool picked = false);
} // namespace sawgod::game
