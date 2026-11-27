// Hook points in the decompiled game code (the SoH/2Ship GameInteractor model): features attach here
// instead of editing game logic.
//
// - Events: notifications that something happened (battle started, ...). emit() calls the subscribers.
// - Values: a number the game is about to use (damage dealt, gold awarded, ...). filter() passes the original
//   value through every subscriber in order; with none, it returns it unchanged.
// - Vanilla behaviour (VB): a yes/no decision the game is about to make (should a random encounter start?).
//   should() returns the original decision unless a subscriber overrides it.
//
// Job number one: with nothing subscribed every call is a no-op and the game is exactly the WSC version
// (the difftests run with no subscribers). Subscribers are added by options, mods and tests.
//
// Ownership: each machine's game::Session (session.h) owns one Registry; subscribe through
// Session::of(wsc).hooks(). A subscription lives as long as its Subscription token (move-only; dropping it
// unsubscribes), so a feature's teardown is explicit. The registry outlives game resets: an option's
// subscriptions stay across Reset Game and the game's own restarts.
//
// Dispatch rules (safe for any subscriber to rely on):
// - Subscribers run in subscription order.
// - A subscriber may subscribe, unsubscribe (itself or others), clear(), or cause another hook call (nested
//   dispatch) while it runs. Removals take effect at once: a removed subscriber is never called again, even
//   later in the dispatch that is running. Additions take effect from the next dispatch.
// - A subscriber that removes itself stays alive until it returns.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace sawgod::game {
class Machine;
}

