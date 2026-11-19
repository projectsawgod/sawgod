#include "game/bank34_misc.h"
#include "game/bank36_misc.h"
#include "game/bank37.h"
#include "game/battle_actor_state.h"
#include "game/battle_rewards.h"
#include "game/computed_handlers.h"
#include "game/text_render.h"
// Decompiled battle/UI helpers from bank 0x36 (segment 6000h). See asm/bank_36.s.
#include "game/bank36_battle.h"
#include "game/hooks.h"
#include "game/session.h"

#include "game/bank34.h"
#include "game/bank34_misc.h"
#include "game/bank3d_sound.h"
#include "game/tables.h"
#include "game/text.h"

#include <algorithm>
#include <initializer_list>

namespace sawgod::game {
// Cores defined further down (the battle loop and AI), used by earlier routines.
void shake_screen_for_party_hit_core(Machine &m);   // 0x362B62
void draw_battle_member_boxes_core(Machine &m);     // 0x362987
void draw_enemy_group_portraits_core(Machine &m);   // 0x36C877
void position_enemy_target_cursor_core(Machine &m); // 0x3632EC
void select_next_battle_member_core(Machine &m);    // 0x364D3E
void show_party_surprise_message_core(Machine &m);
void show_enemy_surprise_message_core(Machine &m);
void resolve_drain_hit_core(Machine &m);
void resolve_status_hit_core(Machine &m);
void resolve_fixed_damage_hit_core(Machine &m);
void grow_mutants_after_battle_core(Machine &m);
void apply_status_10_effect_core(Machine &m);
void try_cure_status_40_core(Machine &m);
void try_cure_status_80_core(Machine &m);
void place_equipment_column_cursor_core(Machine &m);
void remove_defeated_enemy_group_core(Machine &m);
void sort_party_after_battle_core(Machine &m);
void setup_battle_core(Machine &m);
void fade_in_battle_scene_core(Machine &m);
void battle_opening_core(Machine &m);
void load_battle_actor_graphics_core(Machine &m);
void show_cannot_escape_message_core(Machine &m);         // 0x36AB9C
void show_enemies_eliminated_message_core(Machine &m);    // 0x36A15F
void show_attack_missed_message_core(Machine &m);         // 0x36A36D
void show_enemy_defeat_count_message_core(Machine &m);    // 0x36C3EB
void show_gold_reward_message_core(Machine &m);           // 0x36BBBD
void show_party_victory_message_core(Machine &m);         // 0x36AADC
void show_party_escaped_message_core(Machine &m);         // 0x36AB1B
void show_party_wiped_out_message_core(Machine &m);       // 0x36AA95
void show_target_knocked_out_message_core(Machine &m);    // 0x36A870
void show_target_died_message_core(Machine &m);           // 0x36A8B2
void show_target_group_defeated_message_core(Machine &m); // 0x36C3A9
void show_no_damage_message_core(Machine &m);             // 0x36A399
void show_damage_message_core(Machine &m);                // 0x36A0E9
void show_actor_confused_message_core(Machine &m);        // 0x36A9F1
void show_actor_recovered_message_core(Machine &m);       // 0x36AA33
void show_target_description_message_core(Machine &m);    // 0x36A907

namespace {
constexpr uint16_t kSpriteBuffer = 0xC000;
constexpr uint16_t kSpritePositions = 0xC002;
constexpr uint16_t kPaletteBuffer = 0xC400;
constexpr uint16_t kPaletteTargetBuffer = 0xC600;
constexpr uint16_t kKeysPressed = 0xC804;
constexpr uint16_t kKeysRepeat = 0xC806;
constexpr uint16_t kSavedSceneSp = 0xC818;
constexpr uint16_t kSceneIndex = 0xC81A;
constexpr uint16_t kIntroStrip = 0xC81C;
constexpr uint16_t kMusicRequest = 0xC820;
constexpr uint16_t kDebugForceMeatDrop = 0xC883; // debug_force_meat_drop
constexpr uint16_t kBattleDebug = 0xC884;
constexpr uint16_t kGameState = 0xD000;
constexpr uint16_t kDialogueLeader = 0xD0E3;
constexpr uint16_t kBattleOptions = 0xD0E7;
constexpr uint16_t kBattleTypePointers = 0xD1FB;
constexpr uint16_t kFormationBackTileSource = 0xD500; // back layer base; field mode reuses this workspace
constexpr uint16_t kFormationBackPaletteSource = 0xD502;
constexpr uint16_t kFormationBackPaletteVariant = 0xD508;
constexpr uint16_t kFormationBackPaletteCount = 0xD50B;
constexpr uint16_t kFormationMiddleTileSource = 0xD50C;
constexpr uint16_t kFormationMiddlePaletteSource = 0xD50E;
constexpr uint16_t kFormationMiddlePaletteVariant = 0xD514;
constexpr uint16_t kFormationMiddlePaletteCount = 0xD517;
constexpr uint16_t kFormationFrontTileSource = 0xD518;
constexpr uint16_t kFormationFrontPaletteSource = 0xD51A;
constexpr uint16_t kFormationFrontPaletteVariant = 0xD520;
constexpr uint16_t kFormationFrontPaletteCount = 0xD523;
constexpr uint16_t kMemberPaletteOffsets = 0xD535;
constexpr uint16_t kMemberPaletteSegments = 0xD53D;
constexpr uint16_t kMemberPaletteSlots = 0xD545;
constexpr uint16_t kMemberPaletteVariants = 0xD54D;
constexpr uint16_t kPortraitTileSource = 0xD551; // portrait_tile_source
constexpr uint16_t kPortraitPaletteSource = 0xD553;
constexpr uint16_t kPortraitMapSource = 0xD555; // portrait_map_source
constexpr uint16_t kPortraitTileCount = 0xD557; // portrait_tile_count
constexpr uint16_t kPortraitPaletteBank = 0xD559;
constexpr uint16_t kIconQueue = 0xD55A;
constexpr uint16_t kPopupState = 0xD60E;
constexpr uint16_t kBattleActorIcons = 0xD60F;
constexpr uint16_t kBattleTargetIcons = 0xD62F; // battle_target_icons
constexpr uint16_t kBattleItemIcons = 0xD64F;
constexpr uint16_t kBattleNumberText = 0xD66F;
constexpr uint16_t kFormationId = 0xD6CD;
constexpr uint16_t kFormationSetIndex = 0xD6CE;
constexpr uint16_t kEnemyCount = 0xD6D1;
constexpr uint16_t kInitialEnemyGroupCount = 0xD6D2; // initial_enemy_group_count
constexpr uint16_t kEnemyTypeScratch = 0xD6D3;       // enemy_type_scratch
constexpr uint16_t kEnemyGroupCount = 0xD6D4;
constexpr uint16_t kEnemyGroups = 0xD6D5;
constexpr uint16_t kRolledEnemyGroups = 0xD6DB; // rolled_enemy_groups
constexpr uint16_t kBattleMember = 0xD6E1;
constexpr uint16_t kBattleMenuState = 0xD6E2;
constexpr uint16_t kBattleMenuHistory = 0xD6E3;      // battle_menu_history
constexpr uint16_t kBattleMenuHistoryCount = 0xD70B; // battle_menu_history_count
constexpr uint16_t kFormationKind = 0xD70D;
constexpr uint16_t kBattleFlags = 0xD70E;
constexpr uint16_t kFormationCleared = 0xD70F;
constexpr uint16_t kPanelSpecialMember = 0xD713;
constexpr uint16_t kFormationGraphicsIndex = 0xD717;  // formation_graphics_index
constexpr uint16_t kFormationPaletteVariant = 0xD718; // formation_palette_variant
constexpr uint16_t kMeatSourceType = 0xD719;          // enemy type offered by the meat/transformation menu
constexpr uint16_t kBattleActors = 0xD71A;
constexpr uint16_t kPartySortRecords = 0xDAFA; // party_sort_records
constexpr uint16_t kBattleOrder = 0xDB76;
constexpr uint16_t kBattleStatusSuccessCount = 0xDBF3;
constexpr uint16_t kItemUseCount = 0xDBF4;
constexpr uint16_t kActionGroupType = 0xDBF5; // action_group_type
constexpr uint16_t kBattleKnockoutMode = 0xDBF6;
constexpr uint16_t kBattleFlashCounter = 0xDBF7;
constexpr uint16_t kBattleGroupEliminationScratch = 0xDBF9;
constexpr uint16_t kActionId = 0xDBFC;
constexpr uint16_t kActionEffect = 0xDBFD;
constexpr uint16_t kActingActor = 0xDBFE;
constexpr uint16_t kActionTarget = 0xDBFF;
constexpr uint16_t kTargetGroup = 0xDC00;
constexpr uint16_t kActingEntry = 0xDC01;
constexpr uint16_t kAiDisposition = 0xDC03;
constexpr uint16_t kBattleOpeningDone = 0xDC04;
constexpr uint16_t kOrderComparisonsLeft = 0xDC05; // order_comparisons_left
constexpr uint16_t kTargetEvade = 0xDC06;          // target_evade
constexpr uint16_t kAttackerEvade = 0xDC07;        // attacker_evade
constexpr uint16_t kEvadeChance = 0xDC08;          // evade_chance
constexpr uint16_t kBattleSavedAttackValue =
    0xDC09;                                   // physical: target defense; other actions reuse this byte
constexpr uint16_t kBattleItemPower = 0xDC0A; // physical: attacker strength; other actions reuse this byte
constexpr uint16_t kMeterInputTarget = 0xDC0B;
constexpr uint16_t kMeterTarget = 0xDC0D;
constexpr uint16_t kMeterOldValue = 0xDC0F;
constexpr uint16_t kEffectPaletteVariant = 0xDC12;
constexpr uint16_t kGrowthHappened = 0xDC13;
constexpr uint16_t kSkillFlagsA = 0xDC14;
constexpr uint16_t kSkillFlagsB = 0xDC15;
constexpr uint16_t kDisplayFlickerCounter = 0xDC16;
constexpr uint16_t kBlinkPhase = 0xDC17;
constexpr uint16_t kMemberTargets = 0xDC20;
constexpr uint16_t kCommandPage = 0xDC61;
constexpr uint16_t kEquipmentRow = 0xDC62;
constexpr uint16_t kTargetSlot = 0xDC66;
constexpr uint16_t kMemberActionFlags = 0xDC6A;
constexpr uint16_t kEquipmentPage = 0xDC6E;
constexpr uint16_t kTextCursorPos = 0xDC75;
constexpr uint16_t kDigits = 0xDC7B;
constexpr uint16_t kPaddedBattleDigits = 0xDC83; // padded_battle_digits
constexpr uint16_t kBattleRewardText = 0xDC9B;   // battle_reward_text
constexpr uint16_t kEffectX =
    0xDCBB; // also target-search flag, surprise-item tally, and graphics palette variant
constexpr uint16_t kEffectY =
    0xDCBC; // also target-search cursor, selectable-member count, graphics member, and fled flag
constexpr uint16_t kEffectRowY = 0xDCBD; // also target-search step count
constexpr uint16_t kBattleEndState = 0xDCC2;
constexpr uint16_t kActorGraphicsTile = 0xDCD5;    // actor_graphics_tile
constexpr uint16_t kActorGraphicsPalette = 0xDCD7; // actor_graphics_palette
constexpr uint16_t kDebugMenuRow = 0xDD2A;
constexpr uint16_t kDebugMapId = 0xDD2D;
constexpr uint16_t kDebugBattleId = 0xDD2E;
constexpr uint16_t kDebugAutoWin = 0xDD2F;
constexpr uint16_t kBattleStrengthBonuses = 0xDD5A;
constexpr uint16_t kBattleDefenseBonuses = 0xDD5E;
constexpr uint16_t kBattleAgilityBonuses = 0xDD62;
constexpr uint16_t kMemberSkipFlags = 0xDD66;
constexpr uint16_t kCommandCursorVisible = 0xDD74;
constexpr uint16_t kCursorHidden = 0xDD75;
constexpr uint16_t kTextColorFlag = 0xDD77;
constexpr uint16_t kDissolveTiles = 0xDD78; // dissolve_tiles
constexpr uint16_t kDissolveAny = 0xEC78;
constexpr uint16_t kDissolveStep = 0xEC79;
constexpr uint16_t kSpecialActorPreviousHp = 0xEC7A;       // special_actor_previous_hp
constexpr uint16_t kSpecialActorUnchangedHpCount = 0xEC7C; // special_actor_unchanged_hp_count
constexpr uint16_t kBattleMode = 0xEC7D;
constexpr uint16_t kInitialEnemyCounts = 0xEC7E; // initial_enemy_counts
constexpr uint16_t kPanelSpecialMode = 0xEC81;
constexpr uint16_t kFooterPanelMode = 0xEC83;
constexpr uint16_t kBattleSavedStrength = 0xEC87;
constexpr uint16_t kBattleSavedDefense = 0xEC8B;
constexpr uint16_t kBattleSavedAgility = 0xEC8F;
constexpr uint16_t kBattleSavedMagic = 0xEC93;
constexpr uint16_t kAbilityUsesBefore = 0xEC97; // ability_uses_before
constexpr uint16_t kAbilityUsesAfter = 0xEC98;  // ability_uses_after
constexpr uint16_t kIram = 0;
constexpr uint16_t kDataSeg = 0x9000;
constexpr uint16_t kBattleGlyphs = 0x2080;      // D_392080
constexpr uint16_t kByteTablePointers = 0x41E0; // D_3941E0
constexpr uint16_t kDigitGlyphSeg = 0x8000;     // bank holding the plain 0-29 digit tile pairs (D_381150)
constexpr uint16_t kDigitTileBase = 0x1150;     // D_381150, indexed by digit value * 2

// PUSH A/W + segment saves around the member-panel renderer. SP/FLAGS are intentionally excluded:
// dead stack writes aren't compared, and a palette copy can leave DF cleared.
struct SavedRegisters {
    uint16_t ax, bx, cx, dx, si, di, bp, ds, es, ss;
    explicit SavedRegisters(Machine &m)
        : ax(m.ax()), bx(m.bx()), cx(m.cx()), dx(m.dx()), si(m.si()), di(m.di()), bp(m.bp()), ds(m.ds()),
          es(m.es()), ss(m.ss()) {}
    void restore(Machine &m) const {
        m.ax() = ax;
        m.bx() = bx;
        m.cx() = cx;
        m.dx() = dx;
        m.si() = si;
        m.di() = di;
        m.bp() = bp;
        m.ds() = ds;
        m.es() = es;
        m.ss() = ss;
    }
};

} // namespace
void battle_actor_pointer_core(Machine &m) { m.si() = uint16_t(kBattleActors + uint16_t(m.al()) * 0x20); }
namespace {

void hide_battle_status_sprites_core(Machine &m) {
    m.ax() = 0x90;
    for (uint16_t member = 0; member < 4; ++member) {
        if ((m.dx() & (1 << member)) == 0)
            continue;
        for (uint16_t tile = 0; tile < 4; ++tile)
            m.wr16(m.ss(), uint16_t(kSpriteBuffer + 0x2E + member * 0x10 + tile * 4), m.ax());
    }
}

// Four cursor tile words from D_392080 + 1F6h; DS is saved but ES is left at the data bank. dest_base
// defaults to sprite_buffer (the original's own destination); draw_battle_cursor_tiles_alt (0x3653EF)
// reuses this with dest_base = sprite_buffer+10h instead of duplicating the loop.
} // namespace
void draw_battle_cursor_tiles_core(Machine &m, uint16_t dest_base) {
    m.es() = kDataSeg;
    m.dx() = 0xF00;
    m.si() = uint16_t(kBattleGlyphs + 0x1F6);
    m.di() = dest_base;
    m.cx() = 4;
    do {
        m.ax() = uint16_t(uint16_t(m.rd16(m.es(), m.si()) + m.dx()) | 0x2000);
        m.wr16(kIram, m.di(), m.ax());
        m.di() = uint16_t(m.di() + 4);
        m.si() = uint16_t(m.si() + 2);
    } while (--m.cx() != 0);
}
namespace {

void draw_battle_command_cursor_core(Machine &m) {
    m.ds() = kIram;
    m.bx() = m.rd8(m.ss(), kBattleMember);
    m.set_al(m.rd8(m.ss(), uint16_t(kEquipmentRow + m.bx())));
    uint16_t y = m.al() == 0 ? 0x54 : (m.al() == 1 ? 0x5C : 0x64);
    // SS-relative positions are separate from the DS=0 tile-word writes in the shared renderer.
    m.wr16(m.ss(), uint16_t(kSpriteBuffer + 2), y);
    m.wr16(m.ss(), uint16_t(kSpriteBuffer + 6), uint16_t(y + 0x800));
    m.wr16(m.ss(), uint16_t(kSpriteBuffer + 10), uint16_t(y + 8));
    m.ax() = uint16_t(y + 0x808);
    m.wr16(m.ss(), uint16_t(kSpriteBuffer + 14), m.ax());
    draw_battle_cursor_tiles_core(m);
}

// L_36528E: unregistered dependency of the status renderer. Preserves AX/CX/DS/ES, but leaves
// SI/DI advanced and DX=0E40h, so later fall-through draws use that value as their source index.
void draw_status_sprite_tiles_core(Machine &m) {
    m.si() = uint16_t(0xF00 + m.dx());
    if (m.rd8(kIram, kBlinkPhase) != 0)
        m.si() = uint16_t(m.si() + 8);
    m.di() = uint16_t(kSpriteBuffer + m.bx());
    m.dx() = 0xE40;
    for (int tile = 0; tile < 4; ++tile) {
        uint16_t tile_word = uint16_t(uint16_t(m.rd16(0x8000, m.si()) + m.dx()) | 0x2000);
        m.wr16(kIram, m.di(), tile_word);
        m.si() = uint16_t(m.si() + 2);
        m.di() = uint16_t(m.di() + 4);
    }
}

void draw_glyph_tile_core(Machine &m) {
    uint16_t si = m.si(), di = m.di(), cx = m.cx(), dx = m.dx(), ds = m.ds();
    m.si() = uint16_t(kBattleGlyphs + uint16_t(m.ax() << 1));
    m.ax() = m.rd16(m.ss(), kTextCursorPos);
    m.cx() = 0x101;
    m.dx() = uint8_t(dx) == 0 ? 0x1F00 : 0xF00;
    m.ds() = kDataSeg;
    draw_tilemap_offset_core(m);
    m.si() = si;
    m.di() = di;
    m.cx() = cx;
    m.dx() = dx;
    m.ds() = ds;
}

} // namespace
void lookup_table_byte_core(Machine &m) {
    // AL = item id, BX = field of its record (tables/items; tables.h): through the pointer table, as the
    // original does, so AH ends as the pointer's high byte.
    uint16_t pointer = m.rd16(kDataSeg, uint16_t(kByteTablePointers + uint16_t(m.al()) * 2));
    m.ax() =
        uint16_t((pointer & 0xFF00) | table_at(m, Table::item_records, kDataSeg, uint16_t(pointer + m.bx())));
}
namespace {

// Core of draw_text_alt (0x363860): draws FFh-terminated DS:SI text at AL screen page, BH row, BL
// column, choosing the palette from text_color_flag. Clears DF; advances SI/BL, preserves DI/ES.
void draw_text_alt_core(Machine &m) {
    m.watch_text("draw_text_alt");
    m.flags() = uint16_t(m.flags() & ~emu::DF);
    uint16_t dest = screen_map_offset(m.al(), m.bx());
    m.ax() = m.rd8(m.ss(), kTextColorFlag) == 0 ? 0x1F00 : 0xF00;
    if (const std::u32string *text = proportional_text(m)) { // a loaded language's string: our font
        const int cells = draw_table_text(m, dest, uint16_t(m.ax() & 0xFF00), *text);
        skip_text(m);
        m.set_al(0xFF);
        m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(uint8_t(m.bx()) + cells));
        return;
    }
    for (;;) {
        m.set_al(m.rd8(m.ds(), m.si()));
        if (m.al() == 0xFF)
            break;
        m.si() = uint16_t(m.si() + 1);
        m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.bx() + 1));
        m.wr16(kIram, dest, m.ax());
        dest = uint16_t(dest + 2);
    }
}

} // namespace
void draw_battle_marker_sprite_core(Machine &m) {
    m.es() = kDataSeg;
    m.dx() = 0xF00;
    m.si() = uint16_t(kBattleGlyphs + 0x1DC);
    m.di() = uint16_t(kSpriteBuffer + 0x20);
    m.ax() = uint16_t(uint16_t(m.rd16(m.es(), m.si()) + m.dx()) | 0x2000);
    m.wr16(kIram, m.di(), m.ax());
    m.di() = uint16_t(m.di() + 4);
    m.si() = uint16_t(m.si() + 2);
    m.cx() = 0;
}
namespace {

// Adds (dbh, dbl) to BX's high/low bytes independently, each wrapping mod 256 -- the packed screen
// row/column cursor convention used throughout this file (`add bh,N`/`add bl,N`/`sub bl,N`).
uint16_t bump_bx(uint16_t bx, uint8_t dbh, uint8_t dbl) {
    return uint16_t((uint16_t(uint8_t(uint8_t(bx >> 8) + dbh)) << 8) | uint8_t(uint8_t(bx) + dbl));
}
} // namespace

void swap_battle_order_entries_core(Machine &m) {
    for (uint16_t byte = 0; byte < 4; ++byte) {
        uint16_t left_address = uint16_t(m.si() + byte), right_address = uint16_t(m.di() + byte);
        uint8_t left_byte = m.rd8(m.ss(), left_address), right_byte = m.rd8(m.ss(), right_address);
        m.wr8(m.ss(), left_address, right_byte);
        m.wr8(m.ss(), right_address, left_byte);
    }
}

// rom: 0x3611B2 (near)
// Swaps four bytes at SS:SI and SS:DI, byte by byte; registers/FLAGS preserved. Overlapping entries
// follow the original sequential reads and writes, rather than a four-byte temporary block swap.
void swap_battle_order_entries(Machine &m) {
    swap_battle_order_entries_core(m);
    m.ret();
}

void battle_order_entry_pointer_core(Machine &m) { m.si() = uint16_t(kBattleOrder + uint16_t(m.al()) * 4); }

// rom: 0x3611F1 (near)
// AL selects a four-byte action-order entry. Returns SI; AX and all other registers preserved.
void battle_order_entry_pointer(Machine &m) {
    battle_order_entry_pointer_core(m);
    m.ret();
}

// rom: 0x362A39 (near)
// Draws four party-member panels, selecting normal/blinking/dead/status frames and palettes from the
// working battle records. All registers preserved; palette copies clear DF. Empty (FFh) actors skip.
void draw_battle_member_panels_core(Machine &m) {
    SavedRegisters saved(m);
    for (uint16_t member = 0; member < 4; ++member) {
        uint16_t frame = 0x9E0;
        bool special =
            m.rd8(m.ss(), kPanelSpecialMode) != 0 && uint8_t(member) == m.rd8(m.ss(), kPanelSpecialMember);
        if (!special) {
            uint8_t status = m.rd8(m.ss(), uint16_t(kBattleActors + member * 0x20 + 2));
            if (status == 0xFF)
                continue;
            if (status & 1)
                frame = uint16_t(frame + 0x18);
            else if (status & 0xFC)
                frame = uint16_t(frame + 0x10);
            else if (m.rd8(m.ss(), kBattleMember) == member && !(status & 2) &&
                     m.rd8(m.ss(), kBattleMenuState) != 5 && m.rd8(m.ss(), kBattleMenuState) != 0 &&
                     m.rd8(m.ss(), kBlinkPhase) == 1)
                frame = uint16_t(frame + 8);
        }
        uint16_t palette = m.rd16(m.ss(), uint16_t(kMemberPaletteOffsets + member * 2));
        uint16_t segment = m.rd16(m.ss(), uint16_t(kMemberPaletteSegments + member * 2));
        uint8_t status = m.rd8(m.ss(), uint16_t(kBattleActors + member * 0x20 + 2));
        if (status & 2)
            palette = uint16_t(palette + 0xC0);
        else
            palette =
                uint16_t(palette + uint16_t(m.rd8(m.ss(), uint16_t(kMemberPaletteVariants + member))) * 0x20);
        uint16_t slot = m.rd16(m.ss(), uint16_t(kMemberPaletteSlots + member * 2));
        uint16_t dest = uint16_t(((kPaletteBuffer + (slot << 5)) & 0x3FFF) + kSpriteBuffer);
        m.flags() = uint16_t(m.flags() & ~emu::DF);
        // copy_palettes has no shared core; here its only use is a single 16-colour palette.
        for (uint16_t color = 0; color < 16; ++color) {
            m.wr16(kIram, dest, m.rd16(segment, palette));
            dest = uint16_t(dest + 2);
            palette = uint16_t(palette + 2);
        }
        m.si() = frame;
        m.ds() = 0xA000;
        m.bx() = uint16_t(0x1001 + member * 7);
        m.dx() = uint16_t(0x3180 + member * 0x210);
        m.cx() = 0x202;
        m.ax() = 1;
        draw_tilemap_offset_core(m);
    }
    saved.restore(m);
}

void draw_battle_member_panels(Machine &m) {
    draw_battle_member_panels_core(m);
    m.ret();
}

// rom: 0x3630D1 (near)
// AX=0 hides all status sprites; otherwise compares battle status/stats with saved party stats and
// draws an icon per member. Quirk: a displayed icon falls through and draws later members too, using
// DX=0E40h after the first draw. Subsequent outer iterations may overwrite/hide those sprites.
void draw_battle_status_sprites_core(Machine &m) {
    if (m.ax() == 0) {
        m.dx() = uint16_t((m.dx() & 0xFF00) | 0xF);
        hide_battle_status_sprites_core(m);
        return;
    }
    m.cx() &= 0xFF00;
    do {
        uint8_t member = uint8_t(m.cx());
        m.di() = uint16_t(kGameState + uint16_t(member) * 0x1F);
        m.set_al(member);
        battle_actor_pointer_core(m);
        m.set_al(m.rd8(m.ss(), uint16_t(m.si() + 2)));
        bool show = m.al() != 0xFF && !(m.al() & 3);
        if (show) {
            if (m.al() & 0x80)
                m.dx() = 0x10;
            else if (m.al() & 0x40)
                m.dx() = 0x20;
            else if (m.al() & 0x20)
                m.dx() = 0x30;
            else if (m.al() & 4)
                m.dx() = 0x40;
            else if (m.al() & 8)
                m.dx() = 0x50;
            else if (m.al() & 0x10)
                m.dx() = 0;
            else {
                show = false;
                for (uint16_t stat = 0; stat < 3; ++stat) {
                    m.set_al(m.rd8(m.ss(), uint16_t(m.si() + 7 + stat)));
                    m.set_ah(m.rd8(m.ss(), uint16_t(m.di() + 0xB + stat)));
                    if (m.al() != m.ah()) {
                        m.dx() = uint16_t(0x70 + stat * 0x20 + (m.al() < m.ah() ? 0x10 : 0));
                        show = true;
                        break;
                    }
                }
            }
        }
        if (!show) {
            m.dx() = uint16_t((m.dx() & 0xFF00) | (1 << member));
            hide_battle_status_sprites_core(m);
        } else {
            for (uint16_t slot = member; slot < 4; ++slot) {
                uint16_t pos = uint16_t(0x6C + slot * 0x3800);
                uint16_t base = uint16_t(kSpriteBuffer + 0x2E + slot * 0x10);
                m.ax() = pos;
                m.wr16(m.ss(), base, m.ax());
                m.ax() = uint16_t(pos + 0x800);
                m.wr16(m.ss(), uint16_t(base + 4), m.ax());
                m.ax() = uint16_t(pos + 8);
                m.wr16(m.ss(), uint16_t(base + 8), m.ax());
                m.ax() = uint16_t(pos + 0x808);
                m.wr16(m.ss(), uint16_t(base + 12), m.ax());
                m.bx() = uint16_t(0x2C + slot * 0x10);
                draw_status_sprite_tiles_core(m);
            }
        }
        m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(member + 1));
    } while (uint8_t(m.cx()) != 4);
}

void draw_battle_status_sprites(Machine &m) {
    draw_battle_status_sprites_core(m);
    m.ret();
}

// rom: 0x36326D (near)
// DL bitmask selects which members' four status sprites to hide by setting their positions to 0090h.
// Returns AX=0090h, otherwise preserves registers.
void hide_battle_status_sprites(Machine &m) {
    hide_battle_status_sprites_core(m);
    m.ret();
}

// rom: 0x3632C5 (near)
// Toggles command-cursor visibility; XOR result exactly 1 draws it, every other result hides it.
// AX=0090h on the hidden path; visible path has draw_battle_command_cursor's register effects.
void blink_battle_command_cursor_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), kCommandCursorVisible) ^ 1));
    m.wr8(m.ss(), kCommandCursorVisible, m.al());
    if (m.al() == 1)
        draw_battle_command_cursor_core(m);
    else {
        m.ax() = 0x90;
        for (uint16_t tile = 0; tile < 4; ++tile)
            m.wr16(m.ss(), uint16_t(kSpriteBuffer + 2 + tile * 4), m.ax());
    }
}

void blink_battle_command_cursor(Machine &m) {
    blink_battle_command_cursor_core(m);
    m.ret();
}

// rom: 0x3634F1 (near)
// Places the four command-cursor sprites on the selected equipment row (0, 1, else 2), and loads
// their tile words. Leaves DS=0, ES=9000h, BX=member, DX=0F00h, CX=0, SI=228Eh, DI=C010h.
void draw_battle_command_cursor(Machine &m) {
    draw_battle_command_cursor_core(m);
    m.ret();
}

// rom: 0x363860
// Draws FFh-terminated DS:SI text at AL screen page, BH row, BL column, choosing the palette from
// text_color_flag. Clears DF; returns AX ending in FFh, advances SI/BL, preserves DI/ES.
void draw_text_alt(Machine &m) {
    draw_text_alt_core(m);
    m.retf();
}

// rom: 0x365042 (near)
// AL indexes a 32-byte battle actor working record. Returns SI, preserves every other register.
void battle_actor_pointer(Machine &m) {
    battle_actor_pointer_core(m);
    m.ret();
}

uint16_t advance_record_pointer_core(Machine &m) { return uint16_t(m.si() + uint32_t(m.ax()) * m.bx()); }

// rom: 0x365056
// Advances SI by AX records of BX bytes, with 16-bit wrap. All other registers preserved.
void advance_record_pointer_call_core(Machine &m) { m.si() = advance_record_pointer_core(m); }
void advance_record_pointer(Machine &m) {
    advance_record_pointer_call_core(m);
    m.retf();
}

uint16_t selected_battle_equipment_core(Machine &m) {
    uint16_t member = m.al();
    uint8_t slot = uint8_t(m.rd8(m.ss(), uint16_t(kEquipmentPage + member)) +
                           m.rd8(m.ss(), uint16_t(kEquipmentRow + member)));
    uint16_t item = uint16_t(kBattleActors + 0xE + member * 0x20 + uint16_t(slot) * 2);
    return m.rd16(m.ss(), item);
}

// rom: 0x36507D
// AL=party member; reads the selected {item id, uses} word from its battle equipment. Slot is the
// wrapped eight-bit sum of the member's equipment page and row. Only AX changes; DL is unused.
void selected_battle_equipment_call_core(Machine &m) { m.ax() = selected_battle_equipment_core(m); }
void selected_battle_equipment(Machine &m) {
    selected_battle_equipment_call_core(m);
    m.retf();
}

// rom: 0x36526E
// AL selects a pointer in D_3941E0, BX the byte within that record. Returns AL; AH retains the high
// byte of the record pointer (an incidental register effect), all other registers preserved.
void lookup_table_byte(Machine &m) {
    lookup_table_byte_core(m);
    m.retf();
}

// rom: 0x3652DA
// Loads one marker's tile word from D_392080+1DCh to sprite_buffer+20h. Preserves DS; leaves
// ES=9000h, DX=0F00h, CX=0, SI=225Eh, DI=C024h, AX=the tile word with attributes.
void draw_battle_marker_sprite(Machine &m) {
    draw_battle_marker_sprite_core(m);
    m.retf();
}

// rom: 0x3653BF
// Loads the four command-cursor sprite tile words; positions are untouched. DS preserved; leaves
// ES=9000h, DX=0F00h, CX=0, SI=228Eh, DI=C010h, AX=the last attributed tile word.
void draw_battle_cursor_tiles(Machine &m) {
    draw_battle_cursor_tiles_core(m);
    m.retf();
}

void draw_numeral_string_core(Machine &m) {
    uint16_t si = m.si(), cx = m.cx();
    m.set_ah(0);
    m.cx() = m.ax();
    do {
        uint16_t bx = m.bx();
        m.ax() = uint16_t(m.rd8(m.ds(), m.si()) + 0xBB);
        draw_glyph_tile_core(m);
        m.bx() = uint16_t((bx & 0xFF00) | uint8_t(bx + 1));
        m.si() = uint16_t(m.si() + 1);
    } while (--m.cx() != 0);
    m.si() = si;
    m.cx() = cx;
}

// rom: 0x3655CD
// AL (not CX) is the numeral count. Draws DS:SI digits plus BBh at successive BL columns; CX/SI
// preserved. Quirk: AL=0 still draws once, then LOOP wraps and draws 65536 times in total.
void draw_numeral_string(Machine &m) {
    draw_numeral_string_core(m);
    m.retf();
}

// rom: 0x3655EB
// AX=glyph index, BX=packed screen position, DL selects colour. Draws the glyph's tile word from
// bank 9000h:2080h + AX*2 at text_cursor_pos. Only AX changes (to text_cursor_pos).
void draw_glyph_tile(Machine &m) {
    draw_glyph_tile_core(m);
    m.retf();
}

uint16_t compact_battle_digits_core(Machine &m) {
    uint16_t dest = kDigits;
    bool significant = false;
    uint16_t significant_digits = 0;
    for (uint16_t digit = 0; digit < 5; ++digit) {
        uint8_t digit_value = m.rd8(m.ds(), uint16_t(kDigits + digit));
        if (digit_value != 0)
            significant = true;
        if (significant) {
            m.wr8(m.ds(), dest, digit_value);
            dest = uint16_t(dest + 1);
            ++significant_digits;
        }
    }
    return significant_digits == 0 ? 1 : significant_digits;
}

// rom: 0x3656A6 (near)
// Compacts the five decimal digits at DS:DC7Bh in place, dropping leading zeroes. Returns AX=count;
// an all-zero buffer returns 1 without writing any byte. All other registers preserved.
void compact_battle_digits(Machine &m) {
    m.ax() = compact_battle_digits_core(m);
    m.ret();
}

// rom: 0x365067
// AL selects a battle actor; returns AL = that actor's status byte (record offset+2, the same byte
// draw_battle_member_panels/draw_battle_status_sprites call "status"). BX/CX/DX/SI/DI are pushed and
// popped around the internal battle_actor_pointer_core call, so they -- and everything else -- end up
// exactly as the caller left them; only AL changes.
void battle_actor_status_byte(Machine &m) {
    uint16_t si = uint16_t(kBattleActors + uint16_t(m.al()) * 0x20);
    m.set_al(m.rd8(m.ss(), uint16_t(si + 2)));
    m.retf();
}

// rom: 0x3653EF
// Loads the same four command-cursor tile words as draw_battle_cursor_tiles (0x3653BF), but into
// sprite slot 10h instead of slot 0 -- likely a second cursor for a different menu layout. DS
// preserved; ES left at 9000h.
void draw_battle_cursor_tiles_alt_core(Machine &m) {
    uint16_t ds = m.ds();
    draw_battle_cursor_tiles_core(m, uint16_t(kSpriteBuffer + 0x10));
    m.ds() = ds;
    return;
}

void draw_battle_cursor_tiles_alt(Machine &m) {
    draw_battle_cursor_tiles_alt_core(m);
    m.retf();
}

// rom: 0x360A9D (near)
// Clears five AI/battle-turn scratch flags (0xDBF2/0xDBF3/0xDBF6/0xDBF7/0xDBF9) to 0. All registers
// preserved (only MOV instructions, which don't touch FLAGS either).
void clear_battle_ai_flags_core(Machine &m) {
    m.wr8(m.ss(), kBattleStatusSuccessCount, 0);
    m.wr8(m.ss(), kBattleKnockoutMode, 0);
    m.wr8(m.ss(), kBattleFlashCounter, 0);
    m.wr8(m.ss(), 0xDBF2, 0);
    m.wr8(m.ss(), kBattleGroupEliminationScratch, 0);
}

void clear_battle_ai_flags(Machine &m) {
    clear_battle_ai_flags_core(m);
    m.ret();
}

uint16_t blank_pad_and_extract_digits_core(Machine &m) {
    bool significant = false;
    for (uint16_t digit_index = 0; digit_index < 5; ++digit_index) {
        uint8_t digit = m.rd8(m.ss(), uint16_t(kDigits + digit_index));
        uint8_t display_digit = digit;
        if (digit != 0)
            significant = true;
        else if (!significant && digit_index != 4)
            display_digit = 0x1E;
        m.wr8(m.ss(), uint16_t(kPaddedBattleDigits + digit_index), display_digit);
    }
    for (uint16_t digit_index = 0; digit_index < 3; ++digit_index)
        m.wr8(m.ss(), uint16_t(kDigits + digit_index),
              m.rd8(m.ss(), uint16_t((kPaddedBattleDigits + 0x2) + digit_index)));
    return 3;
}

// rom: 0x3656E8 (near)
// Copies the 5 decimal digits at DS:DC7Bh to DC83h, replacing each leading zero (before the first
// nonzero digit, and not the last of the 5) with 1Eh (a "blank" numeral tile index elsewhere), then
// copies the LAST 3 of those 5 bytes (DC85h-DC87h) back over DC7Bh-DC7Dh. Returns AX=3 (a fixed
// count for a 3-digit blank-padded field); all other registers preserved (pushed/popped).
void blank_pad_and_extract_digits(Machine &m) {
    m.ax() = blank_pad_and_extract_digits_core(m);
    m.ret();
}

void draw_glyph_string_core(Machine &m) {
    uint16_t si = m.si(), cx = m.cx();
    if (m.ax() != 0) {
        m.set_ah(0);
        m.cx() = m.ax();
        do {
            uint16_t bx = m.bx();
            m.ax() = m.rd8(m.ds(), m.si());
            draw_glyph_tile_core(m);
            m.bx() = uint16_t((bx & 0xFF00) | uint8_t(bx + 1));
            m.si() = uint16_t(m.si() + 1);
        } while (--m.cx() != 0);
    }
    m.si() = si;
    m.cx() = cx;
}

// rom: 0x365686
// Draws AX glyphs (raw indices, not offset like draw_numeral_string) from DS:SI at successive BL
// columns; AX=0 draws nothing. CX/SI preserved; DL (palette selector) is whatever the caller left it
// (draw_glyph_tile_core preserves it too).
void draw_glyph_string(Machine &m) {
    draw_glyph_string_core(m);
    m.retf();
}

// rom: 0x365340
// Loads a third marker tile word (see draw_battle_marker_sprite/draw_battle_cursor_tiles_alt2 at
// 0x36530D) from D_392080+1E2h into sprite_buffer+28h. DS preserved; ES left at 9000h; CX=1 (the
// post-loop leftover, same convention as draw_battle_cursor_tiles).
void draw_battle_marker_sprite_alt2_core(Machine &m) {
    m.es() = kDataSeg;
    m.dx() = 0xF00;
    m.si() = uint16_t(kBattleGlyphs + 0x1E2);
    m.di() = uint16_t(kSpriteBuffer + 0x28);
    m.ax() = uint16_t(uint16_t(m.rd16(m.es(), m.si()) + m.dx()) | 0x2000);
    m.wr16(kIram, m.di(), m.ax());
    m.di() = uint16_t(m.di() + 4);
    m.si() = uint16_t(m.si() + 2);
    m.cx() = 0; // `dec cx; jne` with cx starting at 1: ends at 0, not 1
}

void draw_battle_marker_sprite_alt2(Machine &m) {
    uint16_t ds = m.ds();
    draw_battle_marker_sprite_alt2_core(m);
    m.ds() = ds;
    m.retf();
}

// rom: 0x36530D
// Loads a second marker tile word from D_392080+1E0h into sprite_buffer+24h (see
// draw_battle_marker_sprite at 0x3652DA, offset 1DCh/sprite+20h, and draw_battle_marker_sprite_alt2 at
// 0x365340, offset 1E2h/sprite+28h -- likely a row of related cursor markers). DS preserved; ES left
// at 9000h; CX=1.
void draw_battle_marker_sprite_alt_core(Machine &m) {
    m.es() = kDataSeg;
    m.dx() = 0xF00;
    m.si() = uint16_t(kBattleGlyphs + 0x1E0);
    m.di() = uint16_t(kSpriteBuffer + 0x24);
    m.ax() = uint16_t(uint16_t(m.rd16(m.es(), m.si()) + m.dx()) | 0x2000);
    m.wr16(kIram, m.di(), m.ax());
    m.di() = uint16_t(m.di() + 4);
    m.si() = uint16_t(m.si() + 2);
    m.cx() = 0; // `dec cx; jne` with cx starting at 1: ends at 0, not 1
}

void draw_battle_marker_sprite_alt(Machine &m) {
    uint16_t ds = m.ds();
    draw_battle_marker_sprite_alt_core(m);
    m.ds() = ds;
    m.retf();
}

// rom: 0x362936 (near)
// Return whether battle strength, defense, or agility is below the member's persistent stat.
uint16_t battle_actor_below_requirement_core(Machine &m) {
    uint8_t cl = uint8_t(m.cx());
    uint16_t actor = uint16_t(kBattleActors + uint16_t(cl) * 0x20);
    uint16_t game = uint16_t(kGameState + uint16_t(cl) * 0x1F);
    uint8_t al = 0, ah = 0;
    for (uint16_t stat_index = 0; stat_index < 3; ++stat_index) {
        al = m.rd8(m.ss(), uint16_t(game + 0xB + stat_index));
        ah = m.rd8(m.ss(), uint16_t(actor + 7 + stat_index));
        if (ah < al) {
            al = 1;
            break;
        }
        al = 0;
    }
    return uint16_t((uint16_t(ah) << 8) | al);
}

void battle_actor_below_requirement(Machine &m) {
    uint16_t saved_si = m.si(), saved_di = m.di();
    m.ax() = battle_actor_below_requirement_core(m);
    m.si() = saved_si;
    m.di() = saved_di;
    m.ret();
}

// rom: 0x3666CE (near)
// Test whether actor AL carries item DL in any of its eight battle equipment slots.
void actor_has_equipped_item_core(Machine &m) {
    m.si() = uint16_t(kBattleActors + uint16_t(m.al()) * 0x20 + 0xE);
    uint8_t dl = uint8_t(m.dx());
    uint8_t al = 0, ah = 0;
    for (uint16_t bx = 0; bx < 0x10; bx += 2) {
        ah = m.rd8(m.ss(), uint16_t(m.si() + bx));
        if (ah == dl) {
            al = 1;
            break;
        }
    }
    m.ax() = uint16_t((uint16_t(ah) << 8) | al);
}

void actor_has_equipped_item(Machine &m) {
    actor_has_equipped_item_core(m);
    m.ret();
}

// rom: 0x3650FB
// AL selects a party-member slot (0/1/else-treated-as-2); reads that member's name-id byte from
// game_state (0xD6D5/0xD6D7/0xD6D9), then converts the 9-byte name record at bank 9000h:6FE0h +
// id*9 into a 0xFFFF-terminated tile-word list at DI (shl 1 each byte), stopping at sentinel byte
// 0xD9 or 0xFF. All registers preserved (pushaw/ds/es/ss bracket the whole body); slots 0/2 also set
// DS=0 but it's never read anywhere in the routine, a dead assignment.
void build_party_name_core(Machine &m) {
    uint16_t id_addr = m.al() == 0 ? kEnemyGroups : m.al() == 1 ? (kEnemyGroups + 0x2) : (kEnemyGroups + 0x4);
    uint8_t id = m.rd8(m.ss(), id_addr);
    if (auto *s = Session::find(m.wsc()); s && m.original_popup_text())
        s->record_popup_name(m, m.di(), NameTable::Monster, id);
    const int run = name_run(m, NameTable::Monster, id); // a loaded language's name (text.h): one word
    if (run >= 0) {
        m.wr16(m.ss(), m.di(), uint16_t(kTextWord | run));
        m.wr16(m.ss(), uint16_t(m.di() + 2), 0xFFFF);
        return;
    }
    const TextPtr rec = name_record(m, NameTable::Monster, id);
    uint16_t si = rec.offset;
    uint16_t di = m.di();
    // Not in the original (it reads until a sentinel): the read stops at the record's 9 bytes. Each of the
    // ROM's 200 records ends within them; a record without a sentinel would overrun the caller's buffer.
    for (int n = 0; n < 9; ++n) {
        uint8_t name_glyph = m.rd8(rec.segment, si);
        if (name_glyph == 0xD9 || name_glyph == 0xFF)
            break;
        m.wr16(m.ss(), di, uint16_t(uint16_t(name_glyph) << 1));
        si = uint16_t(si + 1);
        di = uint16_t(di + 2);
    }
    m.wr16(m.ss(), di, 0xFFFF);
}

void build_party_name(Machine &m) {
    SavedRegisters saved(m);
    build_party_name_core(m);
    saved.restore(m);
    m.retf();
}

// rom: 0x365763
// Draws a bordered box directly into the screen map (ES=0) at row AL / column BX (packed like
// screen_map_offset -- the original recomputes it with the same instructions inline rather than
// calling the shared helper), CH rows tall, using CL-2 filler tiles between the left/right edge tiles
// of each row (same CL convention as draw_window). Row tiles: top FD5h/FD6h/FD7h, each middle row
// FD8h/FD9h/FDAh, last row FDBh/FDCh/FDDh -- a sibling routine at 0x3657E1 does the
// same with a different tile set. A screen row is 0x40 bytes further down the tile map. DI/ES end back
// at the caller's values (local scratch, discarded via push/pop each row in the original -- here
// simply never written to m); CX ends at 0 (the last row's REP count runs to completion); AX ends at
// FDDh, the last tile written. BL is left doubled (`shl bl,1` after the `pop bx` that otherwise
// restores it -- the same dead-leftover pattern as draw_window_frame); DS/SI/DX are never touched.
void draw_tile_box_border_core(Machine &m) {
    uint16_t es = kIram;
    uint16_t di = screen_map_offset(m.al(), m.bx());
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(uint8_t(m.bx()) << 1));
    uint16_t orig_cx = m.cx();
    uint8_t width_cells = uint8_t(uint8_t(orig_cx) - 2);
    uint8_t ch = uint8_t(orig_cx >> 8);

    auto draw_row = [&](uint16_t row_start, uint16_t left_tile) {
        uint16_t tile_destination = row_start;
        uint16_t ax = left_tile;
        m.wr16(es, tile_destination, ax);
        tile_destination = uint16_t(tile_destination + 2);
        ax = uint16_t(ax + 1);
        for (uint8_t i = 0; i < width_cells; ++i) {
            m.wr16(es, tile_destination, ax);
            tile_destination = uint16_t(tile_destination + 2);
        }
        ax = uint16_t(ax + 1);
        m.wr16(es, tile_destination, ax);
        return ax;
    };

    uint16_t last_ax = draw_row(di, 0xFD5);
    di = uint16_t(di + 0x40);
    --ch;
    for (;;) {
        --ch;
        if (ch == 0)
            break;
        last_ax = draw_row(di, 0xFD8);
        di = uint16_t(di + 0x40);
    }
    last_ax = draw_row(di, 0xFDB);

    m.ax() = last_ax;
    m.cx() = 0;
}

void draw_tile_box_border(Machine &m) {
    draw_tile_box_border_core(m);
    m.retf();
}

// Core of draw_class_icon_or_label (0x3637E0): DL selects what to draw at BX (the caller's screen
// position, untouched here) using text_color_flag for colour: DL==9 draws the fixed "spell icon" glyph
// D1h via draw_glyph_tile_core (after forcing text_cursor_pos to row 1); otherwise DL indexes D_396EE0
// (bank 9000h) for a class/race icon id -- 0xFF draws the literal fallback text D_38195F (bank 8000h)
// via draw_text_alt_core, anything else looks up a 1x1 tile from D_3819EC (bank 8000h) + id*2 via
// draw_tilemap_offset_core. Registers other than memory/BX/DS/etc used internally are the caller's
// problem to preserve (the wrapper brackets the whole thing in SavedRegisters).
void draw_class_icon_or_label_core(Machine &m) {
    if (uint8_t(m.dx()) == 9) {
        m.wr16(m.ss(), kTextCursorPos, 1);
        m.dx() = m.rd8(m.ss(), kTextColorFlag);
        m.ax() = 0xD1;
        draw_glyph_tile_core(m);
    } else {
        uint16_t si = uint16_t(0x6EE0 + uint8_t(m.dx())); // D_396EE0, bank 9000h
        uint8_t id = m.rd8(kDataSeg, si);
        if (id == 0xFF) {
            const TextPtr t = text(m, TextKey::draw_class_icon_or_label_1);
            m.ds() = t.segment;
            m.ax() = 1;
            m.si() = t.offset;
            draw_text_alt_core(m);
            text_done(m, TextKey::draw_class_icon_or_label_1);
        } else {
            m.ds() = 0x8000;
            m.si() = uint16_t(0x19EC + uint16_t(id) * 2); // D_3819EC
            m.cx() = 0x101;
            m.dx() = m.rd8(m.ss(), kTextColorFlag) != 0 ? 0xE13 : 0x1E13;
            m.ax() = 1;
            draw_tilemap_offset_core(m);
        }
    }
}

// rom: 0x3637E0 (near)
// See draw_class_icon_or_label_core. All registers preserved (pushaw/ds/es/ss bracket the whole body in
// the original); only screen-map memory changes.
void draw_class_icon_or_label(Machine &m) {
    SavedRegisters saved(m);
    draw_class_icon_or_label_core(m);
    saved.restore(m);
    m.ret();
}

// rom: 0x365010 (near)
// Ignores the caller's AL and reads the id from 0xDBFDh instead. That id==0xFF: sets flags as
// `and al,al` on FFh (CF=OF=AF=0, SF=1, ZF=0, PF=1) and returns with AL=FFh. Otherwise looks up
// D_3941E0[id] byte 1 via lookup_table_byte_core (BX=1), sets ZF/SF/PF from `test al,1` on that
// result (CF=OF=AF=0) and then forces CF=1 (clc;cmc); AL/AH end at the lookup's return value. BX/DX
// preserved. Callers branch on both CF and ZF (jae/je/jb), so FLAGS must be exact -- not
// clobbers_flags.
void has_table_flag_bit1_core(Machine &m) {
    uint16_t bx = m.bx(), dx = m.dx();
    uint16_t flags = uint16_t(m.flags() & ~(emu::CF | emu::OF | emu::AF | emu::SF | emu::ZF | emu::PF));
    uint8_t id = m.rd8(m.ss(), kActionEffect);
    if (id == 0xFF) {
        m.set_al(id);
        flags = uint16_t(flags | emu::SF | emu::PF); // `and al,al` on FFh
    } else {
        m.set_al(id);
        m.bx() = 1;
        lookup_table_byte_core(m);
        uint8_t masked = uint8_t(m.al() & 1);
        if (masked == 0)
            flags = uint16_t(flags | emu::ZF | emu::PF);
        flags = uint16_t(flags | emu::CF); // clc; cmc
    }
    m.flags() = flags;
    m.bx() = bx;
    m.dx() = dx;
}

void has_table_flag_bit1(Machine &m) {
    has_table_flag_bit1_core(m);
    m.ret();
}

// rom: 0x3653A8
// Hides 30 sprites' Y-position words (di+2, 4-byte stride) starting at sprite_buffer+6Ch by writing
// 90h (matches hide_battle_status_sprites_core's off-screen convention) -- likely a spell/item menu's
// icon column. DI/CX/BX are left at their final loop values (sprite_buffer+6Ch+78h, 0, 90h); nothing
// preserves them (no push/pop wraps this routine at all).
void hide_battle_menu_sprites_core(Machine &m) {
    m.di() = uint16_t(kSpriteBuffer + 0x6C);
    m.cx() = 0x1E;
    m.bx() = 0x90;
    do {
        m.wr16(m.ss(), uint16_t(m.di() + 2), m.bx());
        m.di() = uint16_t(m.di() + 4);
    } while (--m.cx() != 0);
}

void hide_battle_menu_sprites(Machine &m) {
    hide_battle_menu_sprites_core(m);
    m.retf();
}

// Count equipped actions with uses remaining and item flag bit 0 set.
void count_actor_available_items_core(Machine &m) {
    uint16_t si = uint16_t(kBattleActors + uint16_t(m.al()) * 0x20 + 0xE);
    uint8_t available_actions = 0;
    uint8_t ah = m.ah();
    for (uint8_t slot = 0; slot < 8; ++slot) {
        uint8_t hi = m.rd8(m.ss(), uint16_t(si + 1));
        bool skip = hi == 0xFF || hi == 0;
        if (!skip && hi == 0xFE && m.rd8(m.ss(), si) == 0xFE)
            skip = true;
        if (!skip) {
            uint8_t item = m.rd8(m.ss(), si);
            uint16_t pointer = m.rd16(kDataSeg, uint16_t(kByteTablePointers + uint16_t(item) * 2));
            uint8_t item_flags = table_at(m, Table::item_records, kDataSeg, pointer);
            ah = uint8_t(pointer >> 8);
            if (item_flags & 1)
                ++available_actions;
        }
        si = uint16_t(si + 2);
    }
    m.ax() = uint16_t((uint16_t(ah) << 8) | available_actions);
}

// rom: 0x364D87 (near)
// See count_actor_available_items_core. SI/CX modified internally but restored via push/pop.
void count_actor_available_items(Machine &m) {
    count_actor_available_items_core(m);
    m.ret();
}

// The RCL AL,1 / RCR CH,1 chain shared by reverse_bits_al and battle_priority_less_than (0x36403B, which
// inlines two calls to this with an explicit incoming CF). Returns the reversed byte and the CF the chain
// exits with (the last bit rotated out of CH), since 0x36403B threads that CF into its second call.
struct BitReverseResult {
    uint8_t value;
    bool cf;
};
BitReverseResult reverse_bits_al_core(uint8_t al, bool cf) {
    uint8_t ch = 0;
    for (int i = 0; i < 8; ++i) {
        bool msb = (al & 0x80) != 0;
        al = uint8_t((al << 1) | (cf ? 1 : 0));
        bool ch_lsb = (ch & 1) != 0;
        ch = uint8_t((ch >> 1) | (msb ? 0x80 : 0));
        cf = ch_lsb;
    }
    return {ch, cf};
}

// rom: 0x36402B (near)
// Reverses the 8 bits of AL via 8 rounds of RCL AL,1 / RCR CH,1 chained through CF -- the entering CF
// (from the caller) becomes the first (highest) bit inserted, and CH's incoming value is fully rotated
// out over the 8 rounds so it doesn't matter (the original loads CX=8 fresh, so CH actually starts at
// 0 anyway). Returns the reversed byte in AL; CX is preserved (pushed/popped around the whole body).
void reverse_bits_al(Machine &m) {
    BitReverseResult reversed_bits = reverse_bits_al_core(m.al(), (m.flags() & emu::CF) != 0);
    m.set_al(reversed_bits.value);
    m.ret();
}

// Shared body of clear_battle_message_box (0x365450): fills a rectangle of the screen map with the
// "blank" tile (D_392080+1FEh) via draw_tilemap_offset (AX=1, CX=0101h/1x1, DX=1F00h), sweeping column
// BL from the caller's starting BX up to col_end, row BH up to row_end. The original tracks the swept
// position in AX in parallel with BX (kept in sync via `mov bl,al`/`mov bh,ah` after every change,
// bracketed by a push/pop AX around each draw call that temporarily borrows AX for the call's tile
// index), so AX and BX are always equal; DS is left at 9000h (bank 34's data segment, never restored).
// AX/BX end at (row_end<<8)|col_end; CX/DX/SI/DS end at the last call's values.
void fill_blank_tile_rect_core(Machine &m, uint8_t col_end, uint8_t row_end) {
    for (;;) {
        m.ds() = kDataSeg;
        m.si() = uint16_t(kBattleGlyphs + 0x1FE);
        m.cx() = 0x101;
        m.dx() = 0x1F00;
        m.ax() = 1;
        draw_tilemap_offset_core(m);
        uint8_t col = uint8_t(m.bx());
        uint8_t row = uint8_t(m.bx() >> 8);
        if (col == col_end) {
            if (row == row_end) {
                m.ax() = m.bx();
                return;
            }
            m.bx() = uint16_t((row + 1) << 8);
        } else {
            m.bx() = uint16_t((row << 8) | (col + 1));
        }
    }
}

// Core of clear_battle_field_tiles (0x3655A0): fills columns 1-29 of screen-map row 1 with the blank
// tile (D_392080+1FEh) via draw_tilemap_offset_core (AX=1 for every call; BH stays 1 throughout --
// unlike clear_battle_message_box, there is no row loop here at all). DS is left at 9000h (never
// restored). AX/BX end at 011Dh (row 1, column 1Dh).
void clear_battle_field_tiles_core(Machine &m) {
    m.bx() = 0x0101;
    for (;;) {
        m.ds() = kDataSeg;
        m.si() = uint16_t(kBattleGlyphs + 0x1FE);
        m.cx() = 0x101;
        m.dx() = 0x1F00;
        m.ax() = 1;
        draw_tilemap_offset_core(m);
        if (uint8_t(m.bx()) == 0x1D) {
            m.ax() = m.bx();
            break;
        }
        m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(uint8_t(m.bx()) + 1));
    }
}

// rom: 0x3655A0
// See clear_battle_field_tiles_core.
void clear_battle_field_tiles(Machine &m) {
    clear_battle_field_tiles_core(m);
    m.retf();
}

// rom: 0x365450
// Fills columns 1-29, rows 10-13 of the screen map with the blank tile via fill_blank_tile_rect_core --
// likely clearing the battle message box. See fill_blank_tile_rect_core.
void clear_battle_message_box_core(Machine &m) {
    m.bx() = 0x0A01;
    fill_blank_tile_rect_core(m, 0x1D, 0x0D);
}

void clear_battle_message_box(Machine &m) {
    clear_battle_message_box_core(m);
    m.retf();
}

// rom: 0x365422
// Hides four command-cursor-adjacent sprites (Y-positions at sprite_buffer+12h/16h/1Ah/1Eh) and the
// marker sprite at sprite_buffer+22h, then redraws the cursor tiles (draw_battle_cursor_tiles_alt's
// slot, 0x3653EF) and the primary marker sprite (0x3652DA). DS is forced to 0 and never restored (no
// push ds here, unlike its siblings).
void hide_and_redraw_battle_cursors_core(Machine &m) {
    m.ds() = kIram;
    for (uint16_t off : {0x12, 0x16, 0x1A, 0x1E})
        m.wr16(kIram, uint16_t(kSpriteBuffer + off), 0x90);
    draw_battle_cursor_tiles_core(m, uint16_t(kSpriteBuffer + 0x10));
    m.wr16(kIram, uint16_t(kSpriteBuffer + 0x22), 0x90);
    draw_battle_marker_sprite_core(m);
}

void hide_and_redraw_battle_cursors(Machine &m) {
    hide_and_redraw_battle_cursors_core(m);
    m.retf();
}

// AL = a race/name id; searches the 3 current party members' id bytes (0xD6D5/D6D7/D6D9, the same
// table build_party_name reads) for a match, returning AL = the matching slot (0-2), or 3 if none
// match. AH ends up holding the last compared table byte (the match, or 0xD6D9's id if none matched) --
// an incidental register effect, masked when build_battle_member_display calls this since it saves/
// restores AX wholesale. BX/CX/SI are modified internally but restored via push/pop in the original; only
// AL/AH change.
void party_slot_for_name_id_core(Machine &m) {
    uint8_t al = m.al();
    uint8_t slot = 0;
    uint8_t ah = 0;
    for (; slot < 3; ++slot) {
        ah = m.rd8(m.ss(), uint16_t(kEnemyGroups + slot * 2));
        if (ah == al)
            break;
    }
    m.set_al(slot);
    m.set_ah(ah);
}

// rom: 0x3640E6
// Far wrapper for party_slot_for_name_id_core -- see there for the AH leftover, which is directly
// observable here (unlike at build_battle_member_display, whose save/restore hides it). BX/CX/SI are
// pushed/popped around the call in the original, and the core never touches them, so they come back
// unchanged either way.
void party_slot_for_name_id(Machine &m) {
    party_slot_for_name_id_core(m);
    m.retf();
}

// AL selects a party-member slot (0-3); SI = fixed per-slot game_state offset+1 (0xD001/0xD020/0xD03F/
// 0xD05E). Converts up to 4 bytes from that record into a 0xFFFF-terminated tile-word list at DI (shl
// 1 each, stop early at byte 0xFF). No registers are preserved in the original (no push/pop at all);
// AX ends at 0xFFFF (the terminator write is always the last thing to touch AX).
void build_status_icons_core(Machine &m) {
    const uint8_t name_slot = m.al();
    const uint16_t name_buffer = m.di();
    uint16_t si = m.al() == 0   ? kGameState
                  : m.al() == 1 ? (kGameState + 0x1F)
                  : m.al() == 2 ? (kGameState + 0x3E)
                                : (kGameState + 0x5D);
    si = uint16_t(si + 1);
    uint16_t di = m.di();
    uint16_t cx = 4;
    while (cx != 0) {
        uint8_t name_glyph = m.rd8(m.ss(), si);
        if (name_glyph == 0xFF)
            break;
        m.wr16(m.ss(), di, uint16_t(uint16_t(name_glyph) << 1));
        si = uint16_t(si + 1);
        di = uint16_t(di + 2);
        --cx;
    }
    m.wr16(m.ss(), di, 0xFFFF);
    m.ax() = 0xFFFF;
    m.cx() = cx;
    m.si() = si;
    m.di() = di;
    if (auto *s = Session::find(m.wsc());
        s && s->player_names().enabled && name_slot < 4 && !s->player_names().values[name_slot].empty()) {
        s->text().set_player_name(name_slot, s->player_names().rendered(name_slot));
        const uint16_t word = uint16_t(kTextWord | (TextTable::kPlayerNameRun + name_slot));
        if (m.original_popup_text())
            s->record_popup_words(name_buffer, {word});
        else {
            m.wr16(m.ss(), name_buffer, word);
            m.wr16(m.ss(), uint16_t(name_buffer + 2), 0xFFFF);
        }
    }
}

// rom: 0x3651C2
// Far wrapper for build_status_icons_core -- see there. No registers are preserved (the original has no
// push/pop at all around this one, unlike most of its siblings).
void build_status_icons(Machine &m) {
    build_status_icons_core(m);
    m.retf();
}

// rom: 0x369D8E (near)
// Builds either a party-member name (byte[0xDBFF]>=4: convert the actor's name-id via
// party_slot_for_name_id_core, then build_party_name_core) or a status-icon row (byte[0xDBFF]<4: the
// value is used directly as the party slot via build_status_icons_core) into the fixed tile-word
// buffer at 0xD62F. All registers preserved (pushaw/ds/es/ss bracket the whole body).
void build_battle_member_display_core(Machine &m) {
    SavedRegisters saved(m);
    uint8_t target_actor = m.rd8(m.ss(), kActionTarget);
    if (target_actor >= 4) {
        m.set_al(target_actor);
        uint16_t si = uint16_t(kBattleActors + uint16_t(m.al()) * 0x20);
        uint8_t name_id = m.rd8(m.ss(), uint16_t(si + 0x1F));
        m.set_al(name_id);
        party_slot_for_name_id_core(m);
        m.di() = kBattleTargetIcons;
        build_party_name_core(m);
    } else {
        m.set_al(target_actor);
        m.di() = kBattleTargetIcons;
        build_status_icons_core(m);
    }
    saved.restore(m);
}

void build_battle_member_display(Machine &m) {
    build_battle_member_display_core(m);
    m.ret();
}

// rom: 0x360ABC (near)
// Mark no active party members (04h) or no active enemies with HP remaining (02h).
void mark_battle_end_flags_core(Machine &m) {
    constexpr uint16_t kBattleEndState = 0xDCC2;
    bool party_active = false;
    for (uint8_t actor_index = 0; actor_index < 4 && !party_active; ++actor_index) {
        uint16_t si = uint16_t(kBattleActors + actor_index * 0x20);
        if ((m.rd8(m.ss(), uint16_t(si + 2)) & 3) == 0)
            party_active = true;
    }
    if (!party_active) {
        m.wr8(m.ss(), kBattleEndState, uint8_t(m.rd8(m.ss(), kBattleEndState) | 4));
        return;
    }
    uint8_t enemy_count = m.rd8(m.ss(), kEnemyCount);
    bool enemy_active = false;
    for (uint8_t actor_index = 0; actor_index < enemy_count && !enemy_active; ++actor_index) {
        uint16_t si = uint16_t(kBattleActors + (4 + actor_index) * 0x20);
        if ((m.rd8(m.ss(), uint16_t(si + 2)) & 3) == 0 && m.rd16(m.ss(), uint16_t(si + 3)) != 0)
            enemy_active = true;
    }
    if (!enemy_active)
        m.wr8(m.ss(), kBattleEndState, uint8_t(m.rd8(m.ss(), kBattleEndState) | 2));
}

void mark_battle_end_flags(Machine &m) {
    mark_battle_end_flags_core(m);
    m.ret();
}

// rom: 0x360D6D (near)
// Recount active enemies with HP remaining into the three type/count group records.
void tally_enemy_type_counts_core(Machine &m) {
    uint16_t di = uint16_t(kBattleActors + 4 * 0x20);
    uint16_t si = kEnemyGroups;
    uint16_t bx = 0;
    uint16_t cx = m.rd8(m.ss(), kEnemyCount);
    uint16_t dx = di;
    m.wr8(m.ss(), uint16_t(si + 1), 0);
    uint8_t al = m.al(), ah = m.ah(); // untouched if the loop never runs (0xD6D1 == 0)
    for (;;) {
        if (uint8_t(cx) == 0)
            break;
        di = uint16_t(dx + 0x1F);
        al = m.rd8(m.ss(), di);
        ah = m.rd8(m.ss(), si);
        if (al == ah) {
            di = uint16_t(dx + 2);
            al = uint8_t(m.rd8(m.ss(), di) & 3);
            if (al == 0) {
                di = uint16_t(dx + 3);
                uint16_t value = m.rd16(m.ss(), di);
                al = uint8_t(value);
                ah = uint8_t(value >> 8);
                if (value != 0) {
                    al = uint8_t(m.rd8(m.ss(), uint16_t(si + 1)) + 1);
                    m.wr8(m.ss(), uint16_t(si + 1), al);
                }
            }
            cx = uint16_t(cx - 1);
            di = uint16_t(dx + 0x20);
            dx = di;
            continue;
        }
        bx = uint16_t(bx + 1);
        if (bx == 4)
            break;
        si = uint16_t(si + 2);
        m.wr8(m.ss(), uint16_t(si + 1), 0);
    }
    m.di() = di;
    m.si() = si;
    m.bx() = bx;
    m.cx() = cx;
    m.dx() = dx;
    m.set_al(al);
    m.set_ah(ah);
}

void tally_enemy_type_counts(Machine &m) {
    tally_enemy_type_counts_core(m);
    m.ret();
}

// rom: 0x36502E (near)
// Return actor AL's agility, halved unless status bit 3 is set; SI follows the result.
void battle_actor_stat_byte9_halved_core(Machine &m) {
    battle_actor_pointer_core(m);
    uint8_t al = m.rd8(m.ss(), uint16_t(m.si() + 9));
    uint8_t flags = m.rd8(m.ss(), uint16_t(m.si() + 2)); // `mov ah, ...`: AH ends up holding this byte
    m.set_ah(flags);
    if ((flags & 8) == 0)
        al = uint8_t(al >> 1);
    m.set_al(al);
    battle_actor_pointer_core(m);
}

void battle_actor_stat_byte9_halved(Machine &m) {
    battle_actor_stat_byte9_halved_core(m);
    m.ret();
}

// rom: 0x36C4D7 (near)
// No-op: saves and immediately restores DS/ES/SS (pushaw/push ds es ss ... pop ss es ds/popaw) without
// touching anything in between. Every register and all memory are left exactly as the caller had them.
// Called from many places across this bank's equipment/spell-list renderers -- looks like a placeholder
// for something that was stripped out, or a leftover no-op guard call.
void restore_saved_registers(Machine &m) { m.ret(); }

// The four-word (or -byte) table lookup shared by build_formation_layer_records (0x36368F, which calls
// this once per active party member). AL selects a "formation slot"; ES:DI is the battle-formation record
// to fill in.
void build_formation_layer_record_core(Machine &m) {
    uint8_t slot = m.al();
    m.ax() = kDataSeg;
    m.ds() = kDataSeg;
    uint16_t si = uint16_t(0x7878 + slot * 2); // D_397878[slot]: a {byte,byte} pair
    uint8_t graphics_index = table_at(m, Table::monster_type_descriptor, m.ds(), si);
    uint8_t palette_variant = table_at(m, Table::monster_type_descriptor, m.ds(), uint16_t(si + 1));
    m.wr8(m.ss(), kFormationGraphicsIndex, graphics_index);
    m.wr8(m.ss(), kFormationPaletteVariant, palette_variant);
    m.wr8(m.ss(), uint16_t(m.di() + 8), palette_variant);
    // Every table below is indexed by graphics_index (D_397878[slot]'s first byte), not by the original slot
    // -- the original reuses DL (just overwritten with graphics_index) as CL for each of the following loops.
    uint8_t graphics_table_index = graphics_index;
    constexpr uint16_t kDataBank = 0xE000; // bank 0x3E

    m.ax() = kDataBank;
    m.ds() = kDataBank;
    si = uint16_t(graphics_table_index *
                  6); // D_3E0000[graphics_table_index]: a 3-word {ptr?, count?, ?} triple
    m.wr16(m.ss(), m.di(), m.rd16(m.ds(), si));
    m.wr16(m.ss(), uint16_t(m.di() + 2), m.rd16(m.ds(), uint16_t(si + 2)));
    m.wr16(m.ss(), uint16_t(m.di() + 4), m.rd16(m.ds(), uint16_t(si + 4)));

    m.ax() = kDataBank;
    m.ds() = kDataBank;
    si = uint16_t(0x00D8 + graphics_table_index * 2); // D_3E00D8[graphics_table_index]: a word
    m.wr16(m.ss(), uint16_t(m.di() + 6), m.rd16(m.ds(), si));

    m.ax() = kDataBank;
    m.ds() = kDataBank;
    si = uint16_t(0x0120 + graphics_table_index * 2); // D_3E0120[graphics_table_index]: a {byte,byte} pair
    m.wr8(m.ss(), uint16_t(m.di() + 9), m.rd8(m.ds(), si));
    m.wr8(m.ss(), uint16_t(m.di() + 0xA), m.rd8(m.ds(), uint16_t(si + 1)));

    m.ax() = kDataBank;
    m.ds() = kDataBank;
    si = uint16_t(0x0168 + graphics_table_index); // D_3E0168[graphics_table_index]: a byte
    uint8_t last_byte = m.rd8(m.ds(), si);
    m.wr8(m.ss(), uint16_t(m.di() + 0xB), last_byte);

    m.si() = si;
    m.set_al(last_byte); // `mov al, byte ptr [si]` only touches AL; AH stays 0xE0 from the last `mov ax,
                         // 0xe000` above
    m.dx() = uint16_t((uint16_t(palette_variant) << 8) | graphics_table_index);
    m.cx() = uint16_t(m.cx() & 0xFF00); // every internal loop finishes with CL=0; CH is never touched
}

// rom: 0x3635E1 (near)
// Far-callable wrapper for build_formation_layer_record_core -- see there.
void build_formation_layer_record(Machine &m) {
    build_formation_layer_record_core(m);
    m.ret();
}

// rom: 0x36368F
// Builds 1-3 battle-formation records (build_formation_layer_record_core, at 0xD500/0xD50C/0xD518) for
// the current party, one per member, from each member's formation-slot id (0xD6D5/D6D7/D6D9). Party size
// comes from game_state byte 0xD6D4 (0-3); sizes 0 and 1 both build just the first record -- reproduced
// as-is, the 0 case looks like an original-code gap rather than an intentional choice. ES ends at 0;
// AX/DS/DX/CX/SI end at whatever the last build_formation_layer_record_core call left them (see there);
// DI ends at the last record base used; BL = the party size read (BH untouched, no register-preservation
// prologue here).
void build_formation_layer_records_core(Machine &m) {
    m.ds() = 0;
    uint8_t party_size = m.rd8(0, kEnemyGroupCount);
    m.bx() = uint16_t((m.bx() & 0xFF00) | party_size);

    m.es() = 0;
    m.di() = kFormationBackTileSource;
    m.ax() = m.rd8(m.ss(), kEnemyGroups);
    build_formation_layer_record_core(m);

    if (party_size == 2 || party_size == 3) {
        m.es() = 0;
        m.di() = kFormationMiddleTileSource;
        m.ax() = m.rd8(m.ss(), (kEnemyGroups + 0x2));
        build_formation_layer_record_core(m);
    }
    if (party_size == 3) {
        m.es() = 0;
        m.di() = kFormationFrontTileSource;
        m.ax() = m.rd8(m.ss(), (kEnemyGroups + 0x4));
        build_formation_layer_record_core(m);
    }
}

void build_formation_layer_records_call_core(Machine &m) { build_formation_layer_records_core(m); }
void build_formation_layer_records(Machine &m) {
    build_formation_layer_records_call_core(m);
    m.retf();
}

// rom: 0x369E1B (near)
// Builds either a status-icon row (byte[0xDBFE]<4: the value is used directly as the party slot via
// build_status_icons_core) or a party-member name (byte[0xDBFE]>=4: searches the party's name-id table at
// 0xD6D5/D6D7/D6D9 for a match with that battle actor's name id, offset+0x1F -- an unbounded version of
// party_slot_for_name_id_core's search: it has no 3-mismatch cutoff, so it would loop forever if no match
// were ever found) into the fixed tile-word buffer at 0xD60F. All registers preserved (pushaw/ds/es/ss
// bracket the whole body). Near sibling of build_battle_member_display (0x369D8E), which builds the same
// pair of things into 0xD62F using byte 0xDBFF instead of 0xDBFE.
void build_battle_member_name_or_icons_core(Machine &m) {
    SavedRegisters saved(m);
    uint8_t acting_actor = m.rd8(m.ss(), kActingActor);
    if (acting_actor < 4) {
        m.ax() = acting_actor;
        m.di() = kBattleActorIcons;
        build_status_icons_core(m);
    } else {
        uint16_t di = uint16_t(kBattleActors + uint16_t(acting_actor) * 0x20 + 0x1F);
        uint8_t name_id = m.rd8(m.ss(), di);
        uint16_t si = kEnemyGroups;
        uint8_t slot = 0;
        while (m.rd8(m.ss(), si) != name_id) {
            si = uint16_t(si + 2);
            ++slot;
        }
        m.set_al(slot);
        m.di() = kBattleActorIcons;
        build_party_name_core(m);
    }
    saved.restore(m);
}

void build_battle_member_name_or_icons(Machine &m) {
    build_battle_member_name_or_icons_core(m);
    m.ret();
}

// rom: 0x365373
// Fills 20 sprite entries at sprite_buffer+6Ch (4-byte stride) with a tile word read from bank 9000h:
// D_392080+1E4h (the same word every time -- SI never advances) + 0F00h | 2000h, and a Y/X position word
// starting at (8,8) with Y (BH) incrementing by 8 each entry -- likely the spell/item icon column's
// initial layout (compare hide_battle_menu_sprites, 0x3653A8, which hides a 30-entry range at the same
// base but only clears the position half). ES left at 9000h; DI ends past the filled range; CX ends 0;
// BX ends (Y=0xA8, X=8) after the 8-bit wraparound increments; SI/DX are unchanged; AX is left holding
// the last tile word written.
void draw_battle_status_icon_tiles_core(Machine &m) {
    m.es() = kDataSeg;
    uint16_t dx = 0xF00;
    uint16_t si = uint16_t(kBattleGlyphs + 0x1E4);
    uint16_t di = uint16_t(kSpriteBuffer + 0x6C);
    uint16_t cx = 0x14;
    uint8_t bl = 8, bh = 8;
    uint16_t ax = 0;
    do {
        ax = uint16_t(uint16_t(m.rd16(m.es(), si) + dx) | 0x2000);
        m.wr16(m.ss(), di, ax);
        m.wr16(m.ss(), uint16_t(di + 2), uint16_t((uint16_t(bh) << 8) | bl));
        di = uint16_t(di + 4);
        bh = uint8_t(bh + 8);
        --cx;
    } while (cx != 0);
    m.ax() = ax; // AX is left holding the last tile word written
    m.di() = di;
    m.cx() = 0;
    m.bx() = uint16_t((uint16_t(bh) << 8) | bl);
    m.si() = si;
    m.dx() = dx;
}

void draw_battle_status_icon_tiles(Machine &m) {
    draw_battle_status_icon_tiles_core(m);
    m.retf();
}

// rom: 0x3650C3
// Renders AX as a digit tile-word list: converts AX to 5 raw digits via number_to_digits_core (into
// scratch buffer 0xDC7B), strips leading zeros via compact_battle_digits_core (returns the significant-
// digit count, always >=1), then for each remaining digit builds a tile word (digit + 0xBB, i.e. numeral
// tiles start at index 0xBB) into a 0xFFFF-terminated word list at DI (callers pass DI = 0xD66F, the
// on-screen numeral-display buffer). The digit-compaction writes go through DS while the tile-word build
// reads the same buffer through SS -- reproduced as-is; they only diverge if a caller's DS != SS. All
// registers preserved (pushaw/ds/es/ss bracket the whole body).
void format_number_core(Machine &m) {
    SavedRegisters saved(m);
    uint16_t si = kDigits;
    uint16_t di = kBattleNumberText;
    number_to_digits_core(m, m.ax(), m.ds(), si);
    uint16_t count = compact_battle_digits_core(m);
    for (uint16_t i = 0; i < count; ++i) {
        uint8_t digit = m.rd8(m.ss(), si);
        m.wr16(m.ss(), di, uint16_t((uint16_t(digit) + 0xBB) << 1));
        si = uint16_t(si + 1);
        di = uint16_t(di + 2);
    }
    m.wr16(m.ss(), di, 0xFFFF);
    saved.restore(m);
}

void format_number(Machine &m) {
    format_number_core(m);
    m.retf();
}

// rom: 0x365212
// DL selects how the item index is obtained: DL==1 uses AH directly as the index; otherwise calls
// selected_battle_equipment_core (AL = party member) and uses the low byte of its returned word. Looks up
// the 8-byte icon-code record at bank 9000h:4BE0h + index*8 and converts each byte to a tile word (shl
// 1), stopping early at sentinel byte 0xD9 or 0xFF, into a 0xFFFF-terminated list at the fixed buffer
// 0xD64F. All registers preserved (pushaw/ds/es/ss bracket the whole body). Low confidence on the exact
// purpose -- possibly a spell/skill icon-label builder, same family as build_name_string.
void build_icon_list_core(Machine &m) {
    SavedRegisters saved(m);
    uint8_t item_id = uint8_t(m.dx()) == 1 ? m.ah() : uint8_t(selected_battle_equipment_core(m));
    if (auto *s = Session::find(m.wsc()); s && m.original_popup_text())
        s->record_popup_name(m, kBattleItemIcons, NameTable::Item, item_id);
    const int run = name_run(m, NameTable::Item, item_id); // a loaded language's name (text.h): one word
    if (run >= 0) {
        m.wr16(kIram, kBattleItemIcons, uint16_t(kTextWord | run));
        m.wr16(kIram, (kBattleItemIcons + 0x2), 0xFFFF);
        saved.restore(m);
        return;
    }
    const TextPtr rec = name_record(m, NameTable::Item, item_id);
    uint16_t si = rec.offset;
    uint16_t di = kBattleItemIcons;
    for (uint16_t glyph_index = 0; glyph_index < 8; ++glyph_index) {
        uint8_t icon_glyph = m.rd8(rec.segment, si);
        if (icon_glyph == 0xD9 || icon_glyph == 0xFF)
            break;
        m.wr16(kIram, di, uint16_t(uint16_t(icon_glyph) << 1));
        si = uint16_t(si + 1);
        di = uint16_t(di + 2);
    }
    m.wr16(kIram, di, 0xFFFF);
    saved.restore(m);
}

void build_icon_list(Machine &m) {
    build_icon_list_core(m);
    m.retf();
}

bool battle_priority_parity8(uint8_t value) {
    int bits = 0;
    for (int i = 0; i < 8; ++i)
        bits += (value >> i) & 1;
    return (bits & 1) == 0;
}

// rom: 0x36403B (near)
// Compare action priorities using the original reversed-bit ordering and carry chain.
void battle_priority_less_than_core(Machine &m) {
    uint8_t al0 = m.al();
    uint8_t dl0 = uint8_t(m.dx());
    uint8_t al_in, dl_in;
    bool cmp_cf;
    bool via_clc_cmc;
    if (al0 == dl0) {
        al_in = al0;
        dl_in = dl0;
        cmp_cf = false;
        via_clc_cmc = true;
    } else if (al0 == 0xFF) {
        al_in = 0xFF;
        dl_in = dl0;
        cmp_cf = false;
        via_clc_cmc = true;
    } else if (dl0 == 0xFF) {
        al_in = al0;
        dl_in = 0xFF;
        cmp_cf = false;
        via_clc_cmc = false;
    } else {
        BitReverseResult left_priority_bits = reverse_bits_al_core(uint8_t(dl0 & 0xF), false);
        BitReverseResult right_priority_bits = reverse_bits_al_core(uint8_t(al0 & 0xF), false);
        al_in = right_priority_bits.value;
        dl_in = left_priority_bits.value;
        cmp_cf =
            right_priority_bits.value <
            left_priority_bits.value; // `cmp al,dl; jae` -- CF=1 iff right_priority_bits < left_priority_bits
        via_clc_cmc = !cmp_cf;
    }
    BitReverseResult r1 = reverse_bits_al_core(al_in, cmp_cf);
    BitReverseResult r2 = reverse_bits_al_core(dl_in, r1.cf);
    m.ax() = uint16_t(r1.value * 0x0101);
    m.dx() = uint16_t((m.dx() & 0xFF00) | r2.value);
    uint16_t flags = uint16_t(m.flags() & ~(emu::CF | emu::ZF | emu::SF | emu::PF | emu::AF | emu::OF));
    if (via_clc_cmc) {
        // clc;cmc -> CF=1; the other flags are untouched leftovers from reverse_bits_al_core's internal
        // loop, which always ends on left_priority_bits `dec cl` from 1 to 0: ZF=1, PF=1, SF=0, AF=0, OF=0,
        // every time.
        flags |= emu::CF | emu::ZF | emu::PF;
    } else {
        // `and al,al` on r1.value: CF=OF=AF=0, SF/ZF/PF from the value.
        if (r1.value == 0)
            flags |= emu::ZF;
        if (r1.value & 0x80)
            flags |= emu::SF;
        if (battle_priority_parity8(r1.value))
            flags |= emu::PF;
    }
    m.flags() = flags;
}

void battle_priority_less_than(Machine &m) {
    battle_priority_less_than_core(m);
    m.ret();
}

// Core of draw_battle_digit_tile (0x365638): AX selects a digit glyph (tile index AX*2 into bank
// 8000h:1150h) unless AX==1Eh, which instead draws a "blank" glyph from bank 9000h:2080h + (1Eh+BBh)*2
// (reusing the same glyph table as the other draw_*_tile helpers) -- the greyed-dash marker used for a
// requirement-not-met slot. DL selects the palette for that blank glyph only (0 => 1F00h, nonzero =>
// F00h); plain digits always use palette FC0h regardless of DL. The whole body is bracketed in
// pushaw+ds/es/ss (unlike draw_glyph_tile_core), so every register -- including AX and BX -- is genuinely
// restored; only memory (the drawn tile) changes.
void draw_battle_digit_tile_core(Machine &m) {
    SavedRegisters saved(m);
    if (m.ax() == 0x1E) {
        m.si() = uint16_t(kBattleGlyphs + uint16_t((m.ax() + 0xBB) << 1));
        m.ds() = kDataSeg;
        m.ax() = m.rd16(m.ss(), kTextCursorPos);
        m.cx() = 0x101;
        m.dx() = uint8_t(saved.dx) == 0 ? 0x1F00 : 0xF00;
    } else {
        m.si() = uint16_t(kDigitTileBase + uint16_t(m.ax() << 1));
        m.ds() = kDigitGlyphSeg;
        m.ax() = m.rd16(m.ss(), kTextCursorPos);
        m.cx() = 0x101;
        m.dx() = 0xFC0;
    }
    draw_tilemap_offset_core(m);
    saved.restore(m);
}

// rom: 0x365638
// Draws a single greyable digit-or-dash tile at text_cursor_pos; see draw_battle_digit_tile_core.
void draw_battle_digit_tile(Machine &m) {
    draw_battle_digit_tile_core(m);
    m.retf();
}

// Core of draw_battle_digit_row (0x36561D). Loops the AL low byte of AX times (AH is zeroed first, so a
// full 16-bit AX is never used as the count), reading one digit/marker byte from DS:SI per iteration and
// drawing it via draw_battle_digit_tile_core, advancing SI and the low byte of BX (BL) each time; DL
// (palette) flows straight through. Quirk shared with draw_numeral_string_core: the original uses a
// post-decrement `loop`, so AX low byte == 0 still draws once and then wraps to 65536 total iterations.
void draw_battle_digit_row_core(Machine &m) {
    uint16_t si = m.si();
    uint16_t remaining = uint8_t(m.ax()); // `mov ah,0; mov cx,ax`
    do {
        m.ax() = m.rd8(m.ds(), si);
        draw_battle_digit_tile_core(m);
        m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.bx() + 1));
        si = uint16_t(si + 1);
    } while (uint16_t(--remaining) != 0);
}

// rom: 0x36561D
// Draws AL greyable digit/dash tiles from DS:SI at successive BL columns (see draw_battle_digit_row_core);
// SI and CX are restored to their entry values (the loop uses its own copies internally), matching the
// original's push/pop bracket around the whole body.
void draw_battle_digit_row(Machine &m) {
    uint16_t si = m.si(), cx = m.cx();
    draw_battle_digit_row_core(m);
    m.si() = si;
    m.cx() = cx;
    m.retf();
}

// rom: 0x36271F (near)
// Draws the four-row battle skill/spell list: for each of party members 0-3 (row = CX), walks a
// 1Fh-byte-record, FFh-terminated list at kSkillListPointer, drawing each entry's 4-glyph name plus its
// max-use count (or a greyed dash-padded field if the actor's working record falls below the game_state
// requirement checked by battle_actor_below_requirement_core, or if the actor's own status byte is
// nonzero) and its current per-battle use count; once the list hits its FFh terminator, the row instead
// draws a fixed "empty slot" label. The caller's DL supplies one bit per row (bit `1 << row`); a set bit
// selects the normal palette for that row, a clear bit the greyed one. Two original quirks are
// reproduced: the empty-slot label's screen position uses whatever BX happened to be left over from the
// *previous* row's rendering, because BX is only ever refreshed from kSkillListPos on the
// "list still has an entry" path, never on the empty-slot path; and the max-uses field's
// blank-vs-shown bit (kSkillRowOtherBits) is decided by a `cmp ax,0` that can never be true on the path
// that reaches it (max-uses is already known nonzero there), so it always takes the "or 1" side in
// practice. ES ends at 8000h; CX ends at 4; every other register is genuinely left wherever the last
// sub-draw for row 3 left it.
void draw_battle_skill_list_core(Machine &m) {
    constexpr uint16_t kSkillListPointer = 0xDC79;   // 0xFF-terminated skill/spell list cursor
    constexpr uint16_t kSkillListPos = 0xDC73;       // packed BH/BL screen cursor for the current row
    constexpr uint16_t kSkillRowAvailBits = 0xDC1A;  // running per-row "list had an entry" bit history
    constexpr uint16_t kSkillRowOtherBits = 0xDC1B;  // running per-row bit history, same shift pattern;
                                                     // never read back anywhere in this file
    constexpr uint16_t kTextColorScratch = 0xDCD3;   // scratch save slot for kTextColorFlag
    constexpr uint16_t kZeroUsesTextOffset = 0x1948; // D_381948, ds=8000h: "0 max uses" label

    m.ds() = kIram;
    m.es() = 0x8000;
    m.wr16(m.ss(), kSkillListPointer, kGameState);
    m.wr16(m.ss(), kTextCursorPos, 1);
    m.wr16(m.ss(), kSkillListPos, 0xF02);

    m.cx() = 0;
    for (;;) {
        uint8_t row = uint8_t(m.cx());
        uint16_t input_dx = m.dx();
        uint8_t row_dl = (uint8_t(input_dx) & (1 << row)) ? 0 : 1;
        m.dx() = row_dl; // stays live (nothing below touches DX) until restored at the row's end

        m.si() = m.rd16(m.ss(), kSkillListPointer); // real `mov si, [kSkillListPointer]`
        uint8_t id = m.rd8(m.ss(), m.si());
        if (id == 0xFF) {
            // Empty slot: BX keeps whatever the previous row's rendering left it at (reproduced quirk).
            // AL is genuinely left holding (avail << 1) -- it's set *before* the pushaw bracket below, so
            // the block's own register-restore leaves it in place rather than undoing it.
            uint8_t avail = m.rd8(m.ss(), kSkillRowAvailBits);
            avail = uint8_t(avail << 1);
            m.wr8(m.ss(), kSkillRowAvailBits, avail);
            m.set_al(avail);

            SavedRegisters saved(m);
            uint8_t color = m.rd8(m.ss(), kTextColorFlag);
            m.wr8(m.ss(), kTextColorScratch, color);
            m.wr8(m.ss(), kTextColorFlag, row_dl != 0 ? 1 : 0);
            const TextPtr t = text(m, TextKey::draw_battle_skill_list_1);
            m.ds() = t.segment;
            m.si() = t.offset;
            m.bx() = bump_bx(m.bx(), 1, uint8_t(-1));
            m.ax() = 1;
            draw_text_alt_core(m);
            text_done(m, TextKey::draw_battle_skill_list_1);
            m.wr8(m.ss(), kTextColorFlag, m.rd8(m.ss(), kTextColorScratch));
            saved.restore(m);
        } else {
            uint16_t list_si = m.si(); // still == kSkillListPointer's value from just above
            uint8_t avail = m.rd8(m.ss(), kSkillRowAvailBits);
            m.wr8(m.ss(), kSkillRowAvailBits, uint8_t((avail << 1) | 1));

            m.ax() = 4;
            m.si() = uint16_t(list_si + 1);
            m.bx() = m.rd16(m.ss(), kSkillListPos);
            draw_glyph_string_core(m);

            m.si() = uint16_t(kBattleActors + 3 + uint16_t(row) * 0x20);
            m.ax() = m.rd16(m.ss(), m.si()); // real `mov ax, [si]` -- AX = max_uses from here on
            if (m.ax() == 0) {
                SavedRegisters saved(m);
                // BX here is whatever draw_glyph_string_core (the name draw, above) left it at -- NOT a
                // fresh read of kSkillListPos.
                m.bx() = bump_bx(m.bx(), 1, uint8_t(-3));
                m.ds() = 0x8000;
                m.si() = kZeroUsesTextOffset;
                uint8_t color = m.rd8(m.ss(), kTextColorFlag);
                m.wr8(m.ss(), kTextColorScratch, color);
                m.wr8(m.ss(), kTextColorFlag, row_dl != 0 ? 1 : 0);
                m.ax() = 1;
                draw_text_alt_core(m);
                m.wr8(m.ss(), kTextColorFlag, m.rd8(m.ss(), kTextColorScratch));
                saved.restore(m); // AX/SI restored to max_uses(=0)/kBattleActors+3+row*0x20
            } else {
                if (m.ax() >= 0x3E7)
                    m.ax() = 0x3E7;
                uint8_t other = m.rd8(m.ss(), kSkillRowOtherBits);
                other = uint8_t((other << 1) | (m.ax() == 0 ? 0 : 1)); // dead `cmp ax,0`: always the |1 side
                m.wr8(m.ss(), kSkillRowOtherBits, other);

                m.si() = kDigits;
                m.ax() = number_to_digits_core(m, m.ax(), m.ds(), m.si()); // real `lcall number_to_digits`
                m.ax() = blank_pad_and_extract_digits_core(m);             // real `call L_3656E8`
                uint16_t count = m.ax();

                m.bx() = bump_bx(m.rd16(m.ss(), kSkillListPos), 1, 1);

                bool draw_number = false;
                if (m.rd8(m.ss(), kPanelSpecialMode) == 1 && row == m.rd8(m.ss(), kPanelSpecialMember)) {
                    draw_number = true;
                } else {
                    m.set_al(row);
                    battle_actor_pointer_core(m);
                    uint8_t status = m.rd8(m.ss(), uint16_t(m.si() + 2));
                    if (status == 0) {
                        uint16_t below = battle_actor_below_requirement_core(m); // reads CX (== row)
                        if ((below & 1) == 0)
                            draw_number = true;
                    }
                    m.si() = kDigits; // real `pop si`: restores SI past the actor-pointer probe
                }

                m.ax() = count;
                if (draw_number) {
                    draw_numeral_string_core(m);
                } else {
                    draw_battle_digit_row_core(m); // DX is still row_dl
                }
            }

            m.ax() = 0xD1;
            m.bx() = bump_bx(m.rd16(m.ss(), kSkillListPos), 2, 1);
            draw_glyph_tile_core(m);

            m.ax() = 0xCF;
            m.bx() = bump_bx(m.rd16(m.ss(), kSkillListPos), 2, 2);
            draw_glyph_tile_core(m);

            m.si() = uint16_t(list_si + 0xA);
            m.set_al(uint8_t(m.rd8(m.ss(), m.si()) & 0xF));
            m.set_ah(0);
            m.si() = kDigits;
            m.ax() = number_to_digits_core(m, m.ax(), m.ds(), m.si());
            m.ax() = compact_battle_digits_core(m);

            m.bx() = bump_bx(m.rd16(m.ss(), kSkillListPos), 2, 3);
            draw_numeral_string_core(m);
        }

        // L_362914 footer -- AX/BX genuinely end up holding these two values (not restored afterward).
        m.ax() = uint16_t(m.rd16(m.ss(), kSkillListPointer) + 0x1F);
        m.wr16(m.ss(), kSkillListPointer, m.ax());
        m.bx() = m.rd16(m.ss(), kSkillListPos);
        m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(uint8_t(m.bx()) + 7));
        m.wr16(m.ss(), kSkillListPos, m.bx());

        m.dx() = input_dx;
        m.cx() = uint16_t(m.cx() + 1);
        if (uint8_t(m.cx()) == 4)
            break;
    }
}

void draw_battle_skill_list(Machine &m) {
    draw_battle_skill_list_core(m);
    m.ret();
}

// bank 0x36 battle routines, third pass

// rom: 0x364FF5 (near)
// Return target defense, halved when status bit 2 is set.
void battle_actor_stat_byte8_halved_core(Machine &m) {
    uint8_t actor = m.rd8(m.ss(), kActionTarget);
    uint16_t si = uint16_t(kBattleActors + uint16_t(actor) * 0x20);
    uint8_t al = m.rd8(m.ss(), uint16_t(si + 8));
    uint8_t flags = m.rd8(m.ss(), uint16_t(si + 2));
    if ((flags & 4) != 0)
        al = uint8_t(al >> 1);
    m.set_al(al);
}

void battle_actor_stat_byte8_halved(Machine &m) {
    battle_actor_stat_byte8_halved_core(m);
    m.ret();
}

// rom: 0x364FDA (near)
// Return acting-actor strength, halved when status bit 2 is set.
void battle_actor_stat_byte7_halved_core(Machine &m) {
    uint8_t actor = m.rd8(m.ss(), kActingActor);
    uint16_t si = uint16_t(kBattleActors + uint16_t(actor) * 0x20);
    uint8_t al = m.rd8(m.ss(), uint16_t(si + 7));
    uint8_t flags = m.rd8(m.ss(), uint16_t(si + 2));
    if ((flags & 4) != 0)
        al = uint8_t(al >> 1);
    m.set_al(al);
}

void battle_actor_stat_byte7_halved(Machine &m) {
    battle_actor_stat_byte7_halved_core(m);
    m.ret();
}

// rom: 0x364FCC (near)
// Return the target's resistance/immunity flags from battle record field 0Bh.
void battle_actor_stat_byte_b_core(Machine &m) {
    uint8_t actor = m.rd8(m.ss(), kActionTarget);
    uint16_t si = uint16_t(kBattleActors + uint16_t(actor) * 0x20);
    m.set_al(m.rd8(m.ss(), uint16_t(si + 0xB)));
}

void battle_actor_stat_byte_b_call_core(Machine &m) { battle_actor_stat_byte_b_core(m); }
void battle_actor_stat_byte_b(Machine &m) {
    battle_actor_stat_byte_b_call_core(m);
    m.ret();
}

// rom: 0x361976 (near)
// Appends the current battle-menu state byte (kBattleMenuState, 0xD6E2) to a history log at 0xD6E3,
// indexed and then incremented by a running counter at 0xD70B. AL ends holding the logged state byte;
// BX ends holding the post-increment index (BH is genuinely zeroed by the original's `xor bx,bx`).
void log_battle_menu_state_core(Machine &m) {
    uint8_t state = m.rd8(m.ss(), kBattleMenuState);
    m.set_al(state);
    uint8_t history_index = m.rd8(m.ss(), kBattleMenuHistoryCount);
    m.wr8(m.ss(), uint16_t(kBattleMenuHistory + history_index), state);
    history_index = uint8_t(history_index + 1);
    m.bx() = history_index;
    m.wr8(m.ss(), kBattleMenuHistoryCount, history_index);
}

void log_battle_menu_state(Machine &m) {
    log_battle_menu_state_core(m);
    m.ret();
}

// rom: 0x364083 (near)
// Copies CL bytes from DS:SI to SS:DI (`mov al,[si]; mov [di],al; inc si; inc di`), advancing SI/DI. AX
// is pushed/popped around the loop, so it ends up exactly as the caller left it. Quirk: the loop
// condition is `dec cl; jne`, so CL=0 on entry doesn't copy zero bytes -- it wraps to FFh first and
// copies 256 bytes instead.
void copy_bytes_ds_si_to_ss_di_core(Machine &m) {
    uint16_t ax = m.ax();
    do {
        uint8_t al = m.rd8(m.ds(), m.si());
        m.wr8(m.ss(), m.di(), al);
        m.si() = uint16_t(m.si() + 1);
        m.di() = uint16_t(m.di() + 1);
        m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(uint8_t(m.cx()) - 1));
    } while (uint8_t(m.cx()) != 0);
    m.ax() = ax;
}

void copy_bytes_ds_si_to_ss_di(Machine &m) {
    copy_bytes_ds_si_to_ss_di_core(m);
    m.ret();
}

// rom: 0x363579 (near)
// Chooses which pair of pre-battle-transition marker sprites (draw_battle_marker_sprite_alt2/_alt,
// 0x365340/0x36530D) show "highlighted" (546Ch) vs "off" (90h) tile-attribute words at sprite_buffer+
// 2Ah/+26h, based on the selected member's (kBattleMember) equipment page (kEquipmentPage[member]):
// page 0 highlights the alt2 marker only, page 5 highlights the alt marker only, anything else
// highlights both. DS ends at 0 (kIram, set at entry to read kEquipmentPage); BX is left holding the
// selected member (`xor bx,bx; mov bl,...`, never restored -- there's no push bx in the original). Other
// registers follow whichever marker core ran last.
void update_battle_marker_highlight_core(Machine &m) {
    m.ds() = kIram;
    uint8_t member = m.rd8(kIram, kBattleMember);
    m.bx() = member; // `xor bx,bx; mov bl,...`: BX is never restored, so this leftover survives to return
    uint8_t page = m.rd8(kIram, uint16_t(kEquipmentPage + member));
    if (page == 0) {
        m.wr16(kIram, uint16_t(kSpriteBuffer + 0x2A), 0x546C);
        draw_battle_marker_sprite_alt2_core(m);
        m.wr16(kIram, uint16_t(kSpriteBuffer + 0x26), 0x90);
        draw_battle_marker_sprite_alt_core(m);
    } else if (page == 5) {
        m.wr16(kIram, uint16_t(kSpriteBuffer + 0x2A), 0x90);
        draw_battle_marker_sprite_alt2_core(m);
        m.wr16(kIram, uint16_t(kSpriteBuffer + 0x26), 0x544C);
        draw_battle_marker_sprite_alt_core(m);
    } else {
        m.wr16(kIram, uint16_t(kSpriteBuffer + 0x2A), 0x546C);
        draw_battle_marker_sprite_alt2_core(m);
        m.wr16(kIram, uint16_t(kSpriteBuffer + 0x26), 0x544C);
        draw_battle_marker_sprite_alt_core(m);
    }
}

void update_battle_marker_highlight(Machine &m) {
    update_battle_marker_highlight_core(m);
    m.ret();
}

// rom: 0x3657E1
// Draws a bordered box like draw_tile_box_border (0x365763), but with a different, non-sequential tile
// set: the top row is FE6h/FE7h/FE8h and the bottom row FEBh/FECh/FEDh (each left/fill/right in
// sequence, like draw_tile_box_border's rows), but a middle row's left/right corners are FE9h/FEAh
// while its *filler* tile is FD9h -- not FE9h+1 as the corner-tile pattern would otherwise suggest.
// Row/width geometry (screen position from AL/BX, CH rows, CL-2 filler columns) and the doubled-BL
// leftover match draw_tile_box_border exactly. ES=0. AX ends at the last tile written; CX ends 0.
void draw_tile_box_border_alt_core(Machine &m) {
    uint16_t es = kIram;
    uint16_t di = screen_map_offset(m.al(), m.bx());
    m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(uint8_t(m.bx()) << 1));
    uint16_t orig_cx = m.cx();
    uint8_t width_cells = uint8_t(uint8_t(orig_cx) - 2);
    uint8_t ch = uint8_t(orig_cx >> 8);

    auto draw_row = [&](uint16_t row_start, uint16_t left, uint16_t fill, uint16_t right) {
        uint16_t tile_destination = row_start;
        m.wr16(es, tile_destination, left);
        tile_destination = uint16_t(tile_destination + 2);
        for (uint8_t i = 0; i < width_cells; ++i) {
            m.wr16(es, tile_destination, fill);
            tile_destination = uint16_t(tile_destination + 2);
        }
        m.wr16(es, tile_destination, right);
        return right;
    };

    uint16_t last_ax = draw_row(di, 0xFE6, 0xFE7, 0xFE8);
    di = uint16_t(di + 0x40);
    --ch;
    for (;;) {
        --ch;
        if (ch == 0)
            break;
        last_ax = draw_row(di, 0xFE9, 0xFD9, 0xFEA);
        di = uint16_t(di + 0x40);
    }
    last_ax = draw_row(di, 0xFEB, 0xFEC, 0xFED);

    m.ax() = last_ax;
    m.cx() = 0;
}

void draw_tile_box_border_alt(Machine &m) {
    draw_tile_box_border_alt_core(m);
    m.retf();
}

// rom: 0x36306E (near)
// Draws the selected battle member's equipped-item name into row 1 starting at column 1 (after
// clearing that row via clear_battle_field_tiles_core): the item word looked up via
// selected_battle_equipment_core for kBattleMember has AH=FFh ("no item"), which draws the fixed
// D_381998 placeholder from bank 8000h; otherwise it draws the item's name string from bank 9000h,
// offset 53E0h + id*1Bh. The original also calls lookup_table_byte on the item id (BX=0) and
// immediately discards the result (`mov ax, dx`) -- reproduced for fidelity, though it has no
// observable effect. CX ends at 0 on the "item present" path (the offset-computing loop's counter) but
// is left holding the item id on the "no item" path, since that loop never runs; DX ends holding the
// raw item word. Other registers follow draw_text_alt_core (0x363860).
void selected_equipment_name_or_placeholder_core(Machine &m) {
    clear_battle_field_tiles_core(m);
    m.set_al(m.rd8(kIram, kBattleMember));
    m.ax() = selected_battle_equipment_core(m); // mirrors the real `lcall L_36507D`'s AX result
    m.dx() = m.ax();
    m.set_al(uint8_t(m.dx()));
    m.bx() = 0;
    lookup_table_byte_core(m); // discarded next (`mov ax, dx`) -- dead in the original, kept for fidelity
    m.ax() = m.dx();
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.ax()));
    m.set_al(uint8_t(m.ax() >> 8));
    if (m.al() == 0xFF) {
        const TextPtr t = text(m, TextKey::selected_equipment_name_or_placeholder_1);
        m.ds() = t.segment;
        m.si() = t.offset;
    } else {
        uint16_t si = 0x53E0;
        while (uint8_t(m.cx()) != 0) {
            si = uint16_t(si + 0x1B);
            m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(uint8_t(m.cx()) - 1));
        }
        m.ds() = 0x9000;
        m.si() = si;
    }
    m.bx() = 0x0101;
    m.ax() = 1;
    draw_text_alt_core(m);
    text_done(m, TextKey::selected_equipment_name_or_placeholder_1);
}

void selected_equipment_name_or_placeholder(Machine &m) {
    selected_equipment_name_or_placeholder_core(m);
    m.ret();
}

// rom: 0x362EAC (near)
// Renders the "usable items" panel for one battle member (AL = actor id): if
// count_actor_available_items_core reports zero, shows the fixed two-line "nothing usable" label
// (D_38197C/D_381986) and returns; otherwise draws three item rows -- the actor's equipment slots at
// page+0, page+1, page+2, where page is kEquipmentPage[actor] -- each showing the item's class icon
// (draw_class_icon_or_label_core, DL=item id), its name (D_394BE0, bank 9000h, 8 bytes/entry) and its
// remaining-use count: a dash (D_38195C) for the FEh sentinel value, values clamped to 99 and formatted
// via number_to_digits_core/compact_battle_digits_core (padded with a leading "0" glyph, D_381955,
// below 10), or the fixed "empty" label (D_38194C/D_381955) when the slot itself is unequipped (FFh, or
// FEh with a FEh id). All registers are preserved (pushaw/ds/es/ss bracket the whole original); only
// screen-map memory (and the scratch byte at 0xDC72) changes. Quirk reproduced: BX (the screen column)
// is never reset between rows, so the icon draw on rows after the first starts from wherever the
// previous row's digit/label draw left BL -- the same kind of leftover-BX quirk as
// draw_battle_skill_list's empty-slot case.
void draw_actor_item_list_core(Machine &m) {
    SavedRegisters saved(m);
    constexpr uint16_t kSelectedPageScratch = 0xDC72;

    uint8_t actor = m.al();
    m.set_al(actor);
    count_actor_available_items_core(m);
    if (m.al() == 0) {
        m.wr16(m.ss(), kSpritePositions, 0x90);
        m.wr16(m.ss(), (kSpritePositions + 0x4), 0x90);
        m.wr16(m.ss(), (kSpritePositions + 0x8), 0x90);
        m.wr16(m.ss(), (kSpritePositions + 0xC), 0x90);
        const TextPtr t1 = text(m, TextKey::draw_actor_item_list_1);
        m.ds() = t1.segment;
        m.si() = t1.offset;
        m.bx() = 0x0B02;
        m.ax() = 1;
        draw_text_alt_core(m);
        text_done(m, TextKey::draw_actor_item_list_1);
        const TextPtr t2 = text(m, TextKey::draw_actor_item_list_2);
        m.ds() = t2.segment;
        m.si() = t2.offset;
        m.bx() = 0x0C02;
        m.ax() = 1;
        draw_text_alt_core(m);
        text_done(m, TextKey::draw_actor_item_list_2);
    } else {
        uint8_t page = m.rd8(kIram, uint16_t(kEquipmentPage + actor));
        m.wr8(kIram, kSelectedPageScratch, page);
        uint16_t item_base = uint16_t(kBattleActors + 0xE + actor * 0x20);

        uint16_t bx = 0x0B00; // bh=row, bl carries over between rows (reproduced quirk)
        for (uint8_t row = 0; row < 3; ++row) {
            uint16_t slot_si = uint16_t(item_base + 2 * (page + row));
            uint8_t lo = m.rd8(kIram, slot_si);
            uint8_t hi = m.rd8(kIram, uint16_t(slot_si + 1));
            bool empty = hi == 0xFF || (hi == 0xFE && lo == 0xFE);
            if (empty) {
                m.dx() = 0xFF;
                m.bx() = uint16_t((bx & 0xFF00) | 3);
                draw_class_icon_or_label_core(m);
                const TextPtr t6 = text(m, TextKey::draw_actor_item_list_6);
                m.ds() = t6.segment;
                m.si() = t6.offset;
                m.bx() = uint16_t((bx & 0xFF00) | 4);
                m.ax() = 1;
                draw_text_alt_core(m);
                text_done(m, TextKey::draw_actor_item_list_6);
                const TextPtr t7 = text(m, TextKey::draw_actor_item_list_7);
                m.ds() = t7.segment;
                m.si() = t7.offset;
                m.bx() = uint16_t((bx & 0xFF00) | 0xC);
                m.ax() = 1;
                draw_text_alt_core(m);
                text_done(m, TextKey::draw_actor_item_list_7);
                bx = uint16_t((bx & 0xFF00) | 0xC);
            } else {
                m.dx() = lo; // item id
                m.bx() = uint16_t((bx & 0xFF00) | 3);
                draw_class_icon_or_label_core(m);
                const TextPtr name = name_record(m, NameTable::Item, uint8_t(lo)); // D_394BE0
                m.ds() = name.segment;
                m.si() = name.offset;
                m.bx() = uint16_t((bx & 0xFF00) | 4);
                m.ax() = 1;
                draw_text_alt_core(m);

                if (hi == 0xFE) {
                    const TextPtr t5 = text(m, TextKey::draw_actor_item_list_5);
                    m.ds() = t5.segment;
                    m.si() = t5.offset;
                    m.bx() = uint16_t((bx & 0xFF00) | 0xC);
                    m.ax() = 1;
                    draw_text_alt_core(m);
                    text_done(m, TextKey::draw_actor_item_list_5);
                    bx = uint16_t((bx & 0xFF00) | 0xC);
                } else {
                    uint8_t count = hi >= 0x64 ? 0x63 : hi;
                    m.ds() = kIram;
                    m.si() = kDigits;
                    m.ax() = number_to_digits_core(m, count, kIram, kDigits);
                    m.ax() = compact_battle_digits_core(m);
                    uint16_t digit_count = m.ax();

                    uint16_t col;
                    if (count < 0xA) {
                        const TextPtr t4 = text(m, TextKey::draw_actor_item_list_4);
                        m.ds() = t4.segment;
                        m.si() = t4.offset;
                        m.bx() = uint16_t((bx & 0xFF00) | 0xC);
                        m.ax() = 1;
                        draw_text_alt_core(m);
                        text_done(m, TextKey::draw_actor_item_list_4);
                        m.ds() = kIram;
                        col = 0xD;
                    } else {
                        col = 0xC;
                    }
                    m.si() = kDigits;
                    m.dx() = m.rd8(m.ss(), kTextColorFlag);
                    m.bx() = uint16_t((bx & 0xFF00) | col);
                    m.ax() = digit_count;
                    draw_numeral_string_core(m);
                    bx = uint16_t((bx & 0xFF00) | col);
                }
            }
            bx = uint16_t(bx + 0x100); // `inc bh`
        }
    }
    saved.restore(m);
}

void draw_actor_item_list(Machine &m) {
    draw_actor_item_list_core(m);
    m.ret();
}

// bank 0x36 routines that wait for frames or call original code (yields), plus their surviving
// dependencies (routines that wait for frames or call original code).

// rom: 0x360142
// End-of-frame flush: flips the sprite/palette DMA buffers out, then either forces display bank 0
// (menu-state 5) or cycles bank 0x37 and a ~35-frame flicker counter (kDisplayFlickerCounter, 0xDC16)
// that flips kBlinkPhase every 0x23 ticks -- before waiting one vblank. AL is left however the taken
// branch last set it (leftover from the original's fallthrough).
void flush_battle_frame_core(Machine &m) {
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.out8(5, 0);
    uint8_t state = m.rd8(m.ss(), kBattleMenuState);
    if (state == 5) {
        m.set_al(0);
        m.out8(6, 0);
    } else {
        m.set_al(0x37);
        m.out8(6, 0x37);
        uint8_t flicker_phase = uint8_t(m.rd8(m.ss(), kDisplayFlickerCounter) + 1);
        m.wr8(m.ss(), kDisplayFlickerCounter, flicker_phase);
        m.set_al(flicker_phase);
        if (flicker_phase == 0x23) {
            m.wr8(m.ss(), kDisplayFlickerCounter, 0);
            uint8_t phase = uint8_t(m.rd8(m.ss(), kBlinkPhase) ^ 1);
            m.wr8(m.ss(), kBlinkPhase, phase);
            m.set_al(phase);
        }
    }
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009); // wait_vblank
}

void flush_battle_frame(Machine &m) {
    flush_battle_frame_core(m);
    m.retf();
}

// rom: 0x364F43 (near)
// Apply signed damage to target HP, clamp to zero/max HP, and redraw the HP display.
void battle_meter_step_core(Machine &m) {
    uint16_t entry_dx = m.dx();
    uint16_t entry_bx = m.bx();
    uint16_t entry_si = m.si();
    uint16_t entry_di = m.di();
    m.wr16(m.ss(), kMeterTarget, entry_dx);
    if (m.al() < 4) {
        uint8_t kind = m.rd8(m.ss(), kActionId);
        if (kind != 0xEE && kind != 0xD9)
            shake_screen_for_party_hit_core(m);
        m.wr8(m.ss(), kBattleFlashCounter, uint8_t(m.rd8(m.ss(), kBattleFlashCounter) + 1));
    }
    m.set_al(m.rd8(m.ss(), kActionTarget));
    battle_actor_pointer_core(m);
    uint16_t si = m.si();
    uint16_t previous_hp = m.rd16(m.ss(), uint16_t(si + 3));
    m.wr16(m.ss(), kMeterOldValue, previous_hp);
    // Signed damage is subtracted; negative damage heals, capped at the target's maximum HP.
    uint16_t remaining_hp = uint16_t(previous_hp - m.dx());
    uint16_t bx;
    if (remaining_hp & 0x8000) {
        bx = 0;
    } else {
        // `mov ax, word ptr ss:[si+5]` always runs here, whether or not the jb below is taken -- AX is
        // left holding the max_hp (or, on overflow, recomputed) all the way to the pushaw below.
        m.ax() = m.rd16(m.ss(), uint16_t(si + 5));
        uint16_t max_hp = m.ax();
        if (remaining_hp < max_hp) {
            bx = remaining_hp;
        } else {
            m.ax() = uint16_t(max_hp - previous_hp);
            m.wr16(m.ss(), kMeterTarget, m.ax());
            bx = max_hp;
        }
    }
    BattleActorState(m, si).write_hp(bx);
    m.bx() = bx; // the original computes the clamp in BX itself, so BX is still live here (not entry_bx)

    SavedRegisters saved(m);
    m.dx() = uint16_t((m.dx() & 0xFF00) | 0x0F); // dl = 0xf
    draw_battle_skill_list_core(m);
    saved.restore(m);

    m.di() = entry_di;
    m.si() = entry_si;
    m.bx() = entry_bx;
    m.dx() = entry_dx;
}

void battle_meter_step(Machine &m) {
    battle_meter_step_core(m);
    m.ret();
}

// rom: 0x364FB1 (near)
// Filter and store signed damage (negative heals), then apply it to the selected target's HP.
void set_battle_meter_target_core(Machine &m) {
    {
        // hook: the amount about to move the actor's HP meter (every battle damage path ends here). With no
        // subscribers DX is unchanged.
        const uint8_t actor = m.rd8(m.ss(), kActionTarget);
        const uint16_t hp = m.rd16(m.ss(), uint16_t(kBattleActors + actor * 0x20 + 3));
        m.dx() = uint16_t(hooks::filter(hooks::Value::BattleMeterChange, m, m.dx(),
                                        {actor, hp, m.rd8(m.ss(), kActingActor)}));
        hooks::emit(hooks::Event::BattleHpChange, m, {actor, int16_t(m.dx()), hp});
    }
    m.wr16(m.ss(), kMeterInputTarget, m.dx());
    m.set_al(m.rd8(m.ss(), kActionTarget));
    battle_meter_step_core(m);
}

void set_battle_meter_target(Machine &m) {
    set_battle_meter_target_core(m);
    m.ret();
}

// rom: 0x36A3DE (near)
// Appends an item-count footer line to the icon/message popup queue (see open_status_popup): for the
// actor at kActionTarget (0xDBFF) with id 4+ (an enemy slot), unless kPanelSpecialMode (0xEC83, a
// different flag from the panel-mode byte at 0xEC81 used elsewhere in this file) is 0 or the byte at
// 0xD6D1 is 1, queues a "count:" label (D_3836FE), the count itself (kItemUseCount, 0xDBF4, plus one,
// via format_number), a separator label (D_383688) and a blank-line label (D_3830DC); otherwise queues
// just the blank-line label on its own. Every register (including DS/ES/SS) is saved around the whole
// body and restored before returning, so the caller sees no register effects at all.
void queue_item_count_footer_core(Machine &m) {
    uint8_t actor = m.rd8(m.ss(), kActionTarget);
    bool show_count = false;
    if (actor >= 4 && m.rd8(m.ss(), kFooterPanelMode) != 0 && m.rd8(m.ss(), kEnemyCount) != 1)
        show_count = true;

    if (show_count) {
        uint8_t count = uint8_t(m.rd8(m.ss(), kItemUseCount) + 1); // `xor dx,dx; mov dl,..; inc dl`: DH=0
        const TextPtr t1 = text(m, TextKey::queue_item_count_footer_1);
        m.ds() = t1.segment;
        m.si() = t1.offset; // D_3836FE
        m.ax() = 0xFF;
        m.bx() = 0xFF;
        append_word_queue_core(m);
        text_done(m, TextKey::queue_item_count_footer_1);
        m.ds() = 0;
        m.ax() = count;
        format_number_core(m);
        m.ds() = 0;
        m.si() = kBattleNumberText;
        m.ax() = 0xFF;
        m.bx() = 0xFF;
        append_word_queue_core(m);
        const TextPtr t2 = text(m, TextKey::queue_item_count_footer_2);
        m.ds() = t2.segment;
        m.si() = t2.offset; // D_383688
        m.ax() = 0xFF;
        m.bx() = 0xFF;
        append_word_queue_core(m);
        text_done(m, TextKey::queue_item_count_footer_2);
    }
    // D_3830DC: the original appends it in each branch (two sites, so two keys)
    const TextKey footer =
        show_count ? TextKey::queue_item_count_footer_3 : TextKey::queue_item_count_footer_4;
    const TextPtr t = text(m, footer);
    m.ds() = t.segment;
    m.si() = t.offset;
    m.ax() = 0xFF;
    m.bx() = 0xFF;
    append_word_queue_core(m);
    text_done(m, footer);
}

void queue_item_count_footer_call_core(Machine &m) {
    SavedRegisters saved(m);
    queue_item_count_footer_core(m);
    saved.restore(m);
}
void queue_item_count_footer(Machine &m) {
    queue_item_count_footer_call_core(m);
    m.ret();
}

// rom: 0x36A557 (near)
// Builds and opens the "party member details" status popup: redraws the member display
// (build_battle_member_display), queues the member's name (icon_queue, D_38... at 0xD62F), the item-count
// footer (queue_item_count_footer), the item's remaining-use count (kMeterInputTarget, 0xDC0B, via
// format_number), a separator (D_3830E2), then opens the popup window from the composed queue at 0xD55A
// (open_status_popup). restore_saved_registers (0x36C4D7) is a no-op (just `ret`), so its call is skipped.
// Every register (including DS/ES/SS) is saved around the whole body and restored before returning.
void open_party_member_popup_core(Machine &m) {
    SavedRegisters saved(m);
    build_battle_member_display_core(m);

    m.ds() = 0;
    m.si() = kBattleTargetIcons;
    m.ax() = 0xFF;
    m.bx() = 0;
    append_word_queue_core(m);

    queue_item_count_footer_core(m);

    m.ds() = 0;
    m.ax() = m.rd16(m.ss(), kMeterInputTarget);
    format_number_core(m);

    m.ds() = 0;
    m.si() = kBattleNumberText;
    m.ax() = 0xFF;
    m.bx() = 0xFF;
    append_word_queue_core(m);

    const TextPtr t = text(m, TextKey::open_party_member_popup_1);
    m.ds() = t.segment;
    m.si() = t.offset; // D_3830E2
    m.ax() = 0xFF;
    m.bx() = 0xFF;
    append_word_queue_core(m);
    text_done(m, TextKey::open_party_member_popup_1);

    m.es() = 0;
    m.si() = kIconQueue; // icon_queue
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m);
        m.cs() = caller_cs;
    } // open_status_popup (not decompiled)

    saved.restore(m);
}

void open_party_member_popup(Machine &m) {
    open_party_member_popup_core(m);
    m.ret();
}

// rom: 0x36CF27
// Copies one of three battle-formation palettes (kFormationKind, 0xD70D: 0/1/2 select 16/23/29 colour
// entries -- kFormationCount1/2/3 at 0xD50B/0xD517/0xD523 -- into palette slot 4/5/6 respectively, from
// the shared formation-palette table D_3EB818 (segment 0xE000)) into the DMA staging buffer (kIram,
// 0xC400), and clears kFormationCleared (0xD70F). Every register (including DS/ES/SS) is saved around
// the whole body and restored before returning.
void copy_formation_palette_core(Machine &m) {
    SavedRegisters saved(m);
    m.wr8(m.ss(), kFormationCleared, 0);
    uint8_t kind = m.rd8(m.ss(), kFormationKind);
    uint16_t palette;
    uint16_t count_addr;
    if (kind == 1) {
        palette = 5;
        count_addr = kFormationMiddlePaletteCount;
    } else if (kind == 2) {
        palette = 6;
        count_addr = kFormationFrontPaletteCount;
    } else { // 0, or any other value (falls through the same as 0 in the original)
        palette = 4;
        count_addr = kFormationBackPaletteCount;
    }
    m.ds() = 0xE000;
    m.ax() = palette;
    m.cx() = m.rd8(m.ss(), count_addr);
    m.si() = 0xB818; // D_3EB818
    m.di() = kPaletteBuffer;
    copy_palettes_core(m);
    saved.restore(m);
}

void copy_formation_palette(Machine &m) {
    copy_formation_palette_core(m);
    m.retf();
}

// rom: 0x36D079
// Companion to copy_formation_palette: copies the SAME three formation palettes, but with each colour
// nudged by reading a source address that is advanced by 0x20 (one palette) per remaining "shown" count
// (kind 0: kFormationShown1 at 0xD508 iterations from a base at ss:0xD502; kind 1/2 read their base
// (0xD50E/0xD51A) via DS instead of SS -- an asymmetry in the original, reproduced as-is, harmless in
// practice since DS and SS are both 0 (IRAM) at these call sites) -- i.e. the "next" palette frame for
// whatever animation this is. Also clears kFormationCleared (0xD70F, via DS this time, again same
// address as copy_formation_palette's SS write). Only DS is saved/restored (`push ds`/`pop ds`); every
// other register is left exactly where the routine's own computation and copy_palettes_core leave it
// (AX=0, CX/DX/SI/DI as copy_palettes_core's inputs/outputs, BX = the palette slot 4/5/6, ES untouched).
void copy_formation_palette_next_core(Machine &m) {
    uint16_t entry_ds = m.ds();
    m.wr8(kIram, kFormationCleared, 0);
    uint8_t kind = m.rd8(kIram, kFormationKind);
    uint16_t bx, ax, dx;
    uint8_t cl;
    if (kind == 1) {
        bx = 5;
        ax = m.rd16(kIram, kFormationMiddlePaletteSource);
        cl = m.rd8(m.ss(), kFormationMiddlePaletteVariant);
        dx = m.rd8(m.ss(), kFormationMiddlePaletteCount);
    } else if (kind == 2) {
        bx = 6;
        ax = m.rd16(kIram, kFormationFrontPaletteSource);
        cl = m.rd8(m.ss(), kFormationFrontPaletteVariant);
        dx = m.rd8(m.ss(), kFormationFrontPaletteCount);
    } else { // 0, or any other value
        bx = 4;
        ax = m.rd16(m.ss(), kFormationBackPaletteSource);
        cl = m.rd8(m.ss(), kFormationBackPaletteVariant);
        dx = m.rd8(m.ss(), kFormationBackPaletteCount);
    }
    ax = uint16_t(ax + cl * 0x20);

    m.si() = ax;
    m.ds() = 0xE000;
    m.bx() = bx;
    m.ax() = bx;
    m.cx() = dx;
    m.dx() = dx;
    m.di() = kPaletteBuffer;
    copy_palettes_core(m);
    m.ds() = entry_ds;
}

void copy_formation_palette_next_call_core(Machine &m) { copy_formation_palette_next_core(m); }
void copy_formation_palette_next(Machine &m) {
    copy_formation_palette_next_call_core(m);
    m.retf();
}

// rom: 0x362E00
// Loads a "formation set" record selected by kFormationSetIndex (0xD6CE) from the banked ROM window at
// segment 0x2000 (bank-switched via port 0xC2, set to 0xD0 first): a table at offset 0x2F (6 bytes per
// entry -- tile source, palette source, tilemap source) and a parallel table at offset 0x149 (2 bytes per
// entry -- tile count), assembled into a 8-byte scratch record at ss:0xD551 (also saving the record's
// own bank-select byte at ss:0xD559, restored via the second `out 0xc2` before using it). Then copies the
// tiles (copy_tiles, AX=0x200), copies 4 palettes into the DI=0xC400/0xC600 staging buffer (DH selects
// which: DH==0 -> 0xC600, otherwise 0xC400 -- the caller's AL, carried through as DH), and draws a
// 0x1E x 0xA tilemap at row/col 0.
void load_formation_set_core(Machine &m) {
    uint8_t dh = m.al();
    m.dx() = uint16_t((m.dx() & 0x00FF) | (dh << 8)); // `mov dh, al`
    m.set_al(0xD0);
    m.out8(0xC2, m.al());
    m.ds() = 0x2000;
    uint8_t dl = m.rd8(m.ss(), kFormationSetIndex);
    m.dx() = uint16_t((m.dx() & 0xFF00) | dl); // `mov dl, byte ptr ss:[0xd6ce]`
    uint8_t bank_byte = m.rd8(m.ds(), dl);
    m.wr8(m.ss(), kPortraitPaletteBank, bank_byte);

    uint16_t graphics_record = uint16_t(0x2F + dl * 6);
    m.wr16(m.ss(), kPortraitTileSource, m.rd16(m.ds(), graphics_record));
    m.wr16(m.ss(), kPortraitPaletteSource, m.rd16(m.ds(), uint16_t(graphics_record + 2)));
    m.wr16(m.ss(), kPortraitMapSource, m.rd16(m.ds(), uint16_t(graphics_record + 4)));

    uint16_t tile_count_entry = uint16_t(0x149 + dl * 2);
    m.wr16(m.ss(), kPortraitTileCount, m.rd16(m.ds(), tile_count_entry));

    m.set_al(bank_byte);
    m.out8(0xC2, m.al());

    m.cx() = m.rd16(m.ss(), kPortraitTileCount);
    m.si() = m.rd16(m.ss(), kPortraitTileSource);
    m.ax() = 0x200;
    copy_tiles_core(m);

    m.cx() = 4;
    m.si() = m.rd16(m.ss(), kPortraitPaletteSource);
    m.ax() = 0;
    m.di() = dh == 0 ? kPaletteTargetBuffer : kPaletteBuffer;
    copy_palettes_core(m);

    m.si() = m.rd16(m.ss(), kPortraitMapSource);
    m.di() = kPortraitTileSource; // `pop di` in the original -- draw_tilemap_core doesn't touch DI itself
    m.bx() = 0;
    m.cx() = 0x0A1E;
    m.ax() = 0;
    draw_tilemap_core(m);
}

void load_formation_set(Machine &m) {
    load_formation_set_core(m);
    m.retf();
}

// rom: 0x363714 (near)
// Draws the party-status row: for each of kEnemyGroupCount (0xD6D4) party slots, builds and draws that
// member's name (build_party_name), their status icons (draw_status_icons, always at row 1, columns
// starting at 0x11), and their item-use count (one of kPartyItemCount1/2/3 at 0xD6D6/0xD6D8/0xD6DA --
// slot index 3+ reuses slot 0's field, an original quirk reproduced as-is) as a compacted digit string
// at column 0x1A. The row used for the count string (and only that -- not the icons) climbs by one
// member per slot, starting at row 0xB.
void draw_party_status_row_core(Machine &m) {
    m.ds() = kIram;
    m.ax() = 0;
    uint8_t cl = m.rd8(kIram, kEnemyGroupCount);
    m.cx() = cl; // `xor cx,cx` then `mov cl,..`: CH=0
    uint8_t bh = 0xA;
    uint8_t bl = 0;
    m.bx() = uint16_t(bh << 8); // `xor bx,bx` then `mov bh,0xa`

    while (cl != 0) {
        m.set_al(bl);
        m.di() = kBattleTargetIcons;
        build_party_name_core(m); // doesn't read/write BX/CX

        m.es() = 0;
        m.ds() = 0x9000;
        m.si() = kBattleTargetIcons; // D_39D62F
        bh = uint8_t(bh + 1);
        m.bx() = uint16_t((bh << 8) | 0x11);
        m.cx() = 1;
        draw_status_icons_core(m);
        m.bx() = uint16_t((bh << 8) | bl); // `pop bx`
        m.cx() = cl;                       // `pop cx`

        m.ds() = kIram;
        uint16_t count_addr = bl == 1   ? (kEnemyGroups + 0x3)
                              : bl == 2 ? (kEnemyGroups + 0x5)
                                        : (kEnemyGroups + 0x1);
        m.set_al(m.rd8(m.ss(), count_addr));
        m.set_ah(0);
        m.si() = kDigits;
        m.ax() = number_to_digits_core(m, m.ax(), m.ds(), m.si());
        m.ax() = compact_battle_digits_core(m);
        m.bx() = uint16_t((bh << 8) | 0x1A);
        m.dx() = m.rd8(m.ss(), kTextColorFlag);
        draw_numeral_string_core(m);       // bumps BL internally; CX/SI preserved
        m.cx() = cl;                       // `pop cx`
        m.bx() = uint16_t((bh << 8) | bl); // `pop bx` -- discards draw_numeral_string_core's own BL bump

        bl = uint8_t(bl + 1);
        cl = uint8_t(cl - 1);
        m.bx() = uint16_t((bh << 8) | bl);
        m.cx() = cl;
    }
}

void draw_party_status_row(Machine &m) {
    draw_party_status_row_core(m);
    m.ret();
}

// battle-menu dispatcher and its input handlers. 0x3611FF/0x36147B/0x3616CD handle the
// command/target/strategy menus' keypad input (kKeysRepeat, 0xC806) and both redraw directly and defer
// to the dispatcher (0x361D60) to rebuild a whole menu state's screen; 0x36198E resets battle-menu state
// and reopens the dispatcher. Several callees below aren't decompiled yet (the AI-dispatcher family and a
// handful of small unnamed helpers) and stay as call-outs.
// one path, reused as a plain flag on others)

// rom: 0x361D60 (near)
// Rebuilds the whole battle-command screen for the current kBattleMenuState (0xD6E2): state 4 is the
// item panel, 0 is the formation/status screen (also drawing the enemy group portraits,
// draw_enemy_group_portraits), 1/2 are the strategy/equipment target screens (which also pick an initial
// kTargetSlot, see the L_361FD0 rotation), 5/6/7 and the default (8, and anything else) redraw the
// status-sprite screen with a fresh formation set. Call-outs remain for the state-4 item-column reset
// (L_363434), plus dma_sprite_table/wait_vblank and draw_window_alt (bank 0x34, no core exposed). Always ends
// by falling through to a shared `ret` (L_3621EE); registers are whatever the taken branch's last call left
// them.
void battle_menu_dispatcher_core(Machine &m) {
    hooks::emit(hooks::Event::BattleMenu, m, {m.rd8(m.ss(), kBattleMenuState)});
    hide_and_redraw_battle_cursors_core(m);
    m.wr8(m.ss(), kCursorHidden, 0);
    m.wr8(m.ss(), kCommandCursorVisible, 0);
    m.wr16(m.ss(), (kSpritePositions + 0x24), 0x90);
    m.wr16(m.ss(), (kSpritePositions + 0x20), 0x90);
    m.wr16(m.ss(), (kSpritePositions + 0x28), 0x90);
    draw_battle_marker_sprite_alt_core(m);
    draw_battle_marker_sprite_alt2_core(m);
    draw_battle_marker_sprite_core(m);
    m.wr8(m.ss(), kCommandPage, 1);
    m.wr8(m.ss(), kBlinkPhase, 0);
    hide_battle_menu_sprites_core(m);

    uint8_t state = m.rd8(m.ss(), kBattleMenuState);
    if (state == 4) {
        // L_362144: item panel for the current kBattleMember.
        m.ds() = kIram;
        uint8_t member = m.rd8(kIram, kBattleMember);
        m.bx() = member;
        m.wr8(m.ss(), uint16_t(kTargetSlot + member), 0);
        place_equipment_column_cursor_core(m);
        draw_battle_status_icon_tiles_core(m);
        draw_battle_status_icon_tiles_core(m); // called twice in the original
        clear_battle_message_box_core(m);
        m.ax() = 0;
        m.set_al(member);
        draw_battle_member_boxes_core(m);
        m.ds() = 0x8000;
        m.es() = 0xB000;
        m.ax() = 0;
        m.bx() = 0x0A01;
        m.cx() = 0x041C;
        draw_tile_box_border_alt_core(m);
        m.ax() = 0;
        m.bx() = 0x0A01;
        m.cx() = 0x040F;
        draw_window_core(m);
        m.ax() = 0;
        m.bx() = 0x0001;
        m.cx() = 0x021C;
        draw_window_core(m);
        m.wr8(m.ss(), kTextColorFlag, 1);
        draw_party_status_row_core(m);
        m.wr8(m.ss(), kTextColorFlag, 0);
        m.ax() = 0;
        m.set_al(member);
        draw_actor_item_list_core(m);
        m.set_al(m.rd8(m.ss(), kBattleMember));
        count_actor_available_items_core(m);
        if (m.al() == 0) {
            const TextPtr t = text(m, TextKey::battle_menu_dispatcher_1);
            m.ds() = t.segment;
            m.si() = t.offset;
            m.bx() = 0x0101;
            m.ax() = 1;
            draw_text_alt_core(m);
            text_done(m, TextKey::battle_menu_dispatcher_1);
        } else {
            m.wr8(m.ss(), kTextColorFlag, 0);
            selected_equipment_name_or_placeholder_core(m);
            m.wr8(m.ss(), kTextColorFlag, 0);
        }
        return;
    }
    if (state == 0) {
        // L_36203F: formation/status screen.
        hide_battle_menu_sprites_core(m);
        dma_sprite_table_core(m);
        m.call_far(0x4000, 0x0009); // wait_vblank
        clear_battle_message_box_core(m);
        clear_battle_field_tiles_core(m);
        m.set_al(1);
        load_formation_set_core(m);
        build_formation_layer_records_core(m);
        m.ax() = 0x101;
        draw_enemy_group_portraits_core(m);
        m.ds() = 0x8000;
        m.ax() = 0x40;
        m.cx() = 0x68;
        m.si() = 0x0200; // D_380200
        copy_tiles_core(m);
        m.ds() = 0x9000;
        m.ax() = 0x13;
        m.cx() = 0x20;
        m.si() = 0x2480; // D_392480
        copy_tiles_core(m);
        m.ax() = 0;
        m.bx() = 0x0A01;
        m.cx() = 0x041C;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x4000;
            draw_window_alt_core(m);
            m.cs() = caller_cs;
        } // draw_window_alt
        m.ax() = 0xFF;
        draw_battle_member_boxes_core(m);
        m.wr8(m.ss(), kTextColorFlag, 0);
        draw_party_status_row_core(m);
        m.wr8(m.ss(), kTextColorFlag, 0);
        m.ds() = 0x9000;
        m.ax() = 0;
        m.cx() = 0x13;
        m.si() = 0x3A70; // D_393A70
        copy_tiles_core(m);
        m.si() = 0x3CD0; // D_393CD0
        m.bx() = 0x0B04;
        m.cx() = 0x0309;
        m.ax() = 1;
        m.dx() = 0x1E00;
        draw_tilemap_offset_core(m);
        m.wr16(m.ss(), kSpritePositions, 0x2058);
        m.wr16(m.ss(), (kSpritePositions + 0x4), 0x2858);
        m.wr16(m.ss(), (kSpritePositions + 0x8), 0x2060);
        m.wr16(m.ss(), (kSpritePositions + 0xC), 0x2860);
        m.ds() = kIram;
        m.wr16(m.ss(), (kSpritePositions + 0x10), 0x90);
        m.wr16(m.ss(), (kSpritePositions + 0x14), 0x90);
        m.wr16(m.ss(), (kSpritePositions + 0x18), 0x90);
        m.wr16(m.ss(), (kSpritePositions + 0x1C), 0x90);
        draw_battle_cursor_tiles_core(m, uint16_t(kSpriteBuffer + 0x10));
        m.ds() = kIram;
        m.wr16(m.ss(), (kSpritePositions + 0x20), 0x90);
        m.wr16(m.ss(), (kSpritePositions + 0x24), 0x90);
        m.wr16(m.ss(), (kSpritePositions + 0x28), 0x90);
        draw_battle_marker_sprite_alt_core(m);
        draw_battle_marker_sprite_alt2_core(m);
        draw_battle_marker_sprite_core(m);
        m.wr8(m.ss(), kBattleMenuHistoryCount, 0);
        return;
    }
    if (state == 1) {
        // L_361EB1: strategy target screen.
        clear_battle_message_box_core(m);
        m.ax() = 0;
        m.set_al(m.rd8(m.ss(), kBattleMember));
        draw_battle_member_boxes_core(m);
        draw_battle_status_icon_tiles_core(m);
        m.ds() = 0x8000;
        m.es() = 0xB000;
        m.ax() = 0;
        m.bx() = 0x0A01;
        m.cx() = 0x041C;
        draw_tile_box_border_alt_core(m);
        m.ax() = 0;
        m.bx() = 0x0A01;
        m.cx() = 0x040F;
        draw_window_core(m);
        m.ax() = 0;
        m.bx() = 0x0001;
        m.cx() = 0x021C;
        draw_window_core(m);
        m.wr8(m.ss(), kTextColorFlag, 1);
        draw_party_status_row_core(m);
        m.wr8(m.ss(), kTextColorFlag, 0);
        m.ax() = 0;
        m.set_al(m.rd8(m.ss(), kBattleMember));
        draw_actor_item_list_core(m);
        m.wr8(m.ss(), kTextColorFlag, 0);
        selected_equipment_name_or_placeholder_core(m);
        m.set_al(m.rd8(m.ss(), kBattleMember));
        count_actor_available_items_core(m);
        if (m.al() == 0) {
            const TextPtr t = text(m, TextKey::battle_menu_dispatcher_2);
            m.ds() = t.segment;
            m.si() = t.offset;
            m.bx() = 0x0101;
            m.ax() = 1;
            draw_text_alt_core(m);
            text_done(m, TextKey::battle_menu_dispatcher_2);
        } else {
            draw_battle_command_cursor_core(m);
            update_battle_marker_highlight_core(m);
        }
        return;
    }
    if (state == 2) {
        // L_361F42: equipment target screen, tail-shared with the kTargetSlot rotation below.
        draw_battle_status_icon_tiles_core(m);
        clear_battle_message_box_core(m);
        m.ax() = 0;
        m.set_al(m.rd8(m.ss(), kBattleMember));
        draw_battle_member_boxes_core(m);
        m.ds() = 0x8000;
        m.es() = 0xB000;
        m.ax() = 0;
        m.bx() = 0x0A01;
        m.cx() = 0x041C;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x4000;
            draw_window_alt_core(m);
            m.cs() = caller_cs;
        } // draw_window_alt
        m.ax() = 0;
        m.bx() = 0x0A01;
        m.cx() = 0x040F;
        draw_window_core(m);
        m.ax() = 0;
        m.bx() = 0x0001;
        m.cx() = 0x021C;
        draw_window_core(m);
        m.wr8(m.ss(), kTextColorFlag, 0);
        draw_party_status_row_core(m);
        m.wr8(m.ss(), kTextColorFlag, 0);
        m.ax() = 0;
        m.set_al(m.rd8(m.ss(), kBattleMember));
        draw_actor_item_list_core(m);
        m.set_al(m.rd8(m.ss(), kBattleMember));
        count_actor_available_items_core(m);
        if (m.al() == 0) {
            const TextPtr t = text(m, TextKey::battle_menu_dispatcher_3);
            m.ds() = t.segment;
            m.si() = t.offset;
            m.bx() = 0x0101;
            m.ax() = 1;
            draw_text_alt_core(m);
            text_done(m, TextKey::battle_menu_dispatcher_3);
        } else {
            m.wr8(m.ss(), kTextColorFlag, 0);
            selected_equipment_name_or_placeholder_core(m);
            m.wr8(m.ss(), kTextColorFlag, 0);
        }
        // L_361FD0: pick an initial kTargetSlot from how many rotation slots (0xD6D6/D6D8) are nonzero,
        // based on kEnemyGroupCount (0xD6D4).
        uint8_t member = m.rd8(m.ss(), kBattleMember);
        m.bx() = member;
        uint8_t rows = m.rd8(m.ss(), kEnemyGroupCount);
        uint8_t slot;
        if (rows == 2) {
            slot = m.rd8(m.ss(), (kEnemyGroups + 0x1)) != 0 ? 0 : 1;
        } else if (rows == 3) {
            if (m.rd8(m.ss(), (kEnemyGroups + 0x1)) != 0)
                slot = 0;
            else if (m.rd8(m.ss(), (kEnemyGroups + 0x3)) != 0)
                slot = 1;
            else
                slot = 2;
        } else {
            slot = 0; // rows==1, or any other value: default slot 0
        }
        m.wr8(m.ss(), uint16_t(kTargetSlot + member), slot);
        draw_battle_marker_sprite_core(m);
        draw_battle_cursor_tiles_core(m, uint16_t(kSpriteBuffer + 0x10));
        position_enemy_target_cursor_core(m);
        return;
    }
    if (state == 5 || state == 6 || state == 7) {
        // L_361E37: status-sprite screen with a fresh two-part formation layout.
        m.set_al(1);
        load_formation_set_core(m);
        clear_battle_message_box_core(m);
        clear_battle_field_tiles_core(m);
        build_formation_layer_records_core(m);
        m.ax() = 0x101;
        draw_enemy_group_portraits_core(m);
        m.ds() = kIram;
        m.ax() = 0xFF;
        draw_battle_member_boxes_core(m);
        m.wr8(m.ss(), kBlinkPhase, 0);
        m.ds() = 0x8000;
        m.ax() = 0;
        m.bx() = 0x0A01;
        m.cx() = 0x041C;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x4000;
            draw_window_alt_core(m);
            m.cs() = caller_cs;
        } // draw_window_alt
        m.ax() = 0x90;
        for (uint16_t addr :
             {int(kSpritePositions), (int(kSpritePositions) + 0x4), (int(kSpritePositions) + 0x8),
              (int(kSpritePositions) + 0xC), (int(kSpritePositions) + 0x10), (int(kSpritePositions) + 0x14),
              (int(kSpritePositions) + 0x18), (int(kSpritePositions) + 0x1C), (int(kSpritePositions) + 0x20),
              (int(kSpritePositions) + 0x24), (int(kSpritePositions) + 0x28)})
            m.wr16(m.ss(), addr, m.ax());
        m.ax() = 0;
        draw_battle_status_sprites_core(m);
        return;
    }
    // L_361DD0: default (state 8, and anything else) -- status-sprite screen, single formation layout.
    m.set_al(1);
    load_formation_set_core(m);
    clear_battle_message_box_core(m);
    clear_battle_field_tiles_core(m);
    m.ds() = kIram;
    m.wr8(m.ss(), kBlinkPhase, 0);
    m.ds() = 0x8000;
    m.ax() = 0;
    m.bx() = 0x0A01;
    m.cx() = 0x041C;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        draw_window_alt_core(m);
        m.cs() = caller_cs;
    } // draw_window_alt
    m.ax() = 0x90;
    for (uint16_t addr :
         {int(kSpritePositions), (int(kSpritePositions) + 0x4), (int(kSpritePositions) + 0x8),
          (int(kSpritePositions) + 0xC), (int(kSpritePositions) + 0x10), (int(kSpritePositions) + 0x14),
          (int(kSpritePositions) + 0x18), (int(kSpritePositions) + 0x1C), (int(kSpritePositions) + 0x20),
          (int(kSpritePositions) + 0x24), (int(kSpritePositions) + 0x28)})
        m.wr16(m.ss(), addr, m.ax());
    m.ax() = 0;
    draw_battle_status_sprites_core(m);
}

void battle_menu_dispatcher(Machine &m) {
    battle_menu_dispatcher_core(m);
    m.ret();
}

// rom: 0x36198E (near)
// Resets battle-menu scratch state (kBattleMenuState=5 plus four zeroed flags) and reopens the dispatcher.
void reset_and_reopen_battle_menu_core(Machine &m) {
    m.wr8(m.ss(), kBattleMenuState, 5);
    m.wr8(m.ss(), 0xDC5F, 0);
    m.wr8(m.ss(), 0xDC60, 0);
    m.wr8(m.ss(), 0xDC5E, 0);
    m.wr8(m.ss(), kPopupState, 0);
    m.wr8(m.ss(), kBattleEndState, 0);
    battle_menu_dispatcher_core(m);
}

void reset_and_reopen_battle_menu(Machine &m) {
    reset_and_reopen_battle_menu_core(m);
    m.ret();
}

// rom: 0x3611FF (near)
// Battle command-menu input: bits 0x40/0x10 of kKeysRepeat (0xC806) cycle kCommandPage (0xDC61) between
// 1 and 2 (identical logic on both bits -- reproduced as-is); bit 4 ("confirm") resets the four
// kBattleOrder entries and plays the confirm sound, then either (kCommandPage==1) logs the state and
// scans all four party members for "active" ones (status byte low bits clear, per
// battle_actor_pointer_core) -- for the members that need an action (bit 0x20 of that status set),
// rolling a random equipment row (random_range_core) and animation seed, looking up its flag byte
// (lookup_table_byte_core) and bucketing it into a kTargetSlot/kMemberActionFlags pair by its high nibble;
// inactive/unselectable members get their kBattleOrder entry reset and kMemberSkipFlags=0xFE -- or
// (kCommandPage!=1) just logs the state and forces state 6. Either way ends by opening the dispatcher
// (twice, when the member scan finds nobody needing a decision: reset_and_reopen_battle_menu_core already
// opens it once, and the original opens it again unconditionally afterward -- reproduced as-is) and
// falls through to redraw the command cursor using one of two screen-position sets.
namespace {
void battle_command_menu_input_body(Machine &m) {
    uint16_t keys = m.rd16(m.ss(), kKeysRepeat);
    if (keys & 0x40) {
        play_sound_effect_core(m, 0x92);
        m.set_al(m.rd8(m.ss(), kCommandPage) == 1 ? 2 : 1);
        m.wr8(m.ss(), kCommandPage, m.al());
    } else if (keys & 0x10) {
        play_sound_effect_core(m, 0x92);
        m.set_al(m.rd8(m.ss(), kCommandPage) == 1 ? 2 : 1);
        m.wr8(m.ss(), kCommandPage, m.al());
    } else if (keys & 4) {
        m.si() = kBattleOrder;
        m.wr8(m.ss(), uint16_t(m.si() + 0), 0xFF);
        m.wr8(m.ss(), uint16_t(m.si() + 3), 0);
        m.wr8(m.ss(), uint16_t(m.si() + 4), 0xFF);
        m.wr8(m.ss(), uint16_t(m.si() + 7), 1);
        m.wr8(m.ss(), uint16_t(m.si() + 8), 0xFF);
        m.wr8(m.ss(), uint16_t(m.si() + 0xB), 2);
        m.wr8(m.ss(), uint16_t(m.si() + 0xC), 0xFF);
        m.wr8(m.ss(), uint16_t(m.si() + 0xF), 3);
        play_sound_effect_core(m, 0x93);
        if (m.rd8(m.ss(), kCommandPage) == 1) {
            log_battle_menu_state_core(m);
            m.wr8(m.ss(), kEffectY, 0);
            for (uint8_t member = 0; member < 4; ++member) {
                m.set_al(member);
                battle_actor_pointer_core(m);
                uint16_t si = m.si();
                uint8_t status = m.rd8(m.ss(), uint16_t(si + 2));
                bool reset_order = true; // default: falls through to the order-reset below
                if ((status & 0xC3) == 0) {
                    if (!(status & 0x20)) {
                        m.wr8(m.ss(), kEffectY, uint8_t(m.rd8(m.ss(), kEffectY) + 1));
                        reset_order = false;
                    } else {
                        m.bx() = member;
                        m.wr8(m.ss(), uint16_t(kMemberSkipFlags + member), 0);
                        m.ax() = 0x0800;
                        random_range_core(m);
                        m.wr8(m.ss(), uint16_t(kMemberTargets + member), m.al());
                        m.ax() = 0x0700;
                        random_range_core(m);
                        m.wr8(m.ss(), uint16_t(kEquipmentPage + member), 0);
                        m.wr8(m.ss(), uint16_t(kEquipmentRow + member), m.al());
                        uint8_t row = m.al();
                        m.set_al(member);
                        battle_actor_pointer_core(m);
                        si = uint16_t(m.si() + 0xE + uint16_t(row) * 2);
                        uint8_t item_lo = m.rd8(m.ss(), si);
                        uint8_t item_hi = m.rd8(m.ss(), uint16_t(si + 1));
                        m.set_al(item_lo);
                        m.bx() = 0;
                        lookup_table_byte_core(m);
                        if ((m.al() & 1) && item_hi != 0xFF && item_hi != 0 && item_hi != 0xFE) {
                            m.set_al(item_lo);
                            m.bx() = 1;
                            lookup_table_byte_core(m);
                            uint8_t halved = uint8_t(m.al() >> 1);
                            m.set_al(item_lo);
                            m.bx() = 7;
                            lookup_table_byte_core(m);
                            uint8_t dl = uint8_t(halved | (m.al() & 0x80));
                            uint8_t bucket = uint8_t(dl & 0xE0);
                            m.bx() = member;
                            if (bucket == 0x20 || bucket == 0x60 || bucket == 0x80 || bucket == 0xA0) {
                                m.wr8(m.ss(), uint16_t(kTargetSlot + member), 0xFF);
                                m.wr8(m.ss(), uint16_t(kMemberActionFlags + member),
                                      item_lo == 0xF2 ? 2 : 0x40);
                            } else if (bucket == 0x40 || bucket == 0xC0 || bucket == 0xE0) {
                                m.wr8(m.ss(), uint16_t(kMemberActionFlags + member),
                                      bucket == 0x40 ? 0x80 : 0x20);
                            } else {
                                // the original's `cmp al,0` here is dead (always falls through): reproduced
                                // as an unconditional "else".
                                m.wr8(m.ss(), uint16_t(kMemberActionFlags + member), 1);
                            }
                            reset_order = false;
                        }
                        // else: lookup failed, or item_hi in {FF,0,FE} -- falls through to the order-reset.
                    }
                }
                if (reset_order) {
                    uint16_t order_off = uint16_t(uint16_t(member) * 4);
                    m.bx() = order_off;
                    m.si() = kBattleOrder;
                    m.wr8(m.ss(), uint16_t(m.bx() + m.si()), 0xFF);
                    m.wr8(m.ss(), uint16_t(m.bx() + m.si() + 3), member);
                    m.bx() = member;
                    m.wr8(m.ss(), uint16_t(kMemberSkipFlags + member), 0xFE);
                }
            }
            if (m.rd8(m.ss(), kEffectY) == 0)
                reset_and_reopen_battle_menu_core(m);
            else
                m.wr8(m.ss(), kBattleMenuState, 1);
        } else {
            log_battle_menu_state_core(m);
            m.wr8(m.ss(), kBattleMenuState, 6);
        }
        battle_menu_dispatcher_core(m);
        // L_36142B: jumps straight to the function exit, skipping the L_361430 epilogue below.
        return;
    }
    // L_361430: shared epilogue -- redraw the command cursor from one of two position sets.
    if (m.rd8(m.ss(), kCommandPage) == 1) {
        m.wr16(m.ss(), kSpritePositions, 0x2058);
        m.wr16(m.ss(), (kSpritePositions + 0x4), 0x2858);
        m.wr16(m.ss(), (kSpritePositions + 0x8), 0x2060);
        m.wr16(m.ss(), (kSpritePositions + 0xC), 0x2860);
    } else {
        m.wr16(m.ss(), kSpritePositions, 0x2060);
        m.wr16(m.ss(), (kSpritePositions + 0x4), 0x2860);
        m.wr16(m.ss(), (kSpritePositions + 0x8), 0x2068);
        m.wr16(m.ss(), (kSpritePositions + 0xC), 0x2868);
    }
    draw_battle_cursor_tiles_core(m);
}
} // namespace

// The original saves DS around the whole body (`push ds` ... `pop ds`): the dispatcher can change it.
void battle_command_menu_input_core(Machine &m) {
    const uint16_t ds = m.ds();
    battle_command_menu_input_body(m);
    m.ds() = ds;
}

void battle_command_menu_input(Machine &m) {
    battle_command_menu_input_core(m);
    m.ret();
}

// rom: 0x36147B (near)
// Equipment/target sub-menu input: bit 0x10 of kKeysRepeat moves the row/page cursor
// (kEquipmentRow/kEquipmentPage) up, bit 0x40 moves it down (both only when the current member has any
// usable item, count_actor_available_items_core), either way redrawing the selected item's name and
// falling through to the shared L_3616BA redraw; bit 4 ("confirm") looks up the selected slot's item via
// selected_battle_equipment_core (leaving BX wherever that call leaves it -- the original's own
// unexplained leftover-BX read at L_361562's store, reproduced as-is) and, when usable, buckets it into
// kTargetSlot/kMemberActionFlags like battle_command_menu_input and opens the dispatcher at state 2 or 4; bit
// 8 cancels the selection (kMemberSkipFlags) and moves on (select_next_battle_member). Falls through to
// L_3616BA (redraws the command cursor, marker highlight and item list) only from the row/page-move paths.
void battle_target_menu_input_core(Machine &m) {
    uint16_t keys = m.rd16(m.ss(), kKeysRepeat);
    m.ax() =
        keys; // real `mov ax, word ptr ss:[0xc806]`: AX stays this value on paths that touch it no further
    bool fall_to_3616ba = false;
    if (keys & 0x10) {
        uint8_t member = m.rd8(m.ss(), kBattleMember);
        m.set_al(member);
        count_actor_available_items_core(m);
        if (m.al() != 0) {
            play_sound_effect_core(m, 0x92);
            m.bx() = member;
            uint8_t row = m.rd8(m.ss(), uint16_t(kEquipmentRow + member));
            if (row != 0) {
                m.wr8(m.ss(), uint16_t(kEquipmentRow + member), uint8_t(row - 1));
            } else {
                uint8_t page = m.rd8(m.ss(), uint16_t(kEquipmentPage + member));
                if (page != 0) {
                    m.wr8(m.ss(), uint16_t(kEquipmentPage + member), uint8_t(page - 1));
                } else {
                    m.wr8(m.ss(), uint16_t(kEquipmentPage + member), 5);
                    m.wr8(m.ss(), uint16_t(kEquipmentRow + member), 2);
                }
            }
            selected_equipment_name_or_placeholder_core(m);
            fall_to_3616ba = true;
        }
    } else if (keys & 0x40) {
        uint8_t member = m.rd8(m.ss(), kBattleMember);
        m.set_al(member);
        count_actor_available_items_core(m);
        if (m.al() != 0) {
            play_sound_effect_core(m, 0x92);
            m.bx() = member;
            uint8_t row = m.rd8(m.ss(), uint16_t(kEquipmentRow + member));
            if (row != 2) {
                m.wr8(m.ss(), uint16_t(kEquipmentRow + member), uint8_t(row + 1));
            } else {
                uint8_t page = m.rd8(m.ss(), uint16_t(kEquipmentPage + member));
                if (page != 5) {
                    m.wr8(m.ss(), uint16_t(kEquipmentPage + member), uint8_t(page + 1));
                } else {
                    m.wr8(m.ss(), uint16_t(kEquipmentPage + member), 0);
                    m.wr8(m.ss(), uint16_t(kEquipmentRow + member), 0);
                }
            }
            selected_equipment_name_or_placeholder_core(m);
            fall_to_3616ba = true;
        }
    } else if (keys & 4) {
        uint8_t member = m.rd8(m.ss(), kBattleMember);
        m.set_al(member);
        count_actor_available_items_core(m);
        if (m.al() == 0) {
            // L_36168D: no item -- cancel the slot.
            m.ax() = 0x93;
            play_sound_effect_core(m, 0x93);
            m.dx() = uint16_t((m.dx() & 0xFF00) | 0xFF);
            m.set_al(member); // real `mov al, byte ptr ss:[0xd6e1]`
            uint16_t order_off = uint16_t(uint16_t(member) * 4);
            m.bx() = order_off;
            m.si() = kBattleOrder;
            m.wr8(m.ss(), uint16_t(m.bx() + m.si()), 0xFF);
            m.wr8(m.ss(), uint16_t(m.bx() + m.si() + 3), member);
            m.bx() = member;
            m.wr8(m.ss(), uint16_t(kMemberSkipFlags + member), 0xFF);
            select_next_battle_member_core(m);
        } else {
            m.ax() = member;
            // L_361562 saves DX around the equipment lookup; its unused DL=0 does not escape.
            m.ax() = selected_battle_equipment_core(m); // leaves BX at its own leftover value (see above)
            uint16_t bx = m.bx();
            m.wr8(m.ss(), uint16_t(bx - 0x23E4), m.al());
            uint8_t cl = m.al();
            m.cx() = uint16_t((m.cx() & 0xFF00) | cl); // mov cl, al
            uint8_t hi = m.ah();
            m.set_al(hi);
            if (hi == 0xFF || hi == 0 || (hi == 0xFE && cl == 0xFE)) {
                // L_361589 -> L_36159A: zero uses refuse before the item-table lookup.
                m.ax() = 0x2A;
                play_sound_effect_core(m, 0x2A);
            } else {
                m.set_al(cl);
                m.bx() = 0;
                lookup_table_byte_core(m);
                m.bx() = bx; // push bx / pop bx around the lookup
                bool usable = (m.al() & 1) != 0;
                if (!usable) {
                    m.ax() = 0x2A;
                    play_sound_effect_core(m, 0x2A);
                    const uint16_t dx = m.dx(); // L_3615A5 saves DX around the refusal text
                    m.wr8(m.ss(), kTextColorFlag, 0);
                    const TextPtr t = text(m, TextKey::battle_target_menu_input_1);
                    m.ds() = t.segment;
                    m.bx() = 0x0101;
                    m.ax() = 1;
                    m.si() = t.offset;
                    draw_text_alt_core(m);
                    text_done(m, TextKey::battle_target_menu_input_1);
                    m.dx() = dx;
                } else {
                    m.ax() = 0x93;
                    play_sound_effect_core(m, 0x93);
                    m.set_al(cl);
                    m.bx() = 1;
                    lookup_table_byte_core(m);
                    m.bx() = bx;
                    uint8_t halved = uint8_t(m.al() >> 1);
                    m.dx() = uint16_t((m.dx() & 0xFF00) | halved);
                    m.set_al(cl);
                    m.bx() = 7;
                    lookup_table_byte_core(m);
                    m.bx() = bx;
                    uint8_t dl = uint8_t(halved | (m.al() & 0x80));
                    m.dx() = uint16_t((m.dx() & 0xFF00) | dl);
                    uint8_t bucket = uint8_t(dl & 0xE0);
                    m.set_al(bucket);
                    bool redirect_to_361629 = false;
                    if (bucket == 0x20 || bucket == 0x60 || bucket == 0x80 || bucket == 0xA0) {
                        // L_361629, direct.
                        redirect_to_361629 = true;
                    } else if (bucket == 0x40 || bucket == 0xC0 || bucket == 0xE0) {
                        // L_361651: redirects to L_361629 when cl==0xab; otherwise picks kMemberActionFlags
                        // by whether the bucket is 0x40 (0x80) or 0xc0/0xe0 (0x20), then opens state 2.
                        if (cl == 0xAB) {
                            redirect_to_361629 = true;
                        } else {
                            m.bx() = member;
                            m.set_al(bucket == 0x40 ? 0x80 : 0x20);
                            m.wr8(m.ss(), uint16_t(kMemberActionFlags + member), m.al());
                            log_battle_menu_state_core(m);
                            m.wr8(m.ss(), kBattleMenuState, 2);
                            battle_menu_dispatcher_core(m);
                        }
                    } else {
                        // the original's `cmp al,0` here is dead (always falls through): reproduced as an
                        // unconditional "else" -- opens state 4 instead of state 2.
                        log_battle_menu_state_core(m);
                        m.wr8(m.ss(), kBattleMenuState, 4);
                        battle_menu_dispatcher_core(m);
                    }
                    if (redirect_to_361629) {
                        m.bx() = member;
                        m.wr8(m.ss(), uint16_t(kTargetSlot + member), 0xFF);
                        m.wr8(m.ss(), uint16_t(kMemberTargets + member), 0xFF);
                        m.set_al(cl == 0xF2 ? 2 : 0x40);
                        m.wr8(m.ss(), uint16_t(kMemberActionFlags + member), m.al());
                        select_next_battle_member_core(m);
                    }
                }
            }
        }
    } else if (keys & 8) {
        // L_361499: reached only when none of bits 0x10/0x40/4 are set.
        m.ax() = 0x94;
        play_sound_effect_core(m, 0x94);
        backtrack_battle_menu_core(m); // L_3618F4: not decompiled
        battle_menu_dispatcher_core(m);
    }
    if (fall_to_3616ba) {
        // L_3616BA.
        draw_battle_command_cursor_core(m);
        update_battle_marker_highlight_core(m);
        m.ax() = 0;
        m.set_al(m.rd8(m.ss(), kBattleMember));
        draw_actor_item_list_core(m);
        selected_equipment_name_or_placeholder_core(m);
    }
}

void battle_target_menu_input(Machine &m) {
    battle_target_menu_input_core(m);
    m.ret();
}

// rom: 0x3616CD (near)
// Strategy-list cursor input: bits 0x20/0x80 of kKeysRepeat cycle kTargetSlot (0xDC66) forward/backward
// among the kEnemyGroupCount (0xD6D4) rotation slots that have a nonzero party-item-count entry
// (0xD6D6/D6D8/D6DA), skipping empty ones (the search always terminates since the current slot is never
// itself removed from the count table); bit 4 ("confirm") adds 4 to the selected slot and moves on
// (select_next_battle_member). All three end with position_enemy_target_cursor. AX is genuinely preserved
// (pushed/popped around the whole original body); every other register is whatever the last call left.
void battle_strategy_menu_input_core(Machine &m) {
    uint16_t entry_ax = m.ax();
    uint16_t keys = m.rd16(m.ss(), kKeysRepeat);
    if (keys & 0x80) {
        // L_361783: cycle backward.
        play_sound_effect_core(m, 0x92);
        uint8_t member = m.rd8(m.ss(), kBattleMember);
        m.bx() = member;
        uint8_t rows = m.rd8(m.ss(), kEnemyGroupCount);
        if (rows != 1) {
            uint8_t limit = rows == 2 ? 2 : 3;
            uint8_t wrap = rows == 2 ? 1 : 2;
            uint8_t al = m.rd8(m.ss(), uint16_t(kTargetSlot + member));
            uint8_t tries = 0;
            for (;;) {
                al = uint8_t(al - 1) == 0xFF ? wrap : uint8_t(al - 1);
                uint16_t si = uint16_t((kEnemyGroups + 0x1) + uint16_t(al) * 2);
                if (m.rd8(m.ss(), si) != 0)
                    break;
                if (++tries == limit)
                    break;
            }
            m.wr8(m.ss(), uint16_t(kTargetSlot + member), al);
        }
        position_enemy_target_cursor_core(m);
    } else if (keys & 0x20) {
        // L_3616FD: cycle forward.
        play_sound_effect_core(m, 0x92);
        uint8_t member = m.rd8(m.ss(), kBattleMember);
        m.bx() = member;
        uint8_t rows = m.rd8(m.ss(), kEnemyGroupCount);
        if (rows != 1) {
            uint8_t limit = rows == 2 ? 2 : 3;
            uint8_t wrap_at = rows == 2 ? 2 : 3;
            uint8_t al = m.rd8(m.ss(), uint16_t(kTargetSlot + member));
            uint8_t tries = 0;
            for (;;) {
                al = uint8_t(al + 1) == wrap_at ? 0 : uint8_t(al + 1);
                uint16_t si = uint16_t((kEnemyGroups + 0x1) + uint16_t(al) * 2);
                if (m.rd8(m.ss(), si) != 0)
                    break;
                if (++tries == limit)
                    break;
            }
            m.wr8(m.ss(), uint16_t(kTargetSlot + member), al);
        }
        position_enemy_target_cursor_core(m);
    } else if (keys & 4) {
        // L_361809: confirm.
        play_sound_effect_core(m, 0x93);
        uint8_t member = m.rd8(m.ss(), kBattleMember);
        m.bx() = member;
        uint8_t slot = m.rd8(m.ss(), uint16_t(kTargetSlot + member));
        m.wr8(m.ss(), uint16_t(kMemberTargets + member), uint8_t(slot + 4));
        select_next_battle_member_core(m);
    } else if (keys & 8) {
        play_sound_effect_core(m, 0x94);
        backtrack_battle_menu_core(m); // L_3618F4: not decompiled
    }
    // else: no matching key -- immediate `pop ax; ret`.
    m.ax() = entry_ax;
}

void battle_strategy_menu_input(Machine &m) {
    battle_strategy_menu_input_core(m);
    m.ret();
}

// AI-dispatcher-family leaves (battle order shuffle/priority, AI fallback target picker,
// small record-scan helpers).

// rom: 0x363FE1 (near)
// Copies one byte DI -> SI, advances DI by 0x1F (a 31-byte record stride) and SI by 1. AL ends holding
// the copied byte; every other register is untouched.
void copy_byte_advance_stride_core(Machine &m) {
    uint8_t al = m.rd8(m.ss(), m.di());
    m.set_al(al);
    m.wr8(m.ss(), m.si(), al);
    m.di() = uint16_t(m.di() + 0x1F);
    m.si() = uint16_t(m.si() + 1);
}

void copy_byte_advance_stride(Machine &m) {
    copy_byte_advance_stride_core(m);
    m.ret();
}

// rom: 0x363FEC (near)
// AL = a row count, DL = a row value to compare against [SI], BH = a second row count. If [SI] equals
// DL, records AL at 0xD0E3. Advances a DI cursor from 0xD000 and an SI cursor from 0xDAFA, each by
// 0x1F (31) bytes per unit of AL/BH respectively (an inlined "stride to the Nth record" walk), then
// copies 0x1F bytes DS:SI (DS forced to SS first) to SS:DI via copy_bytes_ds_si_to_ss_di_core.
// DS/SI/DI are pushed/popped around the whole body, but SI is bumped by 1 again afterward, so SI ends
// one past the caller's value; DI ends back at the caller's value. DX's high byte (DH) is clobbered to
// the entry AL (row count); AX and DS end at SS's value (`mov ax,ss; mov ds,ax`, done to set DS before
// the copy -- AX is along for the ride).
void stride_copy_row_core(Machine &m) {
    uint16_t saved_ds = m.ds(), saved_si = m.si(), saved_di = m.di();
    uint8_t row_count = m.al();
    m.dx() = uint16_t((uint16_t(row_count) << 8) | uint8_t(m.dx())); // `mov dh, al`: DL unchanged
    uint8_t al = m.rd8(m.ss(), m.si());
    m.set_al(al);
    uint8_t dl = uint8_t(m.dx());
    if (al == dl)
        m.wr8(m.ss(), kDialogueLeader, row_count);
    al = row_count;
    m.set_al(al);
    uint16_t di = kGameState;
    while (al != 0) {
        di = uint16_t(di + 0x1F);
        --al;
    }
    m.set_al(al);
    al = uint8_t(m.bx() >> 8);
    m.set_al(al);
    uint16_t si = kPartySortRecords;
    while (al != 0) {
        si = uint16_t(si + 0x1F);
        --al;
    }
    m.set_al(al);
    m.ax() = m.ss(); // `mov ax, ss; mov ds, ax`
    m.ds() = m.ss();
    m.di() = di;
    m.si() = si;
    m.cx() = uint16_t((m.cx() & 0xFF00) | 0x1F);
    copy_bytes_ds_si_to_ss_di_core(m);
    m.di() = saved_di;
    m.si() = uint16_t(saved_si + 1);
    m.ds() = saved_ds;
}

void stride_copy_row(Machine &m) {
    stride_copy_row_core(m);
    m.ret();
}

// rom: 0x360CB7 (near)
// Zero dead actors' HP and apply poison recovery/damage to actors that are not stoned.
void clear_or_flag_actor_records_core(Machine &m) {
    m.cx() = uint16_t(m.cx() & 0xFF00);          // mov cl, 0
    m.bx() = uint16_t((m.bx() & 0xFF00) | 0x1F); // mov bl, 1Fh
    do {
        m.set_al(uint8_t(m.cx()));
        battle_actor_pointer_core(m);
        const uint8_t status = m.rd8(m.ss(), uint16_t(m.si() + 2));
        m.set_al(status);
        if (status & 1) {
            m.ax() = 0;
            BattleActorState(m, m.si()).write_hp(0);
        } else if (!(status & 2) && (status & 0x10)) {
            apply_status_10_effect_core(m); // CL = the actor; BX/CX preserved
        }
        m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.cx() + 1));
        m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.bx() - 1));
    } while (uint8_t(m.bx()) != 0);
}

void clear_or_flag_actor_records(Machine &m) {
    clear_or_flag_actor_records_core(m);
    m.ret();
}

// Compare adjacent active actors' agility and swap their action entries when the next is at least as fast.
void priority_bubble_pass_core(Machine &m) {
    uint16_t saved_cx = m.cx();
    uint8_t comparisons_left = uint8_t(uint8_t(m.rd8(m.ss(), kEnemyCount) - 1) + 4);
    m.wr8(m.ss(), kOrderComparisonsLeft, comparisons_left);
    uint16_t bx = uint16_t(kBattleOrder + 3);
    do {
        m.set_al(m.rd8(m.ss(), bx));
        battle_actor_pointer_core(m);
        uint8_t status = m.rd8(m.ss(), uint16_t(m.si() + 2));
        m.set_al(status);
        // Original quirk: an inactive first actor stalls the remaining comparisons at this entry.
        if ((status & 3) == 0) {
            m.di() = m.si();
            bx = uint16_t(bx + 4);
            m.set_al(m.rd8(m.ss(), bx));
            battle_actor_pointer_core(m);
            status = m.rd8(m.ss(), uint16_t(m.si() + 2));
            m.set_al(status);
            if ((status & 3) == 0) {
                uint8_t dl = m.rd8(m.ss(), uint16_t(m.di() + 9));
                uint8_t dh = m.rd8(m.ss(), uint16_t(m.si() + 9));
                m.dx() = uint16_t((uint16_t(dh) << 8) | dl);
                if (dh >= dl) {
                    m.si() = uint16_t(bx - 3);
                    m.di() = uint16_t(bx - 7);
                    swap_battle_order_entries_core(m);
                }
            }
        }
        m.bx() = bx;
        --comparisons_left;
    } while (comparisons_left != 0);
    m.wr8(m.ss(), kOrderComparisonsLeft, 0);
    m.cx() = saved_cx;
}

// rom: 0x36115C (near)
// One agility-ordering pass; see priority_bubble_pass_core for the inactive-entry quirk.
void priority_bubble_pass(Machine &m) {
    priority_bubble_pass_core(m);
    m.ret();
}

// rom: 0x36110F
// Randomly swap action-order entries before the agility ordering passes.
void battle_order_shuffle_core(Machine &m) {
    m.si() = kBattleOrder;
    uint8_t enemy_count = m.rd8(m.ss(), kEnemyCount);
    uint8_t shuffles = 0x32;
    do {
        m.set_ah(uint8_t(enemy_count + 4 - 1));
        m.set_al(0);
        random_range_core(m);
        m.bx() = uint16_t((m.bx() & 0xFF00) | m.al());
        m.set_ah(uint8_t(uint8_t(enemy_count - 1) + 4));
        m.set_al(0);
        random_range_core(m);
        battle_order_entry_pointer_core(m);
        m.di() = m.si();
        m.set_al(uint8_t(m.bx()));
        battle_order_entry_pointer_core(m);
        swap_battle_order_entries_core(m);
        --shuffles;
    } while (shuffles != 0);
    // Fifty random swaps, then floor((enemy count + 4) / 4) agility passes.
    uint8_t passes = uint8_t((enemy_count + 4) >> 2);
    m.set_al(passes);
    m.cx() = uint16_t((m.cx() & 0xFF00) | passes);
    uint8_t remaining = passes;
    do {
        priority_bubble_pass_core(m);
        --remaining;
    } while (remaining != 0);
    m.cx() = uint16_t((m.cx() & 0xFF00) | remaining);
}

void battle_order_shuffle(Machine &m) {
    battle_order_shuffle_core(m);
    m.ret();
}

// rom: 0x361049 (near)
// Count usable equipped actions for actor AL, preserving the original use-count sentinel rules.
void count_action_order_alive_core(Machine &m) {
    uint16_t saved_bx = m.bx(), saved_cx = m.cx(), saved_dx = m.dx(), saved_si = m.si(), saved_di = m.di();
    battle_actor_pointer_core(m);
    uint16_t si = uint16_t(m.si() + 0xE);
    uint8_t count = 0;
    for (uint8_t cl = 0; cl < 8; ++cl) {
        uint8_t dl = m.rd8(m.ss(), uint16_t(si + 1));
        if (dl != 0xFF && dl != 0) {
            m.set_al(m.rd8(m.ss(), si));
            m.bx() = 0;
            lookup_table_byte_core(m);
            if (m.al() & 1)
                ++count;
        }
        si = uint16_t(si + 2);
    }
    m.set_al(count);
    m.bx() = saved_bx;
    m.cx() = saved_cx;
    m.dx() = saved_dx;
    m.si() = saved_si;
    m.di() = saved_di;
}

void count_action_order_alive(Machine &m) {
    count_action_order_alive_core(m);
    m.ret();
}

// rom: 0x3619B6 (near)
// Fades all 16 palettes to black over 16 frames: 0x10 passes, each darkening all 16 palettes one step
// (darken_palette_step_core), DMAing the sprite table, waiting a frame, disabling both tile-map
// windows (OUT 5/6 = 0), then DMAing palettes. The outer pass count is pushed/popped around the inner
// body (so it survives darken_palette_step_core's own use of CX); final CX = 0. AX ends at whatever
// dma_palettes_core leaves it.
void fade_all_palettes_to_black_core(Machine &m) {
    // A native palette fade: a held battle picture shades with it, then stays black until the field.
    hooks::EventScope presentation(m, hooks::Event::PresentationPicture, {3}, {4});
    uint8_t outer = 0x10;
    do {
        m.ax() = 0;
        m.cx() = 0x10;
        darken_palette_step_core(m);
        dma_sprite_table_core(m);
        m.call_far(0x4000, 0x0009); // wait_vblank
        m.out8(5, 0);
        m.out8(6, 0);
        dma_palettes_core(m);
        --outer;
    } while (outer != 0);
    m.cx() = 0;
}

void fade_all_palettes_to_black(Machine &m) {
    fade_all_palettes_to_black_core(m);
    m.ret();
}

// Reroll a target in the packed random range until it is present and neither dead nor stoned.
void ai_pick_random_alive_target(Machine &m, uint16_t ax_val, uint8_t result_flags) {
    for (;;) {
        m.ax() = ax_val;
        random_range_core(m);
        uint8_t bl = m.al();
        uint8_t status = m.rd8(m.ss(), uint16_t(kBattleActors + uint16_t(bl) * 0x20 + 2));
        m.set_al(status);
        if (status == 0xFF || (status & 3))
            continue;
        m.wr8(m.ss(), uint16_t(m.di() + 1), bl);
        m.wr8(m.ss(), uint16_t(m.di() + 2), result_flags);
        return;
    }
}

// Encode the fixed target/flags fallback for an AI action.
void ai_write_fixed_action2(Machine &m) {
    m.wr8(m.ss(), uint16_t(m.di() + 2), 2);
    m.wr8(m.ss(), uint16_t(m.di() + 1), 0);
}

// rom: 0x360F17 (near)
// Choose a target from the action's target class and the actor's confusion flag.
void resolve_ai_action_fallback_core(Machine &m) {
    SavedRegisters saved(m);
    uint8_t disposition = m.rd8(m.ss(), kAiDisposition);
    if (disposition >= 0xFD) {
        m.wr8(m.ss(), m.di(), disposition);
    } else if (disposition == 0xF2) {
        ai_write_fixed_action2(m);
    } else {
        m.set_al(disposition);
        m.bx() = 1;
        lookup_table_byte_core(m);
        uint8_t cl = m.al();
        m.si() = m.dx();
        uint8_t actor_status = m.rd8(m.ss(), uint16_t(m.si() + 2));
        uint8_t target_class = uint8_t(cl >> 5);
        if (actor_status & 0x20) {
            if (target_class == 0 || target_class == 6)
                ai_pick_random_alive_target(m, 0x0300, 1);
            else
                ai_write_fixed_action2(m);
        } else {
            switch (target_class) {
            case 0:
                ai_pick_random_alive_target(m, 0x1E04, 0x10);
                break;
            case 4:
            case 5:
                ai_pick_random_alive_target(m, 0x0300, 1);
                break;
            default:
                ai_write_fixed_action2(m);
                break;
            }
        }
    }
    saved.restore(m);
}

void resolve_ai_action_fallback(Machine &m) {
    resolve_ai_action_fallback_core(m);
    m.ret();
}

// ---------------------------------------------------------------------------------------------------------
// the battle loop, enemy AI and battle input dispatchers (bank 0x36)

void execute_order_entry_core(Machine &m);

namespace {
constexpr uint16_t kActorStatus = 2;     // actor record: status byte (bit 0 dead, 1 stone, ...)
constexpr uint16_t kActorHp = 3;         // actor record: current HP word
constexpr uint16_t kActorSkills = 0xE;   // actor record: 8 (id, uses) skill/item pairs
constexpr uint16_t kActorGroupId = 0x1F; // actor record: enemy id (the group it belongs to)

bool parity8(uint8_t value) { return battle_priority_parity8(value); }

// FLAGS after `and r8,r8`/`test r8,imm`/`or` with the given result (CF=OF=AF=0); cf=true then applies `clc;
// cmc`.
void set_logic_flags8(Machine &m, uint8_t result, bool cf = false) {
    uint16_t f = uint16_t(m.flags() & ~(emu::CF | emu::ZF | emu::SF | emu::PF | emu::AF | emu::OF));
    if (result == 0)
        f |= emu::ZF;
    if (result & 0x80)
        f |= emu::SF;
    if (parity8(result))
        f |= emu::PF;
    if (cf)
        f |= emu::CF;
    m.flags() = f;
}

uint8_t actor_status(Machine &m, uint8_t actor) {
    return m.rd8(m.ss(), uint16_t(kBattleActors + uint16_t(actor) * 0x20 + kActorStatus));
}
} // namespace

// rom: 0x360B2E (near)
// Resolve a party/group target to a live actor, optionally cycling to another enemy group.
void resolve_action_target_core(Machine &m) {
    const uint16_t di = m.di();
    uint8_t target = m.rd8(m.ss(), uint16_t(di + 1));
    auto cancel = [&](uint8_t tested) {
        m.wr8(m.ss(), di, 0xFF);
        set_logic_flags8(m, tested, true);
    };
    auto found = [&](uint8_t actor) { // L_360BEE
        m.wr8(m.ss(), kActionTarget, actor);
        uint8_t group = m.rd8(m.ss(), kEffectY);
        m.wr8(m.ss(), kTargetGroup, group);
        set_logic_flags8(m, group);
    };
    auto group_flags_or_cancel = [&]() { // L_360B94
        uint8_t target_flags = uint8_t(m.rd8(m.ss(), uint16_t(di + 2)) & 0xA0);
        if (target_flags == 0)
            found(0);
        else
            cancel(target_flags);
    };
    if (target < 4) { // L_360BFE
        m.wr8(m.ss(), kActionTarget, target);
        uint8_t status = actor_status(m, target);
        if ((status & 3) == 0)
            set_logic_flags8(m, status); // L_360C15: `and al,al` on the status byte
        else
            cancel(uint8_t(status & 3));
    } else if ((m.rd8(m.ss(), uint16_t(di + 2)) & 0xA0) == 0) {
        set_logic_flags8(m, target); // L_360C15 with AL = the target again (push/pop ax)
    } else if (int8_t(target) >= 7) {
        group_flags_or_cancel();
    } else {
        m.wr8(m.ss(), kEffectY, uint8_t(target - 4));
        m.wr8(m.ss(), kEffectX, 0);
        for (;;) {
            // L_360B5A: look for a live member of the current group
            uint8_t group = m.rd8(m.ss(), kEffectY);
            uint8_t enemy_id = m.rd8(m.ss(), uint16_t(kEnemyGroups + uint16_t(group) * 2));
            if (enemy_id == 0xFF) {
                group_flags_or_cancel();
                break;
            }
            int matching_actor = -1;
            for (uint8_t actor = 4; actor != 0x1F; ++actor) {
                uint16_t rec = uint16_t(kBattleActors + actor * 0x20);
                if ((m.rd8(m.ss(), uint16_t(rec + kActorStatus)) & 3) == 0 &&
                    m.rd8(m.ss(), uint16_t(rec + kActorGroupId)) == enemy_id) {
                    matching_actor = actor;
                    break;
                }
            }
            if (matching_actor >= 0) {
                found(uint8_t(matching_actor));
                break;
            }
            // L_360B8C: without the retarget option, give up
            if ((m.rd8(m.ss(), kBattleOptions) & 2) == 0) {
                group_flags_or_cancel();
                break;
            }
            // L_360BA5: step to the next group; a full lap (steps == group count) re-enters here
            // straight away without searching, so the lap after it searches again.
            // Original quirk: no live group and no FFh type sentinel can keep this search cycling forever.
            bool search = false;
            while (!search) {
                if (m.rd8(m.ss(), kEffectX) == 0) {
                    m.wr8(m.ss(), kEffectRowY, 0);
                    m.wr8(m.ss(), kEffectX, 0xFF);
                }
                uint8_t rows = m.rd8(m.ss(), kEnemyGroupCount);
                uint8_t next = uint8_t(m.rd8(m.ss(), kEffectY) + 1);
                m.wr8(m.ss(), kEffectY, next >= rows ? 0 : next);
                uint8_t steps = uint8_t(m.rd8(m.ss(), kEffectRowY) + 1);
                m.wr8(m.ss(), kEffectRowY, steps);
                search = steps != rows;
            }
        }
    }
}

void resolve_action_target(Machine &m) {
    resolve_action_target_core(m);
    m.ret();
}

// rom: 0x364D3E (near)
// Advance to the next party member able to choose a command, or close command selection.
void select_next_battle_member_core(Machine &m) {
    SavedRegisters saved(m);
    uint8_t member = m.rd8(m.ss(), kBattleMember);
    bool opened = false;
    for (bool more = member != 3; more;) {
        ++member; // wraps: a member past 4 counts on round to 4
        m.cx() = uint16_t((m.cx() & 0xFF00) | member);
        if (member == 4)
            break;
        m.set_al(member);
        battle_actor_pointer_core(m);
        uint8_t status = m.rd8(m.ss(), uint16_t(m.si() + kActorStatus));
        m.set_al(status);
        if (status & (3 | 0x80 | 0x40 | 0x20))
            continue;
        m.wr8(m.ss(), kBattleMember, member);
        log_battle_menu_state_core(m);
        m.wr8(m.ss(), kBattleMenuState, 1);
        battle_menu_dispatcher_core(m);
        opened = true;
        break;
    }
    if (!opened)
        reset_and_reopen_battle_menu_core(m);
    saved.restore(m);
}

void select_next_battle_member(Machine &m) {
    select_next_battle_member_core(m);
    m.ret();
}

namespace {
// The enemy-target cursor: four 8x8 sprites (positions at sprite_buffer+12h..1Eh) around the chosen group,
// plus the marker sprite position at +22h.
void set_enemy_target_cursor(Machine &m, uint16_t top_left, uint16_t marker) {
    m.wr16(m.ss(), (kSpritePositions + 0x10), top_left);
    m.wr16(m.ss(), (kSpritePositions + 0x14), uint16_t(top_left + 0x800));
    m.wr16(m.ss(), (kSpritePositions + 0x18), uint16_t(top_left + 8));
    m.wr16(m.ss(), (kSpritePositions + 0x1C), uint16_t(top_left + 0x808));
    m.ax() = marker;
    m.wr16(m.ss(), (kSpritePositions + 0x20), marker);
}
} // namespace

// rom: 0x3632EC (near)
// Places the enemy-target cursor on the current member's chosen group (kTargetSlot[kBattleMember]),
// laid out by the number of enemy groups (0xD6D4): three groups at x 08h/48h/88h, two at 20h/70h, one at
// 48h. Then reloads the cursor and marker tiles (draw_battle_cursor_tiles_alt, draw_battle_marker_sprite).
// DS = 0 and BX = the member except on the one-group path, which leaves both alone.
void position_enemy_target_cursor_core(Machine &m) {
    uint8_t groups = m.rd8(m.ss(), kEnemyGroupCount);
    if (groups == 3 || groups == 2) {
        m.ds() = kIram;
        m.bx() = m.rd8(m.ss(), kBattleMember);
        uint8_t slot = m.rd8(m.ss(), uint16_t(kTargetSlot + m.bx()));
        if (groups == 3) {
            if (slot == 0)
                set_enemy_target_cursor(m, 0x0828, 0x7C54);
            else if (slot == 1)
                set_enemy_target_cursor(m, 0x4828, 0x7C5C);
            else
                set_enemy_target_cursor(m, 0x8828, 0x7C64);
        } else if (slot == 0) {
            set_enemy_target_cursor(m, 0x2028, 0x7C54);
        } else {
            set_enemy_target_cursor(m, 0x7028, 0x7C5C);
        }
    } else {
        set_enemy_target_cursor(m, 0x4828, 0x7C54);
    }
    uint16_t ds = m.ds();
    draw_battle_cursor_tiles_core(m, uint16_t(kSpriteBuffer + 0x10)); // draw_battle_cursor_tiles_alt
    m.ds() = ds;
    draw_battle_marker_sprite_core(m);
}

void position_enemy_target_cursor(Machine &m) {
    position_enemy_target_cursor_core(m);
    m.ret();
}

namespace {
void out16(Machine &m, uint8_t port, uint16_t value) {
    m.out8(port, uint8_t(value));
    m.out8(uint8_t(port + 1), uint8_t(value >> 8));
}
} // namespace

// rom: 0x362B62 (near)
// A party member takes a hit: flashes their palette (palette 8 + kActionTarget, 9..11; any target other
// than 0-3 uses 11) with the white flash palette D_381828, plays sound 18h and shakes both screen layers
// (scroll ports 10h/12h: +3, -3, +4, -4 pixels on both axes, a frame each, four times), then restores the
// member's own palette (the kMemberPalette* tables) and the scroll. Every register is saved around it.
void shake_screen_for_party_hit_core(Machine &m) {
    const hooks::EventScope presentation(m, hooks::Event::BattleShake, {1}, {0});
    SavedRegisters saved(m);
    constexpr uint16_t kScroll1 = 0x0008, kScroll2 = 0x0400; // the battle screen's resting scroll values
    uint8_t target = m.rd8(m.ss(), kActionTarget);
    m.bx() = target <= 3 ? uint16_t(8 + target) : 0xB;
    m.ds() = 0x8000;
    m.ax() = m.bx();
    m.cx() = 1;
    m.si() = 0x1828; // D_381828
    m.di() = kPaletteBuffer;
    copy_palettes_core(m);
    m.ax() = 0x18;
    play_sound_effect_core(m, 0x18);
    m.cx() = 5;
    while (--m.cx() != 0) {
        auto shake = [&](uint16_t d1, uint16_t d2) {
            m.dx() = d1;
            m.ax() = d1;
            out16(m, 0x10, d1);
            m.dx() = d2;
            m.ax() = d2;
            out16(m, 0x12, d2);
        };
        shake(uint16_t(kScroll1 + 0x303), uint16_t(kScroll2 + 0x303));
        m.call_far(0x4000, 0x0009); // wait_vblank
        dma_palettes_core(m);
        shake(uint16_t(kScroll1 - 0x303), uint16_t(kScroll2 - 0x303));
        m.call_far(0x4000, 0x0009); // wait_vblank
        shake(uint16_t(kScroll1 + 0x404), uint16_t(kScroll2 + 0x404));
        m.call_far(0x4000, 0x0009); // wait_vblank
        shake(uint16_t(kScroll1 - 0x404), uint16_t(kScroll2 - 0x404));
        m.call_far(0x4000, 0x0009); // wait_vblank
        m.ax() = kScroll1;
        out16(m, 0x10, kScroll1);
        m.ax() = kScroll2;
        out16(m, 0x12, kScroll2);
    }
    target = m.rd8(m.ss(), kActionTarget);
    m.bx() = uint16_t(target * 2);
    m.si() = m.rd16(m.ss(), uint16_t(kMemberPaletteOffsets + m.bx()));
    m.ds() = m.rd16(m.ss(), uint16_t(kMemberPaletteSegments + m.bx()));
    m.si() = uint16_t(m.si() + m.rd8(m.ss(), uint16_t(kMemberPaletteVariants + target)) * 0x20);
    m.cx() = 1;
    m.ax() = m.rd16(m.ss(), uint16_t(kMemberPaletteSlots + m.bx()));
    m.di() = kPaletteBuffer;
    copy_palettes_core(m);
    dma_palettes_core(m);
    out16(m, 0x10, kScroll1);
    out16(m, 0x12, kScroll2);
    saved.restore(m);
}

void shake_screen_for_party_hit(Machine &m) {
    shake_screen_for_party_hit_core(m);
    m.ret();
}

// rom: 0x360C1C (near)
// Resolve the target's selected counteraction, then dispatch the acting item's effect handler.
void execute_battle_action_core(Machine &m) {
    const uint16_t ax = m.ax(), bx = m.bx(), cx = m.cx();
    uint8_t target = m.rd8(m.ss(), kActionTarget);
    m.set_al(target);
    if (target < 4) {
        uint8_t actor = m.rd8(m.ss(), uint16_t(m.di() + 1));
        m.set_al(actor);
        battle_actor_pointer_core(m);
        m.si() = uint16_t(m.si() + kActorSkills);
        uint16_t entry = kBattleOrder;
        uint8_t skill;
        for (;;) {
            skill = m.rd8(m.ss(), entry);
            uint8_t who = m.rd8(m.ss(), uint16_t(entry + 3));
            m.bx() = uint16_t(who << 8 | skill);
            if (who == actor)
                break;
            entry = uint16_t(entry + 4);
        }
        m.ax() = skill;
        m.bx() = 2;
        m.si() = advance_record_pointer_core(m);
        m.set_ah(m.rd8(m.ss(), m.si()));
        m.wr8(m.ss(), kActionEffect, m.ah());
    } else {
        m.si() = kBattleOrder;
        m.cx() = uint16_t((m.cx() & 0xFF00) | 0x1F);
        for (uint8_t left = 0x1F; left != 0; --left) {
            uint8_t who = m.rd8(m.ss(), uint16_t(m.si() + 3));
            m.set_ah(who);
            if (who == target)
                break;
            m.si() = uint16_t(m.si() + 4);
            m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(left - 1));
        }
        m.set_al(m.rd8(m.ss(), m.si()));
        m.wr8(m.ss(), kActionEffect, m.al());
    }
    // L_360C82
    m.ax() = m.rd8(m.ss(), kActionId);
    m.bx() = 2;
    lookup_table_byte_core(m);
    m.ax() = uint8_t(m.al() + m.al()); // add al,al; mov ah,0; add ax, D_360000 (= 0)
    m.si() = m.ax();
    m.ds() = 0x6000;
    m.si() = m.rd16(m.ds(), m.si());
    m.set_al(m.rd8(m.ss(), m.di()));
    if (m.al() == 0xFF) {
        announce_battle_action_once_core(m);
    } else {
        switch (m.si()) {
        case 0x69B1:
            announce_battle_action_once_core(m);
            break;
        case 0x69E6:
            try_cure_status_40_core(m);
            break;
        case 0x6A40:
            try_cure_status_80_core(m);
            break;
        default:
            dispatch_computed_handler_core(m, m.si());
            break; // Other action-table entries are unregistered ROM handlers.
        }
        m.wr8(m.ss(), kBattleFlashCounter, uint8_t(m.rd8(m.ss(), kBattleFlashCounter) + 1));
    }
    m.ax() = ax;
    m.bx() = bx;
    m.cx() = cx;
}

void execute_battle_action(Machine &m) {
    execute_battle_action_core(m);
    m.ret();
}

// rom: 0x362987 (near)
// Draws the four party-member boxes of the battle screen (rows 0Eh, columns 1/8/15h/1Ch, 7x4 tiles): a
// window (draw_window) for the members selected by AX -- 0-3 that member alone, FEh none, anything else
// all four -- and a plain border (draw_tile_box_border) for the rest, then the skill/status list
// (draw_battle_skill_list) with DL = that member mask. Every register is saved around it.
void draw_battle_member_boxes_core(Machine &m) {
    SavedRegisters saved(m);
    uint16_t selector = m.ax();
    uint8_t mask = selector <= 3 ? uint8_t(1 << selector) : selector == 0xFE ? 0 : 0xF;
    const uint16_t dx = uint16_t((m.dx() & 0xFF00) | mask);
    m.dx() = dx;
    uint16_t box = 0x0E01;
    for (uint16_t member = 0; member < 4; ++member) {
        m.ax() = 0;
        m.bx() = box;
        m.cx() = 0x0407;
        if (mask & (1 << member))
            draw_window_core(m);
        else
            draw_tile_box_border_core(m);
        m.dx() = dx;
        box = uint16_t((box & 0xFF00) | uint8_t(box + 7));
        m.bx() = box;
        m.cx() = uint16_t(member + 1);
    }
    draw_battle_skill_list_core(m);
    saved.restore(m);
}

void draw_battle_member_boxes(Machine &m) {
    draw_battle_member_boxes_core(m);
    m.ret();
}

// rom: 0x36C8FE
// Draws the portrait of enemy group AL (2, 1, anything else = 0) at screen position BX: unless DH = 1,
// first uploads its tiles (to tile 300h + 28h*group) and palettes (palette 4 + group, into the sprite (DL
// = 1, C400h) or background (C600h) half) from its 12-byte record at 0xD500 + 12*group (tiles, palette
// source, map, tile count, palette skip, map width/height, palette count; data in segment E000h), then
// draws its tile map with palette/tile base 0900h + 228h*group. A 6-wide map is drawn one column left,
// a 9-wide one two rows up and two columns left. DX is preserved; the rest is left as the last call left
// it (DS = E000h, AX = 1).
void draw_enemy_group_portrait_core(Machine &m) {
    uint8_t group = m.al() == 2 ? 2 : m.al() == 1 ? 1 : 0;
    const uint16_t rec = uint16_t(kFormationBackTileSource + group * 12);
    m.ax() = 0xE000;
    m.ds() = 0xE000;
    if (uint8_t(m.dx() >> 8) != 1) {
        m.si() = m.rd16(m.ss(), rec);
        m.cx() = m.rd16(m.ss(), uint16_t(rec + 6));
        m.ax() = uint16_t(0x300 + 0x28 * group);
        copy_tiles_core(m);
        m.si() = m.rd16(m.ss(), uint16_t(rec + 2));
        for (uint8_t skip = m.rd8(m.ss(), uint16_t(rec + 8)); skip != 0; --skip)
            m.si() = uint16_t(m.si() + 0x20);
        m.ax() = uint16_t(4 + group);
        m.cx() = m.rd8(m.ss(), uint16_t(rec + 0xB));
        m.di() = uint8_t(m.dx()) == 1 ? kPaletteBuffer : kPaletteTargetBuffer;
        copy_palettes_core(m);
    }
    m.ax() = m.rd16(m.ss(), uint16_t(rec + 4));
    uint8_t width = m.rd8(m.ss(), uint16_t(rec + 9));
    m.cx() = uint16_t(m.rd8(m.ss(), uint16_t(rec + 0xA)) << 8 | width);
    if (width == 6) {
        m.bx() = uint16_t((m.bx() & 0xFF00) | uint8_t(m.bx() - 1));
    } else if (width == 9) {
        m.bx() = uint16_t(uint16_t(uint8_t((m.bx() >> 8) - 2)) << 8 | uint8_t(m.bx() - 2));
    }
    m.si() = m.ax();
    const uint16_t dx = m.dx(), at = m.bx();
    m.ds() = 0xE000;
    m.ax() = 1;
    m.dx() = uint16_t(0x900 + 0x228 * group);
    draw_tilemap_offset_core(m);
    m.dx() = dx;
    if (Machine::enemy_portrait_hook) [[unlikely]]
        Machine::enemy_portrait_hook(m, group, at);
}

void draw_enemy_group_portrait(Machine &m) {
    draw_enemy_group_portrait_core(m);
    m.retf();
}

// rom: 0x36C877
// Draws the portraits of every non-empty enemy group (draw_enemy_group_portrait, AX in as its DX flags)
// at the layout for the group count (0xD6D4): three at columns 4/0Ch/14h, two at 7/11h, one at 0Ch (row
// 3). Leaves AL = the last group count tested; the rest as the last portrait left it.
void draw_enemy_group_portraits_core(Machine &m) {
    m.dx() = m.ax();
    static constexpr uint8_t kColumns[3][3] = {{0x0C}, {0x07, 0x11}, {0x04, 0x0C, 0x14}};
    uint8_t groups = m.rd8(m.ss(), kEnemyGroupCount);
    m.set_al(groups);
    uint8_t layout = groups == 3 ? 3 : groups == 2 ? 2 : 1;
    for (uint8_t g = 0; g < layout; ++g) {
        uint8_t count = m.rd8(m.ss(), uint16_t(kEnemyGroups + 1 + g * 2));
        m.set_al(count);
        if (count == 0)
            continue;
        m.ax() = g;
        m.bx() = uint16_t(0x300 | kColumns[layout - 1][g]);
        draw_enemy_group_portrait_core(m);
    }
}

void draw_enemy_group_portraits_call_core(Machine &m) { draw_enemy_group_portraits_core(m); }
void draw_enemy_group_portraits(Machine &m) {
    draw_enemy_group_portraits_call_core(m);
    m.retf();
}

namespace {
// Writes an enemy order entry: action, (target,) flags 1, actor CH.
void set_enemy_entry(Machine &m, uint8_t action, bool with_target) {
    const uint8_t ch = uint8_t(m.cx() >> 8);
    m.set_al(action);
    m.wr8(m.ss(), m.di(), action);
    if (with_target)
        m.wr8(m.ss(), uint16_t(m.di() + 1), ch);
    m.wr8(m.ss(), uint16_t(m.di() + 2), 1);
    m.wr8(m.ss(), uint16_t(m.di() + 3), ch);
}
} // namespace

// rom: 0x360DE5 (near)
// Choose usable enemy skills, spend finite uses, and select targets; honor sleep/paralysis and surprise.
void choose_enemy_actions_core(Machine &m) {
    m.cx() = uint16_t(0x0400 | m.rd8(m.ss(), kEnemyCount));
    m.set_al(4);
    battle_actor_pointer_core(m);
    m.dx() = m.si();
    m.di() = uint16_t(kBattleOrder + 0x10);
    while (uint8_t(m.cx()) != 0) {
        uint8_t mode = m.rd8(m.ss(), kBattleMode);
        m.set_al(mode);
        bool idle = mode == 2;
        if (!idle) {
            m.si() = uint16_t(m.dx() + kActorStatus);
            uint8_t status = m.rd8(m.ss(), m.si());
            m.set_al(status);
            if (status == 0xFF || (status & 3)) {
                idle = true;
            } else if (status & 0x80) {
                set_enemy_entry(m, 0xFD, false);
            } else if (status & 0x40) {
                set_enemy_entry(m, 0xFE, false);
            } else {
                m.set_al(m.rd8(m.ss(), kEnemyGroups));
                if (m.al() == 0xC7) {
                    choose_special_actor_action_core(m); // choose_special_actor_action: no core
                    return;
                }
                m.set_al(m.rd8(m.ss(), kBattleMode));
                if (m.al() == 2) {
                    idle = true;
                } else {
                    m.set_al(uint8_t(m.cx() >> 8));
                    count_action_order_alive_core(m);
                    if (m.al() == 0) {
                        set_enemy_entry(m, 0xFC, true);
                        m.set_al(0);
                    } else {
                        m.si() = uint16_t(m.dx() + kActorSkills);
                        for (;;) {
                            m.ax() = 0x0700;
                            random_range_core(m);
                            m.bx() = uint16_t(m.al() * 2);
                            uint16_t slot = uint16_t(m.bx() + m.si());
                            uint8_t uses = m.rd8(m.ss(), uint16_t(slot + 1));
                            m.set_al(uses);
                            if (uses == 0xFF || uses == 0)
                                continue;
                            uint16_t bx = m.bx();
                            m.set_al(m.rd8(m.ss(), slot));
                            m.bx() = 0;
                            lookup_table_byte_core(m);
                            m.bx() = bx;
                            if ((m.al() & 1) == 0)
                                continue;
                            uses = m.rd8(m.ss(), uint16_t(slot + 1));
                            m.set_al(uses);
                            if (uses != 0xFE) {
                                uses = uint8_t(uses - 1);
                                if (uses == 0)
                                    uses = 0xFF;
                                m.set_al(uses);
                                m.wr8(m.ss(), uint16_t(slot + 1), uses);
                            }
                            uint8_t skill = m.rd8(m.ss(), slot);
                            m.wr8(m.ss(), kAiDisposition, skill);
                            break;
                        }
                        // L_360EF2
                        uint8_t skill = m.rd8(m.ss(), kAiDisposition);
                        m.set_al(skill);
                        m.wr8(m.ss(), m.di(), skill);
                        m.wr8(m.ss(), uint16_t(m.di() + 2), 1);
                        m.wr8(m.ss(), uint16_t(m.di() + 3), uint8_t(m.cx() >> 8));
                        resolve_ai_action_fallback_core(m);
                    }
                }
            }
        }
        if (idle)
            set_enemy_entry(m, 0xFF, true);
        // L_360F05
        m.cx() = uint16_t(uint16_t(uint8_t((m.cx() >> 8) + 1)) << 8 | uint8_t(m.cx() - 1));
        m.si() = uint16_t(m.dx() + 0x20);
        m.dx() = m.si();
        m.di() = uint16_t(m.di() + 4);
    }
}

void choose_enemy_actions(Machine &m) {
    choose_enemy_actions_core(m);
    m.ret();
}

// rom: 0x3605CF (near)
// Execute ordered actions until battle ends, then apply poison and check for a defeated side.
void execute_battle_round_core(Machine &m) {
    m.di() = kBattleOrder;
    m.cx() = 0;
    for (uint8_t member = 0; member < 4; ++member) {
        m.set_al(member);
        battle_actor_pointer_core(m);
        uint8_t status = m.rd8(m.ss(), uint16_t(m.si() + kActorStatus));
        m.set_al(status);
        if (status != 0xFF)
            m.cx() = uint16_t(m.cx() + 0x100);
        m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(member + 1));
    }
    m.set_al(m.rd8(m.ss(), kEnemyCount));
    m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.cx() + m.al()));
    for (;;) {
        const uint16_t cx = m.cx(), di = m.di();
        clear_battle_ai_flags_core(m);
        execute_order_entry_core(m);
        m.di() = di;
        m.cx() = cx;
        m.set_al(m.rd8(m.ss(), kBattleEndState));
        if (m.al() != 0)
            return;
        m.di() = uint16_t(m.di() + 4);
        m.cx() = uint16_t((m.cx() & 0xFF00) | uint8_t(m.cx() - 1));
        if (uint8_t(m.cx()) == 0)
            break;
    }
    clear_or_flag_actor_records_core(m);
    mark_battle_end_flags_core(m);
}

void execute_battle_round(Machine &m) {
    execute_battle_round_core(m);
    m.ret();
}

// rom: 0x3604DA (near)
// Prepare enemy actions and party targets, shuffle/sort the order, then execute one round.
void run_battle_round_core(Machine &m) {
    hooks::emit(hooks::Event::BattleRoundStart, m);
    tally_enemy_type_counts_core(m);
    m.bx() = 0;
    m.si() = kBattleOrder;
    for (uint8_t member = 0; member < 4; ++member) {
        m.bx() = member;
        m.set_al(m.rd8(m.ss(), uint16_t(kMemberSkipFlags + member)));
        if (m.al() != 0)
            continue;
        uint16_t entry = uint16_t(m.si() + member * 4);
        m.set_ah(m.rd8(m.ss(), uint16_t(kEquipmentPage + member)));
        m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(kEquipmentRow + member)) + m.ah()));
        m.wr8(m.ss(), entry, m.al());
        m.set_al(m.rd8(m.ss(), uint16_t(kMemberTargets + member)));
        m.wr8(m.ss(), uint16_t(entry + 1), m.al());
        m.set_al(m.rd8(m.ss(), uint16_t(kMemberActionFlags + member)));
        m.wr8(m.ss(), uint16_t(entry + 2), m.al());
        m.wr8(m.ss(), uint16_t(entry + 3), member);
    }
    m.set_al(0);
    m.wr8(m.ss(), kBattleEndState, 0);
    choose_enemy_actions_core(m);
    battle_order_shuffle_core(m);
    execute_battle_round_core(m);
    tally_enemy_type_counts_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), kBattleEndState) | 1));
    m.wr8(m.ss(), kBattleEndState, m.al());
    m.cx() = 6;
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        wait_frames_core(m);
        m.cs() = caller_cs;
    } // wait_frames
    m.wr8(m.ss(), kBattleMode, 0);
    hooks::emit(hooks::Event::BattleRoundEnd, m);
}

void run_battle_round(Machine &m) {
    run_battle_round_core(m);
    m.ret();
}

namespace {
// FLAGS after `inc r/m8` giving r (CF untouched).
void set_inc_flags8(Machine &m, uint8_t result) {
    uint16_t f = uint16_t(m.flags() & ~(emu::ZF | emu::SF | emu::PF | emu::AF | emu::OF));
    if (result == 0)
        f |= emu::ZF;
    if (result & 0x80)
        f |= emu::SF;
    if (parity8(result))
        f |= emu::PF;
    if ((result & 0xF) == 0)
        f |= emu::AF;
    if (result == 0x80)
        f |= emu::OF;
    m.flags() = f;
}
} // namespace

// rom: 0x364DCE (near)
// Clamp nonpositive target HP to zero, mark death, and count/report the knockout (CF set).
void check_target_knocked_out_core(Machine &m) {
    const uint16_t si = m.si();
    m.set_al(m.rd8(m.ss(), kActionTarget));
    battle_actor_pointer_core(m);
    m.ax() = m.rd16(m.ss(), uint16_t(m.si() + kActorHp));
    if ((m.ax() & 0x8000) == 0 && m.ax() != 0) {
        set_logic_flags8(m, m.al());
        m.si() = si;
        return;
    }
    m.ax() = 0;
    BattleActorState(m, m.si()).write_hp(0);
    BattleActorState(m, m.si()).write_status(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + kActorStatus)) | 1));
    m.set_al(m.rd8(m.ss(), kActionTarget));
    if (m.al() >= 4) {
        remove_defeated_enemy_group_core(m);
        m.set_al(m.rd8(m.ss(), kBattleKnockoutMode));
        if (m.al() & 1)
            show_target_knocked_out_message_core(m);
    } else {
        m.set_al(m.rd8(m.ss(), kActingActor));
        if (m.al() >= 4)
            show_target_died_message_core(m);
        else
            show_target_description_message_core(m);
        draw_battle_member_panels_core(m);
        SavedRegisters saved(m);
        m.dx() = uint16_t((m.dx() & 0xFF00) | 0xF);
        draw_battle_skill_list_core(m);
        saved.restore(m);
    }
    uint8_t count = uint8_t(m.rd8(m.ss(), kBattleStatusSuccessCount) + 1);
    m.wr8(m.ss(), kBattleStatusSuccessCount, count);
    set_inc_flags8(m, count);
    m.flags() |= emu::CF; // clc; cmc
    m.si() = si;
}

void check_target_knocked_out(Machine &m) {
    check_target_knocked_out_core(m);
    m.ret();
}

namespace {
void queue_words(Machine &m, uint16_t ds, uint16_t si, uint16_t bx) {
    m.ds() = ds;
    m.si() = si;
    m.ax() = 0xFF;
    m.bx() = bx;
    append_word_queue_core(m);
}

void queue_words(Machine &m, TextKey key, uint16_t bx) {
    const TextPtr t = text(m, key);
    m.ds() = t.segment;
    m.si() = t.offset;
    m.ax() = 0xFF;
    m.bx() = bx;
    append_word_queue_core(m);
    text_done(m, key);
}

void open_battle_popup(Machine &m, uint16_t timing_words = 0) {
    m.ax() = 0;
    m.es() = 0;
    m.si() = kIconQueue; // icon_queue
    {
        const uint16_t caller_cs = m.cs();
        m.cs() = 0x4000;
        open_status_popup_core(m, timing_words);
        m.cs() = caller_cs;
    } // open_status_popup
}
} // namespace

// rom: 0x369E80 (near)
// Announces an action in the message popup: builds the actor's name/icons and the member display, the
// action's icon list, then queues the complete announcement. Footer mode chooses a single target,
// a group with its count, or all enemies. Presentation joins the original's two popup pages.
// Every register is saved around it.
void announce_battle_action_core(Machine &m) {
    JoinedBattlePopup joined(m, "announce_battle_action");
    hooks::emit(hooks::Event::BattleAction, m);
    SavedRegisters saved(m);
    build_battle_member_name_or_icons_core(m);
    build_battle_member_display_core(m);
    m.ax() = uint16_t(m.rd8(m.ss(), kActionId) << 8 | m.rd8(m.ss(), kActingActor));
    m.dx() = uint16_t((m.dx() & 0xFF00) | 1);
    build_icon_list_core(m);
    queue_words(m, 0, kBattleActorIcons, 0);
    queue_words(m, TextKey::announce_battle_action_1, 0xFF); // D_383068
    queue_words(m, 0, kBattleItemIcons, 0xFF);
    queue_words(m, TextKey::announce_battle_action_2, 0xFF); // D_3830C6
    open_battle_popup(m);
    m.set_al(m.rd8(m.ss(), kFooterPanelMode));
    if (m.al() == 0) {
        queue_words(m, 0, kBattleTargetIcons, 0);
    } else if (m.al() == 1) {
        queue_words(m, 0, kBattleTargetIcons, 0);
        m.dx() = m.rd8(m.ss(), uint16_t(kEnemyGroups + 1 + m.rd8(m.ss(), kTargetGroup) * 2));
        m.bx() = uint16_t(m.rd8(m.ss(), kTargetGroup) * 2);
        queue_words(m, TextKey::announce_battle_action_3, 0xFF); // D_3836FE
        m.ds() = 0;
        m.ax() = m.dx();
        format_number_core(m);
        queue_words(m, 0, kBattleNumberText, 0xFF);
        queue_words(m, TextKey::announce_battle_action_4, 0xFF); // D_383672
    } else {
        queue_words(m, TextKey::announce_battle_action_5, 0); // D_383278
    }
    queue_words(m, TextKey::announce_battle_action_6, 0xFF); // D_3830CC
    open_battle_popup(m);
    saved.restore(m);
}

void announce_battle_action_call_core(Machine &m) { announce_battle_action_core(m); }
void announce_battle_action(Machine &m) {
    announce_battle_action_call_core(m);
    m.ret();
}

// rom: 0x366E36 (near)
// Roll agility-based evasion, then apply weapon/strength damage minus defense and follow-up effects.
void resolve_physical_attack_core(Machine &m) {
    auto flag_bit0 = [&]() { // has_table_flag_bit1: CF=1 && ZF=0 <=> the id isn't FFh and byte 1 bit 0
        has_table_flag_bit1_core(m);
        return (m.flags() & emu::CF) && !(m.flags() & emu::ZF);
    };
    m.set_al(m.rd8(m.ss(), kActionTarget));
    battle_actor_stat_byte9_halved_core(m);
    m.wr8(m.ss(), kTargetEvade, m.al());
    m.set_al(m.rd8(m.ss(), kActingActor));
    battle_actor_stat_byte9_halved_core(m);
    m.wr8(m.ss(), kAttackerEvade, m.al());
    // Evade chance = max(target evade + flagged-action byte - attacker evade, 0), with byte wrapping.
    const uint8_t attacker_evade = m.al();
    m.cx() = uint16_t((m.cx() & 0xFF00) | attacker_evade);
    if (flag_bit0())
        m.wr8(m.ss(), kTargetEvade, uint8_t(m.rd8(m.ss(), kTargetEvade) + m.al()));
    uint8_t target_evade = m.rd8(m.ss(), kTargetEvade);
    uint8_t chance = target_evade < attacker_evade ? 0 : uint8_t(target_evade - attacker_evade);
    m.set_al(chance);
    m.wr8(m.ss(), kEvadeChance, chance);
    m.cx() = uint16_t((m.cx() & 0xFF00) | chance);
    m.ax() = 0x6400;
    random_range_core(m);
    if (m.al() < chance) {
        show_attack_missed_message_core(m);
        return;
    }
    battle_actor_stat_byte8_halved_core(m);
    m.wr8(m.ss(), kBattleSavedAttackValue, m.al());
    battle_actor_stat_byte7_halved_core(m);
    m.wr8(m.ss(), kBattleItemPower, m.al());
    uint8_t dl = uint8_t(m.al() >> 2);
    const uint16_t bx = m.bx();
    m.set_al(m.rd8(m.ss(), kActionId));
    m.bx() = 4;
    lookup_table_byte_core(m);
    m.bx() = bx;
    // Weapon power x (effective strength / 4); random bonus uses only the product's low byte.
    uint16_t product = uint16_t(m.al() * dl); // xor dh,dh; xor ah,ah; mul dl
    m.dx() = product;
    m.ax() = uint16_t(uint8_t(uint8_t(product) >> 1) << 8);
    random_range_core(m);
    m.ax() = m.al();
    uint16_t damage = uint16_t(product + m.ax());
    // Add strength and subtract defense; flagged actions and target resistance each halve signed damage.
    m.ax() = m.rd8(m.ss(), kBattleItemPower);
    damage = uint16_t(damage + m.ax());
    m.ax() = m.rd8(m.ss(), kBattleSavedAttackValue);
    damage = uint16_t(damage - m.ax());
    m.dx() = damage;
    if (flag_bit0())
        m.dx() = uint16_t(int16_t(m.dx()) >> 1);
    battle_actor_stat_byte_b_core(m);
    if (m.al() & 0x40)
        m.dx() = uint16_t(int16_t(m.dx()) >> 1);
    if ((m.dx() & 0x8000) || m.dx() == 0) {
        show_no_damage_message_core(m);
        m.set_al(m.rd8(m.ss(), kActionEffect));
        if (m.al() != 0xFF)
            resolve_counterattack_core(m); // resolve_counterattack: no core
        return;
    }
    set_battle_meter_target_core(m);
    open_party_member_popup_core(m);
    check_target_knocked_out_core(m);
    if (m.flags() & emu::CF)
        return;
    has_table_flag_bit1_core(m);
    if (!(m.flags() & emu::CF))
        return;
    if (m.al() & 2)
        resolve_drain_hit_core(m);
    else if (m.al() & 4)
        resolve_status_hit_core(m);
    else if (m.al() & 0x10)
        resolve_fixed_damage_hit_core(m);
}

void resolve_physical_attack_call_core(Machine &m) { resolve_physical_attack_core(m); }
void resolve_physical_attack(Machine &m) {
    resolve_physical_attack_call_core(m);
    m.ret();
}

namespace {
void set_cl(Machine &m, uint8_t value) { m.cx() = uint16_t((m.cx() & 0xFF00) | value); }
void set_ch(Machine &m, uint8_t value) { m.cx() = uint16_t((m.cx() & 0x00FF) | value << 8); }
void set_dl(Machine &m, uint8_t value) { m.dx() = uint16_t((m.dx() & 0xFF00) | value); }
void set_bl(Machine &m, uint8_t value) { m.bx() = uint16_t((m.bx() & 0xFF00) | value); }
void inc8_mem(Machine &m, uint16_t off) { m.wr8(m.ss(), off, uint8_t(m.rd8(m.ss(), off) + 1)); }

// The enemy-attack flourish of execute_order_entry: an enemy (AL) that can act flashes its formation
// palette twice (copy_formation_palette / _next, 5 frames each) unless kBattleMode = 2. Registers saved.
void flash_attacking_enemy(Machine &m) {
    SavedRegisters saved(m);
    battle_actor_pointer_core(m);
    m.set_al(m.rd8(m.ss(), uint16_t(m.si() + kActorStatus)));
    if ((m.al() & 3) == 0) {
        m.set_al(m.rd8(m.ss(), kBattleMode));
        if (m.al() != 2) {
            m.set_al(m.rd8(m.ss(), uint16_t(m.si() + kActorGroupId)));
            party_slot_for_name_id_core(m);
            m.wr8(m.ss(), kFormationKind, m.al());
            for (int flash = 0; flash < 2; ++flash) {
                copy_formation_palette_core(m);
                dma_palettes_core(m);
                m.cx() = 5;
                {
                    const uint16_t caller_cs = m.cs();
                    m.cs() = 0x4000;
                    wait_frames_core(m);
                    m.cs() = caller_cs;
                } // wait_frames
                copy_formation_palette_next_core(m);
                dma_palettes_core(m);
                if (flash == 0) {
                    m.cx() = 5;
                    {
                        const uint16_t caller_cs = m.cs();
                        m.cs() = 0x4000;
                        wait_frames_core(m);
                        m.cs() = caller_cs;
                    } // wait_frames
                }
            }
        }
    }
    saved.restore(m);
}

// L_3606DF: Clear confusion and select the recovered actor's first usable equipment slot.
void recover_from_confusion(Machine &m) {
    show_actor_recovered_message_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), m.si()) & 0xDF));
    m.wr8(m.ss(), m.si(), m.al());
    {
        SavedRegisters saved(m);
        set_dl(m, 0xF);
        draw_battle_skill_list_core(m);
        saved.restore(m);
    }
    draw_battle_member_panels_core(m);
    m.bx() = m.rd8(m.ss(), kActingActor);
    SavedRegisters saved(m);
    m.cx() = 0;
    for (;;) {
        m.wr8(m.ss(), uint16_t(kEquipmentRow + m.bx()), uint8_t(m.cx()));
        m.wr8(m.ss(), uint16_t(kEquipmentPage + m.bx()), uint8_t(m.cx() >> 8));
        m.ax() = m.bx();
        m.ax() = selected_battle_equipment_core(m);
        if (m.ah() != 0 && m.ah() != 0xFF) {
            const uint16_t bx = m.bx();
            m.set_ah(0);
            m.bx() = 0;
            lookup_table_byte_core(m);
            m.bx() = bx;
            if (m.al() & 1)
                break;
        }
        if (uint8_t(m.cx()) == 2)
            set_ch(m, uint8_t((m.cx() >> 8) + 1));
        else
            set_cl(m, uint8_t(m.cx() + 1));
        if ((m.cx() >> 8) == 6)
            break;
    }
    saved.restore(m);
}

// L_360817: Apply the action to each active enemy of the selected type and tally its knockouts.
void act_on_target_group(Machine &m) {
    clear_battle_ai_flags_core(m);
    m.wr8(m.ss(), kFooterPanelMode, 1);
    m.wr8(m.ss(), kBattleKnockoutMode, 0);
    set_cl(m, 4);
    m.wr8(m.ss(), kItemUseCount, 0);
    m.wr8(m.ss(), kActionGroupType, uint8_t(m.dx()));
    bool actor_down = false;
    for (;;) {
        m.set_al(m.rd8(m.ss(), kActingActor));
        battle_actor_pointer_core(m);
        m.set_al(m.rd8(m.ss(), uint16_t(m.si() + kActorStatus)));
        if (m.al() & 3) {
            actor_down = true;
            break;
        }
        m.set_al(uint8_t(m.cx()));
        battle_actor_pointer_core(m);
        m.set_al(m.rd8(m.ss(), uint16_t(m.si() + kActorStatus)));
        if ((m.al() & 3) == 0) {
            uint8_t group_id = m.rd8(m.ss(), uint16_t(kEnemyGroups + m.rd8(m.ss(), kTargetGroup) * 2));
            set_dl(m, group_id);
            m.set_al(m.rd8(m.ss(), uint16_t(m.si() + kActorGroupId)));
            if (group_id == m.al()) {
                m.wr8(m.ss(), kActionTarget, uint8_t(m.cx()));
                execute_battle_action_core(m);
                inc8_mem(m, kBattleFlashCounter);
                m.set_al(uint8_t(m.rd8(m.ss(), kItemUseCount) + 1));
                m.wr8(m.ss(), kItemUseCount, m.al());
                mark_battle_end_flags_core(m);
                if (m.rd8(m.ss(), kBattleEndState) != 0)
                    break;
            }
        }
        set_cl(m, uint8_t(m.cx() + 1));
        if (uint8_t(m.cx()) == 0x1F)
            break;
    }
    if (actor_down) { // L_360A85
        mark_battle_end_flags_core(m);
        return;
    }
    // L_360893
    m.set_al(m.rd8(m.ss(), kBattleStatusSuccessCount));
    if (m.al() != 0) {
        m.bx() = uint16_t(m.rd8(m.ss(), kTargetGroup) * 2);
        m.set_ah(m.rd8(m.ss(), uint16_t(kEnemyGroups + 1 + m.bx())));
        if (m.ah() != 0) {
            m.set_ah(0);
            m.wr16(m.ss(), kMeterInputTarget, m.ax());
            show_damage_message_core(m);
        } else {
            m.set_ah(m.rd8(m.ss(), uint16_t(kEnemyCount + m.bx())));
            if (m.ah() == 1)
                show_target_knocked_out_message_core(m);
            else
                show_target_group_defeated_message_core(m);
        }
    }
    clear_battle_ai_flags_core(m);
}

// L_3608D1: Apply the action to active enemies across all groups and report each group's knockouts.
void act_on_all_enemies(Machine &m) {
    m.wr8(m.ss(), kFooterPanelMode, 2);
    clear_battle_ai_flags_core(m);
    m.wr8(m.ss(), kBattleKnockoutMode, 0);
    m.ax() = 0;
    set_cl(m, 4);
    m.wr8(m.ss(), kItemUseCount, 0);
    m.set_al(4);
    battle_actor_pointer_core(m);
    m.set_al(m.rd8(m.ss(), uint16_t(m.si() + kActorStatus)));
    bool first_dead = m.al() & 3;
    if (!first_dead) {
        m.si() = uint16_t(m.si() + kActorGroupId);
        m.set_al(m.rd8(m.ss(), m.si()));
        m.wr8(m.ss(), kItemUseCount, 0);
        m.wr8(m.ss(), kActionGroupType, m.al());
        m.wr8(m.ss(), kTargetGroup, 0);
    }
    for (bool skip = first_dead;;) {
        if (!skip) {
            // L_360910
            m.set_al(m.rd8(m.ss(), kActingActor));
            battle_actor_pointer_core(m);
            m.set_al(m.rd8(m.ss(), uint16_t(m.si() + kActorStatus)));
            if (m.al() & 3) { // L_360A85
                mark_battle_end_flags_core(m);
                return;
            }
            m.set_al(uint8_t(m.cx()));
            battle_actor_pointer_core(m);
            m.set_al(m.rd8(m.ss(), uint16_t(m.si() + kActorGroupId)));
            m.set_ah(m.rd8(m.ss(), kActionGroupType));
            bool hit = true;
            if (m.ah() == m.al()) {
                m.set_al(m.rd8(m.ss(), uint16_t(m.si() + kActorStatus)));
                hit = (m.al() & 3) == 0;
            } else {
                // L_360940: a new group
                m.wr8(m.ss(), kItemUseCount, 0);
                m.wr8(m.ss(), kActionGroupType, m.al());
                m.set_ah(0);
                set_dl(m, m.rd8(m.ss(), kBattleGroupEliminationScratch));
                if (uint8_t(m.dx()) == 0) {
                    const uint16_t ax = m.ax();
                    m.set_al(m.rd8(m.ss(), kBattleStatusSuccessCount));
                    m.wr8(m.ss(), kBattleStatusSuccessCount, 0);
                    if (m.al() != 0) {
                        m.set_ah(m.rd8(m.ss(), kEnemyCount));
                        if (m.ah() == 1) {
                            show_target_knocked_out_message_core(m);
                        } else {
                            m.set_ah(0);
                            m.wr16(m.ss(), kMeterInputTarget, m.ax());
                            m.set_al(m.rd8(m.ss(), kActionId));
                            if (m.al() != 0xEE)
                                show_damage_message_core(m);
                        }
                    }
                    m.ax() = ax;
                    m.set_al(m.rd8(m.ss(), uint16_t(m.si() + kActorStatus)));
                    hit = (m.al() & 3) == 0;
                }
            }
            if (hit) {
                // L_36098E
                m.set_al(m.rd8(m.ss(), uint16_t(m.si() + kActorGroupId)));
                party_slot_for_name_id_core(m);
                m.wr8(m.ss(), kTargetGroup, m.al());
                m.wr8(m.ss(), kActionTarget, uint8_t(m.cx()));
                execute_battle_action_core(m);
                inc8_mem(m, kBattleFlashCounter);
                m.set_al(uint8_t(m.rd8(m.ss(), kItemUseCount) + 1));
                m.wr8(m.ss(), kItemUseCount, m.al());
            }
        }
        skip = false;
        // L_3609B2
        set_cl(m, uint8_t(m.cx() + 1));
        if (uint8_t(m.cx()) == 0x1F)
            break;
    }
    // L_3609BC
    m.set_al(m.rd8(m.ss(), kBattleGroupEliminationScratch));
    if (m.al() != 0) {
        show_enemies_eliminated_message_core(m);
    } else {
        m.set_al(m.rd8(m.ss(), kBattleStatusSuccessCount));
        bool report_empty = true;
        if (m.al() != 0) {
            m.set_ah(m.rd8(m.ss(), kEnemyCount));
            if (m.ah() == 1) {
                show_target_knocked_out_message_core(m);
                report_empty = false;
            } else {
                m.set_ah(0);
                m.wr16(m.ss(), kMeterInputTarget, m.ax());
                m.set_al(m.rd8(m.ss(), kActionId));
                if (m.al() != 0xEE) {
                    show_damage_message_core(m);
                    m.wr8(m.ss(), kBattleStatusSuccessCount, 0);
                }
            }
        }
        if (report_empty) {
            // L_3609F2
            m.set_ah(uint8_t(m.rd8(m.ss(), kEnemyGroups + 1) + m.rd8(m.ss(), kEnemyGroups + 3) +
                             m.rd8(m.ss(), kEnemyGroups + 5)));
            m.set_al(m.rd8(m.ss(), kEnemyGroups + 5));
            if (m.ah() == 0) {
                m.set_ah(m.rd8(m.ss(), uint16_t(kEnemyCount + m.bx())));
                if (m.ah() != 1)
                    show_enemy_defeat_count_message_core(m);
            }
        }
    }
    // L_360A1A
    mark_battle_end_flags_core(m);
    clear_battle_ai_flags_core(m);
}

// L_360A22: Apply the action to each active party member until its actor falls or battle ends.
void act_on_whole_party(Machine &m) {
    clear_battle_ai_flags_core(m);
    m.cx() = 0;
    m.wr8(m.ss(), kBattleKnockoutMode, 1);
    m.wr8(m.ss(), kFooterPanelMode, 0);
    do {
        m.set_al(m.rd8(m.ss(), kActingActor));
        battle_actor_pointer_core(m);
        m.set_al(m.rd8(m.ss(), uint16_t(m.si() + kActorStatus)));
        if (m.al() & 3) { // L_360A85
            mark_battle_end_flags_core(m);
            return;
        }
        m.wr8(m.ss(), kActionTarget, uint8_t(m.cx()));
        const uint16_t cx = m.cx();
        m.set_al(uint8_t(m.cx()));
        battle_actor_pointer_core(m);
        m.set_al(m.rd8(m.ss(), uint16_t(m.si() + kActorStatus)));
        m.cx() = cx;
        if ((m.al() & 3) == 0) {
            m.wr8(m.ss(), kActionTarget, uint8_t(cx));
            m.wr8(m.ss(), uint16_t(m.di() + 1), uint8_t(cx));
            execute_battle_action_core(m);
            m.cx() = cx;
            inc8_mem(m, kBattleFlashCounter);
            mark_battle_end_flags_core(m);
            m.cx() = cx;
            if (m.rd8(m.ss(), kBattleEndState) != 0)
                return;
        }
        set_cl(m, uint8_t(m.cx() + 1));
    } while (uint8_t(m.cx()) != 4);
    m.ax() = 0;
    m.wr8(m.ss(), kBattleFlashCounter, 0);
}
} // namespace

// rom: 0x360615 (near)
// Execute one actor's order entry, handling disabling statuses, item uses, and target scope.
void execute_order_entry_core(Machine &m) {
    SavedRegisters saved(m);
    m.ax() = m.di();
    m.wr16(m.ss(), kActingEntry, m.ax());
    m.set_al(m.rd8(m.ss(), uint16_t(m.di() + 3)));
    m.wr8(m.ss(), kActingActor, m.al());
    if (m.al() >= 4)
        flash_attacking_enemy(m);
    // L_360693
    m.set_al(m.rd8(m.ss(), kActingActor));
    battle_actor_pointer_core(m);
    m.si() = uint16_t(m.si() + kActorStatus);
    m.set_al(m.rd8(m.ss(), m.si()));
    auto announce_only = [&]() { // L_3607AB
        announce_battle_action_once_core(m);
        clear_battle_ai_flags_core(m);
    };
    enum class Scope { Party, Group, All, Single, Done } scope = Scope::Done;
    if (m.al() == 0xFF || (m.al() & 3)) {
        // nothing
    } else if (m.al() & 0x80) {
        try_cure_status_80_core(m);
        clear_battle_ai_flags_core(m);
    } else if (m.al() & 0x40) {
        try_cure_status_40_core(m);
        clear_battle_ai_flags_core(m);
    } else {
        bool acts = true;
        if (m.al() & 0x20) {
            m.ax() = 0x6400;
            random_range_core(m);
            if (m.al() >= 10) {
                show_actor_confused_message_core(m);
                m.set_al(m.rd8(m.ss(), kActingActor));
            } else {
                recover_from_confusion(m);
                acts = false;
            }
        }
        if (acts) {
            // L_36074F
            m.set_al(m.rd8(m.ss(), kActingActor));
            bool have_action = true;
            if (m.al() < 4) {
                m.set_al(m.rd8(m.ss(), m.di()));
                if (m.al() == 0xFF) {
                    have_action = false;
                } else if (m.al() >= 0xFD) {
                    scope = Scope::Group;
                } else {
                    resolve_action_target_core(m); // saves every register: AL is still the old [DI]
                    if (!(m.flags() & emu::CF))
                        m.set_al(m.rd8(m.ss(), m.di()));
                    if ((m.flags() & emu::CF) || m.al() == 0xFF) {
                        have_action = false;
                    } else {
                        m.set_al(m.rd8(m.ss(), uint16_t(m.di() + 3)));
                        battle_actor_pointer_core(m);
                        m.si() = uint16_t(m.si() + kActorSkills);
                        m.ax() = m.rd8(m.ss(), m.di());
                        m.bx() = 2;
                        m.si() = advance_record_pointer_core(m);
                        m.set_ah(m.rd8(m.ss(), m.si()));
                        m.wr8(m.ss(), kActionId, m.ah());
                        m.set_al(m.rd8(m.ss(), uint16_t(m.si() + 1)));
                        if (m.al() != 0xFE &&
                            hooks::should(hooks::VB::SpendUse, m, true,
                                          {0, m.rd8(m.ss(), uint16_t(m.di() + 3)), m.ah()})) {
                            m.set_al(uint8_t(m.al() - 1));
                            if (m.al() == 0 && !(m.ah() & 0x80))
                                m.set_al(0xFF);
                        }
                        m.wr8(m.ss(), uint16_t(m.si() + 1), m.al());
                    }
                }
            } else {
                // L_3607B4
                m.set_al(m.rd8(m.ss(), m.di()));
                m.wr8(m.ss(), kActionId, m.al());
                m.set_al(m.rd8(m.ss(), uint16_t(m.di() + 1)));
                m.wr8(m.ss(), kActionTarget, m.al());
                m.set_al(m.rd8(m.ss(), uint16_t(m.di() + 3)));
                m.wr8(m.ss(), kActingActor, m.al());
            }
            if (!have_action) {
                announce_only();
            } else if (scope != Scope::Group) {
                // L_3607CB
                m.set_al(m.rd8(m.ss(), uint16_t(m.di() + 2)));
                if (m.al() & (2 | 4))
                    scope = Scope::Party;
                else if (m.al() & 0x20)
                    scope = Scope::Group;
                else if (m.al() & 0x40)
                    scope = Scope::All;
                else
                    scope = Scope::Single;
            }
        }
    }
    const int audio_item =
        m.rd8(m.ss(), kActingActor) < 4 && m.rd8(m.ss(), m.di()) >= 0xFD ? -1 : m.rd8(m.ss(), kActionId);
    hooks::EventScope audio_action(m, hooks::Event::SoundContext,
                                   {int(hooks::AudioContext::BattleAction), audio_item});
    switch (scope) {
    case Scope::Single:
        m.set_al(0);
        m.wr8(m.ss(), kFooterPanelMode, 0);
        m.wr8(m.ss(), kBattleFlashCounter, 0);
        m.set_al(1);
        m.wr8(m.ss(), kBattleKnockoutMode, 1);
        m.set_al(m.rd8(m.ss(), kActionTarget));
        m.set_ah(m.al());
        battle_actor_pointer_core(m);
        m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + kActorStatus)) & 3));
        if (m.al() != 0) {
            announce_only();
        } else {
            m.set_al(m.ah());
            execute_battle_action_core(m);
            clear_battle_ai_flags_core(m);
            mark_battle_end_flags_core(m);
        }
        break;
    case Scope::Group:
        act_on_target_group(m);
        break;
    case Scope::All:
        act_on_all_enemies(m);
        break;
    case Scope::Party:
        act_on_whole_party(m);
        break;
    case Scope::Done:
        break;
    }
    saved.restore(m);
}

void execute_order_entry(Machine &m) {
    execute_order_entry_core(m);
    m.ret();
}

namespace {
constexpr uint16_t kKeyA = 4;

void read_keypad_callout(Machine &m) { m.call_far(0x4000, 0x005C); } // read_keypad: no core
void wait_vblank_callout(Machine &m) { m.call_far(0x4000, 0x0009); }

// L_360321: Persist battle status, HP, item uses, and saved stats to the four party records.
void store_party_battle_records(Machine &m) {
    m.cx() = 0;
    m.si() = kBattleActors;
    m.di() = kGameState;
    do {
        m.set_al(m.rd8(m.ss(), uint16_t(m.si() + kActorStatus)));
        if (m.al() != 0xFF)
            m.set_al(uint8_t(m.al() & 0xF));
        m.wr8(m.ss(), m.di(), m.al());
        m.ax() = m.rd16(m.ss(), uint16_t(m.si() + kActorHp));
        m.wr16(m.ss(), uint16_t(m.di() + 6), m.ax());
        for (uint16_t k = 0; k < 4; ++k) {
            m.set_al(m.rd8(m.ss(), uint16_t(kBattleSavedStrength + k * 4 + uint8_t(m.cx()))));
            m.wr8(m.ss(), uint16_t(m.di() + 0xB + k), m.al());
        }
        m.bx() = 0;
        for (uint16_t k = 0; k < 8; ++k) {
            m.set_al(m.rd8(m.ss(), uint16_t(m.si() + kActorSkills + 2 * k)));
            m.wr8(m.ss(), uint16_t(m.di() + 0xF + 2 * k), m.al());
            m.set_al(m.rd8(m.ss(), uint16_t(m.si() + kActorSkills + 2 * k + 1)));
            m.wr8(m.ss(), uint16_t(m.di() + 0x10 + 2 * k), m.al());
            m.bx() = uint16_t(k + 1);
        }
        m.si() = uint16_t(m.si() + 0x20);
        m.di() = uint16_t(m.di() + 0x1F);
        m.cx() = uint16_t(m.cx() + 1);
    } while (m.cx() != 4);
}

// L_3603D3: Offer enemy meat after the drop roll, apply mutant growth, award gold, and reorder the party.
void battle_victory_rewards(Machine &m) {
    hooks::emit(hooks::Event::BattleRewards, m);
    bool fled = false;
    for (;;) {
        m.set_al(m.rd8(m.ss(), kBattleFlags));
        if (m.al() & 4) {
            fled = true;
            break;
        }
        read_keypad_callout(m); // no frame wait in this loop
        m.ax() = uint16_t(m.rd16(m.ss(), kKeysRepeat) & kKeyA);
        if (m.ax() != 0)
            break;
    }
    if (!fled) {
        m.bx() = 0;
        m.ax() = 0x93;
        play_sound_effect_core(m, 0x93);
        m.bx() = 0;
        for (;;) {
            // L_3603FC
            m.set_al(m.rd8(m.ss(), uint16_t(kEnemyGroups + m.bx())));
            const uint16_t bx = m.bx();
            m.es() = 0x6000;
            m.si() = 0xD1FB; // D_36D1FB: enemy record pointers
            m.ax() = table_at16(m, Table::monsters, m.es(), uint16_t(m.si() + m.al() * 2));
            m.si() = m.ax();
            m.bx() = bx;
            set_dl(m, uint8_t(table_at(m, Table::monsters, m.es(), m.si()) & 7));
            bool drop = false;
            if (uint8_t(m.dx()) != 0) {
                m.set_al(m.rd8(m.ss(), kDebugForceMeatDrop));
                if (m.al() == 1) {
                    drop = true;
                } else {
                    // Drop if a 0..70 roll is below 10 x the type's low-three-bit drop rate.
                    m.ax() = uint16_t(uint8_t(m.dx()) * 10); // mov al,dl; mov ah,0Ah; mul ah
                    set_dl(m, m.al());
                    m.ax() = 0x4600;
                    random_range_core(m);
                    drop = m.al() < uint8_t(m.dx());
                }
            }
            if (drop) {
                // L_36044D
                m.set_al(0);
                m.wr8(m.ss(), kPopupState, 0);
                m.set_al(m.rd8(m.ss(), uint16_t(kEnemyGroups + m.bx())));
                m.wr8(m.ss(), kMeatSourceType, m.al());
                run_transformation_menu_core(m); // not decompiled
                break;
            }
            set_bl(m, uint8_t(m.bx() + 2));
            m.set_al(uint8_t(m.rd8(m.ss(), kEnemyGroupCount) << 1));
            if (uint8_t(m.bx()) >= m.al())
                break;
        }
        // L_36045F
        m.wr8(m.ss(), kBattleMenuState, 8);
        battle_menu_dispatcher_core(m);
        m.set_al(0);
        m.wr8(m.ss(), kPopupState, 0);
        m.wr8(m.ss(), kGrowthHappened, 0);
        grow_mutants_after_battle_core(m);
        m.set_al(m.rd8(m.ss(), kGrowthHappened));
        if (m.al() != 0) {
            do {
                wait_vblank_callout(m);
                read_keypad_callout(m);
                m.ax() = uint16_t(m.rd16(m.ss(), kKeysRepeat) & kKeyA);
            } while (m.ax() == 0);
        }
        m.ax() = 0x93;
        play_sound_effect_core(m, 0x93);
        award_battle_gold_core(m);
    }
    sort_party_after_battle_core(m);
}
} // namespace

// rom: 0x360186 (near)
// Process battle commands and escape attempts; a failed escape forfeits the party's actions.
bool battle_input_step_core(Machine &m) {
    read_keypad_callout(m);
    set_bl(m, m.rd8(m.ss(), kBattleMenuState));
    uint8_t state = uint8_t(m.bx());
    bool play_round = false, outcome = false, leave = false;
    switch (state) {
    case 0: {
        m.set_al(0);
        for (uint16_t member_index = 0; member_index < 4; ++member_index) {
            m.bx() = member_index;
            m.wr8(m.ss(), uint16_t(kMemberSkipFlags + member_index), 0);
        }
        m.wr8(m.ss(), kBattleMember, 0);
        for (;;) {
            m.set_al(m.rd8(m.ss(), kBattleMember));
            battle_actor_pointer_core(m);
            m.set_al(m.rd8(m.ss(), uint16_t(m.si() + kActorStatus)));
            if ((m.al() & 0xE3) == 0)
                break;
            m.set_al(uint8_t(m.rd8(m.ss(), kBattleMember) + 1));
            if (m.al() == 4)
                break;
            m.wr8(m.ss(), kBattleMember, m.al());
        }
        battle_command_menu_input_core(m);
        m.set_al(m.rd8(m.ss(), kBattleDebug));
        if (m.al() == 1) {
            m.set_al(m.rd8(m.ss(), kDebugAutoWin));
            if (m.al() != 0) {
                m.wr8(m.ss(), kBattleMenuState, 5);
                battle_menu_dispatcher_core(m);
                m.set_al(2);
                outcome = true;
            }
        }
        break;
    }
    case 1:
        battle_target_menu_input_core(m);
        break;
    case 2:
        blink_battle_command_cursor_core(m);
        battle_strategy_menu_input_core(m);
        break;
    case 4:
        blink_battle_command_cursor_core(m);
        handle_ally_target_keys_core(m); // handle_ally_target_keys: no core
        break;
    case 6: {
        m.set_al(m.rd8(m.ss(), kBattleFlags));
        bool failed = true;
        if ((m.al() & 1) == 0) {
            // Flee only when allowed and a 0..100 roll is below 50; failure costs the party's turn.
            m.ax() = 0x6400;
            random_range_core(m);
            if (m.al() < 0x32) {
                m.wr8(m.ss(), kEffectY, 1);
                leave = true;
                failed = false;
            }
        }
        if (failed) {
            // L_360284: the escape failed; the party loses its turn
            m.set_al(0);
            m.wr8(m.ss(), kPopupState, 0);
            show_cannot_escape_message_core(m);
            m.si() = kBattleOrder;
            m.set_al(0xFF);
            for (uint16_t member = 0; member < 4; ++member) {
                m.wr8(m.ss(), uint16_t(m.si() + member * 4), 0xFF);
                m.wr8(m.ss(), uint16_t(m.si() + member * 4 + 3), uint8_t(member));
            }
            for (uint16_t member_index = 0; member_index < 4; ++member_index) {
                m.bx() = member_index;
                m.wr8(m.ss(), uint16_t(kMemberSkipFlags + member_index), 0xFF);
            }
            play_round = true;
        }
        break;
    }
    case 7:
        leave = true;
        break;
    default:
        play_round = true;
        break;
    }
    if (play_round) {
        // L_360232
        run_battle_round_core(m);
        m.set_al(m.rd8(m.ss(), kBattleEndState));
        outcome = m.al() != 0;
    }
    bool rewards = false;
    if (outcome) {
        // L_360240
        if (m.al() & 2) {
            // L_3602F2: victory
            m.cx() = 0xC;
            do {
                wait_vblank_callout(m);
                read_keypad_callout(m);
                m.ax() = uint16_t(m.rd16(m.ss(), kKeysRepeat) & kKeyA);
                if (m.ax() != 0)
                    break;
            } while (--m.cx() != 0);
            m.ax() = 0x95;
            m.wr8(m.ss(), kMusicRequest, m.al());
            m.wr8(m.ss(), kEffectY, 0);
            m.set_al(0);
            m.wr8(m.ss(), kPopupState, 0);
            show_party_victory_message_core(m);
            rewards = true;
        } else if (m.al() & 4) {
            // L_3604A4: the party is wiped out
            m.ax() = 0x96;
            m.wr8(m.ss(), kMusicRequest, m.al());
            m.set_al(0);
            m.wr8(m.ss(), kPopupState, 0);
            show_party_wiped_out_message_core(m);
            do {
                flush_battle_frame_core(m);
                read_keypad_callout(m);
                m.ax() = uint16_t(m.rd16(m.ss(), kKeysPressed) & kKeyA);
            } while (m.ax() == 0);
            m.ax() = 0x93;
            play_sound_effect_core(m, 0x93);
            fade_all_palettes_to_black_core(m);
            m.jmp_far(0x4000, 0x0EE0); // ljmp main
            return false;
        } else {
            m.wr8(m.ss(), kBattleMember, 0);
            m.wr8(m.ss(), kBattleMenuHistoryCount, 0);
            m.wr8(m.ss(), kBattleMenuState, 0);
            battle_menu_dispatcher_core(m);
        }
    }
    if (leave) {
        // L_3602D4
        m.set_al(0);
        m.wr8(m.ss(), kPopupState, 0);
        show_party_escaped_message_core(m);
        m.set_al(uint8_t(m.rd8(m.ss(), kBattleFlags) | 4));
        m.wr8(m.ss(), kBattleFlags, m.al());
        rewards = true;
    }
    if (rewards) {
        store_party_battle_records(m);
        battle_victory_rewards(m);
    }
    return true;
}

void battle_input_step(Machine &m) {
    if (battle_input_step_core(m))
        m.ret();
}

// rom: 0x360062
// The battle: clears the per-member battle flags (0xDD5A/5E/62, 0xDD2D-2F); with a debug encounter
// request (0xC81C) first runs the debug encounter menu (0x365A3D) and returns if it was cancelled
// (0xDD2A = FFh). Sets up the battle (0x361A21), picks the music (8Fh for the boss formations ACh and
// BDh-C6h, 8Eh for C7h, else 91h), draws the scene (0x3619E1, panels, 0x366507), then loops a frame at a
// time -- a random roll, battle_input_step, panels and status sprites (unless kBattleMenuState = 5),
// flush_battle_frame -- until the party has left (kBattleFlags bit 2) or a side is wiped out; then fades
// to black. A game over never returns (battle_input_step jumps to main).
static bool run_battle_body(Machine &m) {
    m.wr8(m.ss(), kDebugMapId, 0);
    m.wr8(m.ss(), kDebugBattleId, 0);
    m.wr8(m.ss(), kDebugAutoWin, 0);
    m.bx() = 0;
    do {
        m.wr8(m.ss(), uint16_t(kBattleStrengthBonuses + m.bx()), 0);
        m.wr8(m.ss(), uint16_t(kBattleDefenseBonuses + m.bx()), 0);
        m.wr8(m.ss(), uint16_t(kBattleAgilityBonuses + m.bx()), 0);
        m.bx() = uint16_t(m.bx() + 1);
    } while (m.bx() != 4);
    m.ax() = m.rd16(m.ss(), kIntroStrip);
    if (m.ax() != 0) {
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x6000;
            run_debug_configuration_menu_core(m);
            m.cs() = caller_cs;
        } // debug encounter menu: not decompiled
        m.set_al(m.rd8(m.ss(), kDebugMenuRow));
        if (m.al() == 0xFF)
            return true;
        m.set_al(0);
        m.wr8(m.ss(), kDebugAutoWin, 0);
    }
    setup_battle_core(m);
    m.set_al(m.rd8(m.ss(), kEnemyGroups));
    uint8_t first = m.al();
    if (first == 0xAC || (first >= 0xBD && first <= 0xC6))
        m.ax() = 0x8F;
    else if (first == 0xC7)
        m.ax() = 0x8E;
    else
        m.ax() = 0x91;
    m.wr8(m.ss(), kMusicRequest, m.al());
    fade_in_battle_scene_core(m);
    draw_battle_member_panels_core(m);
    battle_opening_core(m);
    for (;;) {
        m.ax() = 0x6400;
        random_range_core(m);
        if (!battle_input_step_core(m)) {
            // the near call's return address stays on the abandoned stack, as in the original
            m.sp() = uint16_t(m.sp() - 2);
            m.wr16(m.ss(), m.sp(), 0x0116);
            return false;
        }
        m.set_al(m.rd8(m.ss(), kBattleFlags));
        if (m.al() & 4)
            break;
        m.set_al(m.rd8(m.ss(), kBattleEndState));
        if (m.al() & 6)
            break;
        draw_battle_member_panels_core(m);
        m.set_al(m.rd8(m.ss(), kBattleMenuState));
        if (m.al() != 5) {
            m.ax() = 1;
            draw_battle_status_sprites_core(m);
        }
        flush_battle_frame_core(m);
    }
    fade_all_palettes_to_black_core(m);
    return true;
}

// Every battle (field encounters, scripted and intro battles) goes through here: the battle hook events.
bool run_battle_core(Machine &m) {
    hooks::emit(hooks::Event::BattleStart, m);
    const bool returned = run_battle_body(m);
    if (returned)
        hooks::emit(hooks::Event::BattleEnd, m);
    return returned;
}

void run_battle(Machine &m) {
    if (run_battle_core(m))
        m.retf();
}

// ---- battle message popups (bank 0x36). Each queues a few tile-word strings with append_word_queue
// (the first with BX = 0, starting a new line; the rest BX = FFh, continuing it) and opens the message popup
// (open_status_popup, 4000:CA6E, still original code: a call-out that waits for frames). D_38xxxx strings
// are in segment 8000h; 0xD60F/0xD62F/0xD66F are the actor-name, target-name and number buffers.
// restore_saved_registers (0x36C4D7) is a no-op, so its calls are left out.

namespace {
void queue_actor_icons_message(Machine &m, TextKey text) {
    m.ax() = 0;
    m.di() = kBattleActorIcons;
    build_status_icons_core(m); // AL = 0: party slot 0's status icons into 0xD60F
    queue_words(m, 0, kBattleActorIcons, 0);
    queue_words(m, text, 0xFF);
    open_battle_popup(m);
}
} // namespace

// rom: 0x36AB9C (near)
// Display that escape is impossible.
void show_cannot_escape_message_core(Machine &m) {
    queue_words(m, TextKey::show_cannot_escape_message_1, 0);
    open_battle_popup(m);
}

void show_cannot_escape_message(Machine &m) {
    show_cannot_escape_message_core(m);
    m.ret();
}

// rom: 0x36A15F (near)
// Show that the enemies have vanished.
void show_enemies_eliminated_message_core(Machine &m) {
    SavedRegisters saved(m);
    queue_words(m, TextKey::show_enemies_eliminated_message_1, 0);
    open_battle_popup(m);
    saved.restore(m);
}

void show_enemies_eliminated_message_call_core(Machine &m) { show_enemies_eliminated_message_core(m); }
void show_enemies_eliminated_message(Machine &m) {
    show_enemies_eliminated_message_call_core(m);
    m.ret();
}

// rom: 0x36A36D (near)
// Show the attack-missed message for the current item.
// Every register is saved around it.
void show_attack_missed_message_core(Machine &m) {
    SavedRegisters saved(m);
    queue_words(m, TextKey::show_attack_missed_message_1, 0);
    open_battle_popup(m);
    saved.restore(m);
}

void show_attack_missed_message_call_core(Machine &m) { show_attack_missed_message_core(m); }
void show_attack_missed_message(Machine &m) {
    show_attack_missed_message_call_core(m);
    m.ret();
}

// rom: 0x36C3EB (near)
// Display the target or enemy-group name and the number defeated.
void show_enemy_defeat_count_message_core(Machine &m) {
    SavedRegisters saved(m);
    queue_words(m, TextKey::show_enemy_defeat_count_message_1, 0);
    queue_words(m, TextKey::show_enemy_defeat_count_message_2, 0xFF);
    open_battle_popup(m);
    saved.restore(m);
}

void show_enemy_defeat_count_message(Machine &m) {
    show_enemy_defeat_count_message_core(m);
    m.ret();
}

// rom: 0x36BBBD (near)
// Shows the gold reward (the digits at 0xDC9B, format_battle_reward_total) followed by D_383372. Every
// register is saved around it.
void show_gold_reward_message_core(Machine &m) {
    SavedRegisters saved(m);
    queue_words(m, 0, kBattleRewardText, 0);
    queue_words(m, TextKey::show_gold_reward_message_1, 0xFF);
    open_battle_popup(m);
    saved.restore(m);
}

void show_gold_reward_message(Machine &m) {
    show_gold_reward_message_core(m);
    m.ret();
}

// rom: 0x36AADC (near)
// Display the party's victory message.
void show_party_victory_message_core(Machine &m) {
    queue_actor_icons_message(m, TextKey::show_party_victory_message_1);
}

void show_party_victory_message(Machine &m) {
    show_party_victory_message_core(m);
    m.ret();
}

// rom: 0x36AB1B (near)
// Display that the party escaped.
void show_party_escaped_message_core(Machine &m) {
    queue_actor_icons_message(m, TextKey::show_party_escaped_message_1);
}

void show_party_escaped_message(Machine &m) {
    show_party_escaped_message_core(m);
    m.ret();
}

// rom: 0x36AA95 (near)
// Display that the party has been wiped out.
void show_party_wiped_out_message_core(Machine &m) {
    SavedRegisters saved(m);
    queue_actor_icons_message(m, TextKey::show_party_wiped_out_message_1);
    saved.restore(m);
}

void show_party_wiped_out_message(Machine &m) {
    show_party_wiped_out_message_core(m);
    m.ret();
}

namespace {
// The target's name (build_battle_member_display into 0xD62F) followed by the given D_38xxxx strings.
void target_name_message(Machine &m, std::initializer_list<TextKey> texts, uint16_t timing_words = 0) {
    SavedRegisters saved(m);
    build_battle_member_display_core(m);
    queue_words(m, 0, kBattleTargetIcons, 0);
    for (TextKey t : texts)
        queue_words(m, t, 0xFF);
    open_battle_popup(m, timing_words);
    saved.restore(m);
}

// The actor name and both fragments, joined in one popup.
void actor_two_line_message(Machine &m, TextKey first, TextKey second) {
    JoinedBattlePopup joined(m, "show_actor_recovered_message");
    SavedRegisters saved(m);
    build_battle_member_name_or_icons_core(m);
    queue_words(m, 0, kBattleActorIcons, 0);
    queue_words(m, first, 0xFF);
    open_battle_popup(m);
    queue_words(m, second, 0);
    open_battle_popup(m);
    saved.restore(m);
}
} // namespace

// rom: 0x36A870 (near)
// Shows "<target> D_383188" (the target was knocked out). Every register is saved around it.
void show_target_knocked_out_message_core(Machine &m) {
    target_name_message(m, {TextKey::show_target_knocked_out_message_1});
}

void show_target_knocked_out_message(Machine &m) {
    show_target_knocked_out_message_core(m);
    m.ret();
}

// rom: 0x36A8B2 (near)
// Display that the target died.
void show_target_died_message_core(Machine &m) {
    target_name_message(m, {TextKey::show_target_died_message_1, TextKey::show_target_died_message_2});
}

void show_target_died_message_call_core(Machine &m) { show_target_died_message_core(m); }
void show_target_died_message(Machine &m) {
    show_target_died_message_call_core(m);
    m.ret();
}

// rom: 0x36C3A9 (near)
// Display that every member of the target group was defeated.
void show_target_group_defeated_message_core(Machine &m) {
    target_name_message(m, {TextKey::show_target_group_defeated_message_1});
}

void show_target_group_defeated_message(Machine &m) {
    show_target_group_defeated_message_core(m);
    m.ret();
}

// rom: 0x36A399 (near)
// Show that the attack did no damage.
// Every register is saved.
void show_no_damage_message_core(Machine &m) {
    SavedRegisters saved(m);
    build_battle_member_display_core(m);
    queue_words(m, 0, kBattleTargetIcons, 0);
    queue_item_count_footer_core(m);
    queue_words(m, TextKey::show_no_damage_message_1, 0xFF);
    open_battle_popup(m);
    saved.restore(m);
}

void show_no_damage_message_call_core(Machine &m) { show_no_damage_message_core(m); }
void show_no_damage_message(Machine &m) {
    show_no_damage_message_call_core(m);
    m.ret();
}

// rom: 0x36A0E9 (near)
// Show the target name and pending damage amount in the battle popup.
// Shows the damage message "<target> D_38347E <damage> D_383268": the damage word 0xDC0B is formatted into
// 0xD66F first. Every register is saved around it.
void show_damage_message_core(Machine &m) {
    SavedRegisters saved(m);
    m.ds() = 0;
    m.ax() = m.rd16(m.ss(), kMeterInputTarget);
    format_number_core(m);
    build_battle_member_display_core(m);
    queue_words(m, 0, kBattleTargetIcons, 0);
    queue_words(m, TextKey::show_damage_message_1, 0xFF);
    queue_words(m, 0, kBattleNumberText, 0xFF);
    queue_words(m, TextKey::show_damage_message_2, 0xFF);
    open_battle_popup(m);
    saved.restore(m);
}

void show_damage_message_call_core(Machine &m) { show_damage_message_core(m); }
void show_damage_message(Machine &m) {
    show_damage_message_call_core(m);
    m.ret();
}

// rom: 0x36A9F1 (near)
// Show that the acting actor is confused.
// Every register is saved around it.
void show_actor_confused_message_core(Machine &m) {
    SavedRegisters saved(m);
    build_battle_member_name_or_icons_core(m);
    queue_words(m, 0, kBattleActorIcons, 0);
    queue_words(m, TextKey::show_actor_confused_message_1, 0xFF);
    open_battle_popup(m);
    saved.restore(m);
}

void show_actor_confused_message(Machine &m) {
    show_actor_confused_message_core(m);
    m.ret();
}

// rom: 0x36AA33 (near)
// Show that the acting actor recovered from its status condition.
// Every register is saved around it.
void show_actor_recovered_message_core(Machine &m) {
    actor_two_line_message(m, TextKey::show_actor_recovered_message_1,
                           TextKey::show_actor_recovered_message_2);
}

void show_actor_recovered_message(Machine &m) {
    show_actor_recovered_message_core(m);
    m.ret();
}

// rom: 0x36A907 (near)
// Show the target name with its introductory battle description.
// The description comes from build_action_target_description (0x369DCD, a call-out: its code has no core
// yet). Every register is saved around it.
void show_target_description_message_core(Machine &m) {
    JoinedBattlePopup joined(m, "show_target_description_message");
    SavedRegisters saved(m);
    build_action_target_description_core(m); // build_action_target_description: no core
    queue_words(m, 0, kBattleTargetIcons, 0);
    queue_words(m, TextKey::show_target_description_message_1, 0xFF);
    open_battle_popup(m);
    queue_words(m, TextKey::show_target_description_message_2, 0);
    open_battle_popup(m);
    saved.restore(m);
}

void show_target_description_message(Machine &m) {
    show_target_description_message_core(m);
    m.ret();
}

// rom: 0x364091 (near)
// DI = word AL of the pointer table D_36D1FB (in this bank); ES = 6000h, AX = DI. BX preserved.
void battle_table_pointer_core(Machine &m) {
    m.es() = 0x6000;
    m.di() = 0xD1FB;
    m.ax() = table_at16(m, Table::monsters, m.es(), uint16_t(m.di() + uint16_t(m.al()) * 2));
    m.di() = m.ax();
}

void battle_table_pointer(Machine &m) {
    battle_table_pointer_core(m);
    m.ret();
}

// rom: 0x3664EF
// Uploads the sprite table and palettes, scrolls the screen by 8 (ports 05h/06h = 0/8) and waits a frame.
void present_battle_frame_scrolled_core(Machine &m) {
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.set_al(0);
    m.out8(5, m.al());
    m.set_al(8);
    m.out8(6, m.al());
    hooks::emit(hooks::Event::PresentationPicture, m, {5});
    m.call_far(0x4000, 0x0009); // wait_vblank
}

void present_battle_frame_scrolled(Machine &m) {
    present_battle_frame_scrolled_core(m);
    m.retf();
}

// rom: 0x36668A (near)
// If any enemy actor (4-1Eh) has the item DL equipped, increments the counter 0xDCBB (AL = its new value).
// DH = DL; CL ends at the actor found, or 1Fh. SI/AX are what actor_has_equipped_item leaves.
void count_enemy_with_equipped_item_core(Machine &m) {
    m.dx() = uint16_t(uint8_t(m.dx()) << 8 | uint8_t(m.dx()));
    m.ax() = 0;
    set_cl(m, 4);
    do {
        m.set_al(uint8_t(m.cx()));
        actor_has_equipped_item_core(m);
        if (m.al() == 1) {
            m.set_al(uint8_t(m.rd8(m.ss(), kEffectX) + 1));
            m.wr8(m.ss(), kEffectX, m.al());
            return;
        }
        set_cl(m, uint8_t(uint8_t(m.cx()) + 1));
    } while (uint8_t(m.cx()) != 0x1F);
}

void count_enemy_with_equipped_item(Machine &m) {
    count_enemy_with_equipped_item_core(m);
    m.ret();
}

// rom: 0x3666AC (near)
// If any party actor (0-3) has the item DL equipped, decrements the counter 0xDCBB (AL = its new value).
// DH = DL; CL ends at the actor found, or 4.
void uncount_party_with_equipped_item_core(Machine &m) {
    m.dx() = uint16_t(uint8_t(m.dx()) << 8 | uint8_t(m.dx()));
    m.ax() = 0;
    set_cl(m, 0);
    do {
        m.set_al(uint8_t(m.cx()));
        actor_has_equipped_item_core(m);
        if (m.al() == 1) {
            m.set_al(uint8_t(m.rd8(m.ss(), kEffectX) - 1));
            m.wr8(m.ss(), kEffectX, m.al());
            return;
        }
        set_cl(m, uint8_t(uint8_t(m.cx()) + 1));
    } while (uint8_t(m.cx()) != 4);
}

void uncount_party_with_equipped_item(Machine &m) {
    uncount_party_with_equipped_item_core(m);
    m.ret();
}

// rom: 0x3619E1 (near)
// Fades the battle scene in: clears the palette buffer (C400h, 256 words), then 16 steps of
// palette_step_up_toward (all palettes) with a sprite-table upload, a frame wait, scroll reset and palette
// upload each. CX = 0; DF cleared.
void fade_in_battle_scene_core(Machine &m) {
    m.ds() = 0;
    m.flags() = uint16_t(m.flags() & ~emu::DF);
    m.es() = 0;
    for (uint16_t i = 0; i < 0x100; ++i)
        m.wr16(m.es(), uint16_t(kPaletteBuffer + i * 2), 0);
    m.di() = uint16_t(kPaletteBuffer + 0x200);
    m.ax() = 0;
    uint8_t steps = 0x10;
    do {
        m.ax() = 0;
        m.cx() = 0x10;
        palette_step_up_toward_core(m);
        dma_sprite_table_core(m);
        m.call_far(0x4000, 0x0009); // wait_vblank
        m.set_al(0);
        m.out8(5, m.al());
        m.out8(6, m.al());
        dma_palettes_core(m);
        // Each step is a complete battle picture: the host battle screen fades in with it.
        hooks::emit(hooks::Event::PresentationPicture, m, {5});
    } while (--steps != 0);
    m.cx() = 0;
}

void fade_in_battle_scene(Machine &m) {
    fade_in_battle_scene_core(m);
    m.ret();
}

// rom: 0x361C98 (near)
// Loads a battle actor's graphics: SI = the actor record; its type byte (+5) goes through
// char_type_to_gfx_index (AL = graphics set, AH -> 0xDCBB = palette variant). Copies the set's tiles
// (D_3940B0/D_393F70 table: 8 tiles, then 4 from +800h and 4 from +880h) to tile 0xDCD5 (+8, +0Ch), records
// the palette source (D_3940B2) for slot 0xDCBC in kMemberPaletteOffsets/Segments/Variants and the palette
// number 0xDCD7 in kMemberPaletteSlots, and copies the variant's palette (source + variant * 32) to palette
// 0xDCD7. Every register is saved around it.
void load_battle_actor_graphics_core(Machine &m) {
    SavedRegisters saved(m);
    m.bx() = 0;
    m.si() = uint16_t(m.si() + 5);
    m.set_al(m.rd8(m.ss(), m.si()));
    char_type_to_gfx_index_core(m);
    set_bl(m, m.al());
    m.wr8(m.ss(), kEffectX, m.ah());
    m.bx() = uint16_t(m.bx() * 4);
    m.ds() = kDataSeg;
    m.si() = m.rd16(m.ds(), uint16_t(m.bx() + 0x40B0)); // D_3940B0: tile source offsets
    m.ds() = m.rd16(m.ds(), uint16_t(m.bx() + 0x3F70)); // D_393F70: their segments
    const uint16_t tiles = m.si();
    m.cx() = 8;
    m.ax() = m.rd16(m.ss(), kActorGraphicsTile);
    copy_tiles_core(m);
    m.si() = uint16_t(tiles + 0x800);
    m.ax() = uint16_t(m.rd16(m.ss(), kActorGraphicsTile) + 8);
    m.cx() = 4;
    copy_tiles_core(m);
    m.si() = uint16_t(tiles + 0x880);
    m.ax() = uint16_t(m.rd16(m.ss(), kActorGraphicsTile) + 0xC);
    m.cx() = 4;
    copy_tiles_core(m);
    m.ds() = kDataSeg;
    m.si() = m.rd16(m.ds(), uint16_t(m.bx() + 0x40B2)); // D_3940B2: palette source offsets
    m.ds() = m.rd16(m.ds(), uint16_t(m.bx() + 0x3F70));
    const uint8_t slot = m.rd8(m.ss(), kEffectY);
    m.ax() = m.si();
    // `mov bl,[0xDCBC]; shl bx,1` keeps BH from the graphics set * 4: set 40h+ writes elsewhere (quirk)
    m.wr16(m.ss(), uint16_t(kMemberPaletteOffsets + uint16_t((m.bx() & 0xFF00) | slot) * 2), m.ax());
    m.si() = uint16_t(m.si() + uint16_t(m.rd8(m.ss(), kEffectX)) * 0x20); // advance_record_pointer
    const uint16_t ds = m.ds(), si = m.si();
    m.cx() = 1;
    m.ax() = m.rd16(m.ss(), kActorGraphicsPalette);
    m.di() = kPaletteTargetBuffer;
    copy_palettes_core(m);
    m.si() = si;
    m.ds() = ds;
    m.wr8(m.ss(), uint16_t(kMemberPaletteVariants + slot), m.rd8(m.ss(), kEffectX));
    m.wr16(m.ss(), uint16_t(kMemberPaletteSegments + slot * 2), m.ds());
    m.wr16(m.ss(), uint16_t(kMemberPaletteSlots + slot * 2), m.rd16(m.ss(), kActorGraphicsPalette));
    saved.restore(m);
}

void load_battle_actor_graphics(Machine &m) {
    load_battle_actor_graphics_core(m);
    m.ret();
}

// rom: 0x363F20 (near)
// Reorder persistent party records by status while preserving the dialogue leader.
void sort_party_after_battle_core(Machine &m) {
    m.ax() = 0;
    m.ds() = 0;
    m.si() = kBattleOrder;
    m.di() = kGameState;
    for (int i = 0; i < 4; ++i)
        copy_byte_advance_stride_core(m);
    for (uint8_t i = 0; i < 4; ++i)
        m.wr8(m.ss(), uint16_t(m.si() + i), i);
    m.cx() = 0;
    do {
        const uint16_t pass_cx = m.cx();
        m.si() = kBattleOrder;
        do {
            set_dl(m, m.rd8(m.ss(), m.si()));
            m.set_al(m.rd8(m.ss(), uint16_t(m.si() + 1)));
            battle_priority_less_than_core(m);
            if (!(m.flags() & emu::CF)) {
                m.set_al(m.rd8(m.ss(), m.si()));
                set_dl(m, m.rd8(m.ss(), uint16_t(m.si() + 1)));
                m.wr8(m.ss(), m.si(), uint8_t(m.dx()));
                m.wr8(m.ss(), uint16_t(m.si() + 1), m.al());
                m.set_al(m.rd8(m.ss(), uint16_t(m.si() + 4)));
                set_dl(m, m.rd8(m.ss(), uint16_t(m.si() + 5)));
                m.wr8(m.ss(), uint16_t(m.si() + 4), uint8_t(m.dx()));
                m.wr8(m.ss(), uint16_t(m.si() + 5), m.al());
                const uint16_t ax = m.ax(), bx = m.bx();
                m.set_al(uint8_t(m.cx()));
                set_bl(m, uint8_t(uint8_t(m.cx()) + 1));
                {
                    const uint16_t caller_cs = m.cs();
                    m.cs() = 0x4000;
                    swap_member_progress_core(m);
                    m.cs() = caller_cs;
                } // swap_member_progress: core not exposed
                m.ax() = ax;
                m.bx() = bx;
            }
            m.si() = uint16_t(m.si() + 1);
            set_cl(m, uint8_t(uint8_t(m.cx()) + 1));
        } while (uint8_t(m.cx()) != 3);
        m.cx() = pass_cx;
        set_ch(m, uint8_t((m.cx() >> 8) + 1));
    } while ((m.cx() >> 8) != 4);
    const uint16_t ds = m.ds();
    m.ds() = m.ss();
    m.si() = kGameState;
    m.di() = kPartySortRecords;
    m.ax() = 0x1F * 4; // `mov al,1Fh; mov ah,4; mul ah`
    set_cl(m, m.al());
    copy_bytes_ds_si_to_ss_di_core(m);
    m.ds() = ds;
    m.set_al(m.rd8(m.ss(), kDialogueLeader));
    set_dl(m, m.al());
    m.si() = kBattleOrder + 4;
    for (uint8_t slot = 0; slot < 4; ++slot) {
        m.bx() = uint16_t(m.rd8(m.ss(), m.si()) << 8 | uint8_t(m.bx()));
        m.set_al(slot);
        stride_copy_row_core(m);
    }
    // Host names live outside the records: they move with them (slot i now holds old record order[i]), as in
    // the Order swap (swap_party_records). Without this a sort crossed names with records.
    if (auto *s = Session::find(m.wsc())) {
        auto &names = s->player_names().values;
        const PartyNameList old = names;
        for (uint16_t slot = 0; slot < 4; ++slot)
            if (const uint8_t from = m.rd8(m.ss(), uint16_t(kBattleOrder + 4 + slot)); from < 4)
                names[slot] = old[from];
    }
}

void sort_party_after_battle(Machine &m) {
    sort_party_after_battle_core(m);
    m.ret();
}

// rom: 0x36CD6C
// Dissolves enemy group AL (0-2; any other value returns at once, DL = AL): with sound 99h, copies the
// group's CL * 32 bytes of tile data (from DS=E000h:[0xD500 + 0Ch*AL], CL = word [0xD506 + 0Ch*AL]; CL = 0
// copies 64K -- an original quirk) to 0xDD78, then every 5 frames ANDs each 4-byte row with the mask byte
// D_3EBC18[step] and uploads the tiles (copy_tiles to tile 300h/328h/350h), until a pass leaves every byte
// zero. Scratch: 0xEC78 (OR of the pass), 0xEC79 (the step).
void dissolve_enemy_group_core(Machine &m) {
    set_dl(m, m.al());
    static constexpr uint16_t kTileBase[3] = {0x300, 0x328, 0x350};
    if (m.al() > 2)
        return;
    const uint16_t rec = uint16_t(kFormationBackTileSource + m.al() * 0xC);
    m.ax() = m.rd16(m.ss(), rec);
    m.di() = m.ax();
    m.ax() = kTileBase[uint8_t(m.dx())];
    m.cx() = m.rd16(m.ss(), uint16_t(rec + 6));
    const uint16_t tile = m.ax();
    m.ax() = 0x99;
    play_sound_effect_core(m, 0x99);
    const uint16_t count = m.cx();
    m.ds() = 0xE000;
    m.si() = kDissolveTiles;
    m.ax() = uint16_t(uint8_t(count) * 0x20);
    m.cx() = m.ax();
    do {
        m.set_al(m.rd8(m.ds(), m.di()));
        m.wr8(m.ss(), m.si(), m.al());
        m.si() = uint16_t(m.si() + 1);
        m.di() = uint16_t(m.di() + 1);
    } while (--m.cx() != 0);
    m.ax() = tile;
    m.cx() = count;
    m.wr8(m.ss(), kDissolveStep, 0);
    do {
        m.wr8(m.ss(), kDissolveAny, 0);
        m.di() = kDissolveTiles;
        m.ax() = uint16_t(uint8_t(count) * 8);
        m.cx() = m.ax();
        do {
            m.ax() = m.rd8(m.ss(), kDissolveStep);
            m.si() = uint16_t(0xBC18 + m.ax()); // D_3EBC18
            m.bx() = 0xE000;
            m.ds() = m.bx();
            m.bx() = 0;
            for (; m.bx() < 4; m.bx() = uint16_t(m.bx() + 1)) {
                uint8_t dl = m.rd8(m.ss(), uint16_t(m.bx() + m.di()));
                const uint8_t dh = m.rd8(m.ds(), m.si());
                dl &= dh;
                m.dx() = uint16_t(dh << 8 | dl);
                m.wr8(m.ss(), uint16_t(m.bx() + m.di()), dl);
                m.wr8(m.ss(), kDissolveAny, uint8_t(m.rd8(m.ss(), kDissolveAny) | dl));
            }
            m.di() = uint16_t(m.di() + 4);
        } while (--m.cx() != 0);
        m.ax() = tile;
        m.cx() = count;
        m.ds() = m.ss();
        m.si() = kDissolveTiles;
        copy_tiles_core(m);
        if (Machine::enemy_dissolve_hook) [[unlikely]]
            Machine::enemy_dissolve_hook(m, (tile - 0x300) / 0x28, m.rd8(m.ss(), kDissolveStep));
        m.ax() = tile;
        m.cx() = count;
        set_dl(m, uint8_t(m.rd8(m.ss(), kDissolveStep) + 1));
        m.wr8(m.ss(), kDissolveStep, uint8_t(m.dx()));
        {
            SavedRegisters saved(m);
            m.cx() = 5;
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0x4000;
                wait_frames_core(m);
                m.cs() = caller_cs;
            } // wait_frames
            saved.restore(m);
        }
        set_dl(m, m.rd8(m.ss(), kDissolveAny));
    } while (uint8_t(m.dx()) != 0);
}

void dissolve_enemy_group(Machine &m) {
    dissolve_enemy_group_core(m);
    m.retf();
}

// rom: 0x364E40 (near)
// Recount the target's live group and dissolve its portrait when no active members remain.
void remove_defeated_enemy_group_core(Machine &m) {
    hooks::EventScope audio(m, hooks::Event::SoundContext, {int(hooks::AudioContext::Death)});
    SavedRegisters saved(m);
    tally_enemy_type_counts_core(m);
    m.set_al(m.rd8(m.ss(), kActionTarget));
    m.di() = kEnemyGroups;
    battle_actor_pointer_core(m);
    m.si() = uint16_t(m.si() + 0x1F);
    set_cl(m, 0);
    for (;;) {
        m.ax() = uint16_t(m.rd8(m.ss(), m.di()) << 8 | m.rd8(m.ss(), m.si()));
        if (m.al() == m.ah())
            break;
        m.di() = uint16_t(m.di() + 2);
        set_cl(m, uint8_t(uint8_t(m.cx()) + 1));
    }
    m.set_al(m.rd8(m.ss(), uint16_t(m.di() + 1)));
    if (m.al() == 0) {
        m.set_al(0);
        m.out8(5, m.al());
        m.out8(6, m.al());
        m.set_al(m.rd8(m.ss(), m.si()));
        const uint8_t type = m.al();
        if (type == 0xAC || (type >= 0xBD && type <= 0xC6)) {
            const int group = uint8_t(m.cx());
            hooks::EventScope effect(m, hooks::Event::BattleDefeatEffect, {1, group}, {0, group});
            m.ds() = 0x8000;
            m.si() = 0x2CE5; // D_382CE5
            m.ax() = 0;
            m.cx() = 4;
            m.di() = kPaletteTargetBuffer;
            copy_palettes_core(m);
            auto fade = [&](int steps) {
                for (int i = 0; i < steps; ++i) {
                    m.ax() = 0;
                    m.cx() = 4;
                    palette_step_toward_core(m);
                    dma_sprite_table_core(m);
                    m.call_far(0x4000, 0x0009); // wait_vblank
                    dma_palettes_core(m);
                }
                m.cx() = 0;
            };
            fade(4);
            m.ax() = 0x9A;
            play_sound_effect_core(m, 0x9A);
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0x6000;
                run_battle_raster_shutter_core(m);
                m.cs() = caller_cs;
            } // not decompiled
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0x6000;
                load_battle_palette_display_core(m);
                m.cs() = caller_cs;
            } // load_battle_palette_display: no core
            fade(0x10);
        } else if (type == 0xC7) {
            const int group = uint8_t(m.cx());
            hooks::EventScope effect(m, hooks::Event::BattleDefeatEffect, {1, group}, {0, group});
            m.ax() = 0xA2;
            play_sound_effect_core(m, 0xA2);
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0x6000;
                run_battle_raster_sweep_core(m);
                m.cs() = caller_cs;
            } // not decompiled
        } else {
            m.set_al(uint8_t(m.cx()));
            m.wr8(m.ss(), kTargetGroup, uint8_t(m.cx()));
            dissolve_enemy_group_core(m);
            present_battle_frame_scrolled_core(m);
        }
    }
    saved.restore(m);
}

void remove_defeated_enemy_group(Machine &m) {
    remove_defeated_enemy_group_core(m);
    m.ret();
}

namespace {
uint8_t cl_of(Machine &m) { return uint8_t(m.cx()); }
uint8_t ch_of(Machine &m) { return uint8_t(m.cx() >> 8); }
void set_dh(Machine &m, uint8_t value) { m.dx() = uint16_t((m.dx() & 0x00FF) | value << 8); }
} // namespace

// rom: 0x3625A9 (near)
// Expand an enemy type/count pair into battle actors, separating passive flags from usable skills.
void init_enemy_group_actors_core(Machine &m) {
    m.ds() = 0;
    m.ax() = 0x6000;
    m.es() = m.ax();
    m.set_al(m.rd8(m.ss(), m.si()));
    m.wr8(m.ss(), kEnemyTypeScratch, m.al());
    set_ch(m, cl_of(m));
    set_cl(m, m.rd8(0, uint16_t(m.si() + 1)));
    m.wr8(0, kSkillFlagsA, 0);
    m.wr8(0, kSkillFlagsB, 0);
    m.di() = uint16_t(kBattleActors + ch_of(m) * 0x20);
    m.bx() = m.rd8(0, m.si());
    m.si() = kBattleTypePointers;
    m.bx() = uint16_t(m.bx() * 2);
    m.ax() = table_at16(m, Table::monsters, m.es(), uint16_t(m.bx() + m.si()));
    m.si() = m.ax();
    for (;;) {
        m.set_al(0);
        m.wr8(m.ss(), kSkillFlagsA, 0);
        m.wr8(m.ss(), kSkillFlagsB, 0);
        m.set_al(0xFF);
        for (uint16_t k = 0; k < 16; ++k)
            m.wr8(m.ss(), uint16_t(m.di() + 0xE + k), 0xFF);
        if (cl_of(m) == 0)
            return;
        set_dl(m, table_at(m, Table::monsters, m.es(), m.si()));
        m.wr8(0, m.di(), uint8_t(m.dx()));
        const uint16_t cx = m.cx(), si = m.si(), di = m.di();
        set_cl(m, table_at(m, Table::monsters, m.es(), uint16_t(m.si() + 2)));
        m.si() = uint16_t(m.si() + 0xB);
        m.di() = uint16_t(m.di() + 0xE);
        do {
            m.ax() = table_at(m, Table::monsters, m.es(), m.si());
            set_dl(m, m.al());
            m.bx() = 0;
            lookup_table_byte_core(m);
            if (m.al() & 0xC0) {
                const uint16_t flags_at = (m.al() & 0x40) ? kSkillFlagsA : kSkillFlagsB;
                m.set_al(uint8_t(m.dx()));
                m.bx() = 5;
                lookup_table_byte_core(m);
                m.wr8(0, flags_at, uint8_t(m.rd8(0, flags_at) | m.al()));
                m.set_al(0xFF);
                m.wr8(0, m.di(), 0xFF);
                m.wr8(0, uint16_t(m.di() + 1), 0xFF);
            } else if (m.al() & 1) {
                m.set_al(uint8_t(m.dx()));
                m.bx() = 3;
                lookup_table_byte_core(m);
                m.wr8(0, m.di(), uint8_t(m.dx()));
                m.wr8(0, uint16_t(m.di() + 1), m.al());
            } else {
                m.wr8(0, m.di(), uint8_t(m.dx()));
                set_dh(m, 0xFF);
                m.wr8(0, uint16_t(m.di() + 1), 0xFF);
            }
            m.si() = uint16_t(m.si() + 1);
            m.di() = uint16_t(m.di() + 2);
            set_cl(m, uint8_t(cl_of(m) - 1));
        } while (cl_of(m) != 0);
        m.cx() = cx;
        m.si() = si;
        m.di() = di;
        m.set_al(0);
        m.wr8(0, uint16_t(m.di() + 2), 0);
        m.ax() = table_at16(m, Table::monsters, m.es(), uint16_t(m.si() + 3));
        m.wr16(0, uint16_t(m.di() + 3), m.ax());
        m.wr16(0, uint16_t(m.di() + 5), m.ax());
        for (uint16_t k = 0; k < 4; ++k) {
            m.set_al(table_at(m, Table::monsters, m.es(), uint16_t(m.si() + 5 + k)));
            m.wr8(0, uint16_t(m.di() + 7 + k), m.al());
        }
        m.set_al(table_at(m, Table::monsters, m.es(), uint16_t(m.si() + 1)));
        m.wr8(0, uint16_t(m.di() + 1), m.al());
        m.set_al(m.rd8(m.ss(), kEnemyTypeScratch));
        m.wr8(0, uint16_t(m.di() + 0x1F), m.al());
        m.di() = uint16_t(m.di() + 0xB);
        m.set_al(m.rd8(0, kSkillFlagsA));
        m.wr8(0, m.di(), m.al());
        m.set_al(m.rd8(0, kSkillFlagsB));
        m.wr8(0, uint16_t(m.di() + 1), m.al());
        set_cl(m, uint8_t(cl_of(m) - 1));
        set_ch(m, uint8_t(ch_of(m) + 1));
        m.di() = uint16_t(kBattleActors + ch_of(m) * 0x20);
    }
}

void init_enemy_group_actors(Machine &m) {
    init_enemy_group_actors_core(m);
    m.ret();
}

// rom: 0x3621EF (near)
// Build party stats/skills and randomly sized enemy groups from the selected formation.
void init_battle_actors_core(Machine &m) {
    m.ds() = 0;
    m.ax() = 0x6000;
    m.es() = m.ax();
    set_cl(m, 0);
    m.wr8(0, kSkillFlagsA, 0);
    m.wr8(0, kSkillFlagsB, 0);
    m.si() = kGameState;
    m.di() = uint16_t(kBattleActors + 2);
    m.set_al(0xFF);
    for (set_cl(m, 0); cl_of(m) != 0x1F; set_cl(m, uint8_t(cl_of(m) + 1))) {
        m.wr8(m.ss(), m.di(), 0xFF);
        m.di() = uint16_t(m.di() + 0x20);
    }
    set_cl(m, 0);
    m.di() = kBattleActors;
    while (cl_of(m) != 4) {
        const uint16_t si0 = m.si();
        m.bx() = 0;
        m.si() = uint16_t(m.si() + 5);
        set_bl(m, m.rd8(0, m.si()));
        m.wr8(0, uint16_t(m.di() + 0x1F), uint8_t(m.bx()));
        m.si() = kBattleTypePointers;
        m.bx() = uint16_t(m.bx() * 2);
        m.ax() = table_at16(m, Table::monsters, m.es(), uint16_t(m.bx() + m.si()));
        m.si() = m.ax();
        set_dl(m, table_at(m, Table::monsters, m.es(), m.si()));
        m.wr8(0, m.di(), uint8_t(m.dx()));
        m.si() = si0;
        const uint16_t cx = m.cx(), si = m.si(), di = m.di();
        set_cl(m, 8);
        m.si() = uint16_t(m.si() + 0xF);
        m.di() = uint16_t(m.di() + 0xE);
        m.set_al(0);
        m.wr8(m.ss(), kSkillFlagsA, 0);
        m.wr8(m.ss(), kSkillFlagsB, 0);
        while (cl_of(m) != 0) {
            m.ax() = m.rd8(0, m.si());
            set_dh(m, m.rd8(0, uint16_t(m.si() + 1)));
            set_dl(m, m.al());
            m.bx() = 0;
            lookup_table_byte_core(m);
            m.wr8(0, m.di(), uint8_t(m.dx()));
            m.wr8(0, uint16_t(m.di() + 1), uint8_t(m.dx() >> 8));
            if (m.al() & 0xC0) {
                const uint16_t flags_at = (m.al() & 0x40) ? kSkillFlagsA : kSkillFlagsB;
                m.set_al(uint8_t(m.dx()));
                m.bx() = 5;
                lookup_table_byte_core(m);
                m.wr8(0, flags_at, uint8_t(m.rd8(0, flags_at) | m.al()));
            }
            m.si() = uint16_t(m.si() + 2);
            m.di() = uint16_t(m.di() + 2);
            set_cl(m, uint8_t(cl_of(m) - 1));
        }
        m.cx() = cx;
        m.si() = si;
        m.di() = di;
        m.set_al(m.rd8(0, m.si()));
        m.wr8(0, uint16_t(m.di() + 2), m.al());
        m.ax() = m.rd16(0, uint16_t(m.si() + 6));
        m.wr16(0, uint16_t(m.di() + 3), m.ax());
        m.ax() = m.rd16(0, uint16_t(m.si() + 8));
        m.wr16(0, uint16_t(m.di() + 5), m.ax());
        static constexpr uint16_t kStatSaves[4] = {kBattleSavedStrength, kBattleSavedDefense,
                                                   kBattleSavedAgility, kBattleSavedMagic};
        static constexpr uint16_t kStatBonuses[3] = {kBattleStrengthBonuses, kBattleDefenseBonuses,
                                                     kBattleAgilityBonuses};
        for (uint16_t field_index = 0; field_index < 4; ++field_index) {
            const uint16_t persistent_stat = uint16_t(m.si() + 0xB + field_index),
                           battle_stat = uint16_t(m.di() + 7 + field_index);
            m.set_al(m.rd8(0, persistent_stat));
            // `push bx; mov bl,cl; ... pop bx`: BH (0 here, from the last lookup's BX) is kept
            m.wr8(m.ss(), uint16_t(((m.bx() & 0xFF00) | cl_of(m)) + kStatSaves[field_index]), m.al());
            // Save the original stat, cap its base at 99, then add battle-only bonuses with byte wrapping.
            if (m.al() >= 0x64) {
                m.set_al(0x63);
                m.wr8(0, persistent_stat, m.al());
            }
            m.wr8(0, battle_stat, m.al());
            if (field_index < 3) {
                m.set_al(m.rd8(m.ss(), uint16_t(cl_of(m) + kStatBonuses[field_index])));
                m.wr8(0, battle_stat, uint8_t(m.rd8(0, battle_stat) + m.al()));
            }
        }
        m.di() = uint16_t(m.di() + 0xB);
        m.set_al(m.rd8(0, kSkillFlagsA));
        m.wr8(0, m.di(), m.al());
        m.set_al(m.rd8(0, kSkillFlagsB));
        m.wr8(0, uint16_t(m.di() + 1), m.al());
        set_cl(m, uint8_t(cl_of(m) + 1));
        m.si() = uint16_t(kGameState + cl_of(m) * 0x1F);
        m.di() = uint16_t(kBattleActors + cl_of(m) * 0x20);
    }
    // the formation
    m.ax() = 0;
    m.es() = 0;
    m.ax() = 0x8000;
    m.ds() = m.ax();
    m.wr8(0, kBattleFlags, 0);
    m.ax() = uint16_t(m.rd8(0, kFormationId) * 2);
    m.si() = uint16_t(0x11C8 + m.ax()); // D_3811C8
    m.bx() = table_at16(m, Table::formations, m.ds(), m.si());
    m.si() = m.bx();
    m.set_al(table_at(m, Table::formations, m.ds(), m.si()));
    m.wr8(0, kBattleFlags, (m.al() & 0x10) ? 1 : 0);
    m.set_al(m.al() & 0xF);
    m.wr8(0, kEnemyGroupCount, m.al());
    m.si() = uint16_t(m.si() + 1);
    m.cx() = m.al();
    m.bx() = 0;
    for (uint16_t field_index = 0; field_index < 3; ++field_index) {
        m.wr8(m.ss(), uint16_t(kRolledEnemyGroups + field_index * 2), 0xFF);
        m.wr8(m.ss(), uint16_t((kRolledEnemyGroups + 0x1) + field_index * 2), 0);
    }
    while (cl_of(m) != 0) {
        m.set_al(table_at(m, Table::formations, m.ds(), m.si()));
        m.si() = uint16_t(m.si() + 1);
        m.wr8(0, uint16_t(m.bx() + kRolledEnemyGroups), m.al());
        m.bx() = uint16_t(m.bx() + 1);
        m.set_al(table_at(m, Table::formations, m.ds(), m.si()));
        m.si() = uint16_t(m.si() + 1);
        const uint16_t cx = m.cx();
        // Packed count byte: low nibble is the minimum, high nibble the maximum (inclusive).
        m.ax() = uint16_t(((m.al() >> 4) & 0xF) << 8 | (m.al() & 0xF)); // AL = low nibble, AH = high
        random_range_core(m);
        m.cx() = cx;
        m.wr8(0, uint16_t(m.bx() + kRolledEnemyGroups), m.al());
        m.bx() = uint16_t(m.bx() + 1);
        set_cl(m, uint8_t(cl_of(m) - 1));
    }
    m.bx() = 0;
    m.cx() = 0;
    m.wr8(m.ss(), (kEnemyGroups + 0x1), 0);
    m.wr8(m.ss(), (kEnemyGroups + 0x3), 0);
    m.wr8(m.ss(), (kEnemyGroups + 0x5), 0);
    set_cl(m, m.rd8(0, kEnemyGroupCount));
    m.si() = kEnemyGroups;
    m.di() = kRolledEnemyGroups;
    while (cl_of(m) != 0) {
        m.set_al(m.rd8(0, uint16_t(m.di() + 1)));
        if (m.al() != 0) {
            m.wr8(0, uint16_t(m.si() + 1), m.al());
            m.set_al(m.rd8(0, m.di()));
            m.wr8(0, m.si(), m.al());
            m.si() = uint16_t(m.si() + 2);
            set_ch(m, uint8_t(ch_of(m) + 1));
        }
        set_cl(m, uint8_t(cl_of(m) - 1));
        m.di() = uint16_t(m.di() + 2);
    }
    m.wr8(0, kEnemyGroupCount, ch_of(m));
    m.ax() = 0;
    m.es() = 0;
    m.ax() = 0x6000;
    m.ds() = m.ax();
    m.si() = uint16_t(kBattleActors + 0x80);
    for (set_cl(m, 0x1B); cl_of(m) != 0; set_cl(m, uint8_t(cl_of(m) - 1))) {
        m.wr8(m.ss(), uint16_t(m.si() + 3), 0);
        m.wr8(m.ss(), uint16_t(m.si() + 2), 0xFF);
        m.si() = uint16_t(m.si() + 0x20);
    }
    m.ax() = 0;
    m.ds() = 0;
    m.si() = kEnemyGroups;
    m.set_al(m.rd8(0, kEnemyGroupCount));
    m.wr8(0, kInitialEnemyGroupCount, m.al());
    const uint8_t groups = m.al();
    auto group = [&](uint16_t pair) {
        m.ax() = 0;
        m.ds() = 0;
        m.si() = pair;
        set_cl(m, 4);
        if (pair != kEnemyGroups) {
            m.set_al(m.rd8(m.ss(), (kEnemyGroups + 0x1)));
            set_cl(m, uint8_t(cl_of(m) + m.al()));
            if (pair == (kEnemyGroups + 0x4)) {
                m.set_al(m.rd8(m.ss(), (kEnemyGroups + 0x3)));
                set_cl(m, uint8_t(cl_of(m) + m.al()));
            }
        }
        init_enemy_group_actors_core(m);
    };
    m.ax() = 0;
    const uint8_t first_group_count = m.rd8(m.ss(), (kEnemyGroups + 0x1)),
                  second_group_count = m.rd8(m.ss(), (kEnemyGroups + 0x3)),
                  third_group_count = m.rd8(m.ss(), (kEnemyGroups + 0x5));
    if (groups == 2) {
        m.set_al(uint8_t(first_group_count + second_group_count));
        m.wr8(m.ss(), kEnemyCount, m.al());
        group(kEnemyGroups);
        group((kEnemyGroups + 0x2));
    } else if (groups == 3) {
        m.set_al(uint8_t(first_group_count + second_group_count + third_group_count));
        m.wr8(m.ss(), kEnemyCount, m.al());
        m.set_al(first_group_count);
        m.wr8(m.ss(), kInitialEnemyCounts, m.al());
        m.set_al(second_group_count);
        m.wr8(m.ss(), (kInitialEnemyCounts + 0x1), m.al());
        m.set_al(third_group_count);
        m.wr8(m.ss(), (kInitialEnemyCounts + 0x2), m.al());
        group(kEnemyGroups);
        group((kEnemyGroups + 0x2));
        group((kEnemyGroups + 0x4));
    } else {
        m.set_al(first_group_count);
        m.wr8(m.ss(), kEnemyCount, m.al());
        group(kEnemyGroups);
    }
}

void init_battle_actors(Machine &m) {
    init_battle_actors_core(m);
    m.ret();
}

// rom: 0x361A21 (near)
// Sets up a battle: after a frame wait, blanks the display (port 00h = 0), sets the screen-1 map base and
// scroll (10h = 8, 12h = 400h), clears screen 1 (fill_screen_rect), takes the formation 0xC818/0xC81A into
// 0xD6CD/0xD6CE, zeroes all 32 actor records (status FFh), builds the actors (init_battle_actors), the skill
// list, the tiles/palettes of the battle UI, the formation layers and enemy portraits, and each present
// party member's graphics (tiles 380h + 10h * slot, palette 8 + slot). Resets the battle-menu state; each
// member's selected equipment starts at the first usable slot (page 0xDC6E, row 0xDC62: rows 0-2 of page
// 0, then -- the row isn't reset (quirk) -- row 2 of pages 1-5). Opens the menu (battle_menu_dispatcher),
// draws the status sprites and turns the display on (port 00h = F107h).
void setup_battle_core(Machine &m) {
    m.call_far(0x4000, 0x0009); // wait_vblank
    m.ax() = 0;
    out16(m, 0, m.ax());
    m.ax() = 8;
    out16(m, 0x10, m.ax());
    m.ax() = 0x400;
    out16(m, 0x12, m.ax());
    m.ax() = 1;
    m.bx() = 0;
    m.cx() = 0x2020;
    m.dx() = 0x1FF;
    fill_screen_rect_core(m);
    m.ax() = m.rd16(m.ss(), kSavedSceneSp);
    m.wr8(m.ss(), kFormationId, m.al());
    m.ax() = m.rd16(m.ss(), kSceneIndex);
    m.wr8(m.ss(), kFormationSetIndex, m.al());
    set_cl(m, 0);
    m.si() = kBattleActors;
    m.set_al(0);
    do {
        for (uint16_t k = 0; k < 0x20; ++k)
            m.wr8(m.ss(), uint16_t(m.si() + k), k == 2 ? 0xFF : 0);
        m.si() = uint16_t(m.si() + 0x20);
        set_cl(m, uint8_t(uint8_t(m.cx()) + 1));
    } while (uint8_t(m.cx()) != 0x20);
    init_battle_actors_core(m);
    set_dl(m, 1);
    draw_battle_skill_list_core(m);
    m.ax() = 0x9000;
    m.ds() = m.ax();
    m.ax() = 0x100;
    m.cx() = 0x100;
    m.si() = 0x0080; // D_390080
    copy_tiles_core(m);
    m.ax() = 0xF;
    m.cx() = 1;
    m.si() = 0x0000; // D_390000
    m.di() = kPaletteTargetBuffer;
    copy_palettes_core(m);
    m.ax() = 7;
    m.cx() = 1;
    m.si() = 0x0020;
    m.di() = kPaletteTargetBuffer;
    copy_palettes_core(m);
    m.set_al(0);
    load_formation_set_core(m);
    m.ax() = 0x8000;
    m.ds() = m.ax();
    m.ax() = 0x40;
    m.cx() = 0x68;
    m.si() = 0x0200; // D_380200
    copy_tiles_core(m);
    m.ax() = 0x9000;
    m.ds() = m.ax();
    m.ax() = 0x13;
    m.cx() = 0x20;
    m.si() = 0x2480; // D_392480
    copy_tiles_core(m);
    m.ax() = 0x8000;
    m.ds() = m.ax();
    m.ax() = 0x3C0;
    m.cx() = 0xA;
    m.si() = 0x1010; // D_381010
    copy_tiles_core(m);
    build_formation_layer_records_core(m);
    m.set_al(0);
    draw_enemy_group_portraits_core(m);
    m.si() = kGameState; // D_38D000: the offset is game_state's
    m.ax() = 0x380;
    m.wr16(m.ss(), kActorGraphicsTile, m.ax());
    m.ax() = 8;
    m.wr16(m.ss(), kActorGraphicsPalette, m.ax());
    set_cl(m, 0);
    do {
        m.set_al(m.rd8(m.ss(), m.si()));
        if (m.al() != 0xFF) {
            m.wr8(m.ss(), kEffectY, uint8_t(m.cx()));
            load_battle_actor_graphics_core(m);
        }
        m.si() = uint16_t(m.si() + 0x1F);
        m.ax() = uint16_t(m.rd16(m.ss(), kActorGraphicsTile) + 0x10);
        m.wr16(m.ss(), kActorGraphicsTile, m.ax());
        m.ax() = uint16_t(m.rd16(m.ss(), kActorGraphicsPalette) + 1);
        m.wr16(m.ss(), kActorGraphicsPalette, m.ax());
        set_cl(m, uint8_t(uint8_t(m.cx()) + 1));
    } while (uint8_t(m.cx()) != 4);
    for (uint16_t flag_address : {int(kBattleEndState), 0xDBF8, int(kBlinkPhase), int(kDisplayFlickerCounter),
                                  int(kBattleMember), int(kCursorHidden), int(kCommandCursorVisible)})
        m.wr8(m.ss(), flag_address, 0);
    m.wr16(m.ss(), kSpecialActorPreviousHp, 0);
    m.wr8(m.ss(), kSpecialActorUnchangedHpCount, 0);
    m.wr8(m.ss(), kBattleOpeningDone, 0);
    m.set_al(0);
    for (uint16_t k = 0; k < 4; ++k)
        m.wr8(m.ss(), uint16_t(kMemberSkipFlags + k), 0);
    m.bx() = 3;
    m.wr8(m.ss(), kCommandPage, 1);
    for (m.bx() = 0; m.bx() != 4; m.bx() = uint16_t(m.bx() + 1)) {
        m.wr8(m.ss(), uint16_t(kTargetSlot + m.bx()), 0);
        m.cx() = 0;
        for (;;) {
            m.wr8(m.ss(), uint16_t(kEquipmentRow + m.bx()), uint8_t(m.cx()));
            m.wr8(m.ss(), uint16_t(kEquipmentPage + m.bx()), uint8_t(m.cx() >> 8));
            m.ax() = m.bx();
            m.ax() = selected_battle_equipment_core(m);
            if (m.ah() != 0 && m.ah() != 0xFF) {
                const uint16_t bx = m.bx();
                m.set_ah(0);
                m.bx() = 0;
                lookup_table_byte_core(m);
                m.bx() = bx;
                if (m.al() & 1)
                    break;
            }
            if (uint8_t(m.cx()) == 2)
                set_ch(m, uint8_t((m.cx() >> 8) + 1));
            else
                set_cl(m, uint8_t(uint8_t(m.cx()) + 1));
            if ((m.cx() >> 8) == 6)
                break;
        }
    }
    m.wr8(m.ss(), kTextColorFlag, 0);
    m.set_al(0);
    m.wr8(m.ss(), kPanelSpecialMode, m.al());
    m.wr8(m.ss(), kBattleMenuState, 5);
    m.wr8(m.ss(), kBattleMenuHistoryCount, 0);
    battle_menu_dispatcher_core(m);
    m.ax() = 0;
    draw_battle_status_sprites_core(m);
    m.ax() = 0xF107;
    out16(m, 0, m.ax());
}

void setup_battle(Machine &m) {
    setup_battle_core(m);
    m.ret();
}

// rom: 0x366507 (near)
// Use F9h/FAh/FBh equipment and a random roll to choose normal, party-surprise, or enemy-surprise turns.
void battle_opening_core(Machine &m) {
    auto open_menu = [&] {
        m.wr8(m.ss(), kBattleMenuState, 0);
        battle_menu_dispatcher_core(m);
    };
    auto wait_for_a = [&] {
        m.cx() = 0;
        for (;;) {
            m.call_far(0x4000, 0x0009); // wait_vblank
            m.call_far(0x4000, 0x005C); // read_keypad: no core
            m.ax() = uint16_t(m.rd16(m.ss(), kKeysPressed) & 4);
            if (m.ax() != 0)
                break;
            m.cx() = uint16_t(m.cx() + 1);
            if (m.cx() == 0x20)
                break;
        }
    };
    auto check = [&](uint8_t item, bool enemies, uint8_t cancel) {
        set_dl(m, item);
        if (enemies)
            count_enemy_with_equipped_item_core(m);
        else
            uncount_party_with_equipped_item_core(m);
        m.set_al(m.rd8(m.ss(), kEffectX));
        return m.al() == cancel;
    };
    auto roll_fails = [&] {
        m.ax() = 0x6400;
        random_range_core(m);
        return m.al() >= 0xA;
    };
    m.set_al(0);
    m.wr8(m.ss(), kBattleMode, 0);
    m.set_al(m.rd8(m.ss(), kBattleOpeningDone));
    if (m.al() != 0)
        return;
    m.set_al(1);
    m.wr8(m.ss(), kBattleOpeningDone, 1);
    m.wr8(m.ss(), kEffectX, 0);
    set_dl(m, 0xF9);
    count_enemy_with_equipped_item_core(m);
    set_dl(m, 0xF9);
    uncount_party_with_equipped_item_core(m);
    m.set_al(m.rd8(m.ss(), kEffectX));
    if (m.al() == 0) {
        open_menu();
    } else if (m.al() != 1) {
        m.wr8(m.ss(), kEffectX, 0);
        if (check(0xFA, true, 1) || check(0xFB, true, 1) || roll_fails()) {
            open_menu();
            return;
        }
        m.set_al(2);
        m.wr8(m.ss(), kBattleMode, 2);
        m.wr8(m.ss(), kPopupState, 0);
        show_party_surprise_message_core(m);
        wait_for_a();
        open_menu();
    } else {
        m.wr8(m.ss(), kEffectX, 0);
        if (check(0xFA, false, 0xFF) || check(0xFB, false, 0xFF) || roll_fails()) {
            open_menu();
            return;
        }
        m.set_al(1);
        m.wr8(m.ss(), kBattleMode, 1);
        m.wr8(m.ss(), kPopupState, 0);
        show_enemy_surprise_message_core(m);
        m.si() = kBattleOrder;
        m.set_al(0xFF);
        for (uint8_t k = 0; k < 4; ++k) {
            m.wr8(m.ss(), uint16_t(m.si() + k * 4), 0xFF);
            m.wr8(m.ss(), uint16_t(m.si() + k * 4 + 3), k);
        }
        for (uint16_t k = 0; k < 4; ++k)
            m.wr8(m.ss(), uint16_t(kMemberSkipFlags + k), 0xFF);
        m.bx() = 3;
        m.cx() = 4;
        {
            const uint16_t caller_cs = m.cs();
            m.cs() = 0x4000;
            wait_frames_core(m);
            m.cs() = caller_cs;
        } // wait_frames
        wait_for_a();
    }
}

void battle_opening(Machine &m) {
    battle_opening_core(m);
    m.ret();
}

// rom: 0x363434 (near)
// Places the equipment-column cursor for the current member (kBattleMember): hides sprite 8 (C022h = 90h),
// then sets sprites 4-7's tile/position words from the member's column 0xDC66 (0-2, any other value = 3):
// X 74h/7Ch, Y 0/38h/70h/A8h (first row) and +8 (second row); then the cursor tiles into sprite slot 10h
// (draw_battle_cursor_tiles_alt) and the marker (draw_battle_marker_sprite). DS = 0.
void place_equipment_column_cursor_core(Machine &m) {
    m.ax() = 0x90;
    m.wr16(m.ss(), (kSpritePositions + 0x20), m.ax());
    m.ax() = 0;
    m.ds() = 0;
    m.bx() = m.rd8(m.ss(), kBattleMember);
    m.set_al(m.rd8(m.ss(), uint16_t(m.bx() + kTargetSlot)));
    const uint16_t y = m.al() == 0 ? 0x0000 : m.al() == 1 ? 0x3800 : m.al() == 2 ? 0x7000 : 0xA800;
    m.ds() = 0;
    m.wr16(m.ss(), (kSpritePositions + 0x10), uint16_t(y + 0x74));
    m.wr16(m.ss(), (kSpritePositions + 0x14), uint16_t(y + 0x874));
    m.wr16(m.ss(), (kSpritePositions + 0x18), uint16_t(y + 0x7C));
    m.ax() = uint16_t(y + 0x87C);
    m.wr16(m.ss(), (kSpritePositions + 0x1C), m.ax());
    const uint16_t ds = m.ds();
    draw_battle_cursor_tiles_core(m, uint16_t(kSpriteBuffer + 0x10)); // draw_battle_cursor_tiles_alt
    m.ds() = ds;
    draw_battle_marker_sprite_core(m);
}

void place_equipment_column_cursor(Machine &m) {
    place_equipment_column_cursor_core(m);
    m.ret();
}

namespace {
// The actor's name/icons (build_battle_member_name_or_icons into 0xD60F) followed by the given D_38xxxx
// strings, in one popup. Every register is saved around it.
void actor_name_message(Machine &m, std::initializer_list<TextKey> texts) {
    SavedRegisters saved(m);
    build_battle_member_name_or_icons_core(m);
    queue_words(m, 0, kBattleActorIcons, 0);
    for (TextKey t : texts)
        queue_words(m, t, 0xFF);
    open_battle_popup(m);
    saved.restore(m);
}
} // namespace

// rom: 0x36B337 (near)
// Display that the acting actor's poison ended.
void show_actor_poison_ended_message_core(Machine &m) {
    actor_name_message(
        m, {TextKey::show_actor_poison_ended_message_1, TextKey::show_actor_poison_ended_message_2});
}

void show_actor_poison_ended_message(Machine &m) {
    show_actor_poison_ended_message_core(m);
    m.ret();
}

// rom: 0x36AC7D (near)
// Display that the actor's paralysis ended.
void show_actor_paralysis_ended_message_core(Machine &m) {
    actor_name_message(m, {TextKey::show_actor_paralysis_ended_message_1});
}

void show_actor_paralysis_ended_message(Machine &m) {
    show_actor_paralysis_ended_message_core(m);
    m.ret();
}

// rom: 0x36B0F7 (near)
// Display that the actor is paralyzed.
void show_actor_paralyzed_message_core(Machine &m) {
    actor_name_message(m, {TextKey::show_actor_paralyzed_message_1});
}

void show_actor_paralyzed_message(Machine &m) {
    show_actor_paralyzed_message_core(m);
    m.ret();
}

// rom: 0x36B073 (near)
// Display that the actor woke up.
void show_actor_awakened_message_core(Machine &m) {
    actor_name_message(m, {TextKey::show_actor_awakened_message_1});
}

void show_actor_awakened_message(Machine &m) {
    show_actor_awakened_message_core(m);
    m.ret();
}

// rom: 0x36B0B5 (near)
// Display that the actor is asleep.
void show_actor_asleep_message_core(Machine &m) {
    actor_name_message(m, {TextKey::show_actor_asleep_message_1});
}

void show_actor_asleep_message(Machine &m) {
    show_actor_asleep_message_core(m);
    m.ret();
}

// rom: 0x36B5FE (near)
// Show poison damage with the actor and amount in one popup.
// Every register is saved around it.
void show_actor_amount_message_core(Machine &m) {
    JoinedBattlePopup joined(m, "show_actor_amount_message");
    SavedRegisters saved(m);
    build_battle_member_name_or_icons_core(m);
    m.ds() = 0;
    m.ax() = m.rd16(m.ss(), kMeterInputTarget);
    format_number_core(m);
    queue_words(m, 0, kBattleActorIcons, 0);
    queue_words(m, TextKey::show_actor_amount_message_1, 0xFF);
    open_battle_popup(m);
    queue_words(m, 0, kBattleNumberText, 0);
    queue_words(m, TextKey::show_actor_amount_message_2, 0xFF);
    open_battle_popup(m);
    saved.restore(m);
}

void show_actor_amount_message(Machine &m) {
    show_actor_amount_message_core(m);
    m.ret();
}

// rom: 0x360CE2 (near)
// Cure poison on a magic-based roll; otherwise inflict maximum HP / 16 damage.
void apply_status_10_effect_core(Machine &m) {
    const uint16_t bx = m.bx(), cx = m.cx();
    m.set_al(uint8_t(m.cx()));
    battle_actor_pointer_core(m);
    set_dh(m, m.rd8(m.ss(), uint16_t(m.si() + 0xA)));
    m.ax() = 0x6400;
    random_range_core(m);
    if (m.al() < uint8_t(m.dx() >> 8)) {
        m.set_al(m.rd8(m.ss(), uint16_t(m.si() + 2)));
        set_dl(m, m.al());
        m.set_al(m.al() & 0xEF);
        BattleActorState(m, m.si()).write_status(m.al());
        m.wr8(m.ss(), kTargetGroup, m.al());
        m.wr8(m.ss(), kActingActor, uint8_t(m.cx()));
        show_actor_poison_ended_message_core(m);
    } else {
        m.ax() = m.rd16(m.ss(), uint16_t(m.si() + 3));
        m.dx() = uint16_t(m.rd16(m.ss(), uint16_t(m.si() + 5)) >> 4);
        m.wr16(m.ss(), kMeterInputTarget, m.dx());
        m.set_al(2);
        m.di() = kBattleOrder;
        m.wr8(m.ss(), uint16_t(m.di() + 3), uint8_t(m.cx()));
        m.wr8(m.ss(), kActionTarget, uint8_t(m.cx()));
        m.set_al(uint8_t(m.cx()));
        const uint16_t si = m.si();
        battle_actor_pointer_core(m);
        m.set_al(m.rd8(m.ss(), uint16_t(m.si() + 0x1F)));
        m.si() = si;
        party_slot_for_name_id_core(m);
        m.wr8(m.ss(), kTargetGroup, m.al());
        m.wr8(m.ss(), kActingActor, uint8_t(m.cx()));
        m.wr8(m.ss(), kActionTarget, uint8_t(m.cx()));
        show_actor_amount_message_core(m);
        m.wr8(m.ss(), kActionId, 0);
        m.dx() = m.rd16(m.ss(), kMeterInputTarget);
        set_battle_meter_target_core(m);
        m.set_al(1);
        m.wr8(m.ss(), kBattleKnockoutMode, 1);
        check_target_knocked_out_core(m);
    }
    m.cx() = cx;
    m.bx() = bx;
}

void apply_status_10_effect(Machine &m) {
    apply_status_10_effect_core(m);
    m.ret();
}

namespace {
// Roll item-field-4 recovery odds, clearing the selected status on success.
void try_cure_actor_status(Machine &m, uint8_t item, uint8_t keep_mask, void (*cured)(Machine &),
                           void (*still)(Machine &)) {
    m.ax() = item;
    m.bx() = 4;
    lookup_table_byte_core(m);
    set_cl(m, m.al());
    m.ax() = 0x6400;
    random_range_core(m);
    if (m.al() >= uint8_t(m.cx())) {
        still(m);
        return;
    }
    cured(m);
    m.set_al(m.rd8(m.ss(), kActingActor));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 2)) & keep_mask));
    BattleActorState(m, m.si()).write_status(m.al());
    {
        SavedRegisters saved(m);
        set_dl(m, 0xF);
        draw_battle_skill_list_core(m);
        saved.restore(m);
    }
    draw_battle_member_panels_core(m);
    m.set_al(0);
    m.out8(5, m.al());
    m.out8(6, m.al());
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.call_far(0x4000, 0x0009); // wait_vblank
}
} // namespace

// rom: 0x3669E6 (near)
// Roll paralysis recovery using item FEh's field 4.
void try_cure_status_40_core(Machine &m) {
    try_cure_actor_status(m, 0xFE, 0xBF, show_actor_paralysis_ended_message_core,
                          show_actor_paralyzed_message_core);
}

void try_cure_status_40(Machine &m) {
    try_cure_status_40_core(m);
    m.ret();
}

// rom: 0x366A40 (near)
// Roll sleep recovery using item FDh's field 4.
void try_cure_status_80_core(Machine &m) {
    try_cure_actor_status(m, 0xFD, 0x7F, show_actor_awakened_message_core, show_actor_asleep_message_core);
}

void try_cure_status_80(Machine &m) {
    try_cure_status_80_core(m);
    m.ret();
}

namespace {
struct Text {
    uint16_t seg, off;
};
// Either a fixed segment:offset (a buffer like 0xD60F) or a text key resolved through text(). Queues the
// strings (the first starting a new line, BX = 0; the rest continuing it, BX = FFh).
struct TextItem {
    bool is_key;
    TextKey key;
    Text lit;
    constexpr TextItem(Text t) : is_key(false), key{}, lit(t) {}
    TextItem(TextKey k) : is_key(true), key(k), lit{} {}
};
void queue_texts(Machine &m, std::initializer_list<TextItem> items) {
    bool first = true;
    for (const TextItem &item : items) {
        if (item.is_key)
            queue_words(m, item.key, first ? 0 : 0xFF);
        else
            queue_words(m, item.lit.seg, item.lit.off, first ? 0 : 0xFF);
        first = false;
    }
}
constexpr Text kActorIcons{0, kBattleActorIcons}, kIconList{0, kBattleItemIcons},
    kNumber{0, kBattleNumberText};

// Party member AL's status icons into 0xD60F and the number 0xDC0B into 0xD66F.
void member_icons_and_amount(Machine &m) {
    m.set_ah(0);
    m.di() = kBattleActorIcons;
    build_status_icons_core(m);
    m.ds() = 0;
    m.ax() = m.rd16(m.ss(), kMeterInputTarget);
    format_number_core(m);
}

// "<member AL> <head> <stat> <prefix> <amount> <tail>": a stat went up by 0xDC0B.
void stat_growth_message(Machine &m, TextKey head, TextKey stat, TextKey prefix, TextKey tail) {
    SavedRegisters saved(m);
    member_icons_and_amount(m);
    queue_texts(m, {kActorIcons, head, stat, prefix, kNumber, tail});
    open_battle_popup(m);
    saved.restore(m);
}

// Member AL's icons, the amount and the icon list of ability 0xDBFC (AL = `al` for the list builder), then
// "<member> <a> <list> <c> <d>" in one popup.
void ability_message(Machine &m, bool actor_in_al, TextKey a, TextKey c, TextKey d) {
    SavedRegisters saved(m);
    member_icons_and_amount(m);
    m.ax() = 0;
    if (actor_in_al)
        m.set_al(m.rd8(m.ss(), kActingActor));
    m.set_ah(m.rd8(m.ss(), kActionId));
    set_dl(m, 1);
    build_icon_list_core(m);
    queue_texts(m, {kActorIcons, a, kIconList, c, d});
    open_battle_popup(m);
    saved.restore(m);
}
} // namespace

// rom: 0x36BD14 (near)
// Show the maximum HP growth amount for the selected member.
// Registers saved.
void show_max_hp_growth_message_core(Machine &m) {
    stat_growth_message(m, TextKey::show_max_hp_growth_message_1, TextKey::show_max_hp_growth_message_2,
                        TextKey::show_max_hp_growth_message_3, TextKey::show_max_hp_growth_message_4);
}

void show_max_hp_growth_message(Machine &m) {
    show_max_hp_growth_message_core(m);
    m.ret();
}

// rom: 0x36BDB4 (near)
// Show the strength growth amount for the selected member.
// Registers saved.
void show_stat_b_growth_message_core(Machine &m) {
    stat_growth_message(m, TextKey::show_stat_b_growth_message_1, TextKey::show_stat_b_growth_message_2,
                        TextKey::show_stat_b_growth_message_3, TextKey::show_stat_b_growth_message_4);
}

void show_stat_b_growth_message(Machine &m) {
    show_stat_b_growth_message_core(m);
    m.ret();
}

// rom: 0x36BE54 (near)
// Show the mana growth amount for the selected member.
// Registers saved.
void show_stat_e_growth_message_core(Machine &m) {
    stat_growth_message(m, TextKey::show_stat_e_growth_message_1, TextKey::show_stat_e_growth_message_2,
                        TextKey::show_stat_e_growth_message_3, TextKey::show_stat_e_growth_message_4);
}

void show_stat_e_growth_message(Machine &m) {
    show_stat_e_growth_message_core(m);
    m.ret();
}

// rom: 0x36BEF4 (near)
// Show the agility growth amount for the selected member.
// Registers saved.
void show_stat_d_growth_message_core(Machine &m) {
    stat_growth_message(m, TextKey::show_stat_d_growth_message_1, TextKey::show_stat_d_growth_message_2,
                        TextKey::show_stat_d_growth_message_3, TextKey::show_stat_d_growth_message_4);
}

void show_stat_d_growth_message(Machine &m) {
    show_stat_d_growth_message_core(m);
    m.ret();
}

// rom: 0x36BF94 (near)
// Show the defense growth amount for the selected member.
// Registers saved.
void show_stat_c_growth_message_core(Machine &m) {
    stat_growth_message(m, TextKey::show_stat_c_growth_message_1, TextKey::show_stat_c_growth_message_2,
                        TextKey::show_stat_c_growth_message_3, TextKey::show_stat_c_growth_message_4);
}

void show_stat_c_growth_message(Machine &m) {
    show_stat_c_growth_message_core(m);
    m.ret();
}

// rom: 0x36C034 (near)
// Show the ability gained by the selected member.
// Registers saved.
void show_ability_gained_message_core(Machine &m) {
    ability_message(m, false, TextKey::show_ability_gained_message_1, TextKey::show_ability_gained_message_2,
                    TextKey::show_ability_gained_message_3);
}

void show_ability_gained_message(Machine &m) {
    show_ability_gained_message_core(m);
    m.ret();
}

// rom: 0x36C1AD (near)
// Show that the selected ability gained uses.
// Registers saved.
void show_ability_uses_up_message_core(Machine &m) {
    ability_message(m, true, TextKey::show_ability_uses_up_message_1, TextKey::show_ability_uses_up_message_2,
                    TextKey::show_ability_uses_up_message_3);
}

void show_ability_uses_up_message(Machine &m) {
    show_ability_uses_up_message_core(m);
    m.ret();
}

// rom: 0x36C24C (near)
// Show that the selected ability lost uses.
// Registers saved.
void show_ability_uses_down_message_core(Machine &m) {
    ability_message(m, true, TextKey::show_ability_uses_down_message_1,
                    TextKey::show_ability_uses_down_message_2, TextKey::show_ability_uses_down_message_3);
}

void show_ability_uses_down_message(Machine &m) {
    show_ability_uses_down_message_core(m);
    m.ret();
}

// rom: 0x36C0CF (near)
// Show the old and new abilities when a member replaces an ability.
// Registers saved.
void show_ability_replaced_message_core(Machine &m) {
    JoinedBattlePopup joined(m, "show_ability_replaced_message");
    SavedRegisters saved(m);
    const uint16_t ax = m.ax();
    m.di() = kBattleActorIcons;
    build_status_icons_core(m);
    m.ds() = 0;
    m.ax() = uint16_t(ax >> 8);
    format_number_core(m);
    queue_texts(m, {kActorIcons, TextKey::show_ability_replaced_message_1});
    m.ax() = uint16_t(m.rd8(m.ss(), kActionId) << 8);
    set_dl(m, 1);
    build_icon_list_core(m);
    queue_words(m, 0, kBattleItemIcons, 0xFF);
    queue_words(m, TextKey::show_ability_replaced_message_2, 0xFF);
    open_battle_popup(m);
    m.ax() = uint16_t(m.rd8(m.ss(), kActionEffect) << 8);
    set_dl(m, 1);
    build_icon_list_core(m);
    queue_texts(
        m, {kIconList, TextKey::show_ability_replaced_message_3, TextKey::show_ability_replaced_message_4});
    open_battle_popup(m);
    saved.restore(m);
}

void show_ability_replaced_message(Machine &m) {
    show_ability_replaced_message_core(m);
    m.ret();
}

// rom: 0x3639D2 (near)
// Draw an ability index from 0..31; rolls 80h..90h map to index zero, favoring the first ability.
void random_ability_index_core(Machine &m) {
    m.ax() = 0x9000;
    random_range_core(m);
    m.ax() = m.al() < 0x80 ? uint16_t(m.al() >> 2) : 0;
}

void random_ability_index(Machine &m) {
    random_ability_index_core(m);
    m.ret();
}

namespace {
// L_363C58: Draw an ability absent from the first four slots, optionally excluding the replaced ability.
bool draw_new_ability(Machine &m, bool exclude_old_ability) {
    for (;;) {
        m.set_al(m.rd8(m.ss(), m.si()));
        m.wr8(m.ss(), kActionId, m.al());
        m.ax() = 0x8000;
        m.ds() = m.ax();
        random_ability_index_core(m);
        m.ax() = uint16_t(m.ax() << 1);
        m.di() = uint16_t(0x2FC5 + m.ax()); // D_382FC5
        m.set_al(m.rd8(m.ds(), m.di()));
        if (exclude_old_ability && m.rd8(m.ss(), kActionId) == m.al())
            continue;
        bool already_known = false;
        for (uint16_t ability_slot = 0; ability_slot < 4 && !already_known; ++ability_slot)
            already_known = m.rd8(m.ss(), uint16_t(m.dx() + ability_slot * 2)) == m.al();
        if (!already_known)
            break;
    }
    m.set_al(m.rd8(m.ds(), m.di()));
    auto &session = Session::of(m.wsc());
    const bool allowed = !exclude_old_ability ||
                         session.allow_esper_change(m, uint8_t(m.cx()), m.rd8(m.ss(), kActionId), m.al());
    if (allowed)
        m.wr8(m.ss(), m.si(), m.al());
    m.wr8(m.ss(), exclude_old_ability ? kActionEffect : kActionId, m.al());
    const uint16_t bx = m.bx();
    m.bx() = 3;
    lookup_table_byte_core(m);
    m.bx() = bx;
    if (allowed)
        m.wr8(m.ss(), uint16_t(m.si() + 1), m.al());
    return allowed;
}

// Roll a stat gain and apply it only if the result is at most 99 (modified gains clamp to 99).
void grow_member_stat(Machine &m, uint16_t stat_offset, uint16_t gain_range, void (*message)(Machine &)) {
    const uint16_t si = m.si();
    m.si() = uint16_t(m.si() + stat_offset);
    m.ax() = gain_range;
    random_range_core(m);
    set_bl(m, m.rd8(m.ss(), m.si()));
    const int32_t gain = hooks::filter(hooks::Value::StatGain, m, m.al(), {int32_t(stat_offset), 0});
    if (uint8_t(m.bx()) != 0x63) {
        m.dx() = uint8_t(m.bx());
        if (gain != m.al()) // a changed gain (Stat Growth) is capped at 99, not dropped
            set_bl(m, uint8_t(std::min<int32_t>(uint8_t(m.bx()) + gain, 0x63)));
        else
            set_bl(m, uint8_t(uint8_t(m.bx()) + m.al()));
        if (uint8_t(m.bx()) <= 0x63) {
            m.wr8(m.ss(), m.si(), uint8_t(m.bx()));
            m.bx() = uint8_t(uint8_t(m.bx()) - uint8_t(m.dx()));
            m.wr16(m.ss(), kMeterInputTarget, m.bx());
            m.set_al(uint8_t(m.cx()));
            message(m);
            m.wr8(m.ss(), kGrowthHappened, 1);
        }
    }
    m.si() = si;
}
} // namespace

// rom: 0x363B61 (near)
// Roll one growth outcome per living mutant: ability, max HP, stat, ability uses, or no change.
void grow_mutants_after_battle_core(Machine &m) {
    m.si() = kGameState;
    set_cl(m, 0);
    do {
        m.set_al(m.rd8(m.ss(), m.si()));
        if (m.al() & 3)
            goto next;
        m.set_al(m.rd8(m.ss(), uint16_t(m.si() + 0xA)));
        if (!(m.al() & 0x20))
            goto next;
        m.ax() = 0xFF00;
        random_range_core(m);
        // Growth thresholds: 23h ability, 45h HP, 63h magic, 75h agility, 87h strength, 8Fh defense, 93h
        // uses.
        if (m.al() < 0x23) {
            const uint16_t si = m.si();
            m.ax() = 0x300;
            random_range_core(m);
            m.si() = uint16_t(m.si() + 0xF);
            m.dx() = m.si();
            m.set_ah(0);
            m.set_al(uint8_t(m.al() << 1));
            m.si() = uint16_t(m.si() + m.ax());
            m.set_al(m.rd8(m.ss(), uint16_t(m.si() + 1)));
            bool empty = m.al() == 0xFF;
            if (!empty && m.al() == 0xFE) {
                m.set_al(m.rd8(m.ss(), m.si()));
                empty = m.al() == 0xFE;
            }
            const bool allowed = draw_new_ability(m, !empty);
            m.set_al(uint8_t(m.cx()));
            if (empty)
                show_ability_gained_message_core(m);
            else {
                // Keep the original message's scratch writes and frame schedule. A declined change
                // presents "No change" instead; only the slot writes and growth flag are suppressed.
                Session::of(m.wsc()).esper_declined = !allowed;
                show_ability_replaced_message_core(m);
                Session::of(m.wsc()).esper_declined = false;
            }
            if (allowed)
                m.wr8(m.ss(), kGrowthHappened, 1);
            m.si() = si;
        } else if (m.al() < 0x45) {
            const uint16_t si = m.si();
            m.si() = uint16_t(m.si() + 8);
            // Maximum HP rises by 10..15, capped at 999; the hook may change the rolled gain.
            m.ax() = 0xF0A;
            random_range_core(m);
            m.set_ah(0);
            m.ax() = uint16_t(
                std::clamp<int32_t>(hooks::filter(hooks::Value::StatGain, m, m.ax(), {8, 0}), 0, 999));
            m.bx() = m.rd16(m.ss(), m.si());
            if (m.bx() != 999) {
                m.dx() = m.bx();
                m.bx() = uint16_t(m.bx() + m.ax());
                if (m.bx() > 999)
                    m.bx() = 999;
                m.wr16(m.ss(), m.si(), m.bx());
                m.bx() = uint16_t(m.bx() - m.dx());
                m.wr16(m.ss(), kMeterInputTarget, m.bx());
                m.set_al(uint8_t(m.cx()));
                show_max_hp_growth_message_core(m);
                m.wr8(m.ss(), kGrowthHappened, 1);
            }
            m.si() = si;
        } else if (m.al() < 0x63) {
            grow_member_stat(m, 0xE, 0x501, show_stat_e_growth_message_core);
        } else if (m.al() < 0x75) {
            grow_member_stat(m, 0xD, 0x301, show_stat_d_growth_message_core);
        } else if (m.al() < 0x87) {
            grow_member_stat(m, 0xB, 0x501, show_stat_b_growth_message_core);
        } else if (m.al() < 0x8F) {
            grow_member_stat(m, 0xC, 0x501, show_stat_c_growth_message_core);
        } else if (m.al() < 0x93) {
            const uint16_t si = m.si();
            m.si() = uint16_t(m.si() + 0xF);
            set_ch(m, 0);
            do {
                m.set_ah(m.rd8(m.ss(), m.si()));
                m.set_al(m.rd8(m.ss(), uint16_t(m.si() + 1)));
                m.wr8(m.ss(), kAbilityUsesBefore, m.al());
                if (m.al() == 0xFE || m.al() == 0xFF)
                    goto next_item;
                m.bx() = 0;
                // Original quirk: eligibility lookup uses the old uses byte as an item ID.
                lookup_table_byte_core(m);
                if (!(m.al() & 1))
                    goto next_item;
                m.ax() = 0x300;
                random_range_core(m);
                {
                    const uint8_t roll = m.al();
                    uint8_t uses;
                    // Uses outcome: halve (minimum 1), add random maximum uses, subtract it, or reset to
                    // maximum.
                    if (roll == 0) {
                        m.set_al(m.rd8(m.ss(), kAbilityUsesBefore));
                        if (m.al() == 0)
                            goto next_item;
                        uses = uint8_t(m.al() >> 1);
                        if (uses == 0)
                            uses = 1;
                    } else if (roll == 3) {
                        m.set_al(m.rd8(m.ss(), m.si()));
                        m.bx() = 3;
                        lookup_table_byte_core(m);
                        uses = m.al();
                    } else {
                        m.set_al(m.rd8(m.ss(), m.si()));
                        m.bx() = 3;
                        lookup_table_byte_core(m);
                        m.set_ah(m.al());
                        m.set_al(0);
                        random_range_core(m);
                        m.set_ah(m.al());
                        m.set_al(m.rd8(m.ss(), kAbilityUsesBefore));
                        if (roll == 1) {
                            if (m.al() >= 0x63)
                                goto next_item;
                            uses = uint8_t(m.al() + m.ah());
                            if (uses >= 0x63)
                                uses = 0x63;
                        } else {
                            if (m.al() == 0)
                                goto next_item;
                            uses = m.al() < m.ah() ? 0 : uint8_t(m.al() - m.ah());
                        }
                    }
                    m.set_al(uses);
                    m.wr8(m.ss(), kAbilityUsesAfter, uses);
                    m.wr8(m.ss(), uint16_t(m.si() + 1), uses);
                }
                m.wr8(m.ss(), kGrowthHappened, 1);
                m.set_al(m.rd8(m.ss(), m.si()));
                m.wr8(m.ss(), kActionId, m.al());
                m.set_al(uint8_t(m.cx()));
                m.bx() = uint16_t(m.rd8(m.ss(), kAbilityUsesAfter) << 8 | m.rd8(m.ss(), kAbilityUsesBefore));
                if (uint8_t(m.bx()) != uint8_t(m.bx() >> 8)) {
                    if (uint8_t(m.bx()) >= uint8_t(m.bx() >> 8))
                        show_ability_uses_down_message_core(m);
                    else
                        show_ability_uses_up_message_core(m);
                }
            next_item:
                set_ch(m, uint8_t((m.cx() >> 8) + 1));
                m.si() = uint16_t(m.si() + 2);
            } while ((m.cx() >> 8) != 4);
            m.si() = si;
        }
    next:
        m.si() = uint16_t(m.si() + 0x1F);
        set_cl(m, uint8_t(uint8_t(m.cx()) + 1));
    } while (uint8_t(m.cx()) != 4);
}

void grow_mutants_after_battle(Machine &m) {
    grow_mutants_after_battle_core(m);
    m.ret();
}

// rom: 0x3640A7 (near)
// Swap acting actor and target, their action/effect IDs, and the target's resolved group.
void swap_actor_and_target_core(Machine &m) {
    const uint16_t si = m.si(), cx = m.cx();
    set_cl(m, m.rd8(m.ss(), kActingActor));
    set_ch(m, m.rd8(m.ss(), kActionTarget));
    m.wr8(m.ss(), kActingActor, uint8_t(m.cx() >> 8));
    m.wr8(m.ss(), kActionTarget, uint8_t(m.cx()));
    m.set_al(uint8_t(m.cx()));
    battle_actor_pointer_core(m);
    m.set_al(m.rd8(m.ss(), uint16_t(m.si() + 0x1F)));
    party_slot_for_name_id_core(m);
    m.wr8(m.ss(), kTargetGroup, m.al());
    set_cl(m, m.rd8(m.ss(), kActionId));
    set_ch(m, m.rd8(m.ss(), kActionEffect));
    m.wr8(m.ss(), kActionId, uint8_t(m.cx() >> 8));
    m.wr8(m.ss(), kActionEffect, uint8_t(m.cx()));
    m.cx() = cx;
    m.si() = si;
}

void swap_actor_and_target(Machine &m) {
    swap_actor_and_target_core(m);
    m.ret();
}

// rom: 0x36AFF9 (near)
// Shows "<target> D_383068 <item 0xDBFD's icons> D_383178".
// Every register is saved around it.
void show_target_item_message_core(Machine &m) {
    SavedRegisters saved(m);
    build_battle_member_display_core(m);
    m.ax() = uint16_t(m.rd8(m.ss(), kActionEffect) << 8 | m.rd8(m.ss(), kActingActor));
    set_dl(m, 1);
    build_icon_list_core(m);
    queue_texts(m, {Text{0, kBattleTargetIcons}, TextKey::show_target_item_message_1, kIconList,
                    TextKey::show_target_item_message_2});
    open_battle_popup(m);
    saved.restore(m);
}

void show_target_item_message(Machine &m) {
    show_target_item_message_core(m);
    m.ret();
}

// rom: 0x36A053 (near)
// Show the actor and attempted item, followed by the no-effect message.
// Every register is saved around it.
void show_actor_item_failed_message_core(Machine &m) {
    JoinedBattlePopup joined(m, "show_actor_item_failed_message");
    SavedRegisters saved(m);
    build_battle_member_name_or_icons_core(m);
    m.ax() = uint16_t(m.rd8(m.ss(), kActionEffect) << 8);
    set_dl(m, 1);
    build_icon_list_core(m);
    queue_texts(m, {kActorIcons, TextKey::show_actor_item_failed_message_1, kIconList,
                    TextKey::show_actor_item_failed_message_2});
    open_battle_popup(m);
    queue_words(m, TextKey::show_actor_item_failed_message_3, 0);
    open_battle_popup(m);
    saved.restore(m);
}

void show_actor_item_failed_message(Machine &m) {
    show_actor_item_failed_message_core(m);
    m.ret();
}

// rom: 0x36A969 (near)
// Show the complete target attack announcement in one popup.
// Every register is saved.
void show_target_attack_message_core(Machine &m) {
    JoinedBattlePopup joined(m, "show_target_attack_message");
    SavedRegisters saved(m);
    build_battle_member_display_core(m);
    queue_texts(m, {TextKey::show_target_attack_message_1, TextKey::show_target_attack_message_2,
                    Text{0, kBattleTargetIcons}, TextKey::show_target_attack_message_3});
    open_battle_popup(m);
    queue_words(m, TextKey::show_target_attack_message_4, 0);
    open_battle_popup(m);
    saved.restore(m);
}

void show_target_attack_message(Machine &m) {
    show_target_attack_message_core(m);
    m.ret();
}

// rom: 0x36B1BA (near)
// Shows D_383288 (no effect).
// Every register is saved around it.
void show_no_effect_message_core(Machine &m) {
    SavedRegisters saved(m);
    queue_words(m, TextKey::show_no_effect_message_1, 0);
    open_battle_popup(m);
    saved.restore(m);
}

void show_no_effect_message_call_core(Machine &m) { show_no_effect_message_core(m); }
void show_no_effect_message(Machine &m) {
    show_no_effect_message_call_core(m);
    m.ret();
}

namespace {
// Target name, optional group ordinal, and status result in one popup. Registers are saved.
void target_status_message(Machine &m, TextKey text, bool two_popups) {
    JoinedBattlePopup joined(
        m, two_popups ? std::string_view(kTextKeys[size_t(text)].name)
                            .substr(0, std::string_view(kTextKeys[size_t(text)].name).size() - 2)
                      : std::string_view{});
    SavedRegisters saved(m);
    build_battle_member_display_core(m);
    queue_words(m, 0, kBattleTargetIcons, 0);
    append_target_group_suffix_core(m); // append_target_group_suffix: no core
    if (two_popups) {
        open_battle_popup(m);
        queue_words(m, text, 0);
    } else {
        queue_words(m, text, 0xFF);
    }
    open_battle_popup(m);
    saved.restore(m);
}
} // namespace

// rom: 0x36B48B (near)
// Show that the target died.
// Every register is saved around it.
void show_status_bit0_message_core(Machine &m) {
    target_status_message(m, TextKey::show_status_bit0_message_1, false);
}

void show_status_bit0_message(Machine &m) {
    show_status_bit0_message_core(m);
    m.ret();
}

// rom: 0x36B4D0 (near)
// Show that the target turned to stone.
// Every register is saved around it.
void show_status_bit1_message_core(Machine &m) {
    target_status_message(m, TextKey::show_status_bit1_message_1, true);
}

void show_status_bit1_message(Machine &m) {
    show_status_bit1_message_core(m);
    m.ret();
}

// rom: 0x36B522 (near)
// Show that the target is cursed.
// Every register is saved around it.
void show_status_bit2_message_core(Machine &m) {
    target_status_message(m, TextKey::show_status_bit2_message_1, false);
}

void show_status_bit2_message(Machine &m) {
    show_status_bit2_message_core(m);
    m.ret();
}

// rom: 0x36B567 (near)
// Show that the target is blinded.
// Every register is saved around it.
void show_status_bit3_message_core(Machine &m) {
    target_status_message(m, TextKey::show_status_bit3_message_1, true);
}

void show_status_bit3_message(Machine &m) {
    show_status_bit3_message_core(m);
    m.ret();
}

// rom: 0x36B5B9 (near)
// Show that the target is poisoned.
// Every register is saved around it.
void show_status_bit4_message_core(Machine &m) {
    target_status_message(m, TextKey::show_status_bit4_message_1, false);
}

void show_status_bit4_message(Machine &m) {
    show_status_bit4_message_core(m);
    m.ret();
}

// rom: 0x36B681 (near)
// Show that the target is confused.
// Every register is saved around it.
void show_status_bit5_message_core(Machine &m) {
    target_status_message(m, TextKey::show_status_bit5_message_1, false);
}

void show_status_bit5_message(Machine &m) {
    show_status_bit5_message_core(m);
    m.ret();
}

// rom: 0x36B6C6 (near)
// Show that the target is paralyzed.
// Every register is saved around it.
void show_status_bit6_message_core(Machine &m) {
    target_status_message(m, TextKey::show_status_bit6_message_1, false);
}

void show_status_bit6_message(Machine &m) {
    show_status_bit6_message_core(m);
    m.ret();
}

// rom: 0x36B70B (near)
// Show that the target is asleep.
// Every register is saved around it.
void show_status_bit7_message_core(Machine &m) {
    target_status_message(m, TextKey::show_status_bit7_message_1, false);
}

void show_status_bit7_message(Machine &m) {
    show_status_bit7_message_core(m);
    m.ret();
}

// rom: 0x36BC84 (near)
// Shows the message for the lowest set status bit of AL (none: nothing).
void show_status_inflicted_message_core(Machine &m) {
    static void (*const kMessages[8])(Machine &) = {
        show_status_bit0_message_core, show_status_bit1_message_core, show_status_bit2_message_core,
        show_status_bit3_message_core, show_status_bit4_message_core, show_status_bit5_message_core,
        show_status_bit6_message_core, show_status_bit7_message_core};
    for (int bit = 0; bit < 8; ++bit)
        if (m.al() & (1 << bit)) {
            kMessages[bit](m);
            return;
        }
}

void show_status_inflicted_message(Machine &m) {
    show_status_inflicted_message_core(m);
    m.ret();
}

namespace {
// The redraw after a status change: skill list (registers saved), member panels, scroll reset, sprite
// table and palettes, one frame.
void redraw_battle_after_status(Machine &m) {
    {
        SavedRegisters saved(m);
        set_dl(m, 0xF);
        draw_battle_skill_list_core(m);
        saved.restore(m);
    }
    draw_battle_member_panels_core(m);
    m.set_al(0);
    m.out8(5, m.al());
    m.out8(6, m.al());
    dma_sprite_table_core(m);
    dma_palettes_core(m);
    m.call_far(0x4000, 0x0009); // wait_vblank
}
} // namespace

// rom: 0x366F2A (near)
// Swap actor and target and apply twice the pending damage through the HP meter.
void resolve_drain_hit_core(Machine &m) {
    // The original doubles the pending amount before reversing actor/target; retain its signed meter
    // behavior.
    m.dx() = uint16_t(m.rd16(m.ss(), kMeterInputTarget) * 2);
    show_target_item_message_core(m);
    swap_actor_and_target_core(m);
    m.set_al(m.rd8(m.ss(), kActionTarget));
    if (m.al() >= 4) {
        m.set_al(m.rd8(m.ss(), kActionId));
        if (m.al() == 0x33) {
            m.wr8(m.ss(), kEffectPaletteVariant, 4);
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0x7000;
                play_hit_sprite_effect_core(m);
                m.cs() = caller_cs;
            } // not decompiled
        } else if (m.al() == 0x79) {
            m.wr8(m.ss(), kEffectPaletteVariant, 3);
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0x7000;
                play_impact_sprite_effect_core(m);
                m.cs() = caller_cs;
            } // not decompiled
        }
    }
    set_battle_meter_target_core(m);
    open_party_member_popup_core(m);
    m.wr8(m.ss(), kBattleKnockoutMode, 1);
    check_target_knocked_out_core(m);
    m.wr8(m.ss(), kBattleKnockoutMode, 0);
    swap_actor_and_target_core(m);
}

void resolve_drain_hit_call_core(Machine &m) { resolve_drain_hit_core(m); }
void resolve_drain_hit(Machine &m) {
    resolve_drain_hit_call_core(m);
    m.ret();
}

// rom: 0x366FFD (near)
// Apply a follow-up status after immunity/resistance checks, counting death or petrification.
void resolve_status_hit_core(Machine &m) {
    show_target_attack_message_core(m);
    m.set_al(m.rd8(m.ss(), kActionEffect));
    m.bx() = 5;
    lookup_table_byte_core(m);
    set_cl(m, m.al());
    m.set_al(m.rd8(m.ss(), kActingActor));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0xB)) & uint8_t(m.cx())));
    if (m.al() != 0) {
        // Original quirk: this failure still swaps actor/target even though no prior swap occurred.
        show_actor_item_failed_message_core(m);
        swap_actor_and_target_core(m);
        return;
    }
    m.set_al(m.rd8(m.ss(), kActingActor));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0xC)) & uint8_t(m.cx())));
    if (m.al() == 0) {
        m.dx() = 0;
    } else {
        // Resistance threshold = actor magic + 50 - target AX; AH retains the item pointer's high byte.
        m.dx() = uint16_t(m.rd8(m.ss(), uint16_t(m.si() + 0xA)) + 0x32);
        m.set_al(m.rd8(m.ss(), kActionTarget));
        battle_actor_pointer_core(m);
        m.set_al(m.rd8(m.ss(), uint16_t(m.si() + 0xA)));
        m.dx() = uint16_t(m.dx() - m.ax());
    }
    m.ax() = 0x6400;
    random_range_core(m);
    m.set_ah(0);
    // Status succeeds only when the signed threshold is strictly below the 0..100 roll.
    if (!(int16_t(m.dx()) < int16_t(m.ax()))) {
        swap_actor_and_target_core(m);
        return;
    }
    swap_actor_and_target_core(m);
    m.set_al(m.rd8(m.ss(), kActionTarget));
    if (m.al() >= 4) {
        m.set_al(m.rd8(m.ss(), kActionId));
        if (m.al() == 0xCD) {
            m.wr8(m.ss(), kEffectPaletteVariant, 6);
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0x7000;
                play_formation_palette_effect_core(m);
                m.cs() = caller_cs;
            } // not decompiled
        } else if (m.al() == 0xCE) {
            m.wr8(m.ss(), kEffectPaletteVariant, 1);
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0x7000;
                play_battle_effect_6_core(m);
                m.cs() = caller_cs;
            } // not decompiled
        } else if (m.al() == 0xCF) {
            m.wr8(m.ss(), kEffectPaletteVariant, 5);
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0x7000;
                play_battle_effect_7_core(m);
                m.cs() = caller_cs;
            } // not decompiled
        }
    }
    m.set_al(m.rd8(m.ss(), kActionTarget));
    battle_actor_pointer_core(m);
    m.set_al(m.rd8(m.ss(), uint16_t(m.si() + 2)));
    set_dl(m, m.al());
    m.set_al(m.rd8(m.ss(), kActionId));
    m.bx() = 4;
    lookup_table_byte_core(m);
    set_dh(m, m.al());
    if ((uint8_t(m.dx() >> 8) & uint8_t(m.dx())) ||
        !hooks::should(hooks::VB::InflictStatus, m, true,
                       {m.rd8(m.ss(), kActionTarget), uint8_t(m.dx() >> 8)})) {
        show_no_effect_message_core(m);
        swap_actor_and_target_core(m);
        return;
    }
    set_dl(m, uint8_t(uint8_t(m.dx()) | m.al()));
    BattleActorState(m, m.si()).write_status(uint8_t(m.dx()));
    m.set_al(uint8_t(m.dx() >> 8));
    show_status_inflicted_message_core(m);
    const uint8_t added = uint8_t(m.dx() >> 8);
    if (added & 3) {
        if (added & 1) {
            m.ax() = 0;
            BattleActorState(m, m.si()).write_hp(0);
        }
        m.set_al(uint8_t(m.rd8(m.ss(), kBattleStatusSuccessCount) + 1));
        m.wr8(m.ss(), kBattleStatusSuccessCount, m.al());
    }
    m.set_al(m.rd8(m.ss(), kActionTarget));
    if (m.al() >= 4)
        remove_defeated_enemy_group_core(m);
    else
        shake_screen_for_party_hit_core(m);
    redraw_battle_after_status(m);
    swap_actor_and_target_core(m);
}

void resolve_status_hit_call_core(Machine &m) { resolve_status_hit_core(m); }
void resolve_status_hit(Machine &m) {
    resolve_status_hit_call_core(m);
    m.ret();
}

// rom: 0x367129 (near)
// Apply item-field-4 damage, randomized unless the actor's field-0Ch mask matches the item.
void resolve_fixed_damage_hit_core(Machine &m) {
    show_target_attack_message_core(m);
    m.set_al(m.rd8(m.ss(), kActionEffect));
    set_dh(m, m.al());
    m.bx() = 5;
    lookup_table_byte_core(m);
    set_cl(m, m.al());
    m.set_al(m.rd8(m.ss(), kActingActor));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0xB)) & uint8_t(m.cx())));
    if (m.al() != 0) {
        show_actor_item_failed_message_core(m);
        return;
    }
    m.set_al(m.rd8(m.ss(), kActionEffect));
    m.bx() = 4;
    lookup_table_byte_core(m);
    set_dh(m, m.al());
    m.set_al(m.rd8(m.ss(), kActingActor));
    battle_actor_pointer_core(m);
    m.set_al(uint8_t(m.rd8(m.ss(), uint16_t(m.si() + 0xC)) & uint8_t(m.cx())));
    // Matching field-0Ch bits keep full power; otherwise roll 0..power (inclusive).
    if (m.al() == 0) {
        m.set_ah(uint8_t(m.dx() >> 8));
        m.set_al(0);
        random_range_core(m);
    } else {
        m.set_al(uint8_t(m.dx() >> 8));
    }
    m.dx() = m.al();
    m.wr16(m.ss(), kMeterInputTarget, m.dx());
    swap_actor_and_target_core(m);
    m.set_al(m.rd8(m.ss(), kActionTarget));
    if (m.al() >= 4) {
        m.set_al(m.rd8(m.ss(), kActionId));
        if (m.al() == 0xD0) {
            m.wr8(m.ss(), kEffectPaletteVariant, 3);
            {
                const uint16_t caller_cs = m.cs();
                m.cs() = 0x7000;
                play_battle_effect_7_core(m);
                m.cs() = caller_cs;
            } // not decompiled
        }
    }
    open_party_member_popup_core(m);
    set_battle_meter_target_core(m);
    m.wr8(m.ss(), kBattleKnockoutMode, 1);
    check_target_knocked_out_core(m);
    m.wr8(m.ss(), kBattleKnockoutMode, 0);
    swap_actor_and_target_core(m);
}

void resolve_fixed_damage_hit_call_core(Machine &m) { resolve_fixed_damage_hit_core(m); }
void resolve_fixed_damage_hit(Machine &m) {
    resolve_fixed_damage_hit_call_core(m);
    m.ret();
}

// rom: 0x364A06 (near)
// Toggles the meat menu's cursor (0xDD75 ^= 1): shown (0) puts sprites 0-3 at the first choice (X 50h/58h,
// Y 50h/58h), hidden (1) hides them (90h). AX = the last word written.
void toggle_meat_menu_cursor_core(Machine &m) {
    m.set_al(uint8_t(m.rd8(m.ss(), kCursorHidden) ^ 1));
    m.wr8(m.ss(), kCursorHidden, m.al());
    static constexpr uint16_t kShown[4] = {0x5050, 0x5850, 0x5058, 0x5858};
    const bool shown = m.al() == 0;
    for (uint16_t k = 0; k < 4; ++k) {
        m.ax() = shown ? kShown[k] : 0x90;
        m.wr16(m.ss(), uint16_t(kSpritePositions + k * 4), m.ax());
    }
}

void toggle_meat_menu_cursor(Machine &m) {
    toggle_meat_menu_cursor_core(m);
    m.ret();
}

// rom: 0x36C2EB (near)
// Show that the party surprised the enemies.
// Every register is saved.
void show_party_surprise_message_core(Machine &m) {
    SavedRegisters saved(m);
    m.set_al(m.rd8(m.ss(), kDialogueLeader));
    m.di() = kBattleActorIcons;
    build_status_icons_core(m);
    queue_texts(m, {kActorIcons, TextKey::show_party_surprise_message_1});
    open_battle_popup(m);
    saved.restore(m);
}

void show_party_surprise_message(Machine &m) {
    show_party_surprise_message_core(m);
    m.ret();
}

// rom: 0x36C333 (near)
// Show that the enemies surprised the party.
// Every register is saved around it.
void show_enemy_surprise_message_core(Machine &m) {
    SavedRegisters saved(m);
    queue_words(m, TextKey::show_enemy_surprise_message_1, 0);
    open_battle_popup(m);
    saved.restore(m);
}

void show_enemy_surprise_message(Machine &m) {
    show_enemy_surprise_message_core(m);
    m.ret();
}

} // namespace sawgod::game