namespace sawgod::game::hooks {

enum class Event : uint8_t {
    BattleStart, // run_battle entered (before setup)
    BattleEnd,   // run_battle returning normally (after rewards); NOT sent on a party wipeout, which restarts
                 // the game from main instead of returning (a new Session generation)
    PresentationField, // a=1 ordinary field movement/idle draw, a=0 event/menu dispatch; observation only
    // The service display rebased both field rings to scroll (8,0); a/b are the old packed SCR1/SCR2 scrolls.
    FieldViewRebased, // observation only, after the native copies
    FieldIdle, // the party stands still in the field with no event pending (process_field_action, each frame)
    BattleMenu, // menu rebuilt: a = state (0 fight/run, 1 equipment, 2 enemy target, 4 ally target)
    BattleRoundStart,
    BattleRoundEnd,
    BattleHpChange,          // a = actor, b = signed damage (negative heals), c = previous HP
    BattleAutoTargetSetting, // a = 1 entering the native selector, 0 leaving; read-only presentation
    BattleRewards,           // reward/transform screens begin; their existing menus remain visible
    BattleAction,            // next action announcement begins
    BattleAnimation,    // a native battle animation starts: a = its ROM entry (bank 37), b = palette variant
                        // (DC12), c = acting action/item id (DBFC; DBFD is the defender's action)
    BattleAnimationEnd, // that animation returned: a = its ROM entry
    BattleShake,        // party-hit presentation: a = 1 begin / 0 end; native duration is 16 VBlanks
    StatusPopup,        // a = source segment, b = source offset, c = popup row counter
    // a = 1 browsing, 2 item move/use destination, 0 leaving/suspended for native dialogs
    InventoryMenu,
    // a = PartyUi::Scene (0 hidden, 1 field categories, 2 status members, 3 status list,
    // 4 equip members, 5 equip source, 6 equip destination, 7 order source, 8 order destination: 0xD840 the
    // picked member, 0xD842 the hovered one; 9 a Status row picked, waiting for A (use) or B: 0xD840 the
    // row; 10 the order swap animation, until the next order source). b = 1 begin nested scope, -1 return to
    // parent,
    // 2 hand off to a submenu, 0 scene update.
    // Presentation only; native fallback is unchanged.
    PartyMenu,
    // A host party category was activated: a = 0 Options.
    PartyMenuAction,
    // An inventory/equipment item dialog (presentation only; native fallback unchanged): a = 0 closed, 1
    // actor target (item_target_selector), 2 refill (item_refill_selector), 3 floor travel
    // (item_destination_selector), 4 overflow discard (discard_overflow_inventory_item), 5 Crystals
    // (select_class_action's special inventory row; D840 choice, D0AB high nibble availability). b = origin:
    // 0 the inventory (D840 its item slot, or Crystal choice), 1 an equipped or Status item (D854 its list,
    // D840 its slot), 2 an item granted in the field.
    ItemDialog,
    // A native item message: a = 0 dismissed, 1 unusable/no effect
    // (wait_unusable_item_message), 2 already wearing equipment (wait_equipment_transfer_error),
    // 3 stat gain (b = existing localized TextKey, c = actual amount); replaced on the next item use or exit.
    ItemMessage,
    // The meat reward menu (run_transformation_menu): a = 0 closed, 1 the drop announcement (waits for A),
    // 2 Eat/Don't (0xD714), 3 eater selection (0xD713; the native preview fills 0xD715/0xD716 and its stat
    // buffers when the Config option shows it), 4 the transformation or "no effect" result, 5 the chosen
    // member can't eat meat (a human or mutant: the native error sound; the stage stays 3).
    MeatMenu,
    // the sound driver was asked for a song (sound_request, as wait_vblank forwards music_request 0xC820):
    // a = the song id. Its first notes start on the driver's next frame (sound_tick).
    MusicRequest,
    // Accepted sound dispatch: a original ID, b 0 queued / 1 immediate, c actual
    // reserved logical-slot mask. Track pointers are readable now at 062C+slot*32h+3.
    SoundRequest,
    SoundReset,   // sound_init completed; observation only
    SoundContext, // a AudioContext (push), b action/item ID; a=0 pops the nested scope
    // character creation (new_game_setup, new game and Guild): a = 1 the type list is up, 0 it is left or
    // suspended for name entry. Presentation only.
    CreationMenu,
    // name entry (edit_creation_name): a = 1 the editor is up, 0 it is left. Presentation only.
    NameEntry,
    // the Bestiary (run_monster_history_menu): a = 1 member selection, 2 history list (browse_bestiary_list),
    // 3 the no-monsters notice, 4 catalog categories, 5 catalog grid, 6 type detail, 0 left.
    // Presentation only; native routines own every selector, page and discovery flag.
    Bestiary,
    // a: 0 close, 1 open, 2 typed token (b opcode, c Unicode), 3 acknowledgement (b bool),
    // 4 projected party-name glyph (b member, c index), 5 native chest message (b = 1 full,
    // 0 acquired; c item id): emitted as the item's outcome is known, before the native message types out,
    // and again after it. Observation only.
    Dialogue,
    // a = ServiceUi scene (0 yields to character creation), b: 1 push, -1 pop, 0 replace top.
    // a = 13 retains the current service layout and displays notice c (TextKey) instead of its footer.
    // Native selectors remain authoritative.
    ServiceMenu,
    // a = SaveUi scene, b: 1 push, -1 pop, 0 replace top; native saves stay unchanged.
    SaveMenu,
    // Presentation only: 0 invalidates a submitted picture, 1 input-loop picture ready,
    // 2 field upload ready; 3/4 begin/end native palette fade (polls there are not layout commits);
    // 5 battle animation/update upload ready (no input loop required).
    PresentationPicture,
    IntroPresentation,     // observation only: IntroStage, caption id/fade level, reveal frames (gb_intro.h)
    IntroCaptionStrip,     // observation only: caption id, copied plane columns, total columns
    EndingNotice,          // observation only: 1 the ending's New Game+ notice is drawn, 0 it has faded out
    IntroClosingMusicFade, // observation only: 1 starts closing music fade, 0 leaves the scene
    // Observation only: a = visible word rows (1..3), 0 finishes the native menu fade or leaves the prompt;
    // b = 1 while PUSH ANY BUTTON waits (a = 0).
    TitleMenu,
    SceneFrame, // presentation-only screen requests, before the first visible raster row
    // A scripted full-width field overlay (animate_intro_screen_overlay_progress, e.g. the story lightning)
    // owns the picture: a = 1 entered, 0 finished. Presentation only.
    ScriptedFieldOverlay,
    // Native boss defeat raster/fade owns the battlefield: a = 1 entered, 2 planes cleared,
    // 3 portrait's left portion copied onto SCR1, 0 finished; b = defeated group on entry/exit,
    // source split in pixels on the copy observation (SCR2 retains the right portion).
    // Host chrome stays retained; this observation never changes machine state.
    BattleDefeatEffect,
    // The travel warp (animate_destination_warp) streams a per-line horizontal scroll distortion over the
    // whole picture: a = 1 entered, 0 finished. Presentation only.
    TravelWarpEffect,
    // Title Continue loaded a save and is entering game_main. The GB view drops what it kept from before the
    // load (title palette binding, holds, title pages). Observation only.
    SaveLoaded,
    Count
};

enum class Value : uint8_t {
    // Extra party categories: 0 (native default), 1 Options before Save, 2 also Load after Save.
    PartyMenuRows,
    // Signed 16-bit damage (negative heals) passed to set_battle_meter_target, where every battle damage
    // path ends. a = actor index (0-3 party, 4+ enemies), b = its current HP, c = acting index. The filter
    // value preserves the raw uint16_t DX bits; subscribers interpret them as int16_t (see cheats.h).
    BattleMeterChange,
    // The number of title menu choices (title_menu): 3 in the original (new game, continue, ORIJINARU). The
    // menu draws that many rows and wraps its cursor within them, so 2 hides ORIJINARU. No Args.
    TitleMenuOptions,
    // A stat gain about to be added to a party member (before the game's caps: 99 for stats, 999 for max HP).
    // a = the stat (member record offset: 8 max HP, 0Bh-0Eh the stats), b = where: 0 a mutant's growth after
    // a
    // battle (grow_mutants_after_battle), 1 a human's stat or HP item (use_field_*). A changed gain that
    // would
    // pass the cap is capped (the original drops a mutant's gain that would pass 99).
    StatGain,
    // A tower floor to teleport to now (0: none), asked whenever the party stands still in the field with no
    // event pending (process_field_action). The game does it as the Door does (animate_destination_warp, the
    // floor's entry from 5000:8945, then the map reloaded) when 1 <= floor <= the highest reached (0xD0DA)
    // and
    // the party is on foot. a = the highest floor reached.
    FieldTeleport,
    InventoryKeys, // top-level inventory key poll only; original is the repeated keypad bitmask
    // An enemy's gold reward about to be added after a victory (award_battle_gold; the game caps the total
    // and
    // the party's money at 999999). a = the enemy actor.
    BattleGold,
    // The repeated keypad bitmask the creation type list (new_game_setup) is about to act on. No Args.
    CreationKeys,
    // The repeated keypad bitmask name entry (edit_creation_name) is about to act on. No Args.
    NameEntryKeys,
    // The glyph name entry is about to add: the original is the charset code under the native grid cursor
    // (the sheet of page 0xD854, row 0xD843, column 0xD842). A subscriber may return another code, or
    // code | glyph << 8 to add a character the game's sheets lack (PlayerNames::NameGlyph): the machine
    // bytes then hold `code` in its place. No Args.
    NameEntryGlyph,
    // The repeated keypad bitmask the Bestiary's history list (browse_bestiary_list) is about to act on. No
    // Args.
    BestiaryKeys,
    // The repeated keypad bitmask a service's two-choice confirmation (a shop's buy and sell, the Inn's stay,
    // the Guild's replacement) is about to act on; the host lays the two side by side, so it may add Up/Down
    // for Left/Right. No Args.
    ServiceChoiceKeys,
    // Pressed keys while acknowledging save completion; the original accepts exactly A.
    SaveCompleteKeys,
    // Repeated keys at the party category list; host requests may leave it before field actions resume.
    PartyMenuKeys,
    // HP a party member is about to lose in the field (positive; heals never pass here). a = the source:
    // 0 a damage floor (process_player_cell_entry, 1..3 to each living member), 1 a dialogue script's HP
    // adjustment (dialogue_adjust_member_hp; b = the member record). 0 leaves the HP unchanged.
    FieldDamage,
    Count
};

enum class AudioContext : int32_t { BattleAction = 1, FieldItem, Warp, BattleEntry, DamageFloor, Death };

enum class VB : uint8_t {
    // Should an action spend one use of the item/ability it uses? a = where: 0 a battle action
    // (execute_order_entry; b = the actor, 0-3 party, c = the item/ability id), 1 an item used from the field
    // menu (use_item_on_actor; c = the id), 2 the refill item's charge (refill_selected_equipment_item).
    // Unlimited (FEh) uses are never spent either way.
    SpendUse,
    // Should reviving a dead member spend one of its hearts (lives)? a = 0 the field revive item
    // (use_field_revive_item), 1 the paid revive menu (revive_selected_member).
    SpendHeart,
    // Should a status-inflicting hit's statuses land on the target? a = the target actor (0-3 party, 4+
    // enemies), b = the status bits it adds (1 knocked out, 2 stone, 10h poison, ...). False takes the game's
    // own "no effect" path (as when the target already has them).
    InflictStatus,
    // Should a random encounter start? Asked when the field's encounter roll hits (roll_field_encounter);
    // false: no battle (the step goes on). a = the terrain's encounter class.
    RandomEncounter,
    // Should the opening (the logos and the story) and the title's reveal play before the title menu? Asked
    // by
    // main each time round its title loop (also after the menu's timeout); false goes straight to the menu.
    PlayIntro,
    // Should the party menu offer its Config tab? Asked when the menu draws its tab list
    // (draw_party_overview) and moves through it (select_party_menu_category); false: the tab is left out
    // (Save moves up a row and the window is a row shorter) -- the app has the Config settings in Options >
    // Gameplay instead (game_config.h).
    ConfigTab,
    // Map run gate in prepare_player_scroll; scripts and the B rule still apply. a = travel mode (0 on foot,
    // 1 the World 2 island, 2 the glider, 3 the bike); vanilla is false for every vehicle (the original runs
    // on foot only), and a true answer for a vehicle runs it at the doubled step too.
    RunningAllowed,
    // Should the Bestiary's member history open (rather than its no-monsters notice)? Asked at
    // run_monster_history_menu's gate; vanilla is the original's answer, which takes an absent party slot
    // (trait FFh) for a monster. No Args.
    BestiaryHistory,
    // May an effect reduce this actor's HP/stat or add status bits at the final commit? Covers direct
    // kills, scripted/group effects and self costs too. a = actor, b = record field, c = proposed value.
    HarmBattleActor,
    // After a completed Equip transfer whose source was the gear list and whose destination was in the
    // Items/Key Items list, go back to the gear list (as the Left key does)? The original stays in the
    // Items list (vanilla false). No Args.
    EquipReturnToGear,
    Count
};

// Extra context for a hook call; meaning per hook as documented above.
struct Args {
    int32_t a = 0, b = 0, c = 0;
};

using EventFn = std::function<void(Machine &, const Args &)>;
using ValueFn = std::function<int32_t(Machine &, int32_t value, const Args &)>;
using VbFn = std::function<bool(Machine &, bool should, const Args &)>;

struct Core; // the subscriber lists (hooks.cpp)

// A live subscription: unsubscribes when reset or destroyed. Safe to outlive its Registry.
class Subscription {
  public:
    Subscription() = default;
    Subscription(Subscription &&o) noexcept;
    Subscription &operator=(Subscription &&o) noexcept;
    Subscription(const Subscription &) = delete;
    Subscription &operator=(const Subscription &) = delete;
    ~Subscription() { reset(); }

    void reset();
    bool active() const;

  private:
    friend class Registry;
    Subscription(std::weak_ptr<Core> core, int id) : core_(std::move(core)), id_(id) {}
    std::weak_ptr<Core> core_;
    int id_ = 0;
};

class Registry {
  public:
    Registry();
    ~Registry();
    Registry(const Registry &) = delete;
    Registry &operator=(const Registry &) = delete;

    [[nodiscard]] Subscription subscribe(Event e, EventFn fn);
    [[nodiscard]] Subscription subscribe(Value v, ValueFn fn);
    [[nodiscard]] Subscription subscribe(VB v, VbFn fn);
    void clear();        // remove every subscriber (their tokens become inactive)
    size_t size() const; // live subscribers

    void emit(Event e, Machine &m, const Args &args = {});
    int32_t filter(Value v, Machine &m, int32_t value, const Args &args = {});
    bool should(VB v, Machine &m, bool vanilla = true, const Args &args = {});

  private:
    std::shared_ptr<Core> core_;
};

// Game-side calls: the running machine's Session registry. Cheap when nothing is subscribed, and a no-op on
// a machine with no Session (the differential tests' own driver).
void emit(Event e, Machine &m, const Args &args = {});
int32_t filter(Value v, Machine &m, int32_t value, const Args &args = {});
bool should(VB v, Machine &m, bool vanilla = true, const Args &args = {});

// A presentation scope: emits `e` with `open` now and `close` (default a = 0) when the scope ends, however
// it ends (a return, or the stack unwinding when a reset abandons the run).
class EventScope {
  public:
    EventScope(Machine &m, Event e, const Args &open, const Args &close = {}) : m_(m), e_(e), close_(close) {
        emit(e, m, open);
    }
    ~EventScope() { emit(e_, m_, close_); }
    EventScope(const EventScope &) = delete;
    EventScope &operator=(const EventScope &) = delete;

  private:
    Machine &m_;
    Event e_;
    Args close_;
};

} // namespace sawgod::game::hooks
